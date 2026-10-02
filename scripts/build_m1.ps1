param(
  [ValidateSet('Release', 'Debug')][string]$Configuration = 'Release',
  [string]$CMake = '',
  [switch]$GenerateAudio
)

$ErrorActionPreference = 'Stop'
$projectRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$buildRoot = Join-Path $projectRoot 'build'
if (-not $CMake) {
  $localCMake = Join-Path $projectRoot 'tools\build-runtime\cmake\data\bin\cmake.exe'
  if (Test-Path -LiteralPath $localCMake) { $CMake = $localCMake }
  else { $CMake = (Get-Command cmake -ErrorAction Stop).Source }
}
$CMake = (Get-Command $CMake -ErrorAction Stop).Source
$ctest = Join-Path (Split-Path $CMake) 'ctest.exe'

& (Join-Path $PSScriptRoot 'bootstrap_engine_sim.ps1')
& $CMake -S $projectRoot -B $buildRoot -G 'Visual Studio 17 2022' -A x64
if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed' }
& $CMake --build $buildRoot --config $Configuration --parallel
if ($LASTEXITCODE -ne 0) { throw 'M1 build failed' }
& $ctest --test-dir $buildRoot -C $Configuration --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'M1 tests failed' }
if ($GenerateAudio) {
  & (Join-Path $buildRoot "$Configuration\soundsim-m1.exe") --output (Join-Path $projectRoot 'artifacts\m1')
  if ($LASTEXITCODE -ne 0) { throw 'M1 audio harness failed' }
}
