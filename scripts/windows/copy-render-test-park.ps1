# Make the exact benchmark park available to both default Windows installations.
# Preserve edited saves; an existing byte-identical copy makes this idempotent.
[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$source = Join-Path $repoRoot 'test/tests/testdata/parks/EverythingPark.park'
$expected = 'c11bca8296bbf6d0b2673c4c80e3703139360b802e04b363d25cedd605459cf4'
if ((Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash.ToLowerInvariant() -ne $expected) {
    throw 'Everything Park differs from the qualified benchmark input.'
}
$documents = [Environment]::GetFolderPath('MyDocuments')
if (!$documents) { throw 'Cannot resolve the shared OpenRCT2 user directory.' }
$saveDirectory = Join-Path $documents 'OpenRCT2/save'
New-Item -ItemType Directory -Path $saveDirectory -Force | Out-Null
$stem = 'Everything Park - Renderer Test'
$suffix = 0
while ($true) {
    $name = if ($suffix -eq 0) { "$stem.park" } else { "$stem ($suffix).park" }
    $target = Join-Path $saveDirectory $name
    if (!(Test-Path -LiteralPath $target)) { break }
    if ((Test-Path -LiteralPath $target -PathType Leaf) -and
        (Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash.ToLowerInvariant() -eq $expected) { break }
    $suffix++
}
$created = !(Test-Path -LiteralPath $target)
if ($created) { [IO.File]::Copy($source, $target, $false) }
if ((Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash.ToLowerInvariant() -ne $expected) {
    throw 'Copied test park failed verification.'
}
[pscustomobject]@{ source = $source; destination = $target; sha256 = $expected; created = $created }
