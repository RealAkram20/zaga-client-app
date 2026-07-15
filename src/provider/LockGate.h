#pragma once

#include <string>

#include "Verifier.h"

namespace zaga {

struct GateInfo {
    bool provisioned;
    bool locked;
    std::string accountNumber;
    std::string model;
    std::string serial;
    std::string name;
    std::string statusText;
    std::string deadlineText;
};

// Bridges the credential provider to the tested core. All store access and clock
// reads live here so the COM layer never touches persistence or time directly.
class LockGate {
public:
    static GateInfo describe();

    static VerifyResult applyCode(const std::string& code, std::string& message);

private:
    static std::wstring storePath();

    static int64_t todayDay();
};

}
