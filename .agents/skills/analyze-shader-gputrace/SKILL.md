---
name: analyze-shader-gputrace
description: Compare a baseline and a variant Nsight Graphics GPU trace to judge a Remix shader change. Use when the user captured .ngfx-gputrace files and wants to know whether a shader refactor changed pass times, registers, occupancy, spills or code size.
---

# Analyze shader changes with GPU traces

The workflow, prerequisites and pitfalls are in
[nv-private/documentation/GpuTraceShaderAnalysis.md](../../../nv-private/documentation/GpuTraceShaderAnalysis.md).
Read it before starting. It needs NVIDIA-internal tools, so it is only usable in the private repository.

The scripts are in `nv-private/tools/gputrace_analysis/`.
