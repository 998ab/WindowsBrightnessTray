#ifndef AppVersion
  #define AppVersion "0.1.0"
#endif
#ifndef BuildDir
  #define BuildDir "..\build\release"
#endif
#ifndef DistDir
  #define DistDir "..\build\dist"
#endif

[Setup]
AppId={{38D7D728-28B4-4404-9458-266942C8075C}
AppName=BrightnessTray
AppVersion={#AppVersion}
AppPublisher=998ab
AppPublisherURL=https://github.com/998ab/WindowsBrightnessTray
AppSupportURL=https://github.com/998ab/WindowsBrightnessTray/issues
AppUpdatesURL=https://github.com/998ab/WindowsBrightnessTray/releases
DefaultDirName={localappdata}\Programs\BrightnessTray
DefaultGroupName=BrightnessTray
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.17763
OutputDir={#DistDir}
OutputBaseFilename=BrightnessTray-{#AppVersion}-Setup-x64
SetupIconFile=..\assets\app.ico
UninstallDisplayIcon={app}\BrightnessTray.exe
LicenseFile=..\LICENSE
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
AppMutex=Local\BrightnessTray.Native.Singleton
CloseApplications=no
RestartApplications=no

[Languages]
Name: "en"; MessagesFile: "compiler:Default.isl"
Name: "ru"; MessagesFile: "compiler:Languages\Russian.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
Name: "startup"; Description: "{cm:StartupTask}"; Flags: unchecked

[CustomMessages]
en.StartupTask=Start BrightnessTray when I sign in to Windows
#include "..\locales\installer-ru.iss"

[Files]
Source: "{#BuildDir}\BrightnessTray.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\LICENSE"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\README.md"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\BrightnessTray"; Filename: "{app}\BrightnessTray.exe"
Name: "{autodesktop}\BrightnessTray"; Filename: "{app}\BrightnessTray.exe"; Tasks: desktopicon
Name: "{userstartup}\BrightnessTray"; Filename: "{app}\BrightnessTray.exe"; Tasks: startup

[Run]
Filename: "{app}\BrightnessTray.exe"; Description: "{cm:LaunchProgram,BrightnessTray}"; Flags: nowait postinstall skipifsilent
