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

[Setup]
AppId=powwow
AppName=powwow
AppVersion={#Version}
AppPublisher=the powwow authors
AppPublisherURL=https://github.com/MUME/powwow
DefaultDirName={autopf}\powwow
DefaultGroupName=powwow
DisableProgramGroupPage=yes
LicenseFile={#Stage}\usr\share\powwow\COPYING
OutputBaseFilename=powwow-{#Version}-setup
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequiredOverridesAllowed=dialog
Compression=lzma2
SolidCompression=yes
UninstallDisplayIcon={app}\bin\mintty.exe

[Tasks]
Name: desktopicon; Description: "{cm:CreateDesktopIcon}"; \
    GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "{#Stage}\*"; DestDir: "{app}"; \
    Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
; powwow runs in mintty, which handles colors, resizing and Unicode
; better than the Windows console
Name: "{group}\powwow"; Filename: "{app}\bin\mintty.exe"; \
    Parameters: "-t powwow -e /bin/powwow"; WorkingDir: "{%USERPROFILE}"
Name: "{autodesktop}\powwow"; Filename: "{app}\bin\mintty.exe"; \
    Parameters: "-t powwow -e /bin/powwow"; WorkingDir: "{%USERPROFILE}"; \
    Tasks: desktopicon
Name: "{group}\Uninstall powwow"; Filename: "{uninstallexe}"
