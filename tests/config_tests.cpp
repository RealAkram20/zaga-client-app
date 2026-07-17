#include <cstdio>
#include <string>

#include <windows.h>

#include "DeviceConfig.h"

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

}

int main() {
    SetEnvironmentVariableW(L"ZAGA_CONFIG_HKCU", L"1");
    DeviceConfig::removeAll();

    std::printf("enforcement flag\n");
    check(!DeviceConfig::lockEnabled(), "lock disabled by default");
    DeviceConfig::setLockEnabled(true);
    check(DeviceConfig::lockEnabled(), "lock enabled after set");
    DeviceConfig::setLockEnabled(false);
    check(!DeviceConfig::lockEnabled(), "lock disabled after clear");

    std::printf("uninstall protection\n");
    check(!DeviceConfig::uninstallProtected(), "unprotected by default");

    DeviceConfig::setUninstallCode("UNINSTALL-4821");
    check(DeviceConfig::uninstallProtected(), "protected after code set");
    check(!DeviceConfig::checkUninstallCode("wrong-code"), "wrong code rejected");
    check(DeviceConfig::checkUninstallCode("UNINSTALL-4821"), "correct code accepted");

    DeviceConfig::clearUninstallCode();
    check(!DeviceConfig::uninstallProtected(), "unprotected after clear");
    check(!DeviceConfig::checkUninstallCode("UNINSTALL-4821"), "cleared code no longer matches");

    std::printf("device-generated uninstall code\n");
    DeviceConfig::removeAll();
    check(DeviceConfig::uninstallCode().empty(), "no code before one is generated");

    std::string generated = DeviceConfig::ensureUninstallCode();
    check(!generated.empty(), "a code is generated on demand");
    // Storing the code is what arms protection, so a device is never left holding a
    // code it does not enforce.
    check(DeviceConfig::uninstallProtected(), "generating a code arms protection");
    check(DeviceConfig::uninstallCode() == generated, "the generated code reads back");
    check(DeviceConfig::checkUninstallCode(generated), "the generated code authorises removal");
    check(!DeviceConfig::checkUninstallCode("AAAA-BBBB-CCCC"), "another code does not");

    // Re-running install or enroll must not invalidate the code an operator has
    // already recorded against the device on the portal.
    check(DeviceConfig::ensureUninstallCode() == generated, "an existing code is kept, not reissued");

    check(generated.find_first_of("IO01") == std::string::npos,
          "the code avoids characters misread when typed by hand");

    DeviceConfig::removeAll();
    std::string second = DeviceConfig::ensureUninstallCode();
    check(second != generated, "a different device gets a different code");

    DeviceConfig::removeAll();

    if (g_failures == 0) {
        std::printf("\nAll config checks passed.\n");
        return 0;
    }

    std::printf("\n%d config check(s) failed.\n", g_failures);
    return 1;
}
