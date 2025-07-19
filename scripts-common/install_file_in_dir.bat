@echo off
setlocal enabledelayedexpansion

REM %1 = source file
REM %2 = destination directory

REM Normalize separators to backslashes
set "SRC=%~1"
set "DST_DIR=%~2"
set "SRC=!SRC:/=\!"
set "DST_DIR=!DST_DIR:/=\!"

REM echo SRC      = "!SRC!"
REM echo DST_DIR  = "!DST_DIR!"

REM Ensure target folder exists
if not exist "!DST_DIR!" (
  md "!DST_DIR!" || (
    echo ERROR: failed to create "!DST_DIR!" >&2
    exit /b 1
  )
)

REM Copy and check result
copy /y "!SRC!" "!DST_DIR!" >nul || (
  echo ERROR: failed to copy "!SRC!" to "!DST_DIR!" >&2
  exit /b 1
)

exit /b 0