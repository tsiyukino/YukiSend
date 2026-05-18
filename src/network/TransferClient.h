#pragma once

#include <QObject>
#include <QString>

// Connects to a remote peer and sends a file.
class TransferClient : public QObject {
    Q_OBJECT
public:
    explicit TransferClient(QObject *parent = nullptr);
    ~TransferClient() override;

    // msgId is echoed to the receiver so it can match this connection to the
    // correct Accept click without relying on FIFO arrival order.
    void sendFile(const QString &address, quint16 port, const QString &filePath,
                  qint64 msgId = -1);
    void sendFolder(const QString &address, quint16 port, const QString &folderPath,
                    qint64 msgId = -1);
    void cancel();

signals:
    void transferProgress(qint64 bytesSent, qint64 total);
    void transferFinished();
    void transferFailed(const QString &reason);

private:
    void connectAndSendNext();
    void sendCurrentHeader();
    void sendNextChunk();
    void cleanup();

    struct Private;
    Private *d;
};
