#pragma once

#include <QString>
#include <QSet>
#include <QList>
#include <QHash>
#include <QJsonArray>

// One rendered row in the folder tree.
struct TreeRow {
    QString name;         // display name (last path segment)
    QString relPath;      // key used for toggle/loadMore; "" = root node itself
    int     depth  = 0;  // 0 = root dir row, 1 = root's children, ...
    bool    isDir  = false;
    qint64  size   = 0;  // bytes; 0 for dirs
    bool    expanded = false;
};

// Sentinel row: user can click "… N more" to load the next bulk.
struct TreeMore {
    QString parentPath; // which directory's children are paged ("" = root)
    int     shown = 0;
    int     total = 0;
};

static constexpr int kTreeBulkSize   = 20; // items per bulk load
static constexpr int kTreeAutoExpand =  5; // auto-expand root if children <= this

// Per-message folder tree state.
// The root folder itself is always row 0 (depth=0). Its children appear below
// when expanded. Sub-folders follow the same pattern at deeper depths.
class FolderTreeState {
public:
    // Init from the local filesystem (sender side, or receiver after Done).
    void init(const QString &rootPath);

    // Init from a JSON array sent by the peer (receiver side, WaitingAccept).
    // Each element: { "p": relPath, "d": isDir, "s": size }
    // rootName is the folder display name (msg.fileName).
    void initFromJson(const QJsonArray &tree, const QString &rootName);

    bool    ready()    const { return m_ready; }
    QString rootPath() const { return m_rootPath; }
    QString rootName() const { return m_rootName; }

    void toggle(const QString &relPath);   // relPath="" → toggle root
    void loadMore(const QString &parentPath);

    struct VisibleList {
        QList<TreeRow>       rows;
        QHash<int, TreeMore> moreAt; // index into rows where a "more" sentinel follows
    };
    const VisibleList &visible() const { return m_visible; }
    void rebuild();

private:
    struct DirEntry {
        QString name;
        QString relPath;
        bool    isDir;
        qint64  size;
    };

    QHash<QString, QList<DirEntry>> m_children; // "" = root's children
    QSet<QString>                   m_expanded;
    QHash<QString, int>             m_revealed;  // how many children are shown

    QString     m_rootPath;
    QString     m_rootName;
    bool        m_ready = false;
    VisibleList m_visible;

    void appendRows(const QString &parentPath, int depth,
                    QList<TreeRow> &rows, QHash<int, TreeMore> &moreAt) const;
};
