param([switch]$Rescue, [string]$Version = '')
$ErrorActionPreference = 'Stop'
$projectVersion = (Get-Content -LiteralPath (Join-Path $PSScriptRoot 'VERSION') -Raw).Trim()
if (-not $Version) { $Version = $projectVersion }
if ($Version -notmatch '^\d+\.\d+\.\d+(?:-[a-zA-Z0-9][a-zA-Z0-9.-]*)?$' -or $Version.Length -gt 31) {
 throw 'Version must fit the ESP application descriptor and use a safe semantic version.'
}
if ($Rescue -and $Version -ne $projectVersion) { throw 'Custom versions are supported for the main application only.' }
$nativeRoot = $PSScriptRoot
$projectRoot = Split-Path $nativeRoot -Parent
# ESP-IDF v6.1 with ESP32-S31 support. Override with IDF_PATH / IDF_TOOLS_PATH.
if (-not $env:THINGSOS_TOOLS) { $env:THINGSOS_TOOLS = 'D:\crossmux\.tools' }
if (-not $env:IDF_PATH) { $env:IDF_PATH = Join-Path $env:THINGSOS_TOOLS 'esp-idf' }
if (-not $env:IDF_TOOLS_PATH) { $env:IDF_TOOLS_PATH = Join-Path $env:THINGSOS_TOOLS 'idf-tools' }
$env:PATH = "$(Join-Path $env:IDF_TOOLS_PATH 'python_env/idf6.1_py3.12_env/Scripts');$env:PATH"
$env:IDF_TARGET = 'esp32s31'
$env:PYTHONUTF8 = '1'
. (Join-Path $env:IDF_PATH 'export.ps1')
python (Join-Path $nativeRoot 'prepare_dependencies.py')
if ($LASTEXITCODE -ne 0) { throw 'Dependency integrity verification failed.' }
if (-not $Rescue) {
 python (Join-Path $nativeRoot 'prepare_assets.py')
 if ($LASTEXITCODE -ne 0) { throw 'Default font preparation failed.' }
}
$projectDirectory = if ($Rescue) { 'rescue' } else { 'firmware' }
$outputDirectory = if ($Rescue) { Join-Path $projectRoot 'build/native-rescue' } else { Join-Path $projectRoot 'build/native-idf' }
if (-not $Rescue -and $Version -ne $projectVersion) { $outputDirectory = Join-Path $projectRoot "build/native-idf-$Version" }
Push-Location (Join-Path $nativeRoot $projectDirectory)
try {
 if ($Rescue) {
  python (Join-Path $env:IDF_PATH 'tools/idf.py') -B $outputDirectory reconfigure build
 } else {
  python (Join-Path $env:IDF_PATH 'tools/idf.py') -B $outputDirectory "-DTHINGSOS_BUILD_VERSION=$Version" reconfigure build
 }
 if ($LASTEXITCODE -ne 0) { throw 'Metalio firmware build failed.' }
 if (-not $Rescue) {
  python (Join-Path $nativeRoot 'verify_jpeg_link.py') (Join-Path $outputDirectory 'ThingsOS_native.map')
  if ($LASTEXITCODE -ne 0) { throw 'JPEG decoder linked outside the application; firmware rejected.' }
 }
 Write-Host 'Metalio development image only. Device acceptance pending. No publication or flashing.'
} finally { Pop-Location }
