#include "OutboxStore.h"

#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QDir>
#include <QDateTime>
#include <QJsonDocument>
#include <QDebug>

struct OutboxStore::Private {
    QSqlDatabase db;

    bool exec(const QString &sql, const QVariantList &binds = {}) {
        QSqlQuery q(db);
        q.prepare(sql);
        for (int i = 0; i < binds.size(); ++i)
            q.bindValue(i, binds[i]);
        if (!q.exec()) {
            qWarning() << "OutboxStore:" << q.lastError().text();
            return false;
        }
        return true;
    }
};

OutboxStore::OutboxStore(const QString &dir, QObject *parent)
    : QObject(parent), d(new Private)
{
    QDir().mkpath(dir);
    const QString dbPath = dir + QStringLiteral("/yukisend.db");

    d->db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                      QStringLiteral("outboxstore"));
    d->db.setDatabaseName(dbPath);
    if (!d->db.open()) {
        qWarning() << "OutboxStore: cannot open db:" << d->db.lastError().text();
        return;
    }

    d->exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS outbox ("
        "  id          INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  peer_id     TEXT NOT NULL,"
        "  type        INTEGER NOT NULL,"
        "  payload     TEXT NOT NULL DEFAULT '{}',"
        "  created_at  INTEGER NOT NULL"
        ")"));
    d->exec(QStringLiteral(
        "CREATE INDEX IF NOT EXISTS idx_outbox_peer ON outbox (peer_id)"));
}

OutboxStore::~OutboxStore() {
    const QString name = d->db.connectionName();
    d->db.close();
    delete d;
    QSqlDatabase::removeDatabase(name);
}

qint64 OutboxStore::enqueue(const QString &peerId,
                             Protocol::MessageType type,
                             const QJsonObject &payload)
{
    const QByteArray json = QJsonDocument(payload).toJson(QJsonDocument::Compact);
    QSqlQuery q(d->db);
    q.prepare(QStringLiteral(
        "INSERT INTO outbox (peer_id, type, payload, created_at)"
        " VALUES (?, ?, ?, ?)"));
    q.bindValue(0, peerId);
    q.bindValue(1, static_cast<int>(type));
    q.bindValue(2, QString::fromUtf8(json));
    q.bindValue(3, QDateTime::currentMSecsSinceEpoch());
    if (!q.exec()) {
        qWarning() << "OutboxStore::enqueue failed:" << q.lastError().text();
        return -1;
    }
    return q.lastInsertId().toLongLong();
}

QList<OutboxEntry> OutboxStore::dequeue(const QString &peerId) {
    QList<OutboxEntry> result;
    QSqlQuery q(d->db);
    q.prepare(QStringLiteral(
        "SELECT id, type, payload, created_at FROM outbox"
        " WHERE peer_id=? ORDER BY id ASC"));
    q.bindValue(0, peerId);
    if (!q.exec()) return result;

    while (q.next()) {
        OutboxEntry e;
        e.id        = q.value(0).toLongLong();
        e.peerId    = peerId;
        e.type      = static_cast<Protocol::MessageType>(q.value(1).toInt());
        e.payload   = QJsonDocument::fromJson(
                          q.value(2).toString().toUtf8()).object();
        e.createdAt = q.value(3).toLongLong();
        result.append(e);
    }

    // Delete all dequeued rows.
    QSqlQuery del(d->db);
    del.prepare(QStringLiteral("DELETE FROM outbox WHERE peer_id=?"));
    del.bindValue(0, peerId);
    del.exec();

    return result;
}

void OutboxStore::remove(qint64 id) {
    d->exec(QStringLiteral("DELETE FROM outbox WHERE id=?"), { id });
}

int OutboxStore::pendingCount(const QString &peerId) const {
    QSqlQuery q(d->db);
    q.prepare(QStringLiteral("SELECT COUNT(*) FROM outbox WHERE peer_id=?"));
    q.bindValue(0, peerId);
    if (!q.exec() || !q.next()) return 0;
    return q.value(0).toInt();
}
