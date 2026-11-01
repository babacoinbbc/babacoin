; Build only from a verified Windows release payload. No wallet files are packaged.
#ifndef PayloadDir
  #error PayloadDir is required
#endif
#ifndef OutputDir
  #error OutputDir is required
#endif
#ifndef ReleaseTag
  #error ReleaseTag is required
#endif

[Setup]
AppId={{C40A2BB5-A761-47E2-A872-284A9E6BBC02}
AppName=Babacoin Core
AppVersion={#ReleaseTag}
AppVerName=Babacoin Core {#ReleaseTag}
AppPublisher=Babacoin contributors
AppPublisherURL=https://github.com/babacoinbbc/babacoin
AppSupportURL=https://github.com/babacoinbbc/babacoin/issues
DefaultDirName={localappdata}\Programs\Babacoin Core
DefaultGroupName=Babacoin Core
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
WizardStyle=modern dynamic
DisableWelcomePage=no
DisableProgramGroupPage=yes
UsePreviousAppDir=yes
CloseApplications=no
RestartApplications=no
AlwaysRestart=no
SetupMutex=BabacoinCoreSetup
Compression=lzma2
SolidCompression=yes
OutputDir={#OutputDir}
OutputBaseFilename=babacoin-{#ReleaseTag}-windows-x86_64-setup
UninstallDisplayIcon={app}\babacoin-qt.exe
LicenseFile={#PayloadDir}\COPYING
InfoBeforeFile=installer-info.txt
SetupIconFile=..\..\src\qt\res\icons\babacoin.ico
VersionInfoVersion=2.0.0.0

[Languages]
Name: english; MessagesFile: compiler:Default.isl

[Tasks]
Name: desktopicon; Description: Create a desktop shortcut; GroupDescription: Shortcuts:

[Files]
Source: "{#PayloadDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\Babacoin Core"; Filename: "{app}\babacoin-qt.exe"; Parameters: "{code:WalletArguments}"; WorkingDir: "{app}"
Name: "{autodesktop}\Babacoin Core"; Filename: "{app}\babacoin-qt.exe"; Parameters: "{code:WalletArguments}"; WorkingDir: "{app}"; Tasks: desktopicon

[Registry]
Root: HKCU; Subkey: "Software\Babacoin\Installer"; ValueType: string; ValueName: WalletDirectory; ValueData: "{code:WalletDirectory}"; Flags: uninsdeletevalue uninsdeletekeyifempty

[Run]
Filename: "{app}\babacoin-qt.exe"; Parameters: "{code:WalletArguments}"; WorkingDir: "{app}"; Description: Launch Babacoin Core; Flags: nowait postinstall skipifsilent unchecked

[Code]
var
  WalletChoice: TInputOptionWizardPage;
  WalletFolder: TInputDirWizardPage;

function HasWalletData(const Directory: String): Boolean;
begin
  Result := FileExists(AddBackslash(Directory) + 'wallet.dat') or
    FileExists(AddBackslash(Directory) + 'babacoin.conf') or
    FileExists(AddBackslash(Directory) + '.cookie') or
    DirExists(AddBackslash(Directory) + 'blocks') or
    DirExists(AddBackslash(Directory) + 'chainstate') or
    DirExists(AddBackslash(Directory) + 'wallets') or
    FileExists(AddBackslash(Directory) + 'Data\wallet.dat');
end;

function WalletDirectory(Param: String): String;
begin
  Result := '';
  if WalletChoice.SelectedValueIndex = 1 then
    Result := Trim(WalletFolder.Values[0]);
end;

function WalletArguments(Param: String): String;
var Directory: String;
begin
  Directory := WalletDirectory('');
  Result := '';
  if Directory <> '' then
    Result := '-datadir="' + Directory + '"';
end;

procedure InitializeWizard;
var Saved: String;
begin
  WalletChoice := CreateInputOptionPage(wpSelectDir, 'Your wallet',
    'Keep using your existing wallet',
    'Choose how Babacoin should find your wallet. Setup does not copy or modify wallet data.', True, False);
  WalletChoice.Add('Use my saved Babacoin settings (recommended)');
  WalletChoice.Add('Use an existing wallet data folder');
  WalletChoice.SelectedValueIndex := 0;
  WalletFolder := CreateInputDirPage(WalletChoice.ID, 'Existing wallet folder',
    'Select the folder containing your wallet and blockchain data',
    'Choose your existing data folder, not the program installation folder. Your wallet files stay in this location.', False, '');
  WalletFolder.Add('Wallet data folder:');
  Saved := ExpandConstant('{param:WALLETDIR|}');
  if Saved = '' then
    RegQueryStringValue(HKCU, 'Software\Babacoin\Installer', 'WalletDirectory', Saved);
  if Saved <> '' then begin
    WalletFolder.Values[0] := Saved;
    WalletChoice.SelectedValueIndex := 1;
  end else if RegQueryStringValue(HKCU, 'Software\Babacoin\Babacoin-Qt', 'strDataDir', Saved) then
    WalletFolder.Values[0] := Saved;
end;

function ShouldSkipPage(PageID: Integer): Boolean;
begin
  Result := (PageID = WalletFolder.ID) and (WalletChoice.SelectedValueIndex <> 1);
end;

function FolderError: String;
var Directory: String;
begin
  Result := '';
  if HasWalletData(WizardDirValue) then begin
    Result := 'Choose a separate program folder. The selected installation folder contains wallet or blockchain data.';
    exit;
  end;
  Directory := WalletDirectory('');
  if WalletChoice.SelectedValueIndex = 1 then begin
    if (Directory = '') or (Pos('"', Directory) > 0) or
       (Pos(#13, Directory) > 0) or (Pos(#10, Directory) > 0) or
       not DirExists(Directory) or not HasWalletData(Directory) then
      Result := 'Select an existing Babacoin data folder containing wallet.dat or blockchain data, or choose saved settings.';
  end;
end;

function NextButtonClick(CurPageID: Integer): Boolean;
var Error: String;
begin
  Result := True;
  if WizardSilent then exit;
  if (CurPageID = wpSelectDir) or (CurPageID = WalletFolder.ID) then begin
    Error := '';
    if CurPageID = wpSelectDir then begin
      if HasWalletData(WizardDirValue) then
        Error := 'Choose a separate program folder. The selected installation folder contains wallet or blockchain data.';
    end else
      Error := FolderError;
    if Error <> '' then begin
      SuppressibleMsgBox(Error, mbError, MB_OK, IDOK);
      Result := False;
    end;
  end;
end;

function RunningWallet: Boolean;
var Locator, Services, Processes: Variant;
begin
  { Do not terminate a running wallet or daemon, including another version. }
  Locator := CreateOleObject('WbemScripting.SWbemLocator');
  Services := Locator.ConnectServer('', 'root\CIMV2');
  Processes := Services.ExecQuery('SELECT Name FROM Win32_Process WHERE Name="babacoin-qt.exe" OR Name="babacoind.exe"');
  Result := Processes.Count > 0;
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  Result := FolderError;
  if Result <> '' then exit;
  try
    if RunningWallet then
      Result := 'Close Babacoin Core and babacoind normally before installing. Setup will not close them for you.';
  except
    Result := 'Windows could not check for a running Babacoin wallet. Close Babacoin and retry Setup.';
  end;
end;

function InitializeUninstall: Boolean;
begin
  Result := False;
  try
    if not RunningWallet then begin
      Result := True;
      exit;
    end;
  except
  end;
  SuppressibleMsgBox('Close Babacoin Core and babacoind normally before uninstalling. Wallet data will be kept.', mbError, MB_OK, IDOK);
end;

procedure RegisterPreviousData(PreviousDataKey: Integer);
begin
  SetPreviousData(PreviousDataKey, 'WalletDirectory', WalletDirectory(''));
end;
