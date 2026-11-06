# Render-target camera reprojection

## Mapping between camera domains

A render-target surface displays a remote camera image in its texture coordinates.
To compute motion, project the previous remote point into that image, map its UV
onto the previous screen plane, then project the screen point through the previous
viewing camera.

In `render_target_motion.slangh`, let `q` be the image UV recovered from the sampled
remote ray, `a` the previous screen-hit position in the viewing camera's virtual
world, and `du`/`dv` the previous screen's world-space UV derivatives. For a remote
point `x` and previous remote world-to-projection matrix `P`:

```text
c = P * float4(x, 1)
uv = (0.5 + 0.5 * c.x / c.w, 0.5 - 0.5 * c.y / c.w)
previousScreenPoint = a + du * (uv.x - q.x) + dv * (uv.y - q.y)
```

Surface interaction construction derives `du` and `dv` from its already-loaded
triangle vertices and UVs, including previous skinning positions and texture transforms.
Only render-target PSR hits request this basis. `beginRenderTargetMotion` consumes
the stored derivatives without reloading vertices or rebuilding surface interactions.
Their scale and skew are required; a normalized material tangent frame is insufficient.
The mapping is stored homogeneously as a 4x4 matrix. Nested mappings transform the
anchor and derivative columns without dividing their w components independently.
Sky uses a direction with w = 0 to exclude remote-camera translation.

PSR uses a quaternion for reflections/refractions within a camera domain and the
projective matrix to map between domains. The tagged primary-pass continuation
stores the screen hit and incoming rotation in the packed origin/rotation fields;
the remote-camera origin comes from the constant buffer. Keep the live projective
state fields unconditional so ray-generation, closest-hit, and miss payload layouts agree.

## Reprojection invariants

- Preserve the packed remote-ray direction, including material/POM UV adjustments,
  at PSR storage precision. Use that same direction for tracing and the mapping
  anchor. Do not regenerate it from the hit's unevaluated material UVs.
- Do not wrap either the recovered sample UV or the previous projected UV again.
  Quantization can put a sample just beyond an image edge; wrapping would select
  unrelated history at the opposite edge.
- The current virtual position, depth, linear view Z, and temporal hit distance use
  the first screen's virtual distance. World motion is the previous mapped screen
  point minus that current position. RTXDI reconstructs the same virtual position.
  Preserve negative hit-distance sentinels for misses. Remote geometry and material
  data still drive shading; the pass-through lobe represents geometry behind the screen.
- Reject non-finite projections, degenerate UV bases, and points at or behind the
  previous remote-camera plane. Nested history is conservatively rejected: two
  negative intermediate denominators can produce a positive final w. Supporting
  nested history requires visibility constraints for every intermediate projected point.
- Invalid correspondence must reach every temporal consumer. Both NRD world-motion
  guides and screen-space motion reproject outside the previous viewport; RR also
  receives its rejection mask. RTXDI skips the entire temporal resampling call,
  including its same-pixel fallback, clears confidence and gradient history, and
  retains the current reservoir. ReSTIR GI also retains the current reservoir.
  Zero motion or NRD's relaxed disocclusion threshold does not ensure rejection.
- Virtual-motion w is -1 for invalid correspondence, 1 for a valid stationary remote
  object on the primary surface, and 0 otherwise. Preserve physical stationarity
  separately from projected image motion: RTXDI visibility bias correction needs
  it when no previous TLAS is available, or camera motion can incorrectly alter lighting.
- RR uses the remote material's shading normal. Its ordinary refraction override
  compares main-camera positions and must not replace guides after an RT-camera jump.

## Scope and regression coverage

The mapping uses Remix's single render-target camera and PSR virtual-surface
approximation. Terrain cascade UVs reject history because their mapping is non-affine.
Animated UV-transform history and general moving-reflector motion
are not represented. Curved or deforming screens use the hit triangle's affine UV
mapping, so correspondence across changing triangle boundaries is approximate.

`tests/rtx/unit/test_render_target_motion` checks projection math.
`test_render_target_motion_integration` exercises production camera entry, packed-ray
restoration, guide output, and temporal gating with CPU resource adapters. It checks
stored half-precision motion and fallback rejection, but does not execute NRD's
binary or a full GPU frame. Extend these cases when changing the invariants above.
