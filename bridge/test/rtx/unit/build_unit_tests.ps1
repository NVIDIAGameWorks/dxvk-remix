#
# Copyright (c) 2024, NVIDIA CORPORATION. All rights reserved.
#
# Permission is hereby granted, free of charge, to any person obtaining a
# copy of this software and associated documentation files (the "Software"),
# to deal in the Software without restriction, including without limitation
# the rights to use, copy, modify, merge, publish, distribute, sublicense,
# and/or sell copies of the Software, and to permit persons to whom the
# Software is furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
# THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
# FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
# DEALINGS IN THE SOFTWARE.
#

function BuildUnitTests {
	param(
		[string]
		$Variant = "debugoptimized"
	)
	Write-Output "Building x86 unit tests..."
	& { 
		. $PSScriptRoot\build_unit_tests_variant.ps1; Build x86 $Variant
		if ( $LASTEXITCODE -ne 0 )  { Write-Error "x86 unit Test Building Failed!" }
	}
	Write-Output "Building x64 unit tests..."
	& {
		. $PSScriptRoot\build_unit_tests_variant.ps1; Build x64 $Variant
		if ( $LASTEXITCODE -ne 0 )  { Write-Error "x64 unit Test Building Failed!" }
	}
	
	$flavorToSubDir = @{ "debug" = "Debug"; "debugoptimized" = "DebugOptimized"; "release" = "Release" }
	$subdirNameComponent = $flavorToSubDir[$Variant]

	$x86BuildDir = "_comp" + $subdirNameComponent + "_UnitTest_x86"
	$x64BuildDir = "_comp" + $subdirNameComponent + "_UnitTest_x64"
	$src = $x86BuildDir + "\test\rtx\unit\*"
	$dst = $x64BuildDir + "\test\rtx\unit\"

	Write-Output "Copying x86 executables to x64 unit test directory"
	Copy-Item -Path $src -Destination $dst -Filter "*.exe"
	Copy-Item -Path $src -Destination $dst -Filter "*.pdb"
}

if ($args.count -eq 0) {
	BuildUnitTests
} elseif ($args.count -eq 1) {
	BuildUnitTests $args[0]
}
