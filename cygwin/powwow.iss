; Inno Setup script for a Windows installer of powwow, from the tree that
; bundle.sh builds; compile it with Inno Setup 6.3 or later:
;
;     ISCC /DStage=C:\path\to\powwow-win /DVersion=1.3.2 powwow.iss
;
; The installer is written to the Output directory next to this script.

#ifndef Stage
  #error Define Stage as the directory built by bundle.sh: /DStage=...
#endif
#ifndef Version
  #define Version "dev"
#endif
; the folder of the data in Stage, by the prefix that powwow was built with
#ifndef Data
  #define Data "usr\share\powwow"
#endif

[Setup]
AppId=powwow
AppName=powwow
AppVersion={#Version}
AppPublisher=the powwow authors
AppPublisherURL=https://github.com/MUME/powwow
DefaultDirName={autopf}\powwow
DefaultGroupName=powwow
DisableProgramGroupPage=yes
; the license of powwow, and those of the programs and libraries of
; Cygwin bundled with it
LicenseFile={#Stage}\{#Data}\COPYING
InfoBeforeFile={#Stage}\{#Data}\LICENSES.txt
OutputBaseFilename=powwow-{#Version}-setup
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequiredOverridesAllowed=dialog
Compression=lzma2
SolidCompression=yes
SetupIconFile=powwow.ico
UninstallDisplayIcon={app}\powwow.ico
ChangesAssociations=yes

; Windows Terminal, which powwow runs in, and its options: the profile
; of powwow, with its icon and title, from the fragment that the installer
; writes (see CurStepChanged)
#define Terminal "{localappdata}\Microsoft\WindowsApps\wt.exe"
#define TerminalOptions "-p powwow"

[Tasks]
Name: desktopicon; Description: "{cm:CreateDesktopIcon}"; \
    GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
Name: associate; Description: "Open .powwow definition files with powwow"; \
    GroupDescription: "File associations:"

[Files]
Source: "{#Stage}\*"; DestDir: "{app}"; \
    Flags: ignoreversion recursesubdirs createallsubdirs
Source: "powwow.ico"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
; powwow runs in Windows Terminal, which draws emoji well and fast; its
; profile runs powwow-start, in the Documents folder
Name: "{group}\powwow"; Filename: "{#Terminal}"; \
    Parameters: "{#TerminalOptions}"; \
    WorkingDir: "{%USERPROFILE}"; IconFilename: "{app}\powwow.ico"
Name: "{autodesktop}\powwow"; Filename: "{#Terminal}"; \
    Parameters: "{#TerminalOptions}"; \
    WorkingDir: "{%USERPROFILE}"; IconFilename: "{app}\powwow.ico"; \
    Tasks: desktopicon
Name: "{group}\Licenses"; Filename: "{app}\{#Data}\LICENSES.txt"
Name: "{group}\Uninstall powwow"; Filename: "{uninstallexe}"

[Registry]
; opening a .powwow file runs the profile of powwow with the file added to
; its command: powwow with it as the definition file, in its folder, as
; powwow-start does; with the icon of the profile, unlike a command given
Root: HKA; Subkey: "Software\Classes\.powwow"; ValueType: string; \
    ValueName: ""; ValueData: "powwow.definitions"; \
    Flags: uninsdeletevalue; Tasks: associate
Root: HKA; Subkey: "Software\Classes\powwow.definitions"; \
    ValueType: string; ValueName: ""; ValueData: "powwow definitions"; \
    Flags: uninsdeletekey; Tasks: associate
Root: HKA; Subkey: "Software\Classes\powwow.definitions\DefaultIcon"; \
    ValueType: string; ValueName: ""; ValueData: "{app}\powwow.ico"; \
    Tasks: associate
Root: HKA; \
    Subkey: "Software\Classes\powwow.definitions\shell\open\command"; \
    ValueType: string; ValueName: ""; \
    ValueData: """{#Terminal}"" {#TerminalOptions} \
        --appendCommandLine ""%1"""; \
    Tasks: associate

[UninstallDelete]
Type: filesandordirs; \
    Name: "{localappdata}\Microsoft\Windows Terminal\Fragments\powwow"; \
    Check: not IsAdminInstallMode
Type: filesandordirs; \
    Name: "{commonappdata}\Microsoft\Windows Terminal\Fragments\powwow"; \
    Check: IsAdminInstallMode

[Code]
{ powwow runs in Windows Terminal: without it, stop and say how to get it }
function InitializeSetup(): Boolean;
begin
  Result := FileExists(ExpandConstant('{#Terminal}'));
  if not Result then
    SuppressibleMsgBox('powwow runs in Windows Terminal, which is not '
      + 'installed. Install it from the Microsoft Store, or with this '
      + 'command, then run this installer again:' + #13#10#13#10
      + 'winget install Microsoft.WindowsTerminal',
      mbCriticalError, MB_OK, IDOK);
end;

{ the folder of the fragment of powwow in the settings of Windows Terminal:
  of all users or of the user, as the installation is }
function FragmentDir(): String;
begin
  if IsAdminInstallMode then
    Result := '{commonappdata}'
  else
    Result := '{localappdata}';
  Result := ExpandConstant(Result
    + '\Microsoft\Windows Terminal\Fragments\powwow');
end;

{ 'S' in a string of JSON }
function JsonString(const S: String): String;
begin
  Result := S;
  StringChangeEx(Result, '\', '\\', True);
  StringChangeEx(Result, '"', '\"', True);
  Result := '"' + Result + '"';
end;

{ the profile of powwow for Windows Terminal, which the shortcuts and the
  .powwow files open (-p powwow): powwow-start, with the icon and title of
  powwow }
procedure CurStepChanged(CurStep: TSetupStep);
var
  App: String;
  Lines: TArrayOfString;
begin
  if CurStep <> ssPostInstall then
    exit;
  App := ExpandConstant('{app}');
  SetArrayLength(Lines, 11);
  Lines[0] := '{';
  Lines[1] := '  "profiles": [';
  Lines[2] := '    {';
  Lines[3] := '      "name": "powwow",';
  Lines[4] := '      "commandline": '
    + JsonString('"' + App + '\bin\sh.exe" /bin/powwow-start') + ',';
  Lines[5] := '      "icon": ' + JsonString(App + '\powwow.ico') + ',';
  Lines[6] := '      "tabTitle": "powwow",';
  Lines[7] := '      "suppressApplicationTitle": true';
  Lines[8] := '    }';
  Lines[9] := '  ]';
  Lines[10] := '}';
  ForceDirectories(FragmentDir());
  if not SaveStringsToUTF8FileWithoutBOM(FragmentDir() + '\powwow.json',
                                         Lines, False) then
    Log('Cannot write the profile of powwow for Windows Terminal');
end;
