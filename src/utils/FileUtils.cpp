#include "FileUtils.h"

#include <QFileInfo>
#include <QDir>
#include <QDirIterator>

namespace FileUtils {

QString formatSize(qint64 bytes) {
    if (bytes < 1024)
        return QString::number(bytes) + " B";
    if (bytes < 1024 * 1024)
        return QString::number(bytes / 1024.0, 'f', 1) + " KB";
    if (bytes < 1024 * 1024 * 1024)
        return QString::number(bytes / (1024.0 * 1024), 'f', 1) + " MB";
    return QString::number(bytes / (1024.0 * 1024 * 1024), 'f', 2) + " GB";
}

QString fileName(const QString &path) {
    return QFileInfo(path).fileName();
}

// Returns every file AND directory inside `dir`, recursively.
// relPath always starts with the root folder name itself, e.g.:
//   root = /home/user/Photos
//   file = /home/user/Photos/2024/cat.jpg  → relPath = "Photos/2024/cat.jpg"
//   dir  = /home/user/Photos/2024          → relPath = "Photos/2024",  isDir = true
//   root itself is NOT included as an entry (the receiver creates it via the
//   first relPath segment anyway).
QList<FileEntry> listFiles(const QString &dir) {
    QList<FileEntry> result;
    const QDir root(dir);
    const QString rootName = root.dirName();          // e.g. "Photos"
    const QString rootAbs  = QDir::cleanPath(dir);    // e.g. "/home/user/Photos"
    const int     prefixLen = rootAbs.size() + 1;     // strip up to and including "/"

    // Enumerate both files and sub-directories, recursively.
    QDirIterator it(rootAbs,
                    QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString abs = QDir::cleanPath(it.next());
        const QFileInfo fi(abs);
        // relPath = "Photos/sub/file.txt"
        const QString rel = rootName + QStringLiteral("/") + abs.mid(prefixLen);
        result.append({ rel, abs, fi.size(), fi.isDir() });
    }

    // Sort so that parent directories always come before their children —
    // the receiver must mkpath in order.
    std::sort(result.begin(), result.end(), [](const FileEntry &a, const FileEntry &b) {
        return a.relPath < b.relPath;
    });

    return result;
}

qint64 totalSize(const QList<FileEntry> &entries) {
    qint64 total = 0;
    for (const auto &e : entries) total += e.size;
    return total;
}

} // namespace FileUtils
