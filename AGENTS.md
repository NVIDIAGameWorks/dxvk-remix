# AGENTS.md — dxvk-remix-nv

This file provides context for AI coding agents working in this repository.

## Project Overview

dxvk-remix is a fork of DXVK that overhauls the fixed-function D3D9 graphics pipeline for path-traced remastering of classic games.

The top-level runtime is built from the repo root and does **not** support x86 builds. Support for 32-bit games lives in `bridge/`, which contains the 32-bit bridge client plus the 64-bit bridge server/runtime handoff.

The repo remains Windows-first, but the Meson build and CI now contain x64, ARM64, and ARM64EC paths. Public-facing docs and release artifacts are still mostly x64-focused.

## Shell

Most repo build/deploy scripts are written for **PowerShell** or `cmd`, but agent sessions in this repo may run in a POSIX shell such as `bash` or `zsh`.

- Prefer POSIX-safe commands for repo inspection and editing.
- When invoking `.ps1`, `.bat`, or `.cmd` entry points, use the shell they expect. In PowerShell, use `;` to chain commands (not `&&`).
- If commands disagree across docs, trust the current Meson files and `tools/ci/gitlab/*.yml` first; use `.agents/skills/` as workflow guides, but verify exact command lines and build-dir names against CI when needed.

## Procedural Skills

Step-by-step workflows for common tasks are in `.agents/skills/`:

| Skill | Description |
|-------|-------------|
| `build-remix` | Build the project (first-time setup, incremental builds, reconfigure) |
| `deploy-and-test-game` | Deploy to a game target, launch, and debug |
| `run-unit-tests` | Build and run unit tests |

Some additional image/capture test instructions are in `tests/rtx/dxvk_rt_testing/AGENTS.md` (private NVIDIA GitLab only).

For exact current build/test command lines and build-dir names, prefer `tools/ci/gitlab/*.yml`; for example, unit-test CI currently uses `_CompUnitTest_x64`.

## C++ Coding Standards

Full guide: `documentation/CONTRIBUTING-style-guide.md`

### Key Rules

- **Indentation**: 2 spaces, no tabs.
- **Line wrapping**: Keep reasonably short lines unwrapped to minimize diff churn.
- **Braces**: Always use braces for `if`, `else`, `for`, `while`, etc. Opening brace on the same line.
- **Naming**:
  - Member variables: `m_` prefix (e.g. `m_value`)
  - Pointers: `p` prefix (e.g. `pInput`, `m_pPointer`)
  - Variables and functions: `camelCase`
  - Functions: Prefer short verb + object names aligned with the subsystem; avoid encoding implementation steps in the identifier (see `documentation/CONTRIBUTING-style-guide.md`, Naming Conventions).
  - Constants: `k` prefix and camelCase, i.e. `kConstantName`
  - Macros and defines: `UPPER_CASE`
  - Classes and structs: `PascalCase`
- **Conditions**: 
  - Test an integer flag by referencing the variable on its own, `if (flag)`, not `if (flag != 0)`. Applies to C++ and Slang, including `uint` flags in constant buffers. Bit tests like `(flags & kSomeBit) != 0` are unaffected.
  - When setting a `uint` flag in a shader constant buffer argument struct from a `bool`, assign it directly, `args.flag = enable;`, not `args.flag = enable ? 1u : 0u;`.
  - Put subjects first. Good: `if (pData == nullptr)`, Bad: `if (nullptr == pData)`.
  - Break long or complex ternaries after the condition, with `? A` and `: B` on the two following lines, each indented one level. Keep short ones on one line.
- **Includes**: Place them at the start of the file, after the copyright header, never partway through the code. Standard library first, then third-party, then local. Separate groups with blank lines.
- **Memory**: Prefer smart pointers (`std::unique_ptr`, `std::shared_ptr`). Use `Rc<T>` for GPU resources.
- **Profiling**: Use `ScopedCpuProfileZone()` / `ScopedGpuProfileZone(ctx, "name")` for performance-critical code.
- **Comments**:
  - Be as concise as you reasonably can. Most comments should fit into a single line.
  - Write proper sentences, even when that costs a few words. Do not join clauses with semicolons, and do not use noun-phrase fragments or shorthand that leaves the reader to guess what is meant, such as "Flags at every pixel." or "omitted, it is looked up".
  - A comment directly above or next to the function, variable or member it describes may leave that subject implied: "Returns the tile's active count.", not "This function returns the tile's active count."
  - When a comment lists alternate results or cases, give each its own sentence on its own line, like a list: "Returns the slot.", then "Returns the sentinel when the pixel is inactive." on the next line.
  - When a comment describes a group of declarations or statements inside a longer run of code, put an empty line above the comment and below the group, so the comment's scope is visible.
  - Break comment lines at natural places whenever possible: after a period, after a comma, or where an idea is finished. Do not end a line with the first word or two of a new sentence, and never leave a single word of a sentence or clause on its own line or at the start of the next one. Prefer pulling the word up onto the previous line, even if it runs a few characters long. If that is too long, break earlier at the start of the clause.
  - Describe the code as it is now. Do not contrast the current state with a previous one, or explain a change.
  - Do not explain things that can easily understood from reading class, function, or variable names.
  - Focus on recording non-obvious interactions and pitfalls.
  - If a long plain-english explanation of how a complicated set of systems interact is needed, that should go in a .md file in the `documentation/` folder.
    - Documentation about how to use a feature should be clearly separated from technical documenation about how a feature works.
    - In code comments should reference sections of that .md file, rather than repeating or trying to summarize.
  - Rational for why a change is needed should go into commit messages or MR descriptions, not the code.

### Changes to Upstream-Derived DXVK Code

Use NV-DXVK start/end blocks for changes to upstream-derived DXVK code, based on origin rather than directory. Mark every new change to upstream-derived code, including short changes, includes and declarations, even where older changes nearby are unmarked.

Remix-owned files do not use these blocks. Reuse an enclosing block without nesting. Extend an adjacent block for the same divergence. Use a separate block for an unrelated change.

See [the full rule](documentation/CONTRIBUTING-style-guide.md#changes-to-upstream-derived-dxvk-code) for provenance checks, scoped profiling and application-defaults exceptions, and vendored-library conventions.

```cpp
// NV-DXVK start: Brief description of change
// ... changed code ...
// NV-DXVK end
```

### Adding New Source Files

- New `.cpp` and `.h` files under `src/dxvk/` must be added to the `dxvk_src` list in `src/dxvk/meson.build` (alphabetically, keep related `.cpp` / `.h` entries adjacent).
- New unit tests in `tests/rtx/unit/` must be added to `tests/rtx/unit/meson.build`.
- The RTX shader compile pipeline is configured from the repo-root `meson.build` and scans `src/dxvk/shaders/rtx/`, so most new shader entry files do not need separate Meson registration.

### Adding New Exports

- Always use REMIXAPI macro for Remix exported functions, do NOT use DLLEXPORT macro.
- Always use REMIXAPI_CALL macro for calling convention for exported function.
- Always add new export function shims to d3d9_rtx_shim.cpp for correct shimming on WoA.

## RTX Options

- Add new options in the relevant feature class, not in `rtx_options.h`.
- Prefer `RTX_OPTION` / `RTX_OPTION_ENV` in the owning feature class or option struct.
- Use categorized string names: `RTX_OPTION("rtx.category", type, name, default, "Description.")`.
- Use `RTX_ENV_VAR` (not `DXVK_ENV_VAR`) for RTX-related environment variables.
- Enumerate enum values in descriptions: `0: First, 1: Second, ...`.
- Regenerate `RtxOptions.md` by running with `DXVK_DOCUMENTATION_WRITE_RTX_OPTIONS_MD=1`.

## Shader Code

Full guide: `src/dxvk/shaders/rtx/README.md`

Shaders use [Slang](https://github.com/shader-slang/slang) (GLSL-compatible). All RTX shaders live in `src/dxvk/shaders/rtx/`.

### File Extensions

| Extension | Purpose |
|-----------|---------|
| `*.h` | Structure definitions shared across files (and sometimes with CPU) |
| `*.slangh` | Implementations and helpers (this is what other files include) |
| `*.slang` | Generic or multi-stage shader entry files |
| `*.comp.slang` | Compute shaders |
| `*.vert.slang` | Vertex shaders |
| `*.frag.slang` | Fragment / pixel shaders |
| `*.rgen.slang` | Ray generation shaders |
| `*.rchit.slang` | Ray closest hit shaders |
| `*.rahit.slang` | Ray any hit shaders |
| `*.rmiss.slang` | Ray miss shaders |

### Naming

Files and folders use `lower_snake_case`, named after the primary type or functionality they provide.

### Shared C++/Shader Headers

Files in `src/dxvk/shaders/rtx/` with `.h` extension are often shared between C++ and Slang/HLSL via `#ifdef __cplusplus` guards or the `cpu_gpu_compat.h` helpers. Key examples:
- `rtx/pass/instance_definitions.h` — TLAS instance masks and custom-index encoding
- `rtx/utility/shader_types.h` — `vec4`, `vec3`, `vec2`, `uint` types compatible in both C++ and Slang
- `rtx/utility/cpu_gpu_compat.h` — cross-compile compatibility macros and helpers for dual-compile headers
- `rtx/pass/common_binding_indices.h` — Binding index constants

When modifying shared headers, ensure both C++ and Slang code paths remain consistent. For new dual-compile headers that need shared macros/utilities, include `rtx/utility/cpu_gpu_compat.h` at the top and `rtx/utility/cpu_gpu_compat_undef.h` at the end. GPU struct size/layout constants (e.g. `kSurfaceGPUSize`) must match the actual packed buffer layouts.

### Slang Conventions

- Declare only one variable per declaration.
- `mat4x3` for 3x4 matrices (3 rows of 4 columns, row-major in Slang).
- `f16vec3` / `float16_t` for half-precision where appropriate.
- `BUFFER_ARRAY(bufferName, bufferIndex, elementIndex)` macro for bindless buffer access.
- `BINDING_INDEX_INVALID` sentinel for missing buffer bindings.
- Struct properties use getter/setter patterns with bitfield packing (see `surface.h`).

### C++ GPU Conventions

- `Matrix4` for 4x4 matrices, `Vector4` for 4-component vectors.
- `vec4` from `shader_types.h` is `alignas(16)` — 16 bytes, compatible with GPU layout.
- GPU buffer writes use `memcpy` for matrix rows or direct `vec4` assignment.

## Pull Requests

- Squash into a single commit before submitting.
- Limit changes to those required for the PR goal — no drive-by style fixes.
- Add your name to `src/dxvk/imgui/dxvk_imgui_about.cpp` under "GitHub Contributors" (A-Z by last name).


## Key Directories

| Path | Description |
|------|-------------|
| `src/dxvk/rtx_render/` | Core RTX rendering/runtime code |
| `src/dxvk/rtx_render/graph/` | Remix graph system and graph components |
| `src/dxvk/shaders/rtx/` | RTX shader code, shared CPU/GPU headers, and pass entry points |
| `src/dxvk/imgui/` | ImGui integration and developer UI |
| `src/usd-plugins/` | USD schema plugins built with the runtime |
| `src/util/` | Shared utility code |
| `public/include/` | Public Remix API / SDK headers |
| `bridge/` | 32-bit to 64-bit bridge |
| `tests/rtx/unit/` | Unit tests |
| `tests/rtx/apps/` | Small runtime/API test applications |
| `tests/rtx/dxvk_rt_testing/` | Image/capture regression tests and tooling (private NVIDIA GitLab) |
| `documentation/` | Project documentation |
