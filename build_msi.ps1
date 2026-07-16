[CmdletBinding()]
param(
    [ValidatePattern('^\d+\.\d+\.\d+$')]
    [string]$Version = '0.1.0',

    [ValidateSet('x64')]
    [string]$Platform = 'x64',

    [switch]$SkipDependencySetup,

    [switch]$Clean
)

$ErrorActionPreference = 'Stop'

$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
$BuildRoot = Join-Path $Root 'build'
$InstallerBuildRoot = Join-Path $BuildRoot 'installer'
$InstallerProject = Join-Path $Root 'installer\Folia.Installer.wixproj'
$PayloadGenerator = Join-Path $Root 'tools\Generate-WixPayload.ps1'
$ProgramRoot = Join-Path $Root "build\app-winui\bin\$Platform\Release"
$AssetsRoot = Join-Path $Root 'src\app-winui\Assets'
$GeneratedPayload = Join-Path $InstallerBuildRoot 'generated\Payload.wxs'
$InstallerOutput = Join-Path $InstallerBuildRoot 'bin'
$InstallerIntermediate = Join-Path $InstallerBuildRoot 'obj'
$ExpectedMsi = Join-Path $InstallerOutput "Folia-$Version-$Platform.msi"
$LicenseRtf = Join-Path $Root 'installer\License.rtf'
$ProductIcon = Join-Path $Root 'src\app-winui\Assets\branding\Folia.ico'

if ($Clean -and (Test-Path -LiteralPath $InstallerBuildRoot)) {
    $resolvedBuildRoot = [System.IO.Path]::GetFullPath($BuildRoot) + [System.IO.Path]::DirectorySeparatorChar
    $resolvedInstallerRoot = [System.IO.Path]::GetFullPath($InstallerBuildRoot)
    if (-not $resolvedInstallerRoot.StartsWith($resolvedBuildRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to remove a directory outside the build tree: $resolvedInstallerRoot"
    }
    Remove-Item -LiteralPath $resolvedInstallerRoot -Recurse -Force
}

if (-not $SkipDependencySetup) {
    & (Join-Path $Root 'setup.ps1')
    if ($LASTEXITCODE -ne 0) { throw "Dependency setup failed with exit code $LASTEXITCODE" }
}

$appBuildArguments = @{
    Configuration = 'Release'
    Platform = $Platform
    AssetsDirectory = '{LocalAppData}\Folia\Assets'
    SelfContained = $true
    SkipAssetsDeployment = $true
}
if ($Clean) { $appBuildArguments.Clean = $true }
& (Join-Path $Root 'build_app.ps1') @appBuildArguments
if ($LASTEXITCODE -ne 0) { throw "Folia application build failed with exit code $LASTEXITCODE" }

if (-not (Test-Path -LiteralPath (Join-Path $ProgramRoot 'Folia.exe') -PathType Leaf)) {
    throw "Folia executable was not produced under $ProgramRoot"
}

& $PayloadGenerator -ProgramRoot $ProgramRoot -AssetsRoot $AssetsRoot -OutputPath $GeneratedPayload
if ($LASTEXITCODE -ne 0) { throw "WiX payload generation failed with exit code $LASTEXITCODE" }

New-Item -ItemType Directory -Force -Path $InstallerOutput, $InstallerIntermediate | Out-Null
$dotnetArguments = @(
    'build', $InstallerProject,
    '--nologo',
    '--configuration', 'Release',
    "-p:FoliaProductVersion=$Version",
    "-p:FoliaGeneratedPayload=$GeneratedPayload",
    "-p:FoliaInstallerOutputPath=$InstallerOutput\",
    "-p:FoliaInstallerIntermediatePath=$InstallerIntermediate\",
    "-p:FoliaLicenseRtf=$LicenseRtf",
    "-p:FoliaProductIcon=$ProductIcon"
)
& dotnet @dotnetArguments
if ($LASTEXITCODE -ne 0) { throw "WiX build failed with exit code $LASTEXITCODE" }

if (-not (Test-Path -LiteralPath $ExpectedMsi -PathType Leaf)) {
    throw "MSI output was not produced: $ExpectedMsi"
}

$size = (Get-Item -LiteralPath $ExpectedMsi).Length
Write-Host "Folia MSI: $ExpectedMsi ($([Math]::Round($size / 1MB, 1)) MiB)"
