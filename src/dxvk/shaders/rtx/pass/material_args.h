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
  float roughnessScale;
  float roughnessBias;
  float albedoScale;
  float albedoBias;
  float metallicScale;
  float metallicBias;
  float normalIntensity;
  float layeredWaterNormalMotionX;
  float layeredWaterNormalMotionY;
  float layeredWaterNormalMotionScale;
  float layeredWaterNormalLodBias;
  uint layeredWaterNormalEnable;
  uint enableThinFilmOverride;
  // Note: This thickness value is normalized on 0-1, predivided by the thinFilmMaxThickness on the CPU.
  float thinFilmNormalizedThicknessOverride = 0.0;
  uint pad0;
  uint pad1;
};

struct TranslucentMaterialArgs {
  float transmittanceColorScale;
  float transmittanceColorBias;
  float normalIntensity;
  uint enableDiffuseLayerOverride;
};

#ifdef __cplusplus
// We're packing these into a constant buffer (see: raytrace_args.h), so need to remain aligned
static_assert((sizeof(OpaqueMaterialArgs) & 15) == 0);
static_assert((sizeof(TranslucentMaterialArgs) & 15) == 0);

#include "rtx_option.h"

namespace dxvk {

struct OpaqueMaterialOptions {
  friend class ImGUI;

  // Modifiers

  RTX_OPTION("rtx.opaqueMaterial", float, roughnessScale, 1.0f, "Scales the original roughness value.");
  RTX_OPTION("rtx.opaqueMaterial", float, roughnessBias, 0.0f, "Offsets the original roughness value.");
  RTX_OPTION("rtx.opaqueMaterial", float, albedoScale, 1.0f, "Scales the original albedo value.");
  RTX_OPTION("rtx.opaqueMaterial", float, albedoBias, 0.0f, "Offsets the original albedo value.");
  RTX_OPTION("rtx.opaqueMaterial", float, metallicScale, 1.0f, "Scales the original metallic value.");
  RTX_OPTION("rtx.opaqueMaterial", float, metallicBias, 0.0f, "Offsets the original metallic value.");
  RTX_OPTION("rtx.opaqueMaterial", float, normalIntensity, 1.0f, "Scales normal map strength.");
  RTX_OPTION("rtx.opaqueMaterial", Vector2, layeredWaterNormalMotion, Vector2(-0.25f, -0.3f), "");
  RTX_OPTION("rtx.opaqueMaterial", float, layeredWaterNormalMotionScale, 9.0f, "");
  RTX_OPTION("rtx.opaqueMaterial", float, layeredWaterNormalLodBias, 5.0f, "");
  RTX_OPTION("rtx.opaqueMaterial", bool, layeredWaterNormalEnable, true, "");

  // Overrides

  RTX_OPTION("rtx.opaqueMaterial", bool, enableThinFilmOverride, false, "");
  // Note: This thickness value is normalized on 0-1, predivided by the thinFilmMaxThickness on the CPU.
  RTX_OPTION("rtx.opaqueMaterial", float, thinFilmNormalizedThicknessOverride, 0.0f, "");

public:
  void fillShaderParams(OpaqueMaterialArgs& args) const {
    args.roughnessScale = roughnessScale();
    args.roughnessBias = roughnessBias();
    args.albedoScale = albedoScale();
    args.albedoBias = albedoBias();
    args.metallicScale = metallicScale();
    args.metallicBias = metallicBias();
    args.normalIntensity = normalIntensity();
    args.layeredWaterNormalMotionX = layeredWaterNormalMotion().x;
    args.layeredWaterNormalMotionY = layeredWaterNormalMotion().y;
    args.layeredWaterNormalMotionScale = layeredWaterNormalMotionScale();
    args.layeredWaterNormalLodBias = layeredWaterNormalLodBias();
    args.layeredWaterNormalEnable = layeredWaterNormalEnable();
    args.enableThinFilmOverride = enableThinFilmOverride();
    args.thinFilmNormalizedThicknessOverride = thinFilmNormalizedThicknessOverride();
  }
};

struct TranslucentMaterialOptions {
  friend class ImGUI;

  // Modifiers

  RTX_OPTION("rtx.translucentMaterial", float, transmittanceColorScale, 1.0f, "");
  RTX_OPTION("rtx.translucentMaterial", float, transmittanceColorBias, 0.0f, "");
  RTX_OPTION("rtx.translucentMaterial", float, normalIntensity, 1.0f, "");

  // Overrides

  RTX_OPTION("rtx.translucentMaterial", bool, enableDiffuseLayerOverride, false, "");

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
