# Terrain System

Terrain is handled by baking original game draw calls into a single material for the terrain. This bakes multiple layers of terrain as the original game composited them into a single material. Then during ray tracing the surfaces associated with terrain textures sample from the shared terrain material rather than from different material layers. Textures that are to be considered as terrain need to be tagged as [rtx.terrainTextures](../RtxOptions.md). The baking is done along a single geometric direction. Namely it bakes material projecting downwards onto a terrain plane and, therefore, only multi-layer surfaces that are horizontal for the most part should be tagged as terrain textures. Some vertical slope is acceptable, though the steeper the slope the larger terrain resolution is needed to reconstruct the full detail of the input textures during ray tracing. The vertical axis in the game is determined via [rtx.zUp](../RtxOptions.md) option. Terrain baker's texture resolution is keyed off from [rtx.sceneScale](../RtxOptions.md). Additionally, to remove original game's lighting data from the baked terrain, undesired textures can be marked as 'ignored' or 'lightmap'. This is beneficial, as the illumination needs to be handled by ray tracing alone, and a game's original lighting solution should be avoided.
Some games blend lightmaps on top of base textures in a single draw call, and in that case, enabling [rtx.ignoreLastTextureStage](../RtxOptions.md) (Game Setup -> Parameter Tuning -> Texture Parameters -> Ignore last texture stage) might be useful to automatically ignore such lightmaps, however, a game might bind not only a lightmap to the last texture stage but also some actual color textures, so the option should be used with care.

Game authoring steps:
- set [rtx.zUp](../RtxOptions.md)
- set [rtx.sceneScale](../RtxOptions.md)
- tag terrain textures
- (optional) tag ignore / lightmap textures
- adjust Terrain Baker properties to reach desired resolution and performance for the terrain

Supported:
- Programmable shaders
- Replacement materials - AlbedoOpacity texture and secondary PBR textures: Normal, Tangent, Roughness, Metallic and Emissive texture

Caveats/limitations:
- If any terrain texture is replaced, all terrain textures must be replaced, and all must have the same types of replacement textures specified.
  - Material property constants [rtx.terrainBaker.material.properties.*](../RtxOptions.md) used by Terrain Baker are set for the material used for terrain rendering. These properties are not baked. If a replacement material for any of the input terrain texture layers replaces this constant (i.e. roughness, emissive, etc. ), then the shared constant will not be used. Therefore all materials tagged as terrain need to have the same types of replacement textures set. In the future this could be handled by baking the constant into a terrain texture if a replacement texture is specified for any of the active terrain meshes.
- Using too high resolution replacement textures can have noticeable performance impact. Adjust [rtx.terrainBaker.material.maxResolutionToUseForReplacementMaterials](../RtxOptions.md) to balance quality vs performance cost of baking replacement textures.
- Terrain baking is not free. It has a computational and memory cost. You can parametrize its properties to suit your needs and memory limitations. In the future there may be a more adaptive mechanism to fit in-game scenario and resource availability. Tune following settings to adjust resolution and memory overhead of the Terrain System [rtx.terrainBaker.cascadeMap.*](../RtxOptions.md) and [rtx.terrainBaker.material.maxResolutionToUseForReplacementMaterials](../RtxOptions.md).
- Programmable shaders with Shader Model 2.0+ utilize a preprocessing compute pass to support baking of secondary PBR textures. This is an expensive operation both performance and memory wise. Draw calls using fixed function pipeline and programmable pipelines with Shader Model 1.0 instead rely on shader injection. This shader injection functionality is controlled via [rtx.terrainBaker.material.replacementSupportInPS_programmableShaders](../RtxOptions.md) and [rtx.terrainBaker.material.replacementSupportInPS_fixedFunction](../RtxOptions.md) that needs to be set prior to launching the game on modification. Both of these are enabled by default. While these options should work they are still in experimental stage. If you observe any crashes or quality issues which get fixed by disabling the two options then file github issues for them.
- Differences between the UV tiling size of the incoming terrain draw calls and the baked terrain map can cause precision loss in baked heightmaps.  Specifically, the baked heightmap's max displacement distance may be significantly larger (or smaller) than the actual max displacement distance of any of the incoming draw calls, resulting in a loss of detail.  [rtx.terrainBaker.material.properties.displaceInFactor](../RtxOptions.md) can be used to fix that by shrinking the baked heightmap's max displacement.  When it is too low, the displacement will lack detail.  When it is too high, the lowest and highest parts of the POM will flatten out. The `Calculate Scene's Optimal Displacement Factor` button in the terrain material properties panel can be used to automatically calculate the highest safe value for a given scene.  `rtx.conf` creators should use that button in representative sample of a game's levels, and then set `displaceInFactor` to the lowest observed value.

Future considerations / not supported (yet):
- Material constants per input terrain replacement material.
- Orthogonal surfaces (i.e. walls with floor) and separate multi-layer surfaces (i.e. baked road on a bridge over another baked road below). For these cases you can use the decal system. The benefit of the terrain system to the decal system is that the terrain baking exactly replicates original game's blending while the decal system only approximates the original blending. On the other hand, decal system supports blending in any direction and the input decal textures are sampled at the desired resolution at the time of ray hits the surface while the terrain system resamples original textures to a shared terrain texture. This can result in a of loss of image fidelity depending on the parametrization of the terrain system (see [rtx.terrainBaker.*](../RtxOptions.md)).


## Terrain as decals

If a game draws a terrain as: an opaque draw call and blending translucent draw calls on top, then potentially, a terrain can be emulated via decals, since there's an opaque termination surface.

Such terrain-as-decals behaviour is set when the terrain baker 'Enable Runtime Terrain Baking' (`rtx.terrainBaker.enableBaking`) is disabled, so that the draw calls that are marked as 'Terrain' will be internally handled as a decal.

Advantages of terrain-as-decals:
- Lower VRAM consumption as there are no terrain baking render targets.
- Lower CPU workload, as draw calls don't need be preprocessed.
- Less GPU context switches caused by rasterization of each terrain piece and layer in a specific way.
- Can handle orthogonal surfaces (e.g. a horizontal beach and vertical cliff can be handled).
- Higher resolution texture blending compared to using terrain baker cascades.

Disadvantages of terrain-as-decals:
- The method is not applicable if terrain does NOT have an opaque surface draw call, as then decals won't have a termination point.
- Potentially, higher GPU workload, as there are more rays to trace against the decals.
