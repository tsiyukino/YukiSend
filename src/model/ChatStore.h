#pragma once

#include <QObject>
#include <QList>
#include <QString>

#include "Message.h"

// Per-peer storage strategy.
// SessionOnly: messages are deleted when the peer sends a goodbye packet.
// Persistent:  messages survive across restarts.
enum class StorageStrategy { SessionOnly, Persistent };

// Owns the SQLite database for all chat history.
// One database file: <AppDataLocation>/yukisend.db
// Thread: must be used from the same thread it was created on (main thread).
class ChatStore : public QObject {
    Q_OBJECT
public:
    explicit ChatStore(QObject *parent = nullptr);
    ~ChatStore() override;

    // Strategy ────────────────────────────────────────────────────────────────
    void            setStrategy(const QString &peerId, StorageStrategy s);
    StorageStrategy strategy(const QString &peerId) const;
    void            setDefaultStrategy(StorageStrategy s);
    StorageStrategy defaultStrategy() const;

    // Messages ────────────────────────────────────────────────────────────────
    qint64          addMessage(const Message &msg);           // returns new id
    void            updateMessage(const Message &msg);        // matched by id
    QList<Message>  messages(const QString &peerId) const;

    // Lifecycle ───────────────────────────────────────────────────────────────
    void            clearSession(const QString &peerId);      // on peer goodbye
    void            clearPeer(const QString &peerId);         // unconditional delete
    void            clearAll();

    // Returns all peer_ids that have at least one message, for the history manager.
    QStringList     peersWithHistory() const;

private:
    struct Private;
    Private *d;
};
