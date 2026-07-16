param(
    [switch]$ForceNuGetDownload
)

$ErrorActionPreference = 'Stop'

$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
$PackagesConfig = Join-Path $Root 'src\app-winui\packages.config'
$PackagesDirectory = Join-Path $Root 'src\app-winui\packages'
$SvgNormalizerDirectory = Join-Path $Root 'src\app-winui\third_party\usvg-normalizer'
$SvgNormalizerManifest = Join-Path $SvgNormalizerDirectory 'Cargo.toml'
$SvgNormalizerTarget = Join-Path $Root 'build\dependencies\usvg-normalizer'
$SvgNormalizerOutput = Join-Path $SvgNormalizerTarget 'release\elmd_svg_normalizer.dll'
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
$PreviousRustFlags = $env:RUSTFLAGS
try {
    $StaticCrtFlag = '-C target-feature=+crt-static'
    if (-not $env:RUSTFLAGS -or -not $env:RUSTFLAGS.Contains($StaticCrtFlag)) {
        $env:RUSTFLAGS = (($env:RUSTFLAGS, $StaticCrtFlag) -join ' ').Trim()
    }
    & cargo build --locked --release --manifest-path $SvgNormalizerManifest --target-dir $SvgNormalizerTarget
    if ($LASTEXITCODE -ne 0) {
        throw "SVG normalizer build failed with exit code $LASTEXITCODE"
    }
}
finally {
    $env:RUSTFLAGS = $PreviousRustFlags
}

if (-not (Test-Path -LiteralPath $SvgNormalizerOutput)) {
    throw "SVG normalizer output was not produced: $SvgNormalizerOutput"
}

Write-Host "Native SVG normalizer built at $SvgNormalizerOutput."
