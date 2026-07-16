#include "HardwareInfo.h"

#include <windows.h>

#include <algorithm>
#include <cstring>

namespace zaga {

namespace {

// 'RSMB' — the raw SMBIOS firmware table. Spelled out rather than written as a
// multi-character literal, whose byte order is implementation defined.
constexpr DWORD RSMB_SIGNATURE = 0x52534D42;

// SMBIOS type 1 (System Information) field offsets, per the DMTF spec.
constexpr uint8_t TYPE_SYSTEM_INFORMATION = 1;
constexpr size_t OFFSET_MANUFACTURER = 0x04;
constexpr size_t OFFSET_PRODUCT_NAME = 0x05;
constexpr size_t OFFSET_SERIAL_NUMBER = 0x07;

std::string trim(const std::string& value) {
    size_t first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return std::string();
    }
    size_t last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

// SMBIOS strings are a 1-based list of null-terminated strings that follow the
// structure's formatted area, ending with an empty string.
std::string smbiosString(const uint8_t* strings, const uint8_t* end, uint8_t index) {
    if (index == 0 || strings >= end) {
        return std::string();
    }

    const uint8_t* cursor = strings;
    for (uint8_t current = 1; cursor < end && *cursor != 0; ++current) {
        size_t remaining = static_cast<size_t>(end - cursor);
        size_t length = strnlen(reinterpret_cast<const char*>(cursor), remaining);
        if (current == index) {
            return trim(std::string(reinterpret_cast<const char*>(cursor), length));
        }
        cursor += length + 1;
    }
    return std::string();
}

std::string registryValue(const wchar_t* name) {
    wchar_t buffer[256];
    DWORD size = sizeof(buffer);
    LONG result = RegGetValueW(HKEY_LOCAL_MACHINE,
                               L"HARDWARE\\DESCRIPTION\\System\\BIOS",
                               name, RRF_RT_REG_SZ, nullptr, buffer, &size);
    if (result != ERROR_SUCCESS) {
        return std::string();
    }

    int length = WideCharToMultiByte(CP_UTF8, 0, buffer, -1, nullptr, 0, nullptr, nullptr);
    std::string value(length > 0 ? length - 1 : 0, '\0');
    if (length > 0) {
        WideCharToMultiByte(CP_UTF8, 0, buffer, -1, &value[0], length, nullptr, nullptr);
    }
    return trim(value);
}

std::string computerName() {
    wchar_t buffer[256];
    DWORD size = 256;
    if (!GetComputerNameExW(ComputerNamePhysicalDnsHostname, buffer, &size)) {
        return std::string();
    }

    int length = WideCharToMultiByte(CP_UTF8, 0, buffer, -1, nullptr, 0, nullptr, nullptr);
    std::string name(length > 0 ? length - 1 : 0, '\0');
    if (length > 0) {
        WideCharToMultiByte(CP_UTF8, 0, buffer, -1, &name[0], length, nullptr, nullptr);
    }
    return name;
}

}

bool Hardware::isPlaceholder(const std::string& value) {
    if (value.empty()) {
        return true;
    }

    std::string lowered = value;
    std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                   [](unsigned char c) { return static_cast<char>(::tolower(c)); });

    // Values OEMs ship when a field was never programmed.
    static const char* junk[] = {
        "to be filled by o.e.m.", "to be filled by oem", "default string",
        "system serial number", "system product name", "system manufacturer",
        "not specified", "none", "n/a", "unknown", "invalid", "0123456789",
        "chassis serial number", "empty",
    };
    for (const char* candidate : junk) {
        if (lowered == candidate) {
            return true;
        }
    }
    return false;
}

bool Hardware::parseSmbios(const std::vector<uint8_t>& table, HardwareInfo& info) {
    // RawSMBIOSData: 4 bytes of version data, then a DWORD length, then the tables.
    constexpr size_t RAW_HEADER = 8;
    if (table.size() <= RAW_HEADER) {
        return false;
    }

    uint32_t declared = 0;
    std::memcpy(&declared, table.data() + 4, sizeof(declared));

    const uint8_t* cursor = table.data() + RAW_HEADER;
    size_t available = table.size() - RAW_HEADER;
    const uint8_t* end = cursor + (std::min)(static_cast<size_t>(declared), available);

    while (cursor + 4 <= end) {
        uint8_t type = cursor[0];
        uint8_t formattedLength = cursor[1];
        if (formattedLength < 4 || cursor + formattedLength > end) {
            break;
        }

        // The string table runs to a double null, which also ends the structure.
        const uint8_t* strings = cursor + formattedLength;
        const uint8_t* scan = strings;
        while (scan + 1 < end && !(scan[0] == 0 && scan[1] == 0)) {
            ++scan;
        }

        if (type == TYPE_SYSTEM_INFORMATION && formattedLength > OFFSET_SERIAL_NUMBER) {
            info.manufacturer = smbiosString(strings, end, cursor[OFFSET_MANUFACTURER]);
            info.model = smbiosString(strings, end, cursor[OFFSET_PRODUCT_NAME]);
            info.serial = smbiosString(strings, end, cursor[OFFSET_SERIAL_NUMBER]);
            return true;
        }

        cursor = scan + 2;
    }
    return false;
}

HardwareInfo Hardware::detect() {
    HardwareInfo info;
    info.hostname = computerName();

    // SMBIOS is the authoritative source and needs no COM/WMI, so it also works
    // from the SYSTEM context at the logon screen.
    UINT size = GetSystemFirmwareTable(RSMB_SIGNATURE, 0, nullptr, 0);
    if (size > 0) {
        std::vector<uint8_t> table(size);
        if (GetSystemFirmwareTable(RSMB_SIGNATURE, 0, table.data(), size) == size) {
            parseSmbios(table, info);
        }
    }

    // The registry mirrors the same firmware fields and covers machines whose
    // SMBIOS table is unreadable.
    if (isPlaceholder(info.manufacturer)) {
        info.manufacturer = registryValue(L"SystemManufacturer");
    }
    if (isPlaceholder(info.model)) {
        info.model = registryValue(L"SystemProductName");
    }

    if (isPlaceholder(info.manufacturer)) info.manufacturer.clear();
    if (isPlaceholder(info.model)) info.model.clear();
    if (isPlaceholder(info.serial)) info.serial.clear();

    return info;
}

}
