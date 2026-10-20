# Runs qmllint on a response file with a hard memory cap and timeout.
# qmllint 6.9 can grow to tens of GB on pathological QML input (e.g. a regex
# literal plus a loop in one function), which can freeze the whole machine.
# The cap turns that into a fast, diagnosable test failure instead.
param(
    [Parameter(Mandatory = $true)][string]$Exe,
    [Parameter(Mandatory = $true)][string]$Rsp,
    [int]$LimitMb = 1024,
    [int]$TimeoutSec = 120
)

$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [Text.Encoding]::UTF8

if (-not (Test-Path -LiteralPath $Exe)) {
    Write-Output "qmllint guard: executable not found: $Exe"
    exit 3
}
if (-not (Test-Path -LiteralPath $Rsp)) {
    Write-Output "qmllint guard: response file not found: $Rsp"
    exit 3
}

$psi = New-Object System.Diagnostics.ProcessStartInfo
$psi.FileName = $Exe
$psi.Arguments = '@"' + $Rsp + '"'
$psi.UseShellExecute = $false
$psi.CreateNoWindow = $true
$psi.RedirectStandardOutput = $true
$psi.RedirectStandardError = $true

$process = New-Object System.Diagnostics.Process
$process.StartInfo = $psi
$process.Start() | Out-Null
# Drain both pipes while running; otherwise a full pipe blocks qmllint.
$stdout = $process.StandardOutput.ReadToEndAsync()
$stderr = $process.StandardError.ReadToEndAsync()

$watch = [Diagnostics.Stopwatch]::StartNew()
$killed = $null
while (-not $process.HasExited) {
    Start-Sleep -Milliseconds 100
    $process.Refresh()
    if ($process.WorkingSet64 -gt $LimitMb * 1MB) {
        $killed = 'memory'
        break
    }
    if ($watch.Elapsed.TotalSeconds -gt $TimeoutSec) {
        $killed = 'timeout'
        break
    }
}
if ($killed) {
    try { $process.Kill() } catch { }
}
$process.WaitForExit()
$peakMb = [math]::Round($process.PeakWorkingSet64 / 1MB)

Write-Output $stdout.Result
if ($stderr.Result) { Write-Output $stderr.Result }

if ($killed -eq 'memory') {
    Write-Output ("qmllint guard: exceeded {0}MB (peak {1}MB) - likely pathological QML for qmllint 6.9; inspect recent QML changes for a regex literal plus a loop in one function." -f $LimitMb, $peakMb)
    exit 4
}
if ($killed -eq 'timeout') {
    Write-Output ("qmllint guard: timed out after {0}s (peak {1}MB)." -f $TimeoutSec, $peakMb)
    exit 5
}
exit $process.ExitCode
