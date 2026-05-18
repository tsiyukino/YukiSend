#include "PeerStore.h"

#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QDir>
#include <QDateTime>
#include <QDebug>

struct PeerStore::Private {
    QSqlDatabase db;

    bool exec(const QString &sql, const QVariantList &binds = {}) {
        QSqlQuery q(db);
        q.prepare(sql);
        for (int i = 0; i < binds.size(); ++i)
            q.bindValue(i, binds[i]);
        if (!q.exec()) {
            qWarning() << "PeerStore:" << q.lastError().text() << sql;
            return false;
        }
        return true;
    }
};

PeerStore::PeerStore(const QString &dir, QObject *parent)
    : QObject(parent), d(new Private)
{
    QDir().mkpath(dir);
    const QString dbPath = dir + QStringLiteral("/yukisend.db");

    d->db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                      QStringLiteral("peerstore"));
    d->db.setDatabaseName(dbPath);
    if (!d->db.open()) {
        qWarning() << "PeerStore: cannot open db:" << d->db.lastError().text();
        return;
    }

    d->exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS saved_peers ("
        "  peer_id      TEXT PRIMARY KEY,"
        "  display_name TEXT NOT NULL DEFAULT '',"
        "  address      TEXT NOT NULL DEFAULT '',"
        "  port         INTEGER NOT NULL DEFAULT 0,"
        "  is_favorite  INTEGER NOT NULL DEFAULT 0,"
        "  is_blocked   INTEGER NOT NULL DEFAULT 0,"
        "  is_saved     INTEGER NOT NULL DEFAULT 0"
        ")"));

    d->exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS known_peers ("
        "  peer_id     TEXT PRIMARY KEY,"
        "  fingerprint TEXT NOT NULL,"
        "  trusted_at  INTEGER NOT NULL"   // Unix ms timestamp
        ")"));
}

PeerStore::~PeerStore() {
    const QString name = d->db.connectionName();
    d->db.close();
    delete d;
    QSqlDatabase::removeDatabase(name);
}

// ── Internal helpers ──────────────────────────────────────────────────────────

// Ensure a row exists for this peer_id.
static void ensureRow(QSqlDatabase &db, const QString &peerId) {
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "INSERT OR IGNORE INTO saved_peers (peer_id) VALUES (?)"));
    q.bindValue(0, peerId);
    q.exec();
}

// ── Public API ────────────────────────────────────────────────────────────────

void PeerStore::setSaved(const QString &peerId, const QString &displayName,
                         const QString &address, quint16 port, bool saved)
{
    ensureRow(d->db, peerId);
    d->exec(QStringLiteral(
        "UPDATE saved_peers SET is_saved=?, display_name=?, address=?, port=?"
        " WHERE peer_id=?"),
        { saved ? 1 : 0, displayName, address, port, peerId });

    // If unsaving and also not favorited/blocked, remove the row entirely.
    if (!saved) {
        QSqlQuery q(d->db);
        q.prepare(QStringLiteral(
            "DELETE FROM saved_peers"
            " WHERE peer_id=? AND is_favorite=0 AND is_blocked=0"));
        q.bindValue(0, peerId);
        q.exec();
    }
}

void PeerStore::setFavorite(const QString &peerId, bool favorite) {
    ensureRow(d->db, peerId);
    d->exec(QStringLiteral(
        "UPDATE saved_peers SET is_favorite=?, is_saved=MAX(is_saved,?) WHERE peer_id=?"),
        { favorite ? 1 : 0, favorite ? 1 : 0, peerId });
}

void PeerStore::setBlocked(const QString &peerId, bool blocked) {
    ensureRow(d->db, peerId);
    d->exec(QStringLiteral(
        "UPDATE saved_peers SET is_blocked=? WHERE peer_id=?"),
        { blocked ? 1 : 0, peerId });
}

bool PeerStore::isSaved(const QString &peerId) const {
    QSqlQuery q(d->db);
    q.prepare(QStringLiteral(
        "SELECT is_saved FROM saved_peers WHERE peer_id=?"));
    q.bindValue(0, peerId);
    return q.exec() && q.next() && q.value(0).toInt() == 1;
}

bool PeerStore::isFavorite(const QString &peerId) const {
    QSqlQuery q(d->db);
    q.prepare(QStringLiteral(
        "SELECT is_favorite FROM saved_peers WHERE peer_id=?"));
    q.bindValue(0, peerId);
    return q.exec() && q.next() && q.value(0).toInt() == 1;
}

bool PeerStore::isBlocked(const QString &peerId) const {
    QSqlQuery q(d->db);
    q.prepare(QStringLiteral(
        "SELECT is_blocked FROM saved_peers WHERE peer_id=?"));
    q.bindValue(0, peerId);
    return q.exec() && q.next() && q.value(0).toInt() == 1;
}

QList<Peer> PeerStore::savedPeers() const {
    QList<Peer> result;
    QSqlQuery q(d->db);
    q.prepare(QStringLiteral(
        "SELECT peer_id, display_name, address, port"
        " FROM saved_peers WHERE is_saved=1 AND is_blocked=0"));
    if (!q.exec()) return result;
    while (q.next()) {
        Peer p;
        p.id          = q.value(0).toString();
        p.displayName = q.value(1).toString();
        p.address     = q.value(2).toString();
        p.port        = static_cast<quint16>(q.value(3).toUInt());
        result.append(p);
    }
    return result;
}

QList<Peer> PeerStore::favoritePeers() const {
    QList<Peer> result;
    QSqlQuery q(d->db);
    q.prepare(QStringLiteral(
        "SELECT peer_id, display_name, address, port"
        " FROM saved_peers WHERE is_favorite=1 AND is_blocked=0"));
    if (!q.exec()) return result;
    while (q.next()) {
        Peer p;
        p.id          = q.value(0).toString();
        p.displayName = q.value(1).toString();
        p.address     = q.value(2).toString();
        p.port        = static_cast<quint16>(q.value(3).toUInt());
        result.append(p);
    }
    return result;
}

QList<Peer> PeerStore::blockedPeers() const {
    QList<Peer> result;
    QSqlQuery q(d->db);
    q.prepare(QStringLiteral(
        "SELECT peer_id, display_name, address, port"
        " FROM saved_peers WHERE is_blocked=1"));
    if (!q.exec()) return result;
    while (q.next()) {
        Peer p;
        p.id          = q.value(0).toString();
        p.displayName = q.value(1).toString();
        p.address     = q.value(2).toString();
        p.port        = static_cast<quint16>(q.value(3).toUInt());
        result.append(p);
    }
    return result;
}

void PeerStore::updatePeerInfo(const QString &peerId, const QString &displayName,
                               const QString &address, quint16 port)
{
    // Only update rows that already exist — don't auto-create.
    d->exec(QStringLiteral(
        "UPDATE saved_peers SET display_name=?, address=?, port=?"
        " WHERE peer_id=?"),
        { displayName, address, port, peerId });
}

QString PeerStore::peerIdByAddress(const QString &address) const {
    QSqlQuery q(d->db);
    q.prepare(QStringLiteral(
        "SELECT peer_id FROM saved_peers WHERE address=? LIMIT 1"));
    q.bindValue(0, address);
    if (q.exec() && q.next())
        return q.value(0).toString();
    return {};
}

// ── Fingerprint trust store ───────────────────────────────────────────────────

QString PeerStore::knownFingerprint(const QString &peerId) const {
    QSqlQuery q(d->db);
    q.prepare(QStringLiteral(
        "SELECT fingerprint FROM known_peers WHERE peer_id=?"));
    q.bindValue(0, peerId);
    if (q.exec() && q.next())
        return q.value(0).toString();
    return {};
}

void PeerStore::trustPeer(const QString &peerId, const QString &fingerprint) {
    d->exec(QStringLiteral(
        "INSERT INTO known_peers (peer_id, fingerprint, trusted_at)"
        " VALUES (?, ?, ?)"
        " ON CONFLICT(peer_id) DO UPDATE SET fingerprint=excluded.fingerprint,"
        "   trusted_at=excluded.trusted_at"),
        { peerId, fingerprint, QDateTime::currentMSecsSinceEpoch() });
}
