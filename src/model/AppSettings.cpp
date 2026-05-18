#include "AppSettings.h"

#include <QDir>
#include <QHostInfo>
#include <QStandardPaths>
#include <QUuid>

#include "utils/StartupHelper.h"

AppSettings::AppSettings() = default;

// ── Bootstrap helpers ─────────────────────────────────────────────────────────

QString AppSettings::resolveDataDir() {
    QSettings reg{QLatin1StringView(kOrg), QLatin1StringView(kApp)};
    const QString stored = reg.value(QLatin1StringView(kKeyDataDir)).toString();
    if (!stored.isEmpty()) return stored;
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
}

// Build the INI path and run the one-time migration if needed.
// Returns the path to the INI file (callers open QSettings themselves).
QString AppSettings::iniPath() {
    const QString dir = resolveDataDir();
    QDir().mkpath(dir);
    return dir + QLatin1StringView("/yukisend.ini");
}

void AppSettings::migrateOnce(QSettings &ini) {
    if (ini.contains(QLatin1StringView(kKeySelfId))) return;

    // First run after upgrade from registry-based build: copy keys then clear them.
    QSettings reg{QLatin1StringView(kOrg), QLatin1StringView(kApp)};
    const QStringList legacyKeys = {
        QLatin1StringView(kKeyClose),
        QLatin1StringView(kKeySelfId),
        QLatin1StringView(kKeyFingerprint),
        QLatin1StringView(kKeyName),
        QLatin1StringView(kKeyDownload),
        QLatin1StringView(kKeyStrategy),
    };
    for (const QString &key : legacyKeys) {
        if (reg.contains(key))
            ini.setValue(key, reg.value(key));
    }
    for (const QString &key : legacyKeys)
        reg.remove(key);
}

// ── dataDir ───────────────────────────────────────────────────────────────────

QString AppSettings::dataDir() const {
    return resolveDataDir();
}

void AppSettings::setDataDir(const QString &dir) {
    QSettings reg{QLatin1StringView(kOrg), QLatin1StringView(kApp)};
    if (dir.isEmpty())
        reg.remove(QLatin1StringView(kKeyDataDir));
    else
        reg.setValue(QLatin1StringView(kKeyDataDir), dir);
}

// ── Identity ──────────────────────────────────────────────────────────────────

QString AppSettings::selfId() const {
    QSettings s(iniPath(), QSettings::IniFormat);
    migrateOnce(s);
    QString id = s.value(QLatin1StringView(kKeySelfId)).toString();
    if (id.isEmpty()) {
        id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        s.setValue(QLatin1StringView(kKeySelfId), id);
    }
    return id;
}

QString AppSettings::fingerprint() const {
    QSettings s(iniPath(), QSettings::IniFormat);
    migrateOnce(s);
    QString fp = s.value(QLatin1StringView(kKeyFingerprint)).toString();
    if (fp.isEmpty()) {
        fp = QUuid::createUuid().toString(QUuid::WithoutBraces);
        s.setValue(QLatin1StringView(kKeyFingerprint), fp);
    }
    return fp;
}

// ── Window ────────────────────────────────────────────────────────────────────

CloseAction AppSettings::closeAction() const {
    QSettings s(iniPath(), QSettings::IniFormat);
    migrateOnce(s);
    const int v = s.value(QLatin1StringView(kKeyClose), -1).toInt();
    switch (v) {
        case static_cast<int>(CloseAction::MinimizeToTray): return CloseAction::MinimizeToTray;
        case static_cast<int>(CloseAction::Quit):           return CloseAction::Quit;
        default:                                            return CloseAction::Unset;
    }
}

void AppSettings::setCloseAction(CloseAction action) {
    QSettings s(iniPath(), QSettings::IniFormat);
    s.setValue(QLatin1StringView(kKeyClose), static_cast<int>(action));
}

// ── Display name ──────────────────────────────────────────────────────────────

QString AppSettings::displayName() const {
    QSettings s(iniPath(), QSettings::IniFormat);
    migrateOnce(s);
    return s.value(QLatin1StringView(kKeyName), QHostInfo::localHostName()).toString();
}

void AppSettings::setDisplayName(const QString &name) {
    QSettings s(iniPath(), QSettings::IniFormat);
    if (name.trimmed().isEmpty())
        s.remove(QLatin1StringView(kKeyName));
    else
        s.setValue(QLatin1StringView(kKeyName), name.trimmed());
}

// ── Download dir ──────────────────────────────────────────────────────────────

QString AppSettings::downloadDir() const {
    QSettings s(iniPath(), QSettings::IniFormat);
    migrateOnce(s);
    return s.value(QLatin1StringView(kKeyDownload), QString{}).toString();
}

void AppSettings::setDownloadDir(const QString &dir) {
    QSettings s(iniPath(), QSettings::IniFormat);
    if (dir.isEmpty())
        s.remove(QLatin1StringView(kKeyDownload));
    else
        s.setValue(QLatin1StringView(kKeyDownload), dir);
}

// ── Startup ───────────────────────────────────────────────────────────────────

bool AppSettings::launchAtStartup() const {
    return StartupHelper::launchAtStartup();
}

void AppSettings::setLaunchAtStartup(bool enable) {
    StartupHelper::setLaunchAtStartup(enable);
}

// ── Storage strategy ──────────────────────────────────────────────────────────

StorageStrategy AppSettings::defaultStorageStrategy() const {
    QSettings s(iniPath(), QSettings::IniFormat);
    migrateOnce(s);
    const int v = s.value(QLatin1StringView(kKeyStrategy),
                          static_cast<int>(StorageStrategy::Persistent)).toInt();
    return (v == static_cast<int>(StorageStrategy::SessionOnly))
        ? StorageStrategy::SessionOnly
        : StorageStrategy::Persistent;
}

void AppSettings::setDefaultStorageStrategy(StorageStrategy st) {
    QSettings s(iniPath(), QSettings::IniFormat);
    s.setValue(QLatin1StringView(kKeyStrategy), static_cast<int>(st));
}
