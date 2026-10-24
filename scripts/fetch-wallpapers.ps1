$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$assetRoot = Join-Path $projectRoot 'assets\wallpapers'
$catalog = Get-Content -LiteralPath (Join-Path $assetRoot 'catalog.json') -Raw | ConvertFrom-Json
Add-Type -AssemblyName System.Drawing
foreach ($photo in $catalog.photos) {
    if ([System.IO.Path]::GetFileName($photo.file) -ne $photo.file -or $photo.downloadUrl -notlike 'https://images.pexels.com/photos/*') { throw 'Invalid wallpaper catalog entry.' }
    $destination = Join-Path $assetRoot $photo.file
    $temporary = $destination + '.download'
    if (!(Test-Path -LiteralPath $destination)) {
        try {
            Invoke-WebRequest -Uri $photo.downloadUrl -OutFile $temporary -UseBasicParsing -TimeoutSec 120
            $image = [System.Drawing.Image]::FromFile($temporary)
            try {
                if ($image.Width -ne 3840 -or $image.Height -ge $image.Width) { throw "Invalid wallpaper dimensions: $($photo.file)" }
            } finally { $image.Dispose() }
            Move-Item -LiteralPath $temporary -Destination $destination
        } finally {
            if (Test-Path -LiteralPath $temporary) { Remove-Item -LiteralPath $temporary -Force }
        }
    }
    Write-Output $photo.file
}
Write-Output 'All 18 curated wallpapers are available locally. Rebuild Dako to package them.'
