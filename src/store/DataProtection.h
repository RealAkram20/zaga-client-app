#pragma once

#include <cstdint>
#include <vector>

namespace zaga {

// Wraps Windows DPAPI at machine scope. Machine scope is required because the
// credential provider runs at the logon screen as SYSTEM, before any user has
// logged on, so user-scope DPAPI keys are not available to decrypt the store.
class DataProtection {
public:
    static bool protect(const std::vector<uint8_t>& plaintext, std::vector<uint8_t>& blob);

    static bool unprotect(const std::vector<uint8_t>& blob, std::vector<uint8_t>& plaintext);
};

}
