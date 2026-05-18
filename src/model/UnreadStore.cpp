#include "UnreadStore.h"

void UnreadStore::increment(const QString &peerId) {
    m_counts[peerId]++;
}

void UnreadStore::clear(const QString &peerId) {
    m_counts.remove(peerId);
}

int UnreadStore::count(const QString &peerId) const {
    return m_counts.value(peerId, 0);
}
