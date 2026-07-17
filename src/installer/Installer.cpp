#include <windows.h>
#include <shlobj.h>
#include <shellapi.h>
#include <objbase.h>
#include <wtsapi32.h>

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
    // Every 15 minutes rather than hourly: this task is not only a check-in, it is what
    // enforces the lock on an already-open session. An hour of free use after going
    // overdue is an hour too many, and the run is cheap when nothing has changed.
    std::wstring command = systemExe(L"schtasks.exe") +
        L" /Create /F /RU SYSTEM /SC MINUTE /MO 15" +
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

// A switch that carries no value of its own, e.g. --force.
bool hasFlag(int argc, wchar_t** argv, const wchar_t* name) {
    for (int i = 2; i < argc; ++i) {
        if (_wcsicmp(argv[i], name) == 0) {
            return true;
        }
    }
    return false;
}

// Removes the device store, which lives in ProgramData rather than the install
// folder and so survives deleting program files. Leaving it behind means a reinstall
// silently adopts the old account number, secret, token and counter — the machine
// comes back answering to a portal record that may no longer exist, instead of
// registering as the new device it now is.
//
// Safe to do here because every route into an uninstall is gated on the uninstall
// code: nothing reaches this without authorization.
void removeDeviceStore() {
    std::wstring store = LocalStore::defaultPath();

    if (!DeleteFileW(store.c_str()) &&
        GetFileAttributesW(store.c_str()) != INVALID_FILE_ATTRIBUTES) {
        MoveFileExW(store.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);
    }

    // Take the folder too, but only if it is empty — RemoveDirectory fails harmlessly
    // when anything else lives there.
    size_t slash = store.find_last_of(L"\\/");
    if (slash != std::wstring::npos) {
        RemoveDirectoryW(store.substr(0, slash).c_str());
    }
}

// Mints this device's account number, or adopts the one it already answers to. A
// machine that is already enrolled has a number its portal record was built around,
// and the customer pays against that; minting a fresh one on upgrade would put a
// number on the lock screen that the portal has never heard of.
std::string adoptOrMintAccountNumber() {
    if (!DeviceConfig::accountNumber().empty()) {
        return DeviceConfig::accountNumber();
    }

    StoredDevice device;
    if (LocalStore::load(LocalStore::defaultPath(), device) && !device.accountNumber.empty()) {
        DeviceConfig::setAccountNumber(device.accountNumber);
        return device.accountNumber;
    }

    return DeviceConfig::ensureAccountNumber();
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

    // Minted here, before the device is known to any portal, so the technician can
    // read it out and record it against the device when registering. Storing it is
    // what arms protection, so removal needs the code from this point on.
    std::string uninstallCode = DeviceConfig::ensureUninstallCode();
    std::string accountNumber = adoptOrMintAccountNumber();

    std::printf("Installed. The lock is OFF (dormant) until the device enrolls.\n");
    std::printf("Account number:      %s\n", accountNumber.c_str());
    if (uninstallCode.empty()) {
        std::printf("WARNING: no uninstall code could be stored, so removal is NOT protected.\n");
    } else {
        std::printf("Uninstall code:      %s\n", uninstallCode.c_str());
        std::printf("Register the device on the portal with these. Keep the uninstall code:\n");
        std::printf("it is required to remove the software.\n");
    }
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

    // The packaged wizard runs this verb rather than "install", so the code has to
    // be minted here too or a device set up from Zaga-Setup.exe would be removable
    // by anyone. The technician reads it out of the app to register the device.
    std::string uninstallCode = DeviceConfig::ensureUninstallCode();
    std::string accountNumber = adoptOrMintAccountNumber();

    std::printf("Registered. The lock is dormant until enrollment; hourly check-in %s.\n",
                scheduled ? "scheduled" : "could not be scheduled");
    std::printf("Account number:      %s\n", accountNumber.c_str());
    if (uninstallCode.empty()) {
        std::printf("WARNING: no uninstall code could be stored, so removal is NOT protected.\n");
    } else {
        std::printf("Uninstall code:      %s\n", uninstallCode.c_str());
    }
    return 0;
}

// Undo doRegister for a packaged uninstall. The package removes the files.
//
// This honors removal protection exactly as the console "uninstall" verb does: the
// packaged wizard is the route a customer would actually take, so leaving it
// ungated would make the lock removable from Add/Remove Programs.
int doUnregister(int argc, wchar_t** argv) {
    if (DeviceConfig::uninstallProtected()) {
        std::string code = argValue(argc, argv, L"--code");
        if (code.empty() || !DeviceConfig::checkUninstallCode(code)) {
            std::printf("Removal is protected. Pass the correct --code to unregister.\n");
            return 1;
        }
    }

    removeHeartbeatTask();
    callDllEntry(exeDirectory() + L"\\" + DLL_NAME, "DllUnregisterServer");

    // The package owns its own Add/Remove entry, but a machine that was ever set up
    // with the console "install" verb also has ours, pointing at zaga_installer.exe.
    // Removing the program files without this leaves that entry behind aimed at a
    // deleted exe, so Windows reports it cannot find the file when someone clicks
    // Uninstall. Our key is distinct from the package's ({AppId}_is1), so clearing it
    // here cannot disturb the wizard's own entry.
    unregisterAddRemove();

    removeDeviceStore();
    DeviceConfig::removeAll();
    std::printf("Unregistered. The provider is removed and all device data is cleared.\n");
    std::printf("A reinstall will register as a new device.\n");
    return 0;
}

// Exit 0 when removal protection is on, so the setup wizard can tell whether it
// needs to ask for a code before uninstalling.
int doIsProtected() {
    bool protectedNow = DeviceConfig::uninstallProtected();
    std::printf("Removal protected: %s\n", protectedNow ? "yes" : "no");
    return protectedNow ? 0 : 1;
}

// Exit 0 when the supplied code would authorise a removal. Reads no machine state
// beyond the stored code, so the wizard can call it before elevating anything.
int doCheckCode(int argc, wchar_t** argv) {
    if (!DeviceConfig::uninstallProtected()) {
        return 0;
    }
    return DeviceConfig::checkUninstallCode(argValue(argc, argv, L"--code")) ? 0 : 1;
}

// `interactive` is set only for the double-clicked/Add-Remove route, which has a
// console to ask in. It never weakens the check — an unprotected device is still
// unprotected, and a wrong code still refuses — it only offers somewhere to type the
// code rather than dead-ending on "pass --code".
int doUninstall(int argc, wchar_t** argv, bool interactive = false) {
    if (DeviceConfig::uninstallProtected()) {
        std::string code = argValue(argc, argv, L"--code");
        if (code.empty() && interactive) {
            std::printf("Removal of this device is protected.\n"
                        "Contact support for the uninstall code. It is released once the\n"
                        "payment plan is complete.\n\n");
            code = promptLine("Uninstall code: ");
        }
        if (code.empty() || !DeviceConfig::checkUninstallCode(code)) {
            std::printf("That code is not correct for this device. Contact support to be\n"
                        "issued the uninstall code.\n");
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
    removeDeviceStore();
    DeviceConfig::removeAll();

    std::printf("Uninstalled. The provider is unregistered and all device data is cleared.\n");
    std::printf("A reinstall will register as a new device.\n");
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
        // Left where the app can read it: the portal's reason is the only thing that
        // tells anyone what to actually do, and it cannot travel back through the UAC
        // launch any other way.
        DeviceConfig::setLastError(result.message);
        std::printf("Enrollment failed: %s\n", result.message.c_str());
        return 1;
    }
    DeviceConfig::clearLastError();

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

    // An enrolled device must not be removable without authorization. Normally the
    // code was minted at install; this covers machines installed before codes were
    // generated, and any install where the write did not land.
    std::string uninstallCode = DeviceConfig::ensureUninstallCode();

    // The portal is the billing authority once a record exists: the customer pays
    // against the number it holds. Normally that is the number this device minted and
    // an operator registered, and this is a no-op; when they differ — a typo at
    // registration, or a device enrolled against an older record — the machine follows
    // the portal rather than showing a number nobody can pay against.
    if (!result.accountNumber.empty() &&
        result.accountNumber != DeviceConfig::accountNumber()) {
        DeviceConfig::setAccountNumber(result.accountNumber);
    }

    // Enrolling arms the lock and leaves the device locked: it has no deadline yet, and
    // the gate reads "no deadline" as overdue. That is deliberate — the device is
    // financed but nothing has been paid into it, so it stays shut until the first
    // unlock token is entered, and the term is counted from that moment rather than
    // from an enrollment date nobody was watching.
    //
    // This is only safe because the portal refuses to enroll a device that is not on a
    // plan, so a token always exists for the operator to type in.
    DeviceConfig::setLockEnabled(true);

    std::printf("Enrolled %s from the portal. The device store is written.\n",
                result.accountNumber.c_str());
    std::printf("Lock enforcement:    ARMED. This device is LOCKED until an unlock\n");
    std::printf("                     token is entered; the term starts from then.\n");
    if (uninstallCode.empty()) {
        std::printf("WARNING: no uninstall code could be stored, so removal is NOT protected.\n");
    } else {
        std::printf("Uninstall code:      %s\n", uninstallCode.c_str());
    }
    return 0;
}

int doEnroll(int argc, wchar_t** argv) {
    return enrollWith(argValue(argc, argv, L"--code"), resolvePortalUrl(argc, argv));
}

// Drops whoever is signed in back to the logon screen, where the credential provider
// gates them.
//
// This exists because the provider only ever guards the *door*: it is consulted when
// someone tries to log in, and has no say over a session that is already open. Without
// this, a customer who stops paying and simply never signs out is never locked out at
// all — the device would go overdue and they would keep using it indefinitely.
//
// Disconnecting rather than logging off deliberately: the session and the customer's
// unsaved work stay intact behind the logon screen, so an overdue device costs them
// access, not their data.
bool disconnectActiveSession() {
    DWORD session = WTSGetActiveConsoleSessionId();
    if (session == 0xFFFFFFFF) {
        return false;   // no console session attached; nobody to disconnect
    }

    // Session 0 is the isolated services session — nobody is signed in there.
    if (session == 0) {
        return false;
    }

    return WTSDisconnectSession(WTS_CURRENT_SERVER_HANDLE, session, FALSE) != FALSE;
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

    // Enforce the lock on a session that is already open. Judged from the local store,
    // never from the check-in above, so a device that has gone overdue still locks with
    // the network unplugged — pulling the cable must not be a way to keep using it.
    if (info.locked) {
        std::printf("Device is locked (%s). Returning to the logon screen.\n",
                    info.statusText.c_str());
        if (disconnectActiveSession()) {
            std::printf("Signed-in session disconnected.\n");
        }
    }

    // Deliberately does not pull or apply a token. A newly enrolled device is locked
    // and stays locked until someone enters the first unlock token, which is what
    // starts its term. Applying one here would open the device on its own, before
    // anyone had chosen to start the clock.
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

    if (applied != VerifyResult::Accepted) {
        return 1;
    }

    // Enrolling arms the device, but only if the portal already had a token to give —
    // which it does not until the device is put on a plan. When the plan comes second,
    // this is the moment the device first has a deadline, so it is the moment to arm.
    // Nothing to do if an operator has deliberately disabled enforcement and the
    // device is already armed.
    if (!DeviceConfig::lockEnabled()) {
        DeviceConfig::setLockEnabled(true);
        GateInfo info = LockGate::describe();
        std::printf("Lock enforcement:    ARMED (gates login after %s).\n",
                    info.deadlineText.empty() ? "the due date" : info.deadlineText.c_str());
    }

    return 0;
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
    std::string uninstallCode = DeviceConfig::uninstallCode();
    if (!uninstallCode.empty()) {
        std::printf("Uninstall code:      %s\n", uninstallCode.c_str());
    }
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
        "  register                        provider reg only (used by the setup package)\n"
        "  unregister [--code <c>]         undo register (used by the setup package)\n"
        "  is-protected                    exit 0 when removal protection is on\n"
        "  check-code --code <c>           exit 0 when the code would authorise removal\n"
        "  enroll --code <c> --url <u>     provision from the portal over the network\n"
        "  provision --account --secret    write the device store from a bundle offline\n"
        "  heartbeat                       check in with the portal\n"
        "  fetch-token                     pull an unlock token from the portal and apply it\n"
        "  apply-code --code <c>           apply a typed unlock code through the verifier\n"
        "  schedule | unschedule           add or remove the hourly check-in task\n"
        "  set-url --url <u>               set the portal base url\n"
        "  enable [--force] | disable      turn the lock on or off\n"
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
        int result = doUninstall(argc, argv, true);
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
        return doUnregister(argc, argv);
    }
    if (command == L"uninstall") {
        return doUninstall(argc, argv);
    }
    if (command == L"is-protected") {
        return doIsProtected();
    }
    if (command == L"check-code") {
        return doCheckCode(argc, argv);
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
        // An enrolled device with no deadline is meant to be locked — that is the state
        // a device sits in between enrolling and its first unlock token, and arming it
        // is the point. What must not happen is arming a device with no store: it has
        // no secret, so no token can ever verify, and the machine would be shut for
        // good with no code in existence that opens it.
        StoredDevice enrolled;
        bool provisioned = LocalStore::load(LocalStore::defaultPath(), enrolled);
        if (!provisioned && !hasFlag(argc, argv, L"--force")) {
            std::printf("This device is not enrolled, so it holds no key to verify an\n"
                        "unlock token with. Enabling the lock would shut it permanently.\n"
                        "Enroll it first, or pass --force if you really mean to.\n");
            return 1;
        }
        DeviceConfig::setLockEnabled(true);
        std::printf("The lock is enabled.\n");
        if (provisioned && enrolled.state.lockDeadlineDay == 0) {
            std::printf("This device has no unlock token yet, so it is LOCKED now. Enter a\n"
                        "token at the login screen to start its term.\n");
        }
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
