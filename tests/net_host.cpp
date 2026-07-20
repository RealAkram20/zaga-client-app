#include <cstdio>
#include <string>

#include "PortalClient.h"

using namespace zaga;

// Manual connectivity check against a running portal. Not part of ctest: it needs
// a live server and a fresh, single-use enrollment code.
//   net_host <base-url> <enrollment-code>
int main(int argc, char** argv) {
    if (argc < 3) {
        std::printf("usage: net_host <base-url> <enrollment-code>\n");
        return 2;
    }

    PortalClient client(argv[1]);

    EnrollResult enroll = client.enroll(argv[2], "0.1.0");
    if (!enroll.ok) {
        std::printf("enroll FAILED: %s\n", enroll.message.c_str());
        return 1;
    }
    std::printf("enroll ok: account=%s secret_len=%zu token_len=%zu serial=%s model=%s\n",
                enroll.accountNumber.c_str(), enroll.hmacSecret.size(),
                enroll.token.size(), enroll.serial.c_str(), enroll.model.c_str());

    HeartbeatResult heartbeat = client.heartbeat(enroll.token, "locked", "0.1.0");
    std::printf("heartbeat: %s grace_days=%d\n", heartbeat.ok ? "ok" : "FAILED",
                heartbeat.graceDays);

    TokenResult token = client.fetchToken(enroll.token);
    std::printf("fetch-token: %s\n", token.ok ? token.token.c_str() : token.message.c_str());

    return 0;
}
