#pragma once

#include <string>

#include "LocalStore.h"

namespace zaga {

struct BundleResult {
    bool ok = false;
    StoredDevice device;
    // Why the bundle was refused, in words worth showing to whoever is holding the
    // memory stick.
    std::string message;
};

// Reads a provisioning bundle downloaded from the portal, for a device that has no way
// to enrol over the network.
//
// Every refusal here is a refusal to arm. A bundle that is short a field, or carries a
// secret of the wrong size, would write a store that cannot verify a single unlock
// code — and the device would find that out at the logon screen, after it had already
// locked, with nothing that opens it again.
BundleResult parseProvisioningBundle(const std::string& text);

}
