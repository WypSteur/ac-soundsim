param(
  [Parameter(Mandatory=$true)]
  [string]$AssettoRoot
)

$ErrorActionPreference = "Stop"
$source = Join-Path $PSScriptRoot "..\integration\csp\apps\ac_soundsim_bridge"
$target = Join-Path $AssettoRoot "apps\lua\ac_soundsim_bridge"

if (-not (Test-Path (Join-Path $AssettoRoot "acs.exe"))) {
  throw "Invalid Assetto Corsa root: acs.exe not found."
}

if (Get-Process acs -ErrorAction SilentlyContinue) {
  throw 'Exit the AC session before installing the bridge.'
}
if (Test-Path -LiteralPath $target) {
  $backupRoot = Join-Path $PSScriptRoot '..\artifacts\bridge-backups'
  New-Item -ItemType Directory -Path $backupRoot -Force | Out-Null
  $backup = Join-Path $backupRoot (Get-Date -Format 'yyyyMMdd-HHmmss-fff')
  Copy-Item -LiteralPath $target -Destination $backup -Recurse
  Write-Host "Previous bridge backed up at $backup"
}

New-Item -ItemType Directory -Path $target -Force | Out-Null
Copy-Item -Path (Join-Path $source "*") -Destination $target -Recurse -Force
Write-Host "Installed AC SoundSim Bridge to $target" -ForegroundColor Green
Write-Host "[ACSoundSim] snapshot: Documents\Assetto Corsa\logs\ac_soundsim_bridge.txt (also CSP debug app)." -ForegroundColor Cyan
Write-Host "Native stream creation/release: Documents\Assetto Corsa\logs\custom_shaders_patch.log." -ForegroundColor Cyan
