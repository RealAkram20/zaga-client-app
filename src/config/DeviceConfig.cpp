#include "DeviceConfig.h"

#include <windows.h>

#include <vector>

#include "DataProtection.h"

namespace zaga {

namespace {

const wchar_t SUBKEY[] = L"SOFTWARE\\Zaga";
const wchar_t VALUE_LOCK_ENABLED[] = L"LockEnabled";
const wchar_t VALUE_UNINSTALL_CODE[] = L"UninstallCode";

// Tests set ZAGA_CONFIG_HKCU so the round-trip can write under the current user
// instead of HKLM, which needs elevation. The provider never sets it, so at the
// logon screen the machine-wide HKLM view is always used.
HKEY rootKey() {
    wchar_t flag[8];
    if (GetEnvironmentVariableW(L"ZAGA_CONFIG_HKCU", flag, 8) > 0) {
        return HKEY_CURRENT_USER;
    }
    return HKEY_LOCAL_MACHINE;
}

DWORD readDword(const wchar_t* name, DWORD fallback) {
    DWORD value = 0;
    DWORD size = sizeof(value);
    DWORD type = 0;
    LONG result = RegGetValueW(rootKey(), SUBKEY, name, RRF_RT_REG_DWORD,
                               &type, &value, &size);
    return result == ERROR_SUCCESS ? value : fallback;
}

void writeDword(const wchar_t* name, DWORD value) {
    RegSetKeyValueW(rootKey(), SUBKEY, name, REG_DWORD, &value, sizeof(value));
}

bool readBinary(const wchar_t* name, std::vector<uint8_t>& out) {
    DWORD size = 0;
    LONG result = RegGetValueW(rootKey(), SUBKEY, name, RRF_RT_REG_BINARY,
                               nullptr, nullptr, &size);
    if (result != ERROR_SUCCESS || size == 0) {
        return false;
    }

    out.resize(size);
    result = RegGetValueW(rootKey(), SUBKEY, name, RRF_RT_REG_BINARY,
                          nullptr, out.data(), &size);
    if (result != ERROR_SUCCESS) {
        return false;
    }

    out.resize(size);
    return true;
}

}

bool DeviceConfig::lockEnabled() {
    return readDword(VALUE_LOCK_ENABLED, 0) != 0;
}

void DeviceConfig::setLockEnabled(bool enabled) {
    writeDword(VALUE_LOCK_ENABLED, enabled ? 1 : 0);
}

bool DeviceConfig::uninstallProtected() {
    std::vector<uint8_t> blob;
    return readBinary(VALUE_UNINSTALL_CODE, blob);
}

void DeviceConfig::setUninstallCode(const std::string& code) {
    std::vector<uint8_t> plaintext(code.begin(), code.end());
    std::vector<uint8_t> blob;
    if (!DataProtection::protect(plaintext, blob)) {
        return;
    }

    RegSetKeyValueW(rootKey(), SUBKEY, VALUE_UNINSTALL_CODE, REG_BINARY,
                    blob.data(), static_cast<DWORD>(blob.size()));
}

bool DeviceConfig::checkUninstallCode(const std::string& code) {
    std::vector<uint8_t> blob;
    if (!readBinary(VALUE_UNINSTALL_CODE, blob)) {
        return false;
    }

    std::vector<uint8_t> stored;
    if (!DataProtection::unprotect(blob, stored)) {
        return false;
    }

    std::string storedCode(stored.begin(), stored.end());
    return storedCode == code;
}

void DeviceConfig::clearUninstallCode() {
    RegDeleteKeyValueW(rootKey(), SUBKEY, VALUE_UNINSTALL_CODE);
}

void DeviceConfig::removeAll() {
    RegDeleteTreeW(rootKey(), SUBKEY);
}

}
