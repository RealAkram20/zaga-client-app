# Removes every trace of a Zaga install from a machine, including a broken one.
#
# Why this exists: the first version registered its own Add/Remove Programs entry
# pointing at C:\Program Files\Zaga\zaga_installer.exe, and nothing removed that entry
# when the files went away. Clicking Uninstall then fails with "Windows cannot find
# ...\zaga_installer.exe" and there is no way out through the UI, because the tool that
# would clean up is the very file that is missing.
#
# Safe to run on a machine with no Zaga install: every step is skip-if-absent.
#
# IMPORTANT: this bypasses the uninstall code. It is a repair tool for a machine you
# physically hold, not a supported removal path. Run it in an ELEVATED PowerShell:
#
#   powershell -ExecutionPolicy Bypass -File cleanup-legacy.ps1
[CmdletBinding()]
param([switch] $WhatIfOnly)

$ErrorActionPreference = 'Stop'

$isAdmin = ([Security.Principal.WindowsPrincipal] [Security.Principal.WindowsIdentity]::GetCurrent()
           ).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) {
    Write-Host "Run this from an elevated PowerShell (Run as administrator)." -ForegroundColor Red
    exit 1
}

$CLSID       = '{B7A9E3C2-4D1F-4A88-9C2E-6F3B1D0A5E77}'
$installDir  = Join-Path $env:ProgramFiles 'Zaga'
$stateDir    = Join-Path $env:ProgramData 'Zaga'

$targets = @(
    @{ What = 'Add/Remove entry (first version)'; Type = 'Key'; Path = "HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\ZagaDeviceLock" }
    @{ What = 'Credential provider registration'; Type = 'Key'; Path = "HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Authentication\Credential Providers\$CLSID" }
    @{ What = 'Credential provider filter';       Type = 'Key'; Path = "HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Authentication\Credential Provider Filters\$CLSID" }
    @{ What = 'COM class registration';           Type = 'Key'; Path = "HKLM:\SOFTWARE\Classes\CLSID\$CLSID" }
    @{ What = 'Device settings and codes';        Type = 'Key'; Path = 'HKLM:\SOFTWARE\Zaga' }
    @{ What = 'Device store';                     Type = 'Dir'; Path = $stateDir }
    @{ What = 'Program files';                    Type = 'Dir'; Path = $installDir }
)

Write-Host "Zaga cleanup$(if ($WhatIfOnly) { ' (dry run)' })`n" -ForegroundColor Cyan

# Unregister the DLL properly first, if it is still there — tidier than deleting the
# keys underneath a registered component.
$dll = Join-Path $installDir 'zaga_lock_provider.dll'
if ((Test-Path $dll) -and -not $WhatIfOnly) {
    try {
        Start-Process regsvr32.exe -ArgumentList '/u', '/s', "`"$dll`"" -Wait -NoNewWindow
        Write-Host "  unregistered  zaga_lock_provider.dll"
    } catch {
        Write-Host "  (regsvr32 /u failed; the keys below are removed directly anyway)" -ForegroundColor DarkYellow
    }
}

# The scheduled check-in.
$task = Get-ScheduledTask -TaskName 'Zaga Device Heartbeat' -ErrorAction SilentlyContinue
if ($task) {
    if ($WhatIfOnly) { Write-Host "  would remove   scheduled task 'Zaga Device Heartbeat'" }
    else {
        Unregister-ScheduledTask -TaskName 'Zaga Device Heartbeat' -Confirm:$false
        Write-Host "  removed       scheduled task 'Zaga Device Heartbeat'"
    }
} else {
    Write-Host "  not present   scheduled task" -ForegroundColor DarkGray
}

# Start-menu shortcut.
$shortcut = Join-Path $env:ProgramData 'Microsoft\Windows\Start Menu\Programs\Zaga Device Lock.lnk'
foreach ($lnk in @($shortcut, (Join-Path $env:PUBLIC 'Desktop\Zaga Device Lock.lnk'))) {
    if (Test-Path $lnk) {
        if ($WhatIfOnly) { Write-Host "  would remove   $lnk" }
        else { Remove-Item $lnk -Force; Write-Host "  removed       $(Split-Path $lnk -Leaf)" }
    }
}

foreach ($t in $targets) {
    if (-not (Test-Path $t.Path)) {
        Write-Host ("  not present   {0}" -f $t.What) -ForegroundColor DarkGray
        continue
    }
    if ($WhatIfOnly) {
        Write-Host ("  would remove  {0}  [{1}]" -f $t.What, $t.Path)
        continue
    }
    try {
        Remove-Item -Path $t.Path -Recurse -Force
        Write-Host ("  removed       {0}" -f $t.What) -ForegroundColor Green
    } catch {
        # A file still in use cannot be deleted now; schedule it for the next boot
        # rather than leaving the caller thinking it is gone.
        Write-Host ("  IN USE        {0} - reboot and re-run" -f $t.What) -ForegroundColor Yellow
    }
}

Write-Host "`nDone. The machine no longer has a Zaga install; a fresh setup will register as a new device." -ForegroundColor Green
