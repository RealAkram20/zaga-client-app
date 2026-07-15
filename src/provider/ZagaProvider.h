#pragma once

#include <windows.h>
#include <credentialprovider.h>

namespace zaga {

class ZagaCredential;

// One object serves as both the credential provider and the provider filter. When
// the device is locked it contributes the unlock tile and hides every other
// logon provider; when the device is active it contributes nothing and lets the
// normal providers through.
class ZagaProvider : public ICredentialProvider, public ICredentialProviderFilter {
public:
    ZagaProvider();

    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
    IFACEMETHODIMP_(ULONG) AddRef() override;
    IFACEMETHODIMP_(ULONG) Release() override;

    IFACEMETHODIMP SetUsageScenario(CREDENTIAL_PROVIDER_USAGE_SCENARIO cpus, DWORD flags) override;
    IFACEMETHODIMP SetSerialization(
        const CREDENTIAL_PROVIDER_CREDENTIAL_SERIALIZATION* serialization) override;

    IFACEMETHODIMP Advise(ICredentialProviderEvents* events, UINT_PTR context) override;
    IFACEMETHODIMP UnAdvise() override;

    IFACEMETHODIMP GetFieldDescriptorCount(DWORD* count) override;
    IFACEMETHODIMP GetFieldDescriptorAt(
        DWORD index,
        CREDENTIAL_PROVIDER_FIELD_DESCRIPTOR** descriptor) override;

    IFACEMETHODIMP GetCredentialCount(
        DWORD* count,
        DWORD* defaultIndex,
        BOOL* autoLogonWithDefault) override;
    IFACEMETHODIMP GetCredentialAt(DWORD index, ICredentialProviderCredential** credential) override;

    IFACEMETHODIMP Filter(
        CREDENTIAL_PROVIDER_USAGE_SCENARIO cpus,
        DWORD flags,
        GUID* providers,
        BOOL* allow,
        DWORD count) override;
    IFACEMETHODIMP UpdateRemoteCredential(
        const CREDENTIAL_PROVIDER_CREDENTIAL_SERIALIZATION* in,
        CREDENTIAL_PROVIDER_CREDENTIAL_SERIALIZATION* out) override;

    void SignalReenumerate();

private:
    ~ZagaProvider();

    void releaseCredential();

    LONG _cRef;
    CREDENTIAL_PROVIDER_USAGE_SCENARIO _cpus;
    ICredentialProviderEvents* _events;
    UINT_PTR _adviseContext;
    ZagaCredential* _credential;
    bool _locked;
};

}
