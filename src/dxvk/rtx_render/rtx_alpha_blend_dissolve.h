/*
* Copyright (c) 2026, NVIDIA CORPORATION. All rights reserved.
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
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
* THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
* FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
* DEALINGS IN THE SOFTWARE.
*/
#pragma once

#include "rtx_types.h"

namespace dxvk {
  // Returns how rtx.enableAlphaBlendDissolve, while it is on, converts a draw call rendered with the given material.
  // Returns AlphaBlendDissolve::None when the draw call keeps its usual alpha handling.
  // isEye is the draw call's DrawCallState::isEye, passed in so that the rule depends only on its arguments.
  inline AlphaBlendDissolve calculateAlphaBlendDissolve(const DrawCallState& drawCall, bool isEye, const MaterialData& materialData) {
    if (materialData.getType() != MaterialDataType::Opaque) {
      return AlphaBlendDissolve::None;
    }

    if (drawCall.isUsingRaytracedRenderTarget ||
        drawCall.testCategoryFlags(InstanceCategories::Particle, InstanceCategories::Beam, DECAL_CATEGORY_FLAGS,
                                   InstanceCategories::AlphaBlendToCutout, InstanceCategories::HairCards,
                                   InstanceCategories::Terrain, InstanceCategories::AnimatedWater,
                                   InstanceCategories::WorldUI, InstanceCategories::WorldMatte, InstanceCategories::Sky)) {
      return AlphaBlendDissolve::None;
    }

    // Eye shading replaces the eye's material and opacity, so an eye's blend does not decide its coverage.
    // Games can pass an object's fade through the vertex alpha of software-lit meshes such as eyes.
    if (isEye) {
      return AlphaBlendDissolve::VertexAlpha;
    }

    // Only standard alpha blending is converted. Additive, inverted, premultiplied, color and multiplicative blends keep blending.
    const LegacyMaterialData& legacyMaterial = drawCall.getMaterialData();
    const DxvkBlendMode& blendMode = legacyMaterial.blendMode;

    if (!blendMode.enableBlending ||
        blendMode.colorBlendOp != VK_BLEND_OP_ADD ||
        blendMode.colorSrcFactor != VK_BLEND_FACTOR_SRC_ALPHA ||
        blendMode.colorDstFactor != VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA) {
      return AlphaBlendDissolve::None;
    }

    // Only the game's texture factor fades of TextureFactorFade are converted. Other alpha blends keep blending.
    if (legacyMaterial.textureFactorFade == TextureFactorFade::None || (legacyMaterial.tFactor >> 24) == 0xff) {
      return AlphaBlendDissolve::None;
    }

    const OpaqueMaterialData& opaqueMaterial = materialData.getOpaqueMaterialData();

    // A replacement that defines its own alpha state ignores the game's blend and alpha, so it dissolves by the game's fade,
    // and keeps its own blending if it authors any.
    if (!opaqueMaterial.getUseLegacyAlphaState()) {
      return opaqueMaterial.getBlendEnabled() ? AlphaBlendDissolve::None : AlphaBlendDissolve::Fade;
    }

    return legacyMaterial.textureFactorFade == TextureFactorFade::ModulateTexture
      ? AlphaBlendDissolve::TextureAlphaTimesTextureFactor
      : AlphaBlendDissolve::TextureFactor;
  }
}
