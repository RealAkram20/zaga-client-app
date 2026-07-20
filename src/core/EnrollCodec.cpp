#include "EnrollCodec.h"

#include <cctype>

#include "Base32.h"
#include "Sha256Hmac.h"

namespace zaga {

namespace {

constexpr int VERSION_BITS = 4;
constexpr int SECRET_BITS = 256;
constexpr int GRACE_BITS = 8;
constexpr int PAD_BITS = 2;
constexpr int CHECK_BITS = 40;

constexpr int V1_VERSION = 1;
constexpr size_t V1_SYMBOLS = 60;
constexpr size_t V2_SYMBOLS = 62;
constexpr int V2_TOTAL_BITS = 310;

const char PREFIX[] = "ZGE";
constexpr size_t PREFIX_LENGTH = 3;

// Uppercase with separators stripped — the shape both the prefix check and the
// account fold work on.
std::string canonical(const std::string& text) {
    std::string out;
    for (char c : text) {
        if (c == '-' || c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            continue;
        }
        out += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return out;
}

void appendByteBits(std::vector<bool>& bits, uint8_t byte) {
    for (int bit = 7; bit >= 0; --bit) {
        bits.push_back(((byte >> bit) & 1) != 0);
    }
}

uint8_t byteFromBits(const std::vector<bool>& bits, size_t offset) {
    uint8_t byte = 0;
    for (int bit = 0; bit < 8; ++bit) {
        byte = static_cast<uint8_t>((byte << 1) | (bits[offset + bit] ? 1 : 0));
    }
    return byte;
}

// version < 0 selects the v1 message ("ZGE1" + account); otherwise the v2 message
// binds the grace byte so the field cannot be altered without failing the check.
bool checkBits(const std::vector<uint8_t>& secretBytes,
               const std::string& normalizedAccount,
               int graceDays,
               std::vector<bool>& out) {
    std::string text;
    if (graceDays < 0) {
        text = "ZGE1" + normalizedAccount;
    } else {
        text = "ZGE2" + normalizedAccount;
        text += static_cast<char>(static_cast<uint8_t>(graceDays));
    }
    std::vector<uint8_t> message(text.begin(), text.end());

    std::array<uint8_t, Sha256Hmac::DIGEST_SIZE> digest{};
    if (!Sha256Hmac::compute(secretBytes, message, digest)) {
        return false;
    }

    out.clear();
    for (size_t i = 0; i < CHECK_BITS / 8; ++i) {
        appendByteBits(out, digest[i]);
    }
    return true;
}

std::string toLowerHex(const std::vector<uint8_t>& bytes) {
    static const char digits[] = "0123456789abcdef";
    std::string hex;
    for (uint8_t byte : bytes) {
        hex += digits[byte >> 4];
        hex += digits[byte & 0xF];
    }
    return hex;
}

}

std::string EnrollCodec::normalizeAccount(const std::string& accountNumber) {
    std::string normalized = canonical(accountNumber);
    for (char& c : normalized) {
        switch (c) {
            case 'O': c = '0'; break;
            case 'I': c = '1'; break;
            case 'L': c = '1'; break;
            case 'U': c = 'V'; break;
            default: break;
        }
    }
    return normalized;
}

std::optional<std::string> EnrollCodec::encode(const std::string& secretHex,
                                               const std::string& accountNumber,
                                               uint8_t graceDays) {
    // No raw-string key fallback here, unlike the unlock codec: the code carries the
    // secret as exactly 256 bits, so anything but 64 hex characters cannot be
    // represented and means the record is broken.
    std::vector<uint8_t> secretBytes;
    if (!Sha256Hmac::hexToBytes(secretHex, secretBytes) || secretBytes.size() != 32) {
        return std::nullopt;
    }

    std::string account = normalizeAccount(accountNumber);
    if (account.empty()) {
        return std::nullopt;
    }

    std::vector<bool> bits;
    bits.reserve(V2_TOTAL_BITS);
    for (int bit = VERSION_BITS - 1; bit >= 0; --bit) {
        bits.push_back(((VERSION >> bit) & 1) != 0);
    }
    for (uint8_t byte : secretBytes) {
        appendByteBits(bits, byte);
    }
    for (int bit = GRACE_BITS - 1; bit >= 0; --bit) {
        bits.push_back(((graceDays >> bit) & 1) != 0);
    }
    for (int bit = 0; bit < PAD_BITS; ++bit) {
        bits.push_back(false);
    }

    std::vector<bool> check;
    if (!checkBits(secretBytes, account, graceDays, check)) {
        return std::nullopt;
    }
    bits.insert(bits.end(), check.begin(), check.end());

    return std::string(PREFIX) + "-" + Base32::encode(bits);
}

std::optional<EnrollCodec::Enrollment> EnrollCodec::decode(const std::string& code,
                                                           const std::string& accountNumber) {
    std::string normalized = canonical(code);

    // The prefix letters are themselves valid alphabet symbols, so they must come
    // off before the base32 walk or they would silently decode as data.
    if (normalized.compare(0, PREFIX_LENGTH, PREFIX) != 0) {
        return std::nullopt;
    }
    normalized = normalized.substr(PREFIX_LENGTH);

    if (normalized.size() != V1_SYMBOLS && normalized.size() != V2_SYMBOLS) {
        return std::nullopt;
    }
    bool isV2 = normalized.size() == V2_SYMBOLS;

    std::vector<bool> bits;
    if (!Base32::decode(normalized, bits) || bits.size() != normalized.size() * 5) {
        return std::nullopt;
    }

    int version = 0;
    for (int i = 0; i < VERSION_BITS; ++i) {
        version = (version << 1) | (bits[i] ? 1 : 0);
    }
    if (version != (isV2 ? VERSION : V1_VERSION)) {
        return std::nullopt;
    }

    std::vector<uint8_t> secretBytes;
    secretBytes.reserve(SECRET_BITS / 8);
    for (size_t offset = VERSION_BITS; offset < VERSION_BITS + SECRET_BITS; offset += 8) {
        secretBytes.push_back(byteFromBits(bits, offset));
    }

    int graceDays = -1;
    size_t checkOffset = VERSION_BITS + SECRET_BITS;
    if (isV2) {
        graceDays = byteFromBits(bits, VERSION_BITS + SECRET_BITS);
        checkOffset += GRACE_BITS + PAD_BITS;

        // The pad bits are dead space today; a nonzero value is a corrupted or
        // future code, not something to guess at.
        for (int i = 0; i < PAD_BITS; ++i) {
            if (bits[VERSION_BITS + SECRET_BITS + GRACE_BITS + i]) {
                return std::nullopt;
            }
        }
    }

    std::vector<bool> expected;
    if (!checkBits(secretBytes, normalizeAccount(accountNumber), graceDays, expected)) {
        return std::nullopt;
    }

    // Constant-time compare: never short-circuit on the first differing bit.
    int difference = 0;
    for (int i = 0; i < CHECK_BITS; ++i) {
        difference |= (bits[checkOffset + i] ? 1 : 0) ^ (expected[i] ? 1 : 0);
    }
    if (difference != 0) {
        return std::nullopt;
    }

    Enrollment enrollment;
    enrollment.secretHex = toLowerHex(secretBytes);
    enrollment.graceDays = static_cast<uint8_t>(graceDays < 0 ? 0 : graceDays);
    return enrollment;
}

}
