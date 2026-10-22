# Texture transforms

Opaque, subsurface and translucent Remix materials can scale, mirror, rotate, offset and scroll their textures.
The controls affect all of a material's textures, on top of any texture transform the game already uses.

## Using texture transforms

### Parameters

The following parameters are available on the `AperturePBR_Opacity` and `AperturePBR_Translucent` materials:

| Parameter | Default | Tooltip |
| --- | --- | --- |
| `texture_scroll_rate` | `0` | UV offset speed for all textures in UV units per second. 0 disables scrolling; negative values reverse the direction. Visible texture features move opposite the UV offset. |
| `texture_scroll_angle` | `0` | Direction of the scrolling UV offset in degrees: 0 is positive U, 90 is positive V, and -90 is negative V. Positive V points down the image. |
| `texture_scale` | `(1, 1)` | Per-axis UV scale applied before rotation: X scales U, Y scales V. (1, 1) preserves scale. Magnitudes below 1 enlarge the texture, above 1 shrink it. A negative component mirrors that axis about the origin, which is the image's top-left corner. 0 collapses that axis before rotation. UVs outside 0-1 follow the U/V wrap modes (repeat by default). |
| `texture_offset` | `(0, 0)` | Static sampling offset for all textures in UV units, added after scale and rotation. (0, 0) adds no offset. Positive V points down the image. Visible texture features move opposite the sampling offset. |
| `texture_rotation` | `0` | Sampling rotation for all textures in degrees around the UV origin, which is the image's top-left corner, after scale and before offset. Positive angles rotate +U toward +V, which points down the image. 0 preserves orientation. |

### How the controls combine

The texture is scaled, then rotated, then offset and scrolled.

- **Rotation changes the scale direction, but not the scroll direction relative to the texture.**
  After a 90° rotation, the X scale stretches the texture along its own V axis.
  Offset and scroll stay aligned with the texture, so on the surface they rotate with it.
- **Different X and Y scale magnitudes skew a rotated texture**, unless the rotation is a multiple of 90°.
- **Offset and scroll are measured in texture repeats.** An offset of 0.5 shifts by half a repeat at any scale.
  At scale 2, the same scroll rate looks half as fast.
- **Positive rotation turns an upright texture counterclockwise.** Mirroring one axis reverses this.

### Supported surfaces

The controls work on:

- Opaque materials, including subsurface materials.
- Translucent materials.
- Particles using the default `UseMaterialSpriteSheet` sprite sheet mode.

They do not work on:

- Baked terrain.
- Eyes.
- Sky.
- Surfaces in the `AnimatedWater` category.
- Ray portals, including legacy translucent portals.
- Draws whose texture coordinates are generated from camera-space normals.
- Particles using the `OverrideMaterial_Lifetime` or `OverrideMaterial_Random` sprite sheet mode.
- MDL renderers other than the Remix runtime.

### Tips

- Use the Repeat or Mirrored_Repeat wrap modes for seamless scrolling. With Clamp or Clip, the texture slides and then snaps back.
- With Clamp, Clip or Mirrored_Repeat, mirroring or rotating needs an offset to keep the texture in place.
- On animated sprite sheets, one UV unit spans one frame, and offsets and scrolling wrap within the frame. Leave gutters between frames.
- Zero or tiny scales can break normal-map shading.
- Fast scrolling can smear or ghost.

## Implementation details

### Data flow

1. `LIST_OPAQUE_MATERIAL_CONSTANTS` and `LIST_TRANSLUCENT_MATERIAL_CONSTANTS` in `rtx_material_data.h` declare the five parameters,
   their USD tokens, identity defaults and ranges.
   All five controls are clamped to [-65504, 65504], per component for `Vector2` values.
   Angles repeat every 360 degrees. MDL soft ranges set the editor slider ranges.
2. On instance updates, `InstanceManager::updateInstance` calls `MaterialData::getTextureTransformData`, which converts the rate and angle into a velocity.
   `composeTextureTransform` in `rtx_materials.h` folds the scale, rotation and static offset into the draw call's captured texture matrix,
   and stores the result in `RtSurface::textureTransform`. The velocity is stored in `RtSurface::textureScrollSpeed`.
3. Each frame, `AccelManager::uploadSurfaceData` calls `RtSurface::writeGPUData`.
   It uploads the U and V output rows of `RtSurface::getTextureTransform(timeUs)`, which adds the wrapped scroll phase to the static matrix.
4. `concept/surface/surface_interaction.slangh` multiplies each vertex's `(u, v, 1, 1)` by that matrix before generating the tangent basis and texture gradients.
   Sprite sheet frame selection runs afterwards.

All texture transform math runs on the CPU. The ray tracing shaders only see the resulting texture matrix, which they already support for game-driven texture transforms.

### Transform math

```text
uv         = R(texture_rotation) * (texture_scale * capturedUV) + texture_offset + wrap(velocity * timeSeconds)
capturedUV = (capturedMatrix * (u, v, 1, 1)).xy
velocity   = texture_scroll_rate * (cos(texture_scroll_angle), sin(texture_scroll_angle))
wrap(p)    = p - 2 * round(p / 2)
```

- Angles are in degrees, and `R` rotates +U toward +V for positive angles. Scaling and wrapping are component-wise.
- `composeTextureTransform` scales and rotates the captured matrix's U and V output rows, then adds the static offset to its translation column.
  The translation therefore does not depend on the captured matrix's w row, unlike a generic `T * R * S * capturedMatrix` product.
  Keep this behaviour for non-affine captured matrices.
- The triangle shader, OMM baking and the CPU helper `transformTexcoord` all extend mesh UVs to `(u, v, 1, 1)` before the multiply.
- `calcScrollDirection` reduces the angle by quadrant, so multiples of 90° give exact 0 and ±1 components,
  and axis-aligned scrolling leaves the other axis still. A rate of 0 gives zero velocity, whatever the angle.
- Only the scroll phase wraps, per axis, to [-1, 1] with a period of 2. The static offset is added unchanged.
  A period of 2 is seamless for both Repeat and Mirrored_Repeat, and wrapping around 0 keeps small offsets small.
  With Clamp or Clip, the texture snaps back every `2 / |velocity|` seconds on each axis, using that axis's velocity component.
- `timeSeconds` comes from `GlobalTime::absoluteTimeUs()`, so scrolling follows fixed-step time and stops when time advancement is disabled.
  `RtSurface::getTextureScrollOffset` computes the phase in double precision, so precision loss stays negligible during long sessions.
- When both scale components are non-zero, visible features move by `-(R * S)^-1 * velocity` per second in the captured mapping's UV space.

### Remix API

API clients use the matching fields on `remixapi_MaterialInfoOpaqueEXT` and `remixapi_MaterialInfoTranslucentEXT`.

### UV convention

Runtime texture space follows the D3D convention: `(0, 0)` is the image's top-left corner, +U points right, and +V points down the image.
Scaling, mirroring and rotation pivot around that corner.

The USD mesh importer (`src/lssusd/usd_mesh_importer.cpp`) imports authored `(u, v)` as `(u, 1 - v)`.
Measured in USD texture coordinates, V offsets, V scroll directions and rotations are therefore reversed, and the pivot is at `(0, 1)`.

### Tangents and normal maps

The shader generates the tangent basis from the complete transformed UV mapping, including mirroring and rotation.
Without rotation, negative U scale reverses the generated tangent direction, and negative V scale reverses the bitangent direction.
Normal maps use that basis without an additional normal-component correction.
Non-uniform scale magnitudes change tiling without changing normal strength.
Zero or tiny scales can make triangles degenerate in transformed UV space.
Tangent generation then uses an orientation unrelated to the UVs, so normal maps on those triangles shade incorrectly.

### Sprite sheets and other UV consumers

- The matrix transforms coordinates before sprite sheet frame selection, so one UV unit spans one frame.
  Frame selection wraps the coordinates into the frame, whatever the sampler wrap modes are.
  When the transform moves coordinates outside [0, 1], the wrap seams fall inside the sprite,
  where texture filtering can blend in the neighbouring frames.
- `InstanceManager::createBillboards` and `InstanceManager::createBeams` transform particle texcoords on the CPU with the same matrix.
  Beams use the full matrix, including scrolling. Billboards are covered in [Opacity micromaps](#opacity-micromaps).

### Opacity micromaps

Opacity micromaps treat scrolling like sprite sheet animation.

- Instances with a non-zero scroll velocity count as animated (`RtInstance::isAnimated`).
  They only get opacity micromaps when `rtx.opacityMicromap.buildRequests.enableAnimatedInstances` is enabled, which it is not by default.
- Baking in `rtx_geometry_utils.cpp` writes the surface at time 0.
  The micromaps therefore include the static scale, rotation and offset, but not the scroll offset.
  They do not track scrolling, so their cutouts can disagree with the shaded opacity.
- Texel footprint estimation in `rtx_opacity_micromap_manager.cpp` also uses the static matrix.
- Billboard texcoords, and therefore the billboard texcoord hash, leave out the scroll offset, so the micromap hash stays stable.
  The scroll offset is added only to the shaded centre UV.

### Temporal filtering

Denoisers and DLSS only track geometric motion, so they treat a scrolling texture like any other animated texture.

### Why some surfaces are unsupported

- **Baked terrain.** Existing issues in the legacy terrain baking path need to be addressed before material texture transforms can be supported reliably.
- **Eyes.** Eye shading derives its texture coordinates from the eyeball's basis instead of the mesh UVs.
  `RtSurface::writeGPUData` writes the eye basis in place of the texture matrix.
- **Sky.** Sky replacements only provide their albedo texture to sky rendering.
  The game's own texture transform still applies, so game-driven cloud scrolling keeps working.
- **Animated water.** `InstanceManager::updateInstance` replaces the authored controls with identity values
  for opaque and translucent surfaces in the `AnimatedWater` category.
  This keeps the game's captured texture transform and built-in water animation unchanged.
- **Ray portals.** Portal materials do not expose the controls, and `MaterialData::getTextureTransformData` returns identity values for `RayPortal` materials.
  Portals use their own sprite sheet and rotation animation instead.
- **Legacy translucent portals.** The USD loader in `rtx_mod_usd.cpp` loads an `AperturePBR_Translucent` shader with a `rayPortalIndex` attribute
  as a ray portal material, so the controls are ignored.
- **Texture coordinates generated from camera-space normals.** Transforming a view-dependent lookup has no clear meaning,
  so `InstanceManager::updateInstance` uses identity values for draws with `TexGenMode::ViewNormals`.
- **Particles with the `OverrideMaterial_Lifetime` or `OverrideMaterial_Random` sprite sheet mode.**
  The particle system writes UVs that already select a sprite, so a material transform would sample the wrong part of the atlas.
  `rtx_particle_system.cpp` disables the material sprite sheet and calls `MaterialData::resetTextureTransform` for these systems.
- **MDL renderers other than the Remix runtime.** The MDL parameters are marked `anno::unused()`, and only the Remix runtime applies them.
  Other renderers show untransformed textures.
