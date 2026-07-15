# Install & Test — Credential Provider

How to register, provision, and test the Zaga credential provider. **Do the live
logon test in a throwaway VM with a snapshot**, not on your working machine — a
credential provider that misbehaves can block sign-in.

## What gets installed

`zaga_lock_provider.dll` is a COM in-process server that registers as both a
credential provider and a credential provider filter under one class id
(`{B7A9E3C2-4D1F-4A88-9C2E-6F3B1D0A5E77}`). When the device is locked it shows the
unlock tile and hides every other logon option; when the device is active it
shows nothing and normal sign-in proceeds.

## 1. Register

From an **elevated** command prompt, in the folder holding the built DLL:

```
regsvr32 zaga_lock_provider.dll
```

`regsvr32` calls `DllRegisterServer`, which writes:

- `HKCR\CLSID\{clsid}` and `…\InprocServer32` → the DLL path, `ThreadingModel = Apartment`
- `HKLM\…\Authentication\Credential Providers\{clsid}`
- `HKLM\…\Authentication\Credential Provider Filters\{clsid}`

To remove it:

```
regsvr32 /u zaga_lock_provider.dll
```

## 2. Provision the device

The provider reads its state from the machine store at
`C:\ProgramData\Zaga\state.bin` (see `FUNCTIONALITY.md`). Until the provisioning
tool exists (a later milestone), seed a store for testing with the same
`LocalStore` format the app uses. For development you can point the provider at a
different store file with the `ZAGA_STATE_PATH` environment variable — this is the
hook the `provider_host` test uses.

If no store is present, the provider fails closed: it treats the device as locked
and shows the tile with "Not provisioned".

## 3. Test without signing out (fast loop)

`provider_host.exe` loads the real DLL, drives the provider, filter, and
credential through a full unlock, and verifies the state persists — all without
LogonUI:

```
ctest --test-dir build -C Debug -R provider_host --output-on-failure
```

Run this after any change to the provider before touching a live logon.

## 4. Test on the logon screen (VM)

1. Snapshot the VM.
2. Copy the DLL in and `regsvr32` it (step 1).
3. Provision a locked store (step 2).
4. Lock the workstation (Win+L) or sign out.
5. Confirm only the Zaga tile appears, showing the account number and status.
6. Enter a wrong code — it must stay locked with an error.
7. Enter a valid code — the tile reports success; a re-enumeration then reveals
   the normal sign-in so you can log in as usual.
8. Revert the snapshot when done.

## 5. Recovery if you get locked out

If a build blocks sign-in, boot into Safe Mode or the recovery command prompt and
delete the registry keys from step 1 (or run `regsvr32 /u`), then reboot. Keep the
class id handy so you can find the keys offline. This is exactly why the live test
belongs in a snapshotted VM.

## Current limitations

- The tile image is a solid brand-colored placeholder; a real logo resource comes
  later.
- The provider only lifts the gate. It does not itself complete a Windows logon —
  after unlock, the standard providers handle authentication. The re-enumeration
  handoff needs confirmation on real hardware across Windows 10 and 11 builds.
- Provisioning and uninstall-authorization UI are later milestones.
