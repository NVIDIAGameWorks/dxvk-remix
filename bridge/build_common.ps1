#
# Copyright (c) 2022-2023, NVIDIA CORPORATION. All rights reserved.
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

function SetupVS {
	param(
		[Parameter(Mandatory)]
		[string]$Platform
	)
	#
	# Find vswhere (installed with recent Visual Studio versions).
	#
	If ($vsWhere = Get-Command "vswhere.exe" -ErrorAction SilentlyContinue) {
	  $vsWhere = $vsWhere.Path
	} ElseIf (Test-Path "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe") {
	  $vsWhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
	}
	 Else {
	  Write-Error "vswhere not found. Aborting." -ErrorAction Stop
	}
	Write-Host "vswhere found at: $vsWhere" -ForegroundColor Yellow


	#
	# Get path to Visual Studio installation using vswhere.
	#
	$vsPath = &$vsWhere -latest -version "[16.0,18.0)" -products * `
	 -requires Microsoft.Component.MSBuild `
	 -property installationPath
	If ([string]::IsNullOrEmpty("$vsPath")) {
	  Write-Error "Failed to find Visual Studio 2019 installation. Aborting." -ErrorAction Stop
	}
	Write-Host "Using Visual Studio installation at: ${vsPath}" -ForegroundColor Yellow


	#
	# Make sure the Visual Studio Command Prompt variables are set.
	#
	#If (Test-Path env:LIBPATH) {
	#  Write-Host "Visual Studio Command Prompt variables already set." -ForegroundColor Yellow
	#} Else {
	  # Load VC vars
	  Push-Location "${vsPath}\VC\Auxiliary\Build"

	  cmd /c "vcvarsall.bat $Platform &set" |
		ForEach-Object {
		  If ($_ -match "=") {
			$v = $_.split("="); Set-Item -Force -Path "ENV:\$($v[0])" -Value "$($v[1])"
		  }
		}
	  Pop-Location
	  Write-Host "Visual Studio Command Prompt variables set." -ForegroundColor Yellow
	#}
}

function PerformBuild {
	param(
	    [Parameter(Mandatory)]
		[string]$Backend,
		
		[Parameter(Mandatory)]
		[string]$Platform,

		[Parameter(Mandatory)]
		[string]$BuildFlavour,
		
		[Parameter(Mandatory)]
		[string]$BuildSubDir,
		
		[string]$BuildTarget
	)
	$CurrentDir = Get-Location
	$OutputDir = [IO.Path]::Combine($CurrentDir, "_output")
	$BuildDir = [IO.Path]::Combine($CurrentDir, $BuildSubDir)

	Push-Location $CurrentDir
		$mesonArgs = "setup --buildtype `"$BuildFlavour`" --backend `"$Backend`" `"$BuildSubDir`" --debug"
		if ( $BuildTarget ) {
	            $mesonArgs += " -Denable_tests=`"$true`""
	        }
		Start-Process "meson" -ArgumentList $mesonArgs -wait
	Pop-Location

	if ( $LASTEXITCODE -ne 0 ) {
		Write-Output "Failed to run meson setup"
		exit $LASTEXITCODE
	}
	
	# Note: Remove this if we modify Meson to include this copy step instead. For now only here so it only executes on build machines.
	Copy-Item "Directory.Build.Props" -Destination $BuildDir
	
	Push-Location $BuildDir
	    & meson compile 
	Pop-Location

	if ( $LASTEXITCODE -ne 0 ) {
		Write-Output "Failed to run msbuild step"
		exit $LASTEXITCODE
	}
}