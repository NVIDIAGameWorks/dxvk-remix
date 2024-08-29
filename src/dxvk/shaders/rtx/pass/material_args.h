/*
* Copyright (c) 2021-2023, NVIDIA CORPORATION. All rights reserved.
*
* Permission is hereby granted, free of charge, to any person obtaining a
* copy of this software and associated documentation files (the "Software"),
* to deal in the Software without restriction, including without limitation
* the rights to use, copy, modify, merge, publish, distribute, sublicense,
* and/or sell copies of the Software, and to permit persons to whom the
* Software is furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
* THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
* FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
* DEALINGS IN THE SOFTWARE.
*/
#pragma once

// Note:
// Modifiers - Options which modify incoming material parameterizations (applied on top of existing assets).
// Overrides - Options which directly override material information globally.
// These would be split into their own structs, but to minimize how much padding is needed they are combined for the time being.

struct OpaqueMaterialArgs {
  float albedoScale = 1.f;
  float albedoBias = 0.f;
  float roughnessScale = 1.f;
  float roughnessBias = 0.f;
  float metallicScale = 1.f;
  float metallicBias = 0.f;
  float normalIntensity = 1.f;
  float layeredWaterNormalMotionX = 0.f;
  float layeredWaterNormalMotionY = 0.f;
  float layeredWaterNormalMotionScale = 1.f;
  float layeredWaterNormalLodBias = 0.f;
  uint layeredWaterNormalEnable = 0;
  uint enableThinFilmOverride = 0;
  // Note: This thickness value is normalized on 0-1, predivided by the thinFilmMaxThickness on the CPU.
  float thinFilmNormalizedThicknessOverride = 0.0;
  uint pad0 = 0;
  uint pad1 = 0;
};

struct TranslucentMaterialArgs {
  float transmittanceColorScale = 1.f;
  float transmittanceColorBias = 0.f;
  float normalIntensity = 1.f;
  uint enableDiffuseLayerOverride = 0;
};

#ifdef __cplusplus
// We're packing these into a constant buffer (see: raytrace_args.h), so need to remain aligned
static_assert((sizeof(OpaqueMaterialArgs) & 15) == 0);
static_assert((sizeof(TranslucentMaterialArgs) & 15) == 0);

#include "rtx_option.h"
#include "../util/util_macro.h"

namespace dxvk {

struct OpaqueMaterialOptions {
  friend class ImGUI;

  // Modifiers

  RTX_OPTION("rtx.opaqueMaterial", float, albedoScale, 1.0f, "A scale factor to apply to all albedo values in the opaque material. Should only be used for debugging or development.");
  RTX_OPTION("rtx.opaqueMaterial", float, albedoBias, 0.0f, "A bias factor to add to all albedo values in the opaque material. Should only be used for debugging or development.");
  RTX_OPTION("rtx.opaqueMaterial", float, roughnessScale, 1.0f, "A scale factor to apply to all roughness values in the opaque material. Should only be used for debugging or development.");
  RTX_OPTION("rtx.opaqueMaterial", float, roughnessBias, 0.0f, "A bias factor to add to all roughness values in the opaque material. Should only be used for debugging or development.");
  RTX_OPTION("rtx.opaqueMaterial", float, metallicScale, 1.0f, "A scale factor to apply to all metallic values in the opaque material. Should only be used for debugging or development.");
  RTX_OPTION("rtx.opaqueMaterial", float, metallicBias, 0.0f, "A bias factor to add to all metallic values in the opaque material. Should only be used for debugging or development.");
  RTX_OPTION("rtx.opaqueMaterial", float, normalIntensity, 1.0f, "An arbitrary strength scale factor to apply when decoding normals in the opaque material. Should only be used for debugging or development.");
  RTX_OPTION("rtx.opaqueMaterial", Vector2, layeredWaterNormalMotion, Vector2(-0.25f, -0.3f),
             "A vector describing the motion in the U and V axes across a texture to apply for layered water.\n"
             "Only takes effect when layered water normals are enabled (and an object is properly classified as animated water).");
  // Todo: This option is somewhat redundant and could be collapsed down into the water normal motion directly.
  RTX_OPTION("rtx.opaqueMaterial", float, layeredWaterNormalMotionScale, 9.0f,
             "A scale factor applied to the layered water normal motion vector.\n"
             "Only takes effect when layered water normals are enabled (and an object is properly classified as animated water).");
  RTX_OPTION("rtx.opaqueMaterial", float, layeredWaterNormalLodBias, 5.0f,
             "The LoD bias to use when sampling from the normal map on layered water for the second layer of detail.\n"
             "This value typically should be greater than 0 to allow for a more blurry mip to be selected as this allows for a low frequency variation of normals to be applied to the higher frequency variation from the typical normal map.\n"
             "Only takes effect when layered water normals are enabled (and an object is properly classified as animated water).");
  RTX_OPTION("rtx.opaqueMaterial", bool, layeredWaterNormalEnable, true,
             "A flag indicating if layered water normal should be enabled or disabled.\n"
             "Note that objects must be properly classified as animated water to be rendered with this mode.");

  // Overrides

  RTX_OPTION("rtx.opaqueMaterial", bool, ignoreAlphaChannelOverride, false, "A flag to ignore the alpha channel of the colormap on the opaque material. Should only be used for debugging or development.");
  RTX_OPTION("rtx.opaqueMaterial", bool, enableThinFilmOverride, false, "A flag to force the thin-film layer on the opaque material to be enabled. Should only be used for debugging or development.");
  RTX_OPTION("rtx.opaqueMaterial", float, thinFilmThicknessOverride, 0.0f,
             "The thin-film layer's thickness in nanometers for the opaque material when the thin-film override is enabled.\n"
             "Should be any value larger than 0, typically within the wavelength of light, but must be less than or equal to OPAQUE_SURFACE_MATERIAL_THIN_FILM_MAX_THICKNESS (" STRINGIFY(OPAQUE_SURFACE_MATERIAL_THIN_FILM_MAX_THICKNESS) " nm).\n"
             "Should only be used for debugging or development.");

public:
  void fillShaderParams(OpaqueMaterialArgs& args) const {
    args.albedoScale = albedoScale();
    args.albedoBias = albedoBias();
    args.roughnessScale = roughnessScale();
    args.roughnessBias = roughnessBias();
    args.metallicScale = metallicScale();
    args.metallicBias = metallicBias();
    args.normalIntensity = normalIntensity();
    args.layeredWaterNormalMotionX = layeredWaterNormalMotion().x;
    args.layeredWaterNormalMotionY = layeredWaterNormalMotion().y;
    args.layeredWaterNormalMotionScale = layeredWaterNormalMotionScale();
    args.layeredWaterNormalLodBias = layeredWaterNormalLodBias();
    args.layeredWaterNormalEnable = layeredWaterNormalEnable();
    args.enableThinFilmOverride = enableThinFilmOverride();
    // Note: GPU expects the thin film thickness override to be normalized on the maximum range.
    args.thinFilmNormalizedThicknessOverride = std::clamp(thinFilmThicknessOverride() / OPAQUE_SURFACE_MATERIAL_THIN_FILM_MAX_THICKNESS, 0.0f, 1.0f);
  }
};

struct TranslucentMaterialOptions {
  friend class ImGUI;

  // Modifiers

  RTX_OPTION("rtx.translucentMaterial", float, transmittanceColorScale, 1.0f, "A scale factor to apply to all transmittance color values in the translucent material. Should only be used for debugging or development.");
  RTX_OPTION("rtx.translucentMaterial", float, transmittanceColorBias, 0.0f, "A bias factor to add to all transmittance color values in the opaque material. Should only be used for debugging or development.");
  RTX_OPTION("rtx.translucentMaterial", float, normalIntensity, 1.0f, "An arbitrary strength scale factor to apply when decoding normals in the translucent material. Should only be used for debugging or development.");

  // Overrides

  RTX_OPTION("rtx.translucentMaterial", bool, enableDiffuseLayerOverride, false, "A flag to force the diffuse layer on the translucent material to be enabled. Should only be used for debugging or development.");

public:
  void fillShaderParams(TranslucentMaterialArgs& args) const {
    args.transmittanceColorScale = transmittanceColorScale();
    args.transmittanceColorBias = transmittanceColorBias();
    args.normalIntensity = normalIntensity();
    args.enableDiffuseLayerOverride = enableDiffuseLayerOverride();
  }
};

} // namespace dxvk
#endif
