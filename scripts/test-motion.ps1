$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vsRoot = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
& (Join-Path $vsRoot 'Common7\Tools\Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation | Out-Null
$testOutput = Join-Path $projectRoot 'build\DockMotionTests.exe'
& cl.exe /nologo /std:c++20 /EHsc /W4 /WX /O2 /MT (Join-Path $projectRoot 'tests\DockMotionTests.cpp') "/Fe:$testOutput" "/Fo:$(Join-Path $projectRoot 'build\DockMotionTests.obj')"
if ($LASTEXITCODE -ne 0) { throw 'Dock motion test compilation failed.' }
& $testOutput
if ($LASTEXITCODE -ne 0) { throw 'Dock motion tests failed.' }
