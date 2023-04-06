@echo off
setlocal
pushd %~dp0

Rem This scripts invokes xcopy to recursively copy directories on Windows
Rem we need to do the / to \ replacement because meson absolutely refuses to pass down \ here

set SRC="%1"
set DST="%2"

@REM echo ////////////////////////////////////////////////////
@REM echo  copy of %SRC:/=\%:
@REM echo     to
@REM echo     %DST:/=\%

rem robocopy seems to lock input files and ends up hanging sometimes (/w:1 helps, but doesn't solve the problem)
rem robocopy /w:1 /njh /njs /nc /ns /np /ndl /is /it /E %1 %2
xcopy /q /e /f /y %SRC:/=\% %DST:/=\%

if errorlevel 0 (
    rem echo  All files copied.
) else if errorlevel 1 (
    rem echo  No files copied. No changes detected.
) else (
    echo  Other thing happened while copying files.
    echo  Did you make sure there's an /_output dir?
)
@REM echo ////////////////////////////////////////////////////

@REM robocopy returns non-0  on successes + partial successes
exit /B 0