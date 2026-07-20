#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace zaga {

// The typed offline enrollment code: ZGE- plus base32 symbols of the token
// alphabet. Version 2 carries version(4) | hmac secret(256) | grace days(8) |
// pad(2) | check(40) in 62 symbols; version 1 (60 symbols, no grace field) is
// still accepted so codes printed before the field existed keep working. This is
// how a device that cannot reach the portal receives the secret its unlock codes
// are signed with — typed in like a prepaid electricity token instead of fetched
// over a network.
//
// The check is the first 40 bits of HMAC-SHA256(key = secret, msg = "ZGE2" + the
// normalized account number + the grace-days byte); v1 signs "ZGE1" + account.
// It is integrity and account binding, NOT authenticity: the key travels inside
// the code, so anyone can mint one that passes. What it guarantees is that a
// mistyped symbol is caught, that a code issued for another account refuses to
// enroll this device, and that the grace field cannot be altered in transit.
// Authorization is the installer's admin gate, and a forged secret punishes only
// its forger.
//
// The PHP twin is app/Services/OfflineEnrollCodec.php in the portal. The two must
// stay bit-identical; the shared golden vectors in each side's tests are the
// contract.
class EnrollCodec {
public:
    static constexpr int VERSION = 2;

    struct Enrollment {
        // Lowercase hex, byte-identical to what the portal minted.
        std::string secretHex;
        // Days past the deadline before the lock engages; 0 for v1 codes.
        uint8_t graceDays = 0;
    };

    // Builds the code for a 64-hex-character secret. Exists for test parity with the
    // portal; the device itself only ever decodes.
    static std::optional<std::string> encode(const std::string& secretHex,
                                             const std::string& accountNumber,
                                             uint8_t graceDays = 0);

    // Returns the enrollment payload, or nothing for any code not worth acting on.
    // A 40-bit check cannot say whether the typist slipped or the account is wrong,
    // so neither can the caller.
    static std::optional<Enrollment> decode(const std::string& code,
                                            const std::string& accountNumber);

    // The account form the check is computed over, identical character-for-character
    // to the portal's OfflineEnrollCodec::normalizeAccount: uppercase, separators
    // stripped, O/I/L/U folded the way the token alphabet reads them. Public so the
    // tests can pin the parity.
    static std::string normalizeAccount(const std::string& accountNumber);
};

}
