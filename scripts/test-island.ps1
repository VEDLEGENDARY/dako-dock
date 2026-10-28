$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (!(Test-Path -LiteralPath $vswhere)) { throw 'Visual Studio C++ Build Tools are required.' }
$vsRoot = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$vsRoot) { throw 'No installed Visual Studio x64 C++ toolchain was found.' }
if ($env:VSCMD_ARG_TGT_ARCH -notin @('x64','amd64')) {
    & (Join-Path $vsRoot 'Common7\Tools\Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation | Out-Null
}
$testDir = Join-Path $projectRoot 'build\tests\island'
New-Item -ItemType Directory -Path $testDir -Force | Out-Null
Push-Location $testDir
try {
    & cl.exe /nologo /std:c++20 /EHsc /W4 /WX /permissive- /utf-8 /O2 /MT "/I$(Join-Path $projectRoot 'src')" (Join-Path $projectRoot 'tests\IslandTests.cpp') /Fe:IslandTests.exe
    if ($LASTEXITCODE -ne 0) { throw 'Island/anchor test compilation failed.' }
    & .\IslandTests.exe
    if ($LASTEXITCODE -ne 0) { throw 'Island/anchor tests failed.' }
} finally { Pop-Location }
