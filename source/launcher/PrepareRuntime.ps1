param(
    [Parameter(Mandatory=$true)][string]$ManifestPath,
    [Parameter(Mandatory=$true)][string]$GameDirectory,
    [Parameter(Mandatory=$true)][string]$CacheDirectory,
    [string]$ExpectedGameVersion = "1.26.51.01",
    [string]$ExpectedLoaderVersion = "26.51.6",
    [switch]$Attach
)
$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = New-Object System.Text.UTF8Encoding($false)
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
Add-Type -AssemblyName System.IO.Compression.FileSystem

function Safe-Child([string]$Root, [string]$Relative) {
    $normalized = $Relative.Replace('\', '/')
    if ([string]::IsNullOrEmpty($normalized) -or [IO.Path]::IsPathRooted($normalized) -or
        $normalized.Contains(':') -or $normalized -match '[\x00-\x1f<>"|?*]') { throw 'Invalid relative path.' }
    foreach ($segment in $normalized.Split('/')) {
        if ($segment -eq '' -or $segment -eq '.' -or $segment -eq '..' -or
            $segment.EndsWith('.') -or $segment.EndsWith(' ')) { throw 'Invalid path segment.' }
    }
    $separator = [IO.Path]::DirectorySeparatorChar
    $rootPath = [IO.Path]::GetFullPath($Root).TrimEnd($separator)
    $targetPath = [IO.Path]::GetFullPath([IO.Path]::Combine($rootPath, $normalized))
    if (-not $targetPath.StartsWith($rootPath + $separator, [StringComparison]::OrdinalIgnoreCase)) { throw 'Path escaped its root.' }
    # Reject existing junctions/symlinks throughout the destination chain.
    $cursor = $targetPath
    while ($cursor.Length -ge $rootPath.Length) {
        $existing = $null
        try { $existing = Get-Item -Force -LiteralPath $cursor -ErrorAction Stop }
        catch [System.Management.Automation.ItemNotFoundException] { }
        if ($null -ne $existing -and ($existing.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw "Linked destination is unsupported: $cursor"
        }
        if ($cursor -eq $rootPath) { break }
        $cursor = [IO.Path]::GetDirectoryName($cursor)
    }
    return $targetPath
}

function File-Hash([string]$Path) {
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

# Progress for the launcher window, one ASCII line in the cache folder:
#   percent|phase|index/count|label|receivedMB/totalMB     (the launcher writes the Japanese text)
$script:ProgressFile = Join-Path $CacheDirectory 'progress.txt'
$script:TotalBytes = 1L
$script:DoneBytes = 0L
$script:AssetIndex = 0
$script:AssetCount = 6
function Set-Progress([int]$Percent, [string]$Phase, [string]$Label = '', [string]$Detail = '') {
    try {
        if ($Percent -gt 100) { $Percent = 100 }
        $line = "$Percent|$Phase|$($script:AssetIndex)/$($script:AssetCount)|$Label|$Detail"
        [IO.File]::WriteAllText($script:ProgressFile, $line, (New-Object Text.UTF8Encoding($false)))
    }
    catch { }
}

function Read-Lock {
    $lock = Get-Content -Raw -LiteralPath $ManifestPath -Encoding UTF8 | ConvertFrom-Json
    if ($lock.schemaVersion -ne 1 -or $lock.gameVersion -ne $ExpectedGameVersion -or $lock.loaderVersion -ne $ExpectedLoaderVersion) {
        throw 'Runtime manifest version mismatch.'
    }
    if (@($lock.assets).Count -ne 6) { throw 'Incomplete runtime dependency manifest.' }
    foreach ($asset in $lock.assets) {
        if ($asset.url -notmatch '^https://(github\.com|codeload\.github\.com)/LiteLDev/' -or
            $asset.sha256 -notmatch '^[a-f0-9]{64}$' -or $asset.bytes -le 0 -or $asset.bytes -gt 536870912) {
            throw 'Invalid pinned runtime asset.'
        }
        $null = Safe-Child $CacheDirectory $asset.cacheFile
        foreach ($file in $asset.installedFiles.PSObject.Properties) {
            $null = Safe-Child $GameDirectory $file.Name
            if ($file.Value -notmatch '^[a-f0-9]{64}$') { throw 'Invalid installed file hash.' }
        }
    }
    return $lock
}

# Streams the file so the launcher can show real progress (Invoke-WebRequest reports none).
function Download-File($Asset, [string]$OutFile) {
    $request = [Net.HttpWebRequest]::Create($Asset.url)
    $request.Timeout = 120000
    $request.ReadWriteTimeout = 120000
    $request.UserAgent = 'MyClientLauncher'
    $response = $request.GetResponse()
    try {
        $source = $response.GetResponseStream()
        $target = [IO.File]::Create($OutFile)
        try {
            $buffer = New-Object byte[] 81920
            $received = 0L
            $last = [Environment]::TickCount - 1000
            while (($count = $source.Read($buffer, 0, $buffer.Length)) -gt 0) {
                $target.Write($buffer, 0, $count)
                $received += $count
                $now = [Environment]::TickCount
                if (($now - $last) -ge 250) {
                    $last = $now
                    $percent = [int](85 * ($script:DoneBytes + $received) / $script:TotalBytes)
                    $detail = '{0:N1}/{1:N1}' -f ($received / 1MB), ($Asset.bytes / 1MB)
                    Set-Progress $percent 'download' "$($Asset.name) $($Asset.version)" $detail
                }
            }
        }
        finally { $target.Dispose(); $source.Dispose() }
    }
    finally { $response.Close() }
    $script:DoneBytes += [long]$Asset.bytes
}

function Get-Asset($Asset) {
    $path = Safe-Child $CacheDirectory $Asset.cacheFile
    $script:AssetIndex++
    if ((Test-Path -LiteralPath $path) -and (Get-Item -LiteralPath $path).Length -eq $Asset.bytes -and
        (File-Hash $path) -eq $Asset.sha256) {
        $script:DoneBytes += [long]$Asset.bytes
        Set-Progress ([int](85 * $script:DoneBytes / $script:TotalBytes)) 'cached' "$($Asset.name) $($Asset.version)"
        return $path
    }
    $temporary = $path + '.download'
    for ($attempt = 1; $attempt -le 3; $attempt++) {
        try {
            Write-Output "Downloading $($Asset.name) $($Asset.version) (attempt $attempt)."
            Download-File $Asset $temporary
            if ((Get-Item -LiteralPath $temporary).Length -ne $Asset.bytes -or (File-Hash $temporary) -ne $Asset.sha256) {
                throw 'Downloaded archive does not match its pinned SHA256/size.'
            }
            Move-Item -Force -LiteralPath $temporary -Destination $path
            return $path
        }
        catch {
            if (Test-Path -LiteralPath $temporary) { Remove-Item -LiteralPath $temporary -Force }
            if ($attempt -eq 3) { throw }
        }
    }
}

function Place-Asset($Asset, [string]$ArchivePath) {
    $archive = [IO.Compression.ZipFile]::OpenRead($ArchivePath)
    try {
        $entries = New-Object 'System.Collections.Generic.Dictionary[string,System.IO.Compression.ZipArchiveEntry]' ([StringComparer]::Ordinal)
        $seen = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
        foreach ($entry in $archive.Entries) {
            $name = $entry.FullName.Replace('\', '/')
            $null = Safe-Child $CacheDirectory $name.TrimEnd('/')
            if (-not $seen.Add($name.TrimEnd('/'))) { throw 'Duplicate ZIP path.' }
            $unixType = ($entry.ExternalAttributes -shr 16) -band 0xF000
            if ($unixType -ne 0 -and $unixType -ne 0x8000 -and $unixType -ne 0x4000) { throw 'Special ZIP entry is unsupported.' }
            if (-not $name.EndsWith('/')) { $entries.Add($name, $entry) }
        }
        $mapping = New-Object 'System.Collections.Generic.Dictionary[string,System.IO.Compression.ZipArchiveEntry]' ([StringComparer]::OrdinalIgnoreCase)
        foreach ($placement in $Asset.placements) {
            $source = $Asset.archivePrefix + $placement.src.TrimEnd('/')
            if ($placement.type -eq 'file') {
                if (-not $entries.ContainsKey($source)) { throw "Missing archive file: $source" }
                $mapping.Add($placement.dest, $entries[$source])
            }
            elseif ($placement.type -eq 'dir') {
                $prefix = $source + '/'
                foreach ($pair in $entries.GetEnumerator()) {
                    if ($pair.Key.StartsWith($prefix, [StringComparison]::Ordinal)) {
                        $mapping.Add($placement.dest.TrimEnd('/') + '/' + $pair.Key.Substring($prefix.Length), $pair.Value)
                    }
                }
            }
            else { throw 'Unknown placement type.' }
        }
        if ($mapping.Count -ne @($Asset.installedFiles.PSObject.Properties).Count) { throw 'Placement inventory differs from its lock.' }
        foreach ($file in $Asset.installedFiles.PSObject.Properties) {
            if (-not $mapping.ContainsKey($file.Name)) { throw 'Missing expected placement.' }
            $destination = Safe-Child $GameDirectory $file.Name
            $null = [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($destination))
            $sourceStream = $mapping[$file.Name].Open()
            try {
                $destinationStream = [IO.File]::Create($destination)
                try { $sourceStream.CopyTo($destinationStream) } finally { $destinationStream.Dispose() }
            }
            finally { $sourceStream.Dispose() }
            if ((File-Hash $destination) -ne $file.Value) { throw "Installed file hash mismatch: $($file.Name)" }
        }
    }
    finally { $archive.Dispose() }
}

$receipt = Join-Path $GameDirectory 'MyClient-runtime.json'
$original = Join-Path $GameDirectory 'MyClient-original.exe'
$executable = Join-Path $GameDirectory 'Minecraft.Windows.exe'
try {
    $marker = Get-Content -Raw -LiteralPath (Join-Path $GameDirectory 'MyClient-game.json') -Encoding UTF8 | ConvertFrom-Json
    if ($Attach) {
        if ($marker.owner -ne 'MyClient' -or $marker.schemaVersion -ne 1 -or $marker.mode -ne 'attach') {
            throw 'Only the MyClient attach directory may be prepared.'
        }
    }
    else {
        if ($marker.owner -ne 'MyClient' -or $marker.schemaVersion -ne 1 -or -not (Test-Path -LiteralPath $original -PathType Leaf)) {
            throw 'Only the MyClient-managed game directory may be prepared.'
        }
        $null = Safe-Child $GameDirectory 'MyClient-original.exe'
        $null = Safe-Child $GameDirectory 'Minecraft.Windows.exe'
    }
    $null = Safe-Child $GameDirectory 'MyClient-runtime.json'
    $null = [IO.Directory]::CreateDirectory($CacheDirectory)
    $lock = Read-Lock
    $script:TotalBytes = [long](($lock.assets | Measure-Object -Property bytes -Sum).Sum)
    $script:AssetCount = @($lock.assets).Count
    Set-Progress 0 'prepare'
    if (Test-Path -LiteralPath $receipt) { Remove-Item -LiteralPath $receipt -Force }
    $placed = 0
    foreach ($asset in $lock.assets) {
        # Capture only the return path; progress messages go to stdout/log.
        $output = @(Get-Asset $asset)
        if ($output.Count -gt 1) { $output[0..($output.Count - 2)] | Write-Output }
        Set-Progress ([int](85 + 9 * $placed / $script:AssetCount)) 'extract' "$($asset.name) $($asset.version)"
        Place-Asset $asset $output[-1]
        $placed++
    }
    if ($Attach) {
        # No game executable is copied or edited; the launcher injects PreLoader.dll at run time.
        $result = @{ready=$true; attach=$true; manifestSha256=(File-Hash $ManifestPath)}
    }
    else {
        [IO.File]::Copy($original, $executable, $true)
        $editorBackup = Safe-Child $GameDirectory 'Minecraft.Windows.exe.bak'
        if (Test-Path -LiteralPath $editorBackup) { Remove-Item -LiteralPath $editorBackup -Force }
        Push-Location -LiteralPath $GameDirectory
        try {
            Write-Output 'Activating the official PreLoader in the managed executable.'
            Set-Progress 95 'activate'
            & (Join-Path $GameDirectory 'PeEditor.exe') '-mb' '--exe' 'Minecraft.Windows.exe' '--inplace'
            if ($LASTEXITCODE -ne 0) { throw "PeEditor failed with exit code $LASTEXITCODE." }
        }
        finally { Pop-Location }
        # The C++ launcher also checks the resulting PE imports before launch.
        $result = @{ready=$true; manifestSha256=(File-Hash $ManifestPath); gameExeSha256=(File-Hash $executable)}
    }
    $temporaryReceipt = Safe-Child $GameDirectory 'MyClient-runtime.json.tmp'
    [IO.File]::WriteAllText($temporaryReceipt, ($result | ConvertTo-Json), (New-Object Text.UTF8Encoding($false)))
    Move-Item -Force -LiteralPath $temporaryReceipt -Destination $receipt
    Set-Progress 100 'done'
    Write-Output 'MyClient runtime preparation completed.'
    exit 0
}
catch {
    [Console]::Error.WriteLine($_.Exception.Message)
    exit 1
}
