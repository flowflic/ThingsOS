$ErrorActionPreference = 'Stop'

$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$BuildRoot = [System.IO.Path]::GetFullPath((Join-Path $ProjectRoot 'build'))
$SiteOutput = Join-Path $BuildRoot 'native-site-current'
$ReleaseManifest = Join-Path $SiteOutput 'release.json'
$VercelProjectRoot = Join-Path $BuildRoot 'native-site-vercel'
$StaticRoot = Join-Path $VercelProjectRoot 'site'
$VercelConfigSource = Join-Path $PSScriptRoot 'site\vercel-project.json'
$VercelIgnoreSource = Join-Path $PSScriptRoot 'site\vercel-project.ignore'

if (-not (Test-Path -LiteralPath $ReleaseManifest -PathType Leaf)) {
    throw "Packaged site not found at '$SiteOutput'. Build and package the current site first."
}

if (-not (Test-Path -LiteralPath $VercelConfigSource -PathType Leaf) -or
    -not (Test-Path -LiteralPath $VercelIgnoreSource -PathType Leaf)) {
    throw 'Vercel project configuration source files are missing from native/site.'
}

try {
    $Release = Get-Content -LiteralPath $ReleaseManifest -Raw -Encoding UTF8 | ConvertFrom-Json
} catch {
    throw "Could not read the packaged release manifest: $($_.Exception.Message)"
}

if (-not $Release.version -or -not $Release.name) {
    throw 'The packaged release manifest is missing the site name or version.'
}

$BuildPrefix = $BuildRoot.TrimEnd('\') + '\'
$StageRootFull = [System.IO.Path]::GetFullPath($VercelProjectRoot)
if (-not $StageRootFull.StartsWith($BuildPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing to prepare Vercel staging outside the build directory: '$StageRootFull'."
}

if (Test-Path -LiteralPath $VercelProjectRoot -PathType Container) {
    $StageResolved = (Resolve-Path -LiteralPath $VercelProjectRoot).Path
    if (-not $StageResolved.StartsWith($BuildPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to use Vercel staging path outside the build directory: '$StageResolved'."
    }
} else {
    New-Item -ItemType Directory -Path $VercelProjectRoot -Force | Out-Null
}

$StagePrefix = $StageRootFull.TrimEnd('\') + '\'
if (Test-Path -LiteralPath $StaticRoot) {
    $StaticResolved = (Resolve-Path -LiteralPath $StaticRoot).Path
    if (-not $StaticResolved.StartsWith($StagePrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to replace Vercel static output outside the staging project: '$StaticResolved'."
    }
    Remove-Item -LiteralPath $StaticResolved -Recurse -Force
}
New-Item -ItemType Directory -Path $StaticRoot -Force | Out-Null

Get-ChildItem -LiteralPath $SiteOutput -Force | ForEach-Object {
    Copy-Item -LiteralPath $_.FullName -Destination $StaticRoot -Recurse -Force
}

# Netlify interprets this file; Vercel receives equivalent headers from its project config.
$CopiedHeaders = Join-Path $StaticRoot '_headers'
if (Test-Path -LiteralPath $CopiedHeaders -PathType Leaf) {
    Remove-Item -LiteralPath $CopiedHeaders -Force
}

Copy-Item -LiteralPath $VercelConfigSource -Destination (Join-Path $VercelProjectRoot 'vercel.json') -Force
Copy-Item -LiteralPath $VercelIgnoreSource -Destination (Join-Path $VercelProjectRoot '.vercelignore') -Force

$StagedManifest = Join-Path $StaticRoot 'release.json'
$StagedRelease = Get-Content -LiteralPath $StagedManifest -Raw -Encoding UTF8 | ConvertFrom-Json
if ($StagedRelease.version -ne $Release.version) {
    throw "Staged site version mismatch: expected $($Release.version), found $($StagedRelease.version)."
}

$Vercel = Get-Command 'vercel' -ErrorAction SilentlyContinue
$Npx = Get-Command 'npx' -ErrorAction SilentlyContinue
if (-not $Vercel -and -not $Npx) {
    throw 'Neither Vercel CLI nor npx is installed. Install Node.js/npm or the Vercel CLI, then rerun.'
}

Push-Location $VercelProjectRoot
try {
    $LinkedProject = Join-Path $VercelProjectRoot '.vercel\project.json'
    if (-not (Test-Path -LiteralPath $LinkedProject -PathType Leaf)) {
        throw "Staging directory is prepared. Link it once with: npx --yes vercel@64.5.0 link --cwd `"$VercelProjectRoot`" --scope <your-scope> --project thingsos --yes"
    }

    Write-Host "Prepared $($Release.name) site package version $($Release.version) for Vercel."
    if ($Vercel) {
        & $Vercel.Source project inspect --non-interactive
        if ($LASTEXITCODE -ne 0) { throw 'Vercel project inspection failed.' }
        & $Vercel.Source deploy --prod --yes
    } else {
        & $Npx.Source --yes 'vercel@64.5.0' project inspect --non-interactive
        if ($LASTEXITCODE -ne 0) { throw 'Vercel project inspection failed.' }
        & $Npx.Source --yes 'vercel@64.5.0' deploy --prod --yes
    }
    if ($LASTEXITCODE -ne 0) {
        throw "Vercel production deployment failed with exit code $LASTEXITCODE."
    }
} finally {
    Pop-Location
}
