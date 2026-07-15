#include "ZagaProvider.h"

#include <new>
#include <shlwapi.h>

#include "Fields.h"
#include "Guid.h"
#include "LockGate.h"
#include "ZagaCredential.h"

#pragma comment(lib, "shlwapi.lib")

namespace zaga {

namespace {

HRESULT copyFieldDescriptor(
    const CREDENTIAL_PROVIDER_FIELD_DESCRIPTOR& source,
    CREDENTIAL_PROVIDER_FIELD_DESCRIPTOR** out) {
    auto* copy = static_cast<CREDENTIAL_PROVIDER_FIELD_DESCRIPTOR*>(
        CoTaskMemAlloc(sizeof(CREDENTIAL_PROVIDER_FIELD_DESCRIPTOR)));
    if (copy == nullptr) {
        return E_OUTOFMEMORY;
    }

    copy->dwFieldID = source.dwFieldID;
    copy->cpft = source.cpft;
    copy->guidFieldType = source.guidFieldType;
    copy->pszLabel = nullptr;

    HRESULT hr = SHStrDupW(source.pszLabel != nullptr ? source.pszLabel : L"", &copy->pszLabel);
    if (FAILED(hr)) {
        CoTaskMemFree(copy);
        return hr;
    }

    *out = copy;
    return S_OK;
}

bool gatesScenario(CREDENTIAL_PROVIDER_USAGE_SCENARIO cpus) {
    return cpus == CPUS_LOGON || cpus == CPUS_UNLOCK_WORKSTATION;
}

}

ZagaProvider::ZagaProvider()
    : _cRef(1),
      _cpus(CPUS_INVALID),
      _events(nullptr),
      _adviseContext(0),
      _credential(nullptr),
      _locked(false) {
}

ZagaProvider::~ZagaProvider() {
    releaseCredential();
    if (_events != nullptr) {
        _events->Release();
        _events = nullptr;
    }
}

void ZagaProvider::releaseCredential() {
    if (_credential != nullptr) {
        _credential->Release();
        _credential = nullptr;
    }
}

IFACEMETHODIMP ZagaProvider::QueryInterface(REFIID riid, void** ppv) {
    if (ppv == nullptr) {
        return E_POINTER;
    }

    if (riid == IID_IUnknown || riid == IID_ICredentialProvider) {
        *ppv = static_cast<ICredentialProvider*>(this);
        AddRef();
        return S_OK;
    }

    if (riid == IID_ICredentialProviderFilter) {
        *ppv = static_cast<ICredentialProviderFilter*>(this);
        AddRef();
        return S_OK;
    }

    *ppv = nullptr;
    return E_NOINTERFACE;
}

IFACEMETHODIMP_(ULONG) ZagaProvider::AddRef() {
    return InterlockedIncrement(&_cRef);
}

IFACEMETHODIMP_(ULONG) ZagaProvider::Release() {
    LONG count = InterlockedDecrement(&_cRef);
    if (count == 0) {
        delete this;
    }
    return count;
}

IFACEMETHODIMP ZagaProvider::SetUsageScenario(CREDENTIAL_PROVIDER_USAGE_SCENARIO cpus, DWORD) {
    if (!gatesScenario(cpus)) {
        return E_NOTIMPL;
    }

    _cpus = cpus;
    _locked = LockGate::describe().locked;

    releaseCredential();
    if (_locked) {
        _credential = new (std::nothrow) ZagaCredential();
        if (_credential == nullptr) {
            return E_OUTOFMEMORY;
        }
        _credential->Initialize(_cpus, this);
    }

    return S_OK;
}

IFACEMETHODIMP ZagaProvider::SetSerialization(
    const CREDENTIAL_PROVIDER_CREDENTIAL_SERIALIZATION*) {
    return E_NOTIMPL;
}

IFACEMETHODIMP ZagaProvider::Advise(ICredentialProviderEvents* events, UINT_PTR context) {
    if (_events != nullptr) {
        _events->Release();
    }
    _events = events;
    _adviseContext = context;
    if (_events != nullptr) {
        _events->AddRef();
    }
    return S_OK;
}

IFACEMETHODIMP ZagaProvider::UnAdvise() {
    if (_events != nullptr) {
        _events->Release();
        _events = nullptr;
    }
    _adviseContext = 0;
    return S_OK;
}

IFACEMETHODIMP ZagaProvider::GetFieldDescriptorCount(DWORD* count) {
    if (count == nullptr) {
        return E_POINTER;
    }
    *count = FIELD_COUNT;
    return S_OK;
}

IFACEMETHODIMP ZagaProvider::GetFieldDescriptorAt(
    DWORD index,
    CREDENTIAL_PROVIDER_FIELD_DESCRIPTOR** descriptor) {
    if (descriptor == nullptr) {
        return E_POINTER;
    }
    if (index >= FIELD_COUNT) {
        return E_INVALIDARG;
    }

    return copyFieldDescriptor(c_fieldDescriptors[index], descriptor);
}

IFACEMETHODIMP ZagaProvider::GetCredentialCount(
    DWORD* count,
    DWORD* defaultIndex,
    BOOL* autoLogonWithDefault) {
    if (count == nullptr || defaultIndex == nullptr || autoLogonWithDefault == nullptr) {
        return E_POINTER;
    }

    if (_locked && _credential != nullptr) {
        *count = 1;
        *defaultIndex = 0;
    } else {
        *count = 0;
        *defaultIndex = CREDENTIAL_PROVIDER_NO_DEFAULT;
    }

    *autoLogonWithDefault = FALSE;
    return S_OK;
}

IFACEMETHODIMP ZagaProvider::GetCredentialAt(
    DWORD index,
    ICredentialProviderCredential** credential) {
    if (credential == nullptr) {
        return E_POINTER;
    }
    if (index != 0 || _credential == nullptr) {
        return E_INVALIDARG;
    }

    return _credential->QueryInterface(IID_ICredentialProviderCredential,
                                       reinterpret_cast<void**>(credential));
}

IFACEMETHODIMP ZagaProvider::Filter(
    CREDENTIAL_PROVIDER_USAGE_SCENARIO cpus,
    DWORD,
    GUID* providers,
    BOOL* allow,
    DWORD count) {
    if (providers == nullptr || allow == nullptr) {
        return E_POINTER;
    }

    // Only gate the logon and unlock screens, and only when this device is locked.
    if (!gatesScenario(cpus) || !LockGate::describe().locked) {
        return S_OK;
    }

    for (DWORD i = 0; i < count; ++i) {
        allow[i] = IsEqualGUID(providers[i], CLSID_ZagaLockProvider) ? TRUE : FALSE;
    }

    return S_OK;
}

IFACEMETHODIMP ZagaProvider::UpdateRemoteCredential(
    const CREDENTIAL_PROVIDER_CREDENTIAL_SERIALIZATION*,
    CREDENTIAL_PROVIDER_CREDENTIAL_SERIALIZATION*) {
    return E_NOTIMPL;
}

void ZagaProvider::SignalReenumerate() {
    if (_events != nullptr) {
        _events->CredentialsChanged(_adviseContext);
    }
}

}
