<#
  Copyright (c) 2021-2023, NVIDIA CORPORATION. All rights reserved.

  Permission is hereby granted, free of charge, to any person obtaining a
  copy of this software and associated documentation files (the "Software"),
  to deal in the Software without restriction, including without limitation
  the rights to use, copy, modify, merge, publish, distribute, sublicense,
  and/or sell copies of the Software, and to permit persons to whom the
  Software is furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in
  all copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
  THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
  FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
  DEALINGS IN THE SOFTWARE.
#>

param(
  [switch]$SkipApics,
  # Unrecognized values would otherwise fall through to the arm64 cross toolchain in build_common.ps1.
  [ValidateSet("x64","arm64","arm64ec")]
  [string]$BuildArch = "x64"
)

.   ".\build_common.ps1"

$BuildFlavours = @("debug","debugoptimized","release")
$BuildSubDirs = @("_CompDebug","_CompDebugOptimized","_CompRelease")

$Backend = "ninja"
if ($BuildArch -eq "arm64ec") {
  # Meson+ninja cannot do arm64ec
  $Backend = "vs"
}

For ($i=0; $i -lt $BuildFlavours.Length; $i++) {
  $buildArgs = @{
    BuildFlavour = $BuildFlavours[$i]
    BuildSubDir = $BuildSubDirs[$i] + "_" + ${BuildArch}
    Backend = ${Backend}
    EnableTracy = 'false'
    BuildArch = ${BuildArch}
  }
  if ($SkipApics) { $buildArgs['SkipApics'] = $true }
  PerformBuild @buildArgs
}
