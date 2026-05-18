#include "ChatClient.h"
#include "MessageChannel.h"

#include <QTcpSocket>
#include <QHash>
#include "model/AppSettings.h"

// ── Per-peer connection state ─────────────────────────────────────────────────

struct PeerConn {
    QTcpSocket     *socket  = nullptr;
    MessageChannel *channel = nullptr;
    quint16         port    = 0;

    // Frames queued while connecting.
    struct QueuedFrame {
        Protocol::MessageType type;
        qint64                msgId;
        QJsonObject           payload;
    };
    QList<QueuedFrame> queue;

    bool isConnected() const {
        return socket && socket->state() == QAbstractSocket::ConnectedState;
    }
};

// ── Private ───────────────────────────────────────────────────────────────────

struct ChatClient::Private {
    QHash<QString, PeerConn *> conns; // keyed by IP address string

    PeerConn *getOrCreate(ChatClient *q, const QString &address, quint16 port);
    void      setupSocket(ChatClient *q, PeerConn *pc, const QString &address);
    void      flushQueue(PeerConn *pc);
};

void ChatClient::Private::flushQueue(PeerConn *pc) {
    if (!pc->isConnected() || !pc->channel) return;
    for (const auto &f : pc->queue)
        pc->channel->send(f.type, f.msgId, f.payload);
    pc->queue.clear();
}

void ChatClient::Private::setupSocket(ChatClient *q,
                                      PeerConn *pc,
                                      const QString &address)
{
    auto *sock = new QTcpSocket(q);
    pc->socket  = sock;
    pc->channel = nullptr; // will be created on connect

    QObject::connect(sock, &QTcpSocket::connected, q, [this, q, pc, address] {
        pc->channel = new MessageChannel(pc->socket, pc->socket);

        QObject::connect(pc->channel, &MessageChannel::frameReceived,
                         q, [q, address](Protocol::MessageType type,
                                          qint64 msgId,
                                          QJsonObject payload) {
                             emit q->frameReceived(address, type, msgId, payload);
                         });

        // Announce our peer ID immediately so the remote end can identify us
        // without waiting for a UDP discovery beacon.
        QJsonObject hello;
        hello[QStringLiteral("id")] = AppSettings{}.selfId();
        pc->channel->send(Protocol::MessageType::Hello, 0, hello);

        flushQueue(pc);
    });

    QObject::connect(sock, &QAbstractSocket::errorOccurred, q,
                     [q, pc, address](QAbstractSocket::SocketError) {
                         emit q->failed(address, pc->socket->errorString());
                         // Clear the queue so stale frames don't pile up.
                         pc->queue.clear();
                     });

    QObject::connect(sock, &QTcpSocket::disconnected, q, [pc] {
        // Channel is parented to socket, so it dies here too.
        pc->channel = nullptr;
    });
}

PeerConn *ChatClient::Private::getOrCreate(ChatClient *q,
                                            const QString &address,
                                            quint16 port)
{
    auto it = conns.find(address);
    if (it != conns.end()) {
        PeerConn *pc = it.value();
        // Reconnect if the socket dropped.
        if (!pc->isConnected() &&
            pc->socket->state() == QAbstractSocket::UnconnectedState) {
            pc->socket->connectToHost(address, port);
        }
        return pc;
    }

    auto *pc = new PeerConn;
    pc->port = port;
    conns.insert(address, pc);
    setupSocket(q, pc, address);
    pc->socket->connectToHost(address, port);
    return pc;
}

// ── ChatClient ────────────────────────────────────────────────────────────────

ChatClient::ChatClient(QObject *parent)
    : QObject(parent), d(new Private)
{}

ChatClient::~ChatClient() {
    for (auto *pc : d->conns) {
        if (pc->socket) pc->socket->abort();
        delete pc;
    }
    delete d;
}

void ChatClient::send(const QString &address, quint16 port,
                      Protocol::MessageType type, qint64 msgId,
                      const QJsonObject &payload)
{
    PeerConn *pc = d->getOrCreate(this, address, port);

    if (pc->isConnected() && pc->channel) {
        pc->channel->send(type, msgId, payload);
    } else {
        // Buffer until connected.
        pc->queue.append({type, msgId, payload});
    }
}
