param([Parameter(Mandatory=$true)][string]$Plan)
$ErrorActionPreference = 'Stop'
$repoPath = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$namespacePath = [IO.Path]::GetFullPath((Join-Path $repoPath 'obj\vulkan-parity'))
$planPath = [IO.Path]::GetFullPath($Plan)
function Assert-Contained([string]$Path) {
    $full = [IO.Path]::GetFullPath($Path)
    if (-not $full.StartsWith($namespacePath + '\', [StringComparison]::OrdinalIgnoreCase)) {
        throw "Cleanup path is outside the Vulkan artifact namespace: $full"
    }
    $cursor = $full
    while ($cursor -and $cursor.StartsWith($namespacePath, [StringComparison]::OrdinalIgnoreCase)) {
        # Native attributes avoid repeatedly invoking the PowerShell filesystem
        # provider for every ancestor of thousands of leaf files. Missing paths
        # still fail closed; every deletion ancestor is checked again on apply.
        $attributes = [IO.File]::GetAttributes($cursor)
        if ($attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Reparse point: $cursor" }
        if ($cursor -eq $namespacePath) { break }
        $cursor = [IO.Path]::GetDirectoryName($cursor)
    }
    return $full
}
$null = Assert-Contained $planPath
$receiptPath = Join-Path ([IO.Path]::GetDirectoryName($planPath)) 'deletion-result.json'
if (Test-Path -LiteralPath $receiptPath) { throw 'A deletion-result journal already exists; prepare a new plan of remaining files' }
$manifest = Get-Content -LiteralPath $planPath -Raw | ConvertFrom-Json
if ($manifest.schema -ne 1 -or $manifest.root -ne $repoPath -or $manifest.status -ne 'planned') {
    throw 'Invalid or previously applied deletion plan'
}
if (Get-Process openrct2,openrct2-cli,tests,ui-parity,screenshot-parity -ErrorAction SilentlyContinue) {
    throw 'Game/parity processes are active; cleanup postponed'
}
# Validate the complete list and retained metadata BEFORE deleting the first file.
foreach ($entry in $manifest.files) {
    $full = Assert-Contained (Join-Path $repoPath $entry.path)
    $item = [IO.FileInfo]::new($full)
    $modifiedNs = ($item.LastWriteTimeUtc.Ticks - 621355968000000000) * 100
    if (($item.Attributes -band [IO.FileAttributes]::Directory) -or $item.Length -ne $entry.bytes -or $modifiedNs -ne $entry.mtimeNs) {
        throw "Artifact changed since planning: $full"
    }
}
foreach ($entry in $manifest.preservedMetadata) {
    $full = Assert-Contained (Join-Path $repoPath $entry.path)
    if ((Get-FileHash -LiteralPath $full -Algorithm SHA256).Hash.ToLowerInvariant() -ne $entry.sha256) {
        throw "Retained metadata changed since planning: $full"
    }
}
$cache = Join-Path $namespacePath 'incremental-ui-cache'
if ($manifest.groups.'incremental-ui-cache') {
    if (Get-ChildItem -LiteralPath $cache -Filter exclusive.lock -Recurse -File) { throw 'UI cache is locked' }
    Get-ChildItem -LiteralPath $cache -Directory | ForEach-Object {
        $state = Join-Path $_.FullName 'state.json'
        if (Test-Path -LiteralPath $state) {
            $null = Assert-Contained $state
            Copy-Item -LiteralPath $state -Destination ($state + '.precleanup')
            @{usable=$false; reason='Historical compiler cache retired by 2026-10-14 cleanup'; plan=$planPath} |
                ConvertTo-Json | Set-Content -LiteralPath $state -Encoding utf8
        }
    }
}
$beforeFree = (Get-PSDrive -Name ([IO.Path]::GetPathRoot($repoPath).Substring(0,1))).Free
$removedBytes = [long]0
$removedFiles = 0
try {
    foreach ($entry in $manifest.files) {
        $full = Assert-Contained (Join-Path $repoPath $entry.path)
        $item = [IO.FileInfo]::new($full)
        $modifiedNs = ($item.LastWriteTimeUtc.Ticks - 621355968000000000) * 100
        if ($item.Length -ne $entry.bytes -or $modifiedNs -ne $entry.mtimeNs) { throw "Artifact changed during cleanup: $full" }
        # Leaf files only. No recursive deletion and no traversal into shared data.
        Remove-Item -LiteralPath $full -Force
        $removedBytes += $entry.bytes
        $removedFiles++
        if ($removedFiles % 5000 -eq 0) { Write-Output "Removed $removedFiles files ($([math]::Round($removedBytes / 1GB,2)) GiB)" }
    }
    $status = 'complete'
} finally {
    if (-not $status) { $status = 'incomplete' }
    $afterFree = (Get-PSDrive -Name ([IO.Path]::GetPathRoot($repoPath).Substring(0,1))).Free
    @{schema=1; status=$status; plan=$planPath; removedBytes=$removedBytes; removedFiles=$removedFiles;
        freeBytesBefore=$beforeFree; freeBytesAfter=$afterFree; freeSpaceDelta=$afterFree-$beforeFree;
        note='Volume free-space delta can include concurrent activity in other repositories. See deletion-plan.json for original paths and preserved evidence.'} |
        ConvertTo-Json | Set-Content -LiteralPath $receiptPath -Encoding utf8
}
Get-Content -LiteralPath $receiptPath
