param([Parameter(Mandatory)][ValidateRange(1,8)][int]$Phase)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$evidence = Join-Path $projectRoot "docs\verification\design-phases\phase-$Phase"
New-Item -ItemType Directory -Force -Path $evidence | Out-Null
& (Join-Path $PSScriptRoot 'build.ps1')
& (Join-Path $PSScriptRoot 'check-style.ps1')
& (Join-Path $PSScriptRoot 'verify.ps1')
& (Join-Path $PSScriptRoot 'test-motion.ps1')
$renders = Join-Path $projectRoot 'build\tests\render'
foreach ($name in @('v4-Controls','v4-Today','v4-Stats','v4-Power','v4-Wallpaper',
                     'theme-high-contrast','text-200-controls','wallpaper-error','v4-dock-1-150')) {
    Copy-Item -LiteralPath (Join-Path $renders "$name.png") -Destination $evidence -Force
}
Copy-Item -LiteralPath (Join-Path $renders 'verification.txt') -Destination $evidence -Force
if ($Phase -ge 7) {
    foreach ($kind in @('confirm','editor','menu')) {
        Copy-Item -LiteralPath (Join-Path $renders "polish-$kind-0-1.png") -Destination $evidence -Force
    }
}
Set-Content -LiteralPath (Join-Path $evidence 'checks.txt') -Value @(
    "Phase ${Phase}: build /W4 /WX /permissive-, check-style, verify, test-motion passed.",
    'Before: docs/verification/design-baseline. After: this directory.',
    'Fixture renders are deterministic; live desktop and performance evidence are recorded separately.'
)
