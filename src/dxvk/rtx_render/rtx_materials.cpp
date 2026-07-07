/*
* Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.
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

#include "rtx_materials.h"

#include "rtx_options.h"
#include "../util/util_struct_hash.h"

namespace dxvk {

XXH64_hash_t LegacyMaterialData::computeIdentityHash() const {
  // Only fields consumed by determineMaterialData(), mergeLegacyMaterial(), and
  // InstanceManager surface alpha/blend setup. D3DMATERIAL9 constants, color-source
  // intermediates, texture slots, and alphaTestEnabled are excluded.
  struct LegacyMaterialIdentityHashData {
    XXH64_hash_t colorTextureHash0;
    XXH64_hash_t colorTextureHash1;
    XXH64_hash_t samplerHash0;
    XXH64_hash_t samplerHash1;
    uint32_t alphaTestCompareOp;
    uint32_t tFactor;
    uint32_t blendEnableBlending;
    uint32_t blendColorSrcFactor;
    uint32_t blendColorDstFactor;
    uint32_t blendColorBlendOp;
    uint32_t blendAlphaSrcFactor;
    uint32_t blendAlphaDstFactor;
    uint32_t blendAlphaBlendOp;
    uint32_t blendWriteMask;
    uint8_t alphaTestReferenceValue;
    uint8_t textureColorArg1Source;
    uint8_t textureColorArg2Source;
    uint8_t textureColorOperation;
    uint8_t textureAlphaArg1Source;
    uint8_t textureAlphaArg2Source;
    uint8_t textureAlphaOperation;
    uint8_t isTextureFactorBlend;
    uint8_t isVertexColorBakedLighting;
    uint8_t padding[7];
  };

  LegacyMaterialIdentityHashData data{};
  data.colorTextureHash0 = colorTextures[0].getImageHash();
  data.colorTextureHash1 = colorTextures[1].getImageHash();
  data.samplerHash0 = samplers[0].ptr() != nullptr ? samplers[0]->info().calculateHash() : kEmptyHash;
  data.samplerHash1 = samplers[1].ptr() != nullptr ? samplers[1]->info().calculateHash() : kEmptyHash;
  data.alphaTestCompareOp = static_cast<uint32_t>(alphaTestCompareOp);
  data.tFactor = tFactor;
  data.blendEnableBlending = static_cast<uint32_t>(blendMode.enableBlending);
  data.blendColorSrcFactor = static_cast<uint32_t>(blendMode.colorSrcFactor);
  data.blendColorDstFactor = static_cast<uint32_t>(blendMode.colorDstFactor);
  data.blendColorBlendOp = static_cast<uint32_t>(blendMode.colorBlendOp);
  data.blendAlphaSrcFactor = static_cast<uint32_t>(blendMode.alphaSrcFactor);
  data.blendAlphaDstFactor = static_cast<uint32_t>(blendMode.alphaDstFactor);
  data.blendAlphaBlendOp = static_cast<uint32_t>(blendMode.alphaBlendOp);
  data.blendWriteMask = static_cast<uint32_t>(blendMode.writeMask);
  data.alphaTestReferenceValue = alphaTestReferenceValue;
  data.textureColorArg1Source = static_cast<uint8_t>(textureColorArg1Source);
  data.textureColorArg2Source = static_cast<uint8_t>(textureColorArg2Source);
  data.textureColorOperation = static_cast<uint8_t>(textureColorOperation);
  data.textureAlphaArg1Source = static_cast<uint8_t>(textureAlphaArg1Source);
  data.textureAlphaArg2Source = static_cast<uint8_t>(textureAlphaArg2Source);
  data.textureAlphaOperation = static_cast<uint8_t>(textureAlphaOperation);
  data.isTextureFactorBlend = isTextureFactorBlend ? 1u : 0u;
  data.isVertexColorBakedLighting = isVertexColorBakedLighting ? 1u : 0u;

  return hashStructByMemory<LegacyMaterialIdentityHashData,
      &LegacyMaterialIdentityHashData::colorTextureHash0,
      &LegacyMaterialIdentityHashData::colorTextureHash1,
      &LegacyMaterialIdentityHashData::samplerHash0,
      &LegacyMaterialIdentityHashData::samplerHash1,
      &LegacyMaterialIdentityHashData::alphaTestCompareOp,
      &LegacyMaterialIdentityHashData::tFactor,
      &LegacyMaterialIdentityHashData::blendEnableBlending,
      &LegacyMaterialIdentityHashData::blendColorSrcFactor,
      &LegacyMaterialIdentityHashData::blendColorDstFactor,
      &LegacyMaterialIdentityHashData::blendColorBlendOp,
      &LegacyMaterialIdentityHashData::blendAlphaSrcFactor,
      &LegacyMaterialIdentityHashData::blendAlphaDstFactor,
      &LegacyMaterialIdentityHashData::blendAlphaBlendOp,
      &LegacyMaterialIdentityHashData::blendWriteMask,
      &LegacyMaterialIdentityHashData::alphaTestReferenceValue,
      &LegacyMaterialIdentityHashData::textureColorArg1Source,
      &LegacyMaterialIdentityHashData::textureColorArg2Source,
      &LegacyMaterialIdentityHashData::textureColorOperation,
      &LegacyMaterialIdentityHashData::textureAlphaArg1Source,
      &LegacyMaterialIdentityHashData::textureAlphaArg2Source,
      &LegacyMaterialIdentityHashData::textureAlphaOperation,
      &LegacyMaterialIdentityHashData::isTextureFactorBlend,
      &LegacyMaterialIdentityHashData::isVertexColorBakedLighting,
      &LegacyMaterialIdentityHashData::padding>(data);
}

bool getEnableDiffuseLayerOverrideHack() {
  return TranslucentMaterialOptions::enableDiffuseLayerOverride();
}

float getEmissiveIntensity() {
  return RtxOptions::emissiveIntensity();
}

float getDisplacementFactor() {
  return RtxOptions::Displacement::displacementFactor();
}

float getDisplacementInFactor() {
  return RtxOptions::Displacement::displacementFactor() * RtxOptions::Displacement::displacementInFactor();
}

float getDisplacementOutFactor() {
  return RtxOptions::Displacement::displacementFactor() * RtxOptions::Displacement::displacementOutFactor();
}

dxvk::OpaqueMaterialData LegacyMaterialData::createDefault() {
  OpaqueMaterialData opaqueMat;
  opaqueMat.setAnisotropyConstant(LegacyMaterialDefaults::anisotropy());
  opaqueMat.setEmissiveIntensity(LegacyMaterialDefaults::emissiveIntensity());
  opaqueMat.setAlbedoConstant(LegacyMaterialDefaults::albedoConstant());
  opaqueMat.setOpacityConstant(LegacyMaterialDefaults::opacityConstant());
  opaqueMat.setRoughnessConstant(LegacyMaterialDefaults::roughnessConstant());
  opaqueMat.setMetallicConstant(LegacyMaterialDefaults::metallicConstant());
  opaqueMat.setEmissiveColorConstant(LegacyMaterialDefaults::emissiveColorConstant());
  opaqueMat.setEnableEmission(LegacyMaterialDefaults::enableEmissive());
  opaqueMat.setEnableThinFilm(LegacyMaterialDefaults::enableThinFilm());
  opaqueMat.setAlphaIsThinFilmThickness(LegacyMaterialDefaults::alphaIsThinFilmThickness());
  opaqueMat.setThinFilmThicknessConstant(LegacyMaterialDefaults::thinFilmThicknessConstant());
  return opaqueMat;
}

template<> OpaqueMaterialData LegacyMaterialData::as() const {
  // Legacy materials have parameters that can directly carry over onto the opaque material.
  const OpaqueMaterialData defaultLegacyOpaqueMaterial = createDefault();
  // Copy off the defaults, and make dynamic adjustments for the remaining params from this legacy material
  OpaqueMaterialData opaqueMat(defaultLegacyOpaqueMaterial);
  if (LegacyMaterialDefaults::useAlbedoTextureIfPresent()) {
    opaqueMat.setAlbedoOpacityTexture(getColorTexture());
  }
  if (getColorTexture2().isValid()) {
    opaqueMat.setSecondaryTexture(getColorTexture2());
  }
  // Indicate that we have an exact sampler to use on this material, directly from game
  if (getSampler().ptr()) {
    opaqueMat.setSamplerOverride(getSampler());
  }
  // Ignore colormap alpha of legacy texture if tagged as 'ignoreAlphaOnTextures' 
  bool ignoreAlphaChannel = LegacyMaterialDefaults::ignoreAlphaChannel();
  if (!ignoreAlphaChannel) {
    ignoreAlphaChannel = lookupHash(RtxOptions::ignoreAlphaOnTextures(), getHash());
  }
  opaqueMat.setIgnoreAlphaChannel(ignoreAlphaChannel);
  return opaqueMat;
}

template<> TranslucentMaterialData LegacyMaterialData::as() const {
  TranslucentMaterialData transluscentMat;
  if (getSampler().ptr()) {
    transluscentMat.setSamplerOverride(getSampler());
  }
  return transluscentMat;
}

template<> RayPortalMaterialData LegacyMaterialData::as() const {
  RayPortalMaterialData portalMat;
  portalMat.getMaskTexture() = getColorTexture();
  portalMat.getMaskTexture2() = getColorTexture2();
  portalMat.setEnableEmission(true);
  portalMat.setEmissiveIntensity(1.f);
  portalMat.setSpriteSheetCols(1);
  portalMat.setSpriteSheetRows(1);
  if (getSampler().ptr()) {
    portalMat.setSamplerOverride(getSampler());
  }
  return portalMat;
}


} // namespace dxvk
