param(
    [ValidateSet('Dako.exe','Dako.next.exe','Dako.preview.exe')][string]$OutputName='Dako.exe',
    [string]$RenderDirectory
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$executable = Join-Path $projectRoot "build\$OutputName"
$renderDir = if ($RenderDirectory) { [IO.Path]::GetFullPath($RenderDirectory) } else { Join-Path $projectRoot 'docs\renders\v2' }
if (!(Test-Path -LiteralPath $executable)) { throw 'Build Dako first with scripts\build.ps1.' }
New-Item -ItemType Directory -Force -Path $renderDir | Out-Null
$validation = Start-Process -FilePath $executable -ArgumentList ('--render-dir="' + $renderDir + '"') -WindowStyle Hidden -Wait -PassThru
if ($validation.ExitCode -ne 0) { throw "Native render/state/accessibility validation failed: $($validation.ExitCode). See $renderDir\verification.txt" }
$views = 'controls','today','stats','clipboard','apps','wallpaper','power'
$expected = @($views) + @('compact','wallpaper-last','wallpaper-error','wallpaper-64','wallpaper-one','wallpaper-empty','wifi-password','wifi','wifi-error','bluetooth','bluetooth-busy','media-mini','theme-blue','theme-pink','theme-light','theme-high-contrast','controls-final','browser-apps-200','browser-wifi-200')
foreach ($scale in 100,125,150,175,200) {
    foreach ($view in $views) { $expected += "matrix-$view-$scale" }
    $expected += "matrix-today-scrolled-$scale"
}
$expected += @('controls-volume-zero','controls-volume-one','controls-volume-full','controls-muted','controls-no-audio','wifi-unselected','wifi-radio-off','wifi-no-adapter','wifi-permission-error','bluetooth-unpaired','bluetooth-error','bluetooth-empty','bluetooth-radio-off','bluetooth-no-adapter','today-empty','apps-empty','clipboard-empty','clipboard-long','wallpaper-loading','wallpaper-applying','media-unavailable','media-live-stream','media-paused','media-long')
foreach ($textScale in 125,150,200,225) { foreach ($view in (@($views) + @('wifi','mini'))) { $expected += "text-$textScale-$view" } }
$expected += 'theme-dark'
foreach ($name in $expected) {
    $capture = Get-Item -LiteralPath (Join-Path $renderDir "$name.png")
    $bytes = [System.IO.File]::ReadAllBytes($capture.FullName)
    if ($bytes.Length -lt 60 -or $bytes[0] -ne 137 -or $bytes[1] -ne 80 -or $bytes[2] -ne 78 -or $bytes[3] -ne 71) { throw "Invalid PNG render: $name" }
    $imageWidth = [System.Net.IPAddress]::NetworkToHostOrder([BitConverter]::ToInt32($bytes,16))
    $imageHeight = [System.Net.IPAddress]::NetworkToHostOrder([BitConverter]::ToInt32($bytes,20))
    if ($name -eq 'compact' -and ($imageWidth -ne 216 -or $imageHeight -ne 18)) { throw 'The resting dock must be a 216x18 affordance.' }
    if ($name -like 'matrix-*' -and ($imageWidth -gt 1366 -or $imageHeight -gt 768)) { throw "Render exceeds fixture monitor: $name" }
}
$captureCount = $expected.Count - 1
if (!(Select-String -LiteralPath (Join-Path $renderDir 'verification.txt') -Pattern "^PASS captures=$captureCount;")) { throw 'Native scene assertions did not complete.' }
Write-Output "PASS: $($expected.Count) fixture PNGs, geometry/identity assertions, catalog replacement, 5 DPI tiers, 4 text tiers and expanded MSAA semantics. Live desktop, screen-reader and hardware validation are separate."
