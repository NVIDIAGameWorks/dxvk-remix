# Changelog

## [Unreleased]

### Added

- Add the optional NUMOS sky (`rtx.skyMode = 1`) with atmosphere, volumetric clouds, haze, night sky and weather presets. Its haze runs only while Remix volumetrics are off. See `documentation/NumosSky.md`.
- Add optional surface-area particle spawn distribution and uniform sampling within triangles, including reliable lookup for replacement materials.
- Add optional emitter-material alpha and luminance birth masks with grayscale probability or threshold cutout, including Toolkit schema and Remix API/bridge support.
