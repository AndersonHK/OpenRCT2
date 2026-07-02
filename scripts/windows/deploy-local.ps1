[CmdletBinding()]
param(
    [string]$Destination = "D:\Games\Independent\OpenRCT2Mod",

    [string]$BaseDeployment = "D:\Games\Independent\OpenRCT2",

    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Release",

    [ValidateSet("x64", "Win32", "ARM64")]
    [string]$Platform = "x64",

    [string]$VCToolsVersion = "14.44.35207",

    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$destinationPath = [System.IO.Path]::GetFullPath($Destination)
$allowedRoot = [System.IO.Path]::GetFullPath("D:\Games\Independent\")

if (-not $destinationPath.StartsWith($allowedRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing to deploy outside $allowedRoot. Destination was $destinationPath."
}
if ($destinationPath.Equals($allowedRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing to deploy directly into $allowedRoot."
}

if (-not $SkipBuild) {
    & (Join-Path $PSScriptRoot "build-local.ps1") -Configuration $Configuration -Platform $Platform -VCToolsVersion $VCToolsVersion
}

$sourceBin = Join-Path $repoRoot "bin"
$sourceData = Join-Path $sourceBin "data"
$sourceExe = Join-Path $sourceBin "openrct2.exe"
$sourceCli = Join-Path $sourceBin "openrct2-cli.exe"

if (-not (Test-Path -LiteralPath $sourceExe)) {
    throw "Built executable not found: $sourceExe"
}
if (-not (Test-Path -LiteralPath $sourceData)) {
    throw "Built data directory not found: $sourceData"
}

New-Item -ItemType Directory -Path $destinationPath -Force | Out-Null

$copyFromBase = @(
    "changelog.txt",
    "contributors.md",
    "licence.txt",
    "openrct2.d.ts",
    "PRIVACY.md",
    "readme.txt",
    "scripting.md"
)

if (Test-Path -LiteralPath $BaseDeployment) {
    foreach ($fileName in $copyFromBase) {
        $baseFile = Join-Path $BaseDeployment $fileName
        if (Test-Path -LiteralPath $baseFile) {
            Copy-Item -LiteralPath $baseFile -Destination (Join-Path $destinationPath $fileName) -Force
        }
    }
}

Copy-Item -LiteralPath $sourceExe -Destination (Join-Path $destinationPath "openrct2.exe") -Force
if (Test-Path -LiteralPath $sourceCli) {
    Copy-Item -LiteralPath $sourceCli -Destination (Join-Path $destinationPath "openrct2-cli.exe") -Force
}

$destinationData = Join-Path $destinationPath "data"
$robocopyArgs = @(
    $sourceData,
    $destinationData,
    "/MIR",
    "/NFL",
    "/NDL",
    "/NJH",
    "/NJS",
    "/NC",
    "/NS"
)

robocopy @robocopyArgs | Out-Host
if ($LASTEXITCODE -gt 7) {
    throw "robocopy failed with exit code $LASTEXITCODE."
}

Write-Host "Deployed $Configuration $Platform build to $destinationPath"
