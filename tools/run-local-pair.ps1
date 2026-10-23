param([switch]$Build)
$ErrorActionPreference='Stop'
$sourceRoot=Split-Path -Parent $PSScriptRoot
$taskRoot=Split-Path -Parent (Split-Path -Parent $sourceRoot)
if($Build) { & (Join-Path $PSScriptRoot 'build-coop.ps1') -Test }
$builtExecutable=Join-Path $taskRoot 'work/revc-build/src/RelWithDebInfo/reVC.exe'
$runtimeRoot=Join-Path $taskRoot 'work/revc-live-test'
foreach($role in @('Host','Guest')) {
    $gameRoot=Join-Path $runtimeRoot $role
    if(-not (Test-Path -LiteralPath (Join-Path $gameRoot 'models/gta3.img'))) { throw "Missing local game resources: $gameRoot" }
    $gameExecutable=Join-Path $gameRoot 'reVC.exe'
    $ownedProcesses=Get-Process reVC -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $gameExecutable }
    if($ownedProcesses) { throw "$role is still running; close that test window before replacing its executable" }
    Copy-Item -LiteralPath $builtExecutable -Destination $gameExecutable
    Copy-Item -LiteralPath ([IO.Path]::ChangeExtension($builtExecutable,'.pdb')) -Destination (Join-Path $gameRoot 'reVC.pdb')
}
$hostRoot=Join-Path $runtimeRoot 'Host'
$hostProcess=Start-Process -FilePath (Join-Path $hostRoot 'reVC.exe') -WorkingDirectory $hostRoot -WindowStyle Normal -PassThru
$hostProcess.Id | Set-Content (Join-Path $taskRoot 'work/revc-host.pid')
$deadline=[DateTime]::UtcNow.AddSeconds(20)
$ready=$false
while([DateTime]::UtcNow -lt $deadline) {
    if($hostProcess.HasExited) { throw 'Native host exited before its world became ready' }
    $statusPath=Join-Path $hostRoot 'vc-coop-state.json'
    if(Test-Path -LiteralPath $statusPath) {
        try {
            $fresh=(Get-Item -LiteralPath $statusPath).LastWriteTimeUtc -gt $hostProcess.StartTime.ToUniversalTime()
            $status=Get-Content -Raw -LiteralPath $statusPath | ConvertFrom-Json
            if($fresh -and $status.online -and $status.ready) { $ready=$true; break }
        } catch { }
    }
    Start-Sleep -Milliseconds 100
}
if(-not $ready) { throw 'Host readiness timed out; see vc-coop-native.log' }
$guestRoot=Join-Path $runtimeRoot 'Guest'
$guestProcess=Start-Process -FilePath (Join-Path $guestRoot 'reVC.exe') -WorkingDirectory $guestRoot -WindowStyle Normal -PassThru
$guestProcess.Id | Set-Content (Join-Path $taskRoot 'work/revc-guest.pid')
Write-Output "Native reVC Host PID $($hostProcess.Id); Guest PID $($guestProcess.Id)"
