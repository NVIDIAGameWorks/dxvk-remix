@echo off
setlocal EnableDelayedExpansion

REM %1 Input file for packman (xml file)
REM %2 Output file, in this case a log file

REM resolve %1/%2 to full paths
set "inputFile=%~f1"
set "outputFile=%~f2"

pushd %~dp0

call packman\packman pull "!inputFile:\=/!"
if errorlevel 1 (
    echo packman error, aborting!
    exit /B 1
)
echo Successfully updated deps > "!outputFile!"
