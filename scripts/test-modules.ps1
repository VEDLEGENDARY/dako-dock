param([string]$OutputDirectory)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vsRoot = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if ($env:VSCMD_ARG_TGT_ARCH -notin @('x64','amd64')) {
    & (Join-Path $vsRoot 'Common7\Tools\Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation | Out-Null
}
$buildDir = Join-Path $projectRoot 'build\tests\modules'
New-Item -ItemType Directory -Force -Path $buildDir | Out-Null
$reports = if ($OutputDirectory) { [IO.Path]::GetFullPath($OutputDirectory) } else { $buildDir }
Push-Location $buildDir
try {
    & rc.exe /nologo /fo Dako.modules.res (Join-Path $projectRoot 'src\ModuleStrings.rc')
    if ($LASTEXITCODE -ne 0) { throw 'Module resource compilation failed.' }
    & cl.exe /nologo /std:c++20 /EHsc /W4 /WX /permissive- /utf-8 /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /DNOMINMAX /O2 /MT (Join-Path $projectRoot 'tests\ModuleTests.cpp') (Join-Path $projectRoot 'src\ModuleRegistry.cpp') (Join-Path $projectRoot 'src\WindowsShortcuts.cpp') (Join-Path $projectRoot 'src\UtilityModule.cpp') (Join-Path $projectRoot 'src\ClipboardModule.cpp') (Join-Path $projectRoot 'src\ClipboardVault.cpp') (Join-Path $projectRoot 'src\SnippetsModule.cpp') (Join-Path $projectRoot 'src\KeepAwakeModule.cpp') (Join-Path $projectRoot 'src\AwakeRules.cpp') (Join-Path $projectRoot 'src\FileSearchModule.cpp') (Join-Path $projectRoot 'src\ShelfModule.cpp') (Join-Path $projectRoot 'src\RecycleBinModule.cpp') (Join-Path $projectRoot 'src\DrivesModule.cpp') (Join-Path $projectRoot 'src\ShelfOle.cpp') (Join-Path $projectRoot 'src\ClipboardTransfer.cpp') (Join-Path $projectRoot 'src\ModuleCheck.cpp') Dako.modules.res /Fe:ModuleTests.exe /link onecoreuap.lib shlwapi.lib uuid.lib crypt32.lib user32.lib shell32.lib ole32.lib wtsapi32.lib psapi.lib
    if ($LASTEXITCODE -ne 0) { throw 'Module tests did not compile.' }
    & .\ModuleTests.exe $reports
    if ($LASTEXITCODE -ne 0) { throw 'Module lifecycle verification failed.' }
    Get-Content -LiteralPath (Join-Path $reports 'module-tests\module-check.txt')
    & cl.exe /nologo /std:c++20 /EHsc /W4 /WX /permissive- /utf-8 /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /DNOMINMAX /O2 /MT (Join-Path $projectRoot 'tests\PrivacyTests.cpp') /Fe:PrivacyTests.exe /link onecoreuap.lib shlwapi.lib uuid.lib crypt32.lib user32.lib shell32.lib ole32.lib wtsapi32.lib psapi.lib winhttp.lib
    if ($LASTEXITCODE -ne 0) { throw 'Privacy/settings tests did not compile.' }
    & .\PrivacyTests.exe $reports
    if ($LASTEXITCODE -ne 0) { throw 'Privacy/settings verification failed.' }
} finally { Pop-Location }
