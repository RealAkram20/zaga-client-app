# Install & Test — Credential Provider

How to install, provision, arm, and test the Zaga client on a device. **Do the
live logon test in a throwaway VM with a snapshot**, not on your working machine —
a credential provider that misbehaves can block sign-in.

## What ships

- `zaga_lock_provider.dll` — the credential provider and filter (one COM class id,
  `{B7A9E3C2-4D1F-4A88-9C2E-6F3B1D0A5E77}`).
- `zaga_installer.exe` — installs, provisions, arms, and removes the client. It
  requests administrator rights, so double-clicking prompts for elevation.

Keep the two files together; the installer copies the DLL from its own folder.

## Off by default

A fresh install is **dormant**: the lock is disabled and removal is not protected.
Installing changes nothing at the login screen. The client only gates login after
you provision a device **and** run `enable`. This is deliberate, so an install can
never lock out a test machine on its own.

## 1. Install

From an elevated command prompt, in the folder holding both files:

```
zaga_installer install
```

This copies the DLL to `C:\Program Files\Zaga`, registers it (both as a provider
and a filter), and leaves the lock disabled.

## 2. Provision the device

Write the device's provisioning bundle (account number and HMAC secret) into the
machine store:

```
zaga_installer provision --account ZG-40000 --secret <64-hex-secret> ^
    --serial 5CG9482Q1B --model "Dell Latitude 5490" --name "Reception PC"
```

Retrieve the bundle from the portal device page (super-admin, audit-logged).
Fetching it directly from the portal over the network is a planned enhancement;
until then the bundle is passed in here.

## 3. Arm the lock

```
zaga_installer enable
```

From now on, when the provisioned device is overdue, the tile gates login. Check
state at any time:

```
zaga_installer status
```

## 4. Protect against removal (optional)

By default the client can be uninstalled freely. To require the device's uninstall
code first:

```
zaga_installer protect --code <uninstall code>
zaga_installer unprotect --code <uninstall code>
```

## 5. Uninstall

```
zaga_installer uninstall
```

If removal protection is on, pass `--code <uninstall code>`. Uninstall unregisters
the provider, deletes the files, and clears all settings.

## 6. Fast test loop (no sign-out)

`provider_host.exe` loads the real DLL and drives the provider, filter, and
credential through a full unlock without LogonUI:

```
ctest --test-dir build -C Debug -R provider_host --output-on-failure
```

Run this after any provider change before touching a live logon.

## 7. Live logon test (VM)

1. Snapshot the VM.
2. `zaga_installer install`, then `provision`, then `enable`.
3. Set the store's deadline in the past (or use an overdue bundle) so the device
   is locked.
4. Lock the workstation (Win+L) or sign out.
5. Confirm only the Zaga tile appears with the account number and status.
6. Wrong code must stay locked with an error; a valid code reports success and a
   re-enumeration then reveals normal sign-in.
7. Revert the snapshot.

## 8. Recovery if you get locked out

Boot into Safe Mode or the recovery command prompt and either run
`zaga_installer uninstall` or delete the registry keys by hand, then reboot. This
is exactly why the live test belongs in a snapshotted VM.

## Current limitations

- The tile image is a solid brand-colored placeholder; a real logo resource comes
  later.
- Provisioning takes the bundle on the command line; pulling it from the portal
  over the network is the next step.
- The provider only lifts the gate; it does not complete a Windows logon. The
  post-unlock handoff needs confirmation on real Windows 10 and 11 builds.
