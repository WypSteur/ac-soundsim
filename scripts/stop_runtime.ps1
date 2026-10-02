$ErrorActionPreference = 'Stop'
try {
  $stopSignal = [Threading.EventWaitHandle]::OpenExisting('Local\AcTools.ACSoundSim.Status.v1.Stop')
  $stopSignal.Set() | Out-Null
  $stopSignal.Dispose()
  Write-Host 'Graceful SoundSim stop requested.'
} catch [Threading.WaitHandleCannotBeOpenedException] { Write-Host 'No SoundSim runtime stop event exists.' }
