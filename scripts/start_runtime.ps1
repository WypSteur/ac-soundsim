param([int]$Seconds = 0, [switch]$DiagnosticTone, [switch]$LegacyModel)
$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$binary = Join-Path $projectRoot 'build\Release\soundsim-runtime.exe'
if (-not (Test-Path -LiteralPath $binary)) { throw 'Build first with scripts/build_m1.ps1.' }
$arguments = @('--logs', ('"' + (Join-Path $projectRoot 'logs\runtime') + '"'))
if ($Seconds -lt 0) { throw 'Seconds must be non-negative.' }
if ($Seconds -gt 0) { $arguments += @('--seconds', $Seconds) }
if ($DiagnosticTone) {
  $arguments += '--diagnostic-tone'
  Write-Warning 'Diagnostic 440 Hz tone only; NOT the engine simulation.'
}
if ($LegacyModel) { $arguments += '--legacy-model'; Write-Warning 'Legacy provisional M1 model (comparison/rollback only).' }
$process = Start-Process -FilePath $binary -ArgumentList $arguments -WorkingDirectory $projectRoot -WindowStyle Hidden -PassThru
Start-Sleep -Milliseconds 500
if ($process.HasExited) { throw ('Runtime exited with code ' + $process.ExitCode + '; inspect logs/runtime (another runtime might already be active).') }
Write-Host "SoundSim runtime started (PID $($process.Id)); logs/runtime. Stop with scripts/stop_runtime.ps1."
