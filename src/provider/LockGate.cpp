#include "LockGate.h"

#include <windows.h>

#include "LocalStore.h"

namespace zaga {

namespace {

constexpr uint64_t TICKS_PER_DAY = 864000000000ULL;

std::string formatDay(int64_t day) {
    if (day <= 0) {
        return "";
    }

    uint64_t ticks = static_cast<uint64_t>(day) * TICKS_PER_DAY;
    FILETIME ft;
    ft.dwLowDateTime = static_cast<DWORD>(ticks & 0xFFFFFFFF);
    ft.dwHighDateTime = static_cast<DWORD>(ticks >> 32);

    SYSTEMTIME st;
    if (!FileTimeToSystemTime(&ft, &st)) {
        return "";
    }

    static const char* months[] = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

    char buffer[32];
    wsprintfA(buffer, "%02d %s %04d", st.wDay, months[st.wMonth - 1], st.wYear);
    return buffer;
}

}

int64_t LockGate::todayDay() {
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER value;
    value.LowPart = ft.dwLowDateTime;
    value.HighPart = ft.dwHighDateTime;
    return static_cast<int64_t>(value.QuadPart / TICKS_PER_DAY);
}

std::wstring LockGate::storePath() {
    // A test-only override so the console harness can point at a temp store
    // instead of the machine-wide location, which needs elevation to write.
    wchar_t override[512];
    DWORD length = GetEnvironmentVariableW(L"ZAGA_STATE_PATH", override, 512);
    if (length > 0 && length < 512) {
        return std::wstring(override, length);
    }

    return LocalStore::defaultPath();
}

GateInfo LockGate::describe() {
    GateInfo info{};
    info.provisioned = false;
    info.locked = true;
    info.statusText = "Device locked";

    StoredDevice device;
    if (!LocalStore::load(storePath(), device)) {
        return info;
    }

    info.provisioned = true;
    info.accountNumber = device.accountNumber;
    info.model = device.model;
    info.serial = device.serial;
    info.name = device.name;

    int64_t today = todayDay();
    info.deadlineText = formatDay(device.state.lockDeadlineDay);

    if (today >= device.state.lockDeadlineDay) {
        info.locked = true;
        info.statusText = "Payment overdue";
    } else {
        info.locked = false;
        info.statusText = "Active";
    }

    return info;
}

VerifyResult LockGate::applyCode(const std::string& code, std::string& message) {
    StoredDevice device;
    if (!LocalStore::load(storePath(), device)) {
        message = "This device is not provisioned.";
        return VerifyResult::RejectedInvalid;
    }

    VerifyResult result = Verifier::apply(device.state, code, device.hmacSecretHex, todayDay());

    switch (result) {
        case VerifyResult::Accepted:
            if (!LocalStore::save(storePath(), device)) {
                message = "Could not save device state. The device stays locked.";
                return VerifyResult::RejectedInvalid;
            }
            message = "Code accepted. The device is unlocked.";
            break;
        case VerifyResult::RejectedReplay:
            message = "This code has already been used.";
            break;
        case VerifyResult::RejectedInvalid:
            message = "Incorrect code. The device stays locked.";
            break;
    }

    return result;
}

}
