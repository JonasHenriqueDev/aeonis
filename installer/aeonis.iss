; Instalador do Aeonis (Inno Setup 6). Gerado por scripts\release.ps1, que define AppVersion e StageDir.
#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef StageDir
  #define StageDir "..\dist\stage"
#endif

[Setup]
AppId={{6E2B7C1A-5F3D-4C8E-9A41-AE0715A2B0C9}
AppName=Aeonis
AppVersion={#AppVersion}
AppVerName=Aeonis {#AppVersion}
AppPublisher=Aeonis contributors
AppPublisherURL=https://github.com/JonasHenriqueDev/aeonis
AppSupportURL=https://github.com/JonasHenriqueDev/aeonis/issues
AppUpdatesURL=https://github.com/JonasHenriqueDev/aeonis/releases
; Instalação por usuário: não pede administrador.
PrivilegesRequired=lowest
DefaultDirName={localappdata}\Programs\Aeonis
DisableProgramGroupPage=yes
DisableDirPage=auto
OutputDir=..\dist
OutputBaseFilename=Aeonis-Setup-{#AppVersion}
SetupIconFile=..\assets\aeonis.ico
UninstallDisplayIcon={app}\Aeonis.exe
UninstallDisplayName=Aeonis
LicenseFile=..\LICENSE
Compression=lzma2/ultra64
SolidCompression=yes
LZMAUseSeparateProcess=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
WizardStyle=modern
AppMutex=Aeonis.SingleInstance
CloseApplications=yes

[Languages]
Name: "ptbr"; MessagesFile: "compiler:Languages\BrazilianPortuguese.isl"
Name: "en"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"
Name: "autostart"; Description: "Iniciar o Aeonis junto com o Windows (recomendado)"; GroupDescription: "Inicialização:"

[Files]
Source: "{#StageDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\Aeonis"; Filename: "{app}\Aeonis.exe"
Name: "{autodesktop}\Aeonis"; Filename: "{app}\Aeonis.exe"; Tasks: desktopicon

[Registry]
; Mesma chave que a opção "Iniciar com o Windows" das configurações do app usa.
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; ValueName: "Aeonis"; \
  ValueData: """{app}\Aeonis.exe"" --minimized"; Tasks: autostart; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueName: "Aeonis"; \
  Flags: uninsdeletevalue dontcreatekey

[Run]
Filename: "{app}\Aeonis.exe"; Description: "{cm:LaunchProgram,Aeonis}"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
; Remove só caches/buffer; configurações e vídeos do usuário são preservados.
Type: filesandordirs; Name: "{localappdata}\Aeonis\buffer"
Type: filesandordirs; Name: "{localappdata}\Aeonis\thumbs"
