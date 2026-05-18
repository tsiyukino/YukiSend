#pragma once

#include <QObject>
#include <QString>
#include <QList>

#include "network/Discovery.h"  // Peer struct

// Persistent store for saved, favorited, and blocked peers.
// Backed by the shared yukisend.db SQLite database (table: saved_peers).
// A peer can be saved without being online — this enables offline display
// and queued delivery. Blocking a peer silently drops all incoming frames.
class PeerStore : public QObject {
    Q_OBJECT
public:
    explicit PeerStore(QObject *parent = nullptr);
    ~PeerStore() override;

    // Save or unsave a peer. Saving stores name/address/port for offline display.
    // Unsaving removes the record unless the peer is also favorited/blocked.
    void setSaved(const QString &peerId, const QString &displayName,
                  const QString &address, quint16 port, bool saved);

    // Favoriting implies saved. Unfavoriting does not unsave.
    void setFavorite(const QString &peerId, bool favorite);

    // Blocking does not imply saved. Blocked peers are hidden from the peer list.
    void setBlocked(const QString &peerId, bool blocked);

    bool isSaved(const QString &peerId)    const;
    bool isFavorite(const QString &peerId) const;
    bool isBlocked(const QString &peerId)  const;

    // All saved peers (for offline display in peer list).
    QList<Peer> savedPeers()    const;
    QList<Peer> favoritePeers() const;  // WHERE is_favorite=1 AND is_blocked=0
    QList<Peer> blockedPeers()  const;  // WHERE is_blocked=1

    // Upsert peer metadata (called when a saved peer comes online so we keep
    // the stored address/name fresh).
    void updatePeerInfo(const QString &peerId, const QString &displayName,
                        const QString &address, quint16 port);

    // Reverse-lookup: given an IP address string, return the peer_id of any
    // saved/blocked/favorited peer stored at that address. Returns empty string
    // if no match. Used to resolve incoming frames that arrive before the
    // discovery beacon has been processed.
    QString peerIdByAddress(const QString &address) const;

    // ── Fingerprint trust store ───────────────────────────────────────────────
    // Returns the trusted fingerprint for peerId, or empty string if unknown.
    QString knownFingerprint(const QString &peerId) const;
    // Records peerId as trusted with the given fingerprint. Overwrites if stored.
    void trustPeer(const QString &peerId, const QString &fingerprint);

private:
    struct Private;
    Private *d;
};
