#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace zaga {

enum class TokenType {
    Full,
    Grace,
};

struct DecodedToken {
    int version;
    uint32_t counter;
    uint32_t durationDays;
    int flags;
    TokenType type;
};

class TokenCodec {
public:
    static constexpr int VERSION = 1;
    static constexpr uint32_t MAX_COUNTER = (1u << 20) - 1;
    static constexpr uint32_t MAX_DURATION_DAYS = (1u << 12) - 1;

    static std::optional<std::string> encode(
        uint32_t counter,
        uint32_t durationDays,
        TokenType type,
        const std::string& hmacSecretHex);

    static std::optional<DecodedToken> decode(
        const std::string& token,
        const std::string& hmacSecretHex);
};

}
