#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace zaga {

class Sha256Hmac {
public:
    static constexpr size_t DIGEST_SIZE = 32;

    static bool compute(
        const std::vector<uint8_t>& key,
        const std::vector<uint8_t>& message,
        std::array<uint8_t, DIGEST_SIZE>& digest);

    static bool hexToBytes(const std::string& hex, std::vector<uint8_t>& out);
};

}
