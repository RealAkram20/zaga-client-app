#include "TokenCodec.h"

#include "Base32.h"
#include "Sha256Hmac.h"

#include <array>
#include <vector>

namespace zaga {

namespace {

constexpr int COUNTER_BITS = 20;
constexpr int DURATION_BITS = 12;
constexpr int FLAG_BITS = 4;
constexpr int PAYLOAD_BITS = 40;
constexpr int SIGNATURE_BITS = 60;
constexpr int TOKEN_BITS = 100;

int flagsFor(TokenType type) {
    return type == TokenType::Grace ? 1 : 0;
}

uint64_t payloadInt(uint32_t counter, uint32_t durationDays, int flags) {
    return (static_cast<uint64_t>(TokenCodec::VERSION) << 36)
        | (static_cast<uint64_t>(counter) << (DURATION_BITS + FLAG_BITS))
        | (static_cast<uint64_t>(durationDays) << FLAG_BITS)
        | static_cast<uint64_t>(flags);
}

std::vector<uint8_t> payloadBytes(uint64_t payload) {
    std::vector<uint8_t> bytes;
    for (int shift = 32; shift >= 0; shift -= 8) {
        bytes.push_back(static_cast<uint8_t>((payload >> shift) & 0xFF));
    }
    return bytes;
}

// Resolve the shared key exactly as the portal does: hex-decode when the string
// is valid hex, otherwise treat the raw string bytes as the key.
std::vector<uint8_t> resolveKey(const std::string& hmacSecretHex) {
    std::vector<uint8_t> key;
    if (Sha256Hmac::hexToBytes(hmacSecretHex, key)) {
        return key;
    }
    return std::vector<uint8_t>(hmacSecretHex.begin(), hmacSecretHex.end());
}

bool signatureBits(uint64_t payload, const std::string& hmacSecretHex, std::vector<bool>& out) {
    std::array<uint8_t, Sha256Hmac::DIGEST_SIZE> digest{};
    if (!Sha256Hmac::compute(resolveKey(hmacSecretHex), payloadBytes(payload), digest)) {
        return false;
    }

    out.clear();
    out.reserve(SIGNATURE_BITS);
    for (int bit = 0; bit < SIGNATURE_BITS; ++bit) {
        uint8_t byte = digest[bit / 8];
        out.push_back(((byte >> (7 - (bit % 8))) & 1) != 0);
    }
    return true;
}

}

std::optional<std::string> TokenCodec::encode(
    uint32_t counter,
    uint32_t durationDays,
    TokenType type,
    const std::string& hmacSecretHex) {
    if (counter > MAX_COUNTER || durationDays > MAX_DURATION_DAYS) {
        return std::nullopt;
    }

    uint64_t payload = payloadInt(counter, durationDays, flagsFor(type));

    std::vector<bool> signature;
    if (!signatureBits(payload, hmacSecretHex, signature)) {
        return std::nullopt;
    }

    std::vector<bool> bits;
    bits.reserve(TOKEN_BITS);
    for (int i = PAYLOAD_BITS - 1; i >= 0; --i) {
        bits.push_back(((payload >> i) & 1) != 0);
    }
    bits.insert(bits.end(), signature.begin(), signature.end());

    return Base32::encode(bits);
}

std::optional<DecodedToken> TokenCodec::decode(
    const std::string& token,
    const std::string& hmacSecretHex) {
    std::vector<bool> bits;
    if (!Base32::decode(token, bits) || bits.size() != TOKEN_BITS) {
        return std::nullopt;
    }

    uint64_t payload = 0;
    for (int i = 0; i < PAYLOAD_BITS; ++i) {
        payload = (payload << 1) | (bits[i] ? 1 : 0);
    }

    std::vector<bool> expected;
    if (!signatureBits(payload, hmacSecretHex, expected)) {
        return std::nullopt;
    }

    // Constant-time compare: never short-circuit on the first differing bit.
    int difference = 0;
    for (int i = 0; i < SIGNATURE_BITS; ++i) {
        difference |= (bits[PAYLOAD_BITS + i] ? 1 : 0) ^ (expected[i] ? 1 : 0);
    }
    if (difference != 0) {
        return std::nullopt;
    }

    int version = static_cast<int>((payload >> 36) & 0xF);
    if (version != VERSION) {
        return std::nullopt;
    }

    int flags = static_cast<int>(payload & ((1u << FLAG_BITS) - 1));

    DecodedToken decoded;
    decoded.version = version;
    decoded.counter = static_cast<uint32_t>((payload >> (DURATION_BITS + FLAG_BITS)) & MAX_COUNTER);
    decoded.durationDays = static_cast<uint32_t>((payload >> FLAG_BITS) & MAX_DURATION_DAYS);
    decoded.flags = flags;
    decoded.type = (flags & 1) ? TokenType::Grace : TokenType::Full;

    return decoded;
}

}
