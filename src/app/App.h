#pragma once

#include <QObject>
#include <QString>
#include <QImage>
#include <QSet>
#include <QHash>
#include <QQueue>
#include <QStringList>

#include "network/Discovery.h"
#include "network/TransferServer.h"
#include "network/TransferClient.h"
#include "network/ChatServer.h"
#include "network/ChatClient.h"
#include "model/ChatStore.h"
#include "model/PeerStore.h"
#include "model/OutboxStore.h"
#include "model/Message.h"

// Central application controller.
// Owns the network objects and exposes a clean interface to the UI layer.
class App : public QObject {
    Q_OBJECT
public:
    explicit App(QObject *parent = nullptr);
    ~App() override;

    void start();

    QList<Peer>    peers() const;
    QList<Message> messages(const QString &peerId) const;
    ChatStore     *chatStore() const;
    PeerStore     *peerStore() const;

    // Outbound — called by UI.
    void sendMessage(const QString &peerId, const QString &text);
    void sendFile(const QString &peerId, const QString &filePath);
    void sendFolder(const QString &peerId, const QString &folderPath);
    void sendImage(const QString &peerId, const QImage &image);
    void acceptTransfer(qint64 messageId);
    void acceptTransferTo(qint64 messageId, const QString &destDir);
    void denyTransfer(qint64 messageId);
    void requestAgain(qint64 messageId);

    void    setDownloadDir(const QString &dir);
    QString downloadDir() const;

    // Persists name via AppSettings and immediately re-announces to peers.
    void setDisplayName(const QString &name);

    // Peer management — forwarded to PeerStore + side effects.
    void setSaved(const QString &peerId, bool saved);
    void setFavorite(const QString &peerId, bool favorite);
    void setBlocked(const QString &peerId, bool blocked);

    // Message storage strategy.
    void setDefaultStorageStrategy(StorageStrategy s);
    void setPeerStorageStrategy(const QString &peerId, StorageStrategy s);

    // History management.
    void deleteHistory(const QStringList &peerIds);

signals:
    void peersChanged();
    void messageAdded(const Message &msg);
    void messageUpdated(const Message &msg);
    void transferProgress(qint64 sent, qint64 total);
    void transferFinished(const QString &path);
    void transferFailed(const QString &reason);

    // Emitted when a peer with no stored fingerprint appears for the first time.
    // UI should show a trust dialog; call peerStore()->trustPeer() or setBlocked()
    // based on user choice. Peer's messages are held until the user responds.
    void unknownPeerFingerprint(const Peer &peer);

    // Emitted when a known peer's fingerprint no longer matches the stored one.
    // May indicate a different device reusing the same peer ID (unlikely) or spoofing.
    // UI should warn the user; call peerStore()->trustPeer() to accept the new
    // fingerprint, or setBlocked() to reject this peer.
    void fingerprintMismatch(const Peer &peer,
                             const QString &knownFingerprint,
                             const QString &seenFingerprint);

private:
    Peer peerById(const QString &peerId) const;
    bool isPeerOnline(const QString &peerId) const;
    void flushOutbox(const Peer &peer);   // deliver queued entries on reconnect
    void routeIncoming(const QString &peerIdOrAddr,
                       bool isPeerId,
                       Protocol::MessageType type,
                       qint64 msgId,
                       QJsonObject payload);

    // Frames that arrived before discovery resolved the sender's peerId.
    // Retried on the next peersChanged tick (UDP usually arrives within ~1 s).
    struct DeferredFrame {
        QString                peerIdOrAddr;
        bool                   isPeerId;
        Protocol::MessageType  type;
        qint64                 msgId;
        QJsonObject            payload;
    };
    QList<DeferredFrame> m_deferredFrames;

    Discovery      *m_discovery;
    TransferServer *m_server;
    ChatServer     *m_chatServer;
    ChatClient     *m_chatClient;
    ChatStore      *m_store;
    PeerStore      *m_peerStore;
    OutboxStore    *m_outbox;

    struct PendingOffer {
        QString peerId;
        QString filePath;
    };
    QHash<qint64, PendingOffer> m_pendingOffers;

    // Concurrent outbound transfers: msgId → client instance.
    QHash<qint64, TransferClient *> m_outboundClients;

    // Concurrent inbound transfers.
    // Each Accept click enqueues one InboundEntry. A folder transfer opens one
    // TCP connection per file, so multiple tids map to the same entry. We use
    // a refcount: when it drops to zero the entry is considered fully done.
    struct InboundEntry {
        qint64  msgId;
        QString peerId;
        int     refCount  = 0;
        bool    isFolder  = false;   // true when the offer was a Folder message
        QString folderName;          // display name of the root folder (Folder transfers)
    };
    QHash<quintptr, InboundEntry *> m_inboundIdMap;  // tid → entry (not owned)
    QList<InboundEntry *>           m_pendingInbound; // ordered by Accept time (owned)

    // Tracks which peer IDs were online in the last peersChanged snapshot.
    // Used to detect peer dropouts and trigger clearSession on their history.
    QSet<QString> m_onlinePeerIds;

    // Peers whose fingerprint has already been flagged this session.
    // Prevents the trust dialog from re-firing every 5 s until the user acts.
    QSet<QString> m_fingerprintPrompted;
};
