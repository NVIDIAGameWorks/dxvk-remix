## Remix API Changelog

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Fixed
- Clients built against 0.6.3 get `remixapi_Interface` in the 0.6.3 order, with `SetCameraMediumMaterial` after `SetupCamera`.

### Added
- `remixapi_InstanceInfoParticleScreenSizeEXT` provides optional per-emitter screen-size bounds and opacity coverage compensation, including bridge transport.
- remixapi_Interface.SetWaterSim, remixapi_WaterSimInfo, REMIXAPI_STRUCT_TYPE_WATER_SIM_INFO: the state of the GPU water simulation (0.6.11)
- remixapi_Interface.AddWaterDisturbance, remixapi_WaterDisturbanceInfo, REMIXAPI_STRUCT_TYPE_WATER_DISTURBANCE_INFO: a dent in the simulated water (0.6.11)
- REMIXAPI_INSTANCE_CATEGORY_BIT_SLUDGE: sludge floating on top of the water surface (see rtx.sludgeTextures)

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
