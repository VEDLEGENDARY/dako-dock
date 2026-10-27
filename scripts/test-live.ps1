param([string]$OutputDirectory,[switch]$Quick,[switch]$RecordOnly)
$ErrorActionPreference = 'Stop'
if ($Quick -and $RecordOnly) { throw 'Choose Quick or RecordOnly, not both.' }
$projectRoot = Split-Path -Parent $PSScriptRoot
& (Join-Path $PSScriptRoot 'test-modules.ps1') -OutputDirectory $OutputDirectory
$executable = Join-Path $projectRoot 'build\Dako.exe'
if (!(Test-Path -LiteralPath $executable)) { throw 'Build Dako before live verification.' }
$moduleCheck = Start-Process -FilePath $executable -ArgumentList '--module-check' -WindowStyle Hidden -Wait -PassThru
if ($moduleCheck.ExitCode -ne 0) { throw 'Production --module-check failed. See build\module-check.txt.' }
$helperCheck = Start-Process -FilePath $executable -ArgumentList '--clipboard-transfer-check' -WindowStyle Hidden -Wait -PassThru
if ($helperCheck.ExitCode -ne 0) { throw 'Production clipboard helper check failed. See build\clipboard-helper-check.txt.' }
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vsRoot = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if ($env:VSCMD_ARG_TGT_ARCH -notin @('x64','amd64')) {
    & (Join-Path $vsRoot 'Common7\Tools\Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation | Out-Null
}
New-Item -ItemType Directory -Force -Path (Join-Path $projectRoot 'build\tests\live') | Out-Null
Copy-Item -LiteralPath (Join-Path $projectRoot 'build\assets') -Destination (Join-Path $projectRoot 'build\tests\live') -Recurse -Force
$testOutput = Join-Path $projectRoot 'build\tests\live\NativeLifecycleTests.exe'
$renderDir = if ($OutputDirectory) { [IO.Path]::GetFullPath($OutputDirectory) } else { Join-Path $projectRoot 'build\tests\live\reports' }
Push-Location (Join-Path $projectRoot 'build\tests\live')
try {
    & rc.exe /nologo /fo Dako.modules.res (Join-Path $projectRoot 'src\ModuleStrings.rc')
    if ($LASTEXITCODE -ne 0) { throw 'Module resource compilation failed.' }
    & cl.exe /nologo /std:c++20 /EHsc /W4 /WX /permissive- /utf-8 /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /DNOMINMAX /O2 /MT (Join-Path $projectRoot 'tests\NativeLifecycleTests.cpp') (Join-Path $projectRoot 'src\ModuleRegistry.cpp') (Join-Path $projectRoot 'src\WindowsShortcuts.cpp') (Join-Path $projectRoot 'src\UtilityModule.cpp') (Join-Path $projectRoot 'src\ClipboardModule.cpp') (Join-Path $projectRoot 'src\ClipboardVault.cpp') (Join-Path $projectRoot 'src\SnippetsModule.cpp') (Join-Path $projectRoot 'src\KeepAwakeModule.cpp') (Join-Path $projectRoot 'src\AwakeRules.cpp') (Join-Path $projectRoot 'src\FileSearchModule.cpp') (Join-Path $projectRoot 'src\ShelfModule.cpp') (Join-Path $projectRoot 'src\RecycleBinModule.cpp') (Join-Path $projectRoot 'src\DrivesModule.cpp') (Join-Path $projectRoot 'src\ShelfOle.cpp') (Join-Path $projectRoot 'src\ClipboardTransfer.cpp') Dako.modules.res (Join-Path $projectRoot 'src\ControlsService.cpp') (Join-Path $projectRoot 'src\SystemEffects.cpp') (Join-Path $projectRoot 'src\ParityServices.cpp') (Join-Path $projectRoot 'src\AccessibilityGuids.cpp') (Join-Path $projectRoot 'src\WindowList.cpp') (Join-Path $projectRoot 'src\DockShell.cpp') /Fe:NativeLifecycleTests.exe /link onecoreuap.lib shlwapi.lib uuid.lib crypt32.lib d3d11.lib dcomp.lib user32.lib gdi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib d2d1.lib dwrite.lib dwmapi.lib wlanapi.lib bthprops.lib psapi.lib windowsapp.lib windowscodecs.lib oleacc.lib powrprof.lib advapi32.lib iphlpapi.lib shcore.lib pdh.lib dxgi.lib winhttp.lib comctl32.lib dxva2.lib wbemuuid.lib wtsapi32.lib
    if ($LASTEXITCODE -ne 0) { throw 'Native lifecycle test compilation failed.' }
} finally { Pop-Location }
if ($RecordOnly) { & $testOutput $renderDir --record-only } elseif ($Quick) { & $testOutput $renderDir --quick } else { & $testOutput $renderDir }
if ($LASTEXITCODE -ne 0) { throw 'Native lifecycle verification failed.' }
Get-Content -LiteralPath (Join-Path $renderDir 'results.txt')

Push-Location (Join-Path $projectRoot 'build\tests\live')
try {
    & cl.exe /nologo /std:c++20 /EHsc /W4 /WX /permissive- /utf-8 /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /DNOMINMAX /O2 /MT (Join-Path $projectRoot 'tests\NativeControlsReadTests.cpp') (Join-Path $projectRoot 'src\ControlsService.cpp') (Join-Path $projectRoot 'src\SystemEffects.cpp') /Fe:NativeControlsReadTests.exe /link onecoreuap.lib shlwapi.lib uuid.lib crypt32.lib user32.lib gdi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib dwmapi.lib windowsapp.lib dxva2.lib wbemuuid.lib wtsapi32.lib psapi.lib dxgi.lib pdh.lib iphlpapi.lib advapi32.lib
    if ($LASTEXITCODE -ne 0) { throw 'Read-only control tests did not compile.' }
    & .\NativeControlsReadTests.exe $renderDir
    if ($LASTEXITCODE -ne 0) { throw 'Read-only control verification failed.' }
} finally { Pop-Location }
