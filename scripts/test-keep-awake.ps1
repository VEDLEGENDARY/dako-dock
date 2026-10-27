param([switch]$BuildOnly)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vsRoot = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if ($env:VSCMD_ARG_TGT_ARCH -notin @('x64','amd64')) {
    & (Join-Path $vsRoot 'Common7\Tools\Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation | Out-Null
}
$buildDir = Join-Path $projectRoot 'build\tests\power'
New-Item -ItemType Directory -Force -Path $buildDir | Out-Null
Push-Location $buildDir
try {
    & rc.exe /nologo /fo Dako.modules.res (Join-Path $projectRoot 'src\ModuleStrings.rc')
    if ($LASTEXITCODE -ne 0) { throw 'Module resources failed.' }
    & cl.exe /nologo /std:c++20 /EHsc /W4 /WX /permissive- /utf-8 /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /DNOMINMAX /O2 /MT (Join-Path $projectRoot 'tests\NativeKeepAwakeTests.cpp') (Join-Path $projectRoot 'src\KeepAwakeModule.cpp') (Join-Path $projectRoot 'src\AwakeRules.cpp') (Join-Path $projectRoot 'src\ModuleRegistry.cpp') Dako.modules.res /Fe:NativeKeepAwakeTests.exe /link user32.lib shell32.lib ole32.lib wtsapi32.lib
    if ($LASTEXITCODE -ne 0) { throw 'Native power tests did not compile.' }
    Copy-Item -LiteralPath .\NativeKeepAwakeTests.exe -Destination .\AwakeRuleEventSource.exe -Force
    if (!$BuildOnly) {
        & .\NativeKeepAwakeTests.exe
        if ($LASTEXITCODE -ne 0) { throw 'Native power request restoration failed.' }
    }
} finally { Pop-Location }
