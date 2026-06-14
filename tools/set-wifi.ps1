<#
.SYNOPSIS
  Provision WiFi credentials on a laser-tag board over serial (TagNet).

.DESCRIPTION
  Sends the TagNet "ssid"/"pass"/"wifi-save" commands over the serial port, so a
  board stores the credentials in NVS and connects. Works for both the Lolin32
  (CP210x) and the ESP32-S3-Matrix (native USB CDC).

.EXAMPLE
  ./set-wifi.ps1 -Port COM14 -Ssid "MyNetwork" -Password "s3cret"

.EXAMPLE
  ./set-wifi.ps1 -Port COM7 -Ssid "Guest WiFi" -Password "p@ss with spaces"
#>
param(
  [Parameter(Mandatory = $true)] [string]$Port,
  [Parameter(Mandatory = $true)] [string]$Ssid,
  [Parameter(Mandatory = $true)] [string]$Password,
  [int]$Baud = 115200
)

$sp = New-Object System.IO.Ports.SerialPort($Port, $Baud)
$sp.ReadTimeout = 1000
$sp.NewLine = "`n"
# Assert DTR so the ESP32-S3 USB-CDC delivers its serial output; keep RTS low so
# the board does not reset into the bootloader.
$sp.DtrEnable = $true
$sp.RtsEnable = $false
$sp.Open()

try {
  Write-Host "Opened $Port; settling..."
  Start-Sleep -Seconds 1
  $sp.DiscardInBuffer()

  $sp.WriteLine("ssid $Ssid");     Start-Sleep -Milliseconds 250
  $sp.WriteLine("pass $Password"); Start-Sleep -Milliseconds 250
  $sp.WriteLine("wifi-save")
  Write-Host "Sent credentials; waiting for connection result (15s)...`n"

  $sw = [Diagnostics.Stopwatch]::StartNew()
  $askedStatus = $false
  while ($sw.Elapsed.TotalSeconds -lt 15) {
    try { Write-Host "  $($sp.ReadLine())" } catch {}
    # Halfway through, explicitly ask for status in case the connect line was missed
    if (-not $askedStatus -and $sw.Elapsed.TotalSeconds -gt 8) {
      $sp.WriteLine("wifi-status"); $askedStatus = $true
    }
  }
}
finally {
  $sp.Close()
}

Write-Host "`nDone. Look for 'TagNet online: ... @ <IP>' or 'wifi-status: connected ip=...'."
Write-Host "If it says connection failed: the ESP32 is 2.4 GHz-only - check the SSID is a 2.4 GHz network."
