param([string]$OutputDirectory)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$output = if ($OutputDirectory) { [IO.Path]::GetFullPath($OutputDirectory) } else {
    Join-Path $projectRoot 'build\tests\unified-live'
}
New-Item -ItemType Directory -Force $output | Out-Null
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vsRoot = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if ($env:VSCMD_ARG_TGT_ARCH -notin @('x64','amd64')) {
    & (Join-Path $vsRoot 'Common7\Tools\Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation | Out-Null
}
$name = 'ThumbnailCompositionTests'
& cl.exe /nologo /std:c++20 /EHsc /W4 /WX /permissive- /utf-8 /DNOMINMAX /O2 /MT `
    (Join-Path $projectRoot "tests\$name.cpp") "/Fo:$output\$name.obj" "/Fe:$output\$name.exe" `
    /link windowsapp.lib d3d11.lib dcomp.lib d2d1.lib dxgi.lib dwmapi.lib user32.lib gdi32.lib ole32.lib
if ($LASTEXITCODE -ne 0) { throw 'Thumbnail composition test compilation failed.' }
$test = Start-Process -FilePath (Join-Path $output "$name.exe") -WindowStyle Hidden -Wait -PassThru `
    -RedirectStandardOutput (Join-Path $output 'thumbnail-composition.txt') `
    -RedirectStandardError (Join-Path $output 'thumbnail-errors.txt')
Get-Content (Join-Path $output 'thumbnail-composition.txt')
if ($test.ExitCode -ne 0) {
    Get-Content (Join-Path $output 'thumbnail-errors.txt')
    throw "Thumbnail composition failed: $($test.ExitCode)."
}
