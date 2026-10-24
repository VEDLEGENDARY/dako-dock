$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vsRoot = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
& (Join-Path $vsRoot 'Common7\Tools\Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation | Out-Null
Push-Location (Join-Path $projectRoot 'build')
try {
    & cl.exe /nologo /std:c++20 /EHsc /W4 /WX /permissive- /utf-8 /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /DNOMINMAX /O2 /MT (Join-Path $projectRoot 'tests\NativeShellTests.cpp') /Fe:NativeShellTests.exe /link user32.lib gdi32.lib shell32.lib shcore.lib dwmapi.lib
    if ($LASTEXITCODE -ne 0) { throw 'Shell tests did not compile.' }
    & .\NativeShellTests.exe
    if ($LASTEXITCODE -ne 0) { throw "Shell integration tests failed: $LASTEXITCODE" }
} finally { Pop-Location }
