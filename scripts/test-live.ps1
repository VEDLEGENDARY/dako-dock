param([string]$OutputDirectory,[switch]$Quick,[switch]$RecordOnly)
$ErrorActionPreference = 'Stop'
if ($Quick -and $RecordOnly) { throw 'Choose Quick or RecordOnly, not both.' }
$projectRoot = Split-Path -Parent $PSScriptRoot
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vsRoot = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
& (Join-Path $vsRoot 'Common7\Tools\Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation | Out-Null
$testOutput = Join-Path $projectRoot 'build\NativeLifecycleTests.exe'
$renderDir = if ($OutputDirectory) { [IO.Path]::GetFullPath($OutputDirectory) } else { Join-Path $projectRoot 'build\live-verification' }
Push-Location (Join-Path $projectRoot 'build')
try {
    & cl.exe /nologo /std:c++20 /EHsc /W4 /WX /permissive- /utf-8 /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /DNOMINMAX /O2 /MT (Join-Path $projectRoot 'tests\NativeLifecycleTests.cpp') (Join-Path $projectRoot 'src\AccessibilityGuids.cpp') /Fe:NativeLifecycleTests.exe /link d3d11.lib dcomp.lib user32.lib gdi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib d2d1.lib dwrite.lib dwmapi.lib wlanapi.lib bthprops.lib psapi.lib windowsapp.lib windowscodecs.lib oleacc.lib powrprof.lib advapi32.lib iphlpapi.lib shcore.lib pdh.lib dxgi.lib
    if ($LASTEXITCODE -ne 0) { throw 'Native lifecycle test compilation failed.' }
} finally { Pop-Location }
if ($RecordOnly) { & $testOutput $renderDir --record-only } elseif ($Quick) { & $testOutput $renderDir --quick } else { & $testOutput $renderDir }
if ($LASTEXITCODE -ne 0) { throw 'Native lifecycle verification failed.' }
Get-Content -LiteralPath (Join-Path $renderDir 'results.txt')
