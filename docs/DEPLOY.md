# Deploy — putting the client on a device

The installer and provider are unsigned by default, so Windows shows an
"unknown publisher" prompt (and SmartScreen on a downloaded copy). This guide
covers building a self-contained release, signing it for your own fleet, and
installing it on a device.

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

After this the installer shows "Zaga Device Lock" as the publisher and is not
blocked. This step is a natural part of imaging or provisioning a fleet device.

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
