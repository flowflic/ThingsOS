param([switch]$Rescue, [string]$Version = '0.4.2')
$ErrorActionPreference = 'Stop'
if ($Version -notmatch '^\d+\.\d+\.\d+(?:-[a-zA-Z0-9][a-zA-Z0-9.-]*)?$' -or $Version.Length -gt 31) {
 throw 'Version must fit the ESP application descriptor and use a safe semantic version.'
}
if ($Rescue -and $Version -ne '0.4.2') { throw 'Custom versions are supported for the main application only.' }
$nativeRoot = $PSScriptRoot
$taskWorkspace = Split-Path (Split-Path $nativeRoot -Parent) -Parent
$env:IDF_PATH = Join-Path $taskWorkspace '.tools/esp-idf'
$env:IDF_TOOLS_PATH = Join-Path $taskWorkspace '.tools/idf-tools'
$env:PATH = "$(Join-Path $env:IDF_TOOLS_PATH 'python_env/idf6.1_py3.12_env/Scripts');$env:PATH"
$env:IDF_TARGET = 'esp32s3'
$env:PYTHONUTF8 = '1'
. (Join-Path $env:IDF_PATH 'export.ps1')
python (Join-Path $nativeRoot 'prepare_dependencies.py')
if ($LASTEXITCODE -ne 0) { throw 'Dependency integrity verification failed.' }
if (-not $Rescue) {
 python (Join-Path $nativeRoot 'prepare_assets.py')
 if ($LASTEXITCODE -ne 0) { throw 'Default font preparation failed.' }
}
$projectDirectory = if ($Rescue) { 'rescue' } else { 'firmware' }
$outputDirectory = if ($Rescue) { '../../build/native-rescue' } else { '../../build/native-idf' }
if (-not $Rescue -and $Version -ne '0.4.2') { $outputDirectory = "../../build/native-idf-$Version" }
Push-Location (Join-Path $nativeRoot $projectDirectory)
try {
 if ($Rescue) {
  python (Join-Path $env:IDF_PATH 'tools/idf.py') -B $outputDirectory reconfigure build
 } else {
  python (Join-Path $env:IDF_PATH 'tools/idf.py') -B $outputDirectory "-DTHINGSOS_BUILD_VERSION=$Version" reconfigure build
 }
 if ($LASTEXITCODE -ne 0) { throw 'Independent firmware build failed.' }
 Write-Host 'Development image only. Device acceptance pending. No publication or flashing.'
} finally { Pop-Location }
