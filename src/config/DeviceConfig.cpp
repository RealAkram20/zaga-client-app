#include "DeviceConfig.h"

#include <windows.h>
#include <bcrypt.h>

#include <vector>

#include "DataProtection.h"

namespace zaga {

namespace {

const wchar_t SUBKEY[] = L"SOFTWARE\\Zaga";
const wchar_t VALUE_LOCK_ENABLED[] = L"LockEnabled";
const wchar_t VALUE_UNINSTALL_CODE[] = L"UninstallCode";
const wchar_t VALUE_PORTAL_URL[] = L"PortalUrl";
const wchar_t VALUE_UNLOCK_INSTRUCTIONS[] = L"UnlockInstructions";
const wchar_t VALUE_SUPPORT_CONTACT[] = L"SupportContact";
const wchar_t VALUE_ACCOUNT_NUMBER[] = L"AccountNumber";
const wchar_t VALUE_LAST_ERROR[] = L"LastError";

std::string narrow(const std::wstring& text) {
    if (text.empty()) {
        return std::string();
    }
    int length = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string narrowed(length > 0 ? length - 1 : 0, '\0');
    if (length > 0) {
        WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, &narrowed[0], length, nullptr, nullptr);
    }
    return narrowed;
}

std::wstring widen(const std::string& text) {
    if (text.empty()) {
        return std::wstring();
    }
    int length = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
    std::wstring wide(length > 0 ? length - 1 : 0, L'\0');
    if (length > 0) {
        MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, &wide[0], length);
    }
    return wide;
}

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

// Excludes I, O, 0 and 1: the code is read off a screen and typed back by hand
// when a removal is authorised. Exactly 32 characters, so masking a random byte
// with 31 picks one without modulo bias.
const char CODE_ALPHABET[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
constexpr size_t CODE_LENGTH = 12;

std::string generateUninstallCode() {
    uint8_t bytes[CODE_LENGTH];
    if (!BCRYPT_SUCCESS(BCryptGenRandom(nullptr, bytes, sizeof(bytes),
                                        BCRYPT_USE_SYSTEM_PREFERRED_RNG))) {
        return std::string();
    }

    std::string code;
    for (size_t i = 0; i < CODE_LENGTH; ++i) {
        if (i > 0 && i % 4 == 0) {
            code += '-';
        }
        code += CODE_ALPHABET[bytes[i] & 31];
    }
    return code;
}

// "ZG-" plus ten characters of the same alphabet, grouped for reading: ZG-ABCDE-FGHJK.
//
// Ten characters is 32^10 ≈ 1.1e15 possibilities. Devices mint these with no
// co-ordination and no database to check against, so the space alone has to make
// collisions negligible: across 100,000 devices the chance that any two ever match is
// about 4 in a million, and it stays under a thousandth of a percent at ten times
// that. The portal's unique index remains the backstop.
constexpr size_t ACCOUNT_RANDOM_LENGTH = 10;

std::string generateAccountNumber() {
    uint8_t bytes[ACCOUNT_RANDOM_LENGTH];
    if (!BCRYPT_SUCCESS(BCryptGenRandom(nullptr, bytes, sizeof(bytes),
                                        BCRYPT_USE_SYSTEM_PREFERRED_RNG))) {
        return std::string();
    }

    std::string number = "ZG-";
    for (size_t i = 0; i < ACCOUNT_RANDOM_LENGTH; ++i) {
        if (i == ACCOUNT_RANDOM_LENGTH / 2) {
            number += '-';
        }
        number += CODE_ALPHABET[bytes[i] & 31];
    }
    return number;
}

std::string readString(const wchar_t* name) {
    wchar_t buffer[512];
    DWORD size = sizeof(buffer);
    LONG result = RegGetValueW(rootKey(), SUBKEY, name, RRF_RT_REG_SZ,
                               nullptr, buffer, &size);
    return result == ERROR_SUCCESS ? narrow(buffer) : std::string();
}

void writeString(const wchar_t* name, const std::string& value) {
    std::wstring wide = widen(value);
    RegSetKeyValueW(rootKey(), SUBKEY, name, REG_SZ,
                    wide.c_str(), static_cast<DWORD>((wide.size() + 1) * sizeof(wchar_t)));
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

std::string DeviceConfig::portalUrl() {
    return readString(VALUE_PORTAL_URL);
}

void DeviceConfig::setPortalUrl(const std::string& url) {
    writeString(VALUE_PORTAL_URL, url);
}

std::string DeviceConfig::unlockInstructions() {
    return readString(VALUE_UNLOCK_INSTRUCTIONS);
}

void DeviceConfig::setUnlockInstructions(const std::string& instructions) {
    writeString(VALUE_UNLOCK_INSTRUCTIONS, instructions);
}

std::string DeviceConfig::supportContact() {
    return readString(VALUE_SUPPORT_CONTACT);
}

void DeviceConfig::setSupportContact(const std::string& contact) {
    writeString(VALUE_SUPPORT_CONTACT, contact);
}

std::string DeviceConfig::accountNumber() {
    return readString(VALUE_ACCOUNT_NUMBER);
}

void DeviceConfig::setAccountNumber(const std::string& accountNumber) {
    if (accountNumber.empty()) {
        return;
    }
    writeString(VALUE_ACCOUNT_NUMBER, accountNumber);
}

void DeviceConfig::setLastError(const std::string& message) {
    if (message.empty()) {
        clearLastError();
        return;
    }
    writeString(VALUE_LAST_ERROR, message);
}

std::string DeviceConfig::lastError() {
    return readString(VALUE_LAST_ERROR);
}

void DeviceConfig::clearLastError() {
    RegDeleteKeyValueW(rootKey(), SUBKEY, VALUE_LAST_ERROR);
}

std::string DeviceConfig::ensureAccountNumber() {
    std::string existing = accountNumber();
    if (!existing.empty()) {
        return existing;
    }

    std::string minted = generateAccountNumber();
    if (minted.empty()) {
        return std::string();
    }

    setAccountNumber(minted);

    // Report only what actually landed, so a caller never shows a number the portal
    // will not be registered against.
    return accountNumber();
}

bool DeviceConfig::uninstallProtected() {
    std::vector<uint8_t> blob;
    return readBinary(VALUE_UNINSTALL_CODE, blob);
}

std::string DeviceConfig::uninstallCode() {
    std::vector<uint8_t> blob;
    if (!readBinary(VALUE_UNINSTALL_CODE, blob)) {
        return std::string();
    }

    std::vector<uint8_t> stored;
    if (!DataProtection::unprotect(blob, stored)) {
        return std::string();
    }

    return std::string(stored.begin(), stored.end());
}

std::string DeviceConfig::ensureUninstallCode() {
    std::string existing = uninstallCode();
    if (!existing.empty()) {
        return existing;
    }

    std::string code = generateUninstallCode();
    if (code.empty()) {
        return std::string();
    }

    setUninstallCode(code);

    // Only report the code once it is readable back, so a caller never shows a
    // code that a failed write means the machine will not accept.
    return uninstallCode();
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
