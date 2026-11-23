# Light Gobos

A gobo is a texture that a spot light projects onto the scene, like a slide projector. Typical uses are window frames, foliage shadows, stage patterns and flashlight lenses. In Remix a gobo is authored on a replacement sphere light with shaping, which is how spot lights are expressed. The gobo's color multiplies the light, so colored textures act as gels. A gobo can tile, scroll, spin, shake and play a flipbook. By default, light reaching a point outside the projected image is blocked.

## For modders

### Attributes

Add these attributes to a `SphereLight` that has shaping, either on a new light or as an override on a captured game light. All names start with `inputs:shaping:gobo:`. Only the `file` is required.

**Image**

| Attribute | Type | Default | Range | Meaning |
| --- | --- | --- | --- | --- |
| `file` | asset | none | | The gobo texture. It is resolved and loaded like material textures, so it must be a DDS file. A relative path is relative to the layer that authors it. |
| `color_space` | token | `auto` | `auto`, `srgb` | `auto` trusts the texture's format. `srgb` also decodes an image that was saved as sRGB in a linear format, such as `BC7_UNORM` or `BC4_UNORM`. |
| `use_alpha` | bool | false | | Multiplies the color by the alpha channel, for cutouts. |
| `invert` | bool | false | | Uses one minus the linear color, never below zero. The area around a clamped image stays black. |
| `strength` | float | 1 | 0 to 1 | Blends between no gobo at 0 and the full gobo at 1, including the area outside the image. |

**Projection**

| Attribute | Type | Default | Range | Meaning |
| --- | --- | --- | --- | --- |
| `fov` | float | 0 | 1 to 179 | The full angle the image's longer side spans, in degrees. Zero or less uses twice the shaping cone angle. |
| `aspect` | float | 0 | 0.01 to 100 | The image's width divided by its height. Zero or less uses the shape of one flipbook cell in texels, which is the whole texture without a flipbook. |
| `scale` | float2 | (1, 1) | -100 to 100 | Scales the texture coordinates around the image's center. Values above 1 make the projected image smaller. Negative values mirror it. |
| `offset` | float2 | (0, 0) | -100 to 100 | Shifts the texture coordinates, in image widths and heights. Positive values move the projected image left and up, as seen from the light. With `clamp` and `extend` the offset is stored as a half float, so it is exact to a 2000th of the image up to 2, a 500th up to 8, a 64th up to 64 and a 32nd above. |
| `rotation` | float | 0 | any | Rotates the image around the light's direction, in degrees, counterclockwise as seen from the light. |
| `wrap_mode` | token | `clamp` | see right | What is outside the image. `clamp` is black, `repeat` tiles, `mirror` tiles with every other copy flipped and `extend` continues the edge texels. |

**Motion**

| Attribute | Type | Default | Range | Meaning |
| --- | --- | --- | --- | --- |
| `scroll_speed` | float2 | (0, 0) | -100 to 100 | Adds to the offset, in image widths and heights per second. It needs a `wrap_mode` of `repeat` or `mirror`, and is ignored otherwise. |
| `rotation_speed` | float | 0 | -36000 to 36000 | Adds to the rotation, in degrees per second. |
| `shake_angle` | float | 0 | 0 to 180 | How far the image swings to each side when it shakes, in degrees. |
| `shake_frequency` | float | 0 | 0 to 100 | How many times per second the image swings back and forth. |
| `time_offset` | float | 0 | -86400 to 86400 | Seconds added to the clock of all the motion and the flipbook. |
| `time_offset_random` | float | 0 | 0 to 86400 | Adds a random time of up to this many seconds per light, so copies of a light do not animate in lockstep. |

**Flipbook**

| Attribute | Type | Default | Range | Meaning |
| --- | --- | --- | --- | --- |
| `sprite_sheet_rows` | int | 1 | 1 to 255 | The rows of cells in the texture, as on materials. |
| `sprite_sheet_cols` | int | 1 | 1 to 255 | The columns of cells in the texture, as on materials. |
| `sprite_sheet_fps` | float | 0 | -255 to 255 | Frames per second. Zero holds the first frame and negative values play in reverse. |
| `sprite_sheet_first_frame` | int | 0 | on the sheet | The first cell that plays, counted from zero. With a frame rate of zero it picks one gobo from a sheet, like a gobo wheel. |
| `sprite_sheet_frame_count` | int | 0 | on the sheet | How many cells play. Zero plays up to the last cell. Set it when the last row is not full. |
| `sprite_sheet_blend` | bool | false | | Fades between frames instead of switching. |
| `sprite_sheet_loop_mode` | token | `loop` | see right | `loop` starts over, `ping_pong` plays forward and back, `random` picks a random cell for every frame and `once` stops on the last frame. |

Only the `inputs:` spelling is read. `double` and `double2` values are accepted, and numbers may also be integers. Values outside the ranges are clamped. NaN values and unknown tokens are ignored with a warning.

The Remix Toolkit does not show these attributes yet, and its viewport does not render gobos. Author them in a USDA layer by hand, as shown below. Toolkit packaging collects asset paths, so it picks up the texture.

### Step 1: Make the texture

1. Paint the pattern in any image editor. White lets the light through, black blocks it, and colors tint it. Keep a dark border if the pattern should not reach the edge of the projection.
2. Use a power of two size such as 256, 512 or 1024 pixels. The image does not have to be square. For a flipbook, make each cell a power of two, for example 256 pixels, so that the mips line up with the cells. The whole mip chain is kept in video memory, and soft or large lights blur the pattern anyway, so larger textures cost memory without looking sharper.
3. Save it as a DDS with a full mip chain, for example with the NVIDIA Texture Tools Exporter, Microsoft's `texconv`, or the Remix Toolkit's texture ingestion.
   - For color, use BC7, BC1 or uncompressed RGBA8.
   - For grayscale, the one channel formats BC4, R8, R16 and R32F also work and show as gray.
   - Use the sRGB variant (for example `BC7_UNORM_SRGB`) when the image was painted by eye, which is almost always the case. If the format has no sRGB variant or the tool writes a linear one, set `color_space` to `srgb`.
   - For HDR, use BC6H or a float format. Values above 1 brighten the light.
   - Alpha is ignored unless `use_alpha` is on.
   - For flipbooks, a box filter for the mips keeps neighboring cells out of each other.
4. Put the DDS next to the mod's USD layer, for example in a `textures` folder.

### Step 2: Author the light

**On a captured game light.** Find the light's hash first. In the Remix developer menu, open the Rendering tab, then Lighting, then Light Statistics, and enable Enable Debug Visualization and Draw Light Hashes. Hover over the light to see its tooltip. A middle click copies the hash. Then add an `over` for `/RootNode/lights/light_<hash>` to a layer of the mod:

```usda
#usda 1.0

over "RootNode"
{
    over "lights"
    {
        over "light_0123456789ABCDEF"
        {
            asset inputs:shaping:gobo:file = @./textures/window_gobo.dds@
            float inputs:shaping:gobo:rotation = 15
        }
    }
}
```

The light keeps its position, direction, cone, color and intensity from the game, and only gains the gobo. The game light must be a spot light, or the override must author `inputs:shaping:cone:angle` to give it shaping.

Use `over`, not `def`. A `def` prim defines a complete light, so every attribute it does not author falls back to its USD default, and the game light's own values are lost.

**On a new light.** Lights defined in a mesh or light replacement are complete lights, so they use `def` and author all of their attributes, including the shaping:

```usda
over "RootNode"
{
    over "meshes"
    {
        over "mesh_0123456789ABCDEF"
        {
            def SphereLight "WindowLight" (
                prepend apiSchemas = ["ShapingAPI"]
            )
            {
                float inputs:radius = 2
                float inputs:intensity = 5000
                float inputs:shaping:cone:angle = 30
                float inputs:shaping:cone:softness = 0.1
                asset inputs:shaping:gobo:file = @./textures/window_gobo.dds@
                float3 xformOp:rotateXYZ = (-90, 0, 0)
                double3 xformOp:translate = (0, 100, 0)
                uniform token[] xformOpOrder = ["xformOp:translate", "xformOp:rotateXYZ"]
            }
        }
    }
}
```

A USD light shines along its -Z axis, so the rotation above points this light down.

A child light under a `light_<hash>` replacement is placed relative to the game light, and the game light only has a direction. The image of such a child that is not rolled against its parent is upright in the scene whichever way the game light points, and a child that is rolled keeps its roll.

**An animated flipbook.** This plays a texture with 4 rows and 4 columns of cells, of which the first 14 are used, at 12 frames per second with a fade between the frames. Each copy of the light starts at a different time.

```usda
#usda 1.0

over "RootNode"
{
    over "lights"
    {
        over "light_0123456789ABCDEF"
        {
            asset inputs:shaping:gobo:file = @./textures/water_caustics_4x4.dds@
            int inputs:shaping:gobo:sprite_sheet_rows = 4
            int inputs:shaping:gobo:sprite_sheet_cols = 4
            int inputs:shaping:gobo:sprite_sheet_frame_count = 14
            float inputs:shaping:gobo:sprite_sheet_fps = 12
            bool inputs:shaping:gobo:sprite_sheet_blend = true
            token inputs:shaping:gobo:sprite_sheet_loop_mode = "loop"
            float inputs:shaping:gobo:time_offset_random = 2
        }
    }
}
```

**Aspect and scale.** This shows a 16:9 image at three quarters of the cone's width, mirrored left to right, with a soft edge:

```usda
#usda 1.0

over "RootNode"
{
    over "lights"
    {
        over "light_0123456789ABCDEF"
        {
            asset inputs:shaping:gobo:file = @./textures/film_frame.dds@
            float inputs:shaping:gobo:aspect = 1.7778
            float2 inputs:shaping:gobo:scale = (-1.3333, 1.3333)
        }
    }
}
```

### Size and shape

The longer side of the image spans the field of view and the shorter side follows the aspect, so an image is not stretched. With the default field of view the longer side is as wide as the round cone, so the cone cuts off the corners. To show a whole rectangle, lower `fov` or raise `scale` until the corners fit.

| To make the image | Set |
| --- | --- |
| Twice as large | `scale = (0.5, 0.5)` |
| Half as large | `scale = (2, 2)` |
| Twice as wide | `scale = (0.5, 1)` |
| Twice as tall | `scale = (1, 0.5)` |
| Mirrored left to right | `scale = (-1, 1)` |
| Mirrored top to bottom | `scale = (1, -1)` |
| Fit a different shape, such as an anamorphic image | `aspect` to the intended width divided by the height |
| Tile three times across | `scale = (3, 3)` and `wrap_mode = "repeat"` |

The scale applies along the image's own axes, so a rotated image keeps its shape. The image rotates around the light's direction, so an image with an offset circles around it.

### Animation

- A flipbook plays from left to right, then from top to bottom, on the clock of material sprite sheets. The same sheet, rows, columns and integer frame rate show the same cell on a material and a gobo. A partial sheet, blending, another loop mode or a time offset changes that.
- A material shows the whole texture at a frame rate of zero. A gobo shows one cell.
- `once` starts on the first frame the light renders its gobo. Updating the light does not restart it. It starts again when the light is created again.
- The clock of material sprite sheets wraps every 4.66 hours, where flipbooks skip to another frame. Scrolling, rotation and shake do not wrap.
- Scrolling and tiling work inside each flipbook cell.

### Step 3: Check it in game

1. Load the mod and look at the lit area. The pattern appears inside the cone and in volumetric lighting.
2. Hover over the light with the light debug visualization on. The tooltip lists the gobo with its texture, all of its options, the resolved aspect and the current flipbook cell.
   - "Rendered" means the gobo is drawn this frame.
   - "Not Rendered" means the texture is not loaded yet or failed to load, the light has no shaping, or gobos are turned off.
3. Toggle Enable Light Gobos to compare the light with and without its gobo. It is in the Game Setup tab, under Step 2: Parameter Tuning, Light Gobos, and is also the `rtx.lights.enableGobos` option.
4. If the light shows no gobo, check the log for these messages. In each case the light renders without its gobo.
   - `Texture <path> asset data cannot be found or corrupted.` The file path is wrong or the file is not a DDS.
   - `Ignoring a light gobo on a light that is not a sphere light with shaping.` The light is not a spot light.
   - `A light gobo texture failed to load.` The DDS is invalid or uses an unsupported format.

### Orientation

The light projects along its shaping direction, which is the light's -Z axis in USD. The light's local Y axis is the up direction of the image, so rotating the light turns the gobo as well. The top row of the texture is projected towards up. When the up direction is parallel to the shaping direction, another axis is picked. A captured game light has only a direction and no orientation, so its up direction is the scene's up axis (`rtx.zUp`), and `inputs:shaping:gobo:rotation` turns the image. The side that is right follows the handedness of the world as the game's camera shows it, so the image reads the same way on screen in left-handed and right-handed games.

### Limits

- Gobos only work on sphere lights with shaping. A gobo on any other light, or on a sphere light without shaping, is ignored and a warning is logged once.
- The cone falloff, softness and focus still apply on top of the gobo.
- Use a texture with mip maps. The renderer blurs the pattern with lower mips for large lights and for volumetric lighting. A texture without mips aliases at a distance.
- The light's radius blurs the pattern and the edge of a clamped image by the emitter's full diameter at every distance. With a field of view of 60 degrees, a pattern that needs detail of 2% of the image, such as a window frame, needs a radius below about 1% of the distance to the lit surface. A light with a radius of 2 shows such a pattern sharp at a distance of about 200 or more.
- The whole mip chain of a gobo texture is kept in video memory. Until the texture is loaded, the light renders without its gobo.
- Light sampling does not account for the gobo. A mostly black gobo on a bright light wastes light samples, so the scene can look noisier than with a dimmer light and no gobo.
- A light has one gobo. Gobos cannot be layered.
- The Remix API cannot set gobos yet.
- An animated gobo shortens the history of volumetric lighting inside its cone, so the fog there is noisier. Only the 8 such lights nearest to the camera do this. Others can leave trails in fog.
- Indirect light, radiance caches and denoisers follow a moving pattern with some delay.
- `color_space = "srgb"` decodes after filtering. An sRGB texture format filters more accurately.

## For developers

### Data flow

1. `UsdMod::Impl::processLight` loads the texture with `getTexture(..., forcePreload = true)` and stores it with `LightData::setGoboTexture`. `LightData::deserializeGobo` reads the other attributes, which are listed in `LIST_GOBO_CONSTANTS`.
2. Each gobo field has a dirty flag, and `LightData::merge` keeps the dirty fields, so a gobo on an override survives the merge with the captured D3D9 light.
3. `LightData::toRtLight` attaches an immutable `RtLightGobo` and the light's local Y axis to the `RtLight`. The gobo is held outside of the light type union and outside of the light hashes, so replacement matching is unchanged.
4. `LightManager::trackGoboTextures` runs every frame after the lights are added and before the bindless tables are built. It tracks every gobo texture, and stores its bindless index, size and format on the light. A texture without an image gets no index, so the light is packed without its gobo rather than sampling the black dummy texture. It also resolves one anisotropic sampler per wrap mode: clamp to edge for `clamp` and `extend`, repeat and mirrored repeat.
5. `RtLight::writeGPUData` builds the projection frame, resolves the animation and packs the gobo into the sphere light.

### Time

The shaders get no time. `LightManager::prepareSceneData` packs and uploads every light each frame, so static, sleeping and externally tracked lights animate without being updated, and their hashes and identities do not change.

- Flipbooks use `GlobalTime::animationTimeMs`, the clock behind `timeSinceStartSeconds`. Without blending the frame is computed in single precision like `calcSpritesheetValues`, so the two switch on the same frame.
- Scrolling, rotation and shake use the unwrapped `GlobalTime::absoluteTimeUs` in double precision, and wrap before packing.
- `once` counts from `RtLight::m_goboStartTimeUs`. Assigning to a light keeps its start time.
- `time_offset_random` and random playback are seeded by the light's stable identity.

### GPU layout

The gobo uses the sphere light's free bytes, so `MemoryPolymorphicLight` stays 64 bytes:

| Location | Content |
| --- | --- |
| `data0.w` bits 16-31 | Texture index |
| `data2.x` | Tangent X and Y, snorm16 |
| `data2.y` bits 0-15 | Tangent Z, snorm16 |
| `data2.y` bits 16-31 | Unused |
| `data2.z` | UV scale X and Y, half precision. The CPU folds in the field of view, aspect and scale. |
| `data2.w` | UV offset X and Y. They are half precision for clamp and extend, and unorm fractions of the wrap period, in 1/65536 steps, for repeat and mirror. |
| `data3.x` bits 0-7 | Sprite sheet columns |
| `data3.x` bits 8-15 | Sprite sheet rows |
| `data3.x` bits 16-23 | Weight of the next cell, in 1/256 steps |
| `data3.x` bits 24-31 | Strength, unorm8 |
| `data3.y` bits 0-15 | Current cell |
| `data3.y` bits 16-31 | Next cell |
| `data3.w` bit 3 | `lightFlagGobo` |
| `data3.w` bits 4-5 | Wrap mode |
| `data3.w` bit 6 | `lightFlagGoboInvert` |
| `data3.w` bit 7 | `lightFlagGoboUseAlpha` |
| `data3.w` bit 8 | `lightFlagGoboGrayscale`, set for one channel formats |
| `data3.w` bit 9 | `lightFlagGoboDecodeSrgb` |

A light without a gobo pays one bit test. The value limits keep the packed fields finite, and the UV scale is also capped at 65000. The texture's size is read on the GPU from the bound texture.

### Evaluation

`sphereLightCalcShaping` applies the gobo after the cone falloff, and only fetches the texture where the falloff is above zero. Every sampling path uses it: RTXDI, the NEE cache, the integrators and volumetrics. The projection starts at the light's center, not at the sampled point on the emitter, so it is deterministic per receiver and adds no noise.

The filter combines two widths:

- The emitter's projection on the image, which softens the pattern for large lights. Froxels widen it by `lightGoboVolumeLodBias`.
- The footprint of the pixel or froxel, estimated from the camera's distance to the receiver. On surfaces it is an ellipse, which becomes the gradients of one anisotropic fetch. Reflections and portals use the same straight distance, so their footprint is an approximation.

Both widths also soften the edge of a clamped image, the emitter's up to the size of the image. An image without cells wraps or clamps in the sampler of its wrap mode, so the taps of an anisotropic fetch never reach the opposite edge. A flipbook uses the clamping sampler, is filtered evenly, stops at the mip where a cell is two texels wide, and clamps to the centers of the texels wholly inside the cell. Sheets whose size is not a power of two can bleed slightly at their small mips, as those mips already mix neighboring cells. A repeating flipbook can show a seam about a texel wide.

A gobo costs one texture fetch, or two while frames blend.

Light selection weights ignore the gobo, as they ignore shaping.

### Temporal reuse

- RTXDI evaluates reused samples with the current lights, so shading has no stale pattern. Temporal bias correction and the gradients evaluate `previousLights`, which hold the previous frame's animation state. A moving pattern gives a gradient where it changes luminance, which lowers the NRD confidence. Gradients are not available with sparse rendering, and Ray Reconstruction does not read the confidence.
- The NEE cache learns which lights matter over a few frames and shades with the current lights, so a moving pattern causes noise rather than a trail.
- ReSTIR GI stores radiance. Lighting validation only discards samples that are on screen and whose gradient is above `rtx.restirGI.lightingValidationThreshold`. Other samples, and the SHaRC and NRC caches, catch up with their own history.
- Volumetric ReSTIR evaluates the selected sample with the current lights, but the froxel history would hold a pattern for over a hundred frames. `LightManager` passes the animated gobo lights nearest to the camera to `volume_integrate_pass`, which caps the stored history age in their cones. The integrator weighs a new frame with one over the stored age plus `lightGoboVolumeHistoryAgeOffset`, so the cap gives new frames a weight of one over `lightGoboVolumeHistoryFrames`, 10 frames.

### Previous frame lights

RTXDI reads `previousLights`, which hold last frame's texture indices. These stay valid while the texture cache keeps its generation, because the cache never shrinks within a generation. After `RtxTextureManager::clear` they may point past the current texture table, so `LightManager` compares the generations of the two light buffers and clears `lightGoboArgs.enablePreviousLightGobos`. `RAB_GetMemoryPolymorphicLight` then removes the gobo flag from previous lights.

### Tests

`tests/rtx/unit/test_light_gobo.cpp` covers attribute parsing, defaults and clamping, the merge with a captured light, frame selection, motion over long times, the aspect with rotation and scale, the packed GPU layout, the world handedness, the up vector of relative lights, and the tangent decoder, which the shaders and the test share through `light_types.h`.
