#include <windows.h>
#include <shlobj.h>

#include <cstdio>
#include <string>

#include "DeviceConfig.h"
#include "LocalStore.h"
#include "LockGate.h"

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")

using namespace zaga;

namespace {

const wchar_t DLL_NAME[] = L"zaga_lock_provider.dll";

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

std::string argValue(int argc, wchar_t** argv, const wchar_t* name) {
    for (int i = 2; i + 1 < argc; ++i) {
        if (_wcsicmp(argv[i], name) == 0) {
            std::wstring value = argv[i + 1];
            return std::string(value.begin(), value.end());
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

    DeviceConfig::setLockEnabled(false);

    std::printf("Installed. The lock is OFF (dormant) and removal is not protected.\n");
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

    callDllEntry(installedDll(), "DllUnregisterServer");
    DeleteFileW(installedDll().c_str());
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
        "  provision --account --secret    write the device store from a bundle\n"
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
