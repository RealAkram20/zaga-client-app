#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "DeviceState.h"

namespace zaga {

struct StoredDevice {
    std::string accountNumber;
    std::string serial;
    std::string model;
    std::string name;
    std::string hmacSecretHex;
    std::string biosPassword;
    std::string recoveryKey;
    std::string uninstallCode;
    std::string deviceToken;
    DeviceState state;
};

class LocalStore {
public:
    static bool save(const std::wstring& path, const StoredDevice& device);

    static bool load(const std::wstring& path, StoredDevice& device);

    static std::wstring defaultPath();

    static std::vector<uint8_t> serialize(const StoredDevice& device);

    static bool deserialize(const std::vector<uint8_t>& bytes, StoredDevice& device);
};

}
