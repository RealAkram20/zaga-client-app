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
; InitializeUninstall has already checked the code; passing it here means the
; installer enforces it again rather than trusting the wizard.
Filename: "{app}\zaga_installer.exe"; Parameters: "unregister --code ""{code:GetUninstallCode}"""; \
    Flags: runhidden waituntilterminated; RunOnceId: "ZagaUnregister"

[UninstallDelete]
; The device store lives outside {app}, so the package would otherwise leave it and a
; reinstall would adopt the old identity. "unregister" already deletes it; this is the
; backstop for when that binary is missing or did not run.
Type: files; Name: "{commonappdata}\Zaga\state.bin"
Type: dirifempty; Name: "{commonappdata}\Zaga"

[Code]
var
  EnrollPage: TInputQueryWizardPage;
  UninstallCodeValue: String;

procedure InitializeWizard;
begin
  EnrollPage := CreateInputQueryPage(wpSelectTasks,
    'Enroll this device',
    'Connect the device to your billing portal (optional).',
    'Only fill this in if the device is already registered on the portal and you ' +
    'have its one-time enrollment code. Otherwise leave both blank: open the Zaga ' +
    'app after setup, register the device with the details it shows, and enroll ' +
    'from there.');
  EnrollPage.Add('Portal URL (e.g. http://192.168.1.20/zagatech):', False);
  EnrollPage.Add('Enrollment code:', False);
  EnrollPage.Values[0] := 'http://';
end;

function GetUninstallCode(Param: String): String;
begin
  Result := UninstallCodeValue;
end;

// Pascal Script has no InputQuery, and TInputQueryWizardPage belongs to the install
// wizard, so the uninstall prompt is built by hand.
function AskUninstallCode(var Code: String): Boolean;
var
  Form: TSetupForm;
  Prompt: TNewStaticText;
  Edit: TNewEdit;
  OKButton, CancelButton: TNewButton;
  W: Integer;
begin
  Result := False;
  Form := CreateCustomForm(ScaleX(380), ScaleY(150), False, True);
  try
    Form.Caption := 'Zaga Device Lock';

    Prompt := TNewStaticText.Create(Form);
    Prompt.Parent := Form;
    Prompt.Left := ScaleX(10);
    Prompt.Top := ScaleY(10);
    Prompt.Width := Form.ClientWidth - ScaleX(2 * 10);
    Prompt.Height := ScaleY(34);
    Prompt.AutoSize := False;
    Prompt.WordWrap := True;
    Prompt.Caption := 'Removal of this device is protected.' + #13#10 +
                      'Contact support for the uninstall code. It is released once your' + #13#10 +
                      'payment plan is complete.';

    Edit := TNewEdit.Create(Form);
    Edit.Parent := Form;
    Edit.Left := ScaleX(10);
    Edit.Top := ScaleY(54);
    Edit.Width := Form.ClientWidth - ScaleX(2 * 10);
    Edit.Height := ScaleY(23);

    OKButton := TNewButton.Create(Form);
    OKButton.Parent := Form;
    OKButton.Caption := 'OK';
    OKButton.Left := Form.ClientWidth - ScaleX(75 + 6 + 75 + 10);
    OKButton.Top := Form.ClientHeight - ScaleY(23 + 10);
    OKButton.Height := ScaleY(23);
    OKButton.ModalResult := mrOk;
    OKButton.Default := True;

    CancelButton := TNewButton.Create(Form);
    CancelButton.Parent := Form;
    CancelButton.Caption := 'Cancel';
    CancelButton.Left := Form.ClientWidth - ScaleX(75 + 10);
    CancelButton.Top := Form.ClientHeight - ScaleY(23 + 10);
    CancelButton.Height := ScaleY(23);
    CancelButton.ModalResult := mrCancel;
    CancelButton.Cancel := True;

    W := Form.CalculateButtonWidth([OKButton.Caption, CancelButton.Caption]);
    OKButton.Width := W;
    CancelButton.Width := W;

    Form.ActiveControl := Edit;

    if Form.ShowModal() = mrOk then
    begin
      Code := Trim(Edit.Text);
      Result := True;
    end;
  finally
    Form.Free();
  end;
end;

// Removal protection is armed at install, so uninstalling through Add/Remove
// Programs has to ask for the code the same way the console verb does.
function InitializeUninstall(): Boolean;
var
  Installer: String;
  Code: String;
  ResultCode: Integer;
begin
  Result := True;
  Installer := ExpandConstant('{app}\zaga_installer.exe');
  if not FileExists(Installer) then
    Exit;

  // Exit code 0 means protection is on; anything else means there is nothing to ask.
  if not Exec(Installer, 'is-protected', '', SW_HIDE, ewWaitUntilTerminated, ResultCode) then
    Exit;
  if ResultCode <> 0 then
    Exit;

  Code := '';
  repeat
    if not AskUninstallCode(Code) then
    begin
      Result := False;
      Exit;
    end;

    Exec(Installer, 'check-code --code "' + Code + '"', '', SW_HIDE,
         ewWaitUntilTerminated, ResultCode);
    if ResultCode <> 0 then
      MsgBox('That code is not correct for this device.' + #13#10 + #13#10 +
             'Contact support to be issued the uninstall code for this device.',
             mbError, MB_OK);
  until ResultCode = 0;

  UninstallCodeValue := Code;
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
