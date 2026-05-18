#include "ChatServer.h"
#include "MessageChannel.h"

#include <memory>

#include <QTcpServer>
#include <QTcpSocket>
#include <QHostAddress>

// Normalise IPv4-mapped IPv6 addresses ("::ffff:1.2.3.4") to plain IPv4.
static QString peerIp(QTcpSocket *sock) {
    const QHostAddress ha = sock->peerAddress();
    bool ok = false;
    const quint32 v4 = ha.toIPv4Address(&ok);
    return ok ? QHostAddress(v4).toString() : ha.toString();
}

struct ChatServer::Private {
    QTcpServer *server = nullptr;
};

ChatServer::ChatServer(QObject *parent)
    : QObject(parent), d(new Private)
{
    d->server = new QTcpServer(this);

    connect(d->server, &QTcpServer::newConnection, this, [this] {
        while (d->server->hasPendingConnections()) {
            QTcpSocket *sock = d->server->nextPendingConnection();
            if (!sock) continue;

            // Channel is parented to sock so it dies when the socket closes.
            auto *ch = new MessageChannel(sock, sock);

            // peerId starts as the IP; replaced by the Hello frame's "id" field.
            // Using a shared_ptr so both the lambda and the outer scope own it.
            auto peerId = std::make_shared<QString>(peerIp(sock));

            connect(ch, &MessageChannel::frameReceived,
                    this, [this, peerId, ch](Protocol::MessageType type,
                                             qint64 msgId,
                                             QJsonObject payload) {
                        if (type == Protocol::MessageType::Hello) {
                            // Bind this connection to the sender's declared peer ID.
                            *peerId = payload[QStringLiteral("id")].toString();
                            return; // Hello is not forwarded to App.
                        }
                        emit frameReceived(*peerId, type, msgId, payload);
                    });

            connect(sock, &QTcpSocket::disconnected, sock, &QObject::deleteLater);
        }
    });
}

ChatServer::~ChatServer() {
    delete d;
}

bool ChatServer::listen(quint16 port) {
    return d->server->listen(QHostAddress::Any, port);
}

void ChatServer::close() {
    d->server->close();
}
