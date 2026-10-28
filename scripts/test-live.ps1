param([string]$OutputDirectory,[switch]$Quick,[switch]$RecordOnly,[switch]$ReuseObjects,[switch]$UseProductionObjects)
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
$sources = @(& (Join-Path $PSScriptRoot 'sources.ps1') -ExcludeMain) + @(Join-Path $projectRoot 'src\SettingsCore.cpp')
$nativeLibraries = @('setupapi.lib', 'cfgmgr32.lib')
$testOutput = Join-Path $projectRoot 'build\tests\live\NativeLifecycleTests.exe'
$renderDir = if ($OutputDirectory) { [IO.Path]::GetFullPath($OutputDirectory) } else { Join-Path $projectRoot 'build\tests\live\reports' }
Push-Location (Join-Path $projectRoot 'build\tests\live')
try {
    & rc.exe /nologo /fo Dako.modules.res (Join-Path $projectRoot 'src\ModuleStrings.rc')
    if ($LASTEXITCODE -ne 0) { throw 'Module resource compilation failed.' }
    $allSources = @((Join-Path $projectRoot 'tests\NativeLifecycleTests.cpp')) + $sources
    $headers = Get-ChildItem -LiteralPath (Join-Path $projectRoot 'src') -Recurse -File -Filter '*.h'
    $headerTime = ($headers | Sort-Object LastWriteTimeUtc -Descending | Select-Object -First 1).LastWriteTimeUtc
    $compileSources = $allSources
    if ($UseProductionObjects) {
        $productionDir = Join-Path $projectRoot 'build\obj'
        if ((Get-Content -LiteralPath (Join-Path $productionDir 'compile-options.txt') -Raw) -ne '/O2 /MT /Zi /Gy /Gw') {
            throw 'Production-object verification requires the current release build.'
        }
        foreach ($source in $sources) {
            $object = Join-Path $productionDir ([IO.Path]::GetFileNameWithoutExtension($source) + '.obj')
            if (!(Test-Path -LiteralPath $object) -or
                (Get-Item -LiteralPath $object).LastWriteTimeUtc -lt (Get-Item -LiteralPath $source).LastWriteTimeUtc -or
                (Get-Item -LiteralPath $object).LastWriteTimeUtc -lt $headerTime) {
                throw "Missing or stale production object: $object. Run build.ps1 first."
            }
        }
        $compileSources = @((Join-Path $projectRoot 'tests\NativeLifecycleTests.cpp'))
        Write-Output 'Native lifecycle links the timestamp-validated production release objects.'
    }
    $changed = @($compileSources | Where-Object {
        $object = Join-Path (Get-Location) ([IO.Path]::GetFileNameWithoutExtension($_) + '.obj')
        !$ReuseObjects -or !(Test-Path -LiteralPath $object) -or
            (Get-Item -LiteralPath $object).LastWriteTimeUtc -lt (Get-Item -LiteralPath $_).LastWriteTimeUtc -or
            (Get-Item -LiteralPath $object).LastWriteTimeUtc -lt $headerTime -or
            (Get-Item -LiteralPath $object).LastWriteTimeUtc -lt (Get-Item -LiteralPath $PSCommandPath).LastWriteTimeUtc
    })
    if ($changed.Count) {
        & cl.exe /nologo /c /std:c++20 /EHsc /W4 /WX /permissive- /utf-8 /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /DNOMINMAX /O2 /MT /MP2 @changed
        if ($LASTEXITCODE -ne 0) { throw 'Native lifecycle compilation failed.' }
    }
    $objects = if ($UseProductionObjects) {
        @('NativeLifecycleTests.obj') + @($sources | ForEach-Object {
            Join-Path $productionDir ([IO.Path]::GetFileNameWithoutExtension($_) + '.obj')
        })
    } else { @($allSources | ForEach-Object { [IO.Path]::GetFileNameWithoutExtension($_) + '.obj' }) }
    & link.exe /nologo /OUT:NativeLifecycleTests.exe @objects Dako.modules.res @nativeLibraries onecoreuap.lib shlwapi.lib uuid.lib crypt32.lib d3d11.lib dcomp.lib user32.lib gdi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib d2d1.lib dwrite.lib dwmapi.lib wlanapi.lib bthprops.lib psapi.lib windowsapp.lib windowscodecs.lib oleacc.lib powrprof.lib advapi32.lib iphlpapi.lib shcore.lib pdh.lib dxgi.lib winhttp.lib comctl32.lib dxva2.lib wbemuuid.lib wtsapi32.lib
    if ($LASTEXITCODE -ne 0) { throw 'Native lifecycle test compilation failed.' }
} finally { Pop-Location }
if ($RecordOnly) { & $testOutput $renderDir --record-only } elseif ($Quick) { & $testOutput $renderDir --quick } else { & $testOutput $renderDir }
if ($LASTEXITCODE -ne 0) { throw 'Native lifecycle verification failed.' }
Get-Content -LiteralPath (Join-Path $renderDir 'results.txt')

Push-Location (Join-Path $projectRoot 'build\tests\live')
try {
    & cl.exe /nologo /std:c++20 /EHsc /W4 /WX /permissive- /utf-8 /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /DNOMINMAX /O2 /MT (Join-Path $projectRoot 'tests\NativeControlsReadTests.cpp') (Join-Path $projectRoot 'src\ControlsService.cpp') (Join-Path $projectRoot 'src\SystemEffects.cpp') /Fe:NativeControlsReadTests.exe /link @nativeLibraries onecoreuap.lib shlwapi.lib uuid.lib crypt32.lib user32.lib gdi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib dwmapi.lib windowsapp.lib dxva2.lib wbemuuid.lib wtsapi32.lib psapi.lib dxgi.lib pdh.lib iphlpapi.lib advapi32.lib
    if ($LASTEXITCODE -ne 0) { throw 'Read-only control tests did not compile.' }
    & .\NativeControlsReadTests.exe $renderDir
    if ($LASTEXITCODE -ne 0) { throw 'Read-only control verification failed.' }
} finally { Pop-Location }
