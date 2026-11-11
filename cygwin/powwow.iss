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
; the license of powwow, and those of the programs and libraries of
; Cygwin bundled with it
LicenseFile={#Stage}\usr\share\powwow\COPYING
InfoBeforeFile={#Stage}\usr\share\powwow\LICENSES.txt
OutputBaseFilename=powwow-{#Version}-setup
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequiredOverridesAllowed=dialog
Compression=lzma2
SolidCompression=yes
SetupIconFile=powwow.ico
UninstallDisplayIcon={app}\powwow.ico
ChangesAssociations=yes

; mintty and its options: the title, and the icon of its window, by
; its path in the Cygwin of the bundle, whose root is {app}
#define Mintty "-t powwow -i /powwow.ico"
; what runs in it: powwow, in the Documents folder unless given a file
#define Start "/bin/sh /bin/powwow-start"

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
; powwow runs in mintty, which handles colors, resizing and Unicode
; better than the Windows console
Name: "{group}\powwow"; Filename: "{app}\bin\mintty.exe"; \
    Parameters: "{#Mintty} -e {#Start}"; WorkingDir: "{%USERPROFILE}"; \
    IconFilename: "{app}\powwow.ico"
Name: "{autodesktop}\powwow"; Filename: "{app}\bin\mintty.exe"; \
    Parameters: "{#Mintty} -e {#Start}"; WorkingDir: "{%USERPROFILE}"; \
    IconFilename: "{app}\powwow.ico"; Tasks: desktopicon
Name: "{group}\Licenses"; Filename: "{app}\usr\share\powwow\LICENSES.txt"
Name: "{group}\Uninstall powwow"; Filename: "{uninstallexe}"

[Registry]
; opening a .powwow file runs powwow with it as the definition file
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
    ValueData: """{app}\bin\mintty.exe"" {#Mintty} -e {#Start} ""%1"""; \
    Tasks: associate
