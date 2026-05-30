---
name: rtx-build
description: Build the dxvk-remix runtime (64-bit d3d9.dll) via the repo-root build.ps1 wrapper. Use when the user asks to build, compile, compile-check, verify, or smoke-test the runtime. Runs in the background, reports exit code + verifies artifacts. Do NOT invoke for doc/skill/config-only edits — those don't affect compilation.
---

# rtx-build

Wraps [`build.ps1`](../../../build.ps1) at the repo root. The script
discovers Visual Studio via vswhere, calls vcvarsall.bat by full path
(avoiding the `build_common.ps1::SetupVS` silent-failure trap), runs
`meson setup`/`compile`/`install` in a single isolated cmd.exe shell,
and verifies build artifacts exist before reporting success.

## When to invoke

- User asks to build, compile, compile-check, verify, or smoke-test
- Before pushing code changes
- After a code change that touches runtime sources under `src/`,
  `include/`, or `public/`

## When NOT to invoke

- Doc-only changes (`.md`, `documentation/`, `.claude/`) — no
  compile impact
- Skill / settings / config edits
- Bridge-only changes — invoke `bridge-build` instead
- Before the user has finished their code edits — building against a
  work-in-progress tree wastes time

## Steps

1. From the project root, kick off the build with
   `run_in_background: true` (expected duration: ~30s incremental,
   10-15 min cold full):

   ```powershell
   & .\build.ps1
   ```

   For non-default flavor, add `-Flavor debug` or
   `-Flavor debugoptimized`. Add `-Clean` to wipe the build dir
   first. Add `-EnableTracy` to enable the Tracy profiler.

2. Wait for the task-completion notification — DO NOT poll, the
   runtime will wake you up when the background command exits.

3. Report back:
   - **Green (exit 0):** the script's own output ends with
     `=== Runtime build OK ===` and prints the artifact paths +
     mtimes. Quote those lines.
   - **Red (non-zero exit):** scan the captured output for
     `error C[0-9]`, `fatal error`, `FAILED:`, `ninja: build stopped`,
     or `Failed to run`. Quote the first ~10 error lines so the user
     sees the real cause before any fix attempt.

## Flavors

| Flavor | `--buildtype` | Build dir |
|---|---|---|
| release (default) | `release` | `_Comp64Release` |
| debug | `debug` | `_Comp64Debug` |
| debugoptimized | `debugoptimized` | `_Comp64DebugOptimized` |

## Gotchas

- **`LNK4098: defaultlib 'LIBCMT' conflicts`** is a pre-existing
  warning, not a failure.
- **`WARNING: msvc does not support C++11; attempting best effort`**
  is upstream noise, not a failure.
- **Build produces `d3d9.dll`** in `_Comp64Release/src/d3d9/` and
  installs it to `_output/`, `public/bin/`, and the test app dirs.
- **Don't bypass the script** by calling `build_common.ps1::PerformBuild`
  directly. It works, but its `SetupVS` function lies about success
  when vcvarsall.bat lookup fails — invisible until the next
  consumer hits a stale build. `build.ps1` is the audited entry
  point that doesn't have that trap.
- **Optional auto-deploy:** drop an `auto-deploy.local.ps1` at the
  repo root (gitignored) that sets `$AutoDeployTargets = @('C:\path\to\game\.trex')`.
  The build will copy `_output/d3d9.dll` to each listed path after a
  successful build.
