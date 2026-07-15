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

    bool heartbeat(const std::string& deviceToken,
                   const std::string& status,
                   const std::string& agentVersion);

    TokenResult fetchToken(const std::string& deviceToken);

private:
    std::string url(const std::string& path) const;

    std::string _baseUrl;
};

}
