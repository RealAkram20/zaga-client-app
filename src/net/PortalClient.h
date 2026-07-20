#pragma once

#include <string>

namespace zaga {

struct EnrollResult {
    bool ok;
    std::string message;
    std::string token;
    std::string accountNumber;
    std::string hmacSecret;
    std::string serial;
    std::string model;
    std::string name;
    // Days past the deadline before the lock engages, from the device's plan.
    int graceDays = 0;
};

struct HeartbeatResult {
    bool ok = false;
    // The plan's current grace allowance, so a check-in refreshes a value that
    // may have changed on the portal since enrollment. Negative = not reported.
    int graceDays = -1;
};

struct TokenResult {
    bool ok;
    std::string message;
    std::string token;
};

// Talks to the portal device API described in docs/CONNECTIVITY.md. Never logs the
// device token or the secret it receives.
class PortalClient {
public:
    explicit PortalClient(const std::string& baseUrl);

    EnrollResult enroll(const std::string& enrollmentCode, const std::string& agentVersion);

    HeartbeatResult heartbeat(const std::string& deviceToken,
                              const std::string& status,
                              const std::string& agentVersion);

    TokenResult fetchToken(const std::string& deviceToken);

private:
    std::string url(const std::string& path) const;

    std::string _baseUrl;
};

}
