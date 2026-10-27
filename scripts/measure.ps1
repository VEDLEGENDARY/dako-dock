param([ValidateRange(2,60)][int]$Seconds = 10,[int[]]$ProcessIds,[switch]$ModuleBudgets)
$ErrorActionPreference = 'Stop'
if ($ModuleBudgets) {
    $projectRoot = Split-Path -Parent $PSScriptRoot
    $executable = Join-Path $projectRoot 'build\Dako.exe'
    $probe = Start-Process -FilePath $executable -ArgumentList "--module-measure=$Seconds" -WindowStyle Hidden -Wait -PassThru
    $report = Join-Path $projectRoot 'build\module-budgets.csv'
    if ($probe.ExitCode -ne 0) { throw "Module performance budget failed. See $report" }
    $rows = @(Import-Csv -LiteralPath $report)
    if (!$rows.Count -or @($rows | Where-Object { $_.pass -ne '1' }).Count) { throw 'Invalid module budget report.' }
    $rows | Format-Table
    return
}
$explicitProcessSelection = $PSBoundParameters.ContainsKey('ProcessIds')
$nativeProcesses = if ($ProcessIds) { @(Get-Process -Id $ProcessIds -ErrorAction Stop) } else { @(Get-Process -Name Dako -ErrorAction Stop) }
if ($nativeProcesses.Count -lt 1) { throw 'Start Dako before measuring.' }
$processIds = @($nativeProcesses.Id)
$cpuStart = ($nativeProcesses | ForEach-Object { $_.TotalProcessorTime.TotalMilliseconds } | Measure-Object -Sum).Sum
$timer = [System.Diagnostics.Stopwatch]::StartNew()
Start-Sleep -Seconds $Seconds
$nativeProcesses = @(Get-Process -Id $processIds -ErrorAction Stop)
if (!$explicitProcessSelection -and @(Compare-Object $processIds @((Get-Process -Name Dako -ErrorAction Stop).Id)).Count -ne 0) { throw 'Dako process membership changed during sampling. Wait for startup/recovery to settle and rerun.' }
$cpuEnd = ($nativeProcesses | ForEach-Object { $_.TotalProcessorTime.TotalMilliseconds } | Measure-Object -Sum).Sum
$timer.Stop()
[pscustomobject]@{
    ProcessIds = $processIds -join ', '
    ProcessCount = $nativeProcesses.Count
    SampleSeconds = [math]::Round($timer.Elapsed.TotalSeconds, 2)
    WorkingSetMiB = [math]::Round(($nativeProcesses | Measure-Object WorkingSet64 -Sum).Sum / 1MB, 2)
    PrivateMiB = [math]::Round(($nativeProcesses | Measure-Object PrivateMemorySize64 -Sum).Sum / 1MB, 2)
    CpuMilliseconds = [math]::Round($cpuEnd - $cpuStart, 2)
    CpuPercentNormalized = [math]::Round(100 * ($cpuEnd - $cpuStart) / $timer.Elapsed.TotalMilliseconds / [Environment]::ProcessorCount, 4)
    Handles = ($nativeProcesses | Measure-Object HandleCount -Sum).Sum
    Threads = ($nativeProcesses | ForEach-Object { $_.Threads.Count } | Measure-Object -Sum).Sum
} | Format-List
