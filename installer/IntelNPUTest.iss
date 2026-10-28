#define MyAppName "Intel NPU Test Tool"
#define MyAppVersion "2.0"
#ifndef AppSource
  #error AppSource must point to the compiled application directory
#endif
#ifndef Redistributable
  #error Redistributable must point to official vc_redist.x64.exe
#endif

[Setup]
AppId={{A37015B9-A7D0-4AA7-A4E7-4EC811E07C26}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
DefaultDirName={autopf}\IntelNPUTest
DefaultGroupName=Intel NPU Test Tool
OutputDir=..\dist
OutputBaseFilename=IntelNPUTest_Setup_v2.0
Compression=lzma2
SolidCompression=yes
ArchitecturesAllowed=x64
ArchitecturesInstallIn64BitMode=x64
PrivilegesRequired=admin
WizardStyle=modern
UninstallDisplayIcon={app}\IntelNPUTest.exe

[Files]
Source: "{#AppSource}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#Redistributable}"; DestDir: "{tmp}"; DestName: "vc_redist.x64.exe"; Flags: deleteafterinstall

[Tasks]
Name: "desktopicon"; Description: "创建桌面快捷方式"; GroupDescription: "快捷方式："

[Icons]
Name: "{group}\Intel NPU 测试"; Filename: "{app}\IntelNPUTest.exe"
Name: "{autodesktop}\Intel NPU 测试"; Filename: "{app}\IntelNPUTest.exe"; Tasks: desktopicon

[Run]
Filename: "{tmp}\vc_redist.x64.exe"; Parameters: "/install /quiet /norestart"; Flags: waituntilterminated runhidden; StatusMsg: "安装 Microsoft Visual C++ 运行库..."; Check: not HasVCRedist
Filename: "{app}\IntelNPUTest.exe"; Description: "启动 Intel NPU 测试程序"; Flags: nowait postinstall skipifsilent

[Code]
function HasVCRedist: Boolean;
var
  Installed: Cardinal;
begin
  Result := RegQueryDWordValue(HKLM64,
    'SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64',
    'Installed', Installed) and (Installed = 1);
end;
