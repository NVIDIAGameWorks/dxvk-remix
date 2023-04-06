# RTX Remix

## Build instructions

### Requirements:
1. Windows 10 or 11
2. [Visual Studio ](https://visualstudio.microsoft.com/vs/older-downloads/)
    - VS 2019 is tested
    - VS 2022 may also work, but it is not actively tested
    - Note that our build system will always use the most recent version available on the system
3. [Meson](https://mesonbuild.com/) - v0.61.4 has been tested, latest version should work
    - Follow [instructions](https://mesonbuild.com/SimpleStart.html#installing-meson) on how to install and reboot the PC before moving on (Meson will indicate as much)
4. [Vulkan SDK](https://vulkan.lunarg.com/sdk/home#windows) - 1.3.211.0 or newer
    - You may need to uninstall previous SDK if you have an old version
5. [Python](https://www.python.org/downloads/) - version 3.9 or newer

#### Additional notes:
- If any dependency paths change (i.e. new Vulkan library), run `meson --reconfigure` in _Compiler64 directory via a command prompt. This may revert some custom VS project settings

### Generate and build dxvk_rt Visual Studio project 
1. Clone the repository with all submodules: <span style="color:red"> (ToDo: Update git path to the OSS git path)</style>
	- `git clone --recursive  https://gitlab-master.nvidia.com/lightspeedrtx/dxvk_rt.git`

	If the clone was made non-recursively and the submodules are missing, clone them separately:
	- `git submodule update --init --recursive`

2. Install all the [requirements](#requirements) before proceeding further

3. Make sure PowerShell scripts are enabled
    - One-time system setup: run `Set-ExecutionPolicy -ExecutionPolicy RemoteSigned` in an elevated PowerShell prompt, then close and reopen any existing PowerShell prompts
	
4. To generate and build dxvk_rt project, open a command prompt and run
    - `powershell -command "& .\build_dxvk_all_ninja.ps1"`
    - This will build all 3 configurations of dxvk_rt project inside subdirectories of the build tree: 
        - **_Comp64Debug** - full debug instrumentation, runtime speed may be slow
        - **_Comp64DebugOptimized** - partial debug instrumentation (i.e. asserts), runtime speed is generally comparable to that of release configuration
        - **_Comp64Release** - fastest runtime 
    - This will generate a project in the **_vs** subdirectory
    - Only x64 build targets are supported
    - A VSCode setup is available as well inside the .vscode directory and set up to use the build subdirectories generated in the previous step

5. Open **_vs/dxvk_rt.sln** in Visual Studio (2019+). 
    - Do not convert the solution on load if prompted when using a newer version of Visual Studio 
    - Once generated, the project can be built via Visual Studio or via powershell scripts
    - A build will copy generated DXVK DLLs to any target project as specified in **gametargets.conf** (see its [setup section](#deploy-built-binaries-to-a-game))

### Deploy built binaries to a game 
1. First time only: copy **gametargets.example.conf** to **gametargets.conf** in the project root

2. Update paths in the **gametargets.conf** for your game. Follow example in the **gametargets.example.conf**. Make sure to remove "#" from the start of all three lines

3. Open and, simply, re-save top-level **meson.build** file (i.e. via notepad) to update its time stamp, and rerun the build. This will trigger a full meson script run which will generate a project within the Visual Studio solution file and deploy built binaries into games' directories specified in **gametargets.conf**


## Project Documentation

- [Rtx Options](/RtxOptions.md)
