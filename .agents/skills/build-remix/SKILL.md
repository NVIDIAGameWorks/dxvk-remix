---
name: build-remix
description: Build dxvk-remix from source. Use when the user asks to compile, build, or rebuild the project, or when code changes need to be compiled and tested.
---

# Build dxvk-remix

All commands run in PowerShell from the repo root.

## Prerequisites

Windows 10+, Visual Studio 2019 (2022 may work), Windows SDK 10.0.19041.0, Meson 1.8.2+, Vulkan SDK 1.4.313.2+, Python 3.9+.

## Build Configurations

| Build Directory | Flavour | Use Case |
|----------------|---------|----------|
| `_Comp64Debug` | `debug` | Full debug instrumentation, slow runtime |
| `_Comp64DebugOptimized` | `debugoptimized` | Asserts enabled, near-release speed (recommended for development) |
| `_Comp64Release` | `release` | Fastest runtime |

## First-Time Setup

### All configurations

```powershell
.\build_dxvk_all_ninja.ps1
```

This also generates the Visual Studio solution at `_vs/dxvk-remix.sln`.

### Single configuration

```powershell
.\build_dxvk.ps1 -BuildFlavour debugoptimized -BuildSubDir _Comp64DebugOptimized -Backend ninja -EnableTracy false
```

## Incremental Build (day-to-day)

Once a build directory exists, rebuild only changed files:

```powershell
meson compile -C _Comp64DebugOptimized
```

**Always use `meson compile`, never call `ninja` directly.** `meson compile` auto-activates the MSVC environment; calling `ninja` directly will fail because `cl.exe` is not on PATH.

This compiles all C++ targets and shaders together. Calling `meson compile` is faster than `build_dxvk.ps1` since it skips the setup step.

## Reconfigure After Dependency Changes

Run inside the relevant build directory when dependency paths change (e.g. new Vulkan SDK):

```powershell
cd _Comp64DebugOptimized; meson --reconfigure
```

## Shaders Only

Quick shader syntax check when C++ hasn't changed:

```powershell
meson compile -C _Comp64DebugOptimized rtx_shaders
```

Shader compiler is `slangc.exe` (Slang language). Compiled SPIR-V output goes to `_Comp64DebugOptimized/src/dxvk/rtx_shaders/*.spv`.
