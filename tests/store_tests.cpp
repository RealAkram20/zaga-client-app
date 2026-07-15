#include <cstdio>
#include <string>

#include <windows.h>

#include "DataProtection.h"
#include "LocalStore.h"

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

StoredDevice sampleDevice() {
    StoredDevice device;
    device.accountNumber = "ZG-40000";
    device.serial = "SN-ABC-123";
    device.model = "Latitude 5490";
    device.name = "Jane's laptop";
    device.hmacSecretHex =
        "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
    device.biosPassword = "bios-secret";
    device.recoveryKey = "111111-222222-333333-444444-555555-666666-777777-888888";
    device.uninstallCode = "UNINSTALL-9876";
    device.deviceToken = "1|N2bXh9AchvGAfLHIfgOTHMnFHPpHi1GqMhptRo2z";
    device.state.lastCounter = 42;
    device.state.lockDeadlineDay = 20123;
    device.state.status = DeviceStatus::Active;
    device.state.lastTokenWasGrace = true;
    return device;
}

bool sameDevice(const StoredDevice& a, const StoredDevice& b) {
    return a.accountNumber == b.accountNumber
        && a.serial == b.serial
        && a.model == b.model
        && a.name == b.name
        && a.hmacSecretHex == b.hmacSecretHex
        && a.biosPassword == b.biosPassword
        && a.recoveryKey == b.recoveryKey
        && a.uninstallCode == b.uninstallCode
        && a.deviceToken == b.deviceToken
        && a.state.lastCounter == b.state.lastCounter
        && a.state.lockDeadlineDay == b.state.lockDeadlineDay
        && a.state.status == b.state.status
        && a.state.lastTokenWasGrace == b.state.lastTokenWasGrace;
}

std::wstring tempPath() {
    wchar_t directory[MAX_PATH];
    DWORD length = GetTempPathW(MAX_PATH, directory);
    std::wstring path(directory, length);
    return path + L"zaga_store_test.bin";
}

void testSerializeRoundTrip() {
    std::printf("serialize round-trip\n");
    StoredDevice original = sampleDevice();
    std::vector<uint8_t> bytes = LocalStore::serialize(original);

    StoredDevice restored;
    check(LocalStore::deserialize(bytes, restored), "deserialize succeeds");
    check(sameDevice(original, restored), "all fields survive round-trip");
}

void testDeserializeRejectsGarbage() {
    std::printf("deserialize rejects bad input\n");
    std::vector<uint8_t> empty;
    StoredDevice out;
    check(!LocalStore::deserialize(empty, out), "empty buffer rejected");

    std::vector<uint8_t> wrongMagic = {'X', 'X', 'X', 'X', 1};
    check(!LocalStore::deserialize(wrongMagic, out), "wrong magic rejected");

    std::vector<uint8_t> truncated = LocalStore::serialize(sampleDevice());
    truncated.resize(truncated.size() / 2);
    check(!LocalStore::deserialize(truncated, out), "truncated buffer rejected");
}

void testDpapiRoundTrip() {
    std::printf("DPAPI round-trip\n");
    std::vector<uint8_t> plaintext = LocalStore::serialize(sampleDevice());

    std::vector<uint8_t> blob;
    check(DataProtection::protect(plaintext, blob), "protect succeeds");
    check(blob != plaintext, "blob differs from plaintext");

    std::vector<uint8_t> recovered;
    check(DataProtection::unprotect(blob, recovered), "unprotect succeeds");
    check(recovered == plaintext, "unprotected bytes match original");

    if (!blob.empty()) {
        std::vector<uint8_t> tampered = blob;
        tampered[tampered.size() / 2] ^= 0xFF;
        std::vector<uint8_t> shouldFail;
        check(!DataProtection::unprotect(tampered, shouldFail), "tampered blob rejected");
    }
}

void testSaveLoad() {
    std::printf("save / load to disk\n");
    std::wstring path = tempPath();
    DeleteFileW(path.c_str());

    StoredDevice original = sampleDevice();
    check(LocalStore::save(path, original), "save succeeds");

    StoredDevice loaded;
    check(LocalStore::load(path, loaded), "load succeeds");
    check(sameDevice(original, loaded), "loaded device matches saved");

    StoredDevice missing;
    check(!LocalStore::load(path + L".nope", missing), "missing file fails closed");

    DeleteFileW(path.c_str());
}

}

int main() {
    testSerializeRoundTrip();
    testDeserializeRejectsGarbage();
    testDpapiRoundTrip();
    testSaveLoad();

    if (g_failures == 0) {
        std::printf("\nAll store checks passed.\n");
        return 0;
    }

    std::printf("\n%d store check(s) failed.\n", g_failures);
    return 1;
}
