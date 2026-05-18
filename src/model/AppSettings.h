#pragma once

#include <QString>
#include <QSettings>

#include "CloseAction.h"
#include "ChatStore.h"   // StorageStrategy

// Thin wrapper around QSettings for app-level preferences.
// Only this class reads/writes QSettings — all other code goes through here.
//
// Storage layout:
//   Registry (bootstrap only): "YukiSend/YukiSend/data/dataDir"
//   INI file:  <dataDir>/yukisend.ini — all other keys
// On first run after upgrade the registry keys are migrated to INI automatically.
class AppSettings {
public:
    AppSettings();

    CloseAction closeAction() const;
    void        setCloseAction(CloseAction action);

    // Stable device identity UUID — generated once, persisted forever.
    QString selfId() const;

    // Stable fingerprint UUID — used for peer trust verification.
    QString fingerprint() const;

    // Display name broadcast to peers. Falls back to OS hostname if unset.
    QString displayName() const;
    void    setDisplayName(const QString &name);

    // Directory where received files are saved. Empty string = use system default.
    QString downloadDir() const;
    void    setDownloadDir(const QString &dir);

    // Root directory for all app data (DB, cache). Changing takes effect after restart.
    // Default: QStandardPaths::AppDataLocation.
    QString dataDir() const;
    void    setDataDir(const QString &dir);

    // Whether to register the app in the OS login-items / startup list.
    bool launchAtStartup() const;
    void setLaunchAtStartup(bool enable);

    // Global default for message retention.
    StorageStrategy defaultStorageStrategy() const;
    void            setDefaultStorageStrategy(StorageStrategy s);

private:
    static constexpr const char *kOrg            = "YukiSend";
    static constexpr const char *kApp            = "YukiSend";
    // Registry-only key (bootstrap — stores dataDir so we can locate the INI):
    static constexpr const char *kKeyDataDir     = "data/dataDir";
    // INI keys:
    static constexpr const char *kKeyClose       = "window/closeAction";
    static constexpr const char *kKeySelfId      = "identity/selfId";
    static constexpr const char *kKeyFingerprint = "identity/fingerprint";
    static constexpr const char *kKeyName        = "identity/displayName";
    static constexpr const char *kKeyDownload    = "transfer/downloadDir";
    static constexpr const char *kKeyStartup     = "system/launchAtStartup";
    static constexpr const char *kKeyStrategy    = "messages/defaultStrategy";

    // Returns the active data directory (from registry, or system default).
    static QString resolveDataDir();

    // Returns the path to the INI file (creates the directory if needed).
    static QString iniPath();

    // Runs the one-time migration from registry to INI (no-op after first run).
    static void migrateOnce(QSettings &ini);
};
