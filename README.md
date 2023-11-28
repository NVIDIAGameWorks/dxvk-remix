# dxvk-remix

[![Build Status](https://github.com/NVIDIAGameWorks/dxvk-remix/actions/workflows/build.yml/badge.svg)](https://github.com/NVIDIAGameWorks/dxvk-remix/actions/workflows/build.yml)

dxvk-remix is a fork of the [DXVK](https://github.com/doitsujin/dxvk) project, which overhauls the fixed-function graphics pipeline implementation in order to remaster games with path tracing.

Thanks to all the contributors to DXVK for creating this foundational piece of software, on top of which we were able to build the RTX Remix Runtime.

While dxvk-remix is a fork of DXVK, please report bugs encountered with dxvk-remix to this repo rather than to the DXVK project.

## Build instructions

### Requirements:
1. Windows 10 or 11
2. [Visual Studio ](https://visualstudio.microsoft.com/vs/older-downloads/)
    - VS 2019 is tested
    - VS 2022 may also work, but it is not actively tested
    - Note that our build system will always use the most recent version available on the system
3. [Windows SDK](https://developer.microsoft.com/en-us/windows/downloads/sdk-archive/)
    - 10.0.19041.0 is tested
4. [Meson](https://mesonbuild.com/) - v0.61.4 has been tested, v1.2.0 does not work (missing library)
    - Follow [instructions](https://mesonbuild.com/SimpleStart.html#installing-meson) on how to install and reboot the PC before moving on (Meson will indicate as much)
5. [Vulkan SDK](https://vulkan.lunarg.com/sdk/home#windows) - 1.3.211.0 or newer
    - You may need to uninstall previous SDK if you have an old version
6. [Python](https://www.python.org/downloads/) - version 3.9 or newer

#### Additional notes:
- If any dependency paths change (i.e. new Vulkan library), run `meson --reconfigure` in _Compiler64 directory via a command prompt. This may revert some custom VS project settings

### Generate and build dxvk-remix Visual Studio project 
1. Clone the repository with all submodules:
	- `git clone --recursive https://github.com/NVIDIAGameWorks/dxvk-remix.git`

	If the clone was made non-recursively and the submodules are missing, clone them separately:
	- `git submodule update --init --recursive`

2. Install all the [requirements](#requirements) before proceeding further

3. Make sure PowerShell scripts are enabled
    - One-time system setup: run `Set-ExecutionPolicy -ExecutionPolicy RemoteSigned` in an elevated PowerShell prompt, then close and reopen any existing PowerShell prompts
	
4. To generate and build dxvk-remix project:
    - Right Click on `dxvk-remix\build_dxvk_all_ninja.ps1` and select "Run with Powershell"
    - If that fails or has problems, run the build manually in a way you can read the errors:
        - open a windows file explorer to the `dxvk-remix` folder
        - type `cmd` in the address bar to open a command line window in that folder.
        - copy and paste `powershell -command "& .\build_dxvk_all_ninja.ps1"` into the command line, then press enter
    - This will build all 3 configurations of dxvk-remix project inside subdirectories of the build tree: 
        - **_Comp64Debug** - full debug instrumentation, runtime speed may be slow
        - **_Comp64DebugOptimized** - partial debug instrumentation (i.e. asserts), runtime speed is generally comparable to that of release configuration
        - **_Comp64Release** - fastest runtime 
    - This will generate a project in the **_vs** subdirectory
    - Only x64 build targets are supported

5. Open **_vs/dxvk-remix.sln** in Visual Studio (2019+). 
    - Do not convert the solution on load if prompted when using a newer version of Visual Studio 
    - Once generated, the project can be built via Visual Studio or via powershell scripts
    - A build will copy generated DXVK DLLs to any target project as specified in **gametargets.conf** (see its [setup section](#deploy-built-binaries-to-a-game))

### Deploy built binaries to a game 
1. First time only: copy **gametargets.example.conf** to **gametargets.conf** in the project root

2. Update paths in the **gametargets.conf** for your game. Follow example in the **gametargets.example.conf**. Make sure to remove "#" from the start of all three lines

3. Open and, simply, re-save top-level **meson.build** file (i.e. via notepad) to update its time stamp, and rerun the build. This will trigger a full meson script run which will generate a project within the Visual Studio solution file and deploy built binaries into games' directories specified in **gametargets.conf**


## Project Documentation

- [Rtx Options](/RtxOptions.md)
- [Anti-Culling System](/documentation/AntiCullingSystem.md)
- [Foliage System](/documentation/FoliageSystem.md)
- [Opacity Micromap](/documentation/OpacityMicromap.md)
- [Terrain System](/documentation/TerrainSystem.md)
- [Unit Test](/documentation/UnitTest.md)
