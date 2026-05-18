#include "Discovery.h"

#include <QUdpSocket>
#include <QTimer>
#include <QHostAddress>
#include <QNetworkInterface>
#include <QHostInfo>
#include <QDateTime>

static constexpr quint16 kDiscoveryPort = 48321;
static constexpr int     kAnnounceMs   = 5000;  // re-announce every 5s
static constexpr qint64  kExpireMs     = 30000; // remove peer after 30s silence

// Wire format (UTF-8, newline-delimited):
//   YUKISEND/1\nid=<uuid>\nname=<display>\nport=<tcp>\nfp=<fingerprint>\n
//   Add bye=1 for goodbye packets. fp field absent = old client, trust skipped.
static QByteArray buildAnnounce(const QString &id, const QString &name,
                                quint16 port, const QString &fingerprint)
{
    return QStringLiteral("YUKISEND/1\nid=%1\nname=%2\nport=%3\nfp=%4\n")
        .arg(id, name, QString::number(port), fingerprint)
        .toUtf8();
}

static QByteArray buildGoodbye(const QString &id) {
    return QStringLiteral("YUKISEND/1\nid=%1\nbye=1\n").arg(id).toUtf8();
}

struct ParsedPacket {
    QString id;
    QString name;
    QString fingerprint;
    quint16 port = 0;
    bool    bye  = false;
};

static bool parsePacket(const QByteArray &data, ParsedPacket &out) {
    const QList<QByteArray> lines = data.split('\n');
    if (lines.isEmpty() || lines[0] != "YUKISEND/1") return false;
    for (const auto &line : lines) {
        const int eq = line.indexOf('=');
        if (eq < 0) continue;
        const QByteArray key = line.left(eq);
        const QString    val = QString::fromUtf8(line.mid(eq + 1));
        if (key == "id")   out.id          = val;
        if (key == "name") out.name        = val;
        if (key == "port") out.port        = static_cast<quint16>(val.toUInt());
        if (key == "fp")   out.fingerprint = val;
        if (key == "bye")  out.bye         = (val == QStringLiteral("1"));
    }
    return !out.id.isEmpty();
}

struct PeerRecord {
    Peer   peer;
    qint64 lastSeenMs;
};

// Returns all subnet broadcast addresses for active IPv4 interfaces.
// Falls back to 255.255.255.255 if none found.
static QList<QHostAddress> subnetBroadcasts() {
    QList<QHostAddress> result;
    const auto ifaces = QNetworkInterface::allInterfaces();
    for (const auto &iface : ifaces) {
        if (!iface.flags().testFlag(QNetworkInterface::IsUp))       continue;
        if (!iface.flags().testFlag(QNetworkInterface::IsRunning))  continue;
        if ( iface.flags().testFlag(QNetworkInterface::IsLoopBack)) continue;
        for (const auto &entry : iface.addressEntries()) {
            if (entry.ip().protocol() != QAbstractSocket::IPv4Protocol) continue;
            const QHostAddress bcast = entry.broadcast();
            if (!bcast.isNull() && bcast != QHostAddress::Any)
                result.append(bcast);
        }
    }
    if (result.isEmpty())
        result.append(QHostAddress::Broadcast); // fallback
    return result;
}

struct Discovery::Private {
    quint16           listenPort;
    QString           selfId;
    QString           selfName;
    QString           selfFingerprint;
    QUdpSocket       *socket        = nullptr;
    QTimer           *announceTimer = nullptr;
    QTimer           *expireTimer   = nullptr;
    QList<PeerRecord> records;

    void sendDatagram(QUdpSocket *sock, const QByteArray &data) {
        for (const auto &bcast : subnetBroadcasts())
            sock->writeDatagram(data, bcast, kDiscoveryPort);
    }
};

Discovery::Discovery(quint16 listenPort, const QString &selfId,
                     const QString &selfFingerprint, QObject *parent)
    : QObject(parent), d(new Private)
{
    d->listenPort      = listenPort;
    d->selfId          = selfId;
    d->selfFingerprint = selfFingerprint;
    d->selfName        = QHostInfo::localHostName();
}

Discovery::~Discovery() {
    stop();
    delete d;
}

void Discovery::start() {
    if (d->socket) return;

    d->socket = new QUdpSocket(this);
    // Disable loopback so we never receive our own broadcast packets.
    d->socket->setSocketOption(QAbstractSocket::MulticastLoopbackOption, 0);
    d->socket->bind(QHostAddress::AnyIPv4, kDiscoveryPort,
                    QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint);

    connect(d->socket, &QUdpSocket::readyRead, this, [this] {
        while (d->socket->hasPendingDatagrams()) {
            QByteArray buf;
            buf.resize(static_cast<int>(d->socket->pendingDatagramSize()));
            QHostAddress sender;
            d->socket->readDatagram(buf.data(), buf.size(), &sender);

            ParsedPacket pkt;
            if (!parsePacket(buf, pkt))  continue;
            if (pkt.id == d->selfId)     continue;

            if (pkt.bye) {
                const int before = d->records.size();
                d->records.removeIf([&](const PeerRecord &r) {
                    return r.peer.id == pkt.id;
                });
                if (d->records.size() != before) emit peersChanged();
                continue;
            }

            if (pkt.name.isEmpty() || pkt.port == 0) continue;

            const QString address = sender.toString();
            const qint64  now     = QDateTime::currentMSecsSinceEpoch();
            bool changed = false;

            bool found = false;
            for (auto &rec : d->records) {
                if (rec.peer.id == pkt.id) {
                    // update in case name/address/fingerprint changed
                    if (rec.peer.displayName != pkt.name
                            || rec.peer.address != address
                            || rec.peer.fingerprint != pkt.fingerprint) {
                        rec.peer.displayName  = pkt.name;
                        rec.peer.address      = address;
                        rec.peer.port         = pkt.port;
                        rec.peer.fingerprint  = pkt.fingerprint;
                        changed = true;
                    }
                    rec.lastSeenMs = now;
                    found = true;
                    break;
                }
            }
            if (!found) {
                d->records.append({
                    { pkt.id, pkt.name, address, pkt.port, pkt.fingerprint }, now });
                changed = true;
            }
            if (changed) emit peersChanged();
        }
    });

    d->announceTimer = new QTimer(this);
    d->announceTimer->setInterval(kAnnounceMs);
    connect(d->announceTimer, &QTimer::timeout, this, [this] {
        d->sendDatagram(d->socket,
            buildAnnounce(d->selfId, d->selfName, d->listenPort, d->selfFingerprint));
    });
    d->announceTimer->start();

    d->expireTimer = new QTimer(this);
    d->expireTimer->setInterval(kAnnounceMs);
    connect(d->expireTimer, &QTimer::timeout, this, [this] {
        const qint64 now    = QDateTime::currentMSecsSinceEpoch();
        const int    before = d->records.size();
        d->records.removeIf([&](const PeerRecord &r) {
            return (now - r.lastSeenMs) > kExpireMs;
        });
        if (d->records.size() != before) emit peersChanged();
    });
    d->expireTimer->start();

    // Initial announce
    d->sendDatagram(d->socket,
        buildAnnounce(d->selfId, d->selfName, d->listenPort, d->selfFingerprint));
}

void Discovery::stop() {
    if (!d->socket) return;
    // Send goodbye before closing so peers remove us immediately
    d->sendDatagram(d->socket, buildGoodbye(d->selfId));
    d->announceTimer->stop();
    d->expireTimer->stop();
    d->socket->close();
    d->socket        = nullptr;
    d->announceTimer = nullptr;
    d->expireTimer   = nullptr;
}

QList<Peer> Discovery::peers() const {
    QList<Peer> out;
    out.reserve(d->records.size());
    for (const auto &r : d->records) out.append(r.peer);
    return out;
}

void Discovery::setDisplayName(const QString &name) {
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty() || trimmed == d->selfName) return;
    d->selfName = trimmed;
    // Immediately re-announce so peers see the new name without waiting up to 5 s.
    if (d->socket)
        d->sendDatagram(d->socket,
            buildAnnounce(d->selfId, d->selfName, d->listenPort, d->selfFingerprint));
}
