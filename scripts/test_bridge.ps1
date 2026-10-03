$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$previousPythonPath = $env:PYTHONPATH
try {
  $env:PYTHONPATH = Join-Path $projectRoot 'tools\build-runtime'
  $bridgeFolder = Join-Path $projectRoot 'integration\csp\apps\ac_soundsim_bridge'
  @{
    bridge = Get-Content -LiteralPath (Join-Path $bridgeFolder 'ac_soundsim_bridge.lua') -Raw
    acoustics = Get-Content -LiteralPath (Join-Path $bridgeFolder 'acoustics.lua') -Raw
    health = Get-Content -LiteralPath (Join-Path $bridgeFolder 'transport_health.lua') -Raw
  } | ConvertTo-Json -Compress | python (Join-Path $projectRoot 'tests\bridge_host.py')
  if ($LASTEXITCODE -ne 0) { throw 'LuaJIT bridge host test failed (requires local dev-only lupa 2.6).' }
} finally { $env:PYTHONPATH = $previousPythonPath }
