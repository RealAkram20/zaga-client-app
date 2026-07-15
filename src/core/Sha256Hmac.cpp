#include "Sha256Hmac.h"

#include <cctype>

#include <windows.h>
#include <bcrypt.h>

#pragma comment(lib, "bcrypt.lib")

namespace zaga {

namespace {

class AlgorithmHandle {
public:
    ~AlgorithmHandle() {
        if (handle_ != nullptr) {
            BCryptCloseAlgorithmProvider(handle_, 0);
        }
    }

    NTSTATUS open() {
        return BCryptOpenAlgorithmProvider(
            &handle_, BCRYPT_SHA256_ALGORITHM, nullptr, BCRYPT_ALG_HANDLE_HMAC_FLAG);
    }

    BCRYPT_ALG_HANDLE get() const { return handle_; }

private:
    BCRYPT_ALG_HANDLE handle_ = nullptr;
};

class HashHandle {
public:
    ~HashHandle() {
        if (handle_ != nullptr) {
            BCryptDestroyHash(handle_);
        }
    }

    NTSTATUS create(BCRYPT_ALG_HANDLE algorithm, const std::vector<uint8_t>& key) {
        return BCryptCreateHash(
            algorithm,
            &handle_,
            nullptr,
            0,
            const_cast<PUCHAR>(key.data()),
            static_cast<ULONG>(key.size()),
            0);
    }

    BCRYPT_HASH_HANDLE get() const { return handle_; }

private:
    BCRYPT_HASH_HANDLE handle_ = nullptr;
};

}

bool Sha256Hmac::compute(
    const std::vector<uint8_t>& key,
    const std::vector<uint8_t>& message,
    std::array<uint8_t, DIGEST_SIZE>& digest) {
    AlgorithmHandle algorithm;
    if (!BCRYPT_SUCCESS(algorithm.open())) {
        return false;
    }

    HashHandle hash;
    if (!BCRYPT_SUCCESS(hash.create(algorithm.get(), key))) {
        return false;
    }

    NTSTATUS status = BCryptHashData(
        hash.get(),
        const_cast<PUCHAR>(message.data()),
        static_cast<ULONG>(message.size()),
        0);
    if (!BCRYPT_SUCCESS(status)) {
        return false;
    }

    status = BCryptFinishHash(
        hash.get(),
        digest.data(),
        static_cast<ULONG>(digest.size()),
        0);

    return BCRYPT_SUCCESS(status);
}

bool Sha256Hmac::hexToBytes(const std::string& hex, std::vector<uint8_t>& out) {
    if (hex.size() % 2 != 0) {
        return false;
    }

    out.clear();
    out.reserve(hex.size() / 2);

    auto nibble = [](char c, int& value) -> bool {
        if (c >= '0' && c <= '9') {
            value = c - '0';
        } else if (c >= 'a' && c <= 'f') {
            value = c - 'a' + 10;
        } else if (c >= 'A' && c <= 'F') {
            value = c - 'A' + 10;
        } else {
            return false;
        }
        return true;
    };

    for (size_t i = 0; i < hex.size(); i += 2) {
        int high = 0;
        int low = 0;
        if (!nibble(hex[i], high) || !nibble(hex[i + 1], low)) {
            return false;
        }
        out.push_back(static_cast<uint8_t>((high << 4) | low));
    }

    return true;
}

}
