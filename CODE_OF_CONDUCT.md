# Contributing to RTX Remix

Welcome to dxvk-remix! We appreciate your interest in contributing. RTX Remix is an open-source project created and maintained by NVIDIA Corporation.  The codebase of DXVK-Remix is provided under the MIT license, and NVIDIA's official statement on the use of this repository and its submodules for projects is as follows:
> NVIDIA permits modders to build and share mods using NVIDIA technologies/binaries from our RTX Remix’s GitHub and the DXVK-Remix GitHub.

If you are looking to contribute to the RTX Remix project, be mindful of the following:

### Learning Remix

A number of useful resources are available on the [RTX Remix Discord](https://discord.gg/c7J6gUhXMk), including a [list of easy-to-run applications with high compatibility](https://discord.com/channels/1028444667789967381/1198957423982018620).

### Bug Reports and Issues

All bug reports and requests should be filed on the [RTX Remix](https://github.com/NVIDIAGameWorks/rtx-remix/issues) repository via New Issue button. Select an appropriate template for your issue/ask and fill out the requested information.  Please avoid submitting reports to the dxvk-remix repository.

Due to ticket volume and internal project demands, these tickets can take time-- be patient!

### Pull Requests

Pull Requests should be submitted to the relevant repository.  Pull requests for the main RTX Remix repository are rarely if ever needed.  If the pull request is for a submodule (DXVK-Remix), send the pull request to the repository instead.

#### Code Quality Standards

DXVK-Remix has two separate code guide style for C/C++ code and shader code:
 1) [RTX Remix C/C++ Style Guide](./documentation/CONTRIBUTING-style-guide.md)
 2) [RTX Remix Shader Style Guide](./src/dxvk/shaders/rtx/README.md)

Additionally, limit changes in your PR to changes that are required for the goal of the PR. Avoid making unnecessary code style changes/fixes to code outside the code of the PR, unless it's a dedicated PR that's addressing code style in particular.

#### Contribution Credits

When making a pull request, remember to update [dxvk_imgui_about.cpp](./src/dxvk/imgui/dxvk_imgui_about.cpp) and add your name to the list of "GitHub Contributors" on a new line.  Names are organized from A-Z by last name.

#### Cherry-Picking

While the source code of RTX Remix is made available via GitHub, internal development uses a private environment then pushes to GitHub.  The repository is open to contributions, but all pulls must be prepared as a single squashed commit so that the commit can be cherrypicked and merged internally.  When the process is complete, this will appear as two commits-- one from the contributor, and one from the merge.

This is done in order to keep a clean history of changes and not interrupt internal development branches more than necessary.

#### Squashing

Squashing is a process in which all of the changes across several commits are transformed into a single, clean commit with a single clean history.

When Squashing, make sure that you replace the automatic squashed commit history with a concise message describing exactly what your change does.

##### GitHub Desktop
- Press `Ctrl`-`Shift`-`H` or select `Branch` > `Merge into current branch...`, then select the branch you are squashing.  The branch will merge into itself with a clean history.  If an error occurs, see [Squash Troubleshooting](#squash-troubleshooting).

##### Git Bash

1. `git fetch`
2. `git checkout yourPRbranch`
3. `git merge --squash yourPRbranch`

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
3. `Squash and Merge` the dirty PR branch by selecting the clean branch, then pressing `Ctrl`-`Shift`-`H`.  Select the dirty branch and all commits will squashed and merged.  The clean branch now has your work as a single commit. 
4. Press `Ctrl`-`` ` `` to open the Command Prompt.
5. Input the following command, with `rebased-commit-branch` being your clean branch from step 3 and `original-dirty-pr-branch` being your dirty PR branch from step 1: 
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


- [RTX Remix API](https://docs.omniverse.nvidia.com/kit/docs/rtx_remix/latest/docs/contributing.html)
- [Community Discord](https://discord.gg/c7J6gUhXMk)
- [DXVK-Remix Documentation](./documentation/)
