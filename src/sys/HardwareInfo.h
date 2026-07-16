#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace zaga {

// The machine's own identity, read from the firmware rather than taken on trust
// from the portal, so an enrolled device always reports what it actually is.
struct HardwareInfo {
    std::string manufacturer;
    std::string model;
    std::string serial;
    std::string hostname;
};

class Hardware {
public:
    static HardwareInfo detect();

    // Exposed for tests: pulls system information (SMBIOS type 1) out of a raw
    // firmware table, so the parser can be exercised without real firmware.
    static bool parseSmbios(const std::vector<uint8_t>& table, HardwareInfo& info);

    // Firmware fields are frequently padded or filled with placeholders like
    // "To Be Filled By O.E.M."; those are treated as absent.
    static bool isPlaceholder(const std::string& value);
};

}
