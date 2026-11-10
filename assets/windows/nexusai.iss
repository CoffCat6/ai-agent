; NexusAI Windows 安装包（Inno Setup 6，docs/49 发布矩阵）。
;   ISCC /DMyAppVersion=0.2.1 /DDeployDir=<绝对路径> /DOutputDir=<绝对路径> nexusai.iss
; DeployDir 指向 deploy.ps1 产出的自包含目录（bin/ plugins/ qml/ translations/）。
; 路径默认值按本脚本位置（assets/windows/）相对解析，CI 传绝对路径覆盖。
; AppId 是升级识别锚点——变更即被视为不同程序，必须保持不变。

#ifndef MyAppVersion
#define MyAppVersion "0.0.0"
#endif
#ifndef DeployDir
#define DeployDir "..\..\deploy-release"
#endif
#ifndef OutputDir
#define OutputDir "..\.."
#endif

#define MyAppName "NexusAI"
#define MyAppExeName "nexus_ai.exe"

[Setup]
AppId={{7E4D9B6C-3A21-4F5E-8C7D-1B2A3C4D5E60}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} {#MyAppVersion}
UninstallDisplayName={#MyAppName} {#MyAppVersion}
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
OutputDir={#OutputDir}
OutputBaseFilename=NexusAI-{#MyAppVersion}-setup
SetupIconFile=..\icons\nexus.ai.ico
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
PrivilegesRequired=admin
ArchitecturesInstallIn64BitMode=x64

[Languages]
Name: "chinese"; MessagesFile: "compiler:Languages\ChineseSimplified.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"

[Files]
Source: "{#DeployDir}\*"; DestDir: "{app}"; Flags: recursesubdirs createallsubdirs ignoreversion

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\bin\{#MyAppExeName}"
Name: "{group}\卸载 {#MyAppName}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\bin\{#MyAppExeName}"; Tasks: desktopicon

[Run]
Filename: "{app}\bin\{#MyAppExeName}"; Description: "启动 {#MyAppName}"; Flags: nowait postinstall skipifsilent
