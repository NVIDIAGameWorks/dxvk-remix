# Particle screen-size controls

## Authoring

Particle emitters expose three controls under **Visual** in Toolkit's Properties panel. Mesh and material emitters support the same attributes:

| Attribute | Default | Meaning |
| --- | --- | --- |
| `primvars:particle:minScreenSize` | 0 | Minimum longest base billboard edge in internal render pixels. Zero disables the lower bound. |
| `primvars:particle:maxScreenSize` | 0 | Maximum longest base billboard edge in internal render pixels. Zero disables the upper bound. |
| `primvars:particle:screenSizeCoverage` | 1 | Opacity compensation on enlargement, from zero to one. |

Both bounds at zero preserve world sizing and existing culling. Equal positive bounds hold a fixed footprint. An enabled maximum must be at least the minimum. For fine mist, try a minimum of **0.8**, maximum of **1.2**, and coverage of **0.25**.

Sizes use the internal resolution before DLSS or another upscaler. Tune them at the intended render resolution and upscaling mode. Coverage zero preserves opacity when enlarging. Coverage one approximately preserves integrated coverage by reducing opacity with projected area. Intermediate values blend those responses. Shrinking never increases opacity.

The bounds preserve aspect ratio and apply to the base billboard. Motion trails retain their added velocity-based length. A maximum size does not cap the complete streak length.

Toolkit discovers these controls from the renderer's schema. Its renderer package must include this feature. API clients attach the optional `InstanceInfoParticleScreenSizeEXT` extension to the instance's `pNext` chain alongside `InstanceInfoParticleSystemEXT`.

## C API initialization

A zero-initialized C extension has `screenSizeCoverage = 0`, which retains authored opacity when particles enlarge. The C++ wrapper and USD schema default to `1`, which compensates opacity for the increased area. Explicitly initialize coverage when enabling a bound to choose the intended behavior:

```c
remixapi_InstanceInfoParticleScreenSizeEXT screenSize = {0};
screenSize.sType = REMIXAPI_STRUCT_TYPE_INSTANCE_INFO_PARTICLE_SCREEN_SIZE_EXT;
screenSize.minScreenSize = 1.0f;
screenSize.screenSizeCoverage = 1.0f; /* Match the C++ and USD defaults. */
```

Direct API submissions require finite, nonnegative bounds, an enabled maximum no smaller than the minimum, and finite coverage in `[0, 1]`. HdRemix normalizes authored USD values first: invalid bounds disable that bound, finite coverage clamps to `[0, 1]`, and nonfinite coverage defaults to `1`.

## Implementation

Geometry generation measures the longest projected base edge using the current camera's projection differential at the particle centre and the rotated billboard axes. Both axes scale uniformly. This estimate is exact for camera-parallel billboards and approximate for tilted cards with substantial depth extent. Enabled bounds bypass legacy tiny-world-size and screen-size culling. Fully transparent and zero-size particles remain hidden. Finite particles at or behind the primary camera retain their world size and opacity. Invalid projection data clears geometry. The shared `measureParticleScreenSize` helper selects clear, keep-world-size or adjust behavior for both the shader and CPU regression tests.

On enlargement, opacity scales by the original-to-adjusted area ratio raised to the coverage amount. Trail coverage includes the scaled base height and fixed trail length. Lifetime opacity is retained. Vertex opacity uses the renderer's existing eight-bit storage.

This scales actual ray-traced geometry relative to the primary camera. Reflections and shadows see that geometry. Temporal filtering and materials' treatment of vertex alpha affect the final appearance. Particle positions, velocities, collision, and lifetime are unchanged.

## Regression tests

The standard CI unit job runs `test_particle_screen_size`, `test_particle_system_schema`, `hdremix_particle_screen_size`, and `hdremix_particle_screen_size_api` through Meson. The shared projection helper covers behind-camera and camera-plane fallbacks, front-camera measurements, invalid data, bounds and coverage on the CPU. The HdRemix tests read mesh and material USD attributes and verify normalized draws against the built runtime, while checking that direct API calls remain strictly validated.

The [RemixViewer image fixture](../tests/rtx/dxvk_rt_testing/assets/RemixViewer/particle_screen_size/README.md) runs in `Image_Core_F`. It covers visible size and coverage changes, aspect ratio, and a fixed-camera mirror reflecting behind-camera particles with disabled bounds and a permissive maximum.
