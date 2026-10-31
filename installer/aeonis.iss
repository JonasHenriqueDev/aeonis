; Instalador do Aeonis (Inno Setup 6). Gerado por scripts\package.ps1, que define AppVersion e StageDir.
; Mesmo AppId da versão C#: instalar por cima atualiza no lugar e mantém configurações, histórico e vídeos.
#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef StageDir
  #define StageDir "..\dist\Aeonis"
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
VersionInfoVersion={#AppVersion}
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
MinVersion=10.0.19041
WizardStyle=modern
AppMutex=Aeonis.SingleInstance
; O app não abre enquanto este mutex existe (exceto o relançamento com --after-update).
SetupMutex=AeonisSetupMutex
CloseApplications=yes
; Avisa o Explorer ao terminar, que recarrega os ícones em cache (os da versão antiga).
ChangesAssociations=yes

[Languages]
Name: "ptbr"; MessagesFile: "compiler:Languages\BrazilianPortuguese.isl"
Name: "en"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"
Name: "autostart"; Description: "Iniciar o Aeonis junto com o Windows (recomendado)"; GroupDescription: "Inicialização:"

[InstallDelete]
; Restos da versão C# (.NET + ffmpeg.exe): tudo que não faz parte desta versão.
Type: files; Name: "{app}\ffmpeg.exe"
Type: files; Name: "{app}\FFMPEG-*.txt"
Type: files; Name: "{app}\Aeonis.dll"
Type: files; Name: "{app}\Aeonis.deps.json"
Type: files; Name: "{app}\Aeonis.runtimeconfig.json"
Type: files; Name: "{app}\createdump.exe"
; Sem "{app}\*.dll": se a instalação for interrompida depois de apagar e antes de copiar, o app não abre
; mais (aconteceu no loop de atualização da 1.1.0). As DLLs do Qt/MSVC são sobrescritas pelo [Files].
Type: filesandordirs; Name: "{app}\qml"
; Marcador de instalação completa: some aqui e volta no [INI], depois de todos os arquivos.
Type: files; Name: "{app}\install.ini"

[Files]
Source: "{#StageDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[INI]
; Sem ele, o app sabe que a instalação parou no meio e se repara (AutoUpdater::installIncomplete).
Filename: "{app}\install.ini"; Section: "Install"; Key: "Version"; String: "{#AppVersion}"

[Icons]
; AppUserModelID igual ao do processo (main.cpp): a janela agrupa com o atalho fixado e usa o ícone novo.
Name: "{autoprograms}\Aeonis"; Filename: "{app}\Aeonis.exe"; IconFilename: "{app}\Aeonis.exe"; AppUserModelID: "Aeonis.Aeonis"
Name: "{autodesktop}\Aeonis"; Filename: "{app}\Aeonis.exe"; IconFilename: "{app}\Aeonis.exe"; AppUserModelID: "Aeonis.Aeonis"; Tasks: desktopicon

[Registry]
; Mesma chave que a opção "Iniciar com o Windows" das configurações do app usa.
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; ValueName: "Aeonis"; \
  ValueData: """{app}\Aeonis.exe"" --minimized"; Tasks: autostart; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueName: "Aeonis"; \
  Flags: uninsdeletevalue dontcreatekey

[Run]
Filename: "{app}\Aeonis.exe"; Parameters: "--after-update"; Description: "{cm:LaunchProgram,Aeonis}"; Flags: nowait postinstall skipifsilent
; Atualização automática (instalador silencioso com /UPDATE=1): reabre com a janela se ela estava
; aberta (/OPENWINDOW=1), senão só na bandeja.
Filename: "{app}\Aeonis.exe"; Parameters: "--after-update {code:AfterUpdateArgs}"; Flags: nowait runasoriginaluser; Check: IsUpdate

[UninstallDelete]
Type: files; Name: "{app}\install.ini"
; Remove só caches/buffer; configurações e vídeos do usuário são preservados.
Type: filesandordirs; Name: "{localappdata}\Aeonis\buffer"
Type: filesandordirs; Name: "{localappdata}\Aeonis\thumbs"
Type: filesandordirs; Name: "{localappdata}\Aeonis\webview"

[Code]
function IsUpdate(): Boolean;
begin
  Result := ExpandConstant('{param:UPDATE|0}') = '1';
end;

function AfterUpdateArgs(Param: String): String;
begin
  if ExpandConstant('{param:OPENWINDOW|0}') = '1' then
    Result := ''
  else
    Result := '--minimized';
end;
