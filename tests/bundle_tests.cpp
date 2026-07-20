#include <cstdio>
#include <string>

#include "ProvisioningBundle.h"

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

const char SECRET[] = "421a4e7c9f0b3d8e5a2c6f1b4d7e0a3c8f5b2e9d6a1c4f7b0e3d8a5c2f9b6e1d";

std::string bundleWith(const std::string& secret) {
    return std::string("{\"format\":\"zaga.provisioning.v1\",")
        + "\"account_number\":\"ZG-40000\","
        + "\"hmac_secret\":\"" + secret + "\","
        + "\"serial\":\"8CC1510VXF\",\"model\":\"ProDesk 600\",\"name\":\"Reception\"}";
}

}

// Everything a bundle can be wrong about, it is wrong about at a customer's desk, on a
// machine with no network to ask. A bundle that parses is a device that arms and locks,
// so each of these has to be caught before that and not at the logon screen.
int main() {
    std::printf("a bundle off the portal\n");
    BundleResult good = parseProvisioningBundle(bundleWith(SECRET));
    check(good.ok, "accepted");
    check(good.device.accountNumber == "ZG-40000", "account number");
    check(good.device.hmacSecretHex == SECRET, "secret");
    check(good.device.serial == "8CC1510VXF", "serial");
    check(good.device.model == "ProDesk 600", "model");
    check(good.device.name == "Reception", "name");

    std::printf("bundles worth refusing\n");
    check(!parseProvisioningBundle("").ok, "empty file");
    check(!parseProvisioningBundle("not json at all").ok, "not json");
    check(!parseProvisioningBundle("[1,2,3]").ok, "json, but not an object");

    // The shapes a half-finished download or a hand-edited file actually takes.
    check(!parseProvisioningBundle(
              "{\"account_number\":\"ZG-40000\"}").ok, "no secret");
    check(!parseProvisioningBundle(
              std::string("{\"hmac_secret\":\"") + SECRET + "\"}").ok, "no account number");

    // A short, long, or non-hex key decodes to the wrong bytes, so every code the
    // portal issues would be rejected by a device that has already locked itself.
    check(!parseProvisioningBundle(bundleWith("abab")).ok, "secret too short");
    check(!parseProvisioningBundle(bundleWith(std::string(SECRET) + "ff")).ok,
          "secret too long");
    check(!parseProvisioningBundle(
              bundleWith("zzzz4e7c9f0b3d8e5a2c6f1b4d7e0a3c8f5b2e9d6a1c4f7b0e3d8a5c2f9b6e1d")).ok,
          "secret not hex");

    std::printf(g_failures == 0 ? "\nbundle tests passed\n" : "\n%d FAILED\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
