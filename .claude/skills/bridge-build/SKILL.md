---
name: bridge-build
description: Build the Remix Bridge (32-bit client d3d9 + 64-bit server NvRemixBridge) via the bridge/build.ps1 wrapper. Use when the user asks to build, compile, compile-check, smoke-test, or force a clean rebuild of the bridge specifically — the bridge is a separate component from the main runtime and has its own build flow. Reports exit code + verifies artifacts. Do NOT invoke for runtime-side edits (src/) — those are rtx-build's domain.
---

# bridge-build

Wraps [`bridge/build.ps1`](../../../bridge/build.ps1). The script
discovers Visual Studio via vswhere, calls vcvarsall.bat by full path
(avoiding the `bridge/build_common.ps1::SetupVS` silent-failure trap),
runs `meson setup`/`compile` per arch in isolated cmd.exe shells (no env
contamination between x64 and x86), and verifies all three expected
artifacts before reporting success.

The bridge is the IPC layer that lets a 32-bit game talk to the 64-bit
Remix runtime. Client and server live under `bridge/` and build into
*separate* meson dirs (one per arch).

## When to invoke

- User asks to build, compile, compile-check, or smoke-test the bridge
- After a code change that touches `bridge/src/client/`,
  `bridge/src/server/`, `bridge/src/util/`, `bridge/src/launcher/`,
  or any IPC opcode/struct shared between client and server
- After a `public/include/remix/remix_c.h` change that affects the
  Remix C API surface (the bridge consumes that header)
- Before pushing changes that touch the bridge

## When NOT to invoke

- Edits only under `src/` or other runtime-side code — that's
  `rtx-build`'s domain
- Doc-only changes (`bridge/README.md`, `.md`, etc.)
- Skill / settings / config edits
- When the user asks for the main runtime build — invoke `rtx-build`
  instead

## Steps

1. From the project root, kick off the build with
   `run_in_background: true` (expected duration: ~30s incremental,
   2-4 min cold full for both arches):

   ```powershell
   & .\bridge\build.ps1
   ```

   For non-default flavor: `-Flavor debug` or `-Flavor debugoptimized`.
   For single-arch: `-Arch x64` or `-Arch x86`. Add `-Clean` to wipe
   the matching `_comp*` dir(s) first.

2. Wait for the task-completion notification — DO NOT poll, the
   runtime will wake you up when the background command exits.

3. Report back:
   - **Green (exit 0):** the script's own output ends with
     `=== Bridge build OK ===` and prints all three artifacts
     (x64 server, x86 client, x86 launcher) with mtimes. Quote those
     lines.
   - **Mixed-date warning:** if x64 server and x86 client mtimes are
     from different commits, flag this — IPC ABI risk if the command
     opcodes/structs in `bridge/src/util/` shifted between commits.
   - **Red (non-zero exit):** scan the captured output for
     `error C[0-9]`, `FAILED:`, `ninja: build stopped`. Quote the
     first ~10 error lines verbatim.

## Flavors

| Flavor | `--buildtype` | Build dirs |
|---|---|---|
| release (default) | `release` | `_compRelease_x64`, `_compRelease_x86` |
| debug | `debug` | `_compDebug_x64`, `_compDebug_x86` |
| debugoptimized | `debugoptimized` | `_compDebugOptimized_x64`, `_compDebugOptimized_x86` |

## Output layout

After a successful build, deployable binaries live in `bridge/_output/`:

```
bridge/_output/
├── d3d9.dll                    ← x86 client (canonical name; deploy AS-IS)
├── NvRemixLauncher32.exe       ← x86 injection launcher
├── d3d9.lib, d3d9.exp          ← link artifacts (not needed at runtime)
└── .trex/
    └── NvRemixBridge.exe       ← x64 server
```

> **PDBs may go stale.** The `copy_d3d9_to_output` custom_target globs
> `d3d9*` but in practice the linker may emit the .pdb after the copy
> step has already run, leaving stale debug symbols in `_output/`. If
> you need fresh symbols (crash analysis, dump inspection), copy
> `_compRelease_x86/src/client/d3d9.pdb` to `_output/d3d9.pdb`
> manually after the build.

## Gotchas

- **Don't bypass the script** by calling `build_bridge_release.bat`
  (or `_debug.bat`, `_all.bat`, etc.) directly. Under any subprocess
  context where the parent shell hasn't already loaded VS env, the
  .bat's `SetupVS`/vcvarsall lookup silently fails, the .bat exits 0
  with no x86 build output, and the .bat's empty captured stdout
  (`cmd.exe /c` stdio quirk) hides the failure. `bridge/build.ps1`
  was written specifically to fix that trap.
- **Two separate build dirs.** Unlike rtx-build (single dir), the
  bridge has `_compRelease_x64` (server only) and `_compRelease_x86`
  (client + launcher). Ninja runs against each independently — one
  side can rebuild while the other is a no-op when sources match.
- **MSVC v142 toolchain required.** Either VS2019 directly, or
  VS2022 with the v142 toolset Individual Component installed.
  The script's vswhere call uses `-version '[16.0,18.0)'` so either
  works.
- **`gametargets.conf` deploy path.** If the user has set up
  `bridge/gametargets.conf`, meson auto-deploys binaries into the
  configured game dirs as part of the build. The bridge's
  `gametargets.conf` output dir should be the *parent* of the
  `.trex/` folder (unlike the main runtime's gametargets, which
  points AT `.trex/`).
- **Bridge can run standalone** (without the main `dxvk-remix`
  runtime). In that case it falls back to system d3d9.dll — no
  pathtracing, but useful for IPC smoke tests.
