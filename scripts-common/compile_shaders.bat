@echo off

set ROOT=%~dp0..

:: Set the DXVK_BUILD_FLAVOR environment variable to the name of your build folder, such as _Comp64DebugOptimized
if "%DXVK_BUILD_FLAVOR%" == "" set DXVK_BUILD_FLAVOR=_output

python.exe %ROOT%\scripts-common\compile_shaders.py ^
	-input %ROOT%\src\dxvk\shaders\rtx ^
	-output %ROOT%\%DXVK_BUILD_FLAVOR%\src\dxvk\rtx_shaders ^
	-include %ROOT%\src\dxvk\shaders ^
	-include %ROOT%\external\rtxdi\rtxdi-sdk\include ^
	-glslang %ROOT%\bin\glslangValidator.exe ^
	-slangc %ROOT%\bin\slangc.exe ^
	-parallel -debug
	