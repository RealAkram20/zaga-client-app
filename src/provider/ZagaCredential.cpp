#include "ZagaCredential.h"

#include <shlwapi.h>

#include "ZagaProvider.h"

#pragma comment(lib, "shlwapi.lib")

namespace zaga {

namespace {

std::wstring widen(const std::string& text) {
    if (text.empty()) {
        return std::wstring();
    }

    int length = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
    std::wstring wide(length > 0 ? length - 1 : 0, L'\0');
    if (length > 0) {
        MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, &wide[0], length);
    }
    return wide;
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

HBITMAP makeTileBitmap() {
    const int size = 128;
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = size;
    info.bmiHeader.biHeight = -size;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HBITMAP bitmap = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (bitmap == nullptr || bits == nullptr) {
        return nullptr;
    }

    // Fill with the brand slate so the tile has a solid identity until a proper
    // logo asset is bundled as a resource.
    DWORD* pixels = static_cast<DWORD*>(bits);
    for (int i = 0; i < size * size; ++i) {
        pixels[i] = 0xFF16233A;
    }

    return bitmap;
}

}

ZagaCredential::ZagaCredential()
    : _cRef(1),
      _cpus(CPUS_INVALID),
      _provider(nullptr),
      _events(nullptr) {
}

ZagaCredential::~ZagaCredential() {
    if (_events != nullptr) {
        _events->Release();
        _events = nullptr;
    }
}

void ZagaCredential::Initialize(CREDENTIAL_PROVIDER_USAGE_SCENARIO cpus, ZagaProvider* provider) {
    _cpus = cpus;
    _provider = provider;
    _info = LockGate::describe();

    _account = _info.provisioned ? widen(_info.accountNumber) : L"Not provisioned";

    std::wstring status = widen(_info.statusText);
    if (!_info.deadlineText.empty()) {
        status += _info.locked ? L" \x2022 was due " : L" \x2022 until ";
        status += widen(_info.deadlineText);
    }
    _status = status;

    std::wstring meta;
    if (!_info.model.empty()) {
        meta = widen(_info.model);
    }
    if (!_info.serial.empty()) {
        if (!meta.empty()) {
            meta += L" \x2022 ";
        }
        meta += widen(_info.serial);
    }
    _meta = meta;
    _message = L"Enter your 20-character unlock code.";
}

IFACEMETHODIMP ZagaCredential::QueryInterface(REFIID riid, void** ppv) {
    if (ppv == nullptr) {
        return E_POINTER;
    }

    if (riid == IID_IUnknown || riid == IID_ICredentialProviderCredential) {
        *ppv = static_cast<ICredentialProviderCredential*>(this);
        AddRef();
        return S_OK;
    }

    *ppv = nullptr;
    return E_NOINTERFACE;
}

IFACEMETHODIMP_(ULONG) ZagaCredential::AddRef() {
    return InterlockedIncrement(&_cRef);
}

IFACEMETHODIMP_(ULONG) ZagaCredential::Release() {
    LONG count = InterlockedDecrement(&_cRef);
    if (count == 0) {
        delete this;
    }
    return count;
}

IFACEMETHODIMP ZagaCredential::Advise(ICredentialProviderCredentialEvents* events) {
    if (_events != nullptr) {
        _events->Release();
    }
    _events = events;
    if (_events != nullptr) {
        _events->AddRef();
    }
    return S_OK;
}

IFACEMETHODIMP ZagaCredential::UnAdvise() {
    if (_events != nullptr) {
        _events->Release();
        _events = nullptr;
    }
    return S_OK;
}

IFACEMETHODIMP ZagaCredential::SetSelected(BOOL* autoLogon) {
    *autoLogon = FALSE;
    return S_OK;
}

IFACEMETHODIMP ZagaCredential::SetDeselected() {
    _code.clear();
    if (_events != nullptr) {
        _events->SetFieldString(this, FIELD_CODE, L"");
    }
    return S_OK;
}

IFACEMETHODIMP ZagaCredential::GetFieldState(
    DWORD field,
    CREDENTIAL_PROVIDER_FIELD_STATE* state,
    CREDENTIAL_PROVIDER_FIELD_INTERACTIVE_STATE* interactiveState) {
    if (field >= FIELD_COUNT || state == nullptr || interactiveState == nullptr) {
        return E_INVALIDARG;
    }

    *state = c_fieldStatePairs[field].state;
    *interactiveState = c_fieldStatePairs[field].interactiveState;
    return S_OK;
}

IFACEMETHODIMP ZagaCredential::GetStringValue(DWORD field, LPWSTR* value) {
    if (value == nullptr) {
        return E_POINTER;
    }

    const wchar_t* source = nullptr;
    switch (field) {
        case FIELD_ACCOUNT: source = _account.c_str(); break;
        case FIELD_STATUS: source = _status.c_str(); break;
        case FIELD_META: source = _meta.c_str(); break;
        case FIELD_CODE: source = _code.c_str(); break;
        case FIELD_MESSAGE: source = _message.c_str(); break;
        default: return E_INVALIDARG;
    }

    return SHStrDupW(source, value);
}

IFACEMETHODIMP ZagaCredential::GetBitmapValue(DWORD field, HBITMAP* bitmap) {
    if (bitmap == nullptr) {
        return E_POINTER;
    }
    if (field != FIELD_TILE_IMAGE) {
        return E_INVALIDARG;
    }

    HBITMAP tile = makeTileBitmap();
    if (tile == nullptr) {
        return E_FAIL;
    }

    *bitmap = tile;
    return S_OK;
}

IFACEMETHODIMP ZagaCredential::GetCheckboxValue(DWORD, BOOL*, LPWSTR*) {
    return E_NOTIMPL;
}

IFACEMETHODIMP ZagaCredential::GetSubmitButtonValue(DWORD field, DWORD* adjacentTo) {
    if (adjacentTo == nullptr) {
        return E_POINTER;
    }
    if (field != FIELD_SUBMIT) {
        return E_INVALIDARG;
    }

    *adjacentTo = FIELD_CODE;
    return S_OK;
}

IFACEMETHODIMP ZagaCredential::GetComboBoxValueCount(DWORD, DWORD*, DWORD*) {
    return E_NOTIMPL;
}

IFACEMETHODIMP ZagaCredential::GetComboBoxValueAt(DWORD, DWORD, LPWSTR*) {
    return E_NOTIMPL;
}

IFACEMETHODIMP ZagaCredential::SetStringValue(DWORD field, LPCWSTR value) {
    if (field != FIELD_CODE) {
        return E_INVALIDARG;
    }

    _code = value != nullptr ? value : L"";
    return S_OK;
}

IFACEMETHODIMP ZagaCredential::SetCheckboxValue(DWORD, BOOL) {
    return E_NOTIMPL;
}

IFACEMETHODIMP ZagaCredential::SetComboBoxSelectedValue(DWORD, DWORD) {
    return E_NOTIMPL;
}

IFACEMETHODIMP ZagaCredential::CommandLinkClicked(DWORD field) {
    if (field == FIELD_HELP_LINK) {
        setMessage(L"On another device, open your unlock portal and enter this "
                   L"account number to pay and receive a code.");
        return S_OK;
    }

    if (field == FIELD_TECH_LINK) {
        setMessage(L"Technician removal requires the uninstall authorization code "
                   L"recorded in the portal.");
        return S_OK;
    }

    return E_INVALIDARG;
}

IFACEMETHODIMP ZagaCredential::GetSerialization(
    CREDENTIAL_PROVIDER_GET_SERIALIZATION_RESPONSE* response,
    CREDENTIAL_PROVIDER_CREDENTIAL_SERIALIZATION* serialization,
    LPWSTR* optionalStatusText,
    CREDENTIAL_PROVIDER_STATUS_ICON* optionalStatusIcon) {
    UNREFERENCED_PARAMETER(serialization);

    std::string message;
    VerifyResult result = LockGate::applyCode(narrow(_code), message);
    setMessage(widen(message));

    // The provider never completes a Windows logon itself. It only lifts the gate;
    // once unlocked, a re-enumeration lets the normal logon providers through.
    *response = CPGSR_NO_CREDENTIAL_NOT_FINISHED;
    SHStrDupW(widen(message).c_str(), optionalStatusText);

    if (result == VerifyResult::Accepted) {
        *optionalStatusIcon = CPSI_SUCCESS;
        if (_provider != nullptr) {
            _provider->SignalReenumerate();
        }
    } else {
        *optionalStatusIcon = CPSI_ERROR;
    }

    return S_OK;
}

IFACEMETHODIMP ZagaCredential::ReportResult(
    NTSTATUS,
    NTSTATUS,
    LPWSTR* optionalStatusText,
    CREDENTIAL_PROVIDER_STATUS_ICON* optionalStatusIcon) {
    if (optionalStatusText != nullptr) {
        *optionalStatusText = nullptr;
    }
    if (optionalStatusIcon != nullptr) {
        *optionalStatusIcon = CPSI_NONE;
    }
    return S_OK;
}

void ZagaCredential::setMessage(const std::wstring& message) {
    _message = message;
    if (_events != nullptr) {
        _events->SetFieldString(this, FIELD_MESSAGE, _message.c_str());
    }
}

}
