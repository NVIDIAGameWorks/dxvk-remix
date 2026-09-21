---
name: run-unit-tests
description: Build and run dxvk-remix unit tests. Use when the user asks to run tests, add new tests, or verify that code changes pass existing tests.
---

# Run Unit Tests

All commands run in PowerShell from the repo root.

Unit tests live in `tests/rtx/unit/`. New test files must be added to `tests/rtx/unit/meson.build`.

**IMPORTANT:** Unit tests use a **separate dedicated build directory** (`_CompUnitTest_x64`) with special build flags. Do NOT run unit tests from game build directories (`_CompDebugOptimized_x64`, `_CompRelease_x64`, etc.) — they will fail.

## First-Time Setup

Build the unit test configuration:

```powershell
.\build_dxvk.ps1 -BuildFlavour release -BuildSubDir _CompUnitTest_x64 -Backend ninja -EnableTracy false -BuildTarget unit_tests -InstallTags unit
```

## Running Tests

Once `_CompUnitTest_x64` exists:

```powershell
cd _CompUnitTest_x64
meson test --verbose          # run all tests
meson test --verbose <name>   # run a specific test
```

## Incremental Rebuild and Retest

If test source files have changed, rebuild before running:

```powershell
cd _CompUnitTest_x64; meson compile -v; meson test --verbose <name>
```

## Test Guidelines

- Minimal dependencies. Refactor tested code if needed to reduce coupling.
- Full branch coverage with carefully designed test data.
- Return 0 on success, -1 on failure.
- Include descriptive logging on failure.

If the above steps don't work, check `tools/ci/gitlab/unit_test.yml` for the latest commands.
