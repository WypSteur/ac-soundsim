param(
  [string]$Destination = ""
)

$ErrorActionPreference = "Stop"

$projectRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if (-not $Destination) { $Destination = Join-Path $projectRoot 'vendor\engine-sim' }
$Destination = [System.IO.Path]::GetFullPath($Destination)
$revision = '85f7c3b959a908ed5232ede4f1a4ac7eafe6b630'
$patch = Join-Path $projectRoot 'integration\engine_sim\patches\0001-headless-block-renderer.patch'

function Invoke-GitChecked {
  param([string[]]$GitArguments)
  & git @GitArguments
  if ($LASTEXITCODE -ne 0) { throw "Git command failed: $($GitArguments -join ' ')" }
}

if (-not (Test-Path -LiteralPath $Destination)) {
  Invoke-GitChecked @('clone', '--no-checkout', 'https://github.com/ange-yaghi/engine-sim.git', $Destination)
  Invoke-GitChecked @('-C', $Destination, 'checkout', '--detach', $revision)
}

$actualRoot = & git -C $Destination rev-parse --show-toplevel
if ($LASTEXITCODE -ne 0 -or [System.IO.Path]::GetFullPath($actualRoot) -ne $Destination) {
  throw "Destination is not an Engine-Sim checkout: $Destination"
}
$actual = & git -C $Destination rev-parse HEAD
if ($LASTEXITCODE -ne 0 -or $actual -ne $revision) {
  throw "Existing checkout has revision $actual; expected $revision. Preserve local changes and re-audit before updating."
}

# M1 uses only SCS body/state support. No graphics, video, scripting or CSV deps.
Invoke-GitChecked @('-C', $Destination, 'submodule', 'update', '--init', 'dependencies/submodules/simple-2d-constraint-solver')
$previousErrorPolicy = $ErrorActionPreference
$ErrorActionPreference = 'Continue' # Windows PowerShell 5.1 turns native stderr into error records.
& git -C $Destination apply --reverse --check $patch 2>$null
$reverseCheckResult = $LASTEXITCODE
$ErrorActionPreference = $previousErrorPolicy
if ($reverseCheckResult -eq 0) {
  Write-Host 'Headless patch already applied.' -ForegroundColor Yellow
} else {
  Invoke-GitChecked @('-C', $Destination, 'apply', '--check', $patch)
  Invoke-GitChecked @('-C', $Destination, 'apply', $patch)
}

Write-Host "Engine-Sim $revision ready at $Destination (headless patch + minimal submodule)." -ForegroundColor Green
