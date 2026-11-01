$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
& (Join-Path $PSScriptRoot 'test-shape-lint.ps1')
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vsRoot = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if ($env:VSCMD_ARG_TGT_ARCH -notin @('x64','amd64')) {
    & (Join-Path $vsRoot 'Common7\Tools\Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation | Out-Null
}
$output = Join-Path $projectRoot 'build\tests\unified'
New-Item -ItemType Directory -Force $output | Out-Null
foreach ($name in @('ShapeTests','ShapeGeometryTests','SurfaceMotionTests','UnifiedRasterTests')) {
    & cl.exe /nologo /std:c++20 /EHsc /W4 /WX /permissive- /utf-8 /DUNICODE /D_UNICODE /DNOMINMAX /O2 /MT `
        (Join-Path $projectRoot "tests\$name.cpp") "/Fe:$output\$name.exe" "/Fo:$output\$name.obj" `
        /link d2d1.lib user32.lib gdi32.lib ole32.lib windowscodecs.lib
    if ($LASTEXITCODE -ne 0) { throw "$name compilation failed." }
    & (Join-Path $output "$name.exe")
    if ($LASTEXITCODE -ne 0) { throw "$name failed." }
}
