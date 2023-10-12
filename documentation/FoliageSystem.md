# Foliage System

To achieve realistic foliage rendering in games, Remix employs a subsurface scattering simulation using a single scattering approximation. The complexity of the volumetric equation is simplified by focusing solely on the boundary-to-volume interaction and restricting it to single-time scattering. This reduces the problem to a scattering radiance equation, similar to the well-known reflection rendering equation. The only difference is the BSDF based on foliage surface fresnel, thickness, volume absorption, and anisotropic properties.

User Instructions:
1. Enable Thin Opaque Surface Globally: Set [rtx.subsurface.enableThinOpaque] to true in your global settings.
2. Material Replacement
3. Foliage Properties Setup in Editor:
    a. [Subsurface Transmittance Color]: The transmittance color of subsurface material. The unit is [mm^-1].
    b. [Subsurface Measurement Distance]: The thickness of the foliage surface in the range [0, 16]. Incidence radiance is attenuated proportionally to the thickness. The unit is [mm].
    c. [Subsurface Single Scattering Albedo]: The coefficient determines how much energy is scattered when trace through subsurface materials.
    d. [Subsurface Volumetric Anisotropy]: The anisotropy of the scattering phase function (-1 being backscattering, 0 being isotropic, 1 being forward scattering).

Real-time debug interface:
1. Debugging View: Enable [Is Thin Opaque](../RtxOptions.md) or [rtx.debugView.debugViewIdx = 800] to verify if thin opaque materials are correctly set up.

Limitations and Future consideration:
1. Multiple Scattering Support: Future iterations may provide support for multiple scattering, allowing some radiance to scatter back to the same side of the surface. This aligns closer with realism and has the potential to enhance render quality.
