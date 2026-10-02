param(
  [string]$AssettoRoot = ""
)

$ErrorActionPreference = "Stop"

function Find-AssettoRoot {
  param([string]$Given)
  $candidates = @()
  if ($Given) { $candidates += $Given }
  $candidates += (Get-Location).Path
  $candidates += "C:\Program Files (x86)\Steam\steamapps\common\assettocorsa"
  $candidates += "C:\Program Files\Steam\steamapps\common\assettocorsa"

  foreach ($c in $candidates) {
    if (-not $c) { continue }
    try { $p = [System.IO.Path]::GetFullPath($c) } catch { continue }
    if (Test-Path (Join-Path $p "acs.exe")) { return $p }
  }
  return $null
}

$root = Find-AssettoRoot $AssettoRoot
if (-not $root) {
  $root = Read-Host "Paste the Assetto Corsa root path"
}

$app = Join-Path $root "apps\lua\ac_soundsim_pcm_probe"
$docs = [Environment]::GetFolderPath("MyDocuments")
$logs = Join-Path $docs "Assetto Corsa\logs"

$stage = Join-Path $env:TEMP "AC_SoundSim_Probe_Results"
if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
New-Item -ItemType Directory -Path $stage -Force | Out-Null

$wantedApp = @(
  "csp_stream_api_report.txt",
  "probe_runtime_report.txt",
  "manifest.ini",
  "ac_soundsim_pcm_probe.lua"
)
foreach ($name in $wantedApp) {
  $src = Join-Path $app $name
  if (Test-Path $src) { Copy-Item $src $stage -Force }
}

$wantedLogs = @(
  "custom_shaders_patch.log",
  "log.txt",
  "py_log.txt"
)
foreach ($name in $wantedLogs) {
  $src = Join-Path $logs $name
  if (Test-Path $src) { Copy-Item $src (Join-Path $stage $name) -Force }
}

$summary = @()
$summary += "AssettoRoot=$root"
$summary += "AppFolder=$app"
$summary += "Documents=$docs"
$summary += "LogsFolder=$logs"
$summary += "Collected=$(Get-Date -Format o)"
$summary | Set-Content (Join-Path $stage "paths.txt") -Encoding UTF8

$out = Join-Path $root "AC_SoundSim_Probe_Results.zip"
if (Test-Path $out) { Remove-Item $out -Force }
Compress-Archive -Path (Join-Path $stage "*") -DestinationPath $out -Force

Write-Host ""
Write-Host "DONE" -ForegroundColor Green
Write-Host "Send me this file:" -ForegroundColor Cyan
Write-Host "  $out"
Write-Host ""
