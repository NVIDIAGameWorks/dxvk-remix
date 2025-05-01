@ECHO OFF
REM
REM Copyright (c) 2022-2023, NVIDIA CORPORATION. All rights reserved.
REM
REM Permission is hereby granted, free of charge, to any person obtaining a
REM copy of this software and associated documentation files (the "Software"),
REM to deal in the Software without restriction, including without limitation
REM the rights to use, copy, modify, merge, publish, distribute, sublicense,
REM and/or sell copies of the Software, and to permit persons to whom the
REM Software is furnished to do so, subject to the following conditions:
REM
REM The above copyright notice and this permission notice shall be included in
REM all copies or substantial portions of the Software.
REM
REM THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
REM IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
REM FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
REM THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
REM LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
REM FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
REM DEALINGS IN THE SOFTWARE.
REM

powershell -command "& { . .\build_bridge.ps1; Build -Platform x64 -BuildFlavour debug -BuildSubDir _compDebug_x64 ; exit $LastExitCode }

powershell -command "& { . .\build_bridge.ps1; Build -Platform x64 -BuildFlavour debugoptimized -BuildSubDir _compDebugOptimized_x64 ; exit $LastExitCode }

powershell -command "& { . .\build_bridge.ps1; Build -Platform x64 -BuildFlavour release -BuildSubDir _compRelease_x64 ; exit $LastExitCode }

powershell -command "& { . .\build_bridge.ps1; Build -Platform x86 -BuildFlavour debug -BuildSubDir _compDebug_x86 ; exit $LastExitCode }

powershell -command "& { . .\build_bridge.ps1; Build -Platform x86 -BuildFlavour debugoptimized -BuildSubDir _compDebugOptimized_x86 ; exit $LastExitCode }

powershell -command "& { . .\build_bridge.ps1; Build -Platform x86 -BuildFlavour release -BuildSubDir _compRelease_x86 ; exit $LastExitCode }
