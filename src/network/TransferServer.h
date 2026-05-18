#pragma once

#include <QObject>

// Listens for incoming TCP connections from peers who want to send files.
class TransferServer : public QObject {
    Q_OBJECT
public:
    explicit TransferServer(QObject *parent = nullptr);
    ~TransferServer() override;

    bool listen(quint16 port);
    void close();

    quint16 port() const;

    // Where received files are saved. Defaults to DownloadLocation.
    void    setDownloadDir(const QString &dir);
    QString downloadDir() const;

signals:
    // transferMsgId matches the msgId the sender passed to sendFile/sendFolder (-1 if unknown/old client).
    void transferStarted(quintptr transferId, const QString &senderName, const QString &fileName, qint64 fileSize, qint64 transferMsgId);
    void transferProgress(quintptr transferId, qint64 bytesReceived, qint64 total);
    void transferFileFinished(quintptr transferId, const QString &savedPath); // one file done, more may follow
    void transferBatchFinished(quintptr transferId); // eos received — all files done
    void transferFailed(quintptr transferId, const QString &reason);

private:
    struct Private;
    Private *d;
};
