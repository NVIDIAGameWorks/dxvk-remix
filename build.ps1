#requires -version 5.1
<#
.SYNOPSIS
  Build the dxvk-remix runtime (64-bit d3d9.dll) reliably.

.DESCRIPTION
  PowerShell wrapper around the meson/ninja runtime build. Discovers
  Visual Studio via vswhere, calls vcvarsall.bat by full path, and runs
  meson setup + compile + install in a single isolated cmd.exe shell so
  VS env stays consistent across steps. Streams output to the PowerShell
  pipeline so progress and errors are visible. Throws on any non-zero
  exit and verifies the expected build artifacts exist before reporting
  success.

  Resolves the failure mode in build_common.ps1::SetupVS where a missing
  C++ workload silently no-ops: cmd writes "'vcvarsall.bat' is not
  recognized" to stderr, the piped `set` dumps the existing env, the
  ForEach loop re-sets the same vars, and the script proceeds with no
  VC env loaded. meson then surfaces a cryptic "cl.exe not found"
  downstream, hiding the real cause.

.PARAMETER Flavor
  release (default), debug, debugoptimized.

.PARAMETER EnableTracy
  Enable Tracy profiler integration (-Denable_tracy=true).

.PARAMETER Clean
  Wipe the matching _Comp64* dir before building.

.EXAMPLE
  .\build.ps1
  .\build.ps1 -Flavor debugoptimized
  .\build.ps1 -Clean -EnableTracy
#>
[CmdletBinding()]
param(
    [ValidateSet('release','debug','debugoptimized')]
    [string]$Flavor = 'release',

    [switch]$EnableTracy,

    [switch]$Clean
)

$ErrorActionPreference = 'Stop'

$RepoRoot = $PSScriptRoot
$flavorTag = switch ($Flavor) {
    'release'        { 'Release' }
    'debug'          { 'Debug' }
    'debugoptimized' { 'DebugOptimized' }
}
$buildDir = "_Comp64$flavorTag"
$tracyFlag = if ($EnableTracy) { 'true' } else { 'false' }

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

Write-Host "VS install: $vsPath"  -ForegroundColor DarkGray
Write-Host "vcvars:     $vcvars"   -ForegroundColor DarkGray
Write-Host "Repo root:  $RepoRoot" -ForegroundColor DarkGray
Write-Host "Build dir:  $buildDir" -ForegroundColor DarkGray
Write-Host "Tracy:      $tracyFlag" -ForegroundColor DarkGray

Set-Location $RepoRoot

# Pre-cleanup. nv-private/ and tests/rtx/dxvk_rt_testing/ are untracked
# install targets that meson regenerates on every install. A stale copy
# left over from a prior build can confuse the next install pass into
# copying or referencing the wrong files. Known-issue workaround.
Remove-Item -Path .\nv-private -Recurse -Force -Confirm:$false -ErrorAction SilentlyContinue
Remove-Item -Path .\tests\rtx\dxvk_rt_testing -Recurse -Force -Confirm:$false -ErrorAction SilentlyContinue

if ($Clean -and (Test-Path $buildDir)) {
    Write-Host "Wiping $buildDir for clean rebuild..." -ForegroundColor Yellow
    Remove-Item -Path $buildDir -Recurse -Force
}

Write-Host ""
Write-Host "=== Runtime $Flavor -> $buildDir ===" -ForegroundColor Cyan

$alreadyConfigured = Test-Path (Join-Path $buildDir 'meson-private')
if ($alreadyConfigured) {
    # meson auto-reconfigures from inside the build dir if meson.build changed
    $chain = "call `"$vcvars`" x64 >nul 2>&1 && cd $buildDir && meson compile -v && meson install"
} else {
    $chain = "call `"$vcvars`" x64 >nul 2>&1 && meson setup --buildtype $Flavor --backend ninja -Denable_tracy=$tracyFlag $buildDir && cd $buildDir && meson compile -v && meson install"
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
    throw "Runtime $Flavor build failed (exit $exit)."
}

# Sanity-check artifacts so the caller can trust exit 0
$artifacts = @{
    "$buildDir/src/d3d9/d3d9.dll" = 'runtime DLL (build dir)'
    '_output/d3d9.dll'            = 'runtime DLL (deploy)'
}

$missing = @()
foreach ($rel in $artifacts.Keys) {
    $abs = Join-Path $RepoRoot $rel
    if (-not (Test-Path $abs)) { $missing += "$($artifacts[$rel]) ($rel)" }
}
if ($missing.Count -gt 0) {
    throw "Build reported success but artifacts are missing: $($missing -join ', ')"
}

Write-Host ""
Write-Host "=== Runtime build OK ===" -ForegroundColor Green
foreach ($rel in $artifacts.Keys) {
    $abs = Join-Path $RepoRoot $rel
    $info = Get-Item $abs
    Write-Host ("  {0,-24} {1:yyyy-MM-dd HH:mm}  {2,12:N0} bytes  {3}" -f $artifacts[$rel], $info.LastWriteTime, $info.Length, $rel) -ForegroundColor Green
}

# --- Optional auto-deploy to user-local game install paths ---
#
# If `auto-deploy.local.ps1` exists at the repo root (gitignored),
# dot-source it and copy `_output/d3d9.dll` to each path listed in
# `$AutoDeployTargets`. Lets contributors skip the manual copy/paste
# into their test game install without leaking machine-specific paths
# into the tracked tree.
#
# Local file template:
#   $AutoDeployTargets = @(
#     'C:\path\to\game\.trex'
#   )
$autoDeployScript = Join-Path $RepoRoot 'auto-deploy.local.ps1'
if (Test-Path $autoDeployScript) {
    $AutoDeployTargets = @()
    . $autoDeployScript
    if ($AutoDeployTargets.Count -gt 0) {
        Write-Host ""
        Write-Host "=== Auto-deploy (from auto-deploy.local.ps1) ===" -ForegroundColor Cyan
        $sourceDll = Join-Path $RepoRoot '_output/d3d9.dll'
        foreach ($target in $AutoDeployTargets) {
            if (-not (Test-Path $target)) {
                Write-Host "  SKIP (target dir missing): $target" -ForegroundColor Yellow
                continue
            }
            $destDll = Join-Path $target 'd3d9.dll'
            try {
                Copy-Item -Path $sourceDll -Destination $destDll -Force
                $info = Get-Item $destDll
                Write-Host ("  copied -> {0:yyyy-MM-dd HH:mm}  {1,12:N0} bytes  {2}" -f $info.LastWriteTime, $info.Length, $destDll) -ForegroundColor Green
            } catch {
                Write-Host "  FAIL: $target -- $($_.Exception.Message)" -ForegroundColor Red
            }
        }
    }
}
