# Build on Windows 11 x64 with VS2022 C++, CMake 3.26+ and official OpenVINO C++ archive.
# Example:
# .\scripts\build_release.ps1 -OpenVINO 'D:\SDK\openvino_2026' -VCRedist 'D:\SDK\vc_redist.x64.exe' -MakeInstaller
param(
 [Parameter(Mandatory=$true)][string]$OpenVINO,
 [string]$VCRedist = '',
 [switch]$MakeInstaller,
 [string]$InnoSetupCompiler = ''
)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$ov = (Resolve-Path $OpenVINO).Path
$cmakeDir = Join-Path $ov 'runtime\cmake'
$runtimeBin = Join-Path $ov 'runtime\bin\intel64\Release'
if (!(Test-Path (Join-Path $cmakeDir 'OpenVINOConfig.cmake'))) { throw "OpenVINOConfig.cmake missing under $cmakeDir" }
if (!(Test-Path $runtimeBin)) { throw "Windows OpenVINO Runtime missing: $runtimeBin" }
$build=Join-Path $root 'build'
$dist=Join-Path $root 'dist\IntelNPUTest'
& cmake -S $root -B $build -G 'Visual Studio 17 2022' -A x64 "-DOpenVINO_DIR=$cmakeDir"
if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed' }
& cmake --build $build --config Release --parallel
if ($LASTEXITCODE -ne 0) { throw 'C++ compilation failed' }
New-Item -ItemType Directory -Force -Path $dist | Out-Null
foreach ($name in @('IntelNPUTest.exe','IntelNPUTestCLI.exe')) {
 $f=Join-Path (Join-Path $build 'Release') $name
 if (!(Test-Path $f)) { throw "Missing compiled EXE: $f" }
 Copy-Item $f $dist -Force
}
# Bundle official OpenVINO DLLs and transitive runtime thirdparty DLLs.
# All DLLs in Release are copied to avoid omissions from version-dependent NPU compiler modules.
Get-ChildItem $runtimeBin -Recurse -File -Filter '*.dll' | ForEach-Object {
 $dest=Join-Path $dist $_.Name
 if (!(Test-Path $dest)) { Copy-Item $_.FullName $dest }
}
$thirdparty=Join-Path $ov 'runtime\3rdparty'
if (Test-Path $thirdparty) {
 Get-ChildItem $thirdparty -Recurse -File -Filter '*.dll' | ForEach-Object {
  $dest=Join-Path $dist $_.Name
  if (!(Test-Path $dest)) { Copy-Item $_.FullName $dest }
 }
}
foreach ($name in @('openvino.dll','openvino_intel_npu_plugin.dll','openvino_intel_cpu_plugin.dll','openvino_ir_frontend.dll')) {
 if (!(Test-Path (Join-Path $dist $name))) { throw "Mandatory runtime dependency missing: $name" }
}
Get-ChildItem $runtimeBin -File -Filter 'plugins.xml' -ErrorAction SilentlyContinue | ForEach-Object { Copy-Item $_.FullName $dist -Force }
$pluginXml=Join-Path $dist 'plugins.xml'
if (!(Test-Path $pluginXml)) {
 # Portable fallback registration when the archive has no packaged plugins.xml.
 @'
<ie>
  <plugins>
    <plugin name="CPU" location="openvino_intel_cpu_plugin"/>
    <plugin name="NPU" location="openvino_intel_npu_plugin"/>
  </plugins>
</ie>
'@ | Set-Content -Path $pluginXml -Encoding UTF8
}
$licenseDir=Join-Path $dist 'licenses'
New-Item -ItemType Directory -Force -Path $licenseDir | Out-Null
foreach ($name in @('LICENSE','LICENSE.txt','LICENSE.md','third-party-programs.txt','Third-Party-Programs.txt')) {
 $f=Join-Path $ov $name
 if (Test-Path $f -PathType Leaf) { Copy-Item $f $licenseDir -Force }
}
foreach ($name in @('licensing','licenses','third-party-programs')) {
 $f=Join-Path $ov $name
 if (Test-Path $f -PathType Container) { Copy-Item $f (Join-Path $licenseDir $name) -Recurse -Force }
}
Copy-Item (Join-Path $root 'README_部署说明.md') $dist -Force
Write-Host "[OK] Portable directory: $dist"
if ($MakeInstaller) {
 if (!$VCRedist -or !(Test-Path $VCRedist -PathType Leaf)) {
  throw 'Offline installer requires -VCRedist path to official Microsoft vc_redist.x64.exe'
 }
 if (!$InnoSetupCompiler) {
  $possible=@(
   (Join-Path ${env:ProgramFiles(x86)} 'Inno Setup 6\ISCC.exe'),
   (Join-Path $env:ProgramFiles 'Inno Setup 6\ISCC.exe')
  )
  $available=@($possible | Where-Object { Test-Path $_ })
  if ($available.Count -gt 0) { $InnoSetupCompiler=$available[0] }
 }
 if (!$InnoSetupCompiler -or !(Test-Path $InnoSetupCompiler)) {
  throw 'Inno Setup 6 missing. Install on BUILD computer or supply -InnoSetupCompiler'
 }
 $iss=Join-Path $root 'installer\IntelNPUTest.iss'
 $redist=(Resolve-Path $VCRedist).Path
 & $InnoSetupCompiler "/DAppSource=$dist" "/DRedistributable=$redist" $iss
 if ($LASTEXITCODE -ne 0) { throw 'Inno Setup failed' }
 Write-Host "[OK] Offline setup: $(Join-Path $root 'dist\IntelNPUTest_Setup_v2.0.exe')"
}
