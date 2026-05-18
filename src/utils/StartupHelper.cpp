#include "StartupHelper.h"

#ifdef Q_OS_WIN
#include <QCoreApplication>
#include <QString>
#include <windows.h>

static constexpr const wchar_t *kRunKey =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
static constexpr const wchar_t *kAppName = L"YukiSend";

bool StartupHelper::launchAtStartup() {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_READ, &key) != ERROR_SUCCESS)
        return false;
    const bool exists = (RegQueryValueExW(key, kAppName, nullptr, nullptr, nullptr, nullptr)
                         == ERROR_SUCCESS);
    RegCloseKey(key);
    return exists;
}

void StartupHelper::setLaunchAtStartup(bool enable) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, &key) != ERROR_SUCCESS)
        return;
    if (enable) {
        // Use the path of the running executable.
        const QString exePath = QCoreApplication::applicationFilePath()
                                    .replace(QLatin1Char('/'), QLatin1Char('\\'));
        // Quote the path in case it contains spaces.
        const QString quoted  = QLatin1Char('"') + exePath + QLatin1Char('"');
        const std::wstring ws = quoted.toStdWString();
        RegSetValueExW(key, kAppName, 0, REG_SZ,
                       reinterpret_cast<const BYTE *>(ws.c_str()),
                       static_cast<DWORD>((ws.size() + 1) * sizeof(wchar_t)));
    } else {
        RegDeleteValueW(key, kAppName);
    }
    RegCloseKey(key);
}

#else

bool StartupHelper::launchAtStartup()          { return false; }
void StartupHelper::setLaunchAtStartup(bool)   {}

#endif
