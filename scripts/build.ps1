param([ValidatePattern('^\d+\.\d+$')][string]$Version='4.0',[switch]$Debug)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$resourceVersion = Select-String -LiteralPath (Join-Path $projectRoot 'src\version.rc') -Pattern '"ProductVersion", "([0-9]+\.[0-9]+)"'
if (!$resourceVersion -or $resourceVersion.Matches[0].Groups[1].Value -ne $Version) {
    throw 'Build version must match src/version.rc. Implement and review each milestone before building its version.'
}
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (!(Test-Path -LiteralPath $vswhere)) { throw 'Visual Studio C++ Build Tools are required (vswhere.exe was not found).' }
$vsRoot = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$vsRoot) { throw 'No installed Visual Studio x64 C++ toolchain was found.' }
$devShell = Join-Path $vsRoot 'Common7\Tools\Launch-VsDevShell.ps1'
& $devShell -Arch amd64 -HostArch amd64 -SkipAutomaticLocation | Out-Null
$buildDir = Join-Path $projectRoot 'build'
$OutputName = 'Dako.exe'
$objectDir = Join-Path $buildDir 'obj'
New-Item -ItemType Directory -Force -Path $objectDir | Out-Null
$wallpaperRoot = Join-Path $projectRoot 'assets\wallpapers'
$wallpaperCatalog = Get-Content -LiteralPath (Join-Path $wallpaperRoot 'catalog.json') -Raw | ConvertFrom-Json
if ($wallpaperCatalog.photos.Count -ne 18) { throw 'The curated collection must contain 18 photos.' }
foreach ($photo in $wallpaperCatalog.photos) {
    if ([System.IO.Path]::GetFileName($photo.file) -ne $photo.file -or !(Test-Path -LiteralPath (Join-Path $wallpaperRoot $photo.file))) { throw 'Wallpaper assets are missing. Run scripts\fetch-wallpapers.ps1 before building.' }
}
$optimization = if ($Debug) { '/Od' } else { '/O2' }
$manifest = Join-Path $projectRoot 'src\app.manifest'
$sources = @(& (Join-Path $PSScriptRoot 'sources.ps1'))
Push-Location $objectDir
try {
    & rc.exe /nologo /fo Dako.version.res (Join-Path $projectRoot 'src\version.rc')
    if ($LASTEXITCODE -ne 0) { throw 'Version resource compilation failed.' }
    & rc.exe /nologo /fo Dako.modules.res (Join-Path $projectRoot 'src\ModuleStrings.rc')
    if ($LASTEXITCODE -ne 0) { throw 'Module resource compilation failed.' }
    & cl.exe /nologo /std:c++20 /EHsc /W4 /WX /permissive- /utf-8 /DUNICODE /D_UNICODE /DNOMINMAX /O2 /MT /c (Join-Path $projectRoot 'src\SettingsCore.cpp') /Fo:SettingsCore.obj
    if ($LASTEXITCODE -ne 0) { throw 'Shared settings core compilation failed.' }
    & cl.exe /nologo /std:c++20 /W4 /WX /DUNICODE /D_UNICODE /c (Join-Path $projectRoot 'src\AccessibilityGuids.cpp') /Fo:AccessibilityGuids.obj
    if ($LASTEXITCODE -ne 0) { throw 'Accessibility GUID compilation failed.' }
    & lib.exe /nologo /OUT:dako_core.lib SettingsCore.obj AccessibilityGuids.obj
    if ($LASTEXITCODE -ne 0) { throw 'Shared settings library failed.' }
    & cl.exe /nologo /std:c++20 /EHsc /W4 /WX /permissive- /utf-8 /DUNICODE /D_UNICODE /DNOMINMAX /O2 /MT (Join-Path $projectRoot 'src\settings\main.cpp') /Fo:DakoSettings.obj Dako.version.res "/Fe:$(Join-Path $buildDir 'DakoSettings.exe')" /link dako_core.lib /SUBSYSTEM:WINDOWS /DYNAMICBASE /NXCOMPAT d3d11.lib dcomp.lib dxgi.lib oleacc.lib user32.lib gdi32.lib shell32.lib ole32.lib oleaut32.lib advapi32.lib d2d1.lib dwrite.lib dwmapi.lib comdlg32.lib comctl32.lib uuid.lib
    if ($LASTEXITCODE -ne 0) { throw 'Settings application compilation failed.' }
    $headersChanged = (Get-ChildItem -LiteralPath (Join-Path $projectRoot 'src') -Filter '*.h' -File | Measure-Object -Property LastWriteTimeUtc -Maximum).Maximum
    $optionsFile = Join-Path $objectDir 'compile-options.txt'
    $buildOptions = "$optimization /MT /Zi /Gy /Gw"
    $force = !(Test-Path -LiteralPath $optionsFile) -or [IO.File]::ReadAllText($optionsFile) -ne $buildOptions
    $changed = @($sources | Where-Object {
        $object = Join-Path $objectDir ([IO.Path]::GetFileNameWithoutExtension($_) + '.obj')
        !(Test-Path -LiteralPath $object) -or $force -or (Get-Item -LiteralPath $object).LastWriteTimeUtc -lt (Get-Item -LiteralPath $_).LastWriteTimeUtc -or (Get-Item -LiteralPath $object).LastWriteTimeUtc -lt $headersChanged
    })
    if ($changed.Count) {
        & cl.exe /nologo /std:c++20 /EHsc /W4 /WX /permissive- /utf-8 /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /DNOMINMAX $optimization /MT /Zi /MP2 /Gy /Gw /c @changed
        if ($LASTEXITCODE -ne 0) { throw "Native compilation failed: $LASTEXITCODE" }
    }
    $objects = @($sources | ForEach-Object { Join-Path $objectDir ([IO.Path]::GetFileNameWithoutExtension($_) + '.obj') })
    & link.exe /nologo @objects Dako.version.res Dako.modules.res "/OUT:$(Join-Path $buildDir $OutputName)" dako_core.lib /OPT:REF "/MAP:$(Join-Path $buildDir 'Dako.map')" onecoreuap.lib setupapi.lib cfgmgr32.lib shlwapi.lib uuid.lib crypt32.lib /SUBSYSTEM:WINDOWS /DYNAMICBASE /NXCOMPAT /MANIFEST:EMBED "/MANIFESTINPUT:$manifest" d3d11.lib dcomp.lib user32.lib gdi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib d2d1.lib dwrite.lib dwmapi.lib wlanapi.lib bthprops.lib psapi.lib windowsapp.lib windowscodecs.lib oleacc.lib powrprof.lib advapi32.lib iphlpapi.lib shcore.lib pdh.lib dxgi.lib winhttp.lib comctl32.lib dxva2.lib wbemuuid.lib wtsapi32.lib
    if ($LASTEXITCODE -ne 0) { throw "Native link failed: $LASTEXITCODE" }
    [IO.File]::WriteAllText($optionsFile, $buildOptions)
} finally { Pop-Location }
$fontOutput = Join-Path $buildDir 'assets\fonts'
New-Item -ItemType Directory -Path $fontOutput -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $projectRoot 'assets\fonts\Rubik.ttf'), (Join-Path $projectRoot 'assets\fonts\OFL.txt') -Destination $fontOutput -Force
$packagedWallpapers = Join-Path $buildDir 'assets\wallpapers'
New-Item -ItemType Directory -Force -Path $packagedWallpapers | Out-Null
# Synchronize only this app-owned asset directory, preserving unrelated build files.
$packagedRoot = [System.IO.Path]::GetFullPath($packagedWallpapers)
$expectedRoot = [System.IO.Path]::GetFullPath((Join-Path $buildDir 'assets\wallpapers'))
if ($packagedRoot -ne $expectedRoot) { throw 'Unexpected wallpaper output directory.' }
$expectedFiles = @('catalog.json') + @($wallpaperCatalog.photos | ForEach-Object { $_.file })
Get-ChildItem -LiteralPath $packagedRoot -File | Where-Object { $_.Name -notin $expectedFiles } | ForEach-Object { Remove-Item -LiteralPath $_.FullName -Force }
foreach ($file in $expectedFiles) { Copy-Item -LiteralPath (Join-Path $wallpaperRoot $file) -Destination (Join-Path $packagedWallpapers $file) -Force }
$reviewLauncher = "@echo off`r`nstart `"`" `"%~dp0$OutputName`" --preview --layout=dock`r`n"
Set-Content -LiteralPath (Join-Path $buildDir 'Review.cmd') -Value $reviewLauncher -Encoding ascii
$featuresLauncher = "@echo off`r`nstart `"`" `"%~dp0$OutputName`" --preview --layout=dock --features`r`n"
Set-Content -LiteralPath (Join-Path $buildDir 'Features.cmd') -Value $featuresLauncher -Encoding ascii
Write-Output (Join-Path $buildDir $OutputName)
