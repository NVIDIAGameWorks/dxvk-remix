# Changelog

## [Unreleased]

### Added

- Add optional surface-area particle spawn distribution and uniform sampling within triangles, including reliable lookup for replacement materials.

### Fixed

- Fix surfaces behind thin alpha tested and alpha blended cutouts, such as chain-link fences, being lit too darkly or too brightly, by resolving the cutouts' opacity at full texture resolution in indirect and visibility rays.
