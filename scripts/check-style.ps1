$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot 'shape-lint.ps1')
$violations = @()
foreach ($directory in @('src', 'tests')) {
    foreach ($file in (Get-ChildItem -LiteralPath (Join-Path $projectRoot $directory) -Recurse -File |
                      Where-Object { $_.Extension -in @('.cpp', '.h') })) {
        $lineNumber = 0
        foreach ($line in [IO.File]::ReadAllLines($file.FullName)) {
            $lineNumber++
            if ($line.Length -gt 120) { $violations += "$($file.FullName):$lineNumber exceeds 120 columns" }
        }
        if ($directory -eq 'src' -and $file.Name -ne 'Shape.h') {
            $relative = $file.FullName.Substring($projectRoot.Length + 1).Replace('\','/')
            $radii = @(Get-ShapeRadiusViolations ([IO.File]::ReadAllText($file.FullName)) $relative)
            if ($radii.Count) {
                foreach ($radius in $radii) {
                    $violations += "$relative`:$($radius.Line) uses literal radius $($radius.Radius) in $($radius.Call)"
                }
            }
        }
    }
}
if ($violations.Count) { throw ($violations -join "`n") }
Write-Output 'PASS all C++ source and test lines fit the 120-column limit.'
Write-Output 'PASS shape radius lint; no numeric radii outside Shape.h.'
