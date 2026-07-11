param(
    [switch]$ForceNuGetDownload
)

$ErrorActionPreference = 'Stop'

$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
$PackagesConfig = Join-Path $Root 'src\app-winui\packages.config'
$PackagesDirectory = Join-Path $Root 'src\app-winui\packages'
$SvgNormalizerDirectory = Join-Path $Root 'src\app-winui\third_party\usvg-normalizer'
$SvgNormalizerManifest = Join-Path $SvgNormalizerDirectory 'Cargo.toml'
$SvgNormalizerOutput = Join-Path $SvgNormalizerDirectory 'target\release\elmd_svg_normalizer.dll'
$SvgNormalizerBin = Join-Path $SvgNormalizerDirectory 'bin\x64\elmd_svg_normalizer.dll'
$ToolDirectory = Join-Path $Root '.tools'
$NuGetPath = Join-Path $ToolDirectory 'nuget.exe'
$NuGetUrl = 'https://dist.nuget.org/win-x86-commandline/latest/nuget.exe'

if (-not (Test-Path -LiteralPath $PackagesConfig)) {
    throw "Missing NuGet manifest: $PackagesConfig"
}

New-Item -ItemType Directory -Force -Path $ToolDirectory | Out-Null

if ($ForceNuGetDownload -or -not (Test-Path -LiteralPath $NuGetPath)) {
    Write-Host "Downloading NuGet command-line client..."
    Invoke-WebRequest -Uri $NuGetUrl -OutFile $NuGetPath
}

New-Item -ItemType Directory -Force -Path $PackagesDirectory | Out-Null

Write-Host "Restoring WinUI dependencies to $PackagesDirectory..."
& $NuGetPath restore $PackagesConfig `
    -PackagesDirectory $PackagesDirectory `
    -NonInteractive `
    -Verbosity normal

if ($LASTEXITCODE -ne 0) {
    throw "NuGet restore failed with exit code $LASTEXITCODE"
}

Write-Host 'NuGet dependencies restored.'

if (-not (Get-Command cargo -ErrorAction SilentlyContinue)) {
    throw 'Rust cargo is required to build the native SVG normalizer.'
}

Write-Host 'Building the native SVG normalizer...'
& cargo build --locked --release --manifest-path $SvgNormalizerManifest
if ($LASTEXITCODE -ne 0) {
    throw "SVG normalizer build failed with exit code $LASTEXITCODE"
}

if (-not (Test-Path -LiteralPath $SvgNormalizerOutput)) {
    throw "SVG normalizer output was not produced: $SvgNormalizerOutput"
}

New-Item -ItemType Directory -Force -Path (Split-Path -Parent $SvgNormalizerBin) | Out-Null
Copy-Item -LiteralPath $SvgNormalizerOutput -Destination $SvgNormalizerBin -Force
Write-Host "Native SVG normalizer copied to $SvgNormalizerBin."
