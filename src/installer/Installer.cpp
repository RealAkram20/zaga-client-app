#include <windows.h>
#include <shlobj.h>

#include <cstdio>
#include <string>
#include <vector>

#include "DeviceConfig.h"
#include "LocalStore.h"
#include "LockGate.h"
#include "PortalClient.h"

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")

using namespace zaga;

namespace {

const wchar_t DLL_NAME[] = L"zaga_lock_provider.dll";
const wchar_t INSTALLER_NAME[] = L"zaga_installer.exe";
const wchar_t TASK_NAME[] = L"Zaga Device Heartbeat";
const char AGENT_VERSION[] = "0.1.0";

std::wstring exeDirectory() {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring full(path);
    size_t slash = full.find_last_of(L"\\/");
    return slash == std::wstring::npos ? full : full.substr(0, slash);
}

std::wstring installDirectory() {
    PWSTR programFiles = nullptr;
    std::wstring base = L"C:\\Program Files";
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_ProgramFiles, 0, nullptr, &programFiles))) {
        base = programFiles;
        CoTaskMemFree(programFiles);
    }
    return base + L"\\Zaga";
}

std::wstring installedDll() {
    return installDirectory() + L"\\" + DLL_NAME;
}

std::wstring installedInstaller() {
    return installDirectory() + L"\\" + INSTALLER_NAME;
}

std::wstring selfPath() {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    return std::wstring(path);
}

std::wstring systemExe(const wchar_t* name) {
    wchar_t dir[MAX_PATH];
    GetSystemDirectoryW(dir, MAX_PATH);
    return std::wstring(dir) + L"\\" + name;
}

DWORD runProcess(const std::wstring& commandLine) {
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};

    std::vector<wchar_t> buffer(commandLine.begin(), commandLine.end());
    buffer.push_back(L'\0');

    if (!CreateProcessW(nullptr, buffer.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process)) {
        return static_cast<DWORD>(-1);
    }

    WaitForSingleObject(process.hProcess, 30000);
    DWORD exitCode = 0;
    GetExitCodeProcess(process.hProcess, &exitCode);
    CloseHandle(process.hProcess);
    CloseHandle(process.hThread);
    return exitCode;
}

bool registerHeartbeatTask() {
    std::wstring command = systemExe(L"schtasks.exe") +
        L" /Create /F /RU SYSTEM /SC HOURLY" +
        L" /TN \"" + TASK_NAME + L"\"" +
        L" /TR \"\\\"" + installedInstaller() + L"\\\" heartbeat\"";
    return runProcess(command) == 0;
}

void removeHeartbeatTask() {
    std::wstring command = systemExe(L"schtasks.exe") +
        L" /Delete /F /TN \"" + TASK_NAME + L"\"";
    runProcess(command);
}

bool callDllEntry(const std::wstring& dllPath, const char* entry) {
    HMODULE module = LoadLibraryW(dllPath.c_str());
    if (module == nullptr) {
        return false;
    }

    using EntryFn = HRESULT(__stdcall*)();
    auto fn = reinterpret_cast<EntryFn>(GetProcAddress(module, entry));
    HRESULT hr = fn != nullptr ? fn() : E_FAIL;

    FreeLibrary(module);
    return SUCCEEDED(hr);
}

std::string narrow(const std::wstring& text) {
    if (text.empty()) {
        return std::string();
    }
    int length = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string narrowed(length > 0 ? length - 1 : 0, '\0');
    if (length > 0) {
        WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, &narrowed[0], length, nullptr, nullptr);
    }
    return narrowed;
}

std::string argValue(int argc, wchar_t** argv, const wchar_t* name) {
    for (int i = 2; i + 1 < argc; ++i) {
        if (_wcsicmp(argv[i], name) == 0) {
            return narrow(argv[i + 1]);
        }
    }
    return std::string();
}

int doInstall() {
    std::wstring source = exeDirectory() + L"\\" + DLL_NAME;
    if (GetFileAttributesW(source.c_str()) == INVALID_FILE_ATTRIBUTES) {
        std::printf("Place %ls next to the installer and try again.\n", DLL_NAME);
        return 1;
    }

    std::wstring directory = installDirectory();
    SHCreateDirectoryExW(nullptr, directory.c_str(), nullptr);

    if (!CopyFileW(source.c_str(), installedDll().c_str(), FALSE)) {
        std::printf("Could not copy the provider into %ls (run as administrator).\n",
                    directory.c_str());
        return 1;
    }

    if (!callDllEntry(installedDll(), "DllRegisterServer")) {
        std::printf("Copied the provider but registration failed (run as administrator).\n");
        return 1;
    }

    CopyFileW(selfPath().c_str(), installedInstaller().c_str(), FALSE);
    bool scheduled = registerHeartbeatTask();

    DeviceConfig::setLockEnabled(false);

    std::printf("Installed. The lock is OFF (dormant) and removal is not protected.\n");
    std::printf("Hourly portal check-in %s.\n",
                scheduled ? "scheduled" : "could not be scheduled");
    std::printf("Provision the device, then run \"zaga_installer enable\" to arm it.\n");
    return 0;
}

int doUninstall(int argc, wchar_t** argv) {
    if (DeviceConfig::uninstallProtected()) {
        std::string code = argValue(argc, argv, L"--code");
        if (code.empty() || !DeviceConfig::checkUninstallCode(code)) {
            std::printf("Removal is protected. Pass the correct --code to uninstall.\n");
            return 1;
        }
    }

    removeHeartbeatTask();
    callDllEntry(installedDll(), "DllUnregisterServer");
    DeleteFileW(installedDll().c_str());
    DeleteFileW(installedInstaller().c_str());
    RemoveDirectoryW(installDirectory().c_str());
    DeviceConfig::removeAll();

    std::printf("Uninstalled. The provider is unregistered and settings are cleared.\n");
    return 0;
}

int doProvision(int argc, wchar_t** argv) {
    std::string account = argValue(argc, argv, L"--account");
    std::string secret = argValue(argc, argv, L"--secret");
    if (account.empty() || secret.empty()) {
        std::printf("Usage: zaga_installer provision --account ZG-00000 --secret <64 hex> "
                    "[--serial S] [--model M] [--name N]\n");
        return 1;
    }

    StoredDevice device;
    device.accountNumber = account;
    device.hmacSecretHex = secret;
    device.serial = argValue(argc, argv, L"--serial");
    device.model = argValue(argc, argv, L"--model");
    device.name = argValue(argc, argv, L"--name");

    if (!LocalStore::save(LocalStore::defaultPath(), device)) {
        std::printf("Could not write the device store (run as administrator).\n");
        return 1;
    }

    std::printf("Provisioned %s. The device store is written.\n", account.c_str());
    return 0;
}

std::string resolvePortalUrl(int argc, wchar_t** argv) {
    std::string url = argValue(argc, argv, L"--url");
    if (!url.empty()) {
        DeviceConfig::setPortalUrl(url);
        return url;
    }
    return DeviceConfig::portalUrl();
}

int doEnroll(int argc, wchar_t** argv) {
    std::string code = argValue(argc, argv, L"--code");
    std::string portal = resolvePortalUrl(argc, argv);
    if (code.empty() || portal.empty()) {
        std::printf("Usage: zaga_installer enroll --code <code> --url <portal url>\n");
        return 1;
    }

    EnrollResult result = PortalClient(portal).enroll(code, AGENT_VERSION);
    if (!result.ok) {
        std::printf("Enrollment failed: %s\n", result.message.c_str());
        return 1;
    }

    StoredDevice device;
    device.accountNumber = result.accountNumber;
    device.hmacSecretHex = result.hmacSecret;
    device.serial = result.serial;
    device.model = result.model;
    device.name = result.name;
    device.deviceToken = result.token;

    if (!LocalStore::save(LocalStore::defaultPath(), device)) {
        std::printf("Enrolled, but could not write the device store (run as administrator).\n");
        return 1;
    }

    std::printf("Enrolled %s from the portal. The device store is written.\n",
                result.accountNumber.c_str());
    return 0;
}

int doHeartbeat() {
    std::string portal = DeviceConfig::portalUrl();
    StoredDevice device;
    if (portal.empty() || !LocalStore::load(LocalStore::defaultPath(), device) ||
        device.deviceToken.empty()) {
        std::printf("Not enrolled. Run \"enroll\" first.\n");
        return 1;
    }

    GateInfo info = LockGate::describe();
    bool ok = PortalClient(portal).heartbeat(device.deviceToken, info.statusText, AGENT_VERSION);
    std::printf(ok ? "Checked in with the portal.\n" : "Could not reach the portal.\n");
    return ok ? 0 : 1;
}

int doFetchToken() {
    std::string portal = DeviceConfig::portalUrl();
    StoredDevice device;
    if (portal.empty() || !LocalStore::load(LocalStore::defaultPath(), device) ||
        device.deviceToken.empty()) {
        std::printf("Not enrolled. Run \"enroll\" first.\n");
        return 1;
    }

    TokenResult result = PortalClient(portal).fetchToken(device.deviceToken);
    if (!result.ok) {
        std::printf("Could not fetch a token: %s\n", result.message.c_str());
        return 1;
    }

    std::string message;
    VerifyResult applied = LockGate::applyCode(result.token, message);
    std::printf("%s\n", message.c_str());
    return applied == VerifyResult::Accepted ? 0 : 1;
}

int doProtect(int argc, wchar_t** argv) {
    std::string code = argValue(argc, argv, L"--code");
    if (code.empty()) {
        std::printf("Usage: zaga_installer protect --code <uninstall code>\n");
        return 1;
    }

    DeviceConfig::setUninstallCode(code);
    std::printf("Removal protection is on. The code is required to uninstall.\n");
    return 0;
}

int doUnprotect(int argc, wchar_t** argv) {
    std::string code = argValue(argc, argv, L"--code");
    if (!DeviceConfig::checkUninstallCode(code)) {
        std::printf("Incorrect code. Removal protection stays on.\n");
        return 1;
    }

    DeviceConfig::clearUninstallCode();
    std::printf("Removal protection is off.\n");
    return 0;
}

int doStatus() {
    GateInfo info = LockGate::describe();
    std::printf("Lock enabled:        %s\n", DeviceConfig::lockEnabled() ? "yes" : "no");
    std::printf("Removal protected:   %s\n", DeviceConfig::uninstallProtected() ? "yes" : "no");
    std::printf("Provisioned:         %s\n", info.provisioned ? "yes" : "no");
    if (info.provisioned) {
        std::printf("Account:             %s\n", info.accountNumber.c_str());
    }
    std::printf("Currently locking:   %s\n", info.locked ? "yes" : "no");
    std::printf("Status:              %s\n", info.statusText.c_str());
    return 0;
}

void usage() {
    std::printf(
        "Zaga Device Lock installer\n\n"
        "  install                         copy and register the provider (dormant)\n"
        "  uninstall [--code <c>]          unregister and remove\n"
        "  enroll --code <c> --url <u>     provision from the portal over the network\n"
        "  provision --account --secret    write the device store from a bundle offline\n"
        "  heartbeat                       check in with the portal\n"
        "  fetch-token                     pull an unlock token from the portal and apply it\n"
        "  schedule | unschedule           add or remove the hourly check-in task\n"
        "  set-url --url <u>               set the portal base url\n"
        "  enable | disable                turn the lock on or off\n"
        "  protect --code <c>              require a code to uninstall\n"
        "  unprotect --code <c>            remove uninstall protection\n"
        "  status                          show the current state\n");
}

}

int wmain(int argc, wchar_t** argv) {
    if (argc < 2) {
        usage();
        return 1;
    }

    std::wstring command = argv[1];

    if (command == L"install") {
        return doInstall();
    }
    if (command == L"uninstall") {
        return doUninstall(argc, argv);
    }
    if (command == L"provision") {
        return doProvision(argc, argv);
    }
    if (command == L"enroll") {
        return doEnroll(argc, argv);
    }
    if (command == L"heartbeat") {
        return doHeartbeat();
    }
    if (command == L"fetch-token") {
        return doFetchToken();
    }
    if (command == L"schedule") {
        std::printf(registerHeartbeatTask() ? "Hourly check-in scheduled.\n"
                                            : "Could not schedule the check-in.\n");
        return 0;
    }
    if (command == L"unschedule") {
        removeHeartbeatTask();
        std::printf("Check-in schedule removed.\n");
        return 0;
    }
    if (command == L"set-url") {
        std::string url = argValue(argc, argv, L"--url");
        if (url.empty()) {
            std::printf("Usage: zaga_installer set-url --url <portal url>\n");
            return 1;
        }
        DeviceConfig::setPortalUrl(url);
        std::printf("Portal URL set.\n");
        return 0;
    }
    if (command == L"enable") {
        DeviceConfig::setLockEnabled(true);
        std::printf("The lock is enabled.\n");
        return 0;
    }
    if (command == L"disable") {
        DeviceConfig::setLockEnabled(false);
        std::printf("The lock is disabled.\n");
        return 0;
    }
    if (command == L"protect") {
        return doProtect(argc, argv);
    }
    if (command == L"unprotect") {
        return doUnprotect(argc, argv);
    }
    if (command == L"status") {
        return doStatus();
    }

    usage();
    return 1;
}
