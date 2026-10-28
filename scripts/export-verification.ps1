param([ValidatePattern('^\d+\.\d+\.\d+-rc\.\d+$')][string]$Version='4.0.0-rc.1')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$output = Join-Path $projectRoot "docs\verification\$Version"
$gates = @(
    @{Name='build'; Log='rc-build.log'; Command='scripts/build.ps1'; Marker='Dako\.exe\s*$'},
    @{Name='v4'; Log='rc-v4.log'; Command='scripts/test-v4.ps1'; Marker='^PASS independent settings GUI'},
    @{Name='motion'; Log='rc-motion.log'; Command='scripts/test-motion.ps1'; Marker='^PASS: inert write gates'},
    @{Name='render'; Log='rc-render.log'; Command='scripts/verify.ps1'; Marker='^PASS: 1068 fixture PNGs'},
    @{Name='shell'; Log='rc-shell.log'; Command='scripts/test-shell.ps1'; Marker='^PASS: per-monitor AppBars'},
    @{Name='live'; Log='rc-live.log'; Command='scripts/test-live.ps1 -UseProductionObjects';
      Marker='^PASS: native read-only control capabilities'}
)
$reports = @{}
foreach ($gate in $gates) {
    $text = [IO.File]::ReadAllText((Join-Path $projectRoot "build\$($gate.Log)"))
    if ($text -notmatch "(?m)$($gate.Marker)" -or $text -match '(?m)^FAIL:|error C\d+|Exception:') {
        throw "Gate $($gate.Name) is missing a complete passing log. No evidence exported."
    }
    $reports[$gate.Name] = $text
}
foreach ($name in @('Dako.exe', 'DakoSettings.exe')) {
    $info = (Get-Item -LiteralPath (Join-Path $projectRoot "build\$name")).VersionInfo
    if ($info.ProductVersion -ne $Version -or !$info.IsPreRelease) { throw "Unexpected binary identity: $name" }
}
& (Join-Path $PSScriptRoot 'check-style.ps1')
New-Item -ItemType Directory -Path $output -Force | Out-Null
$utf8 = [Text.UTF8Encoding]::new($false)
function Write-Evidence([string]$name, [string]$text) {
    $text = $text.Replace($projectRoot, '<repo>').Replace($projectRoot.Replace([char]92,[char]47), '<repo>')
    [IO.File]::WriteAllText((Join-Path $output $name), $text.Replace("`r`n","`n").TrimEnd() + "`n", $utf8)
}
foreach ($gate in $gates) { Write-Evidence "$($gate.Name).txt" $reports[$gate.Name] }
Write-Evidence 'lifecycle-results.txt' ([IO.File]::ReadAllText(
    (Join-Path $projectRoot 'build\tests\live\reports\results.txt')))
Write-Evidence 'style.txt' 'PASS all C++ source and test lines fit the 120-column limit.'
$hasher = [Security.Cryptography.SHA256]::Create()
function Hash-Text([string]$text) {
    return [BitConverter]::ToString($hasher.ComputeHash($utf8.GetBytes($text))).Replace('-','').ToLowerInvariant()
}
try {
    $files = @((Get-Item -LiteralPath (Join-Path $projectRoot '.clang-format')))
    foreach ($directory in @('src','tests','scripts')) {
        $files += @(Get-ChildItem -LiteralPath (Join-Path $projectRoot $directory) -Recurse -File |
                    Where-Object { $_.Extension -in @('.cpp','.h','.rc','.manifest','.ps1') })
    }
    $sources = @($files | Sort-Object FullName | ForEach-Object {
        [ordered]@{
            path=$_.FullName.Substring($projectRoot.Length + 1).Replace([char]92,[char]47)
            sha256=(Hash-Text ([IO.File]::ReadAllText($_.FullName).Replace("`r`n","`n")))
        }
    })
    $binaries = @('Dako.exe','DakoSettings.exe') | ForEach-Object {
        $file = Get-Item -LiteralPath (Join-Path $projectRoot "build\$_")
        [ordered]@{name=$_; sha256=(Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant();
                   productVersion=$file.VersionInfo.ProductVersion; prerelease=$file.VersionInfo.IsPreRelease}
    }
    $manifest = [ordered]@{
        version=$Version; reviewedDate='2026-10-28'; baseCommit=(& git -C $projectRoot rev-parse HEAD).Trim()
        sourceNormalization='UTF-8 text, BOM removed, CRLF normalized to LF'
        sourceSha256=(Hash-Text (($sources | ForEach-Object { $_.path + "`0" + $_.sha256 + "`n" }) -join ''))
        sources=$sources; binaries=@($binaries)
        gates=@($gates | ForEach-Object { [ordered]@{name=$_.Name; command=$_.Command; log=$_.Name+'.txt'; passed=$true} })
    }
    Write-Evidence 'manifest.json' ($manifest | ConvertTo-Json -Depth 6)
} finally { $hasher.Dispose() }
Write-Output "Exported reproducible text evidence to $output"
