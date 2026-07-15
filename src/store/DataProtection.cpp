#include "DataProtection.h"

#include <windows.h>
#include <wincrypt.h>
#include <dpapi.h>

#pragma comment(lib, "crypt32.lib")

namespace zaga {

namespace {

// Namespaces the protected blob to this application so a blob from another DPAPI
// consumer on the same machine cannot be unprotected here. This is not a secret;
// it only scopes the ciphertext.
const BYTE ENTROPY[] = "ZagaDeviceLock/state/v1";

DATA_BLOB entropyBlob() {
    DATA_BLOB blob;
    blob.pbData = const_cast<BYTE*>(ENTROPY);
    blob.cbData = sizeof(ENTROPY);
    return blob;
}

class LocalMemory {
public:
    explicit LocalMemory(PVOID pointer) : pointer_(pointer) {}

    ~LocalMemory() {
        if (pointer_ != nullptr) {
            LocalFree(pointer_);
        }
    }

    LocalMemory(const LocalMemory&) = delete;
    LocalMemory& operator=(const LocalMemory&) = delete;

private:
    PVOID pointer_;
};

}

bool DataProtection::protect(const std::vector<uint8_t>& plaintext, std::vector<uint8_t>& blob) {
    DATA_BLOB input;
    input.pbData = const_cast<BYTE*>(plaintext.data());
    input.cbData = static_cast<DWORD>(plaintext.size());

    DATA_BLOB entropy = entropyBlob();
    DATA_BLOB output{};

    if (!CryptProtectData(&input, L"Zaga device state", &entropy, nullptr, nullptr,
                          CRYPTPROTECT_LOCAL_MACHINE, &output)) {
        return false;
    }

    LocalMemory owned(output.pbData);
    blob.assign(output.pbData, output.pbData + output.cbData);
    return true;
}

bool DataProtection::unprotect(const std::vector<uint8_t>& blob, std::vector<uint8_t>& plaintext) {
    DATA_BLOB input;
    input.pbData = const_cast<BYTE*>(blob.data());
    input.cbData = static_cast<DWORD>(blob.size());

    DATA_BLOB entropy = entropyBlob();
    DATA_BLOB output{};

    if (!CryptUnprotectData(&input, nullptr, &entropy, nullptr, nullptr,
                            CRYPTPROTECT_LOCAL_MACHINE, &output)) {
        return false;
    }

    LocalMemory owned(output.pbData);
    plaintext.assign(output.pbData, output.pbData + output.cbData);
    SecureZeroMemory(output.pbData, output.cbData);
    return true;
}

}
