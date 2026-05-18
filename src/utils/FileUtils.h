#pragma once

#include <QString>
#include <QList>

namespace FileUtils {

// Returns a human-readable file size string, e.g. "4.2 MB".
QString formatSize(qint64 bytes);

// Returns the file name portion of a path.
QString fileName(const QString &path);

// One entry from a recursive directory listing.
struct FileEntry {
    QString relPath; // path including root folder name, e.g. "Photos/sub/file.txt"
    QString absPath; // absolute path on the local filesystem
    qint64  size;    // file size in bytes (0 for directories)
    bool    isDir;   // true for directories (including empty ones)
};

// Recursively list all files inside dir.
// relPath uses '/' as separator regardless of platform.
QList<FileEntry> listFiles(const QString &dir);

// Total size of all entries returned by listFiles.
qint64 totalSize(const QList<FileEntry> &entries);

} // namespace FileUtils
