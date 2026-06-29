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

function SetupBuild {
	param(
		[Parameter(Mandatory)]
		[string]$BuildArch,

		[string]$VcVarsVer
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

	#
	# Get path to Visual Studio installation using vswhere.
	#
	$vsPath = &$vsWhere -latest -version "[16.0,19.0)" -products * `
		-requires Microsoft.Component.MSBuild `
		-property installationPath -prerelease -nologo -utf8
	If ([string]::IsNullOrEmpty("$vsPath")) {
		Write-Error "Failed to find Visual Studio installation. Aborting." -ErrorAction Stop
	}
	Write-Host "Using Visual Studio installation at: $vsPath" -ForegroundColor Yellow

	If ( $BuildArch -eq "x64" ) {
		$Arch = "x64"
	} Else {
		$Arch = "amd64_arm64"
	}

	$vcvarsArgs = $Arch
	if ( -not [string]::IsNullOrEmpty($VcVarsVer) ) {
		$vcvarsArgs += " -vcvars_ver=${VcVarsVer}"
		Write-Host "Pinning MSVC toolset to version: ${VcVarsVer}" -ForegroundColor Yellow
	}

	#
	# Make sure the Visual Studio Command Prompt variables are set.
	#
	If (Test-Path env:LIBPATH) {
		Write-Host "Visual Studio Command Prompt variables already set." -ForegroundColor Yellow
	} Else {
		# Load VC vars
		$vcVarsOutput = cmd /v:on /c "set __VSCMD_ARG_NO_LOGO=1 & call `"$vsPath\VC\Auxiliary\Build\vcvarsall.bat`" ${vcvarsArgs} > nul && set"
		If ($LASTEXITCODE -ne 0) {
			Write-Error "Failed to initialize Visual Studio Command Prompt variables. Aborting." -ErrorAction Stop
		}
	}
	$vcVarsOutput | ForEach-Object {
		If ($_ -match '^\w.*=' -and $_ -notmatch '===') {
			$name, $value = $_ -split '=', 2
			Set-Item -Path "Env:\$($name)" -Value "$value" -Force
			$name, $value = $null
		}
	}
	Write-Host "Visual Studio Command Prompt variables set." -ForegroundColor Yellow
}

function PerformBuild {
	param(
		[Parameter(Mandatory)]
		[string]$Backend,

		[Parameter(Mandatory)]
		[string]$BuildFlavour,
		
		[Parameter(Mandatory)]
		[string]$BuildSubDir,

		[Parameter(Mandatory)]
		[string]$EnableTracy,

		[string]$BuildTarget,
		[string]$BuildArch,
		[string]$VcVarsVer,

		[string[]]$InstallTags,

		[bool]$ConfigureOnly = $false,

		[bool]$ShadersOnly = $false
	)

	if ( [string]::IsNullOrEmpty("$BuildArch") ) {
		$BuildArch = "x64"
	}

	SetupBuild -BuildArch $BuildArch -VcVarsVer $VcVarsVer

	$CurrentDir = Get-Location
	$OutputDir = [IO.Path]::Combine($CurrentDir, "_output")
	$BuildDir = [IO.Path]::Combine($CurrentDir, $BuildSubDir)

	Push-Location $CurrentDir
		$mesonArgs = @("--buildtype", "`"$BuildFlavour`"",
			"--backend", "`"$Backend`"", "-Denable_tracy=`"$EnableTracy`"", "`"$BuildSubDir`"")
		if ( $ShadersOnly ) {
			$mesonArgs += "-Ddownload_apics=False"
		}

		If ( $BuildArch -eq "arm64ec" ) {
			$mesonArgs += @("--cross-file", "build-wina64ec.txt")
		} Else { If ( $BuildArch -eq "arm64" ) {
			$mesonArgs += @("--cross-file", "build-wina64.txt")
		}}

		& meson setup $mesonArgs
	Pop-Location

	if ( $LASTEXITCODE -ne 0 ) {
		Write-Output "Failed to run meson setup"
		exit $LASTEXITCODE
	}

	if ($ShadersOnly) {
		Push-Location $BuildDir
			& meson compile rtx_shaders
		Pop-Location
		exit $LASTEXITCODE
	}

	if (!$ConfigureOnly) {
		$mesonArgs = @()
		If ( $BuildArch -eq "arm64ec" -and -not [string]::IsNullOrEmpty($VcVarsVer) ) {
			# force toolset version for msbuild
			$mesonArgs += @("--vs-args=/p:VCToolsVersion=$VcVarsVer")

			Write-Host "MSBuild toolset version forced to $VcVarsVer." -ForegroundColor Yellow
		}

		Push-Location $BuildDir
			& meson compile -v $mesonArgs

			if ($InstallTags -and $InstallTags.Count -gt 0) {
				# join array into comma-separated list
				$tagList = $InstallTags -join ','
				& meson install --tags $tagList
			}
			else {
				& meson install
			}
		Pop-Location

		if ( $LASTEXITCODE -ne 0 ) {
			Write-Output "Failed to run build step"
			exit $LASTEXITCODE
		}
	}
}