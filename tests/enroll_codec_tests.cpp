#include <cstdio>
#include <string>

#include "EnrollCodec.h"

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

std::string secretOf(const std::optional<EnrollCodec::Enrollment>& enrollment) {
    return enrollment ? enrollment->secretHex : "";
}

int graceOf(const std::optional<EnrollCodec::Enrollment>& enrollment) {
    return enrollment ? enrollment->graceDays : -1;
}

// The cross-language contract, pinned verbatim in the portal's
// tests/Unit/OfflineEnrollCodecTest.php. If either side drifts, its own tests fail
// before a device can.
const char SECRET[] = "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f";
const char ACCOUNT[] = "ZG-KUXQ2-MTR38";
const char GOLDEN[] =
    "ZGE-40010-81G81-860W4-0J2GB-1G6GW-3RG24-91650-N2RBH-G68T3-CE1T7-GZ00M-1N4WR-2E";
const char GOLDEN_GRACE[] =
    "ZGE-40010-81G81-860W4-0J2GB-1G6GW-3RG24-91650-N2RBH-G68T3-CE1T7-GZ1RA-DGE37-9H";
const char GOLDEN_V1[] =
    "ZGE-20010-81G81-860W4-0J2GB-1G6GW-3RG24-91650-N2RBH-G68T3-CE1T7-GZY51-7W4GB";

}

int main() {
    std::printf("parity with the portal\n");
    check(EnrollCodec::encode(SECRET, ACCOUNT).value_or("") == GOLDEN,
          "encode reproduces the golden vector");
    check(EnrollCodec::encode(SECRET, ACCOUNT, 14).value_or("") == GOLDEN_GRACE,
          "encode with grace reproduces its golden vector");
    check(secretOf(EnrollCodec::decode(GOLDEN, ACCOUNT)) == SECRET,
          "golden vector decodes to the pinned secret");
    check(graceOf(EnrollCodec::decode(GOLDEN, ACCOUNT)) == 0,
          "zero-grace code decodes to zero grace days");
    check(graceOf(EnrollCodec::decode(GOLDEN_GRACE, ACCOUNT)) == 14,
          "grace days survive the roundtrip");

    std::printf("version 1 codes still enroll\n");
    check(secretOf(EnrollCodec::decode(GOLDEN_V1, ACCOUNT)) == SECRET,
          "v1 golden vector still decodes");
    check(graceOf(EnrollCodec::decode(GOLDEN_V1, ACCOUNT)) == 0,
          "v1 codes read as zero grace days");

    std::printf("typing habits are forgiven\n");
    {
        std::string lower = GOLDEN;
        for (char& c : lower) c = static_cast<char>(tolower(c));
        check(secretOf(EnrollCodec::decode(lower, ACCOUNT)) == SECRET, "lowercase");

        std::string spaced = GOLDEN;
        for (char& c : spaced) if (c == '-') c = ' ';
        check(secretOf(EnrollCodec::decode(spaced, ACCOUNT)) == SECRET, "spaces for dashes");

        std::string bare;
        for (char c : std::string(GOLDEN)) if (c != '-') bare += c;
        check(secretOf(EnrollCodec::decode(bare, ACCOUNT)) == SECRET, "no separators");
    }

    std::printf("account folding absorbs look-alike registration typos\n");
    check(secretOf(EnrollCodec::decode(GOLDEN, "ZG-KVXQ2-MTR38")) == SECRET,
          "U read as V still enrolls");
    check(secretOf(EnrollCodec::decode(GOLDEN, "zg kuxq2 mtr38")) == SECRET,
          "lowercase spaced account");
    check(EnrollCodec::normalizeAccount("zg-kUxq2-mtr38") ==
              EnrollCodec::normalizeAccount("ZG KVXQ2 MTR38"),
          "normalizeAccount parity fold");

    std::printf("codes worth refusing\n");
    check(!EnrollCodec::decode(GOLDEN, "ZG-40000").has_value(), "wrong account");
    {
        // Every one of the 62 symbols, mistyped, must be caught — the grace field
        // included, or a slipped finger could quietly change the lock date.
        std::string symbols;
        for (size_t i = 4; i < sizeof(GOLDEN_GRACE) - 1; ++i) {
            if (GOLDEN_GRACE[i] != '-') symbols += GOLDEN_GRACE[i];
        }
        bool allCaught = true;
        for (size_t i = 0; i < symbols.size(); ++i) {
            std::string tampered = symbols;
            tampered[i] = symbols[i] == 'A' ? 'B' : 'A';
            if (EnrollCodec::decode("ZGE" + tampered, ACCOUNT).has_value()) {
                allCaught = false;
            }
        }
        check(allCaught, "every mistyped symbol rejected");

        check(!EnrollCodec::decode("ZGE" + symbols.substr(1), ACCOUNT).has_value(),
              "one symbol short");
        check(!EnrollCodec::decode("ZGE" + symbols + "2", ACCOUNT).has_value(),
              "one symbol long");
        check(!EnrollCodec::decode(symbols, ACCOUNT).has_value(), "missing prefix");
    }
    check(!EnrollCodec::decode("ZGE4B7K2MP", ACCOUNT).has_value(),
          "an online-style short code is rejected, not crashed");
    check(!EnrollCodec::decode("", ACCOUNT).has_value(), "empty input");

    std::printf("grace extremes survive a roundtrip\n");
    for (int days : {1, 60, 255}) {
        std::string code = EnrollCodec::encode(SECRET, ACCOUNT,
                                               static_cast<uint8_t>(days)).value_or("");
        check(graceOf(EnrollCodec::decode(code, ACCOUNT)) == days,
              "grace " + std::to_string(days));
    }

    std::printf("encode refuses broken inputs\n");
    check(!EnrollCodec::encode("abab", ACCOUNT).has_value(), "short secret");
    check(!EnrollCodec::encode(SECRET, "  ").has_value(), "blank account");

    std::printf(g_failures == 0 ? "\nenroll codec tests passed\n" : "\n%d FAILED\n",
                g_failures);
    return g_failures == 0 ? 0 : 1;
}
