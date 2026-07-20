#include "ZagaCredential.h"

#include <shlwapi.h>

#include "DeviceConfig.h"
#include "Module.h"
#include "Resource.h"
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

    _account = _info.provisioned ? L"Account Number: " + widen(_info.accountNumber)
                                 : L"Not enrolled";

    std::wstring status = widen(_info.statusText);
    if (!_info.deadlineText.empty()) {
        status += _info.locked ? L" \x2022 was due " : L" \x2022 until ";
        status += widen(_info.deadlineText);
    }
    _status = status;

    // The three lines a stranded customer needs, in the owner's words: whose account,
    // that payment is due and where to pay, and who to call. Prefer the operator's
    // configured wording; otherwise say it plainly with the portal the device knows.
    std::string instructions = DeviceConfig::unlockInstructions();
    if (!instructions.empty()) {
        _purchase = widen(instructions);
    } else {
        std::string portal = DeviceConfig::portalUrl();
        if (!portal.empty()) {
            _purchase = L"Your payment is due. Please pay at " + widen(portal) +
                        L" and quote your account number.";
        } else {
            _purchase = L"Your payment is due. Please contact your vendor to make "
                        L"a payment.";
        }
    }

    std::string support = DeviceConfig::supportContact();
    _support = L"Contact support on: " +
               (support.empty() ? std::wstring(L"+256 704245408") : widen(support));

    _message = codeProgressMessage();
}

// The unlock token is 20 base32 symbols, shown grouped as XXXXX-XXXXX-XXXXX-XXXXX.
// Counting the symbols the customer has actually typed — dashes and spaces ignored,
// since the framework and the verifier both discard them — lets them catch a dropped
// character before they submit.
std::wstring ZagaCredential::codeProgressMessage() const {
    int typed = 0;
    for (wchar_t c : _code) {
        if (c != L'-' && c != L' ' && c != L'\t') {
            ++typed;
        }
    }

    if (typed == 0) {
        return L"Enter your 20-character unlock code.";
    }
    if (typed < 20) {
        return std::to_wstring(typed) + L" of 20 characters entered.";
    }
    if (typed == 20) {
        return L"20 of 20 — press Unlock.";
    }
    return std::to_wstring(typed) + L" characters — that is longer than a 20-character code.";
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
    // Back to the standing prompt, so a re-selected tile never shows a stale count.
    setMessage(codeProgressMessage());
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
        case FIELD_PURCHASE: source = _purchase.c_str(); break;
        case FIELD_SUPPORT: source = _support.c_str(); break;
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

    // Load the bundled logo. Without LR_SHARED, LoadImage returns a handle the caller
    // owns; LogonUI takes ownership of the returned HBITMAP and frees it, so each call
    // hands back its own.
    HBITMAP tile = static_cast<HBITMAP>(LoadImageW(
        dllInstance(), MAKEINTRESOURCEW(IDB_TILE), IMAGE_BITMAP, 0, 0,
        LR_DEFAULTCOLOR));

    // Never leave the tile blank: if the resource is somehow missing, fall back to the
    // solid brand square rather than failing the whole tile.
    if (tile == nullptr) {
        tile = makeTileBitmap();
    }
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

    // The framework calls this on every keystroke, so the count updates live and the
    // customer can see a dropped character before submitting.
    setMessage(codeProgressMessage());
    return S_OK;
}

IFACEMETHODIMP ZagaCredential::SetCheckboxValue(DWORD, BOOL) {
    return E_NOTIMPL;
}

IFACEMETHODIMP ZagaCredential::SetComboBoxSelectedValue(DWORD, DWORD) {
    return E_NOTIMPL;
}

IFACEMETHODIMP ZagaCredential::CommandLinkClicked(DWORD) {
    // The tile carries no command links: the purchase and support lines say
    // everything a customer can act on, and fewer words read better on a lock screen.
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

    SHStrDupW(widen(message).c_str(), optionalStatusText);

    // The provider never completes a Windows logon itself. It only lifts the gate.
    // On acceptance, FINISHED tells LogonUI this tile's work is over, and the
    // re-enumeration then returns zero Zaga tiles — so the lock screen gives way to
    // the normal sign-in on its own, with nothing further to click. On failure,
    // NOT_FINISHED keeps the tile up so the customer can retype.
    if (result == VerifyResult::Accepted) {
        *response = CPGSR_NO_CREDENTIAL_FINISHED;
        *optionalStatusIcon = CPSI_SUCCESS;
        if (_provider != nullptr) {
            _provider->SignalReenumerate();
        }
    } else {
        *response = CPGSR_NO_CREDENTIAL_NOT_FINISHED;
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
