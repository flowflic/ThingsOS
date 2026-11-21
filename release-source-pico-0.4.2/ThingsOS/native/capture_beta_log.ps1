# Read USB console only; do not send commands or request reset.
param([string]$Port, [int]$Minutes = 15)
$ErrorActionPreference = 'Stop'
if (-not $Port) {
 Write-Host ('Available ports: ' + ([IO.Ports.SerialPort]::GetPortNames() -join ', '))
 $Port = Read-Host 'Enter the Pico COM port, for example COM4'
}
$logPath = Join-Path $PSScriptRoot ('pico-beta-' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '.log')
$serial = [IO.Ports.SerialPort]::new($Port,115200,'None',8,'One')
$serial.Encoding = [Text.UTF8Encoding]::new($false)
$serial.DtrEnable = $false
$serial.RtsEnable = $false
$writer = $null
try {
 $serial.Open()
 $writer = [IO.StreamWriter]::new($logPath,$false,[Text.UTF8Encoding]::new($false))
 $writer.AutoFlush = $true
 $writer.WriteLine('Host capture started: ' + (Get-Date -Format o))
 Write-Host "Recording to $logPath"
 Write-Host "Read the book on the device. Press Ctrl+C to stop; maximum $Minutes minutes."
 $deadline = (Get-Date).AddMinutes($Minutes)
 while ((Get-Date) -lt $deadline) {
  $chunk = $serial.ReadExisting()
  if ($chunk) { $writer.Write($chunk); [Console]::Write($chunk) }
  Start-Sleep -Milliseconds 100
 }
} finally {
 if ($writer) { $writer.Dispose() }
 if ($serial.IsOpen) { $serial.Close() }
 $serial.Dispose()
 Write-Host "Log saved: $logPath"
}
