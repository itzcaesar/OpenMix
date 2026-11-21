; Inno Setup script for OpenMix. Built by CI for version tags:
;   iscc /DAppVersion=0.1.0 /DSourceDir=..\dist installer\OpenMix.iss

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef SourceDir
  #define SourceDir "..\dist"
#endif

[Setup]
AppId={{6B0F4C1E-8C2B-4F5B-9B65-4E0D8E7A51C3}
AppName=OpenMix
AppVersion={#AppVersion}
AppVerName=OpenMix {#AppVersion}
AppPublisher=OpenMix
AppPublisherURL=https://github.com/itzcaesar/OpenMix
DefaultDirName={autopf}\OpenMix
DefaultGroupName=OpenMix
DisableProgramGroupPage=yes
LicenseFile={#SourceDir}\LICENSE
OutputDir=..\installer-out
OutputBaseFilename=OpenMix-{#AppVersion}-setup
Compression=lzma2
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequiredOverridesAllowed=dialog
WizardStyle=modern
UninstallDisplayIcon={app}\OpenMix.exe
CloseApplications=yes

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; Flags: unchecked
Name: "startup"; Description: "Start OpenMix when Windows starts (in the tray)"; Flags: unchecked

[Files]
Source: "{#SourceDir}\OpenMix.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceDir}\LICENSE"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceDir}\README.md"; DestDir: "{app}"; Flags: ignoreversion isreadme

[Icons]
Name: "{group}\OpenMix"; Filename: "{app}\OpenMix.exe"
Name: "{group}\Uninstall OpenMix"; Filename: "{uninstallexe}"
Name: "{autodesktop}\OpenMix"; Filename: "{app}\OpenMix.exe"; Tasks: desktopicon

[Registry]
; Same value the app's own "Start when Windows starts" setting writes.
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; ValueName: "OpenMix"; \
  ValueData: """{app}\OpenMix.exe"" --minimized"; Tasks: startup; Flags: uninsdeletevalue

[Run]
Filename: "{app}\OpenMix.exe"; Description: "Open OpenMix now"; Flags: nowait postinstall skipifsilent

; App rerouting is undone by OpenMix itself (when a strip's source changes and when OpenMix
; quits), so it never clears per-app settings the user made in Windows.

[UninstallDelete]
Type: dirifempty; Name: "{app}"
