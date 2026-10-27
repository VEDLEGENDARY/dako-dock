param([Alias("BarOnly")][switch]$DockOnly)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
New-Item -ItemType Directory -Force -Path (Join-Path $projectRoot 'build\tests\shell') | Out-Null
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vsRoot = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if ($env:VSCMD_ARG_TGT_ARCH -notin @('x64','amd64')) {
    & (Join-Path $vsRoot 'Common7\Tools\Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation | Out-Null
}
Push-Location (Join-Path $projectRoot 'build\tests\shell')
try {
    & cl.exe /nologo /std:c++20 /EHsc /W4 /WX /permissive- /utf-8 /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /DNOMINMAX /O2 /MT (Join-Path $projectRoot 'tests\NativeEffectsTests.cpp') /Fe:NativeEffectsTests.exe /link user32.lib gdi32.lib shell32.lib shcore.lib dwmapi.lib advapi32.lib ole32.lib
    if ($LASTEXITCODE -ne 0) { throw 'Recovery journal tests did not compile.' }
    & .\NativeEffectsTests.exe
    if ($LASTEXITCODE -ne 0) { throw 'Recovery journal tests failed.' }
    if (!$DockOnly) {
    & cl.exe /nologo /std:c++20 /EHsc /W4 /WX /permissive- /utf-8 /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /DNOMINMAX /O2 /MT (Join-Path $projectRoot 'tests\NativeShellTests.cpp') /Fe:NativeShellTests.exe /link user32.lib gdi32.lib shell32.lib shcore.lib dwmapi.lib
    if ($LASTEXITCODE -ne 0) { throw 'Shell tests did not compile.' }
    & .\NativeShellTests.exe
    if ($LASTEXITCODE -ne 0) { throw "Shell integration tests failed: $LASTEXITCODE" }
    }
    & cl.exe /nologo /std:c++20 /EHsc /W4 /WX /permissive- /utf-8 /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /DNOMINMAX /O2 /MT /Zi /Fd:NativeDockTests.compiler.pdb (Join-Path $projectRoot 'tests\NativeDockTests.cpp') (Join-Path $projectRoot 'src\WindowList.cpp') (Join-Path $projectRoot 'src\DockShell.cpp') (Join-Path $projectRoot 'src\AccessibilityGuids.cpp') /Fe:NativeDockTests.exe /link /DEBUG /PDB:NativeDockTests.pdb user32.lib gdi32.lib shell32.lib shcore.lib dwmapi.lib d2d1.lib dwrite.lib windowscodecs.lib ole32.lib oleaut32.lib uuid.lib oleacc.lib comctl32.lib advapi32.lib dbghelp.lib d3d11.lib dcomp.lib dxgi.lib
    if ($LASTEXITCODE -ne 0) { throw 'Bar tests did not compile.' }
    & .\NativeDockTests.exe
    if ($LASTEXITCODE -ne 0) { throw "Bar integration tests failed: $LASTEXITCODE" }
} finally { Pop-Location }
