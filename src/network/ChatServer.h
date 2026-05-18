#pragma once

#include <QObject>
#include <QJsonObject>

#include "Protocol.h"

// Listens on the chat port (48321) for incoming peer connections.
// For each connection, creates a MessageChannel and re-emits its frames
// tagged with the peer's IP address.
//
// One ChatServer per application; multiple simultaneous peers are supported.
class ChatServer : public QObject {
    Q_OBJECT
public:
    explicit ChatServer(QObject *parent = nullptr);
    ~ChatServer() override;

    bool listen(quint16 port = Protocol::ChatPort);
    void close();

signals:
    // Emitted for every decoded frame from any connected peer.
    // peerId is the peer's self-declared ID from the Hello frame (preferred),
    // or the normalised IPv4 address as a fallback for pre-Hello frames.
    void frameReceived(const QString &peerId,
                       Protocol::MessageType type,
                       qint64 msgId,
                       QJsonObject payload);

private:
    struct Private;
    Private *d;
};
