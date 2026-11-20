<#
.SYNOPSIS
  Build MyClient (OriginX) on Windows x64: the game DLL, then the HUD / launcher / bridge with MinGW-w64.

.DESCRIPTION
  Runs the same commands as source/tools/build_client.py (DLL, clang-cl + lld-link) and source/build.sh (MinGW-w64).
  There is exactly one build configuration: x64, optimized (/O2 /DNDEBUG, /MD). There is no Debug configuration.
  Run it from a Visual Studio "x64 Native Tools Command Prompt" / "Developer PowerShell" so INCLUDE and LIB point at
  the MSVC and Windows SDK (or pass -MsvcRoot), with python, clang-cl, lld-link, llvm-readobj and llvm-dlltool
  (LLVM) on PATH. The HUD part needs MinGW-w64 (x86_64-w64-mingw32-g++, windres) on PATH.

  Full output of every step is shown and also written to source\build\build.log (nothing is shortened).
  The last lines say SUCCEEDED or FAILED, with the failing step. The exit code is 0 only when every step passed.

.PARAMETER Games
  Game versions for the DLL. Default: 26.51.1, 26.60.5 and 26.45.1 (26.45.1 is standalone only: it is skipped without -Standalone).
.PARAMETER Standalone
  Build the DLL that runs without LeviLamina (build/client[-<ver>]-standalone). Without it the LeviLamina build is made.
.PARAMETER MsvcRoot
  An xwin "splat" root (crt/, sdk/). Leave empty inside a Visual Studio developer prompt.
.PARAMETER SdkRoot
  Where the client SDK is prepared (default: tools/prepare_client_sdk.py default).
.PARAMETER SkipDll
  Do not build the DLL.
.PARAMETER SkipHud
  Do not build the HUD / launcher / bridge / smoke test.
.PARAMETER AllowStlMismatch
  Set CL=/D_ALLOW_COMPILER_AND_STL_VERSION_MISMATCH for this run (needed when clang-cl is older than the MSVC STL
  requires, STL1000). The source is not changed.
.PARAMETER Python
  Python command (default: python).
.PARAMETER Cxx
  MinGW-w64 C++ compiler (default: $env:CXX, else x86_64-w64-mingw32-g++).

.EXAMPLE
  .\build.ps1 -Standalone
.EXAMPLE
  .\build.ps1 -Standalone -Games 26.51.1 -SkipHud
#>
[CmdletBinding()]
param(
    [string[]]$Games = @('26.51.1', '26.60.5', '26.45.1'),
    [switch]$Standalone,
    [string]$MsvcRoot = '',
    [string]$SdkRoot = '',
    [switch]$SkipDll,
    [switch]$SkipHud,
    [switch]$AllowStlMismatch,
    [string]$Python = 'python',
    [string]$Cxx = ''
)

$ErrorActionPreference = 'Continue'   # native stderr must not abort a step; each step's exit code is checked instead
[Console]::OutputEncoding = New-Object System.Text.UTF8Encoding($false)

$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
$Source = Join-Path $Root 'source'
$BuildDir = Join-Path $Source 'build'
New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null
$Log = Join-Path $BuildDir 'build.log'
Set-Content -Path $Log -Value ("build.ps1 started " + (Get-Date -Format 'yyyy-MM-dd HH:mm:ss')) -Encoding UTF8

if ($AllowStlMismatch) { $env:CL = '/D_ALLOW_COMPILER_AND_STL_VERSION_MISMATCH' }
if (-not $Cxx) { $Cxx = if ($env:CXX) { $env:CXX } else { 'x86_64-w64-mingw32-g++' } }

$Results = New-Object System.Collections.Generic.List[object]

function Invoke-Step {
    param([string]$Name, [string]$Exe, [string[]]$Arguments, [string]$WorkDir = $Source)
    $line = "=== $Name`n    $Exe $($Arguments -join ' ')`n    (in $WorkDir)"
    Write-Host $line
    Add-Content -Path $Log -Value $line
    Push-Location $WorkDir
    $code = 1
    try {
        if (-not (Get-Command $Exe -ErrorAction SilentlyContinue)) {
            $msg = "Command not found: $Exe (see BUILD.md, prerequisites)"
            Write-Host $msg -ForegroundColor Red
            Add-Content -Path $Log -Value $msg
        } else {
            # Whole output, nothing filtered: shown and appended to the log.
            & $Exe @Arguments 2>&1 | ForEach-Object { $_.ToString() } | Tee-Object -FilePath $Log -Append | Out-Host
            $code = $LASTEXITCODE
        }
    } catch {
        $msg = "Exception: $($_ | Out-String)"
        Write-Host $msg -ForegroundColor Red
        Add-Content -Path $Log -Value $msg
    } finally {
        Pop-Location
    }
    $ok = ($code -eq 0)
    $Results.Add([pscustomobject]@{ Step = $Name; ExitCode = $code; Ok = $ok })
    if (-not $ok) { Write-Host "STEP FAILED: $Name (exit code $code)" -ForegroundColor Red }
    return $ok
}

$failed = $false

# ---- DLL: source/tools/build_client.py (clang-cl, /O2 /DNDEBUG, x64) -----------------------------------------
if (-not $SkipDll) {
    foreach ($game in $Games) {
        if ($failed) { break }
        if ($game -eq '26.45.1' -and -not $Standalone) { Write-Host "DLL $game skipped (standalone only)"; continue }
        $dllArgs = @('tools/build_client.py', '--game', $game)
        if ($Standalone) { $dllArgs += '--standalone' }
        if ($MsvcRoot)   { $dllArgs += @('--msvc-root', $MsvcRoot) }
        if ($SdkRoot)    { $dllArgs += @('--sdk-root', $SdkRoot) }
        $label = "DLL $game" + $(if ($Standalone) { ' (standalone)' } else { ' (LeviLamina)' })
        if (-not (Invoke-Step -Name $label -Exe $Python -Arguments $dllArgs)) { $failed = $true }
    }
}

# ---- HUD / launcher / bridge: the commands of source/build.sh (MinGW-w64, C++17) -----------------------------
if (-not $SkipHud -and -not $failed) {
    $flags = @('-std=c++17', '-O2', '-Wall', '-Wextra', '-Wno-unknown-pragmas', '-DNOMINMAX', '-D_WIN32_WINNT=0x0601',
               '-static', '-static-libgcc', '-static-libstdc++')

    $machine = ''
    if (Get-Command $Cxx -ErrorAction SilentlyContinue) { $machine = (& $Cxx -dumpmachine 2>&1 | Out-String).Trim() }
    if ($machine -notlike 'x86_64-w64-mingw32*') {
        Write-Host "Windows x64 MinGW-w64 compiler required (got '$machine' from '$Cxx')." -ForegroundColor Red
        $Results.Add([pscustomobject]@{ Step = 'HUD: MinGW-w64 compiler check'; ExitCode = 1; Ok = $false })
        $failed = $true
    }

    if (-not $failed) {
        $steps = @(
            @{ Name = 'HUD: resources (windres)'; Exe = 'x86_64-w64-mingw32-windres'
               Args = @('-O', 'coff', '-o', 'build/myclient_res.o', 'myclient.rc') },
            @{ Name = 'HUD: bridge DLL'; Exe = $Cxx
               Args = $flags + @('-DMYCLIENT_BRIDGE_BUILD', '-shared', 'MyClientBridge.cpp', '-o', 'build/MyClientBridge.dll',
                                 '-Wl,--exclude-all-symbols', '-Wl,--out-implib,build/libMyClientBridge.dll.a') },
            @{ Name = 'HUD: MyClient.exe'; Exe = $Cxx
               Args = $flags + @('MyClient.cpp', 'MinecraftDataProvider.cpp', 'PingMonitor.cpp', 'ReplayPlayer.cpp',
                                 'build/myclient_res.o', '-o', 'build/MyClient.exe', '-mwindows', '-luser32', '-lgdi32',
                                 '-lshell32', '-lws2_32', '-lwinhttp', '-lwinmm', '-lole32', '-luuid', '-lwindowscodecs',
                                 '-lmsimg32') },
            @{ Name = 'HUD: MyClientLauncher.exe'; Exe = $Cxx
               Args = $flags + @('-Ilauncher/vendor', '-I.', 'launcher/MyClientLauncher.cpp',
                                 'launcher/vendor/tinyxml2/tinyxml2.cpp', 'build/myclient_res.o',
                                 '-o', 'build/MyClientLauncher.exe', '-municode', '-mwindows', '-luser32', '-lgdi32',
                                 '-lcomdlg32', '-lshell32', '-lole32', '-luuid', '-lbcrypt', '-lversion', '-lwinhttp',
                                 '-lws2_32', '-lwindowscodecs', '-lmsimg32') },
            @{ Name = 'HUD: bridge smoke test'; Exe = $Cxx
               Args = $flags + @('-I.', 'tests/bridge_smoke.cpp', 'MinecraftDataProvider.cpp', '-o', 'build/bridge_smoke.exe',
                                 'build/libMyClientBridge.dll.a', '-luser32') }
        )
        foreach ($step in $steps) {
            if ($failed) { break }
            if (-not (Invoke-Step -Name $step.Name -Exe $step.Exe -Arguments $step.Args)) { $failed = $true }
        }
    }
}

# ---- Result --------------------------------------------------------------------------------------------------
$summary = New-Object System.Collections.Generic.List[string]
$summary.Add('')
$summary.Add('================ build.ps1 result ================')
foreach ($r in $Results) { $summary.Add(('{0,-8} {1}  (exit {2})' -f $(if ($r.Ok) { 'OK' } else { 'FAILED' }), $r.Step, $r.ExitCode)) }
if ($Results.Count -eq 0) { $summary.Add('Nothing was built (-SkipDll and -SkipHud).') }
if (-not $failed -and $Results.Count -gt 0) {
    $summary.Add('Outputs (source\build): client[-<ver>][-standalone]\MyClientData.dll, MyClient.exe, MyClientLauncher.exe, MyClientBridge.dll')
    $summary.Add('BUILD SUCCEEDED')
} else {
    $summary.Add("BUILD FAILED - the error is in the output above; the full log is $Log")
}
$summary | ForEach-Object { Write-Host $_ -ForegroundColor $(if ($failed -or $Results.Count -eq 0) { 'Red' } else { 'Green' }) }
$summary | Add-Content -Path $Log
if ($failed -or $Results.Count -eq 0) { exit 1 } else { exit 0 }
