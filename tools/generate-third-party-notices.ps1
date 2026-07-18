param(
    [string]$OutputPath
)

$ErrorActionPreference = 'Stop'

$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
if (-not $OutputPath) {
    $OutputPath = Join-Path $Root 'src\app-winui\Assets\licenses\THIRD-PARTY-NOTICES.txt'
}

$Builder = [System.Text.StringBuilder]::new()

function Add-Line([string]$Value = '') {
    [void]$Builder.AppendLine($Value)
}

function Add-LicenseFile([string]$Title, [string]$Path) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "Missing license file for ${Title}: $Path"
    }
    Add-Line ('=' * 78)
    Add-Line $Title
    Add-Line ('=' * 78)
    Add-Line
    Add-Line ([IO.File]::ReadAllText($Path).TrimEnd())
    Add-Line
}

function Find-LicenseFiles([string]$Directory) {
    if (-not (Test-Path -LiteralPath $Directory -PathType Container)) { return @() }
    return @(Get-ChildItem -LiteralPath $Directory -File | Where-Object {
        $_.Name -match '^(LICENSE|LICENCE|COPYING|COPYRIGHT|NOTICE)(\.|$|-)' -or
        $_.Name -match '^(LICENSE|LICENCE)-(MIT|APACHE|BSD|ZLIB|UNICODE)'
    } | Sort-Object Name)
}

Add-Line 'Folia third-party notices'
Add-Line
Add-Line 'This file is generated from the dependency versions locked in this repository.'
Add-Line 'Folia itself is licensed separately under the MIT License in LICENSE.'
Add-Line

Add-LicenseFile 'Boost.UT (test dependency) — Boost Software License 1.0' `
    (Join-Path $Root 'third_party\ut\LICENSE.md')
Add-LicenseFile 'SRELL — BSD 2-Clause License' `
    (Join-Path $Root 'third_party\srell\LICENSE')
Add-LicenseFile 'QuickJS — MIT License' `
    (Join-Path $Root 'src\app-winui\third_party\quickjs\LICENSE')
Add-LicenseFile 'MathJax and MathJax NewCM font — Apache License 2.0' `
    (Join-Path $Root 'src\app-winui\Assets\mathjax\LICENSE-MathJax.txt')
Add-LicenseFile 'Native SVG normalizer wrapper — MIT License' `
    (Join-Path $Root 'src\app-winui\third_party\usvg-normalizer\LICENSE-MIT')
Add-LicenseFile 'Tree-sitter Unicode support data' `
    (Join-Path $Root 'third_party\tree-sitter\runtime\unicode\LICENSE')

$TreeSitterLicenses = Join-Path $Root 'third_party\tree-sitter\licenses'
Get-ChildItem -LiteralPath $TreeSitterLicenses -File | Sort-Object Name | ForEach-Object {
    Add-LicenseFile "Tree-sitter component — $($_.BaseName)" $_.FullName
}

$CargoManifest = Join-Path $Root 'src\app-winui\third_party\usvg-normalizer\Cargo.toml'
$CargoJson = & cargo metadata --manifest-path $CargoManifest --format-version 1 --locked
if ($LASTEXITCODE -ne 0) { throw "cargo metadata failed with exit code $LASTEXITCODE" }
$CargoMetadata = $CargoJson | ConvertFrom-Json
foreach ($Package in @($CargoMetadata.packages | Sort-Object name, version)) {
    $PackageDirectory = Split-Path -Parent $Package.manifest_path
    $LicenseFiles = @(Find-LicenseFiles $PackageDirectory)
    if ($LicenseFiles.Count -eq 0) {
        Add-Line ('=' * 78)
        Add-Line "Rust crate: $($Package.name) $($Package.version)"
        Add-Line ('=' * 78)
        Add-Line
        Add-Line "Declared license: $($Package.license)"
        Add-Line "Source: $($Package.repository)"
        Add-Line
        continue
    }
    foreach ($LicenseFile in $LicenseFiles) {
        Add-LicenseFile `
            "Rust crate: $($Package.name) $($Package.version) — $($LicenseFile.Name)" `
            $LicenseFile.FullName
    }
}

$PackageConfig = [xml][IO.File]::ReadAllText((Join-Path $Root 'src\app-winui\packages.config'))
$NuGetRoot = Join-Path $Root 'src\app-winui\packages'
foreach ($Package in @($PackageConfig.packages.package | Sort-Object id, version)) {
    $PackageDirectory = Join-Path $NuGetRoot "$($Package.id).$($Package.version)"
    $LicenseFiles = @(Find-LicenseFiles $PackageDirectory)
    foreach ($LicenseFile in $LicenseFiles) {
        Add-LicenseFile `
            "NuGet package: $($Package.id) $($Package.version) — $($LicenseFile.Name)" `
            $LicenseFile.FullName
    }
}

$NpmDirectory = Join-Path $Root 'tools\mathjax-bundle'
$NpmLockPath = Join-Path $NpmDirectory 'package-lock.json'
if (Test-Path -LiteralPath $NpmLockPath) {
    if (-not (Get-Command node -ErrorAction SilentlyContinue)) {
        throw 'Node.js is required to enumerate npm dependency licenses.'
    }
    $NpmEntries = & node (Join-Path $Root 'tools\list-npm-packages.cjs') $NpmLockPath
    if ($LASTEXITCODE -ne 0) { throw "npm dependency enumeration failed with exit code $LASTEXITCODE" }
    foreach ($Entry in @($NpmEntries | Sort-Object)) {
        $Fields = $Entry -split "`t", 2
        $PackagePath = $Fields[0]
        $PackageVersion = if ($Fields.Count -gt 1) { $Fields[1] } else { '' }
        $PackageDirectory = Join-Path $NpmDirectory ($PackagePath -replace '/', '\')
        foreach ($LicenseFile in @(Find-LicenseFiles $PackageDirectory)) {
            Add-LicenseFile `
                "npm package: $($PackagePath -replace '^node_modules/', '') $PackageVersion — $($LicenseFile.Name)" `
                $LicenseFile.FullName
        }
    }
}

$OutputDirectory = Split-Path -Parent $OutputPath
[IO.Directory]::CreateDirectory($OutputDirectory) | Out-Null
[IO.File]::WriteAllText(
    $OutputPath,
    $Builder.ToString(),
    [Text.UTF8Encoding]::new($false))

Write-Host "Generated third-party notices: $OutputPath"
