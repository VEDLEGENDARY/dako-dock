param([ValidatePattern('^\d+\.\d+\.\d+$')][string]$Version='2.0.0')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$executable = Join-Path $projectRoot 'build\Dako.exe'
if (!(Test-Path -LiteralPath $executable)) { throw 'Build the release executable with scripts\build.ps1 first.' }
$actualVersion = (Get-Item -LiteralPath $executable).VersionInfo.FileVersion
if ($actualVersion -ne "$Version.0") { throw "Executable version $actualVersion does not match package $Version." }
$dist = Join-Path $projectRoot 'dist'
New-Item -ItemType Directory -Path $dist -Force | Out-Null
$stage = Join-Path $dist ("package-" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $stage | Out-Null
Copy-Item -LiteralPath $executable -Destination (Join-Path $stage 'Dako.exe')
$wallpapers = Join-Path $projectRoot 'build\assets\wallpapers'
$catalog = Get-Content -LiteralPath (Join-Path $wallpapers 'catalog.json') -Raw | ConvertFrom-Json
if ($catalog.photos.Count -ne 18) { throw 'Expected all 18 bundled wallpapers.' }
$assetStage = Join-Path $stage 'assets\wallpapers'
New-Item -ItemType Directory -Path $assetStage -Force | Out-Null
foreach ($name in (@('catalog.json') + @($catalog.photos | ForEach-Object { $_.file }))) {
    if ([IO.Path]::GetFileName($name) -ne $name) { throw 'Invalid wallpaper filename.' }
    Copy-Item -LiteralPath (Join-Path $wallpapers $name) -Destination (Join-Path $assetStage $name)
}
Copy-Item -LiteralPath (Join-Path $projectRoot 'README.md') -Destination (Join-Path $stage 'README.md')
Copy-Item -LiteralPath (Join-Path $projectRoot 'docs\wallpapers\README.md') -Destination (Join-Path $assetStage 'SOURCES.md')
$archive = Join-Path $dist "Dako-$Version-windows-x64.zip"
Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $archive -Force
Copy-Item -LiteralPath $executable -Destination (Join-Path $dist 'Dako.exe') -Force
$checksums = foreach ($file in @('Dako.exe', "Dako-$Version-windows-x64.zip")) {
    $hash = Get-FileHash -LiteralPath (Join-Path $dist $file) -Algorithm SHA256
    '{0}  {1}' -f $hash.Hash.ToLowerInvariant(), $file
}
$checksums | Set-Content -LiteralPath (Join-Path $dist 'SHA256SUMS.txt') -Encoding ascii
# Delete only the uniquely created staging directory inside this project's dist.
$resolvedStage = [IO.Path]::GetFullPath($stage)
if (![IO.Path]::GetDirectoryName($resolvedStage).Equals([IO.Path]::GetFullPath($dist), [StringComparison]::OrdinalIgnoreCase)) { throw 'Unexpected staging path.' }
Remove-Item -LiteralPath $resolvedStage -Recurse -Force
Get-Item -LiteralPath $archive, (Join-Path $dist 'Dako.exe'), (Join-Path $dist 'SHA256SUMS.txt') | Select-Object Name,Length
