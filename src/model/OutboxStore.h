#pragma once

#include <QObject>
#include <QString>
#include <QList>
#include <QJsonObject>

#include "network/Protocol.h"

// One entry waiting to be delivered to an offline peer.
struct OutboxEntry {
    qint64                 id;
    QString                peerId;
    Protocol::MessageType  type;
    QJsonObject            payload;   // same structure as live chat frames
    qint64                 createdAt; // msec since epoch
};

// Persistent queue of messages/files destined for offline saved peers.
// Backed by the shared yukisend.db SQLite database (table: outbox).
// When a peer comes online, App calls dequeue() to flush all pending entries.
class OutboxStore : public QObject {
    Q_OBJECT
public:
    explicit OutboxStore(const QString &dir, QObject *parent = nullptr);
    ~OutboxStore() override;

    // Append an entry. Returns the new row id.
    qint64 enqueue(const QString &peerId,
                   Protocol::MessageType type,
                   const QJsonObject &payload);

    // Return all pending entries for peerId and delete them from the table.
    QList<OutboxEntry> dequeue(const QString &peerId);

    // Explicitly remove a single entry (e.g. after delivery failure).
    void remove(qint64 id);

    // How many entries are waiting for this peer.
    int pendingCount(const QString &peerId) const;

private:
    struct Private;
    Private *d;
};
