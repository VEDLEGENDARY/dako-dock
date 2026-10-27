$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
New-Item -ItemType Directory -Force -Path (Join-Path $projectRoot 'build\tests\motion') | Out-Null
& (Join-Path $PSScriptRoot 'test-modules.ps1')
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vsRoot = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if ($env:VSCMD_ARG_TGT_ARCH -notin @('x64','amd64')) {
    & (Join-Path $vsRoot 'Common7\Tools\Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation | Out-Null
}
$testOutput = Join-Path $projectRoot 'build\tests\motion\DockMotionTests.exe'
& cl.exe /nologo /std:c++20 /EHsc /W4 /WX /O2 /MT (Join-Path $projectRoot 'tests\DockMotionTests.cpp') "/Fe:$testOutput" "/Fo:$(Join-Path $projectRoot 'build\tests\motion\DockMotionTests.obj')"
if ($LASTEXITCODE -ne 0) { throw 'Dock motion test compilation failed.' }
& $testOutput
if ($LASTEXITCODE -ne 0) { throw 'Dock motion tests failed.' }

$frameOutput = Join-Path $projectRoot 'build\tests\motion\FrameClockTests.exe'
& cl.exe /nologo /std:c++20 /EHsc /W4 /WX /O2 /MT (Join-Path $projectRoot 'tests\FrameClockTests.cpp') "/Fe:$frameOutput" "/Fo:$(Join-Path $projectRoot 'build\tests\motion\FrameClockTests.obj')" /link user32.lib
if ($LASTEXITCODE -ne 0) { throw 'Frame readiness test compilation failed.' }
& $frameOutput
if ($LASTEXITCODE -ne 0) { throw 'Frame readiness tests failed.' }

$asyncOutput = Join-Path $projectRoot 'build\tests\motion\NativeAsyncTests.exe'
& cl.exe /nologo /std:c++20 /EHsc /W4 /WX /O2 /MT (Join-Path $projectRoot 'tests\NativeAsyncTests.cpp') "/Fe:$asyncOutput" "/Fo:$(Join-Path $projectRoot 'build\tests\motion\NativeAsyncTests.obj')" /link windowsapp.lib ole32.lib
if ($LASTEXITCODE -ne 0) { throw 'Native async test compilation failed.' }
& $asyncOutput
if ($LASTEXITCODE -ne 0) { throw 'Native async tests failed.' }

$foundationOutput = Join-Path $projectRoot 'build\tests\motion\FoundationTests.exe'
Push-Location (Join-Path $projectRoot 'build\tests\motion')
try {
    & cl.exe /nologo /std:c++20 /EHsc /W4 /WX /permissive- /utf-8 /DUNICODE /D_UNICODE /DNOMINMAX /O2 /MT (Join-Path $projectRoot 'tests\FoundationTests.cpp') /Fe:FoundationTests.exe /link user32.lib shell32.lib ole32.lib advapi32.lib winhttp.lib windowsapp.lib
    if ($LASTEXITCODE -ne 0) { throw 'Foundation test compilation failed.' }
    & $foundationOutput
    if ($LASTEXITCODE -ne 0) { throw 'Foundation tests failed.' }
} finally { Pop-Location }

$milestoneOutput = Join-Path $projectRoot 'build\tests\motion\Milestone32Tests.exe'
& cl.exe /nologo /std:c++20 /EHsc /W4 /WX /permissive- /utf-8 /DUNICODE /D_UNICODE /DNOMINMAX /O2 /MT (Join-Path $projectRoot 'tests\Milestone32Tests.cpp') "/Fe:$milestoneOutput" "/Fo:$(Join-Path $projectRoot 'build\tests\motion\Milestone32Tests.obj')" /link windowsapp.lib ole32.lib
if ($LASTEXITCODE -ne 0) { throw 'Milestone 3.2 tests did not compile.' }
& $milestoneOutput
if ($LASTEXITCODE -ne 0) { throw 'Milestone 3.2 tests failed.' }
