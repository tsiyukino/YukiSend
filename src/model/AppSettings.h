#pragma once

#include <QString>

#include "CloseAction.h"
#include "ChatStore.h"   // StorageStrategy

// Thin wrapper around QSettings for app-level preferences.
// Only this class reads/writes QSettings — all other code goes through here.
class AppSettings {
public:
    AppSettings();

    CloseAction closeAction() const;
    void        setCloseAction(CloseAction action);

    // Stable device identity UUID — generated once, persisted forever.
    // All peers see this as our ID; it must not change across restarts
    // or saved/offline peer deduplication breaks.
    QString selfId() const;   // generates + saves one if not yet set

    // Stable fingerprint UUID — used for peer trust verification.
    // Distinct from selfId so identity and trust can evolve independently.
    // Broadcast in UDP discovery packets; receivers store it in known_peers.
    // FUTURE: Replace UUID with an Ed25519 public key. See FingerprintDialog.cpp
    //         for the full upgrade path.
    QString fingerprint() const;  // generates + saves one if not yet set

    // Display name broadcast to peers. Falls back to OS hostname if unset.
    QString displayName() const;
    void    setDisplayName(const QString &name);

    // Directory where received files are saved. Empty string = use system default.
    QString downloadDir() const;
    void    setDownloadDir(const QString &dir);

    // Whether to register the app in the OS login-items / startup list.
    bool launchAtStartup() const;
    void setLaunchAtStartup(bool enable);

    // Global default for message retention.
    StorageStrategy defaultStorageStrategy() const;
    void            setDefaultStorageStrategy(StorageStrategy s);

private:
    // QSettings is constructed on demand to avoid a persistent object.
    static constexpr const char *kOrg             = "YukiSend";
    static constexpr const char *kApp             = "YukiSend";
    static constexpr const char *kKeyClose        = "window/closeAction";
    static constexpr const char *kKeySelfId       = "identity/selfId";
    static constexpr const char *kKeyFingerprint  = "identity/fingerprint";
    static constexpr const char *kKeyName         = "identity/displayName";
    static constexpr const char *kKeyDownload     = "transfer/downloadDir";
    static constexpr const char *kKeyStartup      = "system/launchAtStartup";
    static constexpr const char *kKeyStrategy     = "messages/defaultStrategy";
};
