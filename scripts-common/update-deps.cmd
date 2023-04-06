@echo off
setlocal
pushd %~dp0

call ..\scripts-common\packman\packman pull -r cloudfront ..\packman-external.xml
if errorlevel 1 (
    echo packman error, aborting!
    exit /B 1
)
