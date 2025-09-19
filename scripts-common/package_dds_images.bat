@echo off
set defaultModPath=..\tests\rtx\dxvk_rt_testing\apics\Portal1\mods\gameReadyAssets

if not [%1] == [] (
  set modPath=%1
) else (
  if exist %defaultModPath% (
    set modPath=%defaultModPath%
    echo Mod was found in default location: %defaultModPath%
  ) else (
    echo Mod path is not specified and mod was not found in default location: %defaultModPath%
    goto :showUsage
  )
)

if not exist %modPath% (
  echo Mod folder %modPath% does not exist
  goto :showUsage
)

shift
set params=%1
shift
:paramLoop
if "%1" == "" goto paramsDone
set params=%params% %1
SHIFT
goto paramLoop
:paramsDone

call :normalizePath %modPath%
set modFullPath=%RETVAL%

dir /s /b "%modFullPath%\*.dds" > dds_files_list
..\external\rtxio\bin\RtxIoResourcePackager.exe -l dds_files_list -o "%modFullPath%\mod.pkg" -b "%modFullPath%" -c 12 -v -m 8 %params%
del dds_files_list

:exit
exit /b

:showUsage
echo.
echo Usage: package_dds_images.bat path_to_mod_folder [--split ^<MB^>] [--sort ^<name, time^>]
echo.
echo        --split ^<MB^>        - [optional] split packages based on approximate size in MB.
echo        --sort ^<name, time^> - [optional] sort resources by name or last write time.
goto :exit

:normalizePath
  set RETVAL=%~f1
  exit /b
