# Synthetic preparation checks. The full process fixture uses a Linux shell
# stub in place of PeEditor; it cannot establish real Windows/game behavior.
param([string]$PreparationScript = (Join-Path $PSScriptRoot '../launcher/PrepareRuntime.ps1'))
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression.FileSystem
$tokens=$null; $parseErrors=$null
$ast = [System.Management.Automation.Language.Parser]::ParseFile([IO.Path]::GetFullPath($PreparationScript), [ref]$tokens, [ref]$parseErrors)
if ($parseErrors.Count) { throw 'Preparation script has syntax errors.' }
$functions = $ast.FindAll({param($node) $node -is [System.Management.Automation.Language.FunctionDefinitionAst]}, $false)
. ([ScriptBlock]::Create(($functions | ForEach-Object { $_.Extent.Text }) -join "`n"))

function Assert([bool]$Condition, [string]$Message) { if (-not $Condition) { throw $Message } }
function Reject([ScriptBlock]$Action) {
    $rejected=$false
    try { & $Action } catch { $rejected=$true }
    Assert $rejected 'Expected malformed input to be rejected.'
}
function Zip([string]$Path, [Collections.IDictionary]$Files) {
    $stream = [IO.File]::Create($Path)
    try {
        $archive = New-Object IO.Compression.ZipArchive($stream, [IO.Compression.ZipArchiveMode]::Create, $true)
        try {
            foreach ($name in $Files.Keys) {
                $entry = $archive.CreateEntry($name)
                $entryStream = $entry.Open()
                try {
                    $bytes = [Text.Encoding]::UTF8.GetBytes($Files[$name])
                    $entryStream.Write($bytes, 0, $bytes.Length)
                } finally { $entryStream.Dispose() }
            }
        } finally { $archive.Dispose() }
    } finally { $stream.Dispose() }
}
function Hash-Text([string]$Text) {
    $hash=[Security.Cryptography.SHA256]::Create()
    try { return ([BitConverter]::ToString($hash.ComputeHash([Text.Encoding]::UTF8.GetBytes($Text)))).Replace('-','').ToLowerInvariant() }
    finally { $hash.Dispose() }
}

$testRoot = Join-Path ([IO.Path]::GetTempPath()) ('myclient-runtime-test-' + [Guid]::NewGuid())
$GameDirectory = Join-Path $testRoot 'managed game'
$CacheDirectory = Join-Path $testRoot 'cache'
$ManifestPath = Join-Path $testRoot 'runtime.lock.json'
$null=[IO.Directory]::CreateDirectory($GameDirectory)
$null=[IO.Directory]::CreateDirectory($CacheDirectory)
try {
    foreach ($path in @('../escape','/rooted','C:/drive','a/../escape','a\..\escape','a/./b','a/space ')) {
        Reject { Safe-Child $GameDirectory $path }
    }
    $badArchive = Join-Path $CacheDirectory 'unsafe.zip'
    Zip $badArchive @{'../escape.dll'='bad'; 'good.dll'='good'}
    $badAsset = [pscustomobject]@{archivePrefix=''; placements=@([pscustomobject]@{type='file';src='good.dll';dest='good.dll'});
        installedFiles=[pscustomobject]@{'good.dll'=(Hash-Text 'good')}}
    Reject { Place-Asset $badAsset $badArchive }
    Assert (-not (Test-Path (Join-Path $testRoot 'escape.dll'))) 'Traversal wrote outside the game directory.'
    $duplicateArchive = Join-Path $CacheDirectory 'duplicate.zip'
    $duplicates=New-Object 'Collections.Generic.Dictionary[string,string]' ([StringComparer]::Ordinal)
    $duplicates.Add('GOOD.dll','one'); $duplicates.Add('good.dll','two')
    Zip $duplicateArchive $duplicates
    Reject { Place-Asset $badAsset $duplicateArchive }
    $wrongArchive=Join-Path $CacheDirectory 'wrong.zip'
    Zip $wrongArchive @{'good.dll'='changed'}
    Reject { Place-Asset $badAsset $wrongArchive }

    # Full offline subprocess flow under PowerShell 7 on Linux, using a cached
    # dependency graph and a deterministic activation stub.
    if ([IO.Path]::DirectorySeparatorChar -eq '/') {
        $editor = "#!/bin/sh`nset -eu`n[ `"`$1`" = '-mb' ]`n[ `"`$2`" = '--exe' ]`n[ `"`$3`" = 'Minecraft.Windows.exe' ]`n[ `"`$4`" = '--inplace' ]`nprintf 'patched fixture game' > Minecraft.Windows.exe`n"
        $definitions = @(
            @{name='LeviLamina'; version='26.51.6'; src='LeviLamina/manifest.json'; dest='mods/LeviLamina/manifest.json'; content='{"name":"LeviLamina","version":"26.51.6"}'},
            @{name='PreLoader'; version='1.16.3'; src='bin/PreLoader.dll'; dest='PreLoader.dll'; content='preloader fixture'},
            @{name='PeEditor'; version='3.9.1'; src='PeEditor.exe'; dest='PeEditor.exe'; content=$editor},
            @{name='bedrock-runtime-data'; version='26.51.1-client.7'; src='bedrock_runtime_data'; dest='bedrock_runtime_data'; content='runtime fixture'},
            @{name='CrashLogger'; version='1.3.3'; src='CrashLogger.exe'; dest='mods/LeviLamina/CrashLogger.exe'; content='crash fixture'},
            @{name='levilamina-loc'; version='1.6.4'; src='fixture-loc/lang/en_US.json'; dest='mods/LeviLamina/lang/en_US.json'; content='{}'}
        )
        $assets=@()
        foreach ($definition in $definitions) {
            $cacheFile=$definition.name+'.zip'
            $path=Join-Path $CacheDirectory $cacheFile
            $files=@{}; $files[$definition.src]=$definition.content
            Zip $path $files
            $placed=@{}; $placed[$definition.dest]=Hash-Text $definition.content
            $prefix=''; $source=$definition.src; $kind='file'; $destination=$definition.dest
            if ($definition.name -eq 'levilamina-loc') { $prefix='fixture-loc/';$source='lang/';$kind='dir';$destination='mods/LeviLamina/lang/' }
            $assets += @{name=$definition.name;version=$definition.version;url=('https://github.com/LiteLDev/'+$definition.name+'/fixture.zip');
                cacheFile=$cacheFile;bytes=(Get-Item $path).Length;sha256=(File-Hash $path);archivePrefix=$prefix;
                placements=@(@{type=$kind;src=$source;dest=$destination});installedFiles=$placed}
        }
        @{schemaVersion=1;gameVersion='1.26.51.01';loaderVersion='26.51.6';assets=$assets} | ConvertTo-Json -Depth 10 |
            Set-Content -LiteralPath $ManifestPath -Encoding utf8
        [IO.File]::WriteAllText((Join-Path $GameDirectory 'MyClient-game.json'), '{"owner":"MyClient","schemaVersion":1}')
        [IO.File]::WriteAllText((Join-Path $GameDirectory 'MyClient-original.exe'), 'original fixture game')
        [IO.File]::WriteAllText((Join-Path $GameDirectory 'PeEditor.exe'), '')
        & /bin/chmod '+x' (Join-Path $GameDirectory 'PeEditor.exe')
        $powershell=(Get-Process -Id $PID).Path
        for ($attempt=1;$attempt -le 2;$attempt++) {
            [IO.File]::WriteAllText((Join-Path $GameDirectory 'Minecraft.Windows.exe.bak'), 'old helper backup')
            & $powershell -NoLogo -NoProfile -NonInteractive -File $PreparationScript -ManifestPath $ManifestPath -GameDirectory $GameDirectory -CacheDirectory $CacheDirectory -ExpectedGameVersion "1.26.51.01" -ExpectedLoaderVersion "26.51.6"
            Assert ($LASTEXITCODE -eq 0) 'Cached preparation subprocess failed.'
            $receipt=Get-Content -Raw (Join-Path $GameDirectory 'MyClient-runtime.json') | ConvertFrom-Json
            Assert $receipt.ready 'Preparation did not mark completion.'
            Assert ($receipt.manifestSha256 -eq (File-Hash $ManifestPath)) 'Receipt has wrong manifest identity.'
            Assert ($receipt.gameExeSha256 -eq (File-Hash (Join-Path $GameDirectory 'Minecraft.Windows.exe'))) 'Receipt has wrong game identity.'
            Assert ([IO.File]::ReadAllText((Join-Path $GameDirectory 'MyClient-original.exe')) -eq 'original fixture game') 'Original game was changed.'
        }
        $outside=Join-Path $testRoot 'outside';$null=[IO.Directory]::CreateDirectory($outside)
        $null=New-Item -ItemType SymbolicLink -Path (Join-Path $GameDirectory 'linked') -Target $outside
        Reject { Safe-Child $GameDirectory 'linked/escape.dll' }
        $null=New-Item -ItemType SymbolicLink -Path (Join-Path $GameDirectory 'dangling.dll') -Target (Join-Path $outside 'missing.dll')
        Reject { Safe-Child $GameDirectory 'dangling.dll' }
    }
    Write-Output 'PASS: PowerShell parse, path/traversal/duplicate/hash rejection, cached placements and directory placement, preparation receipt, repeated setup, original preservation. Activation uses a Linux stub; Windows/Minecraft behavior is untested.'
}
finally { if (Test-Path -LiteralPath $testRoot) { Remove-Item -Recurse -Force -LiteralPath $testRoot } }
