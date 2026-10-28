param([switch]$ExcludeMain)
$sourceRoot = Join-Path (Split-Path -Parent $PSScriptRoot) 'src'
Get-ChildItem -LiteralPath $sourceRoot -Filter '*.cpp' -File |
    Where-Object { ($_.Name -ne 'SettingsCore.cpp') -and (!$ExcludeMain -or $_.Name -ne 'main.cpp') } |
    Sort-Object Name |
    Select-Object -ExpandProperty FullName
