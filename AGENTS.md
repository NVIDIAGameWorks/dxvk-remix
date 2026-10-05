# AGENTS.md — dxvk-remix-nv

This file provides context for AI coding agents working in this repository.

## Project Overview

dxvk-remix is a fork of DXVK that overhauls the fixed-function D3D9 graphics pipeline for path-traced remastering of classic games. The `bridge` subfolder enables 32-bit games to communicate with the 64-bit runtime.

Only x64 build targets are supported.

## Shell

Commands are run in **PowerShell**. Use `;` to chain commands (not `&&`).

## Procedural Skills

Step-by-step workflows for common tasks are in `.agents/skills/`:

| Skill | Description |
|-------|-------------|
| `build-remix` | Build the project (first-time setup, incremental builds, reconfigure) |
| `deploy-and-test-game` | Deploy to a game target, launch, and debug |
| `run-unit-tests` | Build and run unit tests |

Some additional test instructions are in `tests/rtx/dxvk_rt_testing/AGENTS.md` (private NVIDIA GitLab only).

## C++ Coding Standards

Full guide: `documentation/CONTRIBUTING-style-guide.md`

### Key Rules

- **Indentation**: 2 spaces, no tabs.
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
  - Put subjects first. Good: `if (pData == nullptr)`, Bad: `if (nullptr == pData)`.
  - Break long or complex ternaries after the condition, with `? A` and `: B` on the two following lines, each indented one level. Keep short ones on one line.
- **Includes**: Standard library first, then third-party, then local. Separate groups with blank lines.
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

### Changes to Core DXVK Files (Applies to code files outside of `rtx_render`)

Wrap diverging code in comment blocks:

```cpp
// NV-DXVK start: Brief description of change
// ... changed code ...
// NV-DXVK end
```

### Adding New Source Files

- New `.cpp` and `.h` files must be added to the `dxvk_src` list in `src/dxvk/meson.build` (alphabetically, both `.cpp` and `.h` on adjacent lines).
- New shader files (`.comp.slang`, `.rgen.slang`, etc.) are auto-discovered from `src/dxvk/shaders/rtx/` — no build system registration needed.

### Adding New Exports

- Always use REMIXAPI macro for Remix exported functions, do NOT use DLLEXPORT macro.
- Always use REMIXAPI_CALL macro for calling convention for exported function.
- Always add new export function shims to d3d9_rtx_shim.cpp for correct shimming on WoA.

## RTX Options

- Add new options in the relevant feature class, not in `rtx_options.h`.
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
| `*.comp.slang` | Compute shaders |
| `*.rgen.slang` | Ray generation shaders |
| `*.rchit.slang` | Ray closest hit shaders |
| `*.rahit.slang` | Ray any hit shaders |
| `*.rmiss.slang` | Ray miss shaders |

### Naming

Files and folders use `lower_snake_case`, named after the primary type or functionality they provide.

### Shared C++/Shader Headers

Files in `src/dxvk/shaders/rtx/` with `.h` extension are shared between C++ and Slang via `#ifdef __cplusplus` guards. Key examples:
- `rtx/pass/instance_data.h` — `InstanceData` struct (per-TLAS-instance data)
- `rtx/utility/shader_types.h` — `vec4`, `vec3`, `vec2`, `uint` types compatible in both C++ and Slang
- `rtx/pass/common_binding_indices.h` — Binding index constants

When modifying shared headers, ensure both C++ and Slang code paths remain consistent. GPU struct size constants (e.g. `kSurfaceGPUSize`, `INSTANCE_DATA_GPU_SIZE`) must match their respective struct sizes.

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
| `src/dxvk/rtx_render/` | Core RTX rendering code |
| `src/dxvk/shaders/rtx/` | RTX shader code (Slang) |
| `src/dxvk/imgui/` | ImGui integration and developer UI |
| `src/util/` | Shared utility code |
| `bridge/` | 32-bit to 64-bit bridge |
| `tests/rtx/unit/` | Unit tests |
| `documentation/` | Project documentation |
