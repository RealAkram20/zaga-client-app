#include <windows.h>
#include <shlobj.h>
#include <shellapi.h>
#include <objbase.h>

#include <cstdio>
#include <iostream>
#include <string>
#include <vector>

#include "DeviceConfig.h"
#include "LocalStore.h"
#include "LockGate.h"
#include "PortalClient.h"

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "uuid.lib")

using namespace zaga;

namespace {

const wchar_t DLL_NAME[] = L"zaga_lock_provider.dll";
const wchar_t INSTALLER_NAME[] = L"zaga_installer.exe";
const wchar_t APP_NAME[] = L"zaga_app.exe";
const wchar_t SHORTCUT_NAME[] = L"Zaga Device Lock.lnk";
const wchar_t TASK_NAME[] = L"Zaga Device Heartbeat";
const wchar_t UNINSTALL_KEY[] =
    L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\ZagaDeviceLock";
const char AGENT_VERSION[] = "0.1.0";
const wchar_t DISPLAY_VERSION[] = L"0.1.0";

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

std::wstring installedApp() {
    return installDirectory() + L"\\" + APP_NAME;
}

// The Start-menu (All Users) shortcut that launches the management app.
std::wstring startMenuShortcut() {
    PWSTR programs = nullptr;
    std::wstring base = L"C:\\ProgramData\\Microsoft\\Windows\\Start Menu\\Programs";
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_CommonPrograms, 0, nullptr, &programs))) {
        base = programs;
        CoTaskMemFree(programs);
    }
    return base + L"\\" + SHORTCUT_NAME;
}

// Write a .lnk pointing at the installed app. Best-effort: a missing shortcut only
// costs the Start-menu entry, not the install.
bool createShortcut(const std::wstring& target, const std::wstring& link,
                    const std::wstring& description) {
    bool ownInit = SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED));
    IShellLinkW* shellLink = nullptr;
    bool ok = false;
    if (SUCCEEDED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                                   IID_IShellLinkW, reinterpret_cast<void**>(&shellLink)))) {
        shellLink->SetPath(target.c_str());
        std::wstring workDir = installDirectory();
        shellLink->SetWorkingDirectory(workDir.c_str());
        shellLink->SetDescription(description.c_str());
        shellLink->SetIconLocation(target.c_str(), 0);

        IPersistFile* persist = nullptr;
        if (SUCCEEDED(shellLink->QueryInterface(IID_IPersistFile,
                                                reinterpret_cast<void**>(&persist)))) {
            ok = SUCCEEDED(persist->Save(link.c_str(), TRUE));
            persist->Release();
        }
        shellLink->Release();
    }
    if (ownInit) {
        CoUninitialize();
    }
    return ok;
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

bool registerHeartbeatTask(const std::wstring& exePath) {
    std::wstring command = systemExe(L"schtasks.exe") +
        L" /Create /F /RU SYSTEM /SC HOURLY" +
        L" /TN \"" + TASK_NAME + L"\"" +
        L" /TR \"\\\"" + exePath + L"\\\" heartbeat\"";
    return runProcess(command) == 0;
}

void removeHeartbeatTask() {
    std::wstring command = systemExe(L"schtasks.exe") +
        L" /Delete /F /TN \"" + TASK_NAME + L"\"";
    runProcess(command);
}

bool relaunchElevated(const std::wstring& parameters) {
    std::wstring self = selfPath();
    SHELLEXECUTEINFOW info{};
    info.cbSize = sizeof(info);
    info.lpVerb = L"runas";
    info.lpFile = self.c_str();
    info.lpParameters = parameters.c_str();
    info.nShow = SW_SHOWNORMAL;
    return ShellExecuteExW(&info) == TRUE;
}

void registerAddRemove() {
    std::wstring exe = installedInstaller();
    auto setString = [&](const wchar_t* name, const std::wstring& value) {
        RegSetKeyValueW(HKEY_LOCAL_MACHINE, UNINSTALL_KEY, name, REG_SZ,
                        value.c_str(), static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
    };
    auto setDword = [&](const wchar_t* name, DWORD value) {
        RegSetKeyValueW(HKEY_LOCAL_MACHINE, UNINSTALL_KEY, name, REG_DWORD, &value, sizeof(value));
    };

    setString(L"DisplayName", L"Zaga Device Lock");
    setString(L"DisplayVersion", DISPLAY_VERSION);
    setString(L"Publisher", L"Zaga");
    setString(L"InstallLocation", installDirectory());
    setString(L"DisplayIcon", installedApp());
    setString(L"UninstallString", L"\"" + exe + L"\" uninstall-ui");
    setString(L"QuietUninstallString", L"\"" + exe + L"\" uninstall");
    setDword(L"NoModify", 1);
    setDword(L"NoRepair", 1);
}

void unregisterAddRemove() {
    RegDeleteTreeW(HKEY_LOCAL_MACHINE, UNINSTALL_KEY);
}

std::string promptLine(const char* label) {
    std::printf("%s", label);
    std::fflush(stdout);
    std::string line;
    std::getline(std::cin, line);
    return line;
}

void pauseForUser() {
    std::printf("\nPress Enter to close...");
    std::string line;
    std::getline(std::cin, line);
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

bool isElevated() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        return false;
    }
    TOKEN_ELEVATION elevation{};
    DWORD size = sizeof(elevation);
    bool ok = GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size);
    CloseHandle(token);
    return ok && elevation.TokenIsElevated != 0;
}

bool requireAdmin() {
    if (isElevated()) {
        return true;
    }
    std::printf("This command needs an administrator Command Prompt.\n"
                "Right-click Command Prompt, choose \"Run as administrator\", then run it again.\n");
    return false;
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

    // Deploy the management app and a Start-menu shortcut so the user (or a
    // technician) has a real window to open after install. Both are optional: if
    // the app is not shipped next to the installer, the rest still works.
    std::wstring appSource = exeDirectory() + L"\\" + APP_NAME;
    bool haveApp = CopyFileW(appSource.c_str(), installedApp().c_str(), FALSE) != 0;
    if (haveApp) {
        createShortcut(installedApp(), startMenuShortcut(), L"Zaga Device Lock");
    }

    bool scheduled = registerHeartbeatTask(installedInstaller());
    registerAddRemove();

    DeviceConfig::setLockEnabled(false);

    std::printf("Installed. The lock is OFF (dormant) and removal is not protected.\n");
    std::printf("Management app: %s\n",
                haveApp ? "installed (Start menu > Zaga Device Lock)"
                        : "not shipped next to the installer (skipped)");
    std::printf("Hourly portal check-in %s.\n",
                scheduled ? "scheduled" : "could not be scheduled");
    std::printf("Provision the device, then run \"zaga_installer enable\" to arm it.\n");
    return 0;
}

// Thin registration for a packaged install (the Inno wizard). The package owns the
// files, shortcuts, and Add/Remove entry; this only registers the COM provider
// beside the exe, schedules the check-in, and defaults the lock to dormant.
int doRegister() {
    std::wstring dll = exeDirectory() + L"\\" + DLL_NAME;
    if (GetFileAttributesW(dll.c_str()) == INVALID_FILE_ATTRIBUTES) {
        std::printf("Provider DLL not found next to the installer.\n");
        return 1;
    }
    if (!callDllEntry(dll, "DllRegisterServer")) {
        std::printf("Provider registration failed (run as administrator).\n");
        return 1;
    }

    bool scheduled = registerHeartbeatTask(selfPath());
    DeviceConfig::setLockEnabled(false);

    std::printf("Registered. The lock is dormant; hourly check-in %s.\n",
                scheduled ? "scheduled" : "could not be scheduled");
    return 0;
}

// Undo doRegister for a packaged uninstall. The package removes the files.
int doUnregister() {
    removeHeartbeatTask();
    callDllEntry(exeDirectory() + L"\\" + DLL_NAME, "DllUnregisterServer");
    DeviceConfig::removeAll();
    std::printf("Unregistered. The provider is removed and settings are cleared.\n");
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
    unregisterAddRemove();
    callDllEntry(installedDll(), "DllUnregisterServer");
    DeleteFileW(startMenuShortcut().c_str());

    // Delete the program files, falling back to a delete-on-reboot for anything
    // still in use — e.g. the app exe when uninstall was launched from the app,
    // or this installer if it is running from the install folder.
    auto removeFile = [](const std::wstring& path) {
        if (!DeleteFileW(path.c_str()) &&
            GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) {
            MoveFileExW(path.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);
        }
    };
    removeFile(installedApp());
    removeFile(installedDll());
    removeFile(installedInstaller());
    if (!RemoveDirectoryW(installDirectory().c_str())) {
        MoveFileExW(installDirectory().c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);
    }
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

int enrollWith(const std::string& code, const std::string& portal) {
    if (code.empty() || portal.empty()) {
        std::printf("Usage: zaga_installer enroll --code <code> --url <portal url>\n");
        return 1;
    }

    DeviceConfig::setPortalUrl(portal);
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

int doEnroll(int argc, wchar_t** argv) {
    return enrollWith(argValue(argc, argv, L"--code"), resolvePortalUrl(argc, argv));
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

int doApplyCode(int argc, wchar_t** argv) {
    std::string code = argValue(argc, argv, L"--code");
    if (code.empty()) {
        std::printf("Usage: zaga_installer apply-code --code <unlock code>\n");
        return 1;
    }

    std::string message;
    VerifyResult applied = LockGate::applyCode(code, message);
    std::printf("%s\n", message.c_str());
    return applied == VerifyResult::Accepted ? 0 : 1;
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

int runSetup() {
    std::printf("Zaga Device Lock setup\n\n");

    int installed = doInstall();
    if (installed != 0) {
        pauseForUser();
        return installed;
    }

    std::printf("\nProvision this device now? Leave the URL blank to skip.\n");
    std::string url = promptLine("Portal URL (e.g. http://192.168.1.20/zagatech): ");
    if (!url.empty()) {
        std::string code = promptLine("Enrollment code: ");
        enrollWith(code, url);
    }

    std::printf("\nSetup complete. The lock is installed and dormant.\n");
    std::printf("Open the Zaga app any time from the Start menu to see device status.\n");

    if (GetFileAttributesW(installedApp().c_str()) != INVALID_FILE_ATTRIBUTES) {
        std::string open = promptLine("Open the Zaga app now? [Y/n]: ");
        if (open.empty() || open[0] == 'y' || open[0] == 'Y') {
            ShellExecuteW(nullptr, L"open", installedApp().c_str(), nullptr, nullptr,
                          SW_SHOWNORMAL);
        }
    }
    pauseForUser();
    return 0;
}

void usage() {
    std::printf(
        "Zaga Device Lock installer\n\n"
        "  (double-click)                  guided setup with a UAC prompt\n"
        "  install                         copy and register the provider (dormant)\n"
        "  uninstall [--code <c>]          unregister and remove\n"
        "  register | unregister           provider reg only (used by the setup package)\n"
        "  enroll --code <c> --url <u>     provision from the portal over the network\n"
        "  provision --account --secret    write the device store from a bundle offline\n"
        "  heartbeat                       check in with the portal\n"
        "  fetch-token                     pull an unlock token from the portal and apply it\n"
        "  apply-code --code <c>           apply a typed unlock code through the verifier\n"
        "  schedule | unschedule           add or remove the hourly check-in task\n"
        "  set-url --url <u>               set the portal base url\n"
        "  enable | disable                turn the lock on or off\n"
        "  protect --code <c>              require a code to uninstall\n"
        "  unprotect --code <c>            remove uninstall protection\n"
        "  status                          show the current state\n"
        "  app                             open the desktop management app\n");
}

}

int wmain(int argc, wchar_t** argv) {
    // Double-click: run the guided setup, elevating through a UAC prompt first.
    if (argc < 2) {
        if (!isElevated()) {
            relaunchElevated(L"setup");
            return 0;
        }
        return runSetup();
    }

    std::wstring command = argv[1];

    if (command == L"setup") {
        if (!isElevated()) {
            relaunchElevated(L"setup");
            return 0;
        }
        return runSetup();
    }

    if (command == L"uninstall-ui") {
        if (!isElevated()) {
            relaunchElevated(L"uninstall-ui");
            return 0;
        }
        int result = doUninstall(argc, argv);
        pauseForUser();
        return result;
    }

    if (command == L"help" || command == L"--help" || command == L"-h" || command == L"/?") {
        usage();
        return 0;
    }

    bool writesMachineState =
        command == L"install" || command == L"uninstall" || command == L"provision" ||
        command == L"register" || command == L"unregister" ||
        command == L"enroll" || command == L"fetch-token" || command == L"apply-code" ||
        command == L"enable" ||
        command == L"disable" || command == L"protect" || command == L"unprotect" ||
        command == L"schedule" || command == L"unschedule" || command == L"set-url";
    if (writesMachineState && !requireAdmin()) {
        return 1;
    }

    if (command == L"install") {
        return doInstall();
    }
    if (command == L"register") {
        return doRegister();
    }
    if (command == L"unregister") {
        return doUnregister();
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
    if (command == L"apply-code") {
        return doApplyCode(argc, argv);
    }
    if (command == L"schedule") {
        std::printf(registerHeartbeatTask(installedInstaller())
                        ? "Hourly check-in scheduled.\n"
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
    if (command == L"app") {
        std::wstring app = installedApp();
        if (GetFileAttributesW(app.c_str()) == INVALID_FILE_ATTRIBUTES) {
            app = exeDirectory() + L"\\" + APP_NAME; // dev: run from the build folder
        }
        ShellExecuteW(nullptr, L"open", app.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        return 0;
    }

    usage();
    return 1;
}
