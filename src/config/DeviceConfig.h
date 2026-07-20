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

    // What the lock screen tells a stranded customer about buying an unlock code —
    // set once, business-wide, by the operator. Empty by default; the provider then
    // falls back to the portal URL, and to a generic vendor line when even that is
    // unknown. Read as SYSTEM at the logon screen, so it lives in the machine hive.
    static std::string unlockInstructions();
    static void setUnlockInstructions(const std::string& instructions);

    // The support line shown on the lock screen. Empty means the built-in business
    // number; settable per fleet the same way as the instructions.
    static std::string supportContact();
    static void setSupportContact(const std::string& contact);

    // The device's own account number, minted here so it exists before any portal
    // knows about this machine: an operator reads it out of the app and registers the
    // device with it. Not a secret — the customer reads it off the lock screen — so
    // unlike the uninstall code it is stored in the clear.
    //
    // The random space is large enough that unco-ordinated devices do not collide;
    // see generateAccountNumber. ensureAccountNumber keeps any number already issued.
    static std::string ensureAccountNumber();
    static std::string accountNumber();
    static void setAccountNumber(const std::string& accountNumber);

    // Why the last privileged action failed, in the portal's own words. The app has to
    // launch the installer through ShellExecute to get a UAC prompt, and that cannot
    // capture output — so without somewhere to leave the reason, every failure looks
    // the same to the person reading the screen.
    static void setLastError(const std::string& message);
    static std::string lastError();
    static void clearLastError();

    static bool uninstallProtected();

    // The removal-authorization code is generated here, on the device, so it is
    // unique to this machine and exists before the device is known to any portal.
    // An operator reads it out of the app and records it against the device on the
    // portal; storing it is what arms protection.
    static std::string ensureUninstallCode();
    static std::string uninstallCode();

    static void setUninstallCode(const std::string& code);
    static bool checkUninstallCode(const std::string& code);
    static void clearUninstallCode();

    static void removeAll();
};

}
