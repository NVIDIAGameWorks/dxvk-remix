## Remix API Changelog

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Fixed
- Clients built against 0.6.3 get `remixapi_Interface` in the 0.6.3 order, with `SetCameraMediumMaterial` after `SetupCamera`.

### Added
- `remixapi_InstanceInfoParticleSpawnMaskEXT` (struct type 33, API 0.6.13) controls optional emitter-material alpha/luminance birth masking, including bridge transport. Omitting it preserves unmasked spawning and existing public struct layouts. See [Particle Birth Masking](particle-spawn-masking.md).
- `remixapi_InstanceInfoParticleScreenSizeEXT` provides optional per-emitter screen-size bounds and opacity coverage compensation, including bridge transport.
- remixapi_Interface.SetWaterSim, remixapi_WaterSimInfo, REMIXAPI_STRUCT_TYPE_WATER_SIM_INFO: the state of the GPU water simulation (0.6.11)
- remixapi_Interface.AddWaterDisturbance, remixapi_WaterDisturbanceInfo, REMIXAPI_STRUCT_TYPE_WATER_DISTURBANCE_INFO: a dent in the simulated water (0.6.11)
- REMIXAPI_INSTANCE_CATEGORY_BIT_SLUDGE: sludge floating on top of the water surface (see rtx.sludgeTextures)
- remixapi_SceneInstanceHandle (0.6.12)
- remixapi_Interface.SceneInstanceAdd (0.6.12)
- remixapi_Interface.SceneInstanceUpdate (0.6.12)
- remixapi_Interface.SceneInstanceRemove (0.6.12)
- remixapi_Interface.UpdateMaterial (0.6.12)
- The four functions above require `rtx.gpuResidentScene`, and return `REMIXAPI_ERROR_CODE_GENERAL_FAILURE` while it is disabled. The 32-bit bridge forwards them, and returns success before the runtime has run them, so a bridged client finds a failure only in the bridge server log. See [GpuResidentScene.md](GpuResidentScene.md).
- `rtx.gpuResidentScene` (experimental, disabled by default). While it is enabled, `CreateMaterial` and `CreateMesh` return generated handles and ignore `hash`, `CreateMesh` rejects meshes without surfaces, and `DrawInstance` adds an instance for the current frame only.

### Changed
- `remixapi_InitializeLibrary` writes only the part of `remixapi_Interface` that the client's version knows.
- The bridge zero-initializes struct fields that it does not forward.
- The versioning rules for API changes are clarified in [RemixSDK.md](RemixSDK.md#extensibility).

## [0.6.8]

### Added

- `remixapi_InstanceInfoParticleSpawnDistributionEXT` (struct type 29), chained from `remixapi_InstanceInfo` alongside the particle-system extension, selects equal probability per triangle or probability proportional to surface area. Omitting it preserves the By Triangle default and existing public struct layouts. See [Particle Spawn Distribution](particle-spawn-distribution.md#api-and-serialization).

## [0.6.6]

### Added
- remixapi_MaterialInfoOpaqueEXT.textureScrollRate
- remixapi_MaterialInfoOpaqueEXT.textureScrollAngle
- remixapi_MaterialInfoOpaqueEXT.textureScale_hasvalue
- remixapi_MaterialInfoOpaqueEXT.textureScale_value
- remixapi_MaterialInfoOpaqueEXT.textureOffset
- remixapi_MaterialInfoOpaqueEXT.textureRotation
- remixapi_MaterialInfoTranslucentEXT.textureScrollRate
- remixapi_MaterialInfoTranslucentEXT.textureScrollAngle
- remixapi_MaterialInfoTranslucentEXT.textureScale_hasvalue
- remixapi_MaterialInfoTranslucentEXT.textureScale_value
- remixapi_MaterialInfoTranslucentEXT.textureOffset
- remixapi_MaterialInfoTranslucentEXT.textureRotation

## [0.6.5]

### Added
- remixapi_MaterialInfoOpaqueEXT.enableDlssControlMask
- remixapi_MaterialInfoOpaqueEXT.dlssControlMaskIntensity
- remixapi_MaterialInfoOpaqueEXT.dlssControlMaskToneStrength
- remixapi_MaterialInfoOpaqueEXT.dlssControlMaskStructuralStrength

## [0.4.2]

### Added
- MaterialInfoOpaqueEXT.displaceOut

### Changed
- renamed MaterialInfoOpaqueEXT.heightTextureStrength to MaterialInfoOpaqueEXT.displaceIn

### Fixed

### Removed


## [0.4.3]

### Added
- remixapi_MaterialInfoOpaqueSubsurfaceEXT.subsurfaceDiffusionProfile
- remixapi_MaterialInfoOpaqueSubsurfaceEXT.subsurfaceRadius
- remixapi_MaterialInfoOpaqueSubsurfaceEXT.subsurfaceRadiusScale
- remixapi_MaterialInfoOpaqueSubsurfaceEXT.subsurfaceMaxSampleRadius

### Changed

### Fixed

### Removed
