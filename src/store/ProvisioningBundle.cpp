#include "ProvisioningBundle.h"

#include "Json.h"

namespace zaga {

namespace {

// The codec signs with a 32-byte key, written as hex. Anything else is not a key this
// device can verify a code with.
constexpr size_t SECRET_HEX_LENGTH = 64;

bool isHex(const std::string& text) {
    for (char c : text) {
        bool digit = c >= '0' && c <= '9';
        bool lower = c >= 'a' && c <= 'f';
        bool upper = c >= 'A' && c <= 'F';
        if (!digit && !lower && !upper) {
            return false;
        }
    }
    return true;
}

}

BundleResult parseProvisioningBundle(const std::string& text) {
    BundleResult result;

    Json bundle;
    if (!Json::parse(text, bundle) || !bundle.isObject()) {
        result.message = "That file is not a provisioning bundle.";
        return result;
    }

    result.device.accountNumber = bundle.str("account_number");
    result.device.hmacSecretHex = bundle.str("hmac_secret");
    result.device.serial = bundle.str("serial");
    result.device.model = bundle.str("model");
    result.device.name = bundle.str("name");

    // Absent in bundles exported before the plan carried a grace period; those
    // read as zero, meaning lock on the deadline.
    if (const Json* grace = bundle.get("grace_days");
        grace != nullptr && grace->type() == Json::Type::Number) {
        double days = grace->asNumber();
        if (days >= 0 && days <= 255) {
            result.device.state.graceDays = static_cast<uint8_t>(days);
        }
    }

    if (result.device.accountNumber.empty() || result.device.hmacSecretHex.empty()) {
        result.message = "That bundle has no account number and secret in it. Download "
                         "it again from the device's page on the portal.";
        return result;
    }

    if (result.device.hmacSecretHex.size() != SECRET_HEX_LENGTH ||
        !isHex(result.device.hmacSecretHex)) {
        result.message = "That bundle's secret is not a 64-character hex key. Download "
                         "it again from the device's page on the portal.";
        return result;
    }

    result.ok = true;
    return result;
}

}
