# Deploy — putting the client on a device

This guide covers building a signed release and installing it on a device.

## 0. Always build with `build-release.ps1`

```powershell
.\build-release.ps1
```

It builds Release, runs the tests, signs the three binaries, packages
`dist\Zaga-Setup.exe`, signs the package, and refuses to ship if anything went in
unsigned or was rebuilt after the signing pass.

**Do not hand-run `cmake --build` and package the result.** Signing is not part of
CMake, so a plain rebuild silently produces unsigned binaries that look identical
and get quarantined on the customer's machine mid-install — surfacing as an error
about a *missing file* rather than the real cause. This has already happened once.
The sections below explain what the script does and why; the script is the
interface.

## 1. Build a self-contained release

The release links the C runtime statically, so it runs on a clean Windows 10/11
x64 with no Visual C++ redistributable.

```
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

Ship these three files together, in one folder:

```
build\Release\zaga_installer.exe
build\Release\zaga_lock_provider.dll
build\Release\zaga_app.exe
```

`zaga_app.exe` is the desktop management app; the installer copies it into the
program folder and adds a Start-menu shortcut. It is optional — the installer works
with just the DLL beside it — but ship it so the device has a real UI to open.

## 2. Why "unknown publisher" appears

Windows trusts code signed by a certificate it recognizes. An unsigned binary
triggers the UAC "unknown publisher" prompt, and a binary downloaded from the
internet also triggers SmartScreen ("Windows protected your PC"). The fix is to
sign the binaries and trust the signer on each device.

## 3. Sign the binaries (self-signed, for your own fleet)

Create a code-signing certificate once and reuse it. Run in PowerShell:

```powershell
$cert = New-SelfSignedCertificate -Type CodeSigningCert `
    -Subject "CN=Zaga Device Lock, O=Zaga" `
    -KeyUsage DigitalSignature -KeyAlgorithm RSA -KeyLength 2048 `
    -CertStoreLocation Cert:\CurrentUser\My `
    -FriendlyName "Zaga Code Signing" -NotAfter (Get-Date).AddYears(5)

foreach ($f in "zaga_installer.exe","zaga_lock_provider.dll","zaga_app.exe") {
    Set-AuthenticodeSignature -FilePath ".\$f" -Certificate $cert `
        -HashAlgorithm SHA256 -TimestampServer "http://timestamp.digicert.com"
}

Export-Certificate -Cert $cert -FilePath ".\ZagaCodeSigning.cer"
```

Keep the certificate (and its private key) safe. Everything you sign with it will
be trusted by devices that trust this certificate. Export a password-protected
`.pfx` for backup if you want to sign from another machine.

## 4. Trust the certificate on each device

Copy `ZagaCodeSigning.cer` to the device and run **as administrator**, in the
folder with the files, before running the installer:

```
certutil -addstore -f Root ZagaCodeSigning.cer
certutil -addstore -f TrustedPublisher ZagaCodeSigning.cer
```

After this the installer shows "Zaga Device Lock" as the publisher. This step is a
natural part of imaging or provisioning a fleet device, and it must happen **before**
the installer runs.

## 4b. When Defender blocks the install anyway

> *"Operation did not complete successfully because the file contains a virus or
> potentially unwanted software"* — sometimes followed by *"The system cannot find
> the file specified"*, which is the same problem one step later: Defender
> quarantined the file mid-copy, so the installer's next read finds nothing.

Trusting the certificate does **not** prevent this. It fixes *unknown publisher*,
which is a trust question. An antivirus detection is a reputation question, and a
self-signed certificate carries no reputation at all — no certificate authority
vouches for it, so it earns nothing with Defender or SmartScreen. A credential
provider is close to a worst case for antivirus heuristics: it hooks the login
screen, writes DPAPI blobs, and registers COM in winlogon, which is exactly the
shape of credential-stealing malware.

In rough order of how durable they are:

1. **Buy a real code-signing certificate** (OV, or EV for immediate SmartScreen
   reputation) from a public CA. This is the only fix that generalises to machines
   you do not control. For software that ships a credential provider to customer
   devices, treat it as a cost of doing business, not an optimisation.
2. **Report the false positive** to Microsoft at
   <https://www.microsoft.com/en-us/wdsi/filesubmission>. Free, usually resolved in
   a few days. It allow-lists by file hash, so **every rebuild needs resubmitting** —
   useful for a fixed release, useless during active development.
3. **Add a Defender exclusion during provisioning**, before installing:
   `Add-MpPreference -ExclusionPath "C:\Program Files\Zaga"`. Defensible on a fleet
   you manage and have physical access to. Never ask a customer to do this, and note
   that Tamper Protection can block it.

Detection is per-machine: it depends on definition version, cloud verdict, whether
PUA protection is set to Block (the Windows 11 default) or Audit, and whether the
file arrived with a download tag. A machine that installs cleanly proves nothing
about the next one — test on a device configured like the fleet, not like a dev box.

## 5. Install and provision

### Guided (double-click)

Double-click `zaga_installer.exe`. It requests administrator rights through a UAC
prompt, installs the provider (dormant), copies the management app and adds its
Start-menu shortcut, schedules the hourly check-in, registers an entry in *Add or
remove programs*, and then offers to provision: enter the portal URL and a one-time
enrollment code, or leave the URL blank to skip. It finally offers to open the Zaga
app, and the window stays open so you can read the result.

### Command line

From an elevated command prompt in the folder with both files:

```
zaga_installer install
zaga_installer enroll --code <code-from-portal> --url http://<portal-address>/zagatech
zaga_installer enable
```

- Get the code on the portal: `php artisan device:enroll-code ZG-xxxxx`.
- The install is dormant; nothing locks until `enable`.
- The portal address must be reachable from this device. On another machine that
  is not `localhost` — use the portal's LAN address or domain, e.g.
  `http://192.168.1.20/zagatech`. Check with `zaga_installer status`.

### Uninstall

Use **Settings → Apps → Installed apps → Zaga Device Lock → Uninstall**, or run
`zaga_installer uninstall` from an elevated prompt. Both unregister the provider,
remove the files and scheduled task, and clear all settings. If removal protection
is on, the command line form needs `--code <uninstall code>`.

## 6. Selling to the public

A self-signed certificate only clears the prompt on devices where you install the
certificate — right for a fleet you control. To ship to the general public without
that per-device step, buy a code-signing certificate from a certificate authority.
An OV certificate builds SmartScreen reputation over time; an EV certificate clears
SmartScreen immediately and is the usual choice for security software.

## 7. Recovery

If a build blocks sign-in on the device, boot into Safe Mode or the recovery
command prompt and run `zaga_installer uninstall`, or delete the provider's
registry keys by hand, then reboot. Test in a snapshotted VM first.
