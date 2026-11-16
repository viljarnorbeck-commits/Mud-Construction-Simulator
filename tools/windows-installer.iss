#ifndef GameFiles
  #error "GameFiles must point to a verified Windows package"
#endif
#ifndef BuildVersion
  #error "BuildVersion is required"
#endif
#ifndef GameId
  #define GameId "MudConstructionSimulator"
#endif
#ifdef SignedBuild
  #define Distribution "Signed"
#else
  #define Distribution "Local"
#endif

[Setup]
AppId={#GameId}
AppName=Mud Construction Simulator
AppVersion={#BuildVersion}
DefaultDirName={localappdata}\Programs\Mud Construction Simulator
DefaultGroupName=Mud Construction Simulator
DisableProgramGroupPage=yes
UninstallDisplayIcon={app}\versions\{#BuildVersion}\MudConstructionSimulator.exe
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputBaseFilename=MudConstructionSimulator-{#BuildVersion}-Setup
VersionInfoVersion={#BuildVersion}.0
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
CloseApplications=no
AppMutex=Local\{#GameId}.Game
SetupMutex=Local\{#GameId}.Setup
#ifdef SignedBuild
SignTool=MudSign
SignedUninstaller=yes
#endif

[Languages]
Name: "swedish"; MessagesFile: "compiler:Languages\Swedish.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[CustomMessages]
swedish.LaunchGame=Starta Mud Construction Simulator
english.LaunchGame=Launch Mud Construction Simulator
swedish.NewerInstalled=En nyare version är redan installerad.
english.NewerInstalled=A newer version is already installed.
swedish.SignedRequired=Den installerade versionen är signerad och kräver en signerad uppdatering.
english.SignedRequired=The installed version requires a signed update.

[Files]
Source: "{#GameFiles}\*"; DestDir: "{app}\versions\{#BuildVersion}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\Mud Construction Simulator"; Filename: "{app}\versions\{#BuildVersion}\MudConstructionSimulator.exe"; WorkingDir: "{app}\versions\{#BuildVersion}"

[Registry]
Root: HKCU; Subkey: "Software\{#GameId}"; ValueType: string; ValueName: "Version"; ValueData: "{#BuildVersion}"; Flags: uninsdeletevalue uninsdeletekeyifempty
Root: HKCU; Subkey: "Software\{#GameId}"; ValueType: string; ValueName: "Distribution"; ValueData: "{#Distribution}"; Flags: uninsdeletevalue uninsdeletekeyifempty

[Run]
Filename: "{app}\versions\{#BuildVersion}\MudConstructionSimulator.exe"; WorkingDir: "{app}\versions\{#BuildVersion}"; Description: "{cm:LaunchGame}"; Flags: postinstall nowait skipifsilent

[Code]
const
  ProductKey = 'Software\{#GameId}';
  ReleaseVersion = '{#BuildVersion}';

function ReadVersion(const Value: String; var Packed: Int64): Boolean;
var
  I, Dots: Integer;
begin
  Result := False;
  if (Length(Value) < 5) or (Length(Value) > 17) then
    Exit;
  Dots := 0;
  for I := 1 to Length(Value) do
    if Value[I] = '.' then
      Dots := Dots + 1
    else if (Value[I] < '0') or (Value[I] > '9') then
      Exit;
  if Dots = 2 then
    Result := StrToVersion(Value + '.0', Packed);
end;

function InitializeSetup(): Boolean;
var
  HaveVersion, NextVersion: Int64;
  InstalledVersion, InstalledDistribution: String;
begin
  Result := False;
  InstalledVersion := '';
  RegQueryStringValue(HKCU, ProductKey, 'Distribution', InstalledDistribution);
  if ('{#Distribution}' = 'Local') and (InstalledDistribution = 'Signed') then
  begin
    SuppressibleMsgBox(CustomMessage('SignedRequired'), mbError, MB_OK, IDOK);
    Exit;
  end;

  if not RegQueryStringValue(HKCU, ProductKey, 'Version', InstalledVersion) then
    RegQueryStringValue(HKCU, 'Software\Microsoft\Windows\CurrentVersion\Uninstall\{#GameId}_is1',
      'DisplayVersion', InstalledVersion);
  if ReadVersion(InstalledVersion, HaveVersion) and ReadVersion(ReleaseVersion, NextVersion) then
  begin
    if ComparePackedVersion(NextVersion, HaveVersion) < 0 then
    begin
      SuppressibleMsgBox(CustomMessage('NewerInstalled'), mbError, MB_OK, IDOK);
      Exit;
    end;
  end;
  Result := True;
end;
