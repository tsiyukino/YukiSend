#include "AppSettings.h"

#include <QSettings>
#include <QHostInfo>
#include <QUuid>

#include "utils/StartupHelper.h"

AppSettings::AppSettings() = default;

QString AppSettings::selfId() const {
    QSettings s(kOrg, kApp);
    QString id = s.value(kKeySelfId).toString();
    if (id.isEmpty()) {
        id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        s.setValue(kKeySelfId, id);
    }
    return id;
}

QString AppSettings::fingerprint() const {
    QSettings s(kOrg, kApp);
    QString fp = s.value(kKeyFingerprint).toString();
    if (fp.isEmpty()) {
        fp = QUuid::createUuid().toString(QUuid::WithoutBraces);
        s.setValue(kKeyFingerprint, fp);
    }
    return fp;
}

CloseAction AppSettings::closeAction() const {
    QSettings s(kOrg, kApp);
    const int v = s.value(kKeyClose, -1).toInt();
    switch (v) {
        case static_cast<int>(CloseAction::MinimizeToTray): return CloseAction::MinimizeToTray;
        case static_cast<int>(CloseAction::Quit):           return CloseAction::Quit;
        default:                                            return CloseAction::Unset;
    }
}

void AppSettings::setCloseAction(CloseAction action) {
    QSettings s(kOrg, kApp);
    s.setValue(kKeyClose, static_cast<int>(action));
}

QString AppSettings::displayName() const {
    QSettings s(kOrg, kApp);
    return s.value(kKeyName, QHostInfo::localHostName()).toString();
}

void AppSettings::setDisplayName(const QString &name) {
    QSettings s(kOrg, kApp);
    if (name.trimmed().isEmpty())
        s.remove(kKeyName);  // revert to hostname fallback
    else
        s.setValue(kKeyName, name.trimmed());
}

QString AppSettings::downloadDir() const {
    QSettings s(kOrg, kApp);
    return s.value(kKeyDownload, QString{}).toString();
}

void AppSettings::setDownloadDir(const QString &dir) {
    QSettings s(kOrg, kApp);
    if (dir.isEmpty())
        s.remove(kKeyDownload);
    else
        s.setValue(kKeyDownload, dir);
}

bool AppSettings::launchAtStartup() const {
    // Source of truth is the OS registry, not QSettings.
    // QSettings key is kept in sync as a cache for the toggle UI state.
    return StartupHelper::launchAtStartup();
}

void AppSettings::setLaunchAtStartup(bool enable) {
    StartupHelper::setLaunchAtStartup(enable);
}

StorageStrategy AppSettings::defaultStorageStrategy() const {
    QSettings s(kOrg, kApp);
    const int v = s.value(kKeyStrategy,
                          static_cast<int>(StorageStrategy::Persistent)).toInt();
    return (v == static_cast<int>(StorageStrategy::SessionOnly))
        ? StorageStrategy::SessionOnly
        : StorageStrategy::Persistent;
}

void AppSettings::setDefaultStorageStrategy(StorageStrategy st) {
    QSettings s(kOrg, kApp);
    s.setValue(kKeyStrategy, static_cast<int>(st));
}
