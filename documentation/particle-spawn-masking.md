# Particle Birth Masking

Birth masking rejects particle candidates according to the emitter material. It is off by default and works with both By Triangle and By Surface Area spawning. It does not alter the rendered particle material or redistribute rejected births.

## For modders

### Setup

Toolkit must load the updated particle schema as well as the matching renderer. Replacing renderer binaries alone does not add these controls. Select an emitter mesh or material with a particle system, then open **Properties > Particle Properties > Spawn**. The panel discovers the controls from the loaded schema:

| Control | Property | Values | Default |
| --- | --- | --- | --- |
| **Birth Mask Source** | `primvars:particle:spawnMaskSource` | `Off`, `Alpha`, `Luminance`, `AlphaTimesLuminance` | `Off` |
| **Birth Mask Response** | `primvars:particle:spawnMaskResponse` | `Grayscale`, `Cutout` | `Grayscale` |
| **Birth Mask Threshold** | `primvars:particle:spawnMaskThreshold` | 0 through 1 | 0.5 |

Grayscale accepts each candidate with probability equal to the selected normalized mask value. Cutout accepts a nonzero value at or above the threshold. AlphaTimesLuminance multiplies the two values. Cutout ignores randomness, and Grayscale ignores the threshold.

For example, a constant mask weight of 0.25 accepts approximately one quarter of the configured spawn rate. To target 400 visible births per second, start with 1600 candidates per second. Rejected candidates keep their normal age and ring reservation, so `maxNumParticles` may also need to increase. This is a statistical expectation, subject to the existing lifetime and capacity limits.

```usda
uniform token primvars:particle:spawnMaskSource = "AlphaTimesLuminance"
uniform token primvars:particle:spawnMaskResponse = "Grayscale"
float primvars:particle:spawnMaskThreshold = 0.5
```

The equivalent global texture-tagging preset uses `rtx.particles.globalPreset.spawnMaskSource` (0 Off, 1 Alpha, 2 Luminance, 3 AlphaTimesLuminance), `spawnMaskResponse` (0 Grayscale, 1 Cutout), and `spawnMaskThreshold`. The global preset does not override authored emitter descriptions.

### Choosing a mask

The mask comes from the emitter's albedo/opacity texture, not the texture drawn on the generated particles. Alpha uses that texture's alpha channel even if the emitter renders opaque. Luminance uses its linear-color brightness and excludes emission intensity, scene lighting and vertex color. The source UV transform and animated spritesheet frame also affect the mask.

This feature currently supports opaque or alpha-blended opaque materials. Glass/refraction materials, render targets and generated source UVs are unsupported. Masking reduces births rather than moving them into the unmasked areas. It adds work at spawn time and does not automatically reduce GPU dispatch sizes.

## For developers

### Source material and coordinates

Masks use the emitter's resolved opaque material. For an unreplaced legacy material this is the qualifying original color texture and its sampler. For a replacement it is the replacement albedo/opacity source. A valid texture replaces the material color/opacity constants; a material with no resolved texture reference uses those constants. Constants are not additional texture multipliers.

Alpha reads raw source alpha, independently of final render blend state, ignore-alpha flags or thin-film interpretation. Luminance is BT.709 linear-color luminance. Alpha remains linear. Color from an sRGB image view is already decoded by sampling; other source color is decoded with the renderer's gamma conversion. Constant color uses that same conversion. Luminance excludes scene lighting, emissive intensity, vertex colors and global material debug overrides. A gamma-encoded gray value of 0.5 therefore has a lower linear-luminance weight than 0.5.

Coordinates are interpolated at the candidate's barycentric location on the emitter, independently of Use Spawn Mesh's Texture Coordinates and of particle sprite UVs. Source texture transforms, scroll offsets, sampler addressing and animated spritesheet selection are retained. Sampling uses explicit LOD 0 of the currently resident image view. Birth contexts request full-detail streaming independently of emitter visibility, mask acceptance and rejected-slot retry timing. Streaming mip changes can change the sampled mask detail while loading. Mask texture descriptors are resolved independently of white-material debug rendering.

Translucent/refraction materials, render-target sources and generated texture coordinates are unsupported mask sources. A valid texture reference with an unavailable resident image/sampler, absent UVs or nonfinite transformed UVs suppresses births safely and is rechecked on subsequent spawn contexts. The material resolver represents an absent texture and a failed asset-path load with the same empty texture reference; both use material constants. Such path failures do not have a distinct mask-suppression state. A constant material does not require UVs.

### Runtime and API

Each candidate evaluates one mask sample with no retries, mask CDF, synchronous readback or texture preprocessing. Mask Off performs no extra random draws or texture samples and preserves the existing birth random sequence. Rejected slots reuse the existing rejected-spawn state, remain invisible, skip particle forces and retain conservative age/counter bookkeeping. Masking does not reduce dispatch size automatically and has not yet been benchmarked in this change.

The internal GPU spawn context carries current source descriptors and UV data. It never reads a stale prior-frame GPU material table. Mask textures have independent streaming references that remain active through garbage collection, then release after collection or scene clearing. Repeated contexts using the same texture share one such reference. The particle output material can retain its independent sprite configuration.

When the configured spawn rate reaches or exceeds `maxNumParticles`, the constant-count path visits every slot each frame. With masking enabled, a rejected slot waits until its sampled lifetime expires before trying again. Rejection uses the same birth-property lifetime seed as acceptance, independently of the triangle-selection sample. It must not repeatedly reroll the mask each frame and gradually fill the pool. Accepted particles and Mask Off retain their existing respawn behavior.

API 0.6.13 adds `remixapi_InstanceInfoParticleSpawnMaskEXT` (tag 33), chained from `remixapi_InstanceInfo` alongside the existing particle-system extension. Set `maskSource`, `maskResponse` and `maskThreshold`; omitting the extension selects Off. Existing public struct layouts and extension tags remain unchanged. The bridge serializes the additive extension and Hydra forwards mesh or material emitter settings. Hydra clamps finite authored thresholds to [0, 1] before calling the strict API; nonfinite thresholds remain invalid.

### Validation

CPU tests exercise the production shared acceptance and UV helpers, rejected-birth lifetimes, defaults and hashing, CPU/GPU layout, public extension chaining and bridge round trips. The schema test covers both mesh and material authoring, including save/reload, while retaining the existing distribution and screen-size checks. Production Slang compilation checks the actual spawn integration. These checks do not establish visual parity, texture-residency recovery or measured GPU performance.
