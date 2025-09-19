@echo off

set "BUILD_ROOT=%MESON_BUILD_ROOT%"
set "INSTALL_ROOT=%~1"

set "SRC_BASE=%BUILD_ROOT%\src\usd-plugins"
set "DST_BASE=%INSTALL_ROOT%\usd\plugins"

REM Make sure the top level plugins folder exists
if not exist "%DST_BASE%" (
    mkdir "%DST_BASE%"
)

REM Enable delayed expansion so we can use vars set inside the loop
setlocal enabledelayedexpansion

for /D %%D in ("%SRC_BASE%\*") do (
    set "PLUGDIR=%%~fD"
    set "NAME=%%~nD" 
    set "DST=%DST_BASE%\!NAME!"

    REM Ensure plugin directory exists
    if not exist "!DST!" (
        mkdir "!DST!"
    )

    REM Ensure resources directory exists
    if not exist "!DST!\resources" (
        mkdir "!DST!\resources"
    )

    REM Copy plugInfo.json into resources (required)
    copy /y "!PLUGDIR!\plugInfo.json" "!DST!\resources\" >nul

    REM Copy plugin binaries	
    for %%L in ("!PLUGDIR!\*.dll") do (
        copy /y "%%~L" "!DST!\" >nul
    )

    REM Copy generatedSchema.usda
    if exist "!PLUGDIR!\generatedSchema.usda" (
        copy /y "!PLUGDIR!\generatedSchema.usda" "!DST!\" >nul
    )
)

endlocal