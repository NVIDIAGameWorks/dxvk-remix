# Changelog

## [Unreleased]

### Added

- Add optional surface-area particle spawn distribution and uniform sampling within triangles, including reliable lookup for replacement materials.
- Add optional emitter-material alpha and luminance birth masks with grayscale probability or threshold cutout, including Toolkit schema and Remix API/bridge support.
- Add light gobos, textures projected by replacement sphere lights with shaping, with flipbooks, aspect and scale, tiling, motion and strength, authored with the `inputs:shaping:gobo:*` USD attributes and controlled by `rtx.lights.enableGobos`.
