#pragma once

#include <QHash>
#include <QImage>
#include <QList>
#include <QString>

// Two-layer thumbnail cache: a small in-memory LRU backed by per-message JPEG
// files on disk.  Memory holds at most kMaxMemSlots decoded QImages; when full,
// the least-recently-used entry is encoded to JPEG and written to disk before
// being evicted.  Disk files persist across restarts.
//
// Cache directory: AppDataLocation/thumbcache/<msgId>.jpg
// Not thread-safe — must be called from the main thread only.
class ThumbCache {
public:
    ThumbCache();

    // Return the thumbnail for msgId, or a null QImage if not present.
    // Promotes the entry to MRU position on a memory hit.
    // On a memory miss, attempts to load from disk and re-inserts into memory.
    QImage get(qint64 msgId) const;

    // Store img for msgId in memory.  Null images are ignored.
    // Evicts the LRU entry to disk first if memory is at capacity.
    void put(qint64 msgId, const QImage &img);

    // Remove msgId from both memory and disk.
    // Call when a message is deleted or its thumbnail must be rebuilt.
    void invalidate(qint64 msgId);

    // Evict all entries from memory; disk files are kept.
    // Use on peer switch — files remain valid for the next load.
    void clearMemory();

    // Remove all entries from memory and delete all disk files.
    // Use when the user clears history.
    void clearAll();

private:
    static constexpr int kMaxMemSlots = 20;

    QString diskPath(qint64 msgId) const;
    void    evictLruToDisk() const;

    // m_order: front = MRU, back = LRU.
    mutable QList<qint64>         m_order;
    mutable QHash<qint64, QImage> m_mem;
    QString                       m_cacheDir;
};
