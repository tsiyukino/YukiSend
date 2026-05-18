#include "ChatStore.h"

#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QDir>
#include <QDebug>
#include <QJsonDocument>

static constexpr const char *kDbName = "yukisend_chat";

struct ChatStore::Private {
    QSqlDatabase     db;
    StorageStrategy  defaultStrategy = StorageStrategy::SessionOnly;
    QMap<QString, StorageStrategy> peerStrategy;

    bool exec(QSqlQuery &q) {
        if (!q.exec()) {
            qWarning() << "[ChatStore]" << q.lastError().text();
            return false;
        }
        return true;
    }
};

// ── Schema ───────────────────────────────────────────────────────────────────

static void ensureSchema(QSqlDatabase &db) {
    QSqlQuery q(db);
    q.exec(R"(
        CREATE TABLE IF NOT EXISTS messages (
            id               INTEGER PRIMARY KEY AUTOINCREMENT,
            peer_id          TEXT    NOT NULL,
            type             INTEGER NOT NULL,
            outgoing         INTEGER NOT NULL,
            text             TEXT,
            file_name        TEXT,
            file_path        TEXT,
            file_size        INTEGER DEFAULT 0,
            file_hash        TEXT,
            status           INTEGER NOT NULL DEFAULT 0,
            timestamp        INTEGER NOT NULL,
            bytes_transferred INTEGER DEFAULT 0,
            remote_msg_id    INTEGER DEFAULT -1,
            folder_tree      TEXT    DEFAULT NULL
        )
    )");
    // Migrations: add columns when upgrading from older databases.
    q.exec(R"(ALTER TABLE messages ADD COLUMN remote_msg_id INTEGER DEFAULT -1)");
    q.exec(R"(ALTER TABLE messages ADD COLUMN folder_tree TEXT DEFAULT NULL)");
    q.exec(R"(
        CREATE INDEX IF NOT EXISTS idx_messages_peer
        ON messages(peer_id, timestamp)
    )");
    q.exec(R"(
        CREATE TABLE IF NOT EXISTS peer_strategy (
            peer_id  TEXT PRIMARY KEY,
            strategy INTEGER NOT NULL DEFAULT 0
        )
    )");
}

// ── Helpers ──────────────────────────────────────────────────────────────────

static Message rowToMessage(const QSqlQuery &q) {
    Message m;
    m.id               = q.value(0).toLongLong();
    m.peerId           = q.value(1).toString();
    m.type             = static_cast<MessageType>(q.value(2).toInt());
    m.outgoing         = q.value(3).toBool();
    m.text             = q.value(4).toString();
    m.fileName         = q.value(5).toString();
    m.filePath         = q.value(6).toString();
    m.fileSize         = q.value(7).toLongLong();
    m.fileHash         = q.value(8).toString();
    m.status           = static_cast<MessageStatus>(q.value(9).toInt());
    m.timestamp        = QDateTime::fromMSecsSinceEpoch(q.value(10).toLongLong());
    m.bytesTransferred = q.value(11).toLongLong();
    m.remoteMsgId      = q.value(12).isNull() ? -1 : q.value(12).toLongLong();
    const QString treeJson = q.value(13).toString();
    if (!treeJson.isEmpty())
        m.folderTree = QJsonDocument::fromJson(treeJson.toUtf8()).array();
    return m;
}

// ── ChatStore ─────────────────────────────────────────────────────────────────

ChatStore::ChatStore(const QString &dir, QObject *parent)
    : QObject(parent), d(new Private)
{
    QDir().mkpath(dir);

    d->db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), kDbName);
    d->db.setDatabaseName(dir + QStringLiteral("/yukisend.db"));

    if (!d->db.open()) {
        qWarning() << "[ChatStore] Cannot open database:" << d->db.lastError().text();
        return;
    }

    ensureSchema(d->db);

    // Load persisted peer strategies
    QSqlQuery q(d->db);
    q.exec(QStringLiteral("SELECT peer_id, strategy FROM peer_strategy"));
    while (q.next()) {
        d->peerStrategy[q.value(0).toString()] =
            static_cast<StorageStrategy>(q.value(1).toInt());
    }
}

ChatStore::~ChatStore() {
    d->db.close();
    QSqlDatabase::removeDatabase(kDbName);
    delete d;
}

// ── Strategy ─────────────────────────────────────────────────────────────────

void ChatStore::setStrategy(const QString &peerId, StorageStrategy s) {
    d->peerStrategy[peerId] = s;
    QSqlQuery q(d->db);
    q.prepare(QStringLiteral(
        "INSERT OR REPLACE INTO peer_strategy(peer_id, strategy) VALUES(?,?)"));
    q.addBindValue(peerId);
    q.addBindValue(static_cast<int>(s));
    d->exec(q);
}

StorageStrategy ChatStore::strategy(const QString &peerId) const {
    return d->peerStrategy.value(peerId, d->defaultStrategy);
}

void ChatStore::setDefaultStrategy(StorageStrategy s) {
    d->defaultStrategy = s;
}

StorageStrategy ChatStore::defaultStrategy() const {
    return d->defaultStrategy;
}

// ── Messages ─────────────────────────────────────────────────────────────────

qint64 ChatStore::addMessage(const Message &msg) {
    QSqlQuery q(d->db);
    q.prepare(QStringLiteral(R"(
        INSERT INTO messages
            (peer_id, type, outgoing, text, file_name, file_path,
             file_size, file_hash, status, timestamp, bytes_transferred,
             remote_msg_id, folder_tree)
        VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?)
    )"));
    q.addBindValue(msg.peerId);
    q.addBindValue(static_cast<int>(msg.type));
    q.addBindValue(msg.outgoing ? 1 : 0);
    q.addBindValue(msg.text);
    q.addBindValue(msg.fileName);
    q.addBindValue(msg.filePath);
    q.addBindValue(msg.fileSize);
    q.addBindValue(msg.fileHash);
    q.addBindValue(static_cast<int>(msg.status));
    q.addBindValue(msg.timestamp.toMSecsSinceEpoch());
    q.addBindValue(msg.bytesTransferred);
    q.addBindValue(msg.remoteMsgId);
    if (msg.folderTree.isEmpty())
        q.addBindValue(QVariant());
    else
        q.addBindValue(QString::fromUtf8(
            QJsonDocument(msg.folderTree).toJson(QJsonDocument::Compact)));
    if (!d->exec(q)) return -1;
    return q.lastInsertId().toLongLong();
}

void ChatStore::updateMessage(const Message &msg) {
    QSqlQuery q(d->db);
    q.prepare(QStringLiteral(R"(
        UPDATE messages SET
            status=?, bytes_transferred=?, file_hash=?, file_path=?
        WHERE id=?
    )"));
    q.addBindValue(static_cast<int>(msg.status));
    q.addBindValue(msg.bytesTransferred);
    q.addBindValue(msg.fileHash);
    q.addBindValue(msg.filePath);
    q.addBindValue(msg.id);
    d->exec(q);
}

QList<Message> ChatStore::messages(const QString &peerId) const {
    QSqlQuery q(d->db);
    q.prepare(QStringLiteral(
        "SELECT id,peer_id,type,outgoing,text,file_name,file_path,"
        "file_size,file_hash,status,timestamp,bytes_transferred,remote_msg_id,folder_tree "
        "FROM messages WHERE peer_id=? ORDER BY timestamp ASC"));
    q.addBindValue(peerId);
    QList<Message> result;
    if (d->exec(q))
        while (q.next()) result.append(rowToMessage(q));
    return result;
}

// ── Lifecycle ─────────────────────────────────────────────────────────────────

void ChatStore::clearSession(const QString &peerId) {
    if (strategy(peerId) != StorageStrategy::SessionOnly) return;
    QSqlQuery q(d->db);
    q.prepare(QStringLiteral("DELETE FROM messages WHERE peer_id=?"));
    q.addBindValue(peerId);
    d->exec(q);
}

void ChatStore::clearPeer(const QString &peerId) {
    QSqlQuery q(d->db);
    q.prepare(QStringLiteral("DELETE FROM messages WHERE peer_id=?"));
    q.addBindValue(peerId);
    d->exec(q);
}

void ChatStore::clearAll() {
    QSqlQuery q(d->db);
    q.exec(QStringLiteral("DELETE FROM messages"));
}

QStringList ChatStore::peersWithHistory() const {
    QSqlQuery q(d->db);
    q.exec(QStringLiteral(
        "SELECT DISTINCT peer_id FROM messages ORDER BY peer_id ASC"));
    QStringList result;
    while (q.next())
        result.append(q.value(0).toString());
    return result;
}
