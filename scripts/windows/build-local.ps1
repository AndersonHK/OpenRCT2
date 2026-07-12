[CmdletBinding()]
param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Release",

    [ValidateSet("x64", "Win32", "ARM64")]
    [string]$Platform = "x64",

    [string]$VCToolsVersion = "14.44.35207",

    [bool]$EnableVulkan = $true
)

$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..")
Push-Location $repoRoot

try {
    # This repository's large solution can exhaust the Windows process table under unrestricted MSBuild fan-out.
    msbuild openrct2.proj /m:1 /nr:false /p:Configuration=$Configuration /p:Platform=$Platform /p:VCToolsVersion=$VCToolsVersion /p:EnableVulkan=$EnableVulkan
    if ($LASTEXITCODE -ne 0) {
        throw "MSBuild failed with exit code $LASTEXITCODE."
    }
} finally {
    Pop-Location
}
