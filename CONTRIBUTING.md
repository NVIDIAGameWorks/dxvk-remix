# Contributing to RTX Remix

RTX Remix is an open-source project created and maintained by NVIDIA Corporation.  The codebase of DXVK-Remix and Bridge-Remix are provided under the MIT license, and Nvidia's official statement on the use of this repository and its submodules for projects is as follows:
> NVIDIA permits modders to build and share mods using NVIDIA technologies/binaries from our RTX Remix’s GitHub, the DXVK-Remix GitHub, or the Bridge-Remix GitHub.

If you are looking to contribute to the RTX Remix project, be mindful of a few quirks of the project and its 

### Learning Remix

A number of useful resources are available on the RTX Remix Showcase Discord, including a [list of easy-to-run applications with high compatibility](https://discord.com/channels/1028444667789967381/1198957423982018620).

### Bug Reports and Issues

All bug reports and requests should be filed on the RTX Remix repository.  Please avoid submitting reports to the dxvk-remix or bridge-remix repositories.

When an Issue is filed on GitHub, it is processed on an internal Jira board and assigned a ticket.  When this happens, an Nvidia engineer will be assigned to investigate and process this ticket in the near future.

Due to ticket volume and internal project demands, these tickets can take time-- be patient!  If you see the tracking number post, it has been seen and will be processed eventually.

#### Attachments and Essential Information

For all issues, please include:
- All of the information listed on the Bug Report template
- Any relevant .dmp files to the issue
- Any .conf files in use
- If relevant to the issue, run an API trace

Any relevant files should be attached to the issue directly.  If the file is too large or not an acceptable format for GitHub, please do the following:
1. Compile the files into a single ZIP folder.
2. Upload the ZIP folder to VirusTotal and run a scan.
3. On upload to GitHub or an acceptable mirror (e.g. DropBox, Google Drive), include the hash and link.
[Picture Demonstration]

Before submitting an Issue, it is recommended to discuss the issue in the [RTX Remix Showcase](https://discord.gg/rtxremix) community Discord where several Nvidia engineers and a community of experienced users will be able to assist in compiling info for a thorough and accurate report.  This can be done in the `runtime-help` forum or a dedicated `remix-projects` thread for the game/application in specific. 

#### Debugging and API Tracing

#### Reading Crash Info (Visual Studio Community 2019):

When Remix encounters an exception, the program will create a crash minidump before closing.  This dump file can be found in the game's `.trex` subfolder.

To view this, you must download DXVK-Remix and Bridge-Remix to your computer.  Please make sure that you are downloading exactly the same versions that were used in finding the exception.

In order to read the dump file, open it using Visual Studio Community 2019.  In the top right, a button saying `Debug with Native Only` will be available.  Upon clicking this button, you will be prompted to find the source code of either DXVK-Remix or Bridge-Remix.  Navigate to the appropriate folder and the exception will be shown at the exact line in which it occurred, with a callstack and log available. [Picture demonstration]

Note that in some cases, the exception may originate from the game itself.  Without source code and a pdb file, this will simply point to disassembly as opposed to human-readable code.

Please remember to include relevant dmp files for Issue reports.

#### Running an application with Visual Studio Community 2019 debugging:

Once Visual Studio Community is installed [preferrably 2019], you will need to install the following plugin:
- Microsoft Child Process Debugging Power Tool ([For VS2019](https://marketplace.visualstudio.com/items?itemName=vsdbgplat.MicrosoftChildProcessDebuggingPowerTool)) ([For VS2022](https://marketplace.visualstudio.com/items?itemName=vsdbgplat.MicrosoftChildProcessDebuggingPowerTool2022))

To run an application through Visual Studio:
1. Set up the game for testing by installing DXVK and Bridge Remix.  Do not leave out the PDB files.
2. Set up the code environment by downloading the source code for the exact DXVK and Bridge versions you are using and extract them to an accessible place.
3. Click and drag a game EXE into Visual Studio 2019.  A solution will be created to run the game.
4. Navigate to the `Debug` tab of the top ribbon, select `Other Debug Targets` from the drop-down menu, and select `Chid Process Debugging Settings`.
5. Enable the `Enable child process debugging` box and click `Save`.
6. Right-click the game EXE and select `Set as Startup Project`.
7. At the top hotbar, click `Start`.  The application will attempt to open with Visual Studio monitoring.  If successful, VS will run in the background and if an exception or breakpoint is found, the program will pause and bring VS2019 into focus.
8. If prompted, navigate to your DXVK/Bridge Remix installation [as needed] to locate the source file requested.
9. VS2019 will display the error or breakpoint encountered as well as a log and callstack.
[Picture demonstrations per step]

Note: If the application provides an unusual error on startup or fails to start, this may be the result of anti-debugging or DRM modules.  Many early Steam games may trigger these issues, as well as other games where the developers have deliberately included such security measures.  The easiest way to resolve such an issue is to purchase the game on a platform such as GOG and test the DRM-free version instead.

##### APITrace:

In some cases, it will be recommended to include an API Trace so that the engineer investigating your ticket can see how the DirectX API is being used in the original title, and how this differs from the current implementation.  An API trace will allow them to see what is being rendered frame by frame and how in order to find the issue.

In order to run an API Trace on a game, follow these steps:

1. Download APITrace.
    - NOTE: In most cases, you will be using the 32-bit version of the application as the primary use case of Remix is for 32-bit DirectX 9 games.
2. Extract the contents of the download to a place where it is accessible.  Note that API traces can be large in size (more than 15-20 GB in extreme cases) and that additional free space on the drive is highly recommended.  This can be slightly reduced by enabling VSync or adding a framerate limit to the game, so long as this does not impact the issue you are reporting.
3. In order to launch the game, open QAPITrace.exe and set up the interface to open the EXE of the game as a DirectX 9 app.  The application will be opened and the APITrace will begin, and if it hooks successfully, it will end when the application closes.
4. Save the API trace and upload with other bug report materials as described above.  Note that this file may be too large to upload to GitHub.

NOTE: Some applications will immediately be lost by the API trace program, leaving only the first frame of the application in the file or closing with an error.  When this happens, it is most likely the result of anti-debugging or DRM modules that are interfering with the program, and it is recommended to find another build of the game that is DRM-free; this can be done by searching for official patches for the game or by purchasing the game on GOG if possible.  Websites such as PCGamingWiki can also be useful in determining which versions of a game contain additional DRM. 


### Pull Requests

Pull Requests should be submitted to the relevant repository.  If the request is for documentation on the core RTX Remix repository, submit there.  If the pull request is for a submodule (DXVK-Remix or Bridge-Remix), send the pull request to those repositories instead.

#### Code Quality Standards

Please avoid unnecessary whitespaces and follow the code nesting standards seen elsewhere on the script you are currently working on.  Any unnecessary changes [e.g. unnecessary empty lines] can snowball into source control issues for other commits.  

An official style guide for C/C++ code does not exist for DXVK, and as of writing does not exist for DXVK-Remix either.  However, shader changes should adhere to the styling conventions listed here: [RTX Remix Shader Overview](https://github.com/NVIDIAGameWorks/dxvk-remix/blob/main/src/dxvk/shaders/rtx/README.md)

Whenever customizing rtx_options.h, ensure that any additional options are given a concise tooltip as seen in other lines.  This way, the autogenerated documentation on RTX Options will include information on how your added option is used.

#### Contribution Credits
When making a pull request, also remember to update [dxvk_imgui_about.cpp](https://github.com/NVIDIAGameWorks/dxvk-remix/blob/main/src/dxvk/imgui/dxvk_imgui_about.cpp) and add your name to the list.  Names are organized from A-Z by last name.

#### Cherry-Picking

While the source code of RTX Remix is made available via GitHub, internal development uses a private environment then pushes to GitHub.  The repository is open to contributions, but all pulls must be prepared as a single squashed commit so that the commit can be cherrypicked and merged internally.  When the process is complete, this will appear as two commits-- one from the contributor, and one from the merge.

This is done in order to keep a clean history of changes and not interrupt internal development branches more than necessary.

#### Squashing

Squashing is a process in which all of the changes across several commits are transformed into a single, clean commit with a single clean history.

When Squashing, make sure that you replace the automatic squashed commit history with a concise message describing exactly what your change does.

##### GitHub Desktop
- Press `Ctrl`-`Shift`-`H` or select `Branch` > `Merge into current branch...`, then select the branch you are squashing.  The branch will merge into itself.

##### Git Bash

1. `git checkout yourPRbranch`
2. `git merge --squash yourPRbranch`

#### Squash Troubleshooting

If a branch with a new commit has been pulling and merging with the main repository, this can make a clean commit history difficult.  If a squash is no longer possible, the workaround is as follows:

1. `Rebase and Merge` your PR with the latest changes from main.
2. Create a new branch based on `upstream/main`.  This will be a branch for a clean new history.
3. `Squash and Merge` from the PR branch to the new cleaned branch.  Do **not** include the full automatic squashed history changes; just give a concise and accurate description of what the changes do.
4. `Force Push` the cleaned branch to overwrite the PR branch.
5. Your PR branch will now have a clean and concise history with one commit, and you will appear as the only author.

##### GitHub Desktop

1. `Rebase and Merge` your PR branch to `upstream/main` by pressing `Branch` > `Rebase Current Branch`, or `Ctrl`-`Shift`-`E`.
2. Create a new branch by clicking the `Current branch` tab and selecting `New branch`.  The new branch should be based on `upstream/main` with no changes.
3. `Squash and Merge` the PR branch by pressing `Ctrl`-`Shift`-`H`, and select the cleaned branch you are currently squashing.
4. Press `Ctrl`-`` ` `` to open the Command Prompt.
5. Input the folllowing command, with `rebased-commit-branch` being your clean branch from step 3 and `original-dirty-pr-branch` being your dirty PR branch from step 1: 
    - `git push origin +rebased-commit-branch:original-dirty-pr-branch`
    - By adding a `+`, this becomes a force-push, overwriting the previous commit history for only the new branch's history.
6. You will now have a single squashed commit in your PR branch.
##### Git Bash

1. `git fetch`
2. `git pull --rebase upstream/main`
3. `git checkout -b rebased-commit-branch upstream/main`
4. `git merge --squash dirty-original-pr-branch:rebased-commit-branch`
5. `git push origin +rebased-commit-branch:original-dirty-pr-branch`

### Other Info


- RTX Remix API: https://docs.omniverse.nvidia.com/kit/docs/rtx_remix/latest/docs/contributing.html
- Omniverse C/C++ Coding Conventions: https://docs.omniverse.nvidia.com/kit/docs/carbonite/latest/CODING.html#c-c-coding-conventions