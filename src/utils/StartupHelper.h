#pragma once

// Manages launch-at-startup registration for the current user.
// Windows: reads/writes HKCU\Software\Microsoft\Windows\CurrentVersion\Run.
// Other platforms: stubs that always return false / do nothing.
class StartupHelper {
public:
    StartupHelper() = delete;

    static bool launchAtStartup();
    static void setLaunchAtStartup(bool enable);
};
