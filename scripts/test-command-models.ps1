$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vsRoot = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if ($env:VSCMD_ARG_TGT_ARCH -notin @('x64','amd64')) {
    & (Join-Path $vsRoot 'Common7\Tools\Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation | Out-Null
}
$buildDir = Join-Path $projectRoot 'build\tests\commands'
New-Item -ItemType Directory -Force -Path $buildDir | Out-Null
Push-Location $buildDir
try {
    & cl.exe /nologo /std:c++20 /EHsc /W4 /WX /permissive- /utf-8 /O2 /MT (Join-Path $projectRoot 'tests\CommandModelTests.cpp') /Fe:CommandModelTests.exe
    if ($LASTEXITCODE -ne 0) { throw 'Command model tests did not compile.' }
    & .\CommandModelTests.exe
    if ($LASTEXITCODE -ne 0) { throw 'Command model verification failed.' }
} finally { Pop-Location }
