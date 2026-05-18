#include "TransferServer.h"
#include "Protocol.h"

#include <QTcpServer>
#include <QTcpSocket>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QCryptographicHash>
#include <QJsonObject>
#include <QJsonDocument>

// ── Internal types ───────────────────────────────────────────────────────────

struct IncomingTransfer {
    QTcpSocket        *socket        = nullptr;
    QFile             *file          = nullptr;
    qint64             fileSize      = 0;
    qint64             received      = 0;
    QString            savePath;
    QByteArray         buf;          // accumulates raw bytes from socket
    bool               headerDone   = false;
    quint32            headerLen    = 0;
    QString            expectedSha256;
    QCryptographicHash hasher{QCryptographicHash::Sha256};
    qint64             transferMsgId = -1; // declared by sender; -1 = unknown/old client
    bool               msgIdRead    = false; // true once the msgId frame has been consumed

    // Reset per-file state while keeping the socket alive for the next file.
    void resetForNextFile() {
        if (file) { file->close(); delete file; file = nullptr; }
        fileSize      = 0;
        received      = 0;
        savePath.clear();
        headerDone    = false;
        headerLen     = 0;
        expectedSha256.clear();
        hasher.reset();
    }
};

struct TransferServer::Private {
    QTcpServer               *server      = nullptr;
    QList<IncomingTransfer *> transfers;
    QString                   downloadDir;

    void onReadyRead(TransferServer *q, IncomingTransfer *xfer);
    void finishFile(TransferServer *q, IncomingTransfer *xfer);
    void abortTransfer(TransferServer *q, IncomingTransfer *xfer, const QString &reason);
    QString resolveDownloadDir() const;
    // cleanup() removed: use abortTransfer() for error paths, eos handling in onReadyRead.
};

// ── Private implementation ───────────────────────────────────────────────────

void TransferServer::Private::abortTransfer(TransferServer *q, IncomingTransfer *xfer,
                                             const QString &reason)
{
    const quintptr tid = reinterpret_cast<quintptr>(xfer->socket);
    transfers.removeOne(xfer);
    xfer->socket->abort();
    if (xfer->file) { xfer->file->close(); delete xfer->file; }
    delete xfer;
    emit q->transferFailed(tid, reason);
}

void TransferServer::Private::finishFile(TransferServer *q, IncomingTransfer *xfer)
{
    const quintptr tid = reinterpret_cast<quintptr>(xfer->socket);
    const QString path = xfer->savePath;

    // Verify integrity before reporting success.
    if (!xfer->expectedSha256.isEmpty()) {
        const QString actual = QString::fromLatin1(xfer->hasher.result().toHex());
        if (actual != xfer->expectedSha256) {
            if (!path.isEmpty()) QFile::remove(path);
            xfer->resetForNextFile();
            emit q->transferFailed(tid, QStringLiteral("Integrity check failed (SHA-256 mismatch)"));
            // Don't abort the socket — let remaining stream data drain / next header come.
            return;
        }
    }

    emit q->transferFileFinished(tid, path);
    xfer->resetForNextFile();
    // onReadyRead loop continues — next file header or eos follows on the same connection.
}

QString TransferServer::Private::resolveDownloadDir() const {
    if (!downloadDir.isEmpty()) return downloadDir;
    return QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
}

void TransferServer::Private::onReadyRead(TransferServer *q, IncomingTransfer *xfer) {
    xfer->buf += xfer->socket->readAll();
    const quintptr tid = reinterpret_cast<quintptr>(xfer->socket);

    // Process as many complete frames as we have buffered.
    while (true) {
        // ── Read frame length prefix ─────────────────────────────────────────
        if (!xfer->headerDone) {
            if (xfer->headerLen == 0) {
                if (xfer->buf.size() < 4) return;
                quint32 raw = 0;
                memcpy(&raw, xfer->buf.constData(), 4);
                xfer->headerLen = qFromBigEndian(raw);
                xfer->buf.remove(0, 4);

                if (xfer->headerLen == 0 ||
                    xfer->headerLen > static_cast<quint32>(Protocol::MaxFrameBytes)) {
                    abortTransfer(q, xfer, QStringLiteral("Invalid frame length"));
                    return;
                }
            }

            if (static_cast<quint32>(xfer->buf.size()) < xfer->headerLen) return;

            const QByteArray hdrBytes = xfer->buf.left(static_cast<int>(xfer->headerLen));
            xfer->buf.remove(0, static_cast<int>(xfer->headerLen));

            const QJsonObject hdr = QJsonDocument::fromJson(hdrBytes).object();

            // ── transferMsgId identification frame ───────────────────────────
            // First frame from a modern sender carries only {"transferMsgId": N}.
            // Consume it silently and loop to read the real file header.
            if (!xfer->msgIdRead) {
                xfer->msgIdRead = true;
                if (hdr.contains(QStringLiteral("transferMsgId"))) {
                    xfer->transferMsgId = static_cast<qint64>(
                        hdr[QStringLiteral("transferMsgId")].toDouble());
                    xfer->headerLen = 0; // ready for next frame
                    continue;
                }
                // Old client: no msgId frame — treat this frame as the file header.
            }

            // ── End-of-stream sentinel ───────────────────────────────────────
            if (hdr[QStringLiteral("eos")].toBool(false)) {
                // All files sent — notify App that the whole batch is complete.
                emit q->transferBatchFinished(tid);
                transfers.removeOne(xfer);
                xfer->socket->disconnectFromHost();
                delete xfer;
                return;
            }

            const QString fileName = hdr[QStringLiteral("name")].toString();
            const QString relPath  = hdr[QStringLiteral("relPath")].toString();
            const bool    isDir    = hdr[QStringLiteral("isDir")].toBool(false);
            xfer->fileSize       = static_cast<qint64>(hdr[QStringLiteral("size")].toDouble());
            xfer->expectedSha256 = hdr[QStringLiteral("sha256")].toString();
            xfer->headerLen      = 0; // reset for next file's length prefix

            if (fileName.isEmpty()) {
                abortTransfer(q, xfer, QStringLiteral("Invalid transfer header"));
                return;
            }

            const QString baseDir = resolveDownloadDir();

            if (xfer->fileSize > 0) {
                const QStorageInfo storage(baseDir);
                if (storage.bytesAvailable() < xfer->fileSize + 10LL * 1024 * 1024) {
                    abortTransfer(q, xfer, QStringLiteral("Insufficient disk space"));
                    return;
                }
            }

            QString savePath = relPath.isEmpty()
                ? baseDir + QStringLiteral("/") + fileName
                : baseDir + QStringLiteral("/") + relPath;

            if (isDir) {
                QDir().mkpath(savePath);
                emit q->transferStarted(tid, xfer->socket->peerAddress().toString(), fileName, 0, xfer->transferMsgId);
                emit q->transferFileFinished(tid, savePath);
                xfer->resetForNextFile();
                continue;
            }

            QFileInfo(savePath).dir().mkpath(QStringLiteral("."));
            if (QFile::exists(savePath)) {
                const QFileInfo fi(savePath);
                savePath = fi.dir().filePath(
                    fi.baseName() + QStringLiteral("_1.") + fi.completeSuffix());
            }
            xfer->savePath = savePath;
            xfer->file = new QFile(savePath, xfer->socket); // parented to socket for safety
            if (!xfer->file->open(QIODevice::WriteOnly)) {
                abortTransfer(q, xfer, QStringLiteral("Cannot write to: ") + savePath);
                return;
            }

            xfer->headerDone = true;
            emit q->transferStarted(tid, xfer->socket->peerAddress().toString(),
                                    fileName, xfer->fileSize, xfer->transferMsgId);

            if (xfer->fileSize == 0) {
                xfer->file->close();
                finishFile(q, xfer);
                if (!transfers.contains(xfer)) return; // aborted during finishFile
                continue;
            }
        }

        // ── Stream file payload ──────────────────────────────────────────────
        if (xfer->buf.isEmpty()) return;

        const qint64 remaining = xfer->fileSize - xfer->received;
        const qint64 toWrite   = qMin(remaining, static_cast<qint64>(xfer->buf.size()));
        const QByteArray chunk = xfer->buf.left(static_cast<int>(toWrite));
        xfer->buf.remove(0, static_cast<int>(toWrite));

        xfer->file->write(chunk);
        xfer->hasher.addData(chunk);
        xfer->received += toWrite;
        emit q->transferProgress(tid, xfer->received, xfer->fileSize);

        if (xfer->received >= xfer->fileSize) {
            xfer->file->close();
            finishFile(q, xfer);
            if (!transfers.contains(xfer)) return;
            // Loop continues — next file header (or eos) may already be in buf.
        } else {
            return; // need more data
        }
    }
}

// ── TransferServer ───────────────────────────────────────────────────────────

TransferServer::TransferServer(QObject *parent)
    : QObject(parent), d(new Private)
{
    d->server = new QTcpServer(this);

    connect(d->server, &QTcpServer::newConnection, this, [this] {
        QTcpSocket *sock = d->server->nextPendingConnection();
        if (!sock) return;

        auto *xfer   = new IncomingTransfer;
        xfer->socket = sock;
        d->transfers.append(xfer);

        connect(sock, &QTcpSocket::readyRead, this, [this, xfer] {
            d->onReadyRead(this, xfer);
        });

        connect(sock, &QAbstractSocket::errorOccurred, this,
                [this, xfer](QAbstractSocket::SocketError) {
                    if (!d->transfers.contains(xfer)) return;
                    d->abortTransfer(this, xfer, xfer->socket->errorString());
                });

        connect(sock, &QTcpSocket::disconnected, this, [this, xfer] {
            if (!d->transfers.contains(xfer)) return;
            if (xfer->headerDone && xfer->received < xfer->fileSize)
                d->abortTransfer(this, xfer, QStringLiteral("Connection lost"));
            else {
                // Clean disconnect after eos — xfer already removed in onReadyRead.
                // If still in list it means eos was never received (old client).
                d->transfers.removeOne(xfer);
                delete xfer;
            }
        });
    });
}

TransferServer::~TransferServer() {
    for (auto *xfer : d->transfers) {
        xfer->socket->abort();
        delete xfer;
    }
    delete d;
}

bool TransferServer::listen(quint16 port) {
    return d->server->listen(QHostAddress::Any, port);
}

void TransferServer::close() {
    d->server->close();
}

quint16 TransferServer::port() const {
    return d->server->serverPort();
}

void TransferServer::setDownloadDir(const QString &dir) {
    d->downloadDir = dir;
}

QString TransferServer::downloadDir() const {
    return d->resolveDownloadDir();
}
