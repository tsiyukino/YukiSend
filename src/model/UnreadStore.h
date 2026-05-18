#pragma once

#include <QHash>
#include <QString>

// In-process unread message counts per peer. No signals — callers pull counts
// and call update() on the widget after mutating.
class UnreadStore {
public:
    void increment(const QString &peerId);
    void clear(const QString &peerId);
    int  count(const QString &peerId) const;

private:
    QHash<QString, int> m_counts;
};
