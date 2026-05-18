#pragma once

#include <QObject>
#include <QJsonObject>
#include <QString>

#include "Protocol.h"

// Maintains one persistent TCP connection per remote peer (keyed by address).
// If the connection drops it is re-established on the next send (up to 3 retries).
//
// Thread: must be used from the main thread.
class ChatClient : public QObject {
    Q_OBJECT
public:
    explicit ChatClient(QObject *parent = nullptr);
    ~ChatClient() override;

    // Queue a frame to be sent to address:port.
    // If not yet connected, connects first then flushes the queue.
    void send(const QString &address, quint16 port,
              Protocol::MessageType type, qint64 msgId,
              const QJsonObject &payload);

signals:
    // Frames received on the outbound connection (peer → us, e.g. FileAccept).
    void frameReceived(const QString &peerAddress,
                       Protocol::MessageType type,
                       qint64 msgId,
                       QJsonObject payload);
    void failed(const QString &peerAddress, const QString &reason);

private:
    struct Private;
    Private *d;
};
