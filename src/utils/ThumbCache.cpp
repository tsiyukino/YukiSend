#include "ThumbCache.h"

#include <QDir>
#include <QFile>
#include <QStandardPaths>

#include "ImageUtils.h"

ThumbCache::ThumbCache() {
    m_cacheDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
               + QStringLiteral("/thumbcache");
    QDir().mkpath(m_cacheDir);
}

QString ThumbCache::diskPath(qint64 msgId) const {
    return m_cacheDir + QStringLiteral("/") + QString::number(msgId) + QStringLiteral(".jpg");
}

void ThumbCache::evictLruToDisk() const {
    if (m_order.isEmpty()) return;
    const qint64 lruId = m_order.last();
    const QImage &img  = m_mem[lruId];
    if (!img.isNull()) {
        const QByteArray jpeg = ImageUtils::encodeJpeg(img, 85);
        QFile f(diskPath(lruId));
        if (f.open(QIODevice::WriteOnly))
            f.write(jpeg);
    }
    m_mem.remove(lruId);
    m_order.removeLast();
}

QImage ThumbCache::get(qint64 msgId) const {
    // Memory hit — promote to MRU.
    auto it = m_mem.find(msgId);
    if (it != m_mem.end()) {
        if (m_order.front() != msgId) {
            m_order.removeOne(msgId);
            m_order.prepend(msgId);
        }
        return it.value();
    }

    // Disk hit — load, insert into memory.
    const QString path = diskPath(msgId);
    if (!QFile::exists(path)) return {};

    QImage img;
    img.load(path);
    if (img.isNull()) return {};

    if (m_mem.size() >= kMaxMemSlots)
        evictLruToDisk();

    m_order.prepend(msgId);
    m_mem.insert(msgId, img);
    return img;
}

void ThumbCache::put(qint64 msgId, const QImage &img) {
    if (img.isNull()) return;

    // Already in memory — update and promote.
    if (m_mem.contains(msgId)) {
        m_mem[msgId] = img;
        m_order.removeOne(msgId);
        m_order.prepend(msgId);
        return;
    }

    if (m_mem.size() >= kMaxMemSlots)
        evictLruToDisk();

    m_order.prepend(msgId);
    m_mem.insert(msgId, img);
}

void ThumbCache::invalidate(qint64 msgId) {
    m_mem.remove(msgId);
    m_order.removeOne(msgId);
    QFile::remove(diskPath(msgId));
}

void ThumbCache::clearMemory() {
    m_mem.clear();
    m_order.clear();
}

void ThumbCache::clearAll() {
    m_mem.clear();
    m_order.clear();
    const QDir dir(m_cacheDir);
    const QStringList files = dir.entryList({QStringLiteral("*.jpg")}, QDir::Files);
    for (const QString &f : files)
        QFile::remove(dir.filePath(f));
}
