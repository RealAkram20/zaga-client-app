#pragma once

#include <string>

namespace zaga {

// Machine-wide enforcement settings, kept in the registry so the credential
// provider can read them as SYSTEM at the logon screen. Both switches default to
// off: a freshly installed client neither gates login nor resists removal until it
// is deliberately armed.
class DeviceConfig {
public:
    static bool lockEnabled();
    static void setLockEnabled(bool enabled);

    static std::string portalUrl();
    static void setPortalUrl(const std::string& url);

    static bool uninstallProtected();

    static void setUninstallCode(const std::string& code);
    static bool checkUninstallCode(const std::string& code);
    static void clearUninstallCode();

    static void removeAll();
};

}
