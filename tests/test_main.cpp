#include <cstdio>
#include <string>

#include "TokenCodec.h"
#include "Verifier.h"

using namespace zaga;

namespace {

int g_failures = 0;

void check(bool condition, const std::string& label) {
    if (condition) {
        std::printf("  ok   %s\n", label.c_str());
    } else {
        std::printf("  FAIL %s\n", label.c_str());
        ++g_failures;
    }
}

const std::string SECRET =
    "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";

struct Vector {
    uint32_t counter;
    uint32_t durationDays;
    TokenType type;
    std::string token;
};

const Vector VECTORS[] = {
    {1, 30, TokenType::Full, "20002-0F04J-CEBMA-PNWJB"},
    {7, 14, TokenType::Grace, "2000E-071GE-DSY1A-5H56P"},
    {1048575, 4095, TokenType::Full, "3ZZZZ-ZZGGE-62K9J-6J9M1"},
    {42, 7, TokenType::Full, "2002M-03GSD-AESAV-R12RE"},
};

void testEncodeParity() {
    std::printf("encode -> exact token\n");
    for (const auto& v : VECTORS) {
        auto encoded = TokenCodec::encode(v.counter, v.durationDays, v.type, SECRET);
        check(encoded.has_value() && *encoded == v.token,
              "counter " + std::to_string(v.counter) + " -> " + v.token +
                  (encoded ? " (got " + *encoded + ")" : " (got none)"));
    }
}

void testDecodeParity() {
    std::printf("decode -> exact fields\n");
    for (const auto& v : VECTORS) {
        auto decoded = TokenCodec::decode(v.token, SECRET);
        bool matches = decoded.has_value()
            && decoded->counter == v.counter
            && decoded->durationDays == v.durationDays
            && decoded->type == v.type;
        check(matches, "round-trip " + v.token);
    }
}

void testTamperRejects() {
    std::printf("tamper -> reject\n");
    std::string tampered = VECTORS[0].token;
    tampered[0] = (tampered[0] == '2') ? '3' : '2';
    check(!TokenCodec::decode(tampered, SECRET).has_value(), "flipped first char");

    std::string truncated = "20002-0F04J-CEBMA-PNWJ";
    check(!TokenCodec::decode(truncated, SECRET).has_value(), "wrong length");

    std::string garbage = "not-a-valid-token!!";
    check(!TokenCodec::decode(garbage, SECRET).has_value(), "non-alphabet input");
}

void testWrongSecretRejects() {
    std::printf("wrong secret -> reject\n");
    const std::string other =
        "fedcba9876543210fedcba9876543210fedcba9876543210fedcba9876543210";
    for (const auto& v : VECTORS) {
        check(!TokenCodec::decode(v.token, other).has_value(),
              "reject " + v.token + " under different secret");
    }
}

void testNormalizationAccepts() {
    std::printf("normalization\n");
    auto decoded = TokenCodec::decode("20002 0f04j cebma pnwjb", SECRET);
    check(decoded.has_value() && decoded->counter == 1, "lowercase + spaces accepted");

    auto ambiguous = TokenCodec::decode("2ooo2-of04j-cebma-pnwjb", SECRET);
    check(ambiguous.has_value() && ambiguous->counter == 1, "O->0 mapping accepted");
}

void testVerifierFailClosed() {
    std::printf("verifier state machine\n");
    const int64_t today = 20000;

    DeviceState state;
    VerifyResult first = Verifier::apply(state, VECTORS[0].token, SECRET, today);
    check(first == VerifyResult::Accepted, "fresh full token accepted");
    check(state.lastCounter == 1, "counter persisted");
    check(state.lockDeadlineDay == today + 30, "deadline = today + duration");
    check(state.status == DeviceStatus::Active, "status active");

    VerifyResult replay = Verifier::apply(state, VECTORS[0].token, SECRET, today);
    check(replay == VerifyResult::RejectedReplay, "same token rejected as replay");
    check(state.lastCounter == 1, "state unchanged on replay");

    VerifyResult higher = Verifier::apply(state, VECTORS[2].token, SECRET, today);
    check(higher == VerifyResult::Accepted, "higher counter accepted");
    check(state.lockDeadlineDay == today + 30 + 4095, "deadline extends from prior deadline");

    DeviceState overdue;
    overdue.lockDeadlineDay = today - 100;
    Verifier::apply(overdue, VECTORS[3].token, SECRET, today);
    check(overdue.lockDeadlineDay == today + 7, "overdue device extends from today, not past deadline");

    DeviceState bad;
    VerifyResult invalid = Verifier::apply(bad, "ZZZZZ-ZZZZZ-ZZZZZ-ZZZZZ", SECRET, today);
    check(invalid == VerifyResult::RejectedInvalid, "forged token rejected, state stays locked");
    check(bad.status == DeviceStatus::Locked, "status remains locked on reject");
}

}

int main() {
    testEncodeParity();
    testDecodeParity();
    testTamperRejects();
    testWrongSecretRejects();
    testNormalizationAccepts();
    testVerifierFailClosed();

    if (g_failures == 0) {
        std::printf("\nAll checks passed.\n");
        return 0;
    }

    std::printf("\n%d check(s) failed.\n", g_failures);
    return 1;
}
