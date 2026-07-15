#pragma once

#include <string>

#include "DeviceState.h"

namespace zaga {

enum class VerifyResult {
    Accepted,
    RejectedInvalid,
    RejectedReplay,
};

class Verifier {
public:
    // Applies a token to the device state, fail closed. The state is mutated only
    // when the result is Accepted. See docs OFFLINE_CLIENT_GUIDE section 5.
    static VerifyResult apply(
        DeviceState& state,
        const std::string& token,
        const std::string& hmacSecretHex,
        int64_t todayDay);
};

}
