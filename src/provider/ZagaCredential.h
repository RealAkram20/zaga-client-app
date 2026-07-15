#pragma once

#include <windows.h>
#include <credentialprovider.h>

#include <string>

#include "Fields.h"
#include "LockGate.h"

namespace zaga {

class ZagaProvider;

class ZagaCredential : public ICredentialProviderCredential {
public:
    ZagaCredential();

    void Initialize(CREDENTIAL_PROVIDER_USAGE_SCENARIO cpus, ZagaProvider* provider);

    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
    IFACEMETHODIMP_(ULONG) AddRef() override;
    IFACEMETHODIMP_(ULONG) Release() override;

    IFACEMETHODIMP Advise(ICredentialProviderCredentialEvents* events) override;
    IFACEMETHODIMP UnAdvise() override;

    IFACEMETHODIMP SetSelected(BOOL* autoLogon) override;
    IFACEMETHODIMP SetDeselected() override;

    IFACEMETHODIMP GetFieldState(
        DWORD field,
        CREDENTIAL_PROVIDER_FIELD_STATE* state,
        CREDENTIAL_PROVIDER_FIELD_INTERACTIVE_STATE* interactiveState) override;

    IFACEMETHODIMP GetStringValue(DWORD field, LPWSTR* value) override;
    IFACEMETHODIMP GetBitmapValue(DWORD field, HBITMAP* bitmap) override;
    IFACEMETHODIMP GetCheckboxValue(DWORD field, BOOL* checked, LPWSTR* label) override;
    IFACEMETHODIMP GetSubmitButtonValue(DWORD field, DWORD* adjacentTo) override;
    IFACEMETHODIMP GetComboBoxValueCount(DWORD field, DWORD* count, DWORD* selected) override;
    IFACEMETHODIMP GetComboBoxValueAt(DWORD field, DWORD item, LPWSTR* value) override;

    IFACEMETHODIMP SetStringValue(DWORD field, LPCWSTR value) override;
    IFACEMETHODIMP SetCheckboxValue(DWORD field, BOOL checked) override;
    IFACEMETHODIMP SetComboBoxSelectedValue(DWORD field, DWORD item) override;
    IFACEMETHODIMP CommandLinkClicked(DWORD field) override;

    IFACEMETHODIMP GetSerialization(
        CREDENTIAL_PROVIDER_GET_SERIALIZATION_RESPONSE* response,
        CREDENTIAL_PROVIDER_CREDENTIAL_SERIALIZATION* serialization,
        LPWSTR* optionalStatusText,
        CREDENTIAL_PROVIDER_STATUS_ICON* optionalStatusIcon) override;

    IFACEMETHODIMP ReportResult(
        NTSTATUS status,
        NTSTATUS substatus,
        LPWSTR* optionalStatusText,
        CREDENTIAL_PROVIDER_STATUS_ICON* optionalStatusIcon) override;

private:
    ~ZagaCredential();

    void setMessage(const std::wstring& message);

    LONG _cRef;
    CREDENTIAL_PROVIDER_USAGE_SCENARIO _cpus;
    ZagaProvider* _provider;
    ICredentialProviderCredentialEvents* _events;

    GateInfo _info;
    std::wstring _account;
    std::wstring _status;
    std::wstring _meta;
    std::wstring _code;
    std::wstring _message;
};

}
