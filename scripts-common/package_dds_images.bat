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

call :normalizePath %modPath%
set modFullPath=%RETVAL%

dir /s /b "%modFullPath%\*.dds" > dds_files_list
..\external\rtxio\bin\RtxIoResourcePackager.exe -l dds_files_list -o "%modFullPath%\mod_00.pkg" -b "%modFullPath%" -c 12 -v -m 8
del dds_files_list

:exit
exit /b

:showUsage
echo Usage: package_dds_images.bat path_to_mod_folder
goto :exit

:normalizePath
  set RETVAL=%~f1
  exit /b
