// Parser checks for the SMBIOS reader. The table is synthesised here so the parse
// is exercised without depending on the firmware of whatever machine runs the test.
#include <cstdio>
#include <string>
#include <vector>

#include "HardwareInfo.h"

using namespace zaga;

namespace {

int failures = 0;

void check(bool condition, const char* what) {
    if (!condition) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

void checkEqual(const std::string& actual, const std::string& expected, const char* what) {
    if (actual != expected) {
        std::printf("FAIL: %s (got \"%s\", want \"%s\")\n", what, actual.c_str(), expected.c_str());
        ++failures;
    }
}

void put(std::vector<uint8_t>& out, const std::string& text) {
    out.insert(out.end(), text.begin(), text.end());
    out.push_back(0);
}

// A RawSMBIOSData blob holding one type 1 structure with the given strings.
std::vector<uint8_t> makeTable(const std::string& manufacturer,
                               const std::string& product,
                               const std::string& serial) {
    std::vector<uint8_t> body;

    // Type 1 formatted area: type, length, handle, then string indices.
    const uint8_t formattedLength = 0x1B;
    body.push_back(1);                 // type = System Information
    body.push_back(formattedLength);
    body.push_back(0x01);              // handle lo
    body.push_back(0x00);              // handle hi
    body.push_back(1);                 // 0x04 manufacturer -> string 1
    body.push_back(2);                 // 0x05 product name -> string 2
    body.push_back(0);                 // 0x06 version -> not specified
    body.push_back(3);                 // 0x07 serial -> string 3
    while (body.size() < formattedLength) {
        body.push_back(0);             // UUID + wake-up, unused here
    }

    put(body, manufacturer);
    put(body, product);
    put(body, serial);
    body.push_back(0);                 // end of string table

    std::vector<uint8_t> table;
    table.push_back(0);                // Used20CallingMethod
    table.push_back(3);                // major
    table.push_back(2);                // minor
    table.push_back(0);                // DmiRevision
    uint32_t length = static_cast<uint32_t>(body.size());
    for (int shift = 0; shift < 32; shift += 8) {
        table.push_back(static_cast<uint8_t>((length >> shift) & 0xFF));  // little endian
    }
    table.insert(table.end(), body.begin(), body.end());
    return table;
}

}

int main() {
    // A normal machine.
    {
        HardwareInfo info{};
        check(Hardware::parseSmbios(makeTable("HP", "HP ProDesk 600 G6", "8CC1510VXF"), info),
              "parses a type 1 structure");
        checkEqual(info.manufacturer, "HP", "manufacturer");
        checkEqual(info.model, "HP ProDesk 600 G6", "model");
        checkEqual(info.serial, "8CC1510VXF", "serial");
    }

    // Firmware pads fields; the reader must not carry the padding through.
    {
        HardwareInfo info{};
        Hardware::parseSmbios(makeTable("  Dell Inc.  ", " Latitude 5490 ", " 5CG9482Q1B "), info);
        checkEqual(info.manufacturer, "Dell Inc.", "trims manufacturer");
        checkEqual(info.model, "Latitude 5490", "trims model");
        checkEqual(info.serial, "5CG9482Q1B", "trims serial");
    }

    // A truncated table must be rejected rather than read out of bounds.
    {
        HardwareInfo info{};
        check(!Hardware::parseSmbios({0, 3, 2, 0}, info), "rejects a short table");
        check(!Hardware::parseSmbios({}, info), "rejects an empty table");
    }

    // Unprogrammed OEM fields count as absent.
    {
        check(Hardware::isPlaceholder("To Be Filled By O.E.M."), "detects OEM filler");
        check(Hardware::isPlaceholder("System Serial Number"), "detects serial filler");
        check(Hardware::isPlaceholder("Default string"), "detects default string");
        check(Hardware::isPlaceholder(""), "empty is a placeholder");
        check(!Hardware::isPlaceholder("8CC1510VXF"), "a real serial is kept");
        check(!Hardware::isPlaceholder("HP ProDesk 600 G6"), "a real model is kept");
    }

    if (failures == 0) {
        std::printf("hardware_tests: all checks passed\n");
    }
    return failures == 0 ? 0 : 1;
}
