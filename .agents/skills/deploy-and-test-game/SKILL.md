---
name: deploy-and-test-game
description: Deploy built dxvk-remix to a game and launch it. Use when the user asks to deploy, test, run, or debug a game with Remix, or when built changes need to be tested in a running game.
---

# Deploy and Test a Game

All commands run in PowerShell from the repo root unless otherwise noted.

## Game Targets

`gametargets.conf` (INI-style, not checked into git) defines the user's local game targets. Each section has:
- `outputdir` — the `.trex` folder where built DLLs are deployed
- `workingdir` — the game's working directory
- `commandline` — executable and arguments

The file should already be configured by the developer.

To list available targets:

```powershell
python vsgen/get_game_target_list.py
```

This outputs `GameName,outputdir` per line.

## Build, Deploy, and Launch

The recommended configuration for development is **debugoptimized** (`_Comp64DebugOptimized`).

### Step 1: Build (incremental)

```powershell
cd _Comp64DebugOptimized; meson compile -v
```

### Step 2: Deploy to a Game Target

Deploy built artifacts (d3d9.dll, DLSS, NRD, USD libs, etc.) to a game's `.trex` folder:

```powershell
meson install -C _Comp64DebugOptimized --tags <GameTarget> --only-changed
```

The tag is the section name from `gametargets.conf` (e.g. `Portal`, `HalfLife2_RTX`). The `--only-changed` flag skips files that haven't changed.

### Step 3: Launch the Game

Parse the target's `workingdir` and `commandline` from `gametargets.conf`, then launch:

```powershell
$env:DXVK_CONFIG_FILE = "<repo_root>\dxvk.conf"
Start-Process -FilePath "<workingdir>\<executable>" -ArgumentList "<args>" -WorkingDirectory "<workingdir>"
```

## Debugging Crashes

The runtime automatically generates `.dmp` crash dump files. When a crash occurs, analyze the dump to get the call stack and crash context.

## The Bridge (32-bit Games)

For 32-bit games, a two-process architecture is used:

1. The game loads a **32-bit `d3d9.dll`** (bridge client) which intercepts D3D9 calls
2. The client automatically launches **`NvRemixBridge.exe`** (64-bit server) from the `.trex` folder
3. The server loads the **64-bit `d3d9.dll`** (the actual Remix runtime) and renders into the game window
4. Communication between client and server uses shared-memory circular queues

When debugging bridge-based games, both processes may need inspection. The bridge disables IPC timeouts when a debugger is attached, so breakpoints won't cause disconnects. Bridge-specific configuration is in `bridge.conf`.
