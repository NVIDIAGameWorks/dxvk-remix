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
  - Constants: `k` prefix and camelCase, i.e. `kConstantName`
  - Macros and defines: `UPPER_CASE`
  - Classes and structs: `PascalCase`
- **Includes**: Standard library first, then third-party, then local. Separate groups with blank lines.
- **Memory**: Prefer smart pointers (`std::unique_ptr`, `std::shared_ptr`). Use `Rc<T>` for GPU resources.
- **Profiling**: Use `ScopedCpuProfileZone()` / `ScopedGpuProfileZone(ctx, "name")` for performance-critical code.

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
