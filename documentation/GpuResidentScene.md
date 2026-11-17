# GPU-resident scene

The GPU-resident scene is an experimental scene path for applications that render only through the Remix API.
Remix keeps the API's materials, meshes and instances on the GPU across frames and uploads only what changed,
instead of rebuilding its scene from the instances drawn each frame.

## Using the GPU-resident scene

### Enabling it

Set `rtx.gpuResidentScene = True`, for example in `rtx.conf`.

- Remix reads the option once when it creates its device, so changing it requires a restart.
- While it is enabled, D3D9 geometry is not ray traced. Remix logs a warning the first time D3D9 geometry arrives.
- While it is disabled, which is the default, the Remix API behaves as before and none of the GPU-resident tables are allocated.

### API functions by mode

| Function | `rtx.gpuResidentScene = False` | `rtx.gpuResidentScene = True` |
|---|---|---|
| `UpdateMaterial`, `SceneInstanceAdd`, `SceneInstanceUpdate`, `SceneInstanceRemove` | Return `REMIXAPI_ERROR_CODE_GENERAL_FAILURE` and log a warning once. | Supported. |
| `SetWaterSim`, `AddWaterDisturbance` | Supported. | Have no effect, and log a warning once. |
| `CreateMaterial`, `CreateMesh` | Use `hash` as the handle. | Ignore `hash` and return a handle that Remix generates. See [Handles](#handles). |
| D3D9 draw calls | Ray traced. | Not ray traced, and log a warning once. |

All other functions work in both modes. Content that the GPU-resident scene does not render yet is listed in [Known gaps](#known-gaps).

### Handles

- `CreateMaterial` and `CreateMesh` return handles that Remix generates. They ignore the `hash` field of their info struct.
- A handle stays valid until it is destroyed, and a destroyed handle never refers to a later object that reuses its slot.
- `DestroyMaterial`, `DestroyMesh` and `SceneInstanceRemove` return `REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS` for a handle that is already destroyed.
  Other calls ignore destroyed handles.
- `CreateMesh` returns `REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS` for a mesh without surfaces.
- `CreateMaterial`, `CreateMesh` and `SceneInstanceAdd` return `REMIXAPI_ERROR_CODE_GENERAL_FAILURE` when no slot is left for their first surface.
  A mesh or instance that runs out of slots after its first surface still returns success, but it is truncated and Remix logs an error once.
- Surfaces whose material has been destroyed are not rendered.

### Instances

There are two kinds of instances:

- **Frame-local instances.** `DrawInstance` adds an instance for the current frame only.
  It gets new surface slots every frame, so it has no motion vectors, it has no history in the NEE cache, RTXDI or ReSTIR GI,
  and its TLAS instances are rewritten every frame. Use it for transient content.
- **Retained instances.** `SceneInstanceAdd` adds an instance that stays in the scene until `SceneInstanceRemove` removes it.
  `SceneInstanceUpdate` replaces its instance info, for example to move it.
  A retained instance keeps its surface slots, so it keeps its history, and a moved instance gets motion vectors.
  Its TLAS instances are rewritten only when it changes. Use it for everything that persists across frames.

`UpdateMaterial` changes a material in place, and every surface that uses it picks up the change.
It rejects changes that affect surfaces or acceleration structures: the material type, the sprite sheet,
the alpha test and blend parameters, `useDrawCallAlphaState` and the subsurface diffusion profile.
Use `DestroyMaterial` and `CreateMaterial` for those.

`SceneInstanceAdd`, `SceneInstanceUpdate`, `SceneInstanceRemove` and `UpdateMaterial` require `rtx.gpuResidentScene`.
See [API functions by mode](#api-functions-by-mode).

### Limits

The scene has three pools of slots:

- **Materials.** Each material takes one slot.
  There are 65,535 by default, and `rtx.gpuResidentSceneMaxMaterialCount` changes the count.
- **Mesh surfaces.** Each entry of a mesh's `surfaces_values` takes one slot, and has its own BLAS. There are 65,535.
- **Instance surfaces.** Each instance takes one slot for every surface of its mesh.
  These slots are the surfaces that shaders and the TLASes see.
  For example, a mesh with 3 surfaces takes 3 mesh surface slots, and 1,000 instances of it take 3,000 instance surface slots.
  There are 65,535 by default, and `rtx.gpuResidentSceneMaxSurfaceCount` changes the count.

All slots are allocated up front, when Remix creates its device.

Other limits:

- A freed slot is reused only after the GPU has finished the frame that freed it.
  Frame-local instances free their instance surface slots every frame,
  so with the default frame latency they can use about a quarter of the instance surface slots per frame.
- `DestroyMaterial` and `DestroyMesh` scan every instance surface slot on the render thread,
  so they can cost more than the other calls.
- Each mesh surface takes at least two of the roughly 65,000 bindless buffer slots, for its index and vertex buffers.
  Mesh surfaces created after those slots run out are not ray traced, and Remix logs a warning once.
  `CreateMesh` still returns success for them.

### Known gaps

The GPU-resident scene does not support everything the default scene path does yet.
Unsupported content logs one warning and renders as follows:

- Skinned meshes render in their bind pose.
- GPU instancing through `remixapi_InstanceInfoGpuInstancingEXT` is ignored.
- Particle systems render as their base triangles.
- Portal materials render as default opaque materials.
- Texture transforms of materials (scale, offset, rotation and scrolling) are ignored.
- Sky-category instances are not rendered.
- The water simulation does not run, so `SetWaterSim` and `AddWaterDisturbance` have no effect.

These are not implemented yet either, but Remix does not log a warning for them:

- The `IgnoreAlphaChannel` instance category is not supported.
- USD replacements do not apply to API meshes and materials.
- Material samplers do not use the upscaler's mip bias or anisotropic filtering.

## Implementation details

The code lives in `src/dxvk/rtx_render/rtx_gpu_resident_scene.*`.
`SceneManager` creates the scene only when the option is enabled, and its scene getters then return the GPU-resident tables.

- **Slots.** Materials, mesh surfaces and instance surfaces live in fixed-capacity `GenerationArray`s.
  The material and instance surface capacities are read from their options when the scene is created.
  A mesh is a chain of BLAS records and an instance is a chain of surface records, one per mesh surface.
  API handles pack a slot's index and generation as `generation << 32 | (index + 1)`.
  Freed slots go into a per-frame batch and become available again only after that frame's GPU event signals.
- **Capacity.** Remix allocates every material and instance surface slot when it creates its device, whether the slot is used or not.
  Both options go up to 2,097,151, the most that the shaders' 21-bit surface and material indices can address.
  Each instance surface slot takes roughly 460 bytes of GPU memory and about twice that in system memory,
  so 2,097,151 of them take about 1 GiB of GPU memory and 2 GiB of system memory.
- **Tables.** The surface, material and subsurface tables are allocated for every slot, with CPU shadows.
  Only the 64 KiB pages that changed are uploaded. The material table is indexed by material slot,
  and each surface stores its material's slot. Shaders see the full slot count, and empty slots are zero.
- **BLASes.** Each mesh surface has its own `PREFER_FAST_BUILD` BLAS, built when the mesh is created or its material changes.
- **TLASes.** The opaque, unordered and SSS TLASes have persistent instance buffers that are patched in place,
  and a TLAS is rebuilt only when its instances changed. BLAS owners stay alive for the command lists that may still traverse them.
- **History.** A surface maps to itself in the previous-to-current surface mapping when it kept its slot generation and opaque BLAS
  since the last opaque TLAS rebuild. The NEE cache's primitive prefix sums are kept per slot,
  and only the part behind the first changed slot is uploaded.
- **Frame flow.** `SceneManager::prepareSceneResources` creates dirty materials before the frame's texture upload.
  `SceneManager::prepareSceneData` runs the graph overrides and updates the bindless tables and light buffers, then `gpuResidentScenePrepare`
  uploads the tables and builds the BLASes and TLASes. `SceneManager::onFrameEnd` releases frame-local instances and finishes completed frees.
  On frames without a scene, `RtxContext` resolves pending frees by waiting for the GPU.
- **Lifetime.** `SceneManager::clear` leaves the GPU-resident scene alone, because its resources live until the API destroys them.
