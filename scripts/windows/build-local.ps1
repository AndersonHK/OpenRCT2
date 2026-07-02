[CmdletBinding()]
param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Release",

    [ValidateSet("x64", "Win32", "ARM64")]
    [string]$Platform = "x64",

    [string]$VCToolsVersion = "14.44.35207"
)

$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..")
Push-Location $repoRoot

try {
    msbuild openrct2.proj /m /nr:false /p:Configuration=$Configuration /p:Platform=$Platform /p:VCToolsVersion=$VCToolsVersion
} finally {
    Pop-Location
}
