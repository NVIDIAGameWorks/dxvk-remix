# Particle spawn distribution

Choose **By Surface Area** when particles should cover an emitter evenly despite differences in triangle size. Choose **By Triangle** when each triangle should receive the same share of particles.

## Using the setting

In Toolkit builds containing the matching particle schema and renderer, the emitter's particle properties expose **Spawn Distribution** in the **Spawn** group. The setting belongs to the mesh or material that owns the particle emitter settings. Mesh-authored emitter settings take precedence over material-authored settings.

| Mode | USD token | Behaviour |
| --- | --- | --- |
| By Triangle | `ByTriangle` | Each triangle has equal spawn probability. Densely triangulated regions therefore receive more particles per unit area. This is the default when the property is absent. |
| By Surface Area | `BySurfaceArea` | Each triangle's probability is proportional to its current world-space area, giving even expected density over the emitting surface. |

The USD property is `primvars:particle:spawnDistribution`. For example, on a prim with particle emitter settings:

```usda
uniform token primvars:particle:spawnDistribution = "BySurfaceArea"
```

For the runtime's global particle preset, set `rtx.particles.globalPreset.spawnDistribution` to `0` for By Triangle or `1` for By Surface Area. This preset does not override an emitter's explicitly authored particle description. See [Rtx Options](../RtxOptions.md) for the configuration reference.

Both modes sample uniformly within the selected triangle. Effects can therefore look different from builds whose within-triangle sampling concentrated particles toward triangle centres, even when they keep By Triangle selected. Spawn rate and particle limits are unchanged.

Area weighting applies within each submitted emitter context. A material used by several meshes does not receive a new shared area-based allocation of its total emission rate.

### Choosing a mode

Use By Surface Area for emitters with large faces beside small, densely triangulated details, or when changing tessellation should preserve regional particle density. Existing content keeps By Triangle unless the mode is changed explicitly.

Area weighting requires additional GPU storage and work. Static emitters can reuse their area tables. Deforming or skinned emitters need updated tables when their geometry changes. Scenes containing many separate area-weighted emitters also incur additional dispatch overhead. Compare both modes in a representative capture before enabling area weighting broadly.

## Implementation

### Distribution construction and sampling

`particle_system_build_spawn_distribution.comp.slang` reads the same current modified geometry used by spawning, including GPU deformation. Each triangle's weight is the magnitude of the cross product of its transformed edge vectors. Weight construction removes a common scale from the linear object-to-world transform in double precision before upload. The GPU carries each scalar component as an FP32 mantissa and a separate integer exponent through transform dot products and cross products. Products therefore retain their exponent range, and components of one edge cannot disappear because another component is much larger. Dot products combine their largest terms first so cancellation can reveal a small third term. Only the completed normal is converted to a common scale before measuring its length. A common maximum area exponent across the mesh then scales all weights together. This extends the intermediate exponent range, not mantissa precision; ordinary FP32 rounding still applies. The common scale and factor of one half cancel when converting weights to probabilities. Actual particle positions still use the original transform. Fully collapsed or nonfinite linear transforms produce zero-area tables.

The GPU constructs a two-level cumulative distribution:

1. Workgroups measure area mantissas and exponents, retaining the maximum exponent per block of 256 triangles.
2. One workgroup reduces those maxima to a common exponent for the entire emitter.
3. Workgroups rescale all areas by that common exponent and compute local inclusive prefixes.
4. One workgroup computes inclusive prefixes of the block totals.

The prefix layout is unchanged. Construction appends one exponent per triangle and one common exponent to the buffer (`2 * triangleCount + blockCount + 1` floats in total). The extra two dispatches run only when constructing a table; cached spawning uses the same two-level search.

Each parallel sum is followed by a prefix maximum whose candidates come only from positive-weight entries. Different FP32 addition associations can otherwise make neighboring sums decrease, including across zero-weight entries. The repaired prefix is nondecreasing, and every zero-weight entry exactly equals its predecessor (or zero at the start). The same repair applies to triangle prefixes and block-total prefixes. Adding the preceding batch total preserves these properties across block-finalization batches. Small positive weights can still lose probability to FP32 rounding.

Spawning searches the block totals, then the selected block's triangle prefixes. The selected triangle is sampled using two independent uniform values folded across the unit square's diagonal. The construction and lookup helpers are shared with the sampling regression tests.

Zero-area and nonfinite triangle weights are excluded. An emitter with no valid positive area produces no visible particles. Rejected scheduled births create initialized, invisible placeholders marked with `kRejectedSpawnFlag`. Their normal lifetime and conservative counter expiry preserve the contiguous particle ring and its older live particles. These placeholders temporarily occupy bookkeeping capacity until normal expiry. Empty or unsupported distribution sizes follow the same rejection path.

Hidden emitter meshes can populate the material cache before generated particle surfaces exist. Surface-material preparation skips allocation and upload for an empty payload, while retaining its normal material-index bookkeeping. A start-in-medium material still contributes its entry even when no surfaces exist.

### Cache lifetime and invalidation

Spawn contexts identify their particle system using the original material, particle description, and game/API draw origin, matching the system table's lookup key. Game and API emitters retain separate systems. The rendering hash uses the particle's replacement material instead. These identities can differ in-game and must remain separate when finding the owning system to build an area table.

Area tables are cached by persistent instance identity. Geometry creation/update versions, buffer identities and offsets, vertex layout, index format, triangle count, and the object's shape transform determine whether a cached table can be reused.

The transform comparison uses a normalized `A^T A` metric for the linear object-to-world transform. Translation, rigid world-space rotation and uniform scale preserve relative face areas. Nonuniform scale, shear and geometry deformation can change them. Conditioning-aware comparisons handle floating-point differences conservatively near singular transforms.

Tables are prepared only for contexts requesting particle spawning. Entries unused for more than 300 frames expire. A least-recently-used eviction pass limits retained tables to a 64 MiB target. Active and in-flight GPU resources can temporarily exceed that cache target because command lists keep resources alive until their work completes.

The By Triangle path keeps the existing batched spawn dispatch. By Surface Area uses a distribution buffer and spawn dispatch per emitter context. Rebuilding a table adds its construction and finalization dispatches. This scheduling cost matters when many small emitters are active, even when their combined triangle count is modest.

### API and serialization

The additive `remixapi_InstanceInfoParticleSpawnDistributionEXT` extends the instance chain alongside its particle-system extension. Its `spawnDistribution` field accepts `REMIXAPI_PARTICLE_SPAWN_DISTRIBUTION_BY_TRIANGLE` or `REMIXAPI_PARTICLE_SPAWN_DISTRIBUTION_BY_SURFACE_AREA`. Omitting the extension selects By Triangle.

The extension uses struct type 29 and is included in Remix API 0.6.8. Existing public particle-system structs retain their layouts. HdRemix and bridge serialization propagate the distribution mode for mesh- and material-authored emitters.

## Validation

The focused native test targets are `test_particle_spawn_distribution` and `test_particle_system_lookup`. The lookup test constructs the production particle system with different original and replacement material identities and checks both lookup and rendering hashes. Use the [unit-test workflow](UnitTest.md) to build and run them. Validate schema metadata and API/bridge serialization alongside the sampling tests when changing the property or extension chain.

Enable the Vulkan GPU regression in a dedicated unit-test build:

```powershell
meson configure _CompUnitTest_x64 -Dparticle_distribution_gpu_tests=true
meson test -C _CompUnitTest_x64 --verbose test_particle_spawn_distribution_gpu
```

This test compiles and dispatches the production distribution build shader with its normal bindings and geometry reads, then runs the shared production sampler on the GPU. It checks ordered prefixes and exact zero-width intervals for excluded triangles and blocks, including `[16777216, 1, 1, 0, 0, 16777216]`, sampling at `0.5`, both index widths, and more than 256 blocks. It also compares a retained GPU table with a cold build after restoring uniform scales `2^-80` and `2^80` to one, using the production normalization and cache-metric helpers, and checks collapsed transforms. Compensated vertex/transform scales in both directions (`2^-80` and `2^80`), reciprocal edge scales, and anisotropic compensated transforms retain a known 1:3 area ratio. Arbitrary-vertex cases cover skewed edges with components `2^80` and `2^-80`, all three orientations, both index widths, and a known 1:3 area ratio. Cases with absolute areas outside FP32 range check relative probabilities. The transformed-edge underflow case retains equal weights at exponents 64, 80, and 100, and cancellation cases retain small contributions after large transform or cross-product terms cancel. The fixture requires and enables both `storageBuffer16BitAccess` and `uniformAndStorageBuffer16BitAccess` for production shader buffers. This is an isolated cache-policy and kernel regression; it does not instantiate the full renderer's cache manager or measure whole-frame performance. A Vulkan 1.2 device supporting the production shader's descriptor-indexing, scalar-layout, and integer-storage features is required; an unavailable device fails the explicitly enabled test.

For visual and performance validation:

1. Compare equal-area regions with different triangle counts. By Surface Area should give them equal expected particle counts.
2. Subdivide part of an emitter without changing its surface. Regional density should remain consistent in By Surface Area mode.
3. Check the interior of a single large triangle for even coverage, including its edges and corners, in both modes.
4. Test translation, rigid rotation, uniform scale, nonuniform scale and skinned deformation. Confirm table reuse or rebuilding matches changes in relative face area.
5. Collapse an emitter to zero area, then restore it. Confirm it creates no visible particles while invalid and resumes emitting after recovery.
6. Profile warmed static emitters, changing geometry, and many separate emitters. Report table construction separately from cached spawning, and include CPU scheduling and complete-frame GPU timings.
7. Open a scene containing only hidden emitter meshes. Confirm it survives the initial frame before generated particles provide visible surfaces, then check mode changes and undo.

An isolated compute benchmark verifies sampling kernels and their local GPU cost. It does not establish complete renderer performance, Toolkit interaction, or deployment status. Those require validation with matching built binaries and representative captures.
