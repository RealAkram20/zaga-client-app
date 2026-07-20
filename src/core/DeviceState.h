#pragma once

#include <cstdint>

namespace zaga {

enum class DeviceStatus {
    Active,
    Grace,
    Overdue,
    Locked,
};

// Dates are days since an arbitrary fixed epoch. The core stays clock-free so it
// is testable; the credential-provider layer supplies "today" from the system.
struct DeviceState {
    uint32_t lastCounter = 0;
    int64_t lockDeadlineDay = 0;
    DeviceStatus status = DeviceStatus::Locked;
    bool lastTokenWasGrace = false;
    // Days past the deadline before the gate actually locks; 0 = lock on the
    // deadline. Set by the plan at enrollment and refreshed on check-in.
    uint8_t graceDays = 0;
};

}
