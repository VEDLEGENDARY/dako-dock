$ErrorActionPreference='Stop'
$projectRoot=Split-Path -Parent $PSScriptRoot
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vsRoot=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if($env:VSCMD_ARG_TGT_ARCH -notin @('x64','amd64')){ & (Join-Path $vsRoot 'Common7\Tools\Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation | Out-Null }
$output=Join-Path $projectRoot 'build\tests\v4'
New-Item -ItemType Directory -Path $output -Force | Out-Null
Push-Location $output
try {
    & cl.exe /nologo /std:c++20 /EHsc /W4 /WX /permissive- /utf-8 /O2 /MT (Join-Path $projectRoot 'tests\ClipboardPasteTests.cpp') /Fe:ClipboardPasteTests.exe /link user32.lib advapi32.lib
    if($LASTEXITCODE -ne 0){throw 'Paste target tests compilation failed.'}
    & .\ClipboardPasteTests.exe
    if($LASTEXITCODE -ne 0){throw 'Paste target tests failed.'}
    & cl.exe /nologo /std:c++20 /EHsc /W4 /WX /permissive- /utf-8 /O2 /MT (Join-Path $projectRoot 'tests\IslandStateTests.cpp') /Fe:IslandStateTests.exe
    if($LASTEXITCODE -ne 0){throw 'Island state matrix compilation failed.'}
    & .\IslandStateTests.exe
    if($LASTEXITCODE -ne 0){throw 'Island state matrix failed.'}
    & cl.exe /nologo /std:c++20 /EHsc /W4 /WX /permissive- /utf-8 /DUNICODE /D_UNICODE /DNOMINMAX /O2 /MT (Join-Path $projectRoot 'tests\V4SettingsTests.cpp') (Join-Path $projectRoot 'src\SettingsCore.cpp') /Fe:V4SettingsTests.exe /link user32.lib shell32.lib ole32.lib advapi32.lib
    if($LASTEXITCODE -ne 0){throw 'V4 settings tests compilation failed.'}
    & .\V4SettingsTests.exe (Join-Path $output ([guid]::NewGuid().ToString('N')))
    if($LASTEXITCODE -ne 0){throw 'V4 settings tests failed.'}
    & cl.exe /nologo /std:c++20 /EHsc /W4 /WX /permissive- /utf-8 /DUNICODE /D_UNICODE /DNOMINMAX /O2 /MT (Join-Path $projectRoot 'tests\V4IpcTests.cpp') (Join-Path $projectRoot 'src\SettingsCore.cpp') /Fe:V4IpcTests.exe /link user32.lib shell32.lib ole32.lib advapi32.lib
    if($LASTEXITCODE -ne 0){throw 'V4 IPC tests compilation failed.'}
    & .\V4IpcTests.exe
    if($LASTEXITCODE -ne 0){throw 'V4 IPC tests failed.'}
    & cl.exe /nologo /std:c++20 /EHsc /W4 /WX /permissive- /utf-8 /DUNICODE /D_UNICODE /DNOMINMAX /O2 /MT (Join-Path $projectRoot 'tests\V4SettingsUiTests.cpp') (Join-Path $projectRoot 'src\SettingsCore.cpp') /Fe:V4SettingsUiTests.exe /link user32.lib shell32.lib ole32.lib oleaut32.lib oleacc.lib advapi32.lib uuid.lib
    if($LASTEXITCODE -ne 0){throw 'V4 settings GUI tests compilation failed.'}
    & .\V4SettingsUiTests.exe (Join-Path $projectRoot 'build\DakoSettings.exe') (Join-Path $output ([guid]::NewGuid().ToString('N')))
    if($LASTEXITCODE -ne 0){throw 'V4 settings GUI tests failed.'}
} finally {Pop-Location}
