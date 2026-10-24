$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$executable = Join-Path $projectRoot 'build\Dako.exe'
$renderDir = Join-Path $projectRoot 'docs\renders'
if (!(Test-Path -LiteralPath $executable)) { throw 'Build Dako first with scripts\build.ps1.' }
New-Item -ItemType Directory -Force -Path $renderDir | Out-Null
$validation = Start-Process -FilePath $executable -ArgumentList ('--render-dir="' + $renderDir + '"') -WindowStyle Hidden -Wait -PassThru
if ($validation.ExitCode -ne 0) { throw "Native render/state/accessibility validation failed: $($validation.ExitCode)" }
$expected = 'compact','controls','today','stats','clipboard','apps','wallpaper','wallpaper-last','power','wifi','wifi-password','bluetooth','dock-open','dock-browser','media-mini','theme-blue','theme-pink','attached-controls','attached-today','attached-stats','attached-clipboard','attached-apps','attached-wallpaper','attached-power'
foreach ($name in $expected) {
    $capture = Get-Item -LiteralPath (Join-Path $renderDir "$name.png")
    $bytes = [System.IO.File]::ReadAllBytes($capture.FullName)
    if ($bytes.Length -lt 60 -or $bytes[0] -ne 137 -or $bytes[1] -ne 80 -or $bytes[2] -ne 78 -or $bytes[3] -ne 71) { throw "Invalid PNG render: $name" }
    if ($name -eq 'compact') {
        $imageWidth = [System.Net.IPAddress]::NetworkToHostOrder([BitConverter]::ToInt32($bytes,16))
        $imageHeight = [System.Net.IPAddress]::NetworkToHostOrder([BitConverter]::ToInt32($bytes,20))
        if ($imageWidth -ne 216 -or $imageHeight -ne 18) { throw 'The resting dock must be a 216x18 top notch.' }
    }
}
Write-Output 'PASS: twenty-four native renders, attached panels, browser-only detachment, wallpaper/navigation, circular reveal geometry, accent variants, fixed tab anchors, hit testing, Escape and accessibility metadata.'
