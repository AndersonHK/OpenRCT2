param([Parameter(Mandatory=$true)][string]$Plan)
$ErrorActionPreference = 'Stop'
$repoPath = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$namespacePath = [IO.Path]::GetFullPath((Join-Path $repoPath 'obj\vulkan-parity'))
function Assert-Contained([string]$Path) {
    $full = [IO.Path]::GetFullPath($Path)
    if (-not $full.StartsWith($namespacePath + '\', [StringComparison]::OrdinalIgnoreCase)) {
        throw "Artifact path outside namespace: $full"
    }
    $cursor = $full
    while ($cursor.StartsWith($namespacePath, [StringComparison]::OrdinalIgnoreCase)) {
        $attributes = [IO.File]::GetAttributes($cursor)
        if ($attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Reparse point in artifact path: $cursor" }
        if ($cursor -eq $namespacePath) { break }
        $cursor = [IO.Path]::GetDirectoryName($cursor)
    }
    return $full
}
function Read-Entry([string]$Line) {
    $fields = $Line.Split("`t")
    if ($fields.Length -ne 7) { throw 'Malformed archive index row' }
    return @{path=$fields[0]; bytes=[long]$fields[1]; mtimeNs=[long]$fields[2]; sha256=$fields[3]; kind=$fields[4]; archive=$fields[5]; entry=$fields[6]}
}
$planPath = Assert-Contained ([IO.Path]::GetFullPath($Plan))
$archiveRoot = [IO.Path]::GetDirectoryName($planPath)
$resultPath = Join-Path $archiveRoot 'archive-result.json'
if (Test-Path -LiteralPath $resultPath) { throw 'Result already exists; prepare a new index of remaining originals' }
$state = Get-Content -LiteralPath $planPath -Raw | ConvertFrom-Json
if ($state.schema -ne 1 -or $state.root -ne $repoPath -or $state.status -ne 'verified') { throw 'Archive has not passed payload verification' }
if (Get-Process openrct2*,tests*,ui-parity*,screenshot-parity*,object-fixture* -ErrorAction SilentlyContinue) { throw 'Active game/parity processes prevent cleanup' }
$indexPath = Assert-Contained (Join-Path $archiveRoot 'payloads.tsv')
if ((Get-FileHash -LiteralPath $indexPath).Hash.ToLowerInvariant() -ne $state.indexSha256) { throw 'Archive index changed' }
foreach ($archive in $state.archives) {
    $full = Assert-Contained (Join-Path $repoPath $archive.path)
    if ((Get-FileHash -LiteralPath $full).Hash.ToLowerInvariant() -ne $archive.sha256) { throw "Verified archive changed: $full" }
}
foreach ($executable in $state.protectedExecutables) {
    if ((Get-FileHash -LiteralPath $executable.path).Hash.ToLowerInvariant() -ne $executable.sha256) { throw 'Protected executable changed since preparation' }
}
# Stream the large index instead of materializing hundreds of thousands of JSON objects.
# The first complete pass checks every original before deleting the first leaf.
$validated = 0
$reader = [IO.StreamReader]::new($indexPath)
$null = $reader.ReadLine()
try { while ($null -ne ($line = $reader.ReadLine())) {
    $entry = Read-Entry $line
    $full = Assert-Contained (Join-Path $repoPath $entry.path)
    $item = [IO.FileInfo]::new($full)
    $modifiedNs = ($item.LastWriteTimeUtc.Ticks - 621355968000000000) * 100
    if ($item.Length -ne $entry.bytes -or $modifiedNs -ne $entry.mtimeNs) { throw "Original changed: $full" }
    if ($entry.kind -eq 'discard-compiler') {
        if ($entry.archive -or $entry.entry) { throw 'Malformed compiler-discard row' }
    } elseif ($entry.archive -notin $state.archives.path -or $entry.entry -ne $entry.sha256) {
        throw 'Archived original lacks a verified payload reference'
    }
    $validated++
    if ($validated % 25000 -eq 0) { Write-Output "Validated $validated original payload paths" }
}
} finally { $reader.Dispose() }
if ($validated -ne $state.selectedFiles) { throw 'Index row count differs from verified plan' }
$removedBytes = [long]0
$removedFiles = 0
$driveName = [IO.Path]::GetPathRoot($repoPath).Substring(0,1)
$beforeFree = (Get-PSDrive -Name $driveName).Free
try {
    $reader = [IO.StreamReader]::new($indexPath)
    $null = $reader.ReadLine()
    while ($null -ne ($line = $reader.ReadLine())) {
        $entry = Read-Entry $line
        $full = Assert-Contained (Join-Path $repoPath $entry.path)
        $item = [IO.FileInfo]::new($full)
        $modifiedNs = ($item.LastWriteTimeUtc.Ticks - 621355968000000000) * 100
        if ($item.Length -ne $entry.bytes -or $modifiedNs -ne $entry.mtimeNs) { throw "Original changed during cleanup: $full" }
        Remove-Item -LiteralPath $full -Force
        $removedBytes += $entry.bytes
        $removedFiles++
        if ($removedFiles % 25000 -eq 0) { Write-Output "Retired $removedFiles originals ($([math]::Round($removedBytes/1GB,2)) GiB)" }
    }
    $status = 'complete'
} finally {
    if ($reader) { $reader.Dispose() }
    if (-not $status) { $status = 'incomplete' }
    $afterFree = (Get-PSDrive -Name $driveName).Free
    @{schema=1; status=$status; removedFiles=$removedFiles; removedBytes=$removedBytes;
        archiveBytes=($state.archives | Measure-Object bytes -Sum).Sum;
        freeBytesBefore=$beforeFree; freeBytesAfter=$afterFree; freeSpaceDelta=$afterFree-$beforeFree;
        note='Archives were written before the deletion free-space measurement. Only generated compiler products lack archived payloads.'} |
        ConvertTo-Json | Set-Content -LiteralPath $resultPath -Encoding utf8
}
Get-Content -LiteralPath $resultPath
