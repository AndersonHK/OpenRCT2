param(
    [Parameter(Mandatory=$true)][string]$Park,
    [Parameter(Mandatory=$true)][string]$RCT2Data,
    [ValidateRange(0,1000000)][int]$Ticks = 4096,
    [string]$Label = 'texas-investigation',
    [string]$TestDirectory,
    [switch]$ExpectProgress
)

$ErrorActionPreference = 'Stop'
if ($Label -notmatch '^[a-zA-Z0-9_-]+$') { throw 'Label must be a simple filename without a path.' }
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$parkFile = (Resolve-Path -LiteralPath $Park).Path
$rct2Directory = (Resolve-Path -LiteralPath $RCT2Data).Path
if (-not $TestDirectory) { $TestDirectory = Join-Path $repo 'bin' }
$testDirectoryPath = (Resolve-Path -LiteralPath $TestDirectory).Path
New-Item -ItemType Directory -Force -Path (Join-Path $repo 'artifacts') | Out-Null
$prefix = Join-Path $repo "artifacts\$Label"
$variables = @{
    OPENRCT2_TEST_USER_DATA_PATH = (Join-Path $repo 'artifacts\texas-user')
    OPENRCT2_TEST_RCT2_PATH = $rct2Directory
    OPENRCT2_INVESTIGATION_PARK = $parkFile
    OPENRCT2_INVESTIGATION_OUTPUT = $prefix
    OPENRCT2_INVESTIGATION_TICKS = [string]$Ticks
    OPENRCT2_INVESTIGATION_EXPECT_PROGRESS = $(if ($ExpectProgress) { '1' } else { '0' })
}
$previous = @{}
foreach ($key in $variables.Keys) {
    $previous[$key] = [Environment]::GetEnvironmentVariable($key, 'Process')
    [Environment]::SetEnvironmentVariable($key, $variables[$key], 'Process')
}
Push-Location $testDirectoryPath
try {
    & .\tests.exe '--gtest_filter=TexasPathfindingInvestigation.*' *> "$prefix.log"
    if ($LASTEXITCODE -ne 0) { throw "Native diagnostic failed. See $prefix.log" }
    & python (Join-Path $PSScriptRoot 'texas_pathfinding_analog.py') `
        --paths "$prefix-paths.csv" --guests "$prefix-guests.csv" > "$prefix-analysis.json"
    if ($LASTEXITCODE -ne 0) { throw 'Independent graph verification failed.' }
    Get-Content "$prefix-analysis.json"
}
finally {
    Pop-Location
    foreach ($key in $variables.Keys) {
        if ($null -eq $previous[$key]) {
            Remove-Item "Env:$key" -ErrorAction SilentlyContinue
        }
        else {
            [Environment]::SetEnvironmentVariable($key, $previous[$key], 'Process')
        }
    }
}
