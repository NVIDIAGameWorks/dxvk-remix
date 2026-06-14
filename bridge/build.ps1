#requires -version 5.1
<#
.SYNOPSIS
  Build the Remix Bridge (32-bit client + 64-bit server) reliably.

.DESCRIPTION
  PowerShell wrapper around the meson/ninja bridge build. Discovers
  Visual Studio via vswhere, calls vcvarsall.bat by full path, and runs
  meson setup + compile per arch in isolated cmd.exe shells (no env
  contamination between x64 and x86). Streams output to the PowerShell
  pipeline so progress and errors are visible. Throws on any non-zero
  exit and verifies the expected build artifacts exist before reporting
  success.

  Resolves the failure mode in bridge/build_common.ps1::SetupVS where a
  missing C++ workload silently no-ops: cmd writes "'vcvarsall.bat' is
  not recognized" to stderr, the piped `set` dumps the existing env,
  the ForEach loop re-sets the same vars, and the script proceeds with
  no VC env loaded. Under build_bridge_release.bat the failure further
  hides because the .bat captures cmd's empty stdout and exits 0 with
  no client output.

.PARAMETER Flavor
  release (default), debug, debugoptimized.

.PARAMETER Arch
  x64, x86, or both (default).

.PARAMETER Clean
  Wipe the matching _comp* dir(s) before building.

.EXAMPLE
  .\bridge\build.ps1
  .\bridge\build.ps1 -Arch x86 -Clean
  .\bridge\build.ps1 -Flavor debugoptimized -Arch both
#>
[CmdletBinding()]
param(
    [ValidateSet('release','debug','debugoptimized')]
    [string]$Flavor = 'release',

    [ValidateSet('x64','x86','both')]
    [string]$Arch = 'both',

    [switch]$Clean
)

$ErrorActionPreference = 'Stop'

$BridgeRoot = $PSScriptRoot
$flavorTag = switch ($Flavor) {
    'release'        { 'Release' }
    'debug'          { 'Debug' }
    'debugoptimized' { 'DebugOptimized' }
}
$buildDirPrefix = "_comp$flavorTag"

# --- Discover Visual Studio ---
$vsWhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path $vsWhere)) {
    throw "vswhere.exe not found at '$vsWhere'. Install the Visual Studio Installer (ships with VS2017+)."
}
$vsPath = & $vsWhere -latest -version '[16.0,18.0)' -property installationPath
if ([string]::IsNullOrWhiteSpace($vsPath)) {
    throw "No Visual Studio 2019 or 2022 installation with v142 toolchain found via vswhere."
}
$vcvars = Join-Path $vsPath 'VC\Auxiliary\Build\vcvarsall.bat'
if (-not (Test-Path $vcvars)) {
    throw "vcvarsall.bat not found at '$vcvars'. The detected VS install may be incomplete (missing C++ build tools workload)."
}

Write-Host "VS install:  $vsPath"  -ForegroundColor DarkGray
Write-Host "vcvars:      $vcvars"   -ForegroundColor DarkGray
Write-Host "Bridge root: $BridgeRoot" -ForegroundColor DarkGray
Write-Host "Flavor:      $Flavor / $Arch" -ForegroundColor DarkGray

$arches = if ($Arch -eq 'both') { @('x64','x86') } else { @($Arch) }

Set-Location $BridgeRoot

foreach ($a in $arches) {
    $buildDir = "${buildDirPrefix}_$a"
    Write-Host ""
    Write-Host "=== Bridge $a $Flavor -> $buildDir ===" -ForegroundColor Cyan

    if ($Clean -and (Test-Path $buildDir)) {
        Write-Host "Wiping $buildDir for clean rebuild..." -ForegroundColor Yellow
        Remove-Item -Path $buildDir -Recurse -Force
    }

    $alreadyConfigured = Test-Path (Join-Path $buildDir 'meson-private')
    if ($alreadyConfigured) {
        # meson auto-reconfigures from inside the build dir if meson.build changed
        $chain = "call `"$vcvars`" $a >nul 2>&1 && cd $buildDir && meson compile"
    } else {
        $chain = "call `"$vcvars`" $a >nul 2>&1 && meson setup --buildtype $Flavor --backend ninja $buildDir && cd $buildDir && meson compile"
    }

    # Native-command stderr triggers PowerShell's NativeCommandError under
    # EAP=Stop even on exit 0. Exit code is the authoritative signal here.
    $prevEAP = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        cmd.exe /c $chain
        $exit = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $prevEAP
    }
    if ($exit -ne 0) {
        throw "Bridge $a $Flavor build failed (exit $exit)."
    }
}

# Sanity-check artifacts so the caller can trust exit 0
$expected = @{}
if ($arches -contains 'x64') {
    $expected["${buildDirPrefix}_x64/src/server/NvRemixBridge.exe"] = 'x64 server'
}
if ($arches -contains 'x86') {
    $expected["${buildDirPrefix}_x86/src/client/d3d9.dll"]            = 'x86 client'
    $expected["${buildDirPrefix}_x86/src/launcher/NvRemixLauncher32.exe"] = 'x86 launcher'
}

$missing = @()
foreach ($rel in $expected.Keys) {
    $abs = Join-Path $BridgeRoot $rel
    if (-not (Test-Path $abs)) { $missing += "$($expected[$rel]) ($rel)" }
}
if ($missing.Count -gt 0) {
    throw "Build reported success but artifacts are missing: $($missing -join ', ')"
}

Write-Host ""
Write-Host "=== Bridge build OK ===" -ForegroundColor Green
foreach ($rel in $expected.Keys) {
    $abs = Join-Path $BridgeRoot $rel
    $info = Get-Item $abs
    Write-Host ("  {0,-14} {1:yyyy-MM-dd HH:mm}  {2,10:N0} bytes  {3}" -f $expected[$rel], $info.LastWriteTime, $info.Length, $rel) -ForegroundColor Green
}
