#include <windows.h>
#include <credentialprovider.h>

#include <cstdio>
#include <string>

#include <initguid.h>
#include "Guid.h"
#include "Fields.h"
#include "LocalStore.h"
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

const std::string SECRET =
    "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
const wchar_t* VALID_CODE = L"20002-0F04J-CEBMA-PNWJB";

std::wstring seedStorePath() {
    wchar_t dir[MAX_PATH];
    DWORD length = GetTempPathW(MAX_PATH, dir);
    return std::wstring(dir, length) + L"zaga_provider_host.bin";
}

void seedLockedDevice(const std::wstring& path) {
    StoredDevice device;
    device.accountNumber = "ZG-40000";
    device.serial = "5CG9482Q1B";
    device.model = "Dell Latitude 5490";
    device.name = "Reception PC";
    device.hmacSecretHex = SECRET;
    device.state.lastCounter = 0;
    device.state.lockDeadlineDay = 0;
    device.state.status = DeviceStatus::Locked;
    LocalStore::save(path, device);
}

}

int main() {
    std::wstring storePath = seedStorePath();
    DeleteFileW(storePath.c_str());
    seedLockedDevice(storePath);
    SetEnvironmentVariableW(L"ZAGA_STATE_PATH", storePath.c_str());
    SetEnvironmentVariableW(L"ZAGA_CONFIG_HKCU", L"1");
    DeviceConfig::removeAll();
    DeviceConfig::setLockEnabled(true);

    HMODULE dll = LoadLibraryW(L"zaga_lock_provider.dll");
    if (dll == nullptr) {
        std::printf("FAIL could not load zaga_lock_provider.dll (error %lu)\n", GetLastError());
        return 1;
    }

    auto getClassObject = reinterpret_cast<HRESULT(__stdcall*)(REFCLSID, REFIID, void**)>(
        GetProcAddress(dll, "DllGetClassObject"));
    check(getClassObject != nullptr, "DllGetClassObject exported");
    if (getClassObject == nullptr) {
        return 1;
    }

    IClassFactory* factory = nullptr;
    HRESULT hr = getClassObject(CLSID_ZagaLockProvider, IID_IClassFactory,
                                reinterpret_cast<void**>(&factory));
    check(SUCCEEDED(hr) && factory != nullptr, "class factory created");

    ICredentialProvider* provider = nullptr;
    hr = factory->CreateInstance(nullptr, IID_ICredentialProvider,
                                 reinterpret_cast<void**>(&provider));
    check(SUCCEEDED(hr) && provider != nullptr, "provider created");

    hr = provider->SetUsageScenario(CPUS_LOGON, 0);
    check(hr == S_OK, "SetUsageScenario(CPUS_LOGON) accepted");

    DWORD count = 0;
    DWORD defaultIndex = 0;
    BOOL autoLogon = TRUE;
    hr = provider->GetCredentialCount(&count, &defaultIndex, &autoLogon);
    check(SUCCEEDED(hr) && count == 1, "locked device offers one tile");

    ICredentialProviderFilter* filter = nullptr;
    hr = provider->QueryInterface(IID_ICredentialProviderFilter, reinterpret_cast<void**>(&filter));
    check(SUCCEEDED(hr) && filter != nullptr, "provider also serves as filter");
    if (filter != nullptr) {
        GUID others[2] = {CLSID_ZagaLockProvider, {0}};
        BOOL allow[2] = {FALSE, TRUE};
        hr = filter->Filter(CPUS_LOGON, 0, others, allow, 2);
        check(SUCCEEDED(hr) && allow[0] == TRUE && allow[1] == FALSE,
              "filter allows only our tile while locked");
        filter->Release();
    }

    ICredentialProviderCredential* credential = nullptr;
    hr = provider->GetCredentialAt(0, &credential);
    check(SUCCEEDED(hr) && credential != nullptr, "credential retrieved");

    CREDENTIAL_PROVIDER_GET_SERIALIZATION_RESPONSE response = CPGSR_NO_CREDENTIAL_NOT_FINISHED;
    CREDENTIAL_PROVIDER_CREDENTIAL_SERIALIZATION serialization{};
    LPWSTR statusText = nullptr;
    CREDENTIAL_PROVIDER_STATUS_ICON statusIcon = CPSI_NONE;

    credential->SetStringValue(FIELD_CODE, L"ZZZZZ-ZZZZZ-ZZZZZ-ZZZZZ");
    hr = credential->GetSerialization(&response, &serialization, &statusText, &statusIcon);
    check(SUCCEEDED(hr) && statusIcon == CPSI_ERROR, "wrong code reports an error, stays locked");
    if (statusText != nullptr) {
        CoTaskMemFree(statusText);
        statusText = nullptr;
    }

    credential->SetStringValue(FIELD_CODE, VALID_CODE);
    hr = credential->GetSerialization(&response, &serialization, &statusText, &statusIcon);
    check(SUCCEEDED(hr) && statusIcon == CPSI_SUCCESS, "valid code accepted");
    if (statusText != nullptr) {
        CoTaskMemFree(statusText);
    }

    StoredDevice after;
    bool reloaded = LocalStore::load(storePath, after);
    check(reloaded && after.state.lastCounter == 1, "counter persisted through the COM layer");
    check(reloaded && after.state.lockDeadlineDay > 0, "lock deadline extended on unlock");

    credential->SetStringValue(FIELD_CODE, VALID_CODE);
    hr = credential->GetSerialization(&response, &serialization, &statusText, &statusIcon);
    check(SUCCEEDED(hr) && statusIcon == CPSI_ERROR, "same code replayed is rejected");
    if (statusText != nullptr) {
        CoTaskMemFree(statusText);
    }

    credential->Release();
    provider->Release();
    factory->Release();
    DeviceConfig::removeAll();
    DeleteFileW(storePath.c_str());
    FreeLibrary(dll);

    if (g_failures == 0) {
        std::printf("\nAll provider host checks passed.\n");
        return 0;
    }

    std::printf("\n%d provider host check(s) failed.\n", g_failures);
    return 1;
}
