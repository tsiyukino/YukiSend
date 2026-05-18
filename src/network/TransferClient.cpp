#include "TransferClient.h"

#include <QTcpSocket>
#include <QFile>
#include <QFileInfo>
#include <QJsonObject>
#include <QJsonDocument>
#include <QTimer>
#include <QCryptographicHash>
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrentRun>

#include "utils/FileUtils.h"

static constexpr qint64 kChunkSize = 64 * 1024; // 64 KB per write

// ── Private ───────────────────────────────────────────────────────────────────

struct TransferClient::Private {
    QTcpSocket *socket        = nullptr;
    QFile      *file          = nullptr;
    qint64      total         = 0;
    qint64      sent          = 0;
    bool        headerSent    = false;
    bool        cancelled     = false; // set on cancel(); guards watcher callbacks
    qint64      transferMsgId = -1; // echoed to receiver so it can match the Accept

    struct PendingFile {
        QString absPath;
        QString relPath;
        bool    isDir;
    };
    QList<PendingFile> pending;
    QString            peerAddress;
    quint16            peerPort = 0;
};

// ── Construction ──────────────────────────────────────────────────────────────

TransferClient::TransferClient(QObject *parent)
    : QObject(parent), d(new Private)
{
    d->socket = new QTcpSocket(this);

    connect(d->socket, &QTcpSocket::connected, this, [this] {
        // First frame on every connection: identify this transfer so the receiver
        // can match it to the correct Accept click regardless of arrival order.
        if (d->transferMsgId >= 0) {
            QJsonObject id;
            id[QStringLiteral("transferMsgId")] = d->transferMsgId;
            const QByteArray idBytes = QJsonDocument(id).toJson(QJsonDocument::Compact);
            quint32 idLen = qToBigEndian(static_cast<quint32>(idBytes.size()));
            d->socket->write(reinterpret_cast<const char *>(&idLen), sizeof(idLen));
            d->socket->write(idBytes);
        }
        sendCurrentHeader();
    });

    connect(d->socket, &QTcpSocket::bytesWritten, this, [this](qint64) {
        if (d->headerSent && d->file && d->total > 0)
            sendNextChunk();
    });

    connect(d->socket, &QAbstractSocket::errorOccurred, this,
            [this](QAbstractSocket::SocketError) {
                emit transferFailed(d->socket->errorString());
                cleanup();
            });
}

TransferClient::~TransferClient() {
    cleanup();
    delete d;
}

// ── Public API ────────────────────────────────────────────────────────────────

void TransferClient::sendFile(const QString &address, quint16 port,
                              const QString &filePath, qint64 msgId)
{
    cleanup();
    d->peerAddress    = address;
    d->peerPort       = port;
    d->transferMsgId  = msgId;
    d->pending.clear();
    d->pending.append({filePath, {}, false});
    connectAndSendNext();
}

void TransferClient::sendFolder(const QString &address, quint16 port,
                                const QString &folderPath, qint64 msgId)
{
    cleanup();
    d->peerAddress    = address;
    d->peerPort       = port;
    d->transferMsgId  = msgId;
    d->pending.clear();

    const QList<FileUtils::FileEntry> entries = FileUtils::listFiles(folderPath);
    if (entries.isEmpty()) {
        emit transferFinished();
        return;
    }

    for (const auto &e : entries)
        d->pending.append({e.absPath, e.relPath, e.isDir});

    connectAndSendNext();
}

void TransferClient::cancel() {
    d->cancelled = true;
    d->socket->abort();
    cleanup();
}

// ── Private helpers ───────────────────────────────────────────────────────────

void TransferClient::connectAndSendNext() {
    // All files share a single TCP connection — connect once, stream all files.
    if (d->socket->state() == QAbstractSocket::UnconnectedState)
        d->socket->connectToHost(d->peerAddress, d->peerPort);
    else
        sendCurrentHeader(); // already connected (shouldn't happen on first call)
}

void TransferClient::sendCurrentHeader() {
    if (d->pending.isEmpty()) {
        // All files sent — send an end-of-stream sentinel then close.
        QJsonObject eos;
        eos[QStringLiteral("eos")] = true;
        const QByteArray eosBytes = QJsonDocument(eos).toJson(QJsonDocument::Compact);
        quint32 eosLen = qToBigEndian(static_cast<quint32>(eosBytes.size()));
        d->socket->write(reinterpret_cast<const char *>(&eosLen), sizeof(eosLen));
        d->socket->write(eosBytes);
        d->socket->flush();
        d->socket->disconnectFromHost();
        emit transferFinished();
        cleanup();
        return;
    }

    const Private::PendingFile pf = d->pending.takeFirst();

    if (d->file) { d->file->close(); d->file->deleteLater(); d->file = nullptr; }
    d->sent       = 0;
    d->total      = 0;
    d->headerSent = false;

    QJsonObject hdr;

    if (pf.isDir) {
        const QFileInfo fi(pf.relPath);
        hdr[QStringLiteral("name")]    = fi.fileName();
        hdr[QStringLiteral("relPath")] = pf.relPath;
        hdr[QStringLiteral("isDir")]   = true;
        hdr[QStringLiteral("size")]    = 0;
    } else {
        d->file = new QFile(pf.absPath, this);
        if (!d->file->open(QIODevice::ReadOnly)) {
            emit transferFailed(QStringLiteral("Cannot open: ") + pf.absPath);
            cleanup();
            return;
        }
        d->total = d->file->size();

        if (d->total == 0) {
            // Zero-byte file: no hash needed, send immediately.
            const QFileInfo fi(pf.absPath);
            hdr[QStringLiteral("name")] = fi.fileName();
            hdr[QStringLiteral("size")] = 0;
            if (!pf.relPath.isEmpty())
                hdr[QStringLiteral("relPath")] = pf.relPath;

            const QByteArray hdrBytes = QJsonDocument(hdr).toJson(QJsonDocument::Compact);
            quint32 hdrLen = qToBigEndian(static_cast<quint32>(hdrBytes.size()));
            d->socket->write(reinterpret_cast<const char *>(&hdrLen), sizeof(hdrLen));
            d->socket->write(hdrBytes);
            d->headerSent = true;
            d->file->close();
            QTimer::singleShot(0, this, [this] { sendCurrentHeader(); });
            return;
        }

        // Compute SHA-256 on a thread pool thread to keep the UI responsive.
        const QString absPath  = pf.absPath;
        const QString relPath  = pf.relPath;
        const qint64  fileSize = d->total;

        auto *watcher = new QFutureWatcher<QString>(this);
        connect(watcher, &QFutureWatcher<QString>::finished, this,
                [this, watcher, absPath, relPath, fileSize] {
                    watcher->deleteLater();
                    if (d->cancelled) return;
                    if (!d->socket || d->socket->state() != QAbstractSocket::ConnectedState)
                        return; // socket gone while we were hashing
                    const QString sha256 = watcher->result();

                    QJsonObject hdr;
                    const QFileInfo fi(absPath);
                    hdr[QStringLiteral("name")] = fi.fileName();
                    hdr[QStringLiteral("size")] = fileSize;
                    if (!relPath.isEmpty())
                        hdr[QStringLiteral("relPath")] = relPath;
                    if (!sha256.isEmpty())
                        hdr[QStringLiteral("sha256")] = sha256;

                    const QByteArray hdrBytes = QJsonDocument(hdr).toJson(QJsonDocument::Compact);
                    quint32 hdrLen = qToBigEndian(static_cast<quint32>(hdrBytes.size()));
                    d->socket->write(reinterpret_cast<const char *>(&hdrLen), sizeof(hdrLen));
                    d->socket->write(hdrBytes);
                    d->headerSent = true;

                    // Seek back to start and begin streaming.
                    if (d->file) d->file->seek(0);
                    QTimer::singleShot(0, this, [this] { sendNextChunk(); });
                });

        watcher->setFuture(QtConcurrent::run([absPath]() -> QString {
            QFile f(absPath);
            if (!f.open(QIODevice::ReadOnly)) return {};
            QCryptographicHash h(QCryptographicHash::Sha256);
            h.addData(&f);
            return QString::fromLatin1(h.result().toHex());
        }));
        return; // header will be sent from the watcher callback
    }

    // Directory entry path: write header synchronously.
    const QByteArray hdrBytes = QJsonDocument(hdr).toJson(QJsonDocument::Compact);
    quint32 hdrLen = qToBigEndian(static_cast<quint32>(hdrBytes.size()));
    d->socket->write(reinterpret_cast<const char *>(&hdrLen), sizeof(hdrLen));
    d->socket->write(hdrBytes);
    d->headerSent = true;

    // isDir — no payload, move to next file.
    QTimer::singleShot(0, this, [this] { sendCurrentHeader(); });
}

void TransferClient::sendNextChunk() {
    if (!d->file || !d->file->isOpen()) return;
    if (d->socket->bytesToWrite() > kChunkSize * 2) return;
    if (d->file->atEnd()) return;

    const QByteArray chunk = d->file->read(kChunkSize);
    if (chunk.isEmpty()) return;

    d->socket->write(chunk);
    d->sent += chunk.size();
    emit transferProgress(d->sent, d->total);

    if (d->sent >= d->total) {
        // Close the file immediately so further bytesWritten signals cannot
        // re-enter sendNextChunk and send stale data for the next file.
        d->file->close();
        d->file->deleteLater();
        d->file = nullptr;
        QTimer::singleShot(0, this, [this] { sendCurrentHeader(); });
    }
}

void TransferClient::cleanup() {
    if (d->file) {
        d->file->close();
        d->file->deleteLater();
        d->file = nullptr;
    }
    d->total      = 0;
    d->sent       = 0;
    d->headerSent = false;
    d->cancelled  = false;
    d->pending.clear();
}
