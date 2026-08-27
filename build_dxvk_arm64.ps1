<#
  Build dxvk-remix for native ARM64 (Windows on Arm).

  Usage: .\build_dxvk_arm64.ps1 -BuildFlavour release -BuildSubDir _CompA64Release
#>
param(
  [string]$BuildFlavour = 'debugoptimized',
  [string]$BuildSubDir  = '_CompA64DebugOptimized',
  [string]$RepoRoot     = $PSScriptRoot
)
$ErrorActionPreference = 'Stop'

# Prefer a native arm64 Python if one is installed; packman resolves its own interpreter either way.
$arm64Python = 'C:\Program Files\Python312-arm64'
if (Test-Path $arm64Python) {
  $env:Path = "$arm64Python;$arm64Python\Scripts;" + $env:Path
}

if (-not $env:NVM_GTLAPI_TOKEN) {
  $env:NVM_GTLAPI_TOKEN = @('User','Machine') |
    ForEach-Object { [Environment]::GetEnvironmentVariable('NVM_GTLAPI_TOKEN', $_) } |
    Where-Object { $_ } | Select-Object -First 1
}
if (-not $env:NVM_GTLAPI_TOKEN) {
  throw "NVM_GTLAPI_TOKEN is not set in any scope; packman needs it for the internal arm64 packages."
}

Write-Output "flavour: $BuildFlavour -> $BuildSubDir"

Set-Location $RepoRoot
. .\build_common.ps1
PerformBuild -BuildFlavour $BuildFlavour -BuildSubDir $BuildSubDir -Backend ninja -EnableTracy false -BuildArch arm64
Write-Output "BUILD_EXIT: $LASTEXITCODE"
