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

#include "rtx_texture.h"
#include "rtx_option.h"
#include "../../util/util_color.h"
#include "../../util/util_macro.h"
#include "../shaders/rtx/utility/shared_constants.h"
#include "../shaders/rtx/concept/surface/surface_shared.h"

namespace dxvk {
// Surfaces

// Todo: Compute size directly from sizeof of GPU structure (by including it), for now computed by sum of members manually
constexpr std::size_t kSurfaceGPUSize = 16 * 4 * 4;
// Note: 0xFFFF used for inactive buffer and surface material index to indicate to the GPU that no buffer/material is in use
// for a specific variable (as some are optional). Also used for debugging to provide wildly out of range values in case one is not set.
constexpr static uint32_t kSurfaceInvalidBufferIndex = 0xFFFFu;
constexpr static uint32_t kSurfaceInvalidSurfaceMaterialIndex = 0xFFFFu;

// Note: Use caution when changing this enum, must match the values defined on the MDL side of things.

static bool isBlendTypeEmissive(const BlendType type) {
  switch (type) {
  default:
    return false;
  case BlendType::kAlphaEmissive:
  case BlendType::kReverseAlphaEmissive:
  case BlendType::kColorEmissive:
  case BlendType::kReverseColorEmissive:
  case BlendType::kEmissive:
    return true;
  }
}

static BlendType tryConvertToEmissive(const BlendType type) {
  switch (type) {
  case BlendType::kAlpha:
    return BlendType::kAlphaEmissive;
  case BlendType::kColor:
    return BlendType::kColorEmissive;
  default:
    return type;
  }
}

static_assert((int)AlphaTestType::kNever == (int)VkCompareOp::VK_COMPARE_OP_NEVER);
static_assert((int)AlphaTestType::kLess == (int)VkCompareOp::VK_COMPARE_OP_LESS);
static_assert((int)AlphaTestType::kEqual == (int)VkCompareOp::VK_COMPARE_OP_EQUAL);
static_assert((int)AlphaTestType::kLessOrEqual == (int)VkCompareOp::VK_COMPARE_OP_LESS_OR_EQUAL);
static_assert((int)AlphaTestType::kGreater == (int)VkCompareOp::VK_COMPARE_OP_GREATER);
static_assert((int)AlphaTestType::kNotEqual == (int)VkCompareOp::VK_COMPARE_OP_NOT_EQUAL);
static_assert((int)AlphaTestType::kGreaterOrEqual == (int)VkCompareOp::VK_COMPARE_OP_GREATER_OR_EQUAL);
static_assert((int)AlphaTestType::kAlways == (int)VkCompareOp::VK_COMPARE_OP_ALWAYS);

// Note: "Temporary" hacks to get RtxOptions data from this header file as we cannot include rtx_options directly
// due to cyclic includes. This should be removed once the rtx_materials implementation is moved to a source file.
bool getEnableDiffuseLayerOverrideHack();
float getEmissiveIntensity();

struct RtSurface {
  RtSurface() {
  }

  void writeGPUData(unsigned char* data, std::size_t& offset) const {
    [[maybe_unused]] const std::size_t oldOffset = offset;

    // Note: Position buffer and surface material index are required for proper
    // behavior of the Surface on the GPU.
    assert(positionBufferIndex != kSurfaceInvalidBufferIndex);
    assert(surfaceMaterialIndex != kSurfaceInvalidSurfaceMaterialIndex);

    writeGPUHelperExplicit<2>(data, offset, positionBufferIndex);
    writeGPUHelperExplicit<2>(data, offset, previousPositionBufferIndex);
    writeGPUHelperExplicit<2>(data, offset, normalBufferIndex);
    writeGPUHelperExplicit<2>(data, offset, texcoordBufferIndex);
    writeGPUHelperExplicit<2>(data, offset, indexBufferIndex);
    writeGPUHelperExplicit<2>(data, offset, color0BufferIndex);
    writeGPUHelperExplicit<2>(data, offset, surfaceMaterialIndex);

    const uint16_t packedHash = (uint16_t) (associatedGeometryHash >> 48) ^ (uint16_t) (associatedGeometryHash >> 32) ^ (uint16_t) (associatedGeometryHash >> 16) ^ (uint16_t) associatedGeometryHash;
    writeGPUHelper(data, offset, packedHash);

    writeGPUHelper(data, offset, positionOffset);
    writeGPUPadding<4>(data, offset);
    writeGPUHelper(data, offset, normalOffset);
    writeGPUHelper(data, offset, texcoordOffset);
    writeGPUHelper(data, offset, color0Offset);

    writeGPUHelperExplicit<1>(data, offset, positionStride);
    writeGPUHelperExplicit<1>(data, offset, normalStride);
    writeGPUHelperExplicit<1>(data, offset, texcoordStride);
    writeGPUHelperExplicit<1>(data, offset, color0Stride);

    writeGPUHelperExplicit<3>(data, offset, firstIndex);
    writeGPUHelperExplicit<1>(data, offset, indexStride);

    uint32_t flags = 0;

    flags |= isEmissive ? (1 << 0) : 0;
    flags |= alphaState.isFullyOpaque ? (1 << 1) : 0;
    flags |= isStatic ? (1 << 2) : 0;
    flags |= static_cast<uint32_t>(alphaState.alphaTestType) << 3;
    // Note: No mask needed as masking of this value to be 8 bit is done elsewhere.
    flags |= static_cast<uint32_t>(alphaState.alphaTestReferenceValue) << 6;
    flags |= static_cast<uint32_t>(alphaState.blendType) << 14;
    flags |= alphaState.invertedBlend ?      (1 << 18) : 0;
    flags |= alphaState.isBlendingDisabled ? (1 << 19) : 0;
    flags |= alphaState.emissiveBlend ?      (1 << 20) : 0;
    flags |= alphaState.isParticle ?         (1 << 21) : 0;
    flags |= alphaState.isDecal ?            (1 << 22) : 0;
    flags |= alphaState.isBlendedTerrain ?   (1 << 23) : 0;
    flags |= isAnimatedWater ?               (1 << 24) : 0;
    flags |= isClipPlaneEnabled ?            (1 << 25) : 0;
    flags |= isMatte ?                       (1 << 26) : 0;
    flags |= isTextureFactorBlend ?          (1 << 27) : 0;

    writeGPUHelper(data, offset, flags);

    // Note: Matricies are stored on the cpu side in column-major order, the same as the GPU.

    // Note: Last row of object to world matrix not needed as it does not encode any useful information
    writeGPUHelper(data, offset, prevObjectToWorld.data[0].x);
    writeGPUHelper(data, offset, prevObjectToWorld.data[0].y);
    writeGPUHelper(data, offset, prevObjectToWorld.data[0].z);
    writeGPUHelper(data, offset, prevObjectToWorld.data[1].x);
    writeGPUHelper(data, offset, prevObjectToWorld.data[1].y);
    writeGPUHelper(data, offset, prevObjectToWorld.data[1].z);
    writeGPUHelper(data, offset, prevObjectToWorld.data[2].x);
    writeGPUHelper(data, offset, prevObjectToWorld.data[2].y);
    writeGPUHelper(data, offset, prevObjectToWorld.data[2].z);
    writeGPUHelper(data, offset, prevObjectToWorld.data[3].x);
    writeGPUHelper(data, offset, prevObjectToWorld.data[3].y);
    writeGPUHelper(data, offset, prevObjectToWorld.data[3].z);

    writeGPUHelper(data, offset, normalObjectToWorld.data[0]);
    writeGPUHelper(data, offset, normalObjectToWorld.data[1]);
    writeGPUHelper(data, offset, normalObjectToWorld.data[2].x);
    writeGPUHelper(data, offset, normalObjectToWorld.data[2].y);

    writeGPUHelper(data, offset, objectToWorld.data[0].x);
    writeGPUHelper(data, offset, objectToWorld.data[0].y);
    writeGPUHelper(data, offset, objectToWorld.data[0].z);
    writeGPUHelper(data, offset, objectToWorld.data[1].x);
    writeGPUHelper(data, offset, objectToWorld.data[1].y);
    writeGPUHelper(data, offset, objectToWorld.data[1].z);
    writeGPUHelper(data, offset, objectToWorld.data[2].x);
    writeGPUHelper(data, offset, objectToWorld.data[2].y);
    writeGPUHelper(data, offset, objectToWorld.data[2].z);
    writeGPUHelper(data, offset, objectToWorld.data[3].x);
    writeGPUHelper(data, offset, objectToWorld.data[3].y);
    writeGPUHelper(data, offset, objectToWorld.data[3].z);

    writeGPUHelper(data, offset, textureTransform.data[0].x);
    writeGPUHelper(data, offset, textureTransform.data[0].y);
    writeGPUHelper(data, offset, textureTransform.data[0].z);
    writeGPUHelper(data, offset, textureTransform.data[1].x);
    writeGPUHelper(data, offset, textureTransform.data[1].y);
    writeGPUHelper(data, offset, textureTransform.data[1].z);
    writeGPUHelper(data, offset, textureTransform.data[2].x);
    writeGPUHelper(data, offset, textureTransform.data[2].y);
    writeGPUHelper(data, offset, textureTransform.data[2].z);

    writeGPUHelper(data, offset, tFactor);

    uint32_t textureFlags = 0;
    textureFlags |= ((static_cast<uint32_t>(textureColorArg1Source) & 0x3));
    textureFlags |= ((static_cast<uint32_t>(textureColorArg2Source) & 0x3) << 2);
    textureFlags |= ((static_cast<uint32_t>(textureColorOperation)  & 0x7) << 4);
    textureFlags |= ((static_cast<uint32_t>(textureAlphaArg1Source) & 0x3) << 7);
    textureFlags |= ((static_cast<uint32_t>(textureAlphaArg2Source) & 0x3) << 9);
    textureFlags |= ((static_cast<uint32_t>(textureAlphaOperation)  & 0x7) << 11);

    static_assert(static_cast<uint32_t>(TexGenMode::Count) <= 4);
    textureFlags |= ((static_cast<uint32_t>(texgenMode) & 0x3) << 17);

    writeGPUHelper(data, offset, textureFlags);

    // Note: This element of the normal object to world matrix is encoded to minimize padding
    writeGPUHelper(data, offset, normalObjectToWorld.data[2].z);

    writeGPUHelper(data, offset, clipPlane);

    writeGPUHelper(data, offset, textureTransform.data[3].x);
    writeGPUHelper(data, offset, textureTransform.data[3].y);
    writeGPUHelper(data, offset, textureTransform.data[3].z);
    writeGPUHelper(data, offset, textureTransform.data[3].w);

    assert(offset - oldOffset == kSurfaceGPUSize);
  }

  uint32_t positionBufferIndex = kSurfaceInvalidBufferIndex;
  uint32_t previousPositionBufferIndex = kSurfaceInvalidBufferIndex;
  uint32_t positionOffset = 0;
  uint32_t positionStride = 0;

  uint32_t normalBufferIndex = kSurfaceInvalidBufferIndex;
  uint32_t normalOffset = 0;
  uint32_t normalStride = 0;

  uint32_t texcoordBufferIndex = kSurfaceInvalidBufferIndex;
  uint32_t texcoordOffset = 0;
  uint32_t texcoordStride = 0;

  uint32_t indexBufferIndex = kSurfaceInvalidBufferIndex;
  uint32_t firstIndex = 0;
  uint32_t indexStride = 0;

  uint32_t color0BufferIndex = kSurfaceInvalidBufferIndex;
  uint32_t color0Offset = 0;
  uint32_t color0Stride = 0;

  uint32_t surfaceMaterialIndex = kSurfaceInvalidSurfaceMaterialIndex;

  bool isEmissive = false;
  bool isMatte = false;
  bool isStatic = false;
  bool isAnimatedWater = false;
  bool isClipPlaneEnabled = false;
  bool isTextureFactorBlend = false;

  RtTextureArgSource textureColorArg1Source = RtTextureArgSource::Texture;
  RtTextureArgSource textureColorArg2Source = RtTextureArgSource::None;
  DxvkRtTextureOperation textureColorOperation = DxvkRtTextureOperation::Modulate;
  RtTextureArgSource textureAlphaArg1Source = RtTextureArgSource::Texture;
  RtTextureArgSource textureAlphaArg2Source = RtTextureArgSource::None;
  DxvkRtTextureOperation textureAlphaOperation = DxvkRtTextureOperation::SelectArg1;
  uint32_t tFactor = 0xffffffff;   // Value for D3DRS_TEXTUREFACTOR, default value of is opaque white
  TexGenMode texgenMode = TexGenMode::None;

  bool doBuffersMatch(const RtSurface& surface) {
    return positionBufferIndex == surface.positionBufferIndex
        && positionOffset == surface.positionOffset
        && previousPositionBufferIndex == surface.previousPositionBufferIndex
        && normalBufferIndex == surface.normalBufferIndex
        && normalOffset == surface.normalOffset
        && texcoordBufferIndex == surface.texcoordBufferIndex
        && texcoordOffset == surface.texcoordOffset
        && color0BufferIndex == surface.color0BufferIndex
        && color0Offset == surface.color0Offset
        && firstIndex == surface.firstIndex;
  }

  // Used for calculating hashes, keep the members padded and default initialized
  struct AlphaState {
    bool isBlendingDisabled = true;
    bool isFullyOpaque = false;
    AlphaTestType alphaTestType = AlphaTestType::kAlways;
    uint8_t alphaTestReferenceValue = 0;

    BlendType blendType = BlendType::kAlpha;
    bool invertedBlend = false;
    bool emissiveBlend = false;
    bool isParticle = false;
    bool isDecal = false;
    bool isBlendedTerrain = false;
  } alphaState;

  // Static validation to detect any changes that require an alignment re-check
  static_assert(sizeof(AlphaState) == 10);

  Matrix4 objectToWorld;
  Matrix4 prevObjectToWorld;
  Matrix3 normalObjectToWorld;
  Matrix4 textureTransform;
  Vector4 clipPlane;

  XXH64_hash_t associatedGeometryHash; // NOTE: This is used for the debug view
};

// Shared Material Defaults/Limits

struct SharedMaterialDefaults {
  uint8_t SpriteSheetRows = 1;
  uint8_t SpriteSheetCols = 1;
  uint8_t SpriteSheetFPS = 0;
  float EmissiveIntensity = 40.0;
  bool EnableEmissive = false;
};

struct LegacyMaterialDefaults {
  friend class ImGUI;
  RTX_OPTION("rtx.legacyMaterial", float, anisotropy, 0.f, "The default roughness anisotropy to use for non-replaced \"legacy\" materials. Should be in the range -1 to 1, where 0 is isotropic.");
  RTX_OPTION("rtx.legacyMaterial", float, emissiveIntensity, 0.f, "The default emissive intensity to use for non-replaced \"legacy\" materials.");
  RTX_OPTION("rtx.legacyMaterial", bool, useAlbedoTextureIfPresent, true, "A flag to determine if an \"albedo\" texture (a qualifying color texture) from the original application should be used if present on non-replaced \"legacy\" materials.");
  RTX_OPTION("rtx.legacyMaterial", Vector3, albedoConstant, Vector3(1.0f, 1.0f, 1.0f), "The default albedo constant to use for non-replaced \"legacy\" materials. Should be a color in sRGB colorspace with gamma encoding.");
  RTX_OPTION("rtx.legacyMaterial", float, opacityConstant, 1.f, "The default opacity constant to use for non-replaced \"legacy\" materials. Should be in the range 0 to 1.");
  RTX_OPTION_ENV("rtx.legacyMaterial", float, roughnessConstant, 0.7f, "DXVK_LEGACY_MATERIAL_DEFAULT_ROUGHNESS", "The default perceptual roughness constant to use for non-replaced \"legacy\" materials. Should be in the range 0 to 1.");
  RTX_OPTION("rtx.legacyMaterial", float, metallicConstant, 0.1f, "The default metallic constant to use for non-replaced \"legacy\" materials. Should be in the range 0 to 1.");
  RTX_OPTION("rtx.legacyMaterial", Vector3, emissiveColorConstant, Vector3(0.0f, 0.0f, 0.0f), "The default emissive color constant to use for non-replaced \"legacy\" materials. Should be a color in sRGB colorspace with gamma encoding.");
  RTX_OPTION("rtx.legacyMaterial", bool, enableEmissive, false, "A flag to determine if emission should be used on non-replaced \"legacy\" materials.");
  RTX_OPTION("rtx.legacyMaterial", bool, enableThinFilm, false, "A flag to determine if a thin-film layer should be used on non-replaced \"legacy\" materials.");
  RTX_OPTION("rtx.legacyMaterial", bool, alphaIsThinFilmThickness, false, "A flag to determine if the alpha channel from the albedo source should be treated as thin film thickness on non-replaced \"legacy\" materials.");
  // Note: Should be something non-zero as 0 is an invalid thickness to have (even if this is just unused).
  RTX_OPTION("rtx.legacyMaterial", float, thinFilmThicknessConstant, 200.f,
             "The thickness (in nanometers) of the thin-film layer assuming it is enabled on non-replaced \"legacy\" materials.\n"
             "Should be any value larger than 0, typically within the wavelength of light, but must be less than or equal to OPAQUE_SURFACE_MATERIAL_THIN_FILM_MAX_THICKNESS (" STRINGIFY(OPAQUE_SURFACE_MATERIAL_THIN_FILM_MAX_THICKNESS) " nm).");
};

// Note: These material defaults should be kept in sync with the MDL defaults just in case data is not written out by the MDL
// to ensure similarity between rendering with MDL in other programs vs in this runtime.

struct OpaqueMaterialDefaults {
  float Anisotropy = 0.0f;
  Vector4 AlbedoOpacityConstant{ 0.2f, 0.2f, 0.2f, 1.0f };
  float RoughnessConstant = 0.5f;
  float MetallicConstant = 0.0f;
  Vector3 EmissiveColorConstant{ 1.0f, 0.1f, 0.1f };
  bool EnableThinFilm = false;
  bool AlphaIsThinFilmThickness = false;
  // Note: Should be something non-zero as 0 is an invalid thickness to have.
  float ThinFilmThicknessConstant = 200.0f;
  bool UseLegacyAlphaState = true;
  bool BlendEnabled = false;
  // Note: Default prefix used to not match BlendType symbol name.
  BlendType DefaultBlendType = BlendType::kAlpha;
  bool InvertedBlend = false;
  // Note: Default prefix used to not match AlphaTestType symbol name.
  AlphaTestType DefaultAlphaTestType = AlphaTestType::kAlways;
  uint8_t AlphaReferenceValue = 0;
};

struct TranslucentMaterialDefaults {
  // Note: Typical fair value for some kinds of glass, not too refractive but not enough to not be noticable.
  float RefractiveIndex = 1.3f;
  // Note: Slight default attenuation as no physical translucent materials are perfectly transparent.
  Vector3 TransmittanceColor{ 0.97f, 0.97f, 0.97f };
  float TransmittanceMeasurementDistance = 1.f;
  Vector3 EmissiveColorConstant{ 1.0f, 0.1f, 0.1f };
  bool ThinWalled = false;
  float ThinWallThickness = 0.0;
  bool UseDiffuseLayer = false;
};

struct RayPortalMaterialDefaults {
  uint8_t RayPortalIndex = 0;
  float RotationSpeed = 0.f;
};

// Note: These material ranges should be kept in sync with the MDL ranges to prevent mismatching between
// how data is clamped.

struct OpaqueMaterialMins {
  float AlbedoConstant = 0.0f;
  float OpacityConstant = 0.0f;
  float RoughnessConstant = 0.0f;
  float AnisotropyConstant = -1.0f;
  float MetallicConstant = 0.0f;
  float EmissiveColorConstant = 0.0f;
  float EmissiveIntensity = 0.0f;
  // Note: Thickness cannot be 0 so should be kept above this minimum small value (though in practice it'll likely be
  // quantized to 0 with values this small anyways, but it's good to be careful about it for potential future changes).
  float ThinFilmThicknessConstant = 0.001f;
  BlendType MinBlendType = BlendType::kMinValue;
  AlphaTestType MinAlphaTestType = AlphaTestType::kMinValue;
  uint8_t AlphaReferenceValue = 0;
};

struct OpaqueMaterialMaxes {
  float AlbedoConstant = 1.0f;
  float OpacityConstant = 1.0f;
  float RoughnessConstant = 1.0f;
  float AnisotropyConstant = 1.0f;
  float MetallicConstant = 1.0f;
  float EmissiveColorConstant = 1.0f;
  // Note: Maximum clamped to float 16 max due to GPU encoding requirements.
  float EmissiveIntensity = 65504.0f;
  // Note: Max thickness constant be less than the float 16 max due to float 16 usage on the GPU.
  float ThinFilmThicknessConstant = OPAQUE_SURFACE_MATERIAL_THIN_FILM_MAX_THICKNESS;
  BlendType MaxBlendType = BlendType::kMaxValue;
  AlphaTestType MaxAlphaTestType = AlphaTestType::kMaxValue;
  uint8_t AlphaReferenceValue = std::numeric_limits<uint8_t>::max();
};

struct TranslucentMaterialMins {
  // Note: IoR values less than 1 are physically impossible for typical translucent materials.
  float RefractiveIndex = 1.0f;
  // Note: 0.001 to be safe around the minimum of float16 values, as well as due to the fact that we cut off
  // 2 bits of the value in some cases.
  float ThinWallThickness = 0.001f;
  float TransmittanceColor = 0.0f;
  // Note: 0.001 to be safe around the minimum of float16 values, as well as due to the fact that we cut off
  // 2 bits of the value in some cases.
  float TransmittanceMeasurementDistance = 0.001f;
  float EmissiveColorConstant = 0.0f;
  float EmissiveIntensity = 0.0f;
};

struct TranslucentMaterialMaxes {
  // Note: 3 chosen due to virtually no physical materials having an IoR greater to this, and because this
  // is currently the maximum IoR value the GPU supports encoding of as well.
  float RefractiveIndex = 3.0f;
  // Note: Maximum clamped to float16 max due to encoding limitations.
  float ThinWallThickness = 65504.0f;
  float TransmittanceColor = 1.0f;
  // Note: Maximum clamped to float16 max due to encoding limitations.
  float TransmittanceMeasurementDistance = 65504.0f;
  float EmissiveColorConstant = 1.0f;
  // Note: Maximum clamped to float 16 in case in the future we decide to encode Translucent material information
  // with a float16 intensity plus a unorm8x3 color rather than a float16x3 radiance (better to anticipate the future
  // rather than break assets later).
  float EmissiveIntensity = 65504.0f;
};

// Surface Materials

// Todo: Compute size directly from sizeof of GPU structure (by including it), for now computed by sum of members manually
constexpr std::size_t kSurfaceMaterialGPUSize = 2 * 4 * 4;
// Note: 0xFFFF used for inactive texture index to indicate to the GPU that no texture is in use for a specific variable
// (as some are optional). Also used for debugging to provide wildly out of range values in case one is not set.
constexpr uint32_t kSurfaceMaterialInvalidTextureIndex = 0xFFFFu;
// Note: These defaults are used in places where no value is available for the constructor of various Surface Materials, just to
// keep things consistent across the codebase.

enum class RtSurfaceMaterialType {
  // Todo: Legacy SurfaceMaterialType in the future
  Opaque = 0,
  Translucent,
  RayPortal,

  Count
};

// Todo: Legacy SurfaceMaterial in the future

struct RtOpaqueSurfaceMaterial {
  RtOpaqueSurfaceMaterial(
    uint32_t albedoOpacityTextureIndex, uint32_t normalTextureIndex,
    uint32_t tangentTextureIndex, uint32_t roughnessTextureIndex,
    uint32_t metallicTextureIndex, uint32_t emissiveColorTextureIndex,
    float anisotropy, float emissiveIntensity,
    const Vector4& albedoOpacityConstant,
    float roughnessConstant, float metallicConstant,
    const Vector3& emissiveColorConstant, bool enableEmission,
    uint8_t spriteSheetRows, uint8_t spriteSheetCols, uint8_t spriteSheetFPS,
    bool enableThinFilm, bool alphaIsThinFilmThickness, float thinFilmThicknessConstant) :
    m_albedoOpacityTextureIndex{ albedoOpacityTextureIndex }, m_normalTextureIndex{ normalTextureIndex },
    m_tangentTextureIndex{ tangentTextureIndex }, m_roughnessTextureIndex{ roughnessTextureIndex },
    m_metallicTextureIndex{ metallicTextureIndex }, m_emissiveColorTextureIndex{ emissiveColorTextureIndex },
    m_anisotropy{ anisotropy }, m_emissiveIntensity{ emissiveIntensity },
    m_albedoOpacityConstant{ albedoOpacityConstant },
    m_roughnessConstant{ roughnessConstant }, m_metallicConstant{ metallicConstant },
    m_emissiveColorConstant{ emissiveColorConstant }, m_enableEmission{ enableEmission },
    m_spriteSheetRows { spriteSheetRows },
    m_spriteSheetCols { spriteSheetCols },
    m_spriteSheetFPS { spriteSheetFPS },
    m_enableThinFilm { enableThinFilm }, m_alphaIsThinFilmThickness { alphaIsThinFilmThickness }, m_thinFilmThicknessConstant { thinFilmThicknessConstant } {
    updateCachedData();
    updateCachedHash();
  }

  void writeGPUData(unsigned char* data, std::size_t& offset) const {
    [[maybe_unused]] const std::size_t oldOffset = offset;

    // 12 Bytes
    writeGPUHelperExplicit<2>(data, offset, m_albedoOpacityTextureIndex);
    writeGPUHelperExplicit<2>(data, offset, m_normalTextureIndex);
    writeGPUHelperExplicit<2>(data, offset, m_tangentTextureIndex);
    writeGPUHelperExplicit<2>(data, offset, m_roughnessTextureIndex);
    writeGPUHelperExplicit<2>(data, offset, m_metallicTextureIndex);
    writeGPUHelperExplicit<2>(data, offset, m_emissiveColorTextureIndex);

    // 1 Byte
    writeGPUHelper(data, offset, packSnorm<8, uint8_t>(m_anisotropy));
    
    // 3 Bytes
    // Note: Re-ordered to here to re-align singular anisotropy value shifting everything by 8 bits
    writeGPUHelper(data, offset, packUnorm<8, uint8_t>(m_emissiveColorConstant.x));
    writeGPUHelper(data, offset, packUnorm<8, uint8_t>(m_emissiveColorConstant.y));
    writeGPUHelper(data, offset, packUnorm<8, uint8_t>(m_emissiveColorConstant.z));

    assert(m_cachedEmissiveIntensity <= FLOAT16_MAX);
    writeGPUHelper(data, offset, glm::packHalf1x16(m_cachedEmissiveIntensity));

    writeGPUHelper(data, offset, packUnorm<8, uint8_t>(m_albedoOpacityConstant.x));
    writeGPUHelper(data, offset, packUnorm<8, uint8_t>(m_albedoOpacityConstant.y));

    writeGPUHelper(data, offset, packUnorm<8, uint8_t>(m_albedoOpacityConstant.z));
    writeGPUHelper(data, offset, packUnorm<8, uint8_t>(m_albedoOpacityConstant.w));

    writeGPUHelper(data, offset, packUnorm<8, uint8_t>(m_roughnessConstant));
    writeGPUHelper(data, offset, packUnorm<8, uint8_t>(m_metallicConstant));

    // 3 Bytes
    writeGPUHelper(data, offset, m_spriteSheetRows);
    writeGPUHelper(data, offset, m_spriteSheetCols);
    writeGPUHelper(data, offset, m_spriteSheetFPS);
    
    // 1 byte
    writeGPUHelper(data, offset, packUnorm<8, uint8_t>(m_cachedThinFilmNormalizedThicknessConstant));

    uint32_t flags = (0 << 30); // Note: Bit 30 and 31 of last word set to 0 for opaque material type

    if (m_enableThinFilm) {
      flags |= OPAQUE_SURFACE_MATERIAL_FLAG_USE_THIN_FILM_LAYER;

      // Note: Only consider setting alpha as thin film thickness flag if the thin film is enabled, GPU relies on
      // this logical ordering.
      if (m_alphaIsThinFilmThickness) {
        flags |= OPAQUE_SURFACE_MATERIAL_FLAG_ALPHA_IS_THIN_FILM_THICKNESS;
      }
    }

    writeGPUHelper(data, offset, flags);

    assert(offset - oldOffset == kSurfaceMaterialGPUSize);
  }

  bool operator==(const RtOpaqueSurfaceMaterial& r) const {
    return m_cachedHash == r.m_cachedHash;
  }

  XXH64_hash_t getHash() const {
    return m_cachedHash;
  }

  uint32_t getAlbedoOpacityTextureIndex() const {
    return m_albedoOpacityTextureIndex;
  }

  uint32_t getNormalTextureIndex() const {
    return m_normalTextureIndex;
  }

  uint32_t getTangentTextureIndex() const {
    return m_tangentTextureIndex;
  }

  uint32_t getRoughnessTextureIndex() const {
    return m_roughnessTextureIndex;
  }

  uint32_t getMetallicTextureIndex() const {
    return m_metallicTextureIndex;
  }

  uint32_t getEmissiveColorTextureIndex() const {
    return m_emissiveColorTextureIndex;
  }

  float getAnisotropy() const {
    return m_anisotropy;
  }

  float getEmissiveIntensity() const {
    return m_emissiveIntensity;
  }

  Vector4 getAlbedoOpacityConstant() const {
    return m_albedoOpacityConstant;
  }

  float getRoughnessConstant() const {
    return m_roughnessConstant;
  }

  float getMetallicConstant() const {
    return m_metallicConstant;
  }

  Vector3 getEmissiveColorConstant() const {
    return m_emissiveColorConstant;
  }

  bool getEnableEmission() const {
    return m_enableEmission;
  }

  uint8_t getSpriteSheetFPS() const {
    return m_spriteSheetFPS;
  }

private:
  void updateCachedHash() {
    XXH64_hash_t h = 0;

    h = XXH64(&m_albedoOpacityTextureIndex, sizeof(m_albedoOpacityTextureIndex), h);
    h = XXH64(&m_normalTextureIndex, sizeof(m_normalTextureIndex), h);
    h = XXH64(&m_tangentTextureIndex, sizeof(m_tangentTextureIndex), h);
    h = XXH64(&m_roughnessTextureIndex, sizeof(m_roughnessTextureIndex), h);
    h = XXH64(&m_metallicTextureIndex, sizeof(m_metallicTextureIndex), h);
    h = XXH64(&m_emissiveColorTextureIndex, sizeof(m_emissiveColorTextureIndex), h);
    h = XXH64(&m_anisotropy, sizeof(m_anisotropy), h);
    h = XXH64(&m_albedoOpacityConstant, sizeof(m_albedoOpacityConstant), h);
    h = XXH64(&m_roughnessConstant, sizeof(m_roughnessConstant), h);
    h = XXH64(&m_metallicConstant, sizeof(m_metallicConstant), h);
    h = XXH64(&m_emissiveColorConstant, sizeof(m_emissiveColorConstant), h);
    h = XXH64(&m_enableEmission, sizeof(m_enableEmission), h);
    h = XXH64(&m_spriteSheetRows, sizeof(m_spriteSheetRows), h);
    h = XXH64(&m_spriteSheetCols, sizeof(m_spriteSheetCols), h);
    h = XXH64(&m_spriteSheetFPS, sizeof(m_spriteSheetFPS), h);
    h = XXH64(&m_enableThinFilm, sizeof(m_enableThinFilm), h);
    h = XXH64(&m_alphaIsThinFilmThickness, sizeof(m_alphaIsThinFilmThickness), h);
    h = XXH64(&m_thinFilmThicknessConstant, sizeof(m_thinFilmThicknessConstant), h);

    m_cachedHash = h;
  }

  void updateCachedData() {
    // Note: Ensure the thin film thickness constant is within the expected range for normalization.
    assert(m_thinFilmThicknessConstant <= OPAQUE_SURFACE_MATERIAL_THIN_FILM_MAX_THICKNESS);

    // Note: Opaque material does not take an emissive radiance directly, so zeroing out the intensity works
    // fine as a way to disable it (in case a texture is in use).
    m_cachedEmissiveIntensity = std::min(m_enableEmission ? m_emissiveIntensity : 0.0f, FLOAT16_MAX);
    // Note: Pre-normalize thickness constant so that it does not need to be done on the GPU.
    m_cachedThinFilmNormalizedThicknessConstant = m_thinFilmThicknessConstant / OPAQUE_SURFACE_MATERIAL_THIN_FILM_MAX_THICKNESS;
  }

  uint32_t m_albedoOpacityTextureIndex;
  uint32_t m_normalTextureIndex;
  uint32_t m_tangentTextureIndex;
  uint32_t m_roughnessTextureIndex;
  uint32_t m_metallicTextureIndex;
  uint32_t m_emissiveColorTextureIndex;

  float m_anisotropy;
  float m_emissiveIntensity;

  Vector4 m_albedoOpacityConstant;
  float m_roughnessConstant;
  float m_metallicConstant;
  Vector3 m_emissiveColorConstant;

  bool m_enableEmission;

  uint8_t m_spriteSheetRows;
  uint8_t m_spriteSheetCols;
  uint8_t m_spriteSheetFPS;

  bool m_enableThinFilm;
  bool m_alphaIsThinFilmThickness;
  float m_thinFilmThicknessConstant;

  XXH64_hash_t m_cachedHash;

  // Note: Cached values are not involved in the hash as they are derived from the input data
  float m_cachedEmissiveIntensity;
  float m_cachedThinFilmNormalizedThicknessConstant;
};

struct RtTranslucentSurfaceMaterial {
  RtTranslucentSurfaceMaterial(
    uint32_t normalTextureIndex,
    float refractiveIndex, float transmittanceMeasurementDistance,
    uint32_t transmittanceTextureIndex, const Vector3& transmittanceColor,
    bool enableEmission, float emissiveIntensity, const Vector3& emissiveColorConstant,
    bool isThinWalled, float thinWallThickness, bool useDiffuseLayer) :
    m_normalTextureIndex(normalTextureIndex),
    m_refractiveIndex(refractiveIndex), m_transmittanceMeasurementDistance(transmittanceMeasurementDistance),
    m_transmittanceTextureIndex(transmittanceTextureIndex), m_transmittanceColor(transmittanceColor),
    m_enableEmission(enableEmission), m_emissiveIntensity(emissiveIntensity), m_emissiveColorConstant(emissiveColorConstant),
    m_isThinWalled(isThinWalled), m_thinWallThickness(thinWallThickness), m_useDiffuseLayer(useDiffuseLayer)
  {
    updateCachedData();
    updateCachedHash();
  }

  void writeGPUData(unsigned char* data, std::size_t& offset) const {
    [[maybe_unused]] const std::size_t oldOffset = offset;

    // For decode process, see translucent_surface_material.slangh
    // 4 Bytes
    writeGPUHelperExplicit<2>(data, offset, m_normalTextureIndex);                          // data00.x
    writeGPUHelper(data, offset, packUnorm<8, uint8_t>(m_cachedBaseReflectivity));          // data00.y & 0xff
    // Note: Ensure IoR falls in the range expected by the encoding/decoding logic for the GPU (this should also be
    // enforced in the MDL and relevant content pipeline to prevent this assert from being triggered).
    assert(m_refractiveIndex >= 1.0f && m_refractiveIndex <= 3.0f);
    writeGPUHelper(data, offset, packUnorm<8, uint8_t>((m_refractiveIndex - 1.0f) / 2.0f)); // data00.y & 0xff00

    // 6 Bytes
    writeGPUHelper(data, offset, glm::packHalf1x16(m_transmittanceColor.x)); // data01.x
    writeGPUHelper(data, offset, glm::packHalf1x16(m_transmittanceColor.y)); // data01.y
    writeGPUHelper(data, offset, glm::packHalf1x16(m_transmittanceColor.z)); // data02.x

    // 6 Bytes
    assert(m_cachedEmissiveRadiance.x <= FLOAT16_MAX);
    writeGPUHelper(data, offset, glm::packHalf1x16(m_cachedEmissiveRadiance.x));    // data02.y
    assert(m_cachedEmissiveRadiance.y <= FLOAT16_MAX);
    writeGPUHelper(data, offset, glm::packHalf1x16(m_cachedEmissiveRadiance.y));    // data03.x
    assert(m_cachedEmissiveRadiance.z <= FLOAT16_MAX);
    writeGPUHelper(data, offset, glm::packHalf1x16(m_cachedEmissiveRadiance.z));    // data03.y

    // 2 Bytes
    writeGPUHelper(data, offset, glm::packHalf1x16(m_cachedTransmittanceMeasurementDistanceOrThickness));        // data10.x

    // 2 Bytes
    writeGPUHelperExplicit<2>(data, offset, m_transmittanceTextureIndex);

    // 8 Bytes padding
    writeGPUPadding<8>(data, offset);

    uint32_t flags = (1 << 30); // bit 30 set to 1 for translucent material type

    // Note: Respect override flag here to let the GPU do less work in determining if the diffuse layer should be used or not.
    if (m_useDiffuseLayer || getEnableDiffuseLayerOverrideHack()) {
      flags |= TRANSLUCENT_SURFACE_MATERIAL_FLAG_USE_DIFFUSE_LAYER;
    }

    // 4 Bytes
    writeGPUHelper(data, offset, flags);

    assert(offset - oldOffset == kSurfaceMaterialGPUSize);
  }

  bool operator==(const RtTranslucentSurfaceMaterial& r) const {
    return m_cachedHash == r.m_cachedHash;
  }

  XXH64_hash_t getHash() const {
    return m_cachedHash;
  }
private:
  void updateCachedHash() {
    XXH64_hash_t h = 0;

    h = XXH64(&m_normalTextureIndex, sizeof(m_normalTextureIndex), h);
    h = XXH64(&m_refractiveIndex, sizeof(m_refractiveIndex), h);
    h = XXH64(&m_transmittanceTextureIndex, sizeof(m_transmittanceTextureIndex), h);
    h = XXH64(&m_transmittanceColor, sizeof(m_transmittanceColor), h);
    h = XXH64(&m_transmittanceMeasurementDistance, sizeof(m_transmittanceMeasurementDistance), h);
    h = XXH64(&m_enableEmission, sizeof(m_enableEmission), h);
    h = XXH64(&m_emissiveColorConstant, sizeof(m_emissiveColorConstant), h);
    h = XXH64(&m_isThinWalled, sizeof(m_isThinWalled), h);
    h = XXH64(&m_thinWallThickness, sizeof(m_thinWallThickness), h);
    h = XXH64(&m_useDiffuseLayer, sizeof(m_useDiffuseLayer), h);

    m_cachedHash = h;
  }

  void updateCachedData() {
    // Note: Based on the Fresnel Equations with the assumption of a vacuum (nearly air
    // as the surrounding medium always) and an IoR of always >=1 (implicitly ensured by encoding
    // logic assertions later):
    // https://en.wikipedia.org/wiki/Fresnel_equations#Special_cases
    const float x = (1.0f - m_refractiveIndex) / (1.0f + m_refractiveIndex);

    const float kSRGBGamma = 2.2f;
    // Note: Convert gamma to linear here similar to how we gamma correct the emissive color constant on the GPU for opaque materials
    // (since it cannot vary per-pixel unlike the opaque material).
    const auto linearEmissiveColor = sRGBGammaToLinear(m_emissiveColorConstant);

    m_cachedBaseReflectivity = x * x;
    m_cachedTransmittanceMeasurementDistanceOrThickness =
      m_isThinWalled ? -m_thinWallThickness : m_transmittanceMeasurementDistance;
    // Note: Global emissive intensity scalar from options applied here as in the opaque material it is applied on the GPU
    // side, but since we calculate the emissive radiance on the CPU for translucent materials it must be done here.
    m_cachedEmissiveRadiance = m_enableEmission ? getEmissiveIntensity() * m_emissiveIntensity * linearEmissiveColor : 0.0f;

    m_cachedEmissiveRadiance.x = std::min(m_cachedEmissiveRadiance.x, FLOAT16_MAX);
    m_cachedEmissiveRadiance.y = std::min(m_cachedEmissiveRadiance.y, FLOAT16_MAX);
    m_cachedEmissiveRadiance.z = std::min(m_cachedEmissiveRadiance.z, FLOAT16_MAX);

    // Note: Ensure the transmittance measurement distance or thickness was encoded properly by ensuring
    // it is not 0. This is because we currently do not actually check the sign bit but just use a less than
    // comparison to check the sign bit as neither of these values should be 0 in valid materials.
    assert(m_cachedTransmittanceMeasurementDistanceOrThickness != 0.0f);
  }

  uint32_t m_normalTextureIndex;
  float m_refractiveIndex;
  uint32_t m_transmittanceTextureIndex;
  Vector3 m_transmittanceColor;
  float m_transmittanceMeasurementDistance;
  bool m_enableEmission;
  float m_emissiveIntensity;
  Vector3 m_emissiveColorConstant;
  bool m_isThinWalled;
  float m_thinWallThickness;
  bool m_useDiffuseLayer;

  XXH64_hash_t m_cachedHash;

  // Note: Cached values are not involved in the hash as they are derived from the input data
  float m_cachedBaseReflectivity;
  float m_cachedTransmittanceMeasurementDistanceOrThickness;
  Vector3 m_cachedEmissiveRadiance;
};

struct RtRayPortalSurfaceMaterial {
  RtRayPortalSurfaceMaterial(uint32_t maskTextureIndex, uint32_t maskTextureIndex2, uint8_t rayPortalIndex,
        uint8_t spriteSheetRows, uint8_t spriteSheetCols, uint8_t spriteSheetFPS, float rotationSpeed, bool enableEmission, float emissiveIntensity) :
    m_maskTextureIndex{ maskTextureIndex }, m_maskTextureIndex2 { maskTextureIndex2 }, m_rayPortalIndex{ rayPortalIndex },
    m_spriteSheetRows { spriteSheetRows }, m_spriteSheetCols { spriteSheetCols }, m_spriteSheetFPS { spriteSheetFPS }, m_rotationSpeed { rotationSpeed }, m_enableEmission(enableEmission), m_emissiveIntensity(emissiveIntensity) {
    updateCachedHash();
  }

  void writeGPUData(unsigned char* data, std::size_t& offset) const {
    [[maybe_unused]] const std::size_t oldOffset = offset;

    writeGPUHelperExplicit<2>(data, offset, m_maskTextureIndex);
    writeGPUHelperExplicit<2>(data, offset, m_maskTextureIndex2);

    writeGPUHelper(data, offset, m_rayPortalIndex);
    writeGPUHelper(data, offset, m_spriteSheetRows);
    writeGPUHelper(data, offset, m_spriteSheetCols);
    writeGPUHelper(data, offset, m_spriteSheetFPS);
    assert(m_rotationSpeed < FLOAT16_MAX);
    writeGPUHelper(data, offset, glm::packHalf1x16(m_rotationSpeed));
    float emissiveIntensity = m_enableEmission ? m_emissiveIntensity : 1.0f;
    writeGPUHelper(data, offset, glm::packHalf1x16(emissiveIntensity));

    writeGPUPadding<16>(data, offset); // Note: Padding for unused space

    writeGPUHelper(data, offset, static_cast<uint32_t>(2 << 30)); // Note: Bit 30 and 31 of last word set to 2 for ray portal material type

    assert(offset - oldOffset == kSurfaceMaterialGPUSize);
  }

  bool operator==(const RtRayPortalSurfaceMaterial& r) const {
    return m_cachedHash == r.m_cachedHash;
  }

  XXH64_hash_t getHash() const {
    return m_cachedHash;
  }

  uint32_t getMaskTextureIndex() const {
    return m_maskTextureIndex;
  }

  uint32_t getMaskTextureIndex2() const {
    return m_maskTextureIndex2;
  }

  uint8_t getRayPortalIndex() const {
    return m_rayPortalIndex;
  }

  float getRotationSpeed() const {
    return m_rotationSpeed;
  }

  bool getEnableEmission() const {
    return m_enableEmission;
  }

  float getEmissiveIntensity() const {
    return m_emissiveIntensity;
  }

  uint8_t getSpriteSheetFPS() const {
    return m_spriteSheetFPS;
  }

private:
  void updateCachedHash() {
    XXH64_hash_t h = 0;

    h = XXH64(&m_maskTextureIndex, sizeof(m_maskTextureIndex), h);
    h = XXH64(&m_maskTextureIndex2, sizeof(m_maskTextureIndex2), h);
    h = XXH64(&m_rayPortalIndex, sizeof(m_rayPortalIndex), h);
    h = XXH64(&m_spriteSheetRows, sizeof(m_spriteSheetRows), h);
    h = XXH64(&m_spriteSheetCols, sizeof(m_spriteSheetCols), h);
    h = XXH64(&m_spriteSheetFPS, sizeof(m_spriteSheetFPS), h);
    h = XXH64(&m_rotationSpeed, sizeof(m_rotationSpeed), h);
    h = XXH64(&m_enableEmission, sizeof(m_enableEmission), h);

    m_cachedHash = h;
  }

  uint32_t m_maskTextureIndex;
  uint32_t m_maskTextureIndex2;

  uint8_t m_rayPortalIndex;
  uint8_t m_spriteSheetRows;
  uint8_t m_spriteSheetCols;
  uint8_t m_spriteSheetFPS;
  float m_rotationSpeed;
  bool m_enableEmission;
  float m_emissiveIntensity;

  XXH64_hash_t m_cachedHash;
};

struct RtSurfaceMaterial {
  RtSurfaceMaterial(const RtOpaqueSurfaceMaterial& opaqueSurfaceMaterial) :
    m_type{ RtSurfaceMaterialType::Opaque },
    m_opaqueSurfaceMaterial{ opaqueSurfaceMaterial } {}

  RtSurfaceMaterial(const RtTranslucentSurfaceMaterial& translucentSurfaceMaterial) :
    m_type{ RtSurfaceMaterialType::Translucent },
    m_translucentSurfaceMaterial{ translucentSurfaceMaterial } {}

  RtSurfaceMaterial(const RtRayPortalSurfaceMaterial& rayPortalSurfaceMaterial) :
    m_type{ RtSurfaceMaterialType::RayPortal },
    m_rayPortalSurfaceMaterial{ rayPortalSurfaceMaterial } {}

  RtSurfaceMaterial(const RtSurfaceMaterial& surfaceMaterial) :
    m_type{ surfaceMaterial.m_type } {
    switch (m_type) {
    default:
      assert(false);
    case RtSurfaceMaterialType::Opaque:
      new (&m_opaqueSurfaceMaterial) RtOpaqueSurfaceMaterial{ surfaceMaterial.m_opaqueSurfaceMaterial };
      break;
    case RtSurfaceMaterialType::Translucent:
      new (&m_translucentSurfaceMaterial) RtTranslucentSurfaceMaterial{ surfaceMaterial.m_translucentSurfaceMaterial };
      break;
    case RtSurfaceMaterialType::RayPortal:
      new (&m_rayPortalSurfaceMaterial) RtRayPortalSurfaceMaterial{ surfaceMaterial.m_rayPortalSurfaceMaterial };
      break;
    }
  }

  ~RtSurfaceMaterial() {
    switch (m_type) {
    default:
      assert(false);
    case RtSurfaceMaterialType::Opaque:
      m_opaqueSurfaceMaterial.~RtOpaqueSurfaceMaterial();
      break;
    case RtSurfaceMaterialType::Translucent:
      m_translucentSurfaceMaterial.~RtTranslucentSurfaceMaterial();
      break;
    case RtSurfaceMaterialType::RayPortal:
      m_rayPortalSurfaceMaterial.~RtRayPortalSurfaceMaterial();
      break;
    }
  }

  void writeGPUData(unsigned char* data, std::size_t& offset) const {
    switch (m_type) {
    default:
      assert(false);
    case RtSurfaceMaterialType::Opaque:
      m_opaqueSurfaceMaterial.writeGPUData(data, offset);
      break;
    case RtSurfaceMaterialType::Translucent:
      m_translucentSurfaceMaterial.writeGPUData(data, offset);
      break;
    case RtSurfaceMaterialType::RayPortal:
      m_rayPortalSurfaceMaterial.writeGPUData(data, offset);
      break;
    }
  }

  RtSurfaceMaterial& operator=(const RtSurfaceMaterial& rtSurfaceMaterial) {
    if (this != &rtSurfaceMaterial) {
      m_type = rtSurfaceMaterial.m_type;

      switch (rtSurfaceMaterial.m_type) {
      default:
        assert(false);
      case RtSurfaceMaterialType::Opaque:
        m_opaqueSurfaceMaterial = rtSurfaceMaterial.m_opaqueSurfaceMaterial;
        break;
      case RtSurfaceMaterialType::Translucent:
        m_translucentSurfaceMaterial = rtSurfaceMaterial.m_translucentSurfaceMaterial;
        break;
      case RtSurfaceMaterialType::RayPortal:
        m_rayPortalSurfaceMaterial = rtSurfaceMaterial.m_rayPortalSurfaceMaterial;
        break;
      }
    }

    return *this;
  }

  bool operator==(const RtSurfaceMaterial& rhs) const {
    // Note: Different Surface Material types are not the same Surface Material so comparison can return false
    if (m_type != rhs.m_type) {
      return false;
    }

    switch (m_type) {
    default:
      assert(false);
    case RtSurfaceMaterialType::Opaque:
      return m_opaqueSurfaceMaterial == rhs.m_opaqueSurfaceMaterial;
    case RtSurfaceMaterialType::Translucent:
      return m_translucentSurfaceMaterial == rhs.m_translucentSurfaceMaterial;
    case RtSurfaceMaterialType::RayPortal:
      return m_rayPortalSurfaceMaterial == rhs.m_rayPortalSurfaceMaterial;
    }
  }

  XXH64_hash_t getHash() const {
    switch (m_type) {
    default:
      assert(false);
    case RtSurfaceMaterialType::Opaque:
      return m_opaqueSurfaceMaterial.getHash();
    case RtSurfaceMaterialType::Translucent:
      return m_translucentSurfaceMaterial.getHash();
    case RtSurfaceMaterialType::RayPortal:
      return m_rayPortalSurfaceMaterial.getHash();
    }
  }

  RtSurfaceMaterialType getType() const {
    return m_type;
  }

  const RtOpaqueSurfaceMaterial& getOpaqueSurfaceMaterial() const {
    assert(m_type == RtSurfaceMaterialType::Opaque);

    return m_opaqueSurfaceMaterial;
  }

  const RtTranslucentSurfaceMaterial& getTranslucentSurfaceMaterial() const {
    assert(m_type == RtSurfaceMaterialType::Translucent);

    return m_translucentSurfaceMaterial;
  }

  const RtRayPortalSurfaceMaterial& getRayPortalSurfaceMaterial() const {
    assert(m_type == RtSurfaceMaterialType::RayPortal);

    return m_rayPortalSurfaceMaterial;
  }
private:
  // Type-specific Surface Material Information

  RtSurfaceMaterialType m_type;
  union {
    RtOpaqueSurfaceMaterial m_opaqueSurfaceMaterial;
    RtTranslucentSurfaceMaterial m_translucentSurfaceMaterial;
    RtRayPortalSurfaceMaterial m_rayPortalSurfaceMaterial;
  };
};

// Volume Materials

// Todo: Compute size directly from sizeof of GPU structure (by including it), for now computed by sum of members manually
constexpr std::size_t kVolumeMaterialGPUSize = 4;

struct RtVolumeMaterial
{
  RtVolumeMaterial() {
    updateCachedHash();
  }

  void writeGPUData(unsigned char* data, std::size_t& offset) const {
    [[maybe_unused]] const std::size_t oldOffset = offset;

    writeGPUPadding<4>(data, offset);

    assert(offset - oldOffset == kVolumeMaterialGPUSize);
  }

  bool operator==(const RtVolumeMaterial& r) const {
    assert(false);

    return m_cachedHash == r.m_cachedHash;
  }

  XXH64_hash_t getHash() const {
    assert(false);

    return m_cachedHash;
  }
private:
  void updateCachedHash() {
    const XXH64_hash_t h = 0;

    m_cachedHash = h;
  }

  XXH64_hash_t m_cachedHash;
};

enum class MaterialDataType {
  Legacy,
  Opaque,
  Translucent,
  RayPortal,
};

// Note: For use with "Legacy" D3D9 material information
struct LegacyMaterialData {
  LegacyMaterialData()
  { }

  LegacyMaterialData(const TextureRef& colorTexture, const TextureRef& colorTexture2, const D3DMATERIAL9 material)
    : d3dMaterial{ material }
  {
    // Note: Texture required to be populated for hashing to function
    assert(!colorTexture.isImageEmpty());

    updateCachedHash();
  }

  const XXH64_hash_t getHash() const {
    return m_cachedHash;
  }

  const TextureRef& getColorTexture() const {
    return colorTextures[0];
  }

  const TextureRef& getColorTexture2() const {
    return colorTextures[1];
  }

  const D3DMATERIAL9& getLegacyMaterial() const {
    return d3dMaterial;
  }

  inline const bool usesTexture() const {
    return ((getColorTexture().isValid()  && !getColorTexture().isImageEmpty()) ||
            (getColorTexture2().isValid() && !getColorTexture2().isImageEmpty()));
  }
  
  const void printDebugInfo(const char* name = "") const {
#ifdef REMIX_DEVELOPMENT
    Logger::warn(str::format(
      "LegacyMaterialData ", name,
      " address: ", this,
      " alphaTestEnabled: ", alphaTestEnabled,
      " alphaTestReferenceValue: ", alphaTestReferenceValue,
      " alphaTestCompareOp: ", alphaTestCompareOp,
      " alphaBlendEnabled: ", alphaBlendEnabled,
      " srcColorBlendFactor: ", srcColorBlendFactor,
      " dstColorBlendFactor: ", dstColorBlendFactor,
      " colorBlendOp: ", colorBlendOp,
      // " textureColorArg1Source: ", textureColorArg1Source,
      // " textureColorArg2Source: ", textureColorArg2Source,
      // " textureColorOperation: ", textureColorOperation,
      // " textureAlphaArg1Source: ", textureAlphaArg1Source,
      // " textureAlphaArg2Source: ", textureAlphaArg2Source,
      // " textureAlphaOperation: ", textureAlphaOperation,
      " tFactor: ", tFactor,
      " isBlendedTerrain: ", isBlendedTerrain,
      // " m_d3dMaterial.Diffuse: ", m_d3dMaterial.Diffuse,
      // " m_d3dMaterial.Ambient: ", m_d3dMaterial.Ambient,
      // " m_d3dMaterial.Specular: ", m_d3dMaterial.Specular,
      // " m_d3dMaterial.Emissive: ", m_d3dMaterial.Emissive,
      // " m_d3dMaterial.Power: ", m_d3dMaterial.Power,
      std::hex, " m_colorTexture: 0x", colorTextures[0].getImageHash(),
      " m_colorTexture2: 0x", colorTextures[1].getImageHash(),
      " m_cachedHash: 0x", m_cachedHash, std::dec));
#endif
  }

  bool alphaTestEnabled = false;
  uint8_t alphaTestReferenceValue = 0;
  VkCompareOp alphaTestCompareOp = VkCompareOp::VK_COMPARE_OP_ALWAYS;
  bool alphaBlendEnabled = false;
  VkBlendFactor srcColorBlendFactor = VkBlendFactor::VK_BLEND_FACTOR_ONE;
  VkBlendFactor dstColorBlendFactor = VkBlendFactor::VK_BLEND_FACTOR_ZERO;
  VkBlendOp colorBlendOp = VkBlendOp::VK_BLEND_OP_ADD;
  RtTextureArgSource diffuseColorSource= RtTextureArgSource::None;
  RtTextureArgSource specularColorSource = RtTextureArgSource::None;
  RtTextureArgSource textureColorArg1Source = RtTextureArgSource::Texture;
  RtTextureArgSource textureColorArg2Source = RtTextureArgSource::None;
  DxvkRtTextureOperation textureColorOperation = DxvkRtTextureOperation::Modulate;
  RtTextureArgSource textureAlphaArg1Source = RtTextureArgSource::Texture;
  RtTextureArgSource textureAlphaArg2Source = RtTextureArgSource::None;
  DxvkRtTextureOperation textureAlphaOperation = DxvkRtTextureOperation::SelectArg1;
  uint32_t tFactor = 0xffffffff;  // Value for D3DRS_TEXTUREFACTOR, default value of is opaque white
  bool isBlendedTerrain = false;
  D3DMATERIAL9 d3dMaterial = {};
  bool isTextureFactorBlend = false;

private:
  friend class RtxContext;
  friend struct D3D9Rtx;

  void updateCachedHash() {
    // Note: Currently only based on the color texture's data hash. This may have to be changed later to
    // incorporate more textures used to identify a material uniquely. Note this is not the same as the
    // plain data hash used by the RtSurfaceMaterial for storage in map-like data structures, but rather
    // one used to identify a material and compare to user-provided hashes.
    m_cachedHash = colorTextures[0].getImageHash();
  }

  const static uint32_t kMaxSupportedTextures = 2;
  TextureRef colorTextures[kMaxSupportedTextures] = {};
  uint32_t colorTextureSlot[kMaxSupportedTextures] = {};

  XXH64_hash_t m_cachedHash;
};

struct OpaqueMaterialData {
  OpaqueMaterialData(
    const TextureRef& albedoOpacityTexture, const TextureRef& normalTexture,
    const TextureRef& tangentTexture, const TextureRef& roughnessTexture,
    const TextureRef& metallicTexture, const TextureRef& emissiveColorTexture,
    float anisotropy, float emissiveIntensity,
    const Vector4& albedoOpacityConstant,
    float roughnessConstant, float metallicConstant,
    const Vector3& emissiveColorConstant, bool enableEmission,
    uint8_t spriteSheetRows,
    uint8_t spriteSheetCols,
    uint8_t spriteSheetFPS,
    bool enableThinFilm, bool alphaIsThinFilmThickness, float thinFilmThicknessConstant,
    bool useLegacyAlphaState, bool blendEnabled, BlendType blendType, bool invertedBlend,
    AlphaTestType alphaTestType, uint8_t alphaTestReferenceValue) :
    m_albedoOpacityTexture{ albedoOpacityTexture }, m_normalTexture{ normalTexture },
    m_tangentTexture{ tangentTexture }, m_roughnessTexture{ roughnessTexture },
    m_metallicTexture{ metallicTexture }, m_emissiveColorTexture{ emissiveColorTexture },
    m_anisotropy{ anisotropy }, m_emissiveIntensity{ emissiveIntensity },
    m_albedoOpacityConstant{ albedoOpacityConstant },
    m_roughnessConstant{ roughnessConstant }, m_metallicConstant{ metallicConstant },
    m_emissiveColorConstant{ emissiveColorConstant }, m_enableEmission(enableEmission),
    m_spriteSheetRows { spriteSheetRows }, m_spriteSheetCols { spriteSheetCols }, m_spriteSheetFPS { spriteSheetFPS },
    m_enableThinFilm { enableThinFilm }, m_alphaIsThinFilmThickness { alphaIsThinFilmThickness }, m_thinFilmThicknessConstant { thinFilmThicknessConstant },
    m_useLegacyAlphaState{ useLegacyAlphaState }, m_blendEnabled{ blendEnabled }, m_blendType{ blendType }, m_invertedBlend{ invertedBlend },
    m_alphaTestType{ alphaTestType }, m_alphaTestReferenceValue{ alphaTestReferenceValue } {
    sanitizeData();
    // Note: Called after data is sanitized to have the hashed value reflect the adjusted values.
    updateCachedHash();
  }

  const XXH64_hash_t getHash() const {
    return m_cachedHash;
  }

  const TextureRef& getAlbedoOpacityTexture() const {
    return m_albedoOpacityTexture;
  }

  const TextureRef& getNormalTexture() const {
    return m_normalTexture;
  }

  const TextureRef& getTangentTexture() const {
    return m_tangentTexture;
  }

  const TextureRef& getRoughnessTexture() const {
    return m_roughnessTexture;
  }

  const TextureRef& getMetallicTexture() const {
    return m_metallicTexture;
  }

  const TextureRef& getEmissiveColorTexture() const {
    return m_emissiveColorTexture;
  }

  float getAnisotropy() const {
    return m_anisotropy;
  }

  float getEmissiveIntensity() const {
    return m_emissiveIntensity;
  }

  const Vector4& getAlbedoOpacityConstant() const {
    return m_albedoOpacityConstant;
  }

  float getRoughnessConstant() const {
    return m_roughnessConstant;
  }

  float getMetallicConstant() const {
    return m_metallicConstant;
  }

  const Vector3& getEmissiveColorConstant() const {
    return m_emissiveColorConstant;
  }

  bool getEnableEmission() const {
    return m_enableEmission;
  }

  bool getEnableThinFilm() const {
    return m_enableThinFilm;
  }

  bool getAlphaIsThinFilmThickness() const {
    return m_alphaIsThinFilmThickness;
  }

  float getThinFilmThicknessConstant() const {
    return m_thinFilmThicknessConstant;
  }

  uint8_t getSpriteSheetRows() const {
    return m_spriteSheetRows;
  }

  uint8_t getSpriteSheetCols() const {
    return m_spriteSheetCols;
  }

  uint8_t getSpriteSheetFPS() const {
    return m_spriteSheetFPS;
  }

  bool getUseLegacyAlphaState() const {
    return m_useLegacyAlphaState;
  }

  bool getBlendEnabled() const {
    return m_blendEnabled;
  }

  BlendType getBlendType() const {
    return m_blendType;
  }

  bool getInvertedBlend() const {
    return m_invertedBlend;
  }

  AlphaTestType getAlphaTestType() const {
    return m_alphaTestType;
  }

  uint8_t getAlphaTestReferenceValue() const {
    return m_alphaTestReferenceValue;
  }
private:
  // Note: Ensures the data falls within the desired valid ranges in case its source was malformed (e.g.
  // manual USD editing).
  void sanitizeData() {
    constexpr OpaqueMaterialMins mins{};
    constexpr OpaqueMaterialMaxes maxes{};

    m_anisotropy = std::clamp(m_anisotropy, mins.AnisotropyConstant, maxes.AnisotropyConstant);
    m_emissiveIntensity = std::clamp(m_emissiveIntensity, mins.EmissiveIntensity, maxes.EmissiveIntensity);

    m_albedoOpacityConstant.x = std::clamp(m_albedoOpacityConstant.x, mins.AlbedoConstant, maxes.AlbedoConstant);
    m_albedoOpacityConstant.y = std::clamp(m_albedoOpacityConstant.y, mins.AlbedoConstant, maxes.AlbedoConstant);
    m_albedoOpacityConstant.z = std::clamp(m_albedoOpacityConstant.z, mins.AlbedoConstant, maxes.AlbedoConstant);
    m_albedoOpacityConstant.w = std::clamp(m_albedoOpacityConstant.w, mins.OpacityConstant, maxes.OpacityConstant);
    m_roughnessConstant = std::clamp(m_roughnessConstant, mins.RoughnessConstant, maxes.RoughnessConstant);
    m_metallicConstant = std::clamp(m_metallicConstant, mins.MetallicConstant, maxes.MetallicConstant);
    m_emissiveColorConstant.x = std::clamp(m_emissiveColorConstant.x, mins.EmissiveColorConstant, maxes.EmissiveColorConstant);
    m_emissiveColorConstant.y = std::clamp(m_emissiveColorConstant.y, mins.EmissiveColorConstant, maxes.EmissiveColorConstant);
    m_emissiveColorConstant.z = std::clamp(m_emissiveColorConstant.z, mins.EmissiveColorConstant, maxes.EmissiveColorConstant);
    m_thinFilmThicknessConstant = std::clamp(m_thinFilmThicknessConstant, mins.ThinFilmThicknessConstant, maxes.ThinFilmThicknessConstant);
    m_blendType = std::clamp(m_blendType, mins.MinBlendType, maxes.MaxBlendType);
    m_alphaTestType = std::clamp(m_alphaTestType, mins.MinAlphaTestType, maxes.MaxAlphaTestType);
    m_alphaTestReferenceValue = std::clamp(m_alphaTestReferenceValue, mins.AlphaReferenceValue, maxes.AlphaReferenceValue);
  }

  void updateCachedHash() {
    XXH64_hash_t h = 0;
    
    h ^= m_albedoOpacityTexture.getImageHash();
    h ^= m_normalTexture.getImageHash();
    h ^= m_tangentTexture.getImageHash();
    h ^= m_roughnessTexture.getImageHash();
    h ^= m_metallicTexture.getImageHash();
    h ^= m_emissiveColorTexture.getImageHash();

    h = XXH64(&m_anisotropy, sizeof(m_anisotropy), h);
    h = XXH64(&m_albedoOpacityConstant[0], sizeof(m_albedoOpacityConstant), h);
    h = XXH64(&m_roughnessConstant, sizeof(m_roughnessConstant), h);
    h = XXH64(&m_metallicConstant, sizeof(m_metallicConstant), h);
    h = XXH64(&m_emissiveColorConstant[0], sizeof(m_emissiveColorConstant), h);
    h = XXH64(&m_enableEmission, sizeof(m_enableEmission), h);
    h = XXH64(&m_spriteSheetRows, sizeof(m_spriteSheetRows), h);
    h = XXH64(&m_spriteSheetCols, sizeof(m_spriteSheetCols), h);
    h = XXH64(&m_spriteSheetFPS, sizeof(m_spriteSheetFPS), h);
    h = XXH64(&m_enableThinFilm, sizeof(m_enableThinFilm), h);
    h = XXH64(&m_alphaIsThinFilmThickness, sizeof(m_alphaIsThinFilmThickness), h);
    h = XXH64(&m_thinFilmThicknessConstant, sizeof(m_thinFilmThicknessConstant), h);
    h = XXH64(&m_useLegacyAlphaState, sizeof(m_useLegacyAlphaState), h);
    h = XXH64(&m_blendEnabled, sizeof(m_blendEnabled), h);
    h = XXH64(&m_blendType, sizeof(m_blendType), h);
    h = XXH64(&m_invertedBlend, sizeof(m_invertedBlend), h);
    h = XXH64(&m_alphaTestType, sizeof(m_alphaTestType), h);
    h = XXH64(&m_alphaTestReferenceValue, sizeof(m_alphaTestReferenceValue), h);

    m_cachedHash = h;
  }

  // Note: Matches RtOpaqueSurfaceMaterial members

  TextureRef m_albedoOpacityTexture;
  TextureRef m_normalTexture;
  TextureRef m_tangentTexture;
  TextureRef m_roughnessTexture;
  TextureRef m_metallicTexture;
  TextureRef m_emissiveColorTexture;

  float m_anisotropy;
  float m_emissiveIntensity;

  Vector4 m_albedoOpacityConstant;
  float m_roughnessConstant;
  float m_metallicConstant;
  Vector3 m_emissiveColorConstant;

  bool m_enableEmission;

  uint8_t m_spriteSheetRows;
  uint8_t m_spriteSheetCols;
  uint8_t m_spriteSheetFPS;

  bool m_enableThinFilm;
  bool m_alphaIsThinFilmThickness;
  float m_thinFilmThicknessConstant;

  // Todo: These overrides are applied to the Surface and are not technically part of the
  // material itself so should be removed or relocated some day, they are just here for now
  // since they are specified in the Opaque MDL currently.
  bool m_useLegacyAlphaState;
  bool m_blendEnabled;
  BlendType m_blendType;
  bool m_invertedBlend;
  AlphaTestType m_alphaTestType;
  uint8_t m_alphaTestReferenceValue;

  XXH64_hash_t m_cachedHash;
};

struct TranslucentMaterialData {
  TranslucentMaterialData(
    const TextureRef& normalTexture,
    float refractiveIndex, const TextureRef& transmittanceTexture, const Vector3& transmittanceColor,
    float transmittanceMeasureDistance,
    bool enableEmission, float emissiveIntensity, const Vector3& emissiveColorConstant,
    bool thinWalled, float thinWallThickness, bool useDiffuseLayer) :
    m_normalTexture(normalTexture), m_refractiveIndex(refractiveIndex),
    m_transmittanceTexture(transmittanceTexture), m_transmittanceColor(transmittanceColor), m_transmittanceMeasurementDistance(transmittanceMeasureDistance),
    m_enableEmission(enableEmission), m_emissiveIntensity(emissiveIntensity), m_emissiveColorConstant(emissiveColorConstant),
    m_isThinWalled(thinWalled), m_thinWallThickness(thinWallThickness), m_useDiffuseLayer(useDiffuseLayer) {
    sanitizeData();
    // Note: Called after data is sanitized to have the hashed value reflect the adjusted values.
    updateCachedHash();
  }

  const XXH64_hash_t getHash() const {
    return m_cachedHash;
  }

  const TextureRef& getNormalTexture() const {
    return m_normalTexture;
  }

  float getRefractiveIndex() const {
    return m_refractiveIndex;
  }

  const TextureRef& getTransmittanceTexture() const {
    return m_transmittanceTexture;
  }

  const Vector3& getTransmittanceColor() const {
    return m_transmittanceColor;
  }

  float getTransmittanceMeasurementDistance() const {
    return m_transmittanceMeasurementDistance;
  }

  bool getEnableEmission() const {
    return m_enableEmission;
  }

  float getEmissiveIntensity() const {
    return m_emissiveIntensity;
  }

  const Vector3& getEmissiveColorConstant() const {
    return m_emissiveColorConstant;
  }

  bool getIsThinWalled() const {
    return m_isThinWalled;
  }

  float getThinWallThickness() const {
    return m_thinWallThickness;
  }

  bool getUseDiffuseLayer() const {
    return m_useDiffuseLayer;
  }

private:
  // Note: Ensures the data falls within the desired valid ranges in case its source was malformed (e.g.
  // manual USD editing).
  void sanitizeData() {
    constexpr TranslucentMaterialMins mins {};
    constexpr TranslucentMaterialMaxes maxes {};

    m_refractiveIndex = std::clamp(m_refractiveIndex, mins.RefractiveIndex, maxes.RefractiveIndex);
    m_transmittanceColor.x = std::clamp(m_transmittanceColor.x, mins.TransmittanceColor, maxes.TransmittanceColor);
    m_transmittanceColor.y = std::clamp(m_transmittanceColor.y, mins.TransmittanceColor, maxes.TransmittanceColor);
    m_transmittanceColor.z = std::clamp(m_transmittanceColor.z, mins.TransmittanceColor, maxes.TransmittanceColor);
    m_transmittanceMeasurementDistance = std::clamp(m_transmittanceMeasurementDistance, mins.TransmittanceMeasurementDistance, maxes.TransmittanceMeasurementDistance);
    m_emissiveIntensity = std::clamp(m_emissiveIntensity, mins.EmissiveIntensity, maxes.EmissiveIntensity);
    m_emissiveColorConstant.x = std::clamp(m_emissiveColorConstant.x, mins.EmissiveColorConstant, maxes.EmissiveColorConstant);
    m_emissiveColorConstant.y = std::clamp(m_emissiveColorConstant.y, mins.EmissiveColorConstant, maxes.EmissiveColorConstant);
    m_emissiveColorConstant.z = std::clamp(m_emissiveColorConstant.z, mins.EmissiveColorConstant, maxes.EmissiveColorConstant);
    m_thinWallThickness = std::clamp(m_thinWallThickness, mins.ThinWallThickness, maxes.ThinWallThickness);
  }

  void updateCachedHash() {
    XXH64_hash_t h = 0;

    h ^= m_normalTexture.getImageHash();

    h = XXH64(&m_refractiveIndex, sizeof(m_refractiveIndex), h);
    
    h ^= m_transmittanceTexture.getImageHash();

    h = XXH64(&m_transmittanceColor[0], sizeof(m_transmittanceColor), h);
    h = XXH64(&m_transmittanceMeasurementDistance, sizeof(m_transmittanceMeasurementDistance), h);
    h = XXH64(&m_enableEmission, sizeof(m_enableEmission), h);
    h = XXH64(&m_emissiveColorConstant[0], sizeof(m_emissiveColorConstant), h);
    h = XXH64(&m_isThinWalled, sizeof(m_isThinWalled), h);
    h = XXH64(&m_thinWallThickness, sizeof(m_thinWallThickness), h);
    h = XXH64(&m_useDiffuseLayer, sizeof(m_useDiffuseLayer), h);

    m_cachedHash = h;
  }

  TextureRef m_normalTexture;
  float m_refractiveIndex;
  TextureRef m_transmittanceTexture;
  Vector3 m_transmittanceColor;
  float m_transmittanceMeasurementDistance;
  bool m_enableEmission;
  float m_emissiveIntensity;
  Vector3 m_emissiveColorConstant;
  bool m_isThinWalled;
  float m_thinWallThickness;
  bool m_useDiffuseLayer;

  XXH64_hash_t m_cachedHash;
};

struct RayPortalMaterialData {
  RayPortalMaterialData(const TextureRef& maskTexture, const TextureRef& maskTexture2, uint8_t rayPortalIndex,
        uint8_t spriteSheetRows, uint8_t spriteSheetCols, uint8_t spriteSheetFPS, float rotationSpeed, bool enableEmission, float emissiveIntensity) :
    m_maskTexture{ maskTexture }, m_maskTexture2 { maskTexture2 }, m_rayPortalIndex{ rayPortalIndex },
    m_spriteSheetRows { spriteSheetRows }, m_spriteSheetCols { spriteSheetCols }, m_spriteSheetFPS { spriteSheetFPS }, m_rotationSpeed { rotationSpeed }, m_enableEmission(enableEmission), m_emissiveIntensity(emissiveIntensity) {
    updateCachedHash();
  }

  const XXH64_hash_t getHash() const {
    return m_cachedHash;
  }

  const TextureRef& getMaskTexture() const {
    return m_maskTexture;
  }

  const TextureRef& getMaskTexture2() const {
    return m_maskTexture2;
  }

  uint8_t getRayPortalIndex() const {
    return m_rayPortalIndex;
  }

  uint8_t getSpriteSheetRows() const {
    return m_spriteSheetRows;
  }

  uint8_t getSpriteSheetCols() const {
    return m_spriteSheetCols;
  }

  uint8_t getSpriteSheetFPS() const {
    return m_spriteSheetFPS;
  }

  float getRotationSpeed() const {
    return m_rotationSpeed;
  }

  bool getEnableEmission() const {
    return m_enableEmission;
  }

  float getEmissiveIntensity() const {
    return m_emissiveIntensity;
  }

private:
  void updateCachedHash() {
    XXH64_hash_t h = 0;

    h ^= m_maskTexture.getImageHash();
    h ^= m_maskTexture2.getImageHash();

    h = XXH64(&m_rayPortalIndex, sizeof(m_rayPortalIndex), h);
    h = XXH64(&m_spriteSheetRows, sizeof(m_spriteSheetRows), h);
    h = XXH64(&m_spriteSheetCols, sizeof(m_spriteSheetCols), h);
    h = XXH64(&m_spriteSheetFPS, sizeof(m_spriteSheetFPS), h);
    h = XXH64(&m_rotationSpeed, sizeof(m_rotationSpeed), h);
    h = XXH64(&m_enableEmission, sizeof(m_enableEmission), h);

    m_cachedHash = h;
  }

  TextureRef m_maskTexture;
  TextureRef m_maskTexture2;

  uint8_t m_rayPortalIndex;
  uint8_t m_spriteSheetRows;
  uint8_t m_spriteSheetCols;
  uint8_t m_spriteSheetFPS;
  float m_rotationSpeed;
  bool m_enableEmission;
  float m_emissiveIntensity;

  XXH64_hash_t m_cachedHash;
};

struct MaterialData {
  MaterialData(const LegacyMaterialData& legacyMaterialData) :
    m_type{ MaterialDataType::Legacy },
    m_legacyMaterialData{ legacyMaterialData } {}

  MaterialData(const OpaqueMaterialData& opaqueMaterialData, bool ignored = false) :
    m_ignored {ignored},
    m_type{ MaterialDataType::Opaque},
    m_opaqueMaterialData{ opaqueMaterialData } {}

  MaterialData(const TranslucentMaterialData& translucentMaterialData, bool ignored = false) :
    m_ignored {ignored},
    m_type{ MaterialDataType::Translucent },
    m_translucentMaterialData{ translucentMaterialData }{}

  MaterialData(const RayPortalMaterialData& rayPortalMaterialData) :
    m_type{ MaterialDataType::RayPortal },
    m_rayPortalMaterialData{ rayPortalMaterialData } {}

  MaterialData(const MaterialData& materialData) :
    m_type { materialData.m_type }, m_ignored { materialData.m_ignored } {
    switch (m_type) {
    default:
      assert(false);
    case MaterialDataType::Legacy:
      new (&m_legacyMaterialData) LegacyMaterialData{ materialData.m_legacyMaterialData };
      break;
    case MaterialDataType::Opaque:
      new (&m_opaqueMaterialData) OpaqueMaterialData{ materialData.m_opaqueMaterialData };
      break;
    case MaterialDataType::Translucent:
      new (&m_translucentMaterialData) TranslucentMaterialData{ materialData.m_translucentMaterialData };
      break;
    case MaterialDataType::RayPortal:
      new (&m_rayPortalMaterialData) RayPortalMaterialData{ materialData.m_rayPortalMaterialData };
      break;
    }
  }

  ~MaterialData() {
    switch (m_type) {
    default:
      assert(false);
    case MaterialDataType::Legacy:
      m_legacyMaterialData.~LegacyMaterialData();
      break;
    case MaterialDataType::Opaque:
      m_opaqueMaterialData.~OpaqueMaterialData();
      break;
    case MaterialDataType::Translucent:
      m_translucentMaterialData.~TranslucentMaterialData();
      break;
    case MaterialDataType::RayPortal:
      m_rayPortalMaterialData.~RayPortalMaterialData();
      break;
    }
  }

  MaterialData& operator=(const MaterialData& materialData) {
    if (this != &materialData) {
      m_type = materialData.m_type;

      switch (materialData.m_type) {
      default:
        assert(false);
      case MaterialDataType::Legacy:
        m_legacyMaterialData = materialData.m_legacyMaterialData;
        break;
      case MaterialDataType::Opaque:
        m_opaqueMaterialData = materialData.m_opaqueMaterialData;
        break;
      case MaterialDataType::Translucent:
        m_translucentMaterialData = materialData.m_translucentMaterialData;
        break;
      case MaterialDataType::RayPortal:
        m_rayPortalMaterialData = materialData.m_rayPortalMaterialData;
        break;
      }
    }

    return *this;
  }

  const bool getIgnored() const {
    return m_ignored;
  }

  const XXH64_hash_t getHash() const {
    switch (m_type) {
    default:
      assert(false);
    case MaterialDataType::Legacy:
      return m_legacyMaterialData.getHash();
    case MaterialDataType::Opaque:
      return m_opaqueMaterialData.getHash();
    case MaterialDataType::Translucent:
      return m_translucentMaterialData.getHash();
    case MaterialDataType::RayPortal:
      return m_rayPortalMaterialData.getHash();
    }
  }

  MaterialDataType getType() const {
    return m_type;
  }

  const LegacyMaterialData& getLegacyMaterialData() const {
    assert(m_type == MaterialDataType::Legacy);

    return m_legacyMaterialData;
  }

  const OpaqueMaterialData& getOpaqueMaterialData() const {
    assert(m_type == MaterialDataType::Opaque);

    return m_opaqueMaterialData;
  }

  const TranslucentMaterialData& getTranslucentMaterialData() const {
    assert(m_type == MaterialDataType::Translucent);

    return m_translucentMaterialData;
  }

  const RayPortalMaterialData& getRayPortalMaterialData() const {
    assert(m_type == MaterialDataType::RayPortal);

    return m_rayPortalMaterialData;
  }

private:
  // Type-specific Material Data Information
  bool m_ignored = false;

  MaterialDataType m_type;
  union {
    LegacyMaterialData m_legacyMaterialData;
    OpaqueMaterialData m_opaqueMaterialData;
    TranslucentMaterialData m_translucentMaterialData;
    RayPortalMaterialData m_rayPortalMaterialData;
  };
};

} // namespace dxvk
