param(
    [Parameter(Mandatory = $true)][string]$Port,
    [switch]$Rescue,
    [switch]$SkipBackup
)
$ErrorActionPreference = 'Stop'
# Metalio E-Ink4-Plus (ESP32-S31) application update.
# Keeps NVS/settings, SD and the factory rescue slot untouched unless -Rescue.
# Order: full flash backup -> write app -> write OTA selector -> read back and
# compare SHA-256 -> hard reset. Never touches the NVS or system_font regions.
$nativeRoot = $PSScriptRoot
$projectRoot = Split-Path $nativeRoot -Parent
$build = Join-Path $projectRoot 'build/native-idf'
$rescueBuild = Join-Path $projectRoot 'build/native-rescue'
if (-not $env:THINGSOS_TOOLS) { $env:THINGSOS_TOOLS = 'D:\crossmux\.tools' }
if (-not $env:IDF_TOOLS_PATH) { $env:IDF_TOOLS_PATH = Join-Path $env:THINGSOS_TOOLS 'idf-tools' }
$python = Join-Path $env:IDF_TOOLS_PATH 'python_env/idf6.1_py3.12_env/Scripts/python.exe'
if (-not (Test-Path -LiteralPath $python)) { throw "Python not found: $python" }

$app = Join-Path $build 'ThingsOS_native.bin'
$selector = Join-Path $build 'ota_data_usb.bin'
$rescueImage = Join-Path $rescueBuild 'ThingsOS_rescue.bin'
# partitions.csv: otadata 0xf000/0x2000, rescue 0x20000/0x200000, ota_0 0x220000/0xae0000
$appOffset = 0x220000
$appLimit = 0xae0000
$rescueOffset = 0x20000
$rescueLimit = 0x200000
$otaOffset = 0xf000

function Esptool([string[]]$Arguments) {
    & $python -m esptool --chip esp32s31 --no-stub --port $Port --baud 460800 @Arguments
    if ($LASTEXITCODE -ne 0) { throw "esptool failed: $($Arguments -join ' ')" }
}

$images = @($app, $selector)
if ($Rescue) { $images += $rescueImage }
foreach ($file in $images) {
    if (-not (Test-Path -LiteralPath $file)) { throw "Missing image: $file" }
}
$appBytes = (Get-Item -LiteralPath $app).Length
if ($appBytes -gt $appLimit) { throw "Application image exceeds its partition: $appBytes > $appLimit" }
if ((Get-Item -LiteralPath $selector).Length -ne 0x2000) { throw 'ota_data_usb.bin must be 8192 bytes' }
if ($Rescue -and (Get-Item -LiteralPath $rescueImage).Length -gt $rescueLimit) {
    throw 'Rescue image exceeds its partition'
}

Write-Host ("Application {0} bytes sha256={1}" -f $appBytes, (Get-FileHash -LiteralPath $app -Algorithm SHA256).Hash.ToLowerInvariant())
if ($Rescue) {
    Write-Host ("Rescue      {0} bytes sha256={1}" -f (Get-Item -LiteralPath $rescueImage).Length, (Get-FileHash -LiteralPath $rescueImage -Algorithm SHA256).Hash.ToLowerInvariant())
}

if (-not $SkipBackup) {
    $backupDirectory = Join-Path $projectRoot 'build/device-backup'
    New-Item -ItemType Directory -Force -Path $backupDirectory | Out-Null
    $stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
    $backup = Join-Path $backupDirectory "device-before-flash-$stamp.bin"
    Write-Host "Backing up the whole 16 MiB flash to $backup"
    Esptool @('--before', 'default-reset', '--after', 'no-reset', 'read-flash', '0', '0x1000000', $backup)
    if ((Get-Item -LiteralPath $backup).Length -ne 0x1000000) { throw 'Incomplete flash backup; flashing cancelled.' }
    (Get-FileHash -LiteralPath $backup -Algorithm SHA256).Hash |
        Set-Content -LiteralPath ($backup + '.sha256') -Encoding ascii
    Write-Host "Backup verified: $backup"
}

Esptool @('--before', 'default-reset', '--after', 'no-reset',
    'write-flash', '--flash-mode', 'dio', '--flash-freq', '80m', '--flash-size', '16MB',
    ('0x{0:x}' -f $appOffset), $app)
if ($Rescue) {
    # The factory rescue slot is only refreshed on request; it is never selected
    # as an OTA target and the ROM loader runs it only when otadata is invalid.
    Esptool @('--before', 'no-reset', '--after', 'no-reset',
        'write-flash', ('0x{0:x}' -f $rescueOffset), $rescueImage)
}
# The selector must be written after the application: a power cut here can then
# only leave the previous selector pointing at a complete image.
Esptool @('--before', 'no-reset', '--after', 'no-reset', 'write-flash', ('0x{0:x}' -f $otaOffset), $selector)

$verifyDirectory = Join-Path $projectRoot 'build/device-verify'
New-Item -ItemType Directory -Force -Path $verifyDirectory | Out-Null
$check = Join-Path $verifyDirectory 'app-readback.bin'
Esptool @('--before', 'no-reset', '--after', 'no-reset', 'read-flash',
    ('0x{0:x}' -f $appOffset), ('0x{0:x}' -f $appBytes), $check)
if ((Get-FileHash -LiteralPath $check -Algorithm SHA256).Hash -ne (Get-FileHash -LiteralPath $app -Algorithm SHA256).Hash) {
    throw 'Application read-back mismatch; do not trust this write.'
}
if ($Rescue) {
    $rescueBytes = (Get-Item -LiteralPath $rescueImage).Length
    $check = Join-Path $verifyDirectory 'rescue-readback.bin'
    Esptool @('--before', 'no-reset', '--after', 'no-reset', 'read-flash',
        ('0x{0:x}' -f $rescueOffset), ('0x{0:x}' -f $rescueBytes), $check)
    if ((Get-FileHash -LiteralPath $check -Algorithm SHA256).Hash -ne (Get-FileHash -LiteralPath $rescueImage -Algorithm SHA256).Hash) {
        throw 'Rescue read-back mismatch; do not trust this write.'
    }
}
$check = Join-Path $verifyDirectory 'otadata-readback.bin'
Esptool @('--before', 'no-reset', '--after', 'no-reset', 'read-flash',
    ('0x{0:x}' -f $otaOffset), '0x2000', $check)
if ((Get-FileHash -LiteralPath $check -Algorithm SHA256).Hash -ne (Get-FileHash -LiteralPath $selector -Algorithm SHA256).Hash) {
    throw 'OTA selector read-back mismatch.'
}

Esptool @('--before', 'no-reset', '--after', 'hard-reset', 'read-mac')
Write-Host 'Write verified by read-back. Boot log must show: Panel waveform GC16=48 GL16=48 DU=20 phases'
Write-Host 'Settings in NVS, SD content and system_font were not written.'
