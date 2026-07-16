; Zaga Device Lock — single-file setup wizard.
;
; Bundles the credential provider DLL, the desktop app, and the console installer
; into one Zaga-Setup.exe. The package owns the files, shortcuts, and the Add/Remove
; Programs entry; zaga_installer's thin "register"/"unregister" verbs do only the
; provider COM registration, the hourly check-in task, and the dormant default.
;
; Build:  "%LOCALAPPDATA%\Programs\Inno Setup 6\ISCC.exe" zaga.iss
; Output: dist\Zaga-Setup.exe

#define AppName "Zaga Device Lock"
#define AppVersion "0.1.0"
#define Publisher "Zaga"

[Setup]
; AppId ties an install to its uninstaller — keep it stable across versions.
AppId={{B7A9E3C2-4D1F-4A88-9C2E-6F3B1D0A5E77}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher={#Publisher}
DefaultDirName={autopf}\Zaga
DisableProgramGroupPage=yes
UninstallDisplayName={#AppName}
UninstallDisplayIcon={app}\zaga_app.exe
; A credential provider is machine-wide and needs admin to register.
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir=dist
OutputBaseFilename=Zaga-Setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
SetupIconFile=assets\zaga.ico

[Files]
Source: "build\Release\zaga_lock_provider.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "build\Release\zaga_installer.exe";      DestDir: "{app}"; Flags: ignoreversion
Source: "build\Release\zaga_app.exe";            DestDir: "{app}"; Flags: ignoreversion

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Additional shortcuts:"

[Icons]
Name: "{autoprograms}\Zaga Device Lock"; Filename: "{app}\zaga_app.exe"
Name: "{autodesktop}\Zaga Device Lock";  Filename: "{app}\zaga_app.exe"; Tasks: desktopicon

[Run]
; Register the login provider (dormant) and schedule the hourly check-in.
Filename: "{app}\zaga_installer.exe"; Parameters: "register"; \
    StatusMsg: "Registering the login provider..."; Flags: runhidden waituntilterminated
; Optionally enroll with the portal, only when both fields were filled in.
Filename: "{app}\zaga_installer.exe"; \
    Parameters: "enroll --url ""{code:GetUrl}"" --code ""{code:GetCode}"""; \
    StatusMsg: "Enrolling with the portal..."; Flags: runhidden waituntilterminated; \
    Check: WantEnroll
; Offer to open the app when setup finishes.
Filename: "{app}\zaga_app.exe"; Description: "Open Zaga Device Lock now"; \
    Flags: postinstall nowait skipifsilent

[UninstallRun]
Filename: "{app}\zaga_installer.exe"; Parameters: "unregister"; \
    Flags: runhidden waituntilterminated; RunOnceId: "ZagaUnregister"

[Code]
var
  EnrollPage: TInputQueryWizardPage;

procedure InitializeWizard;
begin
  EnrollPage := CreateInputQueryPage(wpSelectTasks,
    'Enroll this device',
    'Connect the device to your billing portal (optional).',
    'Enter the portal address and the one-time enrollment code from the portal. ' +
    'Leave both blank to skip — you can enroll later from the Zaga app.');
  EnrollPage.Add('Portal URL (e.g. http://192.168.1.20/zagatech):', False);
  EnrollPage.Add('Enrollment code:', False);
  EnrollPage.Values[0] := 'http://';
end;

function GetUrl(Param: String): String;
begin
  Result := Trim(EnrollPage.Values[0]);
  if Result = 'http://' then
    Result := '';
end;

function GetCode(Param: String): String;
begin
  Result := Trim(EnrollPage.Values[1]);
end;

function WantEnroll: Boolean;
begin
  Result := (GetUrl('') <> '') and (GetCode('') <> '');
end;
