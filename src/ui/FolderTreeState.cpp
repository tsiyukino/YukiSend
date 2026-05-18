#include "FolderTreeState.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QJsonObject>
#include <algorithm>

void FolderTreeState::init(const QString &rootPath) {
    m_rootPath = rootPath;
    m_rootName = QDir(rootPath).dirName();
    m_children.clear();
    m_expanded.clear();
    m_revealed.clear();
    m_ready = false;

    const QDir root(rootPath);
    if (!root.exists()) return;

    // Seed so root level always has an entry even if the folder is empty.
    m_children.insert(QString(), QList<DirEntry>());

    const QString rootAbs  = QDir::cleanPath(rootPath);
    const int     prefixLen = rootAbs.size() + 1;

    QDirIterator it(rootAbs,
                    QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString abs = QDir::cleanPath(it.next());
        const QFileInfo fi(abs);
        const QString rel       = abs.mid(prefixLen); // e.g. "sub/file.txt"
        const QString parentRel = rel.contains('/')
            ? rel.left(rel.lastIndexOf('/'))
            : QString();                               // root level → ""

        if (!m_children.contains(parentRel))
            m_children.insert(parentRel, QList<DirEntry>());

        m_children[parentRel].append({ fi.fileName(), rel, fi.isDir(), fi.size() });

        if (fi.isDir() && !m_children.contains(rel))
            m_children.insert(rel, QList<DirEntry>());
    }

    // Dirs first, then files; each group alphabetically.
    for (auto &list : m_children) {
        std::sort(list.begin(), list.end(), [](const DirEntry &a, const DirEntry &b) {
            if (a.isDir != b.isDir) return a.isDir > b.isDir;
            return a.name.toLower() < b.name.toLower();
        });
    }

    // Auto-expand root if it has few enough direct children.
    const int rootChildCount = m_children.value(QString()).size();
    if (rootChildCount <= kTreeAutoExpand)
        m_expanded.insert(QString()); // "" = root is expanded

    for (const QString &key : m_expanded)
        m_revealed[key] = qMin(kTreeBulkSize, m_children.value(key).size());

    m_ready = true;
    rebuild();
}

void FolderTreeState::initFromJson(const QJsonArray &tree, const QString &rootName) {
    m_rootPath.clear();
    m_rootName = rootName;
    m_children.clear();
    m_expanded.clear();
    m_revealed.clear();
    m_ready = false;

    m_children.insert(QString(), QList<DirEntry>());

    for (const QJsonValue &v : tree) {
        const QJsonObject obj  = v.toObject();
        const QString rel      = obj[QStringLiteral("p")].toString();
        const bool    isDir    = obj[QStringLiteral("d")].toBool();
        const qint64  size     = static_cast<qint64>(obj[QStringLiteral("s")].toDouble());
        const QString name     = rel.contains('/')
            ? rel.mid(rel.lastIndexOf('/') + 1)
            : rel;
        const QString parentRel = rel.contains('/')
            ? rel.left(rel.lastIndexOf('/'))
            : QString();

        if (!m_children.contains(parentRel))
            m_children.insert(parentRel, QList<DirEntry>());
        m_children[parentRel].append({ name, rel, isDir, size });

        if (isDir && !m_children.contains(rel))
            m_children.insert(rel, QList<DirEntry>());
    }

    for (auto &list : m_children) {
        std::sort(list.begin(), list.end(), [](const DirEntry &a, const DirEntry &b) {
            if (a.isDir != b.isDir) return a.isDir > b.isDir;
            return a.name.toLower() < b.name.toLower();
        });
    }

    const int rootChildCount = m_children.value(QString()).size();
    if (rootChildCount <= kTreeAutoExpand)
        m_expanded.insert(QString());

    for (const QString &key : m_expanded)
        m_revealed[key] = qMin(kTreeBulkSize, m_children.value(key).size());

    m_ready = true;
    rebuild();
}

void FolderTreeState::toggle(const QString &relPath) {
    if (m_expanded.contains(relPath)) {
        m_expanded.remove(relPath);
    } else {
        m_expanded.insert(relPath);
        if (!m_revealed.contains(relPath))
            m_revealed[relPath] = qMin(kTreeBulkSize, m_children.value(relPath).size());
    }
    rebuild();
}

void FolderTreeState::loadMore(const QString &parentPath) {
    const int total   = m_children.value(parentPath).size();
    const int current = m_revealed.value(parentPath, 0);
    m_revealed[parentPath] = qMin(current + kTreeBulkSize, total);
    rebuild();
}

void FolderTreeState::rebuild() {
    m_visible.rows.clear();
    m_visible.moreAt.clear();

    // Row 0 is always the root directory itself.
    TreeRow root;
    root.name     = m_rootName;
    root.relPath  = QString(); // "" = root
    root.depth    = 0;
    root.isDir    = true;
    root.size     = 0;
    root.expanded = m_expanded.contains(QString());
    m_visible.rows.append(root);

    // Children of root (and their subtrees) follow if root is expanded.
    appendRows(QString(), 1, m_visible.rows, m_visible.moreAt);
}

void FolderTreeState::appendRows(const QString &parentPath, int depth,
                                  QList<TreeRow> &rows,
                                  QHash<int, TreeMore> &moreAt) const
{
    if (!m_expanded.contains(parentPath)) return;

    const QList<DirEntry> &children = m_children.value(parentPath);
    const int total    = children.size();
    const int revealed = qMin(m_revealed.value(parentPath, kTreeBulkSize), total);

    for (int i = 0; i < revealed; ++i) {
        const DirEntry &e = children[i];
        TreeRow row;
        row.name     = e.name;
        row.relPath  = e.relPath;
        row.depth    = depth;
        row.isDir    = e.isDir;
        row.size     = e.size;
        row.expanded = e.isDir && m_expanded.contains(e.relPath);
        rows.append(row);

        if (e.isDir)
            appendRows(e.relPath, depth + 1, rows, moreAt);
    }

    if (revealed < total) {
        TreeMore more;
        more.parentPath = parentPath;
        more.shown      = revealed;
        more.total      = total;
        moreAt.insert(rows.size(), more);
    }
}
