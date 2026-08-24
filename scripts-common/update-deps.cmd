@echo off
setlocal EnableDelayedExpansion

REM %1 Input file for packman (xml file)
REM %2 Output file, in this case a log file

REM resolve %1/%2 to full paths
set "inputFile=%~f1"
set "outputFile=%~f2"

pushd %~dp0

call packman\packman pull "!inputFile:\=/!" > "!outputFile!" 2>&1
set "packmanExitCode=!errorlevel!"
type "!outputFile!"
if not "!packmanExitCode!"=="0" (
    echo packman error, aborting!
    echo packman error, aborting! >> "!outputFile!"
    exit /B !packmanExitCode!
)
echo Successfully updated deps >> "!outputFile!"
