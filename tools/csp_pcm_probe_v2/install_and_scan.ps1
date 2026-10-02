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
    if ((Test-Path (Join-Path $p "acs.exe")) -and (Test-Path (Join-Path $p "extension"))) {
      return $p
    }
  }
  return $null
}

$root = Find-AssettoRoot $AssettoRoot
if (-not $root) {
  $root = Read-Host "Paste the Assetto Corsa root path (folder containing acs.exe)"
  if (-not (Test-Path (Join-Path $root "acs.exe"))) {
    throw "Invalid Assetto Corsa folder: acs.exe not found."
  }
}

$sourceApp = Join-Path $PSScriptRoot "app_payload\ac_soundsim_pcm_probe"
$targetApp = Join-Path $root "apps\lua\ac_soundsim_pcm_probe"
$sdk = Join-Path $root "extension\internal\lua-sdk"

New-Item -ItemType Directory -Path $targetApp -Force | Out-Null
Copy-Item -Path (Join-Path $sourceApp "*") -Destination $targetApp -Recurse -Force

$report = Join-Path $targetApp "csp_stream_api_report.txt"
$lines = New-Object System.Collections.Generic.List[string]
$lines.Add("AC SoundSim PCM Probe v0.2")
$lines.Add("AssettoRoot=$root")
$lines.Add("SDK=$sdk")
$lines.Add("Date=$(Get-Date -Format o)")
$lines.Add("")

$hits = @()
if (Test-Path $sdk) {
  $files = Get-ChildItem -Path $sdk -Recurse -File -ErrorAction SilentlyContinue |
    Where-Object { $_.Extension -in ".lua", ".md", ".txt", ".ini" }

  foreach ($file in $files) {
    try {
      $m = Select-String -Path $file.FullName -Pattern `
        "streaming audio|audio stream|audiostream|fromstream|\bpcm\b|AudioEvent.*stream|stream.*AudioEvent" `
        -CaseSensitive:$false -ErrorAction SilentlyContinue
      if ($m) { $hits += $m }
    } catch {}
  }
}

if ($hits.Count -gt 0) {
  $lines.Add("RESULT_A_STREAM_API_FOUND")
  $lines.Add("")
  foreach ($h in $hits | Select-Object -First 200) {
    $rel = $h.Path.Replace($root, "<AC>")
    $lines.Add(("{0}:{1}: {2}" -f $rel, $h.LineNumber, $h.Line.Trim()))
  }
  Write-Host "Scanner: STREAMING/PCM API TEXT FOUND" -ForegroundColor Green
} else {
  $lines.Add("RESULT_B_STREAM_API_NOT_FOUND")
  $lines.Add("No streaming/PCM-related definition was found in extension\internal\lua-sdk.")
  Write-Host "Scanner: NO STREAMING/PCM API TEXT FOUND" -ForegroundColor Yellow
}

$lines | Set-Content -Path $report -Encoding UTF8

Write-Host ""
Write-Host "Installed to: $targetApp" -ForegroundColor Cyan
Write-Host "Scanner report: $report" -ForegroundColor Cyan
Write-Host ""
Write-Host "Launch AC, open 'AC SoundSim PCM Probe', press Play reference tone, then run collect_probe_results.ps1." -ForegroundColor Yellow
