# Raytraced camera materials

## Material controls

Raytraced camera surfaces use the existing opaque material controls:

- **Opacity Amount** multiplies camera coverage, including an authored opacity map and the game's alpha animation. At zero opacity, the camera image and its emitted light disappear. The scene behind the surface remains visible.
- **Enable Emission** makes the live camera image illuminate the scene. With emission disabled, the camera image remains visible at its normal brightness.
- **Emissive Intensity** multiplies the camera image's radiance when emission is enabled. The global emissive intensity scale also applies.

The camera supplies emission colors. Emissive Color and Emissive Mask Map do not replace its live image. An authored Albedo/Opacity Map can supply an opacity mask through its alpha channel. The original raster camera texture does not supply that mask.

These controls belong to each authored material. Mesh replacements can bind different camera materials to different surfaces.

For a texture-level material replacement, the runtime first checks the existing material hash. If no replacement matches, it also checks the camera texture's descriptor hash, as shown in the Raytraced Render Target texture category. This provides a stable identifier when render-target allocation order changes. Surfaces sharing that camera texture also share this fallback material.

The camera identifier comes from the texture that matched the category, even when an ordinary albedo/opacity texture occupies slot 0 and the camera occupies slot 1. Capture naming and material reload use this same descriptor. The ordinary texture remains available as an opacity mask; only the actual camera render target's alpha is ignored.

New captures name raytraced camera materials using this descriptor hash, so Toolkit-authored material overrides can use the same identifier across maps. Mesh hashes identify geometry independently and do not require separate material overrides. Descriptor hashes remain stable while the texture properties (including size and format) stay the same; camera targets with identical descriptors share the fallback. Captures made before this change use allocation-dependent material hashes and need their camera material override migrated to the descriptor hash. Existing raw-hash overrides still take precedence.

The captured raster camera texture can be black with transparent alpha. It is not a preview of the live raytraced image. Check the material's appearance in the running game.

For example, the relevant shader inputs for a half-transparent, emissive camera material are:

```usda
float inputs:opacity_constant = 0.5
bool inputs:enable_emission = true
float inputs:emissive_intensity = 4
```

## Rendering implementation

The camera image is evaluated by tracing rays from the render-target camera. No separate camera-image texture is baked for emission.

At the first visible screen, PSR carries the camera and straight-through background as separate paths. Their radiance weights are opacity times camera intensity and one minus opacity. Lighting integration selects one path with a corresponding selection PDF. Nested screens and indirect lighting use stochastic coverage, so opacity scales emitted light once.

Camera surfaces bypass the ordinary emissive-particle approximation and radiance-cache queries at the screen. Cache queries occur at the destination reached through the camera ray. Ordinary additive camera materials retain alpha coverage instead of inheriting particle opacity approximations.

When the active reflection or transmission PSR bounce budget is exhausted at a camera, the path terminates with its accumulated radiance. It writes the cache miss/termination state and an invalid GBuffer surface, so later lighting passes cannot read an uninitialized NRC path. Recursive cameras need sufficient PSR budget to reach their destination.

Indirect path tracing evaluates the live image's lighting contribution. The NEE cache does not substitute a constant-color triangle light for a camera surface. Small or distant screens can therefore converge slowly. Camera transport needs room in the indirect bounce budget to evaluate the destination scene.

With emission disabled, camera visibility survives multiple specular reflections or refractions, including when reflection PSR is disabled. Diffuse reflection or transmission revokes that visibility exception for the rest of the path, so the camera does not illuminate diffuse receivers.

The raster fallback retains the game's original texture when raytraced render targets are disabled or the camera is unavailable. The GPU material layout remains 64 bytes.

Emissive intensity is applied to visible camera radiance as well as indirect lighting. PSR payloads and attenuation buffers use 32-bit RGB9E5 encoding to preserve gains above one, up to 65408, without allocating larger buffers. Ordinary material colors retain their UNORM encoding. A controlled `screen_test` comparison with fixed exposure confirmed visible intensity scaling, postprocess bloom (including a bloom-off comparison), and opacity fading. A new capture also exported the descriptor-based camera material identifier. Bloom runs before tone mapping, and local tone mapping can compress the apparent brightness of a screen.

## Regression checks

`test_camera_material` covers descriptor lookup, capture identifiers, descriptor hot reload, raw-hash precedence, and retaining a separate slot-0 opacity input. The optional `camera_resource_identity_gpu` variant repeats these checks with Vulkan textures and also verifies that multiple views of the camera resource match while another target with the same descriptor does not. These fixtures supply the classified slot-1 descriptor; they do not exercise D3D9 category detection.

The optional `camera_transport_gpu` test executes the production Slang arithmetic on Vulkan. It checks two gain-256 camera crossings, finite radiance with zero-emission channels before packing, bounded RGB9E5 storage, and specular/diffuse path history, including the first-sample FP16 handoff. Install `slangpy` and `numpy` in the build's Python environment and configure the dedicated unit-test build with `-Dcamera_gpu_tests=true`, then run `meson test -C _CompUnitTest_x64 --suite gpu`. The shader test can also be run directly with `python tests/rtx/unit/test_camera_transport_gpu.py`.

Full rendering validation additionally requires these scene checks:

- Bind an ordinary alpha mask in slot 0 and a categorized camera target in slot 1 with identical UVs. Author and hot-reload a descriptor override, inspect the exported material identifier, and confirm a raw-hash override still wins. The ordinary mask must continue to affect coverage.
- Show a camera recursively with NRC enabled and `rtx.pstrMaxBounces = 1`. Exercise both query-only and training-enabled operation. At the limit, the GBuffer must contain a miss and NRC query/training termination data must be initialized, including when aliased buffers previously contained nonzero radiance.
- Show a non-emissive camera through two mirrors with `rtx.enablePSRR = false` and enough indirect bounces to reach the camera destination. The image must remain visible. A diffuse receiver and a specular-then-diffuse-then-camera path must receive no camera emission.

The GPU helper test does not substitute for these scene checks.

## Remaining visual limitations

The natural Breencast-to-logo transition in `screen_test` can leave the room almost black while the live screen image remains visible. SHaRC cache bypass improves the measured lighting without fully resolving it. Toggling Ignore Game Lights > Directional reportedly restores lighting even after the option is restored, and that UI action also clears the translated-light list. The cause is unresolved. See the [lighting investigation handoff](https://gitlab-master.nvidia.com/lightspeedrtx/developer-forks/jawilson/dxvk-remix-nv/-/blob/codex/remix-5554-camera-materials/documentation/rtx_camera_lighting_handoff.md) for the repro, evidence, and pending checks.

The camera continuation currently uses an identity rotation for motion reprojection. Projecting movement in the camera's scene into the player's view requires a perspective mapping, so moving objects in screens can leave temporal trails. Correcting this mapping remains separate from material opacity and emission.

Pausing the Source simulation or reducing its time scale can interrupt camera rendering, so these are not reliable ways to hold a live camera image for visual comparisons. The controlled test held the Breencast choreography and disabled its looping relay and intervening card while leaving the renderer running normally.
