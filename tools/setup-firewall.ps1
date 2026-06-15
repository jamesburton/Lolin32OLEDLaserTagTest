#Requires -Version 5.1
<#
.SYNOPSIS
  Check (and optionally fix) the Windows Firewall for LaserTag telemetry.

.DESCRIPTION
  The host's UDP listener (LaserTag.Client / LaserTag.Smoke / TagMonitor) needs an
  inbound allow rule for UDP 4210 to receive device heartbeats and EVT telemetry.
  A missing rule is one common cause of the "REST fine, no heartbeats" symptom
  (REST is PC-initiated so it works regardless); a lossy Wi-Fi link at low RSSI
  is the other. This script rules out the firewall.

  Default action: diagnose, and if the rule is missing, offer to add it,
  self-elevating through UAC. Idempotent and safe to re-run.

.PARAMETER Check
  Diagnose only - no prompts, no elevation. Exit 0 = configured, 1 = needs fixing.

.PARAMETER Yes
  Apply without prompting (still self-elevates if needed).

.PARAMETER Remove
  Remove the rule (self-elevates).

.PARAMETER Apply
  Internal: the target of the elevated relaunch; adds the rule.

.EXAMPLE
  ./tools/setup-firewall.ps1          # check, and offer to fix via UAC
  ./tools/setup-firewall.ps1 -Check   # diagnose only (scriptable; sets exit code)
  ./tools/setup-firewall.ps1 -Remove  # undo
#>
[CmdletBinding()]
param(
  [switch]$Check,
  [switch]$Yes,
  [switch]$Remove,
  [switch]$Apply
)

$ErrorActionPreference = 'Stop'
$Port = 4210
$RuleName = 'LaserTag UDP 4210 (inbound)'

function Test-Admin {
  $id = [System.Security.Principal.WindowsIdentity]::GetCurrent()
  $p = New-Object System.Security.Principal.WindowsPrincipal($id)
  $p.IsInRole([System.Security.Principal.WindowsBuiltInRole]::Administrator)
}

# True if an enabled inbound Allow rule with a UDP 4210 port filter exists.
function Test-Rule {
  $rules = Get-NetFirewallRule -DisplayName $RuleName -ErrorAction SilentlyContinue |
    Where-Object { $_.Enabled -eq 'True' -and $_.Direction -eq 'Inbound' -and $_.Action -eq 'Allow' }
  foreach ($r in $rules) {
    $pf = $r | Get-NetFirewallPortFilter -ErrorAction SilentlyContinue
    foreach ($f in $pf) {
      if ($f.Protocol -eq 'UDP' -and ("$($f.LocalPort)" -eq "$Port")) { return $true }
    }
  }
  return $false
}

# Name(s) of any process currently bound to the UDP port (informational).
function Get-PortHolder {
  try {
    $eps = Get-NetUDPEndpoint -LocalPort $Port -ErrorAction SilentlyContinue
    if ($eps) {
      return (($eps | ForEach-Object {
        (Get-Process -Id $_.OwningProcess -ErrorAction SilentlyContinue).ProcessName
      } | Where-Object { $_ } | Select-Object -Unique) -join ', ')
    }
  } catch { }
  return $null
}

function Add-Rule {
  New-NetFirewallRule -DisplayName $RuleName -Direction Inbound -Protocol UDP `
    -LocalPort $Port -Action Allow -Profile Any | Out-Null
}

# Relaunch this script elevated (UAC) in the given mode (-Apply / -Remove).
function Invoke-Elevated([string]$Mode) {
  $hostExe = (Get-Process -Id $PID).Path
  $argv = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', "`"$PSCommandPath`"", $Mode)
  Start-Process -FilePath $hostExe -Verb RunAs -ArgumentList $argv | Out-Null
}

# --- Elevated relaunch targets ---------------------------------------------
if ($Apply) {
  if (-not (Test-Admin)) { Write-Error 'Apply requires elevation.'; exit 1 }
  if (Test-Rule) { Write-Host "[ok] Rule already present." }
  else { Add-Rule; Write-Host "[added] $RuleName  (UDP $Port inbound allow)." }
  exit 0
}
if ($Remove) {
  if (-not (Test-Admin)) { Invoke-Elevated '-Remove'; Write-Host "Launched elevated remove (accept the UAC prompt)."; exit 0 }
  Get-NetFirewallRule -DisplayName $RuleName -ErrorAction SilentlyContinue | Remove-NetFirewallRule -ErrorAction SilentlyContinue
  Write-Host "[removed] $RuleName"
  exit 0
}

# --- Diagnose ---------------------------------------------------------------
$ruleOk = Test-Rule
$holder = Get-PortHolder
Write-Host "LaserTag firewall check (UDP $Port inbound):"
Write-Host ("  allow rule : {0}" -f ($(if ($ruleOk) { 'present' } else { 'MISSING' })))
if ($holder) { Write-Host "  port bound by: $holder" }

if ($Check) { if ($ruleOk) { exit 0 } else { exit 1 } }
if ($ruleOk) { Write-Host "  -> already configured. Nothing to do."; exit 0 }

Write-Host ""
Write-Host "Planned change (requires admin / UAC):"
Write-Host "  New-NetFirewallRule -DisplayName '$RuleName' -Direction Inbound -Protocol UDP -LocalPort $Port -Action Allow -Profile Any"

if (-not $Yes) {
  $ans = Read-Host "Add this rule now? [y/N]"
  if ($ans -notmatch '^(y|yes)$') { Write-Host "Skipped."; exit 0 }
}

if (Test-Admin) { Add-Rule; Write-Host "[added]." }
else { Invoke-Elevated '-Apply'; Write-Host "Launched elevated helper - accept the UAC prompt to finish." }
