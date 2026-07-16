[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug',

    [ValidateSet('x64')]
    [string]$Platform = 'x64',

    [string]$AssetsDirectory,

    [switch]$Clean
)

$ErrorActionPreference = 'Stop'

$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
$Project = Join-Path $Root 'src\app-winui\Folia.vcxproj'
$VsWhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'

if (-not (Test-Path -LiteralPath $Project -PathType Leaf)) {
    throw "Missing WinUI project: $Project"
}
if (-not (Test-Path -LiteralPath $VsWhere -PathType Leaf)) {
    throw "Visual Studio locator was not found: $VsWhere"
}

$Installation = & $VsWhere -latest -products * -requires Microsoft.Component.MSBuild -property installationPath
if ($LASTEXITCODE -ne 0 -or -not $Installation) {
    throw 'A Visual Studio installation with MSBuild was not found.'
}

$MSBuild = Join-Path ($Installation | Select-Object -First 1) 'MSBuild\Current\Bin\MSBuild.exe'
if (-not (Test-Path -LiteralPath $MSBuild -PathType Leaf)) {
    throw "MSBuild was not found: $MSBuild"
}

$Properties = @(
    "/p:Configuration=$Configuration",
    "/p:Platform=$Platform"
)
if ($AssetsDirectory) {
    $Properties += "/p:FoliaAssetsDirectory=$AssetsDirectory"
}

if ($Clean) {
    & $MSBuild $Project /nologo /m /v:minimal /t:Clean @Properties
    if ($LASTEXITCODE -ne 0) {
        throw "Folia clean failed with exit code $LASTEXITCODE"
    }
}

& $MSBuild $Project /nologo /m /v:minimal /t:Build @Properties
if ($LASTEXITCODE -ne 0) {
    throw "Folia build failed with exit code $LASTEXITCODE"
}

$Output = Join-Path $Root "build\app-winui\bin\$Platform\$Configuration"
Write-Host "Folia $Configuration build: $Output"
