#include <windows.h>

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

#include "EnrollCodec.h"
#include "LocalStore.h"
#include "ProvisioningBundle.h"
#include "Verifier.h"

using namespace zaga;

// Manual end-to-end proof that a device provisioned offline can verify a real
// portal-issued unlock token with no network in the loop — from either offline
// source: a downloaded bundle file, or the typed ZGE enrollment code. Writes to
// whatever store path is given, so it never touches the machine's real store or
// arms anything.
//   offline_e2e <bundle.json> <unlock-token> <scratch-store-path>
//   offline_e2e <ZGE-code> <unlock-token> <scratch-store-path> <account-number>
int main(int argc, char** argv) {
    if (argc < 4) {
        std::printf("usage: offline_e2e <bundle.json|ZGE-code> <token> <store-path> [account]\n");
        return 2;
    }

    BundleResult bundle;
    std::string source = argv[1];

    if (source.rfind("ZGE", 0) == 0 || source.rfind("zge", 0) == 0) {
        if (argc < 5) {
            std::printf("a ZGE code needs the device's account number as the 4th argument\n");
            return 2;
        }
        std::optional<EnrollCodec::Enrollment> enrollment = EnrollCodec::decode(source, argv[4]);
        if (!enrollment) {
            std::printf("ZGE code REJECTED\n");
            return 1;
        }
        bundle.ok = true;
        bundle.device.accountNumber = argv[4];
        bundle.device.hmacSecretHex = enrollment->secretHex;
        bundle.device.state.graceDays = enrollment->graceDays;
        std::printf("ZGE code ok: account=%s secret_len=%zu grace=%u\n",
                    bundle.device.accountNumber.c_str(), bundle.device.hmacSecretHex.size(),
                    static_cast<unsigned>(enrollment->graceDays));
    } else {
        std::ifstream in(source, std::ios::binary);
        std::stringstream ss;
        ss << in.rdbuf();

        bundle = parseProvisioningBundle(ss.str());
        if (!bundle.ok) {
            std::printf("bundle REJECTED: %s\n", bundle.message.c_str());
            return 1;
        }
        std::printf("bundle ok: account=%s secret_len=%zu\n",
                    bundle.device.accountNumber.c_str(), bundle.device.hmacSecretHex.size());
    }

    int wlen = MultiByteToWideChar(CP_UTF8, 0, argv[3], -1, nullptr, 0);
    std::wstring store(wlen > 0 ? wlen - 1 : 0, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, argv[3], -1, &store[0], wlen);

    if (!LocalStore::save(store, bundle.device)) {
        std::printf("store write FAILED\n");
        return 1;
    }

    StoredDevice loaded;
    if (!LocalStore::load(store, loaded)) {
        std::printf("store read FAILED\n");
        return 1;
    }
    std::printf("store round-trip ok: account=%s\n", loaded.accountNumber.c_str());

    // A freshly provisioned device is at counter 0 with a locked, past-due state —
    // exactly what enrolling leaves behind. Applying the portal's token has to open it.
    DeviceState state = loaded.state;
    const int64_t today = 20000;   // any day; the token carries its own duration
    VerifyResult result = Verifier::apply(state, argv[2], loaded.hmacSecretHex, today);
    std::printf("verify portal token: %s (counter %u -> %u, deadline day %lld)\n",
                result == VerifyResult::Accepted ? "ACCEPTED" : "REJECTED",
                loaded.state.lastCounter, state.lastCounter,
                static_cast<long long>(state.lockDeadlineDay));

    bool opened = result == VerifyResult::Accepted && state.lockDeadlineDay > today;
    std::printf("device open after token: %s\n", opened ? "YES" : "NO");
    return opened ? 0 : 1;
}
