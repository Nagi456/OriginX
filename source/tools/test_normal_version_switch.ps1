param([string]$LauncherSource = (Join-Path $PSScriptRoot '..\launcher\MyClientLauncher.cpp'))
$ErrorActionPreference = 'Stop'
$text = Get-Content -LiteralPath $LauncherSource -Raw -Encoding UTF8
$match = [regex]::Match($text, '(?s)SwitchScript = R"ps\((.*?)\)ps";')
if (!$match.Success) { throw 'Embedded switch script not found' }
$tokens = $null; $errors = $null
$ast = [System.Management.Automation.Language.Parser]::ParseInput($match.Groups[1].Value, [ref]$tokens, [ref]$errors)
if ($errors.Count) { throw ($errors | Out-String) }
$normalize = $ast.Find({param($node)
    $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq 'Get-MinecraftVersionId'
}, $true)
if (!$normalize) { throw 'Version normalization function not found' }
. ([scriptblock]::Create($normalize.Extent.Text))
foreach ($pair in @(@('1.26.5101.0','26.51.1'), @('1.26.6005.0','26.60.5'), @('1.26.5203.0','26.52.3'),
                     @('1.21.12004.0','1.21.120.4'), @('26.51.1.0','26.51.1'), @('1.26.51.0','26.51.0'))) {
    if ((Get-MinecraftVersionId ([version]$pair[0])) -ne $pair[1]) { throw "Normalization mismatch: $($pair[0])" }
}

# Execute the actual embedded script in a child process with package/process APIs
# shadowed by test functions. Neither deployment nor process termination is permitted.
$root = Join-Path ([System.IO.Path]::GetTempPath()) ('originx-switch-test-' + [guid]::NewGuid().ToString('N'))
[System.IO.Directory]::CreateDirectory($root) | Out-Null
$target = Join-Path $root 'switch.ps1'
$wrapper = Join-Path $root 'mock.ps1'
[System.IO.File]::WriteAllText($target, $match.Groups[1].Value, [System.Text.UTF8Encoding]::new($true))
$fixture = @'
param([string]$Target, [string]$Mode, [string]$ExpectedId, [string]$InitialVersion, [string]$AfterVersion, [int]$Busy)
$ErrorActionPreference = 'Stop'
if ($ExpectedId -eq '__EMPTY__') { $ExpectedId = '' }
$global:OriginXTestVersion = $InitialVersion
$global:OriginXTestAfterVersion = $AfterVersion
$global:OriginXTestBusy = $Busy
function Get-AppxPackage {
    [CmdletBinding()] param([string]$Name)
    if ($Name -eq 'Microsoft.GamingServices') { return [pscustomobject]@{Name=$Name} }
    if ($Name -ne 'Microsoft.MinecraftUWP') { throw 'Unexpected package query in test' }
    return [pscustomobject]@{Version=[version]$global:OriginXTestVersion; SignatureKind='None'; InstallLocation='C:\mock-minecraft'}
}
function Get-Process {
    [CmdletBinding()] param([string]$Name)
    if ($Name -ne 'Minecraft.Windows') { throw 'Unexpected process query in test' }
    if ($global:OriginXTestBusy) { return [pscustomobject]@{Id=1} }
}
function Add-AppxPackage {
    [CmdletBinding()] param([string]$Path, [switch]$ForceUpdateFromAnyVersion)
    if ($Path -ne 'mock-package.msixvc' -or !$ForceUpdateFromAnyVersion) { throw 'Unexpected deployment arguments' }
    Write-Output 'MOCK_PACKAGE_WRITE'
    $global:OriginXTestVersion = $global:OriginXTestAfterVersion
}
function Copy-Item { throw 'File writes are not permitted by the switch test' }
function Stop-Process { throw 'Process termination is not permitted by the switch test' }
& $Target -Mode $Mode -Package 'mock-package.msixvc' -ExpectedId $ExpectedId
exit $LASTEXITCODE
'@
[System.IO.File]::WriteAllText($wrapper, $fixture, [System.Text.UTF8Encoding]::new($true))
$powershell = Join-Path $PSHOME 'powershell.exe'
if (!(Test-Path -LiteralPath $powershell)) { $powershell = Join-Path $PSHOME 'pwsh.exe' }
$cases = @(
    @{name='already matching';mode='check';expected='26.51.1';initial='1.26.5101.0';after='1.26.5101.0';busy=1;code=10;write=$false},
    @{name='different version';mode='check';expected='26.51.1';initial='1.26.5203.0';after='1.26.5203.0';busy=0;code=0;write=$false},
    @{name='verified install';mode='install';expected='26.51.1';initial='1.26.5203.0';after='1.26.5101.0';busy=0;code=0;write=$true},
    @{name='wrong post-install version';mode='install';expected='26.51.1';initial='1.26.5203.0';after='1.26.5203.0';busy=0;code=1;write=$true},
    @{name='running game blocks change';mode='install';expected='26.51.1';initial='1.26.5203.0';after='1.26.5101.0';busy=1;code=1;write=$false},
    @{name='missing target blocks change';mode='install';expected='';initial='1.26.5203.0';after='1.26.5101.0';busy=0;code=1;write=$false}
)
foreach ($case in $cases) {
    $expectedArgument = if ($case.expected) { $case.expected } else { '__EMPTY__' }
    $output = & $powershell -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File $wrapper `
        -Target $target -Mode $case.mode -ExpectedId $expectedArgument -InitialVersion $case.initial `
        -AfterVersion $case.after -Busy $case.busy 2>&1 | Out-String
    $code = $LASTEXITCODE
    $wrote = $output.Contains('MOCK_PACKAGE_WRITE')
    if ($code -ne $case.code -or $wrote -ne $case.write) { throw "Case failed: $($case.name), code=$code`n$output" }
    Write-Output ('PASS: ' + $case.name)
}
Write-Output 'PowerShell syntax, six normalization and six mocked deployment cases passed. No real package/process commands executed.'
