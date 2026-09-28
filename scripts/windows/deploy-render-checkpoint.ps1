# Deploy an explicitly selected, successful renderer build to the existing manual-test installation.
# No build, mirroring, deletion, object traversal, config writes, or automatic launch.
# Also add the exact benchmark park to the shared save folder, preserving existing saves.
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$BuildReceipt,
    [Parameter(Mandatory = $true)][string]$BuildReceiptSha256,
    [Parameter(Mandatory = $true)][string]$OutputDirectory
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..')).TrimEnd('\')
$destination = 'D:\Games\Independent\OpenRCT2Mod'
$binRoot = Join-Path $repoRoot 'bin'

function Assert-NoReparse([string]$Path) {
    $cursor = [IO.Path]::GetFullPath($Path)
    while ($cursor) {
        if ((Test-Path -LiteralPath $cursor) -and
            ((Get-Item -LiteralPath $cursor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) {
            throw "Refusing reparse path: $cursor"
        }
        $parent = Split-Path -Parent $cursor
        if ($parent -eq $cursor) { break }
        $cursor = $parent
    }
}
function Resolve-Child([string]$Root, [string]$Relative) {
    if (!$Relative -or [IO.Path]::IsPathRooted($Relative) -or $Relative.Contains(':') -or
        ($Relative -split '[/\\]' | Where-Object { $_ -in @('', '.', '..') })) {
        throw "Invalid relative path: $Relative"
    }
    $path = [IO.Path]::GetFullPath((Join-Path $Root $Relative))
    if (!$path.StartsWith($Root.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase)) {
        throw "Path escapes root: $Relative"
    }
    Assert-NoReparse $path
    return $path
}
function Get-Digest([string]$Path) {
    Assert-NoReparse $Path
    if (!(Test-Path -LiteralPath $Path -PathType Leaf)) { throw "Missing file: $Path" }
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}
function Assert-Digest([string]$Path, [string]$Expected) {
    if (!$Expected -or (Get-Digest $Path) -ne $Expected) { throw "Hash mismatch: $Path" }
}
function Property-Value($Object, [string]$Name) {
    $property = $Object.PSObject.Properties[$Name]
    if ($null -eq $property) { return $null }
    return $property.Value
}
function Assert-GameClosed {
    # Conservatively reject even processes whose executable path cannot be queried.
    if (Get-Process -Name openrct2, openrct2-cli -ErrorAction SilentlyContinue) {
        throw 'Close OpenRCT2 before deploying.'
    }
}
function Save-Json([string]$Path, $Value) {
    $Value | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $Path -Encoding UTF8
}
function Copy-Verified([string]$Source, [string]$Target, [string]$Digest) {
    Assert-Digest $Source $Digest
    Assert-NoReparse $Target
    New-Item -ItemType Directory -Path (Split-Path -Parent $Target) -Force | Out-Null
    Copy-Item -LiteralPath $Source -Destination $Target -Force
    Assert-Digest $Target $Digest
}

$receiptPath = [IO.Path]::GetFullPath($BuildReceipt)
if (!$receiptPath.StartsWith($repoRoot + '\', [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Build receipt must be inside this workspace.'
}
Assert-Digest $receiptPath $BuildReceiptSha256
$build = Get-Content -LiteralPath $receiptPath -Raw | ConvertFrom-Json
if ($build.status -ne 'pass' -or $build.exitCode -ne 0 -or @($build.missingArtifacts).Count -ne 0 -or
    @($build.sourceChangesDuringBuild).Count -ne 0 -or @($build.generatedInputChangesDuringBuild).Count -ne 0) {
    throw 'Build receipt is not a successful unchanged-input build.'
}
Assert-NoReparse $destination
if (!(Test-Path -LiteralPath $destination -PathType Container) -or
    !(Test-Path -LiteralPath (Join-Path $destination 'openrct2.exe') -PathType Leaf)) {
    throw 'Expected the existing OpenRCT2Mod installation.'
}
Assert-GameClosed
$output = [IO.Path]::GetFullPath($OutputDirectory).TrimEnd('\')
$evidenceRoot = Join-Path $repoRoot 'obj\vulkan-parity'
if (!$output.StartsWith($evidenceRoot + '\', [StringComparison]::OrdinalIgnoreCase) -or
    (Test-Path -LiteralPath $output)) {
    throw 'OutputDirectory must be a fresh directory under obj/vulkan-parity.'
}
Assert-NoReparse $output

$files = [Collections.Generic.List[object]]::new()
function Add-File([string]$Relative, [string]$Expected, [string]$Qualification) {
    $source = Resolve-Child $binRoot $Relative
    Assert-Digest $source $Expected
    $target = Resolve-Child $destination $Relative
    $oldHash = $null
    if (Test-Path -LiteralPath $target) { $oldHash = Get-Digest $target }
    $files.Add([ordered]@{
        path = $Relative; sha256 = $Expected.ToLowerInvariant(); bytes = (Get-Item -LiteralPath $source).Length
        previousSha256 = $oldHash; changed = $oldHash -ne $Expected; qualification = $Qualification
    })
}
foreach ($launcher in @('openrct2.exe', 'openrct2-cli.exe')) {
    Add-File $launcher (Property-Value $build.artifactSha256 ('bin/' + $launcher)) 'build artifact'
}
$shaderEntries = @($build.artifactSha256.PSObject.Properties | Where-Object Name -Match '^bin/data/shaders/vulkan/[^/]+\.spv$')
if ($shaderEntries.Count -eq 0) { throw 'Build receipt has no runtime Vulkan shaders.' }
$shaderDirectory = Resolve-Child $binRoot 'data/shaders/vulkan'
$actualShaders = @(Get-ChildItem -LiteralPath $shaderDirectory -File -Force | Where-Object Extension -EQ '.spv')
if ($actualShaders.Count -ne $shaderEntries.Count) { throw 'Runtime SPV inventory differs from build receipt.' }
foreach ($shader in $actualShaders) {
    Assert-NoReparse $shader.FullName
    if (!(Property-Value $build.artifactSha256 ('bin/data/shaders/vulkan/' + $shader.Name))) {
        throw "Unqualified runtime shader: $($shader.Name)"
    }
}
foreach ($entry in $shaderEntries) { Add-File $entry.Name.Substring(4) $entry.Value 'build artifact' }

# Read ordinary data files without ever entering the object subtree or following junctions.
# Shader sources are development inputs; all runtime SPVs were qualified above.
$unchangedData = [Collections.Generic.List[object]]::new()
$pending = [Collections.Generic.Queue[string]]::new()
$pending.Enqueue((Resolve-Child $binRoot 'data'))
while ($pending.Count -gt 0) {
    foreach ($item in Get-ChildItem -LiteralPath $pending.Dequeue() -Force) {
        $relative = $item.FullName.Substring($binRoot.Length + 1).Replace('\', '/')
        if ($relative -in @('data/object', 'data/shaders')) { continue }
        Assert-NoReparse $item.FullName
        if ($item.PSIsContainer) { $pending.Enqueue($item.FullName); continue }
        $digest = Get-Digest $item.FullName
        $target = Resolve-Child $destination $relative
        if ((Test-Path -LiteralPath $target -PathType Leaf) -and (Get-Digest $target) -eq $digest) {
            $unchangedData.Add([ordered]@{ path = $relative; sha256 = $digest })
            continue
        }
        if ($relative -notmatch '^data/(language|assetpack|scenario_patches|sequence)/' -and
            $relative -notin @('data/fonts.dat', 'data/g2.dat', 'data/palettes.dat', 'data/tracks.dat')) {
            throw "Changed data is outside the runtime deployment scope: $relative"
        }
        $qualified = Property-Value $build.sourceSha256 $relative
        if (!$qualified -or $qualified -ne $digest) {
            throw "Changed non-object data is not qualified by build receipt: $relative"
        }
        Assert-Digest (Resolve-Child $repoRoot $relative) $qualified
        Add-File $relative $qualified 'build source data'
    }
}

# Freeze payload and every changed preimage before the first installation write.
New-Item -ItemType Directory -Path $output | Out-Null
$payload = Join-Path $output 'payload'
$backup = Join-Path $output 'backup'
foreach ($file in $files) {
    Copy-Verified (Resolve-Child $binRoot $file.path) (Resolve-Child $payload $file.path) $file.sha256
    if ($file.changed -and $null -ne $file.previousSha256) {
        Copy-Verified (Resolve-Child $destination $file.path) (Resolve-Child $backup $file.path) $file.previousSha256
    }
}
Copy-Verified $receiptPath (Join-Path $output 'build-receipt.json') $BuildReceiptSha256
$manifest = [ordered]@{
    schema = 1; status = 'backup-complete'; createdUtc = [DateTime]::UtcNow.ToString('o')
    destination = $destination; buildReceipt = $receiptPath; buildReceiptSha256 = $BuildReceiptSha256.ToLowerInvariant()
    helperSha256 = Get-Digest $PSCommandPath; backup = $backup; payload = $payload
    scope = 'Launchers, all qualified SPVs, changed receipt-qualified non-object data, exact Everything Park test save. No deletions; objects/config/existing saves preserved.'
    files = @($files.ToArray()); unchangedNonObjectData = @($unchangedData.ToArray())
}
$manifestPath = Join-Path $output 'manifest.json'
Save-Json $manifestPath $manifest
$result = [ordered]@{
    schema = 1; status = 'deploying'; destination = $destination; manifest = $manifestPath
    manifestSha256 = Get-Digest $manifestPath; changedFiles = @($files | Where-Object { $_.changed }).Count
    startedUtc = [DateTime]::UtcNow.ToString('o'); completedUtc = $null; error = $null; testPark = $null
}
$resultPath = Join-Path $output 'receipt.json'
Save-Json $resultPath $result
try {
    Assert-GameClosed
    # Refuse a concurrent destination edit after backup, including newly created targets.
    foreach ($file in $files) {
        $target = Resolve-Child $destination $file.path
        if ($null -eq $file.previousSha256) {
            if (Test-Path -LiteralPath $target) { throw "Destination appeared after preflight: $target" }
        } else { Assert-Digest $target $file.previousSha256 }
    }
    foreach ($file in $files) {
        if ($file.changed) {
            Copy-Verified (Resolve-Child $payload $file.path) (Resolve-Child $destination $file.path) $file.sha256
        }
    }
    foreach ($file in $files) { Assert-Digest (Resolve-Child $destination $file.path) $file.sha256 }
    foreach ($file in $unchangedData) { Assert-Digest (Resolve-Child $destination $file.path) $file.sha256 }
    $result.testPark = & (Join-Path $PSScriptRoot 'copy-render-test-park.ps1')
    $result.status = 'deployed-verified'
} catch {
    $result.status = 'deploy-failed-backup-preserved'
    $result.error = $_.Exception.Message
    throw
} finally {
    $result.completedUtc = [DateTime]::UtcNow.ToString('o')
    Save-Json $resultPath $result
}
Write-Output "Deployed and verified $($files.Count) qualified files ($($result.changedFiles) changed) to $destination."
Write-Output "Receipt: $resultPath"
Write-Output "Test park: $($result.testPark.destination)"
