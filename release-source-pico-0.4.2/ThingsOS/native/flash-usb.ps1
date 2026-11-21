param([Parameter(Mandatory=$true)][string]$Port)
$ErrorActionPreference = 'Stop'
$taskWorkspace = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$python = Join-Path $taskWorkspace '.tools/idf-tools/python_env/idf6.1_py3.12_env/Scripts/python.exe'
$bundle = Join-Path (Split-Path $PSScriptRoot -Parent) 'build/native-usb'
$manifest = Get-Content -LiteralPath (Join-Path $bundle 'manifest.json') -Raw | ConvertFrom-Json
$image = Join-Path $bundle $manifest.file
if ((Get-FileHash -LiteralPath $image -Algorithm SHA256).Hash.ToLowerInvariant() -ne $manifest.sha256) {
    throw 'USB image hash mismatch. Run verify_build.py and package_usb.py again.'
}
# Explicit invocation is a complete installation: initializes internal settings;
# it never formats or writes the SD card. Save the old flash before any write.
$backup = Join-Path $bundle ('device-before-usb-' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '.bin')
& $python -m esptool --chip esp32s3 --port $Port --baud 460800 --after no-reset read-flash 0 0x1000000 $backup
if ($LASTEXITCODE -ne 0) { throw 'Flash backup failed; installation cancelled.' }
if ((Get-Item -LiteralPath $backup).Length -ne 0x1000000) { throw 'Incomplete flash backup; installation cancelled.' }
(Get-FileHash -LiteralPath $backup -Algorithm SHA256).Hash | Set-Content -LiteralPath ($backup + '.sha256') -Encoding ascii
& $python -m esptool --chip esp32s3 --port $Port --baud 460800 --after hard-reset write-flash --flash-mode dio --flash-freq 80m --flash-size 16MB 0x0 $image
if ($LASTEXITCODE -ne 0) { throw 'USB installation failed; backup retained.' }
Write-Host "Complete USB installation finished. Backup: $backup"
