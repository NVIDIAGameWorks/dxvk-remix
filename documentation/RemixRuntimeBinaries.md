# Remix Runtime Binaries

Technical description of how the Remix runtime is split into binaries, how they are named, and how they are selected at load time.

## Overview

### x86 applications

Remix itself is a 64-bit program (which is a requirement for using Vulkan ray-tracing), but many classic games are 32-bit and cannot load 64-bit code. To bridge the gap, these games get a small 32-bit `d3d9.dll` - the Bridge client - that hands the game's graphics calls over to a separate 64-bit program - the Bridge server - which runs Remix. This is why x86 build ships with extra folders and files compared to x64 build.

The x86 Bridge client picks the server folder by the native architecture of the machine: `.trex_arm64` on ARM64, `.trex` otherwise. It launches the Bridge server from that folder, and the server loads `d3d9.dll` Remix implementation from the same folder.

To run the emulated x64 Bridge server on an ARM64 machine, set `forceX64Server = True` in `.trex_arm64/bridge.conf`.

The package file layout is the following:

```
rtx-remix-for-x86-games-*
├── d3d9.dll                    # x86 Bridge client
├── d3d9.pdb
├── .trex/
│   ├── NvRemixBridge.exe       # x64 Bridge server
│   ├── NvRemixBridge.pdb
│   ├── d3d9.dll                # x64 runtime implementation
│   ├── d3d9.pdb
│   └── runtime dependencies / usd/
└── .trex_arm64/
    ├── NvRemixBridge.exe       # ARM64 Bridge server
    ├── NvRemixBridge.pdb
    ├── d3d9.dll                # ARM64 runtime implementation
    ├── d3d9.pdb
    └── runtime dependencies / usd/
```

### x64 applications

For x64 applications the `d3d9.dll` is a thin proxy. The actual D3D9 and Remix API implementation lives in an architecture-specific DLL next to it. The package file layout is the following:

```
rtx-remix-for-x64-games-*
├── d3d9.dll                    # x64 shim: selects implementation
├── d3d9.pdb
├── d3d9_x64.dll                # x64 runtime implementation
├── d3d9_x64.pdb
├── d3d9_a64ec.dll              # ARM64EC runtime implementation
├── d3d9_a64ec.pdb
└── runtime dependencies / usd/
```

The shim is always built for x64 and so it can load both x64 and ARM64EC implementations. On Windows-on-Arm (WoA), the shim will first attempt to load the ARM64EC implementation and will fall back to x64 if it does not exist.

### Native ARM64 applications

The native ARM64 applications must use a separate native ARM64 Remix build. The ARM64 runtime is a separate deliverable and should not be mixed with the x86, x64 or ARM64EC files. The package file layout is the following:

```
rtx-remix-for-ARM64-games-*
├── d3d9.dll                    # ARM64 runtime implementation
├── d3d9.pdb
└── runtime dependencies / usd/
```

## D3D9 x64 shim technical details

### Implementation Selection

Implemented in `src/d3d9/d3d9_rtx_shim.cpp` (`initShim`), executed lazily on the first forwarded call.

1. Find the shim's own path, strip the extension, and append an architecture suffix. The suffixes (`_x64.dll`, `_a64ec.dll`) must match the naming in `src/d3d9/meson.build`.
2. Query the native machine with `IsWow64Process2`. If it is ARM64, choose `_a64ec.dll`, otherwise `_x64.dll`.
3. If the ARM64EC DLL is chosen but missing on disk, fall back to `_x64.dll`. The x64 implementation must therefore always ship.
4. The chosen DLL is loaded dynamically and every export is resolved with `GetProcAddress`. A missing DLL or export is fatal and is logged to `remix-shim.log`.

The implementation DLL is always loaded from the shim's directory, not the process directory.

### Exports

The shim forwards two groups of exports:

- Standard D3D9 exports (`Direct3DCreate9`, `D3DPERF_*`, ...).
- Remix exports (`remixapi_InitializeLibrary`, `QueryFeatureVersion`, `RtxSentry*`, and other internal entry points).

Rules for adding an export:

- Declare it with `REMIXAPI` and `REMIXAPI_CALL` (`src/util/util_export_macros.h`), not `DLLEXPORT`. These macros must stay in sync with `public/include/remix/remix_c.h`.
- Add a matching forwarder in `d3d9_rtx_shim.cpp`: a function pointer type, a `ShimExports` member, a `LOAD_SHIM_EXPORT` line, and the exported function. Without it the export exists in the implementation DLL but not in `d3d9.dll`, which breaks WoA.

### Build Configuration

Controlled by the `remix_with_shim` meson option (default `true`).

| Target | `remix_with_shim` | Output |
|--------|-------------------|--------|
| x64 | true | `d3d9.dll` (shim) and `d3d9_x64.dll` |
| ARM64EC | true | `d3d9_a64ec.dll` only |
| ARM64 (native) | any | `d3d9.dll` (implementation, no shim) |
| any | false | `d3d9.dll` (implementation, no shim) |

The `d3d9_dep` meson dependency points at the shim when it is built, so other targets do not link against the implementation.

The ARM64EC build does not produce a shim. Combine the outputs of the x64 and ARM64EC builds to get a complete WoA layout for x64 applications.
