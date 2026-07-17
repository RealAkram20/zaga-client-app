# Builds, signs, and packages a shippable Zaga-Setup.exe.
#
# Signing is part of the build rather than a step in a document, because an unsigned
# credential-provider DLL is treated as malware by Defender and the failure surfaces
# on a customer's machine mid-install, not here. Any rebuild that skips signing
# silently reintroduces that, so there is deliberately no way to package without it.
#
#   .\build-release.ps1                 build, sign, package
#   .\build-release.ps1 -SkipBuild      re-sign and repackage what is already built
[CmdletBinding()]
param(
    [string] $CertSubject = 'CN=Zaga Device Lock, O=Zaga',
    [string] $TimestampServer = 'http://timestamp.digicert.com',
    [switch] $SkipBuild
)

$ErrorActionPreference = 'Stop'
$repo = $PSScriptRoot
$releaseDir = Join-Path $repo 'build\Release'
$binaries = @('zaga_lock_provider.dll', 'zaga_installer.exe', 'zaga_app.exe')

function Find-Tool([string] $name, [string[]] $candidates) {
    foreach ($path in $candidates) {
        if ($path -and (Test-Path $path)) { return $path }
    }
    $found = Get-Command $name -ErrorAction SilentlyContinue
    if ($found) { return $found.Source }
    throw "Could not find $name. Looked in: $($candidates -join '; ')"
}

$cmake = Find-Tool 'cmake.exe' @(
    "$env:ProgramFiles\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe",
    "$env:ProgramFiles\CMake\bin\cmake.exe"
)
$iscc = Find-Tool 'ISCC.exe' @(
    "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe",
    "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe"
)

# Resolve the certificate before building, so a missing key fails in seconds rather
# than after a full compile.
$cert = Get-ChildItem Cert:\CurrentUser\My, Cert:\LocalMachine\My -ErrorAction SilentlyContinue |
    Where-Object { $_.Subject -eq $CertSubject -and $_.HasPrivateKey } |
    Sort-Object NotAfter -Descending | Select-Object -First 1
if (-not $cert) {
    throw "No code-signing certificate with a private key for '$CertSubject'. See docs/DEPLOY.md."
}
if ($cert.NotAfter -lt (Get-Date)) {
    throw "The signing certificate expired on $($cert.NotAfter). See docs/DEPLOY.md."
}
Write-Host "Signing as $($cert.Subject), valid to $($cert.NotAfter)" -ForegroundColor Cyan

if (-not $SkipBuild) {
    Write-Host "`n== Building Release ==" -ForegroundColor Cyan
    & $cmake -S $repo -B (Join-Path $repo 'build') | Out-Null
    & $cmake --build (Join-Path $repo 'build') --config Release
    if ($LASTEXITCODE -ne 0) { throw "Build failed." }

    Write-Host "`n== Tests ==" -ForegroundColor Cyan
    $ctest = Join-Path (Split-Path $cmake) 'ctest.exe'
    Push-Location (Join-Path $repo 'build')
    try {
        & $ctest -C Release
        if ($LASTEXITCODE -ne 0) { throw "Tests failed - refusing to package." }
    } finally { Pop-Location }
}

function Set-ZagaSignature([string] $path) {
    if (-not (Test-Path $path)) { throw "Missing $path" }
    $result = Set-AuthenticodeSignature -FilePath $path -Certificate $cert `
        -HashAlgorithm SHA256 -TimestampServer $TimestampServer
    # A self-signed chain reports UnknownError on machines that do not trust the
    # certificate yet, which is expected; the signature itself is still applied.
    # An actually broken signing attempt leaves no signer at all.
    $check = Get-AuthenticodeSignature $path
    if (-not $check.SignerCertificate) { throw "Signing produced no signature on $path" }
    if (-not $check.TimeStamperCertificate) {
        throw "No timestamp on $path - it would stop verifying when the certificate expires."
    }
    Write-Host ("  signed {0,-24} [{1}]" -f (Split-Path $path -Leaf), $result.Status)
}

Write-Host "`n== Signing binaries ==" -ForegroundColor Cyan
foreach ($b in $binaries) { Set-ZagaSignature (Join-Path $releaseDir $b) }

# Package after signing so the setup carries signed payloads, then sign the setup
# itself - both matter: Windows checks the package on launch and the DLL on load.
Write-Host "`n== Packaging ==" -ForegroundColor Cyan
Push-Location $repo
try {
    & $iscc /Qp zaga.iss
    if ($LASTEXITCODE -ne 0) { throw "ISCC failed." }
} finally { Pop-Location }

$setup = Join-Path $repo 'dist\Zaga-Setup.exe'
Write-Host "`n== Signing setup ==" -ForegroundColor Cyan
Set-ZagaSignature $setup

# Guard against the exact regression this script exists to prevent: a package built
# from binaries that were rebuilt, and thus unsigned, after the last signing pass.
foreach ($b in $binaries) {
    $bin = Join-Path $releaseDir $b
    if ((Get-Item $bin).LastWriteTime -gt (Get-Item $setup).LastWriteTime) {
        throw "$b is newer than the package - rebuild and repackage."
    }
    if (-not (Get-AuthenticodeSignature $bin).SignerCertificate) {
        throw "$b went into the package unsigned."
    }
}

$hash = (Get-FileHash $setup -Algorithm SHA256).Hash
Write-Host "`nReady: $setup" -ForegroundColor Green
Write-Host "SHA256: $hash"
Write-Host "Run dist\trust-zaga-cert.cmd as administrator on each device before installing."
