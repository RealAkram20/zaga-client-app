#include <windows.h>
#include <olectl.h>
#include <new>
#include <string>

#include <initguid.h>
#include "Guid.h"
#include "Module.h"
#include "ZagaProvider.h"

using namespace zaga;

namespace {

LONG g_dllReferences = 0;
HINSTANCE g_instance = nullptr;

const wchar_t PROVIDER_NAME[] = L"Zaga Device Lock";
const wchar_t CREDENTIAL_PROVIDERS_KEY[] =
    L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Authentication\\Credential Providers";
const wchar_t CREDENTIAL_FILTERS_KEY[] =
    L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Authentication\\Credential Provider Filters";

class ClassFactory : public IClassFactory {
public:
    ClassFactory() : _cRef(1) {}

    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (ppv == nullptr) {
            return E_POINTER;
        }
        if (riid == IID_IUnknown || riid == IID_IClassFactory) {
            *ppv = static_cast<IClassFactory*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }

    IFACEMETHODIMP_(ULONG) AddRef() override {
        return InterlockedIncrement(&_cRef);
    }

    IFACEMETHODIMP_(ULONG) Release() override {
        LONG count = InterlockedDecrement(&_cRef);
        if (count == 0) {
            delete this;
        }
        return count;
    }

    IFACEMETHODIMP CreateInstance(IUnknown* outer, REFIID riid, void** ppv) override {
        if (outer != nullptr) {
            return CLASS_E_NOAGGREGATION;
        }

        auto* provider = new (std::nothrow) ZagaProvider();
        if (provider == nullptr) {
            return E_OUTOFMEMORY;
        }

        HRESULT hr = provider->QueryInterface(riid, ppv);
        provider->Release();
        return hr;
    }

    IFACEMETHODIMP LockServer(BOOL lock) override {
        if (lock) {
            InterlockedIncrement(&g_dllReferences);
        } else {
            InterlockedDecrement(&g_dllReferences);
        }
        return S_OK;
    }

private:
    ~ClassFactory() = default;
    LONG _cRef;
};

HRESULT clsidString(wchar_t* buffer, int size) {
    return StringFromGUID2(CLSID_ZagaLockProvider, buffer, size) > 0 ? S_OK : E_FAIL;
}

LONG writeDefaultValue(HKEY root, const std::wstring& subKey, const wchar_t* value) {
    HKEY key = nullptr;
    LONG result = RegCreateKeyExW(root, subKey.c_str(), 0, nullptr, 0,
                                  KEY_WRITE, nullptr, &key, nullptr);
    if (result != ERROR_SUCCESS) {
        return result;
    }

    result = RegSetValueExW(key, nullptr, 0, REG_SZ,
                            reinterpret_cast<const BYTE*>(value),
                            static_cast<DWORD>((wcslen(value) + 1) * sizeof(wchar_t)));
    RegCloseKey(key);
    return result;
}

LONG writeNamedValue(HKEY root, const std::wstring& subKey,
                     const wchar_t* name, const wchar_t* value) {
    HKEY key = nullptr;
    LONG result = RegCreateKeyExW(root, subKey.c_str(), 0, nullptr, 0,
                                  KEY_WRITE, nullptr, &key, nullptr);
    if (result != ERROR_SUCCESS) {
        return result;
    }

    result = RegSetValueExW(key, name, 0, REG_SZ,
                            reinterpret_cast<const BYTE*>(value),
                            static_cast<DWORD>((wcslen(value) + 1) * sizeof(wchar_t)));
    RegCloseKey(key);
    return result;
}

}

STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, void** ppv) {
    if (rclsid != CLSID_ZagaLockProvider) {
        return CLASS_E_CLASSNOTAVAILABLE;
    }

    auto* factory = new (std::nothrow) ClassFactory();
    if (factory == nullptr) {
        return E_OUTOFMEMORY;
    }

    HRESULT hr = factory->QueryInterface(riid, ppv);
    factory->Release();
    return hr;
}

STDAPI DllCanUnloadNow() {
    return g_dllReferences > 0 ? S_FALSE : S_OK;
}

STDAPI DllRegisterServer() {
    wchar_t clsid[64];
    if (FAILED(clsidString(clsid, 64))) {
        return SELFREG_E_CLASS;
    }

    wchar_t modulePath[MAX_PATH];
    if (GetModuleFileNameW(g_instance, modulePath, MAX_PATH) == 0) {
        return HRESULT_FROM_WIN32(GetLastError());
    }

    std::wstring clsidKey = std::wstring(L"CLSID\\") + clsid;
    std::wstring inproc = clsidKey + L"\\InprocServer32";
    std::wstring providerKey = std::wstring(CREDENTIAL_PROVIDERS_KEY) + L"\\" + clsid;
    std::wstring filterKey = std::wstring(CREDENTIAL_FILTERS_KEY) + L"\\" + clsid;

    if (writeDefaultValue(HKEY_CLASSES_ROOT, clsidKey, PROVIDER_NAME) != ERROR_SUCCESS ||
        writeDefaultValue(HKEY_CLASSES_ROOT, inproc, modulePath) != ERROR_SUCCESS ||
        writeNamedValue(HKEY_CLASSES_ROOT, inproc, L"ThreadingModel", L"Apartment") != ERROR_SUCCESS ||
        writeDefaultValue(HKEY_LOCAL_MACHINE, providerKey, PROVIDER_NAME) != ERROR_SUCCESS ||
        writeDefaultValue(HKEY_LOCAL_MACHINE, filterKey, PROVIDER_NAME) != ERROR_SUCCESS) {
        return SELFREG_E_CLASS;
    }

    return S_OK;
}

STDAPI DllUnregisterServer() {
    wchar_t clsid[64];
    if (FAILED(clsidString(clsid, 64))) {
        return SELFREG_E_CLASS;
    }

    std::wstring clsidKey = std::wstring(L"CLSID\\") + clsid;
    std::wstring providerKey = std::wstring(CREDENTIAL_PROVIDERS_KEY) + L"\\" + clsid;
    std::wstring filterKey = std::wstring(CREDENTIAL_FILTERS_KEY) + L"\\" + clsid;

    RegDeleteTreeW(HKEY_CLASSES_ROOT, clsidKey.c_str());
    RegDeleteTreeW(HKEY_LOCAL_MACHINE, providerKey.c_str());
    RegDeleteTreeW(HKEY_LOCAL_MACHINE, filterKey.c_str());
    return S_OK;
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_instance = static_cast<HINSTANCE>(module);
        DisableThreadLibraryCalls(module);
    }
    return TRUE;
}

namespace zaga {

HINSTANCE dllInstance() {
    return g_instance;
}

}
