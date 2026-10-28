$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$violations = @()
foreach ($directory in @('src', 'tests')) {
    foreach ($file in (Get-ChildItem -LiteralPath (Join-Path $projectRoot $directory) -Recurse -File |
                      Where-Object { $_.Extension -in @('.cpp', '.h') })) {
        $lineNumber = 0
        foreach ($line in [IO.File]::ReadAllLines($file.FullName)) {
            $lineNumber++
            if ($line.Length -gt 120) { $violations += "$($file.FullName):$lineNumber exceeds 120 columns" }
        }
    }
}
if ($violations.Count) { throw ($violations -join "`n") }
Write-Output 'PASS all C++ source and test lines fit the 120-column limit.'
