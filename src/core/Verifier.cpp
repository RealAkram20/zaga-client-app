#include "Verifier.h"

#include <algorithm>

#include "TokenCodec.h"

namespace zaga {

VerifyResult Verifier::apply(
    DeviceState& state,
    const std::string& token,
    const std::string& hmacSecretHex,
    int64_t todayDay) {
    auto decoded = TokenCodec::decode(token, hmacSecretHex);
    if (!decoded.has_value()) {
        return VerifyResult::RejectedInvalid;
    }

    if (decoded->counter <= state.lastCounter) {
        return VerifyResult::RejectedReplay;
    }

    int64_t base = std::max(todayDay, state.lockDeadlineDay);

    state.lastCounter = decoded->counter;
    state.lockDeadlineDay = base + static_cast<int64_t>(decoded->durationDays);
    state.status = DeviceStatus::Active;
    state.lastTokenWasGrace = decoded->type == TokenType::Grace;

    return VerifyResult::Accepted;
}

}
