$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$output = Join-Path $projectRoot 'build\tests\unified-live'
New-Item -ItemType Directory -Force $output | Out-Null
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vsRoot = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if ($env:VSCMD_ARG_TGT_ARCH -notin @('x64','amd64')) {
    & (Join-Path $vsRoot 'Common7\Tools\Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation | Out-Null
}
$objects = @(& (Join-Path $PSScriptRoot 'sources.ps1') -ExcludeMain | ForEach-Object {
    Join-Path $projectRoot ('build\obj\' + [IO.Path]::GetFileNameWithoutExtension($_) + '.obj')
})
$testObject = Join-Path $output 'UnifiedNavigationTests.obj'
$testExecutable = Join-Path $projectRoot 'build\Dako.UnifiedTests.exe'
& cl.exe /nologo /std:c++20 /EHsc /W4 /WX /permissive- /utf-8 /DUNICODE /D_UNICODE /DNOMINMAX /O2 /MT /c `
    (Join-Path $projectRoot 'tests\UnifiedNavigationTests.cpp') "/Fo:$testObject"
if ($LASTEXITCODE -ne 0) { throw 'Unified navigation test compilation failed.' }
Push-Location (Join-Path $projectRoot 'build\obj')
try {
    & link.exe /nologo $testObject @objects Dako.version.res Dako.modules.res dako_core.lib "/OUT:$testExecutable" `
        /SUBSYSTEM:CONSOLE onecoreuap.lib setupapi.lib cfgmgr32.lib shlwapi.lib uuid.lib crypt32.lib `
        d3d11.lib dcomp.lib user32.lib gdi32.lib shell32.lib ole32.lib oleaut32.lib d2d1.lib dwrite.lib dwmapi.lib `
        wlanapi.lib bthprops.lib psapi.lib windowsapp.lib windowscodecs.lib oleacc.lib powrprof.lib advapi32.lib `
        iphlpapi.lib shcore.lib pdh.lib dxgi.lib winhttp.lib comctl32.lib dxva2.lib wbemuuid.lib wtsapi32.lib
    if ($LASTEXITCODE -ne 0) { throw 'Unified navigation test link failed. Build the application first.' }
} finally { Pop-Location }
$test = Start-Process -FilePath $testExecutable -WindowStyle Hidden -Wait -PassThru `
    -RedirectStandardOutput (Join-Path $output 'navigation.txt') `
    -RedirectStandardError (Join-Path $output 'navigation-errors.txt')
Get-Content (Join-Path $output 'navigation.txt')
if ($test.ExitCode -ne 0) {
    Get-Content (Join-Path $output 'navigation-errors.txt')
    throw "Unified navigation test failed: $($test.ExitCode)."
}
