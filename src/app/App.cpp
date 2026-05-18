#include "App.h"

#include <QFileInfo>
#include <QDateTime>
#include <QJsonObject>
#include <QHostAddress>
#include <QStandardPaths>
#include <QDir>
#include <QImageWriter>

#include <utility>

#include "network/Protocol.h"
#include "model/AppSettings.h"
#include "model/OutboxStore.h"
#include "utils/FileUtils.h"
#include "utils/ImageUtils.h"

// Normalise IPv4-mapped IPv6 ("::ffff:1.2.3.4") to plain IPv4 string.
static QString normaliseIp(const QString &raw) {
    const QHostAddress ha(raw);
    bool ok = false;
    const quint32 v4 = ha.toIPv4Address(&ok);
    return ok ? QHostAddress(v4).toString() : raw;
}

static constexpr quint16 kDataPort = Protocol::DataPort;
static constexpr quint16 kChatPort = Protocol::ChatPort;

// ── Construction ──────────────────────────────────────────────────────────────

App::App(QObject *parent)
    : QObject(parent)
    , m_discovery(new Discovery(kDataPort, AppSettings{}.selfId(),
                                AppSettings{}.fingerprint(), this))
    , m_server(new TransferServer(this))
    , m_chatServer(new ChatServer(this))
    , m_chatClient(new ChatClient(this))
    , m_store(new ChatStore(AppSettings{}.dataDir(), this))
    , m_peerStore(new PeerStore(AppSettings{}.dataDir(), this))
    , m_outbox(new OutboxStore(AppSettings{}.dataDir(), this))
{
    connect(m_discovery, &Discovery::peersChanged, this, [this] {
        const QList<Peer> currentPeers = m_discovery->peers();

        // Build new online set; detect dropouts for session-only history cleanup.
        QSet<QString> nowOnline;
        for (const Peer &p : currentPeers)
            nowOnline.insert(p.id);

        for (const QString &id : m_onlinePeerIds) {
            if (!nowOnline.contains(id)) {
                m_store->clearSession(id);     // no-op if strategy is Persistent
                m_fingerprintPrompted.remove(id); // re-prompt if they reconnect
            }
        }
        m_onlinePeerIds = nowOnline;

        // Refresh saved peer metadata and flush outbox for any newly online peer.
        for (const Peer &p : currentPeers) {
            m_peerStore->updatePeerInfo(p.id, p.displayName, p.address, p.port);
            flushOutbox(p);
        }

        // Fingerprint verification: check each online peer against known_peers.
        // Only verified when the peer broadcasts a fingerprint (fp= field present).
        // m_fingerprintPrompted prevents re-emitting every 5 s for the same peer.
        for (const Peer &p : currentPeers) {
            if (p.fingerprint.isEmpty()) continue;  // old client — skip
            if (m_fingerprintPrompted.contains(p.id)) continue;
            const QString known = m_peerStore->knownFingerprint(p.id);
            if (known.isEmpty()) {
                m_fingerprintPrompted.insert(p.id);
                emit unknownPeerFingerprint(p);
            } else if (known != p.fingerprint) {
                m_fingerprintPrompted.insert(p.id);
                emit fingerprintMismatch(p, known, p.fingerprint);
            }
        }

        // Retry frames that arrived before discovery resolved the sender's peerId.
        if (!m_deferredFrames.isEmpty()) {
            const QList<DeferredFrame> toRetry = std::exchange(m_deferredFrames, {});
            for (const auto &f : toRetry)
                routeIncoming(f.peerIdOrAddr, f.isPeerId, f.type, f.msgId, f.payload);
        }

        emit peersChanged();
    });

    // ── File-data server signals (inbound) ───────────────────────────────────
    // Single TCP connection carries all files for one Accept click.
    // transferStarted fires once per file (including dirs), all on the same tid.
    // transferBatchFinished fires once when eos is received — that is when we
    // mark the message Done and release the entry.
    connect(m_server, &TransferServer::transferStarted,
            this, [this](quintptr tid, const QString &, const QString &, qint64, qint64 transferMsgId) {
                if (m_inboundIdMap.contains(tid)) return; // already mapped for this connection

                InboundEntry *entry = nullptr;
                if (transferMsgId >= 0) {
                    // Modern sender: match by the msgId it declared in the connection header.
                    for (int i = 0; i < m_pendingInbound.size(); ++i) {
                        if (m_pendingInbound[i]->msgId == transferMsgId) {
                            entry = m_pendingInbound.takeAt(i);
                            break;
                        }
                    }
                }
                if (!entry) {
                    // Old sender (no msgId) or msgId not found: fall back to FIFO.
                    if (m_pendingInbound.isEmpty()) return;
                    entry = m_pendingInbound.takeFirst();
                }
                m_inboundIdMap.insert(tid, entry);
            });

    connect(m_server, &TransferServer::transferProgress,
            this, [this](quintptr tid, qint64 recv, qint64 total) {
                InboundEntry *entry = m_inboundIdMap.value(tid, nullptr);
                if (!entry) return;
                Message upd;
                upd.id               = entry->msgId;
                upd.peerId           = entry->peerId;
                upd.status           = MessageStatus::Transferring;
                upd.bytesTransferred = recv;
                upd.fileSize         = total;
                m_store->updateMessage(upd);
                emit messageUpdated(upd);
            });

    // One file in the batch finished — update filePath but keep entry alive.
    connect(m_server, &TransferServer::transferFileFinished,
            this, [this](quintptr tid, const QString &path) {
                InboundEntry *entry = m_inboundIdMap.value(tid, nullptr);
                if (!entry) { emit transferFinished(path); return; }
                // For folder transfers, skip individual file updates — the root folder
                // path is already known (set at accept time) and will be stamped at
                // batch completion. Writing single-file paths here would corrupt filePath.
                if (entry->isFolder) return;
                Message upd;
                upd.id       = entry->msgId;
                upd.peerId   = entry->peerId;
                upd.status   = MessageStatus::Transferring;
                upd.filePath = path;
                m_store->updateMessage(upd);
                emit messageUpdated(upd);
            });

    // eos received — entire Accept is complete.
    connect(m_server, &TransferServer::transferBatchFinished,
            this, [this](quintptr tid) {
                InboundEntry *entry = m_inboundIdMap.take(tid);
                if (!entry) return;
                Message upd;
                upd.id       = entry->msgId;
                upd.peerId   = entry->peerId;
                upd.status   = MessageStatus::Done;
                // For folder transfers, compute the root path deterministically:
                // downloadDir (where TransferServer placed files) + folder display name.
                if (entry->isFolder && !entry->folderName.isEmpty()) {
                    upd.filePath = m_server->downloadDir()
                                 + QDir::separator() + entry->folderName;
                }
                m_store->updateMessage(upd);
                emit messageUpdated(upd);
                emit transferFinished({});
                delete entry;
            });

    connect(m_server, &TransferServer::transferFailed,
            this, [this](quintptr tid, const QString &reason) {
                InboundEntry *entry = m_inboundIdMap.take(tid);
                if (entry) {
                    // Entry may still be in pending if transferStarted never fired
                    // (e.g. disk-space rejection before the first file header).
                    m_pendingInbound.removeOne(entry);
                    delete entry;
                }
                emit transferFailed(reason);
            });

    // ── Chat channel: frames arrive from either server or client socket ───────
    // ChatServer frames carry the peer's self-declared ID (from Hello handshake).
    connect(m_chatServer, &ChatServer::frameReceived,
            this, [this](const QString &peerId,
                          Protocol::MessageType type,
                          qint64 msgId,
                          QJsonObject payload) {
                routeIncoming(peerId, /*isPeerId=*/true, type, msgId, payload);
            });

    // ChatClient frames carry the peer's IP address (we initiated the connection).
    connect(m_chatClient, &ChatClient::frameReceived,
            this, [this](const QString &addr,
                          Protocol::MessageType type,
                          qint64 msgId,
                          QJsonObject payload) {
                routeIncoming(addr, /*isPeerId=*/false, type, msgId, payload);
            });
}

App::~App() {
    // Entries are owned by m_inboundIdMap once dequeued from m_pendingInbound.
    // Clean up both sides; use a set to avoid double-delete.
    QSet<InboundEntry *> owned;
    for (auto *e : m_inboundIdMap) owned.insert(e);
    for (auto *e : m_pendingInbound) owned.insert(e);
    qDeleteAll(owned);
}

// ── Start ─────────────────────────────────────────────────────────────────────

void App::start() {
    // Load persisted user preferences before starting network services.
    const AppSettings settings;
    m_discovery->setDisplayName(settings.displayName());
    const QString savedDir = settings.downloadDir();
    if (!savedDir.isEmpty())
        m_server->setDownloadDir(savedDir);

    m_server->listen(kDataPort);
    m_chatServer->listen(kChatPort);
    m_discovery->start();
}

// ── Queries ───────────────────────────────────────────────────────────────────

QList<Peer> App::peers() const {
    return m_discovery->peers();
}

QList<Message> App::messages(const QString &peerId) const {
    return m_store->messages(peerId);
}

ChatStore *App::chatStore() const {
    return m_store;
}

// ── Helpers ───────────────────────────────────────────────────────────────────

Peer App::peerById(const QString &peerId) const {
    for (const auto &p : m_discovery->peers())
        if (p.id == peerId) return p;
    return {};
}

// ── Outbound actions ──────────────────────────────────────────────────────────

void App::sendMessage(const QString &peerId, const QString &text) {
    Message msg;
    msg.peerId    = peerId;
    msg.type      = MessageType::Text;
    msg.outgoing  = true;
    msg.text      = text;
    msg.timestamp = QDateTime::currentDateTime();

    const Peer peer = peerById(peerId);
    if (peer.id.isEmpty()) {
        // Peer is offline — queue if saved.
        if (m_peerStore->isSaved(peerId)) {
            msg.status = MessageStatus::Pending;
            msg.id     = m_store->addMessage(msg);
            emit messageAdded(msg);
            QJsonObject payload;
            payload[QStringLiteral("text")] = text;
            m_outbox->enqueue(peerId, Protocol::MessageType::ChatMessage, payload);
        }
        return;
    }

    msg.status = MessageStatus::Done;
    msg.id     = m_store->addMessage(msg);
    emit messageAdded(msg);

    QJsonObject payload;
    payload[QStringLiteral("text")] = text;
    m_chatClient->send(peer.address, kChatPort,
                       Protocol::MessageType::ChatMessage, msg.id, payload);
}

void App::sendFile(const QString &peerId, const QString &filePath) {
    if (filePath.isEmpty()) return;

    const QFileInfo fi(filePath);
    Message msg;
    msg.peerId    = peerId;
    msg.type      = MessageType::File;
    msg.outgoing  = true;
    msg.fileName  = fi.fileName();
    msg.filePath  = filePath;
    msg.fileSize  = fi.size();
    msg.timestamp = QDateTime::currentDateTime();

    const Peer peer = peerById(peerId);
    if (peer.id.isEmpty()) {
        if (m_peerStore->isSaved(peerId)) {
            msg.status = MessageStatus::Pending;
            msg.id     = m_store->addMessage(msg);
            emit messageAdded(msg);
            QJsonObject payload;
            payload[QStringLiteral("name")]      = fi.fileName();
            payload[QStringLiteral("size")]      = fi.size();
            payload[QStringLiteral("localPath")] = filePath;
            payload[QStringLiteral("localMsgId")] = msg.id;
            m_outbox->enqueue(peerId, Protocol::MessageType::FileRequest, payload);
        }
        return;
    }

    msg.status = MessageStatus::WaitingAccept;
    msg.id     = m_store->addMessage(msg);
    emit messageAdded(msg);

    m_pendingOffers.insert(msg.id, {peerId, filePath});

    QJsonObject payload;
    payload[QStringLiteral("name")] = fi.fileName();
    payload[QStringLiteral("size")] = fi.size();
    m_chatClient->send(peer.address, kChatPort,
                       Protocol::MessageType::FileRequest, msg.id, payload);
}

void App::sendFolder(const QString &peerId, const QString &folderPath) {
    if (folderPath.isEmpty()) return;

    const QFileInfo fi(folderPath);
    const qint64 totalSz = FileUtils::totalSize(FileUtils::listFiles(folderPath));

    // Serialize the directory structure before persisting so the snapshot is stored in DB.
    QJsonArray treeArr;
    for (const FileUtils::FileEntry &e : FileUtils::listFiles(folderPath)) {
        QJsonObject o;
        o[QStringLiteral("p")] = e.relPath.mid(fi.fileName().size() + 1); // strip "rootName/"
        o[QStringLiteral("d")] = e.isDir;
        o[QStringLiteral("s")] = static_cast<double>(e.size);
        treeArr.append(o);
    }

    Message msg;
    msg.peerId     = peerId;
    msg.type       = MessageType::Folder;
    msg.outgoing   = true;
    msg.fileName   = fi.fileName();
    msg.filePath   = folderPath;
    msg.fileSize   = totalSz;
    msg.status     = MessageStatus::WaitingAccept;
    msg.timestamp  = QDateTime::currentDateTime();
    msg.folderTree = treeArr;
    msg.id         = m_store->addMessage(msg);
    emit messageAdded(msg);

    const Peer peer = peerById(peerId);
    if (peer.id.isEmpty()) return;

    m_pendingOffers.insert(msg.id, {peerId, folderPath});

    QJsonObject payload;
    payload[QStringLiteral("name")]   = fi.fileName();
    payload[QStringLiteral("size")]   = totalSz;
    payload[QStringLiteral("folder")] = true;
    payload[QStringLiteral("tree")]   = treeArr;
    m_chatClient->send(peer.address, kChatPort,
                       Protocol::MessageType::FileRequest, msg.id, payload);
}

void App::acceptTransfer(qint64 messageId) {
    // ChatStore doesn't expose getById; iterate across all known peers (small dataset).
    const Message msg = [&]() -> Message {
        for (const auto &p : m_discovery->peers())
            for (const auto &m : m_store->messages(p.id))
                if (m.id == messageId) return m;
        return {};
    }();

    if (msg.id < 0) return;

    // Update local status.
    Message updated = msg;
    updated.status = MessageStatus::Transferring;
    m_store->updateMessage(updated);
    emit messageUpdated(updated);

    // Enqueue entry so transferStarted FIFO can map the incoming TCP connection(s).
    const bool isFolder = (msg.type == MessageType::Folder);
    auto *entry = new InboundEntry{msg.id, msg.peerId, 0, isFolder};
    if (isFolder)
        entry->folderName = msg.fileName;
    m_pendingInbound.append(entry);

    const Peer peer = peerById(msg.peerId);
    if (peer.id.isEmpty()) return;

    // Echo the sender's original msgId (as the frame msgId) so they can match
    // m_pendingOffers, and include our local msgId in the payload so the sender
    // can stamp it on the data connection for precise inbound matching.
    QJsonObject acceptPayload;
    acceptPayload[QStringLiteral("localMsgId")] = msg.id;
    m_chatClient->send(peer.address, kChatPort,
                       Protocol::MessageType::FileAccept, msg.remoteMsgId, acceptPayload);
}

void App::denyTransfer(qint64 messageId) {
    const Message msg = [&]() -> Message {
        for (const auto &p : m_discovery->peers())
            for (const auto &m : m_store->messages(p.id))
                if (m.id == messageId) return m;
        return {};
    }();

    if (msg.id < 0) return;

    Message updated = msg;
    updated.status = MessageStatus::Denied;
    m_store->updateMessage(updated);
    emit messageUpdated(updated);

    const Peer peer = peerById(msg.peerId);
    if (peer.id.isEmpty()) return;

    // Echo the sender's original msgId.
    m_chatClient->send(peer.address, kChatPort,
                       Protocol::MessageType::FileReject, msg.remoteMsgId, {});
}

void App::requestAgain(qint64 messageId) {
    const Message msg = [&]() -> Message {
        for (const auto &p : m_discovery->peers())
            for (const auto &m : m_store->messages(p.id))
                if (m.id == messageId) return m;
        return {};
    }();

    if (msg.id < 0) return;

    Message updated = msg;
    updated.status = MessageStatus::RequestAgain;
    m_store->updateMessage(updated);
    emit messageUpdated(updated);

    const Peer peer = peerById(msg.peerId);
    if (peer.id.isEmpty()) return;

    // Echo the sender's original msgId; include our local msgId for data-connection matching.
    QJsonObject ragainPayload;
    ragainPayload[QStringLiteral("localMsgId")] = msg.id;
    m_chatClient->send(peer.address, kChatPort,
                       Protocol::MessageType::RequestAgain, msg.remoteMsgId, ragainPayload);
}

void App::sendImage(const QString &peerId, const QImage &image) {
    if (image.isNull()) return;

    // Encode to a temp PNG file — reuses TransferClient unchanged.
    const QString tempDir = QStandardPaths::writableLocation(
        QStandardPaths::TempLocation);
    QDir().mkpath(tempDir);
    const QString fileName = QStringLiteral("yukisend_img_%1.png")
        .arg(QDateTime::currentMSecsSinceEpoch());
    const QString filePath = tempDir + QDir::separator() + fileName;

    QImageWriter writer(filePath, "PNG");
    if (!writer.write(image)) return;

    Message msg;
    msg.peerId    = peerId;
    msg.type      = MessageType::Image;
    msg.outgoing  = true;
    msg.fileName  = fileName;
    msg.filePath  = filePath;
    msg.fileSize  = QFileInfo(filePath).size();
    msg.status    = MessageStatus::WaitingAccept;
    msg.timestamp = QDateTime::currentDateTime();
    msg.id        = m_store->addMessage(msg);
    emit messageAdded(msg);

    const Peer peer = peerById(peerId);
    if (peer.id.isEmpty()) return;

    m_pendingOffers.insert(msg.id, {peerId, filePath});

    // Build a small blurred thumbnail to send with the offer so the receiver
    // can show a blurred preview immediately without waiting for the transfer.
    const QImage thumb = ImageUtils::makeThumbnail(image, QSize(40, 40));
    const QByteArray thumbJpeg = ImageUtils::encodeJpeg(thumb, 40);

    QJsonObject payload;
    payload[QStringLiteral("name")]  = fileName;
    payload[QStringLiteral("size")]  = msg.fileSize;
    payload[QStringLiteral("image")] = true;
    if (!thumbJpeg.isEmpty())
        payload[QStringLiteral("thumb")] =
            QString::fromLatin1(thumbJpeg.toBase64());
    m_chatClient->send(peer.address, kChatPort,
                       Protocol::MessageType::FileRequest, msg.id, payload);
}

// ── Incoming frame router ─────────────────────────────────────────────────────

void App::routeIncoming(const QString &peerIdOrAddr,
                         bool isPeerId,
                         Protocol::MessageType type,
                         qint64 msgId,
                         QJsonObject payload)
{
    QString peerId;

    if (isPeerId) {
        // Frame came via ChatServer — peerIdOrAddr is already the peer's ID
        // as declared in their Hello handshake. Trust it directly.
        peerId = peerIdOrAddr;
    } else {
        // Frame came via ChatClient (we initiated) — peerIdOrAddr is an IP.
        // Resolve to peerId via discovery or PeerStore.
        const QString normAddr = normaliseIp(peerIdOrAddr);
        for (const auto &p : m_discovery->peers()) {
            if (normaliseIp(p.address) == normAddr) {
                peerId = p.id;
                break;
            }
        }
        if (peerId.isEmpty())
            peerId = m_peerStore->peerIdByAddress(normAddr);
        if (peerId.isEmpty()) {
            // Still unknown — defer until next peersChanged.
            m_deferredFrames.append({peerIdOrAddr, isPeerId, type, msgId, payload});
            return;
        }
    }

    // Drop all frames from blocked peers silently.
    if (m_peerStore->isBlocked(peerId)) return;

    // Resolve peer IP for data-channel connections (TransferClient needs an address).
    QString peerAddress = isPeerId ? QString() : normaliseIp(peerIdOrAddr);
    if (peerAddress.isEmpty()) {
        const Peer p = peerById(peerId);
        peerAddress = p.address;
    }

    using MT = Protocol::MessageType;

    switch (type) {

    case MT::ChatMessage: {
        Message msg;
        msg.peerId    = peerId;
        msg.type      = MessageType::Text;
        msg.outgoing  = false;
        msg.text      = payload[QStringLiteral("text")].toString();
        msg.status    = MessageStatus::Done;
        msg.timestamp = QDateTime::currentDateTime();
        msg.id        = m_store->addMessage(msg);
        emit messageAdded(msg);
        break;
    }

    case MT::FileRequest: {
        Message msg;
        msg.peerId      = peerId;
        msg.type        = payload[QStringLiteral("image")].toBool()
                              ? MessageType::Image
                              : (payload[QStringLiteral("folder")].toBool()
                                 ? MessageType::Folder : MessageType::File);
        msg.outgoing    = false;
        msg.fileName    = payload[QStringLiteral("name")].toString();
        msg.fileSize    = static_cast<qint64>(payload[QStringLiteral("size")].toDouble());
        msg.status      = MessageStatus::WaitingAccept;
        msg.timestamp   = QDateTime::currentDateTime();
        // Store the sender's msgId so accept/deny can echo it back correctly.
        msg.remoteMsgId = msgId;
        // Decode embedded thumbnail if present (Image offers only).
        const QString thumbB64 = payload[QStringLiteral("thumb")].toString();
        if (!thumbB64.isEmpty())
            msg.thumbData = QByteArray::fromBase64(thumbB64.toLatin1());
        // Store folder tree sent by sender so receiver can preview structure immediately.
        if (msg.type == MessageType::Folder)
            msg.folderTree = payload[QStringLiteral("tree")].toArray();
        msg.id          = m_store->addMessage(msg);
        emit messageAdded(msg);
        break;
    }

    case MT::FileAccept: {
        // Sender side: peer accepted our file offer.  msgId is our local id.
        auto it = m_pendingOffers.find(msgId);
        if (it == m_pendingOffers.end()) return;

        const PendingOffer offer = it.value();
        m_pendingOffers.erase(it);

        // The receiver echoes their local msgId so we can stamp it on the data
        // connection — this lets them match the TCP socket to the right Accept.
        const qint64 receiverMsgId = static_cast<qint64>(
            payload[QStringLiteral("localMsgId")].toDouble(-1));

        // Update status to Transferring.
        Message updated;
        updated.id     = msgId;
        updated.peerId = offer.peerId;
        updated.status = MessageStatus::Transferring;
        m_store->updateMessage(updated);
        emit messageUpdated(updated);

        // Create a dedicated TransferClient for this transfer.
        const qint64 capturedMsgId   = msgId;
        const QString capturedPeerId = offer.peerId;
        auto *client = new TransferClient(this);
        m_outboundClients.insert(capturedMsgId, client);

        connect(client, &TransferClient::transferProgress,
                this, [this, capturedMsgId, capturedPeerId](qint64 sent, qint64 total) {
                    Message upd;
                    upd.id               = capturedMsgId;
                    upd.peerId           = capturedPeerId;
                    upd.status           = MessageStatus::Transferring;
                    upd.bytesTransferred = sent;
                    upd.fileSize         = total;
                    m_store->updateMessage(upd);
                    emit messageUpdated(upd);
                });

        connect(client, &TransferClient::transferFinished,
                this, [this, capturedMsgId, capturedPeerId, client] {
                    Message upd;
                    upd.id     = capturedMsgId;
                    upd.peerId = capturedPeerId;
                    upd.status = MessageStatus::Done;
                    m_store->updateMessage(upd);
                    emit messageUpdated(upd);
                    emit transferFinished({});
                    m_outboundClients.remove(capturedMsgId);
                    client->deleteLater();
                });

        connect(client, &TransferClient::transferFailed,
                this, [this, capturedMsgId, client](const QString &reason) {
                    emit transferFailed(reason);
                    m_outboundClients.remove(capturedMsgId);
                    client->deleteLater();
                });

        const QFileInfo fi(offer.filePath);
        if (fi.isDir())
            client->sendFolder(peerAddress, kDataPort, offer.filePath, receiverMsgId);
        else
            client->sendFile(peerAddress, kDataPort, offer.filePath, receiverMsgId);
        break;
    }

    case MT::FileReject: {
        // Sender side: peer denied our offer.
        auto it = m_pendingOffers.find(msgId);
        if (it == m_pendingOffers.end()) return;
        const PendingOffer offer = it.value();
        m_pendingOffers.erase(it);

        Message updated;
        updated.id     = msgId;
        updated.peerId = offer.peerId;
        updated.status = MessageStatus::Denied;
        m_store->updateMessage(updated);
        emit messageUpdated(updated);
        break;
    }

    case MT::RequestAgain: {
        // Sender side: peer wants us to resend.  msgId is our local id.
        // m_pendingOffers may have been cleared on the previous accept, so
        // look up the filePath from the chat store instead.
        // Receiver also sends their localMsgId so we can stamp the data connection.
        const qint64 receiverMsgId = static_cast<qint64>(
            payload[QStringLiteral("localMsgId")].toDouble(-1));

        QString filePath;
        QString senderPeerId;
        for (const auto &p : m_discovery->peers()) {
            for (const auto &m : m_store->messages(p.id)) {
                if (m.id == msgId) {
                    filePath     = m.filePath;
                    senderPeerId = p.id;
                    break;
                }
            }
            if (!filePath.isEmpty()) break;
        }
        if (filePath.isEmpty()) break;

        Message updated;
        updated.id     = msgId;
        updated.peerId = senderPeerId;
        updated.status = MessageStatus::Transferring;
        m_store->updateMessage(updated);
        emit messageUpdated(updated);

        const qint64 capturedMsgId   = msgId;
        const QString capturedPeerId = senderPeerId;
        auto *client = new TransferClient(this);
        m_outboundClients.insert(capturedMsgId, client);

        connect(client, &TransferClient::transferProgress,
                this, [this, capturedMsgId, capturedPeerId](qint64 sent, qint64 total) {
                    Message upd;
                    upd.id               = capturedMsgId;
                    upd.peerId           = capturedPeerId;
                    upd.status           = MessageStatus::Transferring;
                    upd.bytesTransferred = sent;
                    upd.fileSize         = total;
                    m_store->updateMessage(upd);
                    emit messageUpdated(upd);
                });

        connect(client, &TransferClient::transferFinished,
                this, [this, capturedMsgId, capturedPeerId, client] {
                    Message upd;
                    upd.id     = capturedMsgId;
                    upd.peerId = capturedPeerId;
                    upd.status = MessageStatus::Done;
                    m_store->updateMessage(upd);
                    emit messageUpdated(upd);
                    emit transferFinished({});
                    m_outboundClients.remove(capturedMsgId);
                    client->deleteLater();
                });

        connect(client, &TransferClient::transferFailed,
                this, [this, capturedMsgId, client](const QString &reason) {
                    emit transferFailed(reason);
                    m_outboundClients.remove(capturedMsgId);
                    client->deleteLater();
                });

        const QFileInfo fi(filePath);
        if (fi.isDir())
            client->sendFolder(peerAddress, kDataPort, filePath, receiverMsgId);
        else
            client->sendFile(peerAddress, kDataPort, filePath, receiverMsgId);
        break;
    }

    default:
        break;
    }
}

// ── Download directory ────────────────────────────────────────────────────────

void App::setDownloadDir(const QString &dir) {
    m_server->setDownloadDir(dir);
}

QString App::downloadDir() const {
    return m_server->downloadDir();
}

// ── Display name ──────────────────────────────────────────────────────────────

void App::setDisplayName(const QString &name) {
    AppSettings settings;
    settings.setDisplayName(name);
    m_discovery->setDisplayName(name);
}

// ── PeerStore accessor ────────────────────────────────────────────────────────

PeerStore *App::peerStore() const { return m_peerStore; }

// ── Peer management ───────────────────────────────────────────────────────────

void App::setSaved(const QString &peerId, bool saved) {
    // Grab current peer info (online or from store) for the metadata fields.
    Peer p = peerById(peerId);
    if (p.id.isEmpty()) {
        // Peer is offline — read stored info.
        for (const Peer &sp : m_peerStore->savedPeers())
            if (sp.id == peerId) { p = sp; break; }
    }
    m_peerStore->setSaved(peerId, p.displayName, p.address, p.port, saved);
    emit peersChanged();
}

void App::setFavorite(const QString &peerId, bool favorite) {
    if (favorite) {
        // Ensure the peer is also saved when favorited.
        Peer p = peerById(peerId);
        if (p.id.isEmpty())
            for (const Peer &sp : m_peerStore->savedPeers())
                if (sp.id == peerId) { p = sp; break; }
        m_peerStore->setSaved(peerId, p.displayName, p.address, p.port, true);
    }
    m_peerStore->setFavorite(peerId, favorite);
    emit peersChanged();
}

void App::setBlocked(const QString &peerId, bool blocked) {
    m_peerStore->setBlocked(peerId, blocked);
    emit peersChanged();
}

// ── Storage strategy ──────────────────────────────────────────────────────────

void App::setDefaultStorageStrategy(StorageStrategy s) {
    AppSettings settings;
    settings.setDefaultStorageStrategy(s);
    m_store->setDefaultStrategy(s);
}

void App::setPeerStorageStrategy(const QString &peerId, StorageStrategy s) {
    m_store->setStrategy(peerId, s);
    // If switching to SessionOnly, clear existing history immediately.
    if (s == StorageStrategy::SessionOnly)
        m_store->clearPeer(peerId);
}

// ── History management ────────────────────────────────────────────────────────

void App::deleteHistory(const QStringList &peerIds) {
    for (const QString &id : peerIds)
        m_store->clearPeer(id);
}

// ── Offline delivery helpers ──────────────────────────────────────────────────

bool App::isPeerOnline(const QString &peerId) const {
    for (const Peer &p : m_discovery->peers())
        if (p.id == peerId) return true;
    return false;
}

void App::flushOutbox(const Peer &peer) {
    const QList<OutboxEntry> entries = m_outbox->dequeue(peer.id);
    for (const OutboxEntry &e : entries) {
        if (e.type == Protocol::MessageType::ChatMessage) {
            m_chatClient->send(peer.address, kChatPort, e.type,
                               e.id,  // use outbox id as msgId for ordering
                               e.payload);
        } else if (e.type == Protocol::MessageType::FileRequest) {
            // File path is stored in payload under "localPath" (set at enqueue time).
            // The actual offer was already recorded in ChatStore — resend the offer frame.
            const qint64 localMsgId = e.payload[QStringLiteral("localMsgId")].toDouble();
            m_pendingOffers.insert(localMsgId, { peer.id,
                e.payload[QStringLiteral("localPath")].toString() });
            QJsonObject offerPayload = e.payload;
            offerPayload.remove(QStringLiteral("localPath"));
            offerPayload.remove(QStringLiteral("localMsgId"));
            m_chatClient->send(peer.address, kChatPort,
                               Protocol::MessageType::FileRequest,
                               localMsgId, offerPayload);
        }
    }
}

// ── acceptTransferTo ──────────────────────────────────────────────────────────

void App::acceptTransferTo(qint64 messageId, const QString &destDir) {
    const Message msg = [&]() -> Message {
        for (const auto &p : m_discovery->peers())
            for (const auto &m : m_store->messages(p.id))
                if (m.id == messageId) return m;
        return {};
    }();

    if (msg.id < 0) return;

    // Override the server download dir for this transfer.
    // The sender will open the TCP data connection immediately after receiving
    // FileAccept, so setting the dir before sending the accept is safe.
    m_server->setDownloadDir(destDir);

    Message updated = msg;
    updated.status = MessageStatus::Transferring;
    m_store->updateMessage(updated);
    emit messageUpdated(updated);

    const bool isFolder = (msg.type == MessageType::Folder);
    auto *inbound = new InboundEntry{msg.id, msg.peerId, 0, isFolder};
    if (isFolder)
        inbound->folderName = msg.fileName;
    m_pendingInbound.append(inbound);

    const Peer peer = peerById(msg.peerId);
    if (peer.id.isEmpty()) return;

    QJsonObject acceptPayload;
    acceptPayload[QStringLiteral("localMsgId")] = msg.id;
    m_chatClient->send(peer.address, kChatPort,
                       Protocol::MessageType::FileAccept, msg.remoteMsgId, acceptPayload);
    // Note: downloadDir stays set to destDir until the user changes it or
    // the next call to setDownloadDir(). This is intentional: "Accept to"
    // becomes the new default until explicitly changed.
}
