/*
* Copyright (c) 2021-2024, NVIDIA CORPORATION. All rights reserved.
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
#include "rtx/utility/shared_constants.h"
#include "rtx/concept/surface/surface_shared.h"
#include "rtx/pass/common_binding_indices.h"
#include "../../dxso/dxso_util.h"
#include "rtx_material_data.h"
#include "../../lssusd/mdl_helpers.h"
#include "rtx/pass/particles/particle_system_common.h"
#include "dxvk_constant_state.h"

namespace dxvk {
// Surfaces

// Todo: Compute size directly from sizeof of GPU structure (by including it), for now computed by sum of members manually
constexpr std::size_t kSurfaceGPUSize = 15 * 4 * 4;

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
float getDisplacementFactor();

struct RtSurface {
  RtSurface() {
  }

  void writeGPUData(unsigned char* data, std::size_t& offset, size_t surfaceIndex = SIZE_MAX) const {
    [[maybe_unused]] const std::size_t oldOffset = offset;

    // Note: Position buffer and surface material index are required for proper
    // behavior of the Surface on the GPU.
    assert(positionBufferIndex != kSurfaceInvalidBufferIndex);

    writeGPUHelperExplicit<2>(data, offset, positionBufferIndex);
    writeGPUHelperExplicit<2>(data, offset, previousPositionBufferIndex);
    writeGPUHelperExplicit<2>(data, offset, normalBufferIndex);
    writeGPUHelperExplicit<2>(data, offset, texcoordBufferIndex);
    writeGPUHelperExplicit<2>(data, offset, indexBufferIndex);
    writeGPUHelperExplicit<2>(data, offset, color0BufferIndex);

    writeGPUHelperExplicit<1>(data, offset, normalFormat == VK_FORMAT_R32_UINT ? 1 : 0);

    // 1 unused bytes here.
    writeGPUPadding<1>(data, offset);

    const uint16_t packedHash =
      (uint16_t) (associatedGeometryHash >> 48) ^
      (uint16_t) (associatedGeometryHash >> 32) ^
      (uint16_t) (associatedGeometryHash >> 16) ^
      (uint16_t) associatedGeometryHash;

    writeGPUHelper(data, offset, packedHash);

    writeGPUHelper(data, offset, positionOffset);
    writeGPUHelper(data, offset, normalOffset);
    writeGPUHelper(data, offset, texcoordOffset);
    writeGPUHelper(data, offset, color0Offset);
    writeGPUHelper(data, offset, objectPickingValue);

    writeGPUHelperExplicit<1>(data, offset, positionStride);
    writeGPUHelperExplicit<1>(data, offset, normalStride);
    writeGPUHelperExplicit<1>(data, offset, texcoordStride);
    writeGPUHelperExplicit<1>(data, offset, color0Stride);

    writeGPUHelperExplicit<3>(data, offset, firstIndex);
    writeGPUHelperExplicit<1>(data, offset, indexStride);

    // Note: Ensure alpha state values fit in the intended amount of bits allocated in the flags bitfield.
    assert(static_cast<uint32_t>(alphaState.alphaTestType) < (1 << 3));
    assert(static_cast<uint32_t>(alphaState.alphaTestReferenceValue) < (1 << 8));
    assert(static_cast<uint32_t>(alphaState.blendType) < (1 << 4));

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
    flags |= hasMaterialChanged ?            (1 << 23) : 0;
    flags |= isAnimatedWater ?               (1 << 24) : 0;
    flags |= isClipPlaneEnabled ?            (1 << 25) : 0;
    flags |= isMatte ?                       (1 << 26) : 0;
    flags |= isTextureFactorBlend ?          (1 << 27) : 0;
    flags |= isMotionBlurMaskOut ?           (1 << 28) : 0;
    flags |= skipSurfaceInteractionSpritesheetAdjustment ? (1 << 29) : 0;
    flags |= ignoreTransparencyLayer ?       (1 << 30) : 0;
    // Note: This flag is purely for debug view purpose. If we need to add more functional flags and running out of bits, we should move this flag to other place.
    flags |= isInsideFrustum ?               (1 << 31) : 0;

    writeGPUHelper(data, offset, flags);

    // Note: Matricies are stored on the cpu side in column-major order, the same as the GPU.

    Matrix4 instanceToWorld = objectToWorld;
    Matrix4 prevInstanceToWorld = prevObjectToWorld;
    Matrix3 normalInstanceToWorld = normalObjectToWorld;

    if (instancesToObject && surfaceIndexOfFirstInstance != SIZE_MAX && surfaceIndex != SIZE_MAX) {
      const size_t instanceIndex = surfaceIndex - surfaceIndexOfFirstInstance;
      if (instanceIndex >= instancesToObject->size()) {
        // Note: This should never happen.
        assert(false);
        Logger::err("Error: invalid instance index in RtSurface::WriteGPUData.");
      } else {
        instanceToWorld = objectToWorld * (*instancesToObject)[instanceIndex];
        prevInstanceToWorld = prevObjectToWorld * (*instancesToObject)[instanceIndex];
        normalInstanceToWorld = transpose(inverse(Matrix3(instanceToWorld)));
      }
    }

    // Note: Last row of object to world matrix not needed as it does not encode any useful information
    writeGPUHelper(data, offset, prevInstanceToWorld.data[0].x);
    writeGPUHelper(data, offset, prevInstanceToWorld.data[0].y);
    writeGPUHelper(data, offset, prevInstanceToWorld.data[0].z);
    writeGPUHelper(data, offset, prevInstanceToWorld.data[1].x);
    writeGPUHelper(data, offset, prevInstanceToWorld.data[1].y);
    writeGPUHelper(data, offset, prevInstanceToWorld.data[1].z);
    writeGPUHelper(data, offset, prevInstanceToWorld.data[2].x);
    writeGPUHelper(data, offset, prevInstanceToWorld.data[2].y);
    writeGPUHelper(data, offset, prevInstanceToWorld.data[2].z);
    writeGPUHelper(data, offset, prevInstanceToWorld.data[3].x);
    writeGPUHelper(data, offset, prevInstanceToWorld.data[3].y);
    writeGPUHelper(data, offset, prevInstanceToWorld.data[3].z);

    writeGPUHelper(data, offset, normalInstanceToWorld.data[0]);
    writeGPUHelper(data, offset, normalInstanceToWorld.data[1]);
    writeGPUHelper(data, offset, normalInstanceToWorld.data[2].x);
    writeGPUHelper(data, offset, normalInstanceToWorld.data[2].y);

    writeGPUHelper(data, offset, instanceToWorld.data[0].x);
    writeGPUHelper(data, offset, instanceToWorld.data[0].y);
    writeGPUHelper(data, offset, instanceToWorld.data[0].z);
    writeGPUHelper(data, offset, instanceToWorld.data[1].x);
    writeGPUHelper(data, offset, instanceToWorld.data[1].y);
    writeGPUHelper(data, offset, instanceToWorld.data[1].z);
    writeGPUHelper(data, offset, instanceToWorld.data[2].x);
    writeGPUHelper(data, offset, instanceToWorld.data[2].y);
    writeGPUHelper(data, offset, instanceToWorld.data[2].z);
    writeGPUHelper(data, offset, instanceToWorld.data[3].x);
    writeGPUHelper(data, offset, instanceToWorld.data[3].y);
    writeGPUHelper(data, offset, instanceToWorld.data[3].z);

    // Note: Only 2 rows of texture transform written for now due to limit of 2 element restriction.
    writeGPUHelper(data, offset, textureTransform.data[0].x);
    writeGPUHelper(data, offset, textureTransform.data[1].x);
    writeGPUHelper(data, offset, textureTransform.data[2].x);
    writeGPUHelper(data, offset, textureTransform.data[3].x);
    writeGPUHelper(data, offset, textureTransform.data[0].y);
    writeGPUHelper(data, offset, textureTransform.data[1].y);
    writeGPUHelper(data, offset, textureTransform.data[2].y);
    writeGPUHelper(data, offset, textureTransform.data[3].y);

    std::uint32_t textureSpritesheetData = 0;

    // Clamp rows and cols to at least 1, to avoid divide by 0 errors.
    textureSpritesheetData |= (static_cast<uint32_t>(std::max<uint8_t>(1, spriteSheetRows)) << 0);
    textureSpritesheetData |= (static_cast<uint32_t>(std::max<uint8_t>(1, spriteSheetCols)) << 8);
    textureSpritesheetData |= (static_cast<uint32_t>(spriteSheetFPS) << 16);
    // pack decalSortOrder into data13.x's last 8 bits.
    textureSpritesheetData |= (static_cast<uint32_t>(decalSortOrder) << 24);

    writeGPUHelper(data, offset, textureSpritesheetData);

    writeGPUHelper(data, offset, tFactor);

    std::uint32_t textureFlags = 0;

    assert((static_cast<uint32_t>(textureColorOperation) & 0x7) == static_cast<uint32_t>(textureColorOperation));
    assert((static_cast<uint32_t>(textureAlphaOperation) & 0x7) == static_cast<uint32_t>(textureAlphaOperation));
    assert(textureAlphaOperation != DxvkRtTextureOperation::Force_Modulate2x);

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
    writeGPUHelper(data, offset, normalInstanceToWorld.data[2].z);

    writeGPUHelper(data, offset, clipPlane);

    assert(offset - oldOffset == kSurfaceGPUSize);
  }

  uint32_t positionBufferIndex = kSurfaceInvalidBufferIndex;
  uint32_t previousPositionBufferIndex = kSurfaceInvalidBufferIndex;
  uint32_t positionOffset = 0;
  uint32_t positionStride = 0;

  uint32_t normalBufferIndex = kSurfaceInvalidBufferIndex;
  uint32_t normalOffset = 0;
  uint32_t normalStride = 0;
  VkFormat normalFormat = VK_FORMAT_UNDEFINED;

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
  bool hasMaterialChanged = false;
  bool isAnimatedWater = false;
  bool isClipPlaneEnabled = false;
  bool isTextureFactorBlend = false;
  bool isMotionBlurMaskOut = false;
  bool skipSurfaceInteractionSpritesheetAdjustment = false;
  bool isInsideFrustum = false;
  bool ignoreTransparencyLayer = false;

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

  void printDebugInfo(const char* name = "") const {
#ifdef REMIX_DEVELOPMENT
    Logger::warn(str::format(
      "RtSurface ", name, "\n",
      "  address: ", this, "\n",
      "  surfaceMaterialIndex: ", surfaceMaterialIndex, "\n",
      "  associatedGeometryHash: 0x", std::hex, associatedGeometryHash, std::dec, "\n",
      "  objectPickingValue: ", objectPickingValue, "\n",
      "  decalSortOrder: ", decalSortOrder));
    
    // Print buffer info
    Logger::warn("=== Buffer Info ===");
    Logger::warn(str::format(
      "  positionBufferIndex: ", positionBufferIndex, "\n",
      "  positionOffset: ", positionOffset, "\n",
      "  positionStride: ", positionStride, "\n",
      "  previousPositionBufferIndex: ", previousPositionBufferIndex, "\n",
      "  normalBufferIndex: ", normalBufferIndex, "\n",
      "  normalOffset: ", normalOffset, "\n",
      "  normalStride: ", normalStride, "\n",
      "  normalFormat: ", static_cast<int>(normalFormat), "\n",
      "  texcoordBufferIndex: ", texcoordBufferIndex, "\n",
      "  texcoordOffset: ", texcoordOffset, "\n",
      "  texcoordStride: ", texcoordStride, "\n",
      "  indexBufferIndex: ", indexBufferIndex, "\n",
      "  firstIndex: ", firstIndex, "\n",
      "  indexStride: ", indexStride, "\n",
      "  color0BufferIndex: ", color0BufferIndex, "\n",
      "  color0Offset: ", color0Offset, "\n",
      "  color0Stride: ", color0Stride));
    
    // Print boolean flags
    Logger::warn("=== Boolean Flags ===");
    Logger::warn(str::format(
      "  isEmissive: ", isEmissive, "\n",
      "  isMatte: ", isMatte, "\n",
      "  isStatic: ", isStatic, "\n",
      "  hasMaterialChanged: ", hasMaterialChanged, "\n",
      "  isAnimatedWater: ", isAnimatedWater, "\n",
      "  isClipPlaneEnabled: ", isClipPlaneEnabled, "\n",
      "  isTextureFactorBlend: ", isTextureFactorBlend, "\n",
      "  isMotionBlurMaskOut: ", isMotionBlurMaskOut, "\n",
      "  skipSurfaceInteractionSpritesheetAdjustment: ", skipSurfaceInteractionSpritesheetAdjustment, "\n",
      "  isInsideFrustum: ", isInsideFrustum, "\n",
      "  ignoreTransparencyLayer: ", ignoreTransparencyLayer));
    
    // Print alpha state
    Logger::warn("=== Alpha State ===");
    Logger::warn(str::format(
      "  isBlendingDisabled: ", alphaState.isBlendingDisabled, "\n",
      "  isFullyOpaque: ", alphaState.isFullyOpaque, "\n",
      "  alphaTestType: ", static_cast<int>(alphaState.alphaTestType), "\n",
      "  alphaTestReferenceValue: ", static_cast<int>(alphaState.alphaTestReferenceValue), "\n",
      "  blendType: ", static_cast<int>(alphaState.blendType), "\n",
      "  invertedBlend: ", alphaState.invertedBlend, "\n",
      "  emissiveBlend: ", alphaState.emissiveBlend, "\n",
      "  isParticle: ", alphaState.isParticle, "\n",
      "  isDecal: ", alphaState.isDecal));
    
    // Print texture operations
    Logger::warn("=== Texture Operations ===");
    Logger::warn(str::format(
      "  textureColorArg1Source: ", static_cast<int>(textureColorArg1Source), "\n",
      "  textureColorArg2Source: ", static_cast<int>(textureColorArg2Source), "\n",
      "  textureColorOperation: ", static_cast<int>(textureColorOperation), "\n",
      "  textureAlphaArg1Source: ", static_cast<int>(textureAlphaArg1Source), "\n",
      "  textureAlphaArg2Source: ", static_cast<int>(textureAlphaArg2Source), "\n",
      "  textureAlphaOperation: ", static_cast<int>(textureAlphaOperation), "\n",
      "  texgenMode: ", static_cast<int>(texgenMode), "\n",
      "  tFactor: 0x", std::hex, tFactor, std::dec));
    
    // Print spritesheet info
    Logger::warn("=== Spritesheet Info ===");
    Logger::warn(str::format(
      "  spriteSheetRows: ", static_cast<int>(spriteSheetRows), "\n",
      "  spriteSheetCols: ", static_cast<int>(spriteSheetCols), "\n",
      "  spriteSheetFPS: ", static_cast<int>(spriteSheetFPS)));
    
    // Print instance info
    Logger::warn("=== Instance Info ===");
    Logger::warn(str::format(
      "  instancesToObject: ", (instancesToObject != nullptr ? "valid" : "null"), "\n",
      "  surfaceIndexOfFirstInstance: ", surfaceIndexOfFirstInstance));
#endif
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
  } alphaState;

  // Original draw call state
  DxvkBlendMode blendModeState;

  // Static validation to detect any changes that require an alignment re-check
  static_assert(sizeof(AlphaState) == 9);

  Matrix4 objectToWorld;
  Matrix4 prevObjectToWorld;
  Matrix3 normalObjectToWorld;
  Matrix4 textureTransform;
  Vector4 clipPlane;

  uint8_t spriteSheetRows = 1;
  uint8_t spriteSheetCols = 1;
  uint8_t spriteSheetFPS = 0;

  XXH64_hash_t associatedGeometryHash; // NOTE: This is used for the debug view
  uint32_t objectPickingValue = 0; // NOTE: a value to fill GBUFFER_BINDING_PRIMARY_OBJECT_PICKING_OUTPUT
  uint32_t decalSortOrder = 0; // see: InstanceManager::m_decalSortOrderCounter

  // PointInstancer support - this surface may represent multiple instances, one for each transform in instancesToObject
  const std::vector<Matrix4>* instancesToObject = nullptr;
  // on the GPU, multiple copies of this surface with different transforms will exist.  They will be in a continuous block, starting at surfaceIndexOfFirstInstance.
  size_t surfaceIndexOfFirstInstance = SIZE_MAX;
};

// Shared Material Defaults/Limits

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
  RTX_OPTION("rtx.legacyMaterial", bool, ignoreAlphaChannel, false, "A flag to determine if the albedo alpha channel should be ignored on non-replaced \"legacy\" materials.");
  RTX_OPTION("rtx.legacyMaterial", bool, enableThinFilm, false, "A flag to determine if a thin-film layer should be used on non-replaced \"legacy\" materials.");
  RTX_OPTION("rtx.legacyMaterial", bool, alphaIsThinFilmThickness, false, "A flag to determine if the alpha channel from the albedo source should be treated as thin film thickness on non-replaced \"legacy\" materials.");
  // Note: Should be something non-zero as 0 is an invalid thickness to have (even if this is just unused).
  RTX_OPTION("rtx.legacyMaterial", float, thinFilmThicknessConstant, 200.f,
             "The thickness (in nanometers) of the thin-film layer assuming it is enabled on non-replaced \"legacy\" materials.\n"
             "Should be any value larger than 0, typically within the wavelength of light, but must be less than or equal to OPAQUE_SURFACE_MATERIAL_THIN_FILM_MAX_THICKNESS (" STRINGIFY(OPAQUE_SURFACE_MATERIAL_THIN_FILM_MAX_THICKNESS) " nm).");
};

// Surface Materials

// Todo: Compute size directly from sizeof of GPU structure (by including it), for now computed by sum of members manually.
// Blocked on float16 support on the c++ side.
constexpr std::size_t kSurfaceMaterialGPUSize = 4 * 4 * 4;
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

  // Extensions
  Subsurface,

  Count
};

// Todo: Legacy SurfaceMaterial in the future

struct RtOpaqueSurfaceMaterial {
  RtOpaqueSurfaceMaterial(
    uint32_t albedoOpacityTextureIndex, uint32_t normalTextureIndex,
    uint32_t tangentTextureIndex, uint32_t heightTextureIndex, uint32_t roughnessTextureIndex,
    uint32_t metallicTextureIndex, uint32_t emissiveColorTextureIndex,
    float anisotropy, float emissiveIntensity,
    const Vector4& albedoOpacityConstant,
    float roughnessConstant, float metallicConstant,
    const Vector3& emissiveColorConstant, bool enableEmission,
    bool ignoreAlphaChannel, bool enableThinFilm, bool alphaIsThinFilmThickness, float thinFilmThicknessConstant,
    uint32_t samplerIndex, float displaceIn, float displaceOut,
    uint32_t subsurfaceMaterialIndex, bool isRaytracedRenderTarget,
    uint16_t samplerFeedbackStamp
  ) :
    m_albedoOpacityTextureIndex{ albedoOpacityTextureIndex }, m_normalTextureIndex{ normalTextureIndex },
    m_tangentTextureIndex { tangentTextureIndex }, m_heightTextureIndex { heightTextureIndex }, m_roughnessTextureIndex{ roughnessTextureIndex },
    m_metallicTextureIndex{ metallicTextureIndex }, m_emissiveColorTextureIndex{ emissiveColorTextureIndex },
    m_anisotropy{ anisotropy }, m_emissiveIntensity{ emissiveIntensity },
    m_albedoOpacityConstant{ albedoOpacityConstant },
    m_roughnessConstant{ roughnessConstant }, m_metallicConstant{ metallicConstant },
    m_emissiveColorConstant{ emissiveColorConstant }, m_enableEmission{ enableEmission },
    m_ignoreAlphaChannel { ignoreAlphaChannel }, m_enableThinFilm { enableThinFilm }, m_alphaIsThinFilmThickness { alphaIsThinFilmThickness },
    m_thinFilmThicknessConstant { thinFilmThicknessConstant }, m_samplerIndex{ samplerIndex }, m_displaceIn{ displaceIn },
    m_displaceOut{ displaceOut }, m_subsurfaceMaterialIndex(subsurfaceMaterialIndex), m_isRaytracedRenderTarget(isRaytracedRenderTarget),
    m_samplerFeedbackStamp{ samplerFeedbackStamp }
  {
    updateCachedData();
    updateCachedHash();
  }

  void writeGPUData(unsigned char* data, std::size_t& offset) const {
    [[maybe_unused]] const std::size_t oldOffset = offset;
    uint16_t flags = surfaceMaterialTypeOpaque;

    // For decode process, see surface_material.h
    // this data is accessed from uint16_t data[32], so data[n] refers to a pair of bytes.

    if (m_enableThinFilm) {
      flags |= OPAQUE_SURFACE_MATERIAL_FLAG_USE_THIN_FILM_LAYER;

      // Note: Only consider setting alpha as thin film thickness flag if the thin film is enabled, GPU relies on
      // this logical ordering.
      if (m_alphaIsThinFilmThickness) {
        flags |= OPAQUE_SURFACE_MATERIAL_FLAG_ALPHA_IS_THIN_FILM_THICKNESS;
      }
    }

    if (m_ignoreAlphaChannel) {
      flags |= OPAQUE_SURFACE_MATERIAL_FLAG_IGNORE_ALPHA_CHANNEL;
    }
    // NOTE: We keep the most commonly used elements in the material close together near the beginning
    //       This hopefully reduces loads for cases like opacity detection.

    if (m_isRaytracedRenderTarget) {
      flags |= OPAQUE_SURFACE_MATERIAL_FLAG_IS_RAYTRACED_RENDER_TARGET;
    }

    float displaceIn = m_displaceIn * getDisplacementFactor();
    float displaceOut = m_displaceOut * getDisplacementFactor();
    uint32_t heightTextureIndex = m_heightTextureIndex;
    if (hasValidDisplacement()) {
      flags |= OPAQUE_SURFACE_MATERIAL_FLAG_HAS_DISPLACEMENT;
    } else {
      // If any POM attribute would disable POM, just disable all POM attributes.
      displaceIn = 0.f;
      displaceOut = 0.f;
      heightTextureIndex = BINDING_INDEX_INVALID;
    }
    assert(displaceIn <= FLOAT16_MAX);
    assert(displaceOut <= FLOAT16_MAX);

    // data[0 - 3]
    writeGPUHelper(data, offset, flags);
    writeGPUHelperExplicit<2>(data, offset, m_samplerIndex);
    writeGPUHelperExplicit<2>(data, offset, m_albedoOpacityTextureIndex);
    writeGPUHelperExplicit<2>(data, offset, m_subsurfaceMaterialIndex);

    // data[4 - 7]
    writeGPUHelper(data, offset, glm::packHalf1x16(m_albedoOpacityConstant.x));
    writeGPUHelper(data, offset, glm::packHalf1x16(m_albedoOpacityConstant.y));
    writeGPUHelper(data, offset, glm::packHalf1x16(m_albedoOpacityConstant.z));
    writeGPUHelper(data, offset, glm::packHalf1x16(m_albedoOpacityConstant.w));

    // data[8 - 11]
    writeGPUHelper(data, offset, glm::packHalf1x16(displaceIn));
    writeGPUHelper(data, offset, glm::packHalf1x16(displaceOut));
    writeGPUHelperExplicit<2>(data, offset, m_heightTextureIndex);
    writeGPUHelper(data, offset, glm::packHalf1x16(m_cachedThinFilmNormalizedThicknessConstant));

    // data[12 - 15]
    writeGPUHelperExplicit<2>(data, offset, m_emissiveColorTextureIndex);
    writeGPUHelperExplicit<2>(data, offset, m_roughnessTextureIndex);
    writeGPUHelperExplicit<2>(data, offset, m_metallicTextureIndex);
    writeGPUHelperExplicit<2>(data, offset, m_normalTextureIndex);

    // data[16 - 19]
    writeGPUHelper(data, offset, glm::packHalf1x16(m_emissiveColorConstant.x));
    writeGPUHelper(data, offset, glm::packHalf1x16(m_emissiveColorConstant.y));
    writeGPUHelper(data, offset, glm::packHalf1x16(m_emissiveColorConstant.z));
    assert(m_cachedEmissiveIntensity <= FLOAT16_MAX);
    writeGPUHelper(data, offset, glm::packHalf1x16(m_cachedEmissiveIntensity));

    // data[20 - 23]
    writeGPUHelper(data, offset, glm::packHalf1x16(m_roughnessConstant));
    writeGPUHelper(data, offset, glm::packHalf1x16(m_metallicConstant));
    writeGPUHelper(data, offset, glm::packHalf1x16(m_anisotropy));
    writeGPUHelperExplicit<2>(data, offset, m_tangentTextureIndex);

    // data[24]
    writeGPUHelperExplicit<2>(data, offset, m_samplerFeedbackStamp);

    // data[25 - 31]
    writeGPUPadding<14>(data, offset);
    assert(offset - oldOffset == kSurfaceMaterialGPUSize);
  }

  bool validate() const {
    const bool hasTexture = m_albedoOpacityTextureIndex != kSurfaceMaterialInvalidTextureIndex ||
                            m_normalTextureIndex != kSurfaceMaterialInvalidTextureIndex ||
                            m_tangentTextureIndex != kSurfaceMaterialInvalidTextureIndex ||
                            m_heightTextureIndex != kSurfaceMaterialInvalidTextureIndex ||
                            m_roughnessTextureIndex != kSurfaceMaterialInvalidTextureIndex ||
                            m_metallicTextureIndex != kSurfaceMaterialInvalidTextureIndex ||
                            m_emissiveColorTextureIndex != kSurfaceMaterialInvalidTextureIndex;

    return !hasTexture || m_samplerIndex != kSurfaceMaterialInvalidTextureIndex;
  }

  bool hasValidDisplacement() const {
    return (m_displaceIn > 0.f || m_displaceOut > 0.f) && m_heightTextureIndex != BINDING_INDEX_INVALID;
  }

  bool operator==(const RtOpaqueSurfaceMaterial& r) const {
    return m_cachedHash == r.m_cachedHash;
  }

  XXH64_hash_t getHash() const {
    return m_cachedHash;
  }

  uint32_t getSamplerIndex() const {
    return m_samplerIndex;
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

  uint32_t getHeightTextureIndex() const {
    return m_heightTextureIndex;
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

  uint32_t getSubsurfaceMaterialIndex() const {
    return m_subsurfaceMaterialIndex;
  }

  uint32_t getIsRaytracedRenderTarget() const {
    return m_isRaytracedRenderTarget;
  }

private:
  void updateCachedHash() {
    XXH64_hash_t h = 0;

    h = XXH64(&m_albedoOpacityTextureIndex, sizeof(m_albedoOpacityTextureIndex), h);
    h = XXH64(&m_normalTextureIndex, sizeof(m_normalTextureIndex), h);
    h = XXH64(&m_tangentTextureIndex, sizeof(m_tangentTextureIndex), h);
    h = XXH64(&m_heightTextureIndex, sizeof(m_heightTextureIndex), h);
    h = XXH64(&m_roughnessTextureIndex, sizeof(m_roughnessTextureIndex), h);
    h = XXH64(&m_metallicTextureIndex, sizeof(m_metallicTextureIndex), h);
    h = XXH64(&m_emissiveColorTextureIndex, sizeof(m_emissiveColorTextureIndex), h);
    h = XXH64(&m_anisotropy, sizeof(m_anisotropy), h);
    h = XXH64(&m_emissiveIntensity, sizeof(m_emissiveIntensity), h);
    h = XXH64(&m_albedoOpacityConstant, sizeof(m_albedoOpacityConstant), h);
    h = XXH64(&m_roughnessConstant, sizeof(m_roughnessConstant), h);
    h = XXH64(&m_metallicConstant, sizeof(m_metallicConstant), h);
    h = XXH64(&m_emissiveColorConstant, sizeof(m_emissiveColorConstant), h);
    h = XXH64(&m_enableEmission, sizeof(m_enableEmission), h);
    h = XXH64(&m_ignoreAlphaChannel, sizeof(m_ignoreAlphaChannel), h);
    h = XXH64(&m_enableThinFilm, sizeof(m_enableThinFilm), h);
    h = XXH64(&m_alphaIsThinFilmThickness, sizeof(m_alphaIsThinFilmThickness), h);
    h = XXH64(&m_thinFilmThicknessConstant, sizeof(m_thinFilmThicknessConstant), h);
    h = XXH64(&m_samplerIndex, sizeof(m_samplerIndex), h);
    h = XXH64(&m_displaceIn, sizeof(m_displaceIn), h);
    h = XXH64(&m_displaceOut, sizeof(m_displaceOut), h);
    h = XXH64(&m_subsurfaceMaterialIndex, sizeof(m_subsurfaceMaterialIndex), h);
    h = XXH64(&m_isRaytracedRenderTarget, sizeof(m_isRaytracedRenderTarget), h);
    h = XXH64(&m_samplerFeedbackStamp, sizeof(m_samplerFeedbackStamp), h);

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
  uint32_t m_heightTextureIndex;
  uint32_t m_roughnessTextureIndex;
  uint32_t m_metallicTextureIndex;
  uint32_t m_emissiveColorTextureIndex;
  uint32_t m_samplerIndex;

  float m_anisotropy;
  float m_emissiveIntensity;

  Vector4 m_albedoOpacityConstant;
  float m_roughnessConstant;
  float m_metallicConstant;
  Vector3 m_emissiveColorConstant;

  bool m_enableEmission;

  bool m_ignoreAlphaChannel;
  bool m_enableThinFilm;
  bool m_alphaIsThinFilmThickness;
  float m_thinFilmThicknessConstant;

  // How far inwards a height_texture value of 0 maps to.
  float m_displaceIn;
  // How far outwards a height_texture value of 1 maps to.
  float m_displaceOut;

  uint32_t m_subsurfaceMaterialIndex;

  bool m_isRaytracedRenderTarget;

  uint16_t m_samplerFeedbackStamp;

  XXH64_hash_t m_cachedHash;

  // Note: Cached values are not involved in the hash as they are derived from the input data
  float m_cachedEmissiveIntensity;
  float m_cachedThinFilmNormalizedThicknessConstant;
};

struct RtTranslucentSurfaceMaterial {
  RtTranslucentSurfaceMaterial(
    uint32_t normalTextureIndex,
    uint32_t transmittanceTextureIndex,
    uint32_t emissiveColorTextureIndex,
    float refractiveIndex,
    float transmittanceMeasurementDistance, const Vector3& transmittanceColor,
    bool enableEmission, float emissiveIntensity, const Vector3& emissiveColorConstant,
    bool isThinWalled, float thinWallThickness, bool useDiffuseLayer, uint32_t samplerIndex) :
    m_normalTextureIndex(normalTextureIndex),
    m_transmittanceTextureIndex(transmittanceTextureIndex),
    m_emissiveColorTextureIndex(emissiveColorTextureIndex),
    m_refractiveIndex(refractiveIndex),
    m_transmittanceMeasurementDistance(transmittanceMeasurementDistance), m_transmittanceColor(transmittanceColor),
    m_enableEmission(enableEmission), m_emissiveIntensity(emissiveIntensity), m_emissiveColorConstant(emissiveColorConstant),
    m_isThinWalled(isThinWalled), m_thinWallThickness(thinWallThickness), m_useDiffuseLayer(useDiffuseLayer), m_samplerIndex(samplerIndex)
  {
    updateCachedData();
    updateCachedHash();
  }

  void writeGPUData(unsigned char* data, std::size_t& offset, uint16_t surfaceIndex) const {
    [[maybe_unused]] const std::size_t oldOffset = offset;

    // For decode process, see surface_material.h
    // this data is accessed from uint16_t data[32], so data[n] refers to a pair of bytes.

    uint16_t flags = surfaceMaterialTypeTranslucent;

    // Note: Respect override flag here to let the GPU do less work in determining if the diffuse layer should be used or not.
    if (m_useDiffuseLayer || getEnableDiffuseLayerOverrideHack()) {
      flags |= TRANSLUCENT_SURFACE_MATERIAL_FLAG_USE_DIFFUSE_LAYER;
    }

    // data[0- 1]
    writeGPUHelper(data, offset, flags);
    writeGPUHelper(data, offset, glm::packHalf1x16(m_cachedBaseReflectivity));
    // data[2 - 4]
    writeGPUHelper(data, offset, glm::packHalf1x16(m_transmittanceColor.x));
    writeGPUHelper(data, offset, glm::packHalf1x16(m_transmittanceColor.y));
    writeGPUHelper(data, offset, glm::packHalf1x16(m_transmittanceColor.z));
    // data[5 - 9]
    writeGPUHelperExplicit<2>(data, offset, m_samplerIndex);
    writeGPUHelperExplicit<2>(data, offset, m_transmittanceTextureIndex);
    writeGPUHelper(data, offset, glm::packHalf1x16(m_cachedTransmittanceMeasurementDistanceOrThickness));
    writeGPUHelperExplicit<2>(data, offset, m_normalTextureIndex);
    writeGPUHelperExplicit<2>(data, offset, m_emissiveColorTextureIndex);

    // data[10]
    assert(m_cachedEmissiveIntensity <= FLOAT16_MAX);
    writeGPUHelper(data, offset, glm::packHalf1x16(m_cachedEmissiveIntensity));

    // data[11]
    // Note: Ensure IoR falls in the range expected by the encoding/decoding logic for the GPU (this should also be
    // enforced in the MDL and relevant content pipeline to prevent this assert from being triggered).
    assert(m_refractiveIndex >= 1.0f && m_refractiveIndex <= 3.0f);
    writeGPUHelper(data, offset, glm::packHalf1x16(m_refractiveIndex));

    // data[12-14]
    writeGPUHelper(data, offset, glm::packHalf1x16(m_emissiveColorConstant.x));
    writeGPUHelper(data, offset, glm::packHalf1x16(m_emissiveColorConstant.y));
    writeGPUHelper(data, offset, glm::packHalf1x16(m_emissiveColorConstant.z));

    // data[15]
    writeGPUHelperExplicit<2>(data, offset, surfaceIndex);
    
    // data[16 - 31]
    writeGPUPadding<32>(data, offset);

    assert(offset - oldOffset == kSurfaceMaterialGPUSize);
  }

  bool validate() const {
    const bool hasTexture = m_normalTextureIndex != kSurfaceMaterialInvalidTextureIndex ||
                            m_transmittanceTextureIndex != kSurfaceMaterialInvalidTextureIndex ||
                            m_emissiveColorTextureIndex != kSurfaceMaterialInvalidTextureIndex;

    return !hasTexture || m_samplerIndex != kSurfaceMaterialInvalidTextureIndex;
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
    h = XXH64(&m_transmittanceTextureIndex, sizeof(m_transmittanceTextureIndex), h);
    h = XXH64(&m_emissiveColorTextureIndex, sizeof(m_emissiveColorTextureIndex), h);
    h = XXH64(&m_refractiveIndex, sizeof(m_refractiveIndex), h);
    h = XXH64(&m_transmittanceColor, sizeof(m_transmittanceColor), h);
    h = XXH64(&m_transmittanceMeasurementDistance, sizeof(m_transmittanceMeasurementDistance), h);
    h = XXH64(&m_enableEmission, sizeof(m_enableEmission), h);
    h = XXH64(&m_emissiveIntensity, sizeof(m_emissiveIntensity), h);
    h = XXH64(&m_emissiveColorConstant, sizeof(m_emissiveColorConstant), h);
    h = XXH64(&m_isThinWalled, sizeof(m_isThinWalled), h);
    h = XXH64(&m_thinWallThickness, sizeof(m_thinWallThickness), h);
    h = XXH64(&m_useDiffuseLayer, sizeof(m_useDiffuseLayer), h);
    h = XXH64(&m_samplerIndex, sizeof(m_samplerIndex), h);

    m_cachedHash = h;
  }

  void updateCachedData() {
    // Note: Based on the Fresnel Equations with the assumption of a vacuum (nearly air
    // as the surrounding medium always) and an IoR of always >=1 (implicitly ensured by encoding
    // logic assertions later):
    // https://en.wikipedia.org/wiki/Fresnel_equations#Special_cases
    const float x = (1.0f - m_refractiveIndex) / (1.0f + m_refractiveIndex);

    m_cachedBaseReflectivity = x * x;
    m_cachedTransmittanceMeasurementDistanceOrThickness =
      m_isThinWalled ? -m_thinWallThickness : m_transmittanceMeasurementDistance;

    // Note: Translucent material does not take an emissive radiance directly, so zeroing out the intensity works
    // fine as a way to disable it (in case a texture is in use).
    m_cachedEmissiveIntensity = std::min(m_enableEmission ? m_emissiveIntensity : 0.0f, FLOAT16_MAX);

    // Note: Ensure the transmittance measurement distance or thickness was encoded properly by ensuring
    // it is not 0. This is because we currently do not actually check the sign bit but just use a less than
    // comparison to check the sign bit as neither of these values should be 0 in valid materials.
    assert(m_cachedTransmittanceMeasurementDistanceOrThickness != 0.0f);
  }

  uint32_t m_normalTextureIndex;
  uint32_t m_transmittanceTextureIndex;
  uint32_t m_emissiveColorTextureIndex;
  uint32_t m_samplerIndex;

  float m_refractiveIndex;
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
  float m_cachedEmissiveIntensity;
};

struct RtRayPortalSurfaceMaterial {
  RtRayPortalSurfaceMaterial(
    uint32_t maskTextureIndex, uint32_t maskTextureIndex2, uint8_t rayPortalIndex,
    float rotationSpeed, bool enableEmission, float emissiveIntensity, uint32_t samplerIndex, uint32_t samplerIndex2) :
    m_maskTextureIndex{ maskTextureIndex }, m_maskTextureIndex2 { maskTextureIndex2 }, m_rayPortalIndex{ rayPortalIndex },
    m_rotationSpeed { rotationSpeed }, m_enableEmission(enableEmission), m_emissiveIntensity(emissiveIntensity), m_samplerIndex(samplerIndex), m_samplerIndex2(samplerIndex2) {
    updateCachedHash();
  }

  void writeGPUData(unsigned char* data, std::size_t& offset) const {
    [[maybe_unused]] const std::size_t oldOffset = offset;

    // For decode process, see surface_material.h
    // this data is accessed from uint16_t data[32], so data[n] refers to a pair of bytes.

    uint16_t flags = surfaceMaterialTypeRayPortal;
    // data[0]
    writeGPUHelper(data, offset, flags);

    // data[1]
    writeGPUHelper(data, offset, uint16_t(m_rayPortalIndex));

    // data[2 - 3]
    writeGPUHelperExplicit<2>(data, offset, m_maskTextureIndex);
    writeGPUHelperExplicit<2>(data, offset, m_maskTextureIndex2);

    // data[4 - 5]
    assert(m_rotationSpeed < FLOAT16_MAX);
    writeGPUHelper(data, offset, glm::packHalf1x16(m_rotationSpeed));
    float emissiveIntensity = m_enableEmission ? m_emissiveIntensity : 1.0f;
    writeGPUHelper(data, offset, glm::packHalf1x16(emissiveIntensity));

    // data[6 - 7]
    writeGPUHelperExplicit<2>(data, offset, m_samplerIndex);
    writeGPUHelperExplicit<2>(data, offset, m_samplerIndex2);

    // data[8 - 31]
    writeGPUPadding<48>(data, offset); // Note: Padding for unused space
    assert(offset - oldOffset == kSurfaceMaterialGPUSize);
  }

  bool validate() const {
    if (m_maskTextureIndex != kSurfaceMaterialInvalidTextureIndex && m_samplerIndex == kSurfaceMaterialInvalidTextureIndex) {
      return false;
    }

    if (m_maskTextureIndex2 != kSurfaceMaterialInvalidTextureIndex && m_samplerIndex2 == kSurfaceMaterialInvalidTextureIndex) {
      return false;
    }

    return true;
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

  uint32_t getSamplerIndex() const {
    return m_samplerIndex;
  }

  uint32_t getSamplerIndex2() const {
    return m_samplerIndex2;
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

private:
  void updateCachedHash() {
    XXH64_hash_t h = 0;

    h = XXH64(&m_maskTextureIndex, sizeof(m_maskTextureIndex), h);
    h = XXH64(&m_maskTextureIndex2, sizeof(m_maskTextureIndex2), h);
    h = XXH64(&m_rayPortalIndex, sizeof(m_rayPortalIndex), h);
    h = XXH64(&m_rotationSpeed, sizeof(m_rotationSpeed), h);
    h = XXH64(&m_enableEmission, sizeof(m_enableEmission), h);
    h = XXH64(&m_emissiveIntensity, sizeof(m_emissiveIntensity), h);
    h = XXH64(&m_samplerIndex, sizeof(m_samplerIndex), h);
    h = XXH64(&m_samplerIndex2, sizeof(m_samplerIndex2), h);

    m_cachedHash = h;
  }

  uint32_t m_maskTextureIndex;
  uint32_t m_maskTextureIndex2;
  uint32_t m_samplerIndex;
  uint32_t m_samplerIndex2;

  uint8_t m_rayPortalIndex;
  float m_rotationSpeed;
  bool m_enableEmission;
  float m_emissiveIntensity;

  XXH64_hash_t m_cachedHash;
};

// Extension of the three basic types of materials.
// Don't use material types below standalone. Instead, attach them to the materials above as side load data.

// Subsurface Material
struct RtSubsurfaceMaterial {
  RtSubsurfaceMaterial(
    const uint32_t subsurfaceTransmittanceTextureIndex,
    const uint32_t subsurfaceThicknessTextureIndex,
    const uint32_t subsurfaceSingleScatteringAlbedoTextureIndex,
    const Vector3& subsurfaceTransmittanceColor,
    const float subsurfaceMeasurementDistance,
    const Vector3& subsurfaceSingleScatteringAlbedo,
    const float subsurfaceVolumetricAnisotropy,
    const float subsurfaceRadiusScale,
    const float subsurfaceMaxSampleRadius)
    :
    m_subsurfaceTransmittanceTextureIndex(subsurfaceTransmittanceTextureIndex),
    m_subsurfaceThicknessTextureIndex(subsurfaceThicknessTextureIndex),
    m_subsurfaceSingleScatteringAlbedoTextureIndex(subsurfaceSingleScatteringAlbedoTextureIndex),
    m_subsurfaceTransmittanceColor { subsurfaceTransmittanceColor },
    m_subsurfaceMeasurementDistance { subsurfaceMeasurementDistance },
    m_subsurfaceSingleScatteringAlbedo { subsurfaceSingleScatteringAlbedo },
    m_subsurfaceVolumetricAnisotropy { subsurfaceVolumetricAnisotropy },
    // Because we do log on the transmittance color when mapping to attenuation coefficient, we need to clamp to a small epsilon value to avoid NaN issue.
    m_subsurfaceVolumetricAttenuationCoefficient {
      Vector3(-log(std::max(subsurfaceTransmittanceColor.x, FLT_EPSILON)),
              -log(std::max(subsurfaceTransmittanceColor.y, FLT_EPSILON)),
              -log(std::max(subsurfaceTransmittanceColor.z, FLT_EPSILON))) / std::max(subsurfaceMeasurementDistance, FLT_EPSILON) },
    m_subsurfaceRadiusScale { subsurfaceRadiusScale },
    m_subsurfaceMaxSampleRadius { subsurfaceMaxSampleRadius }
  {
    updateCachedHash();
  }

  void writeGPUData(unsigned char* data, std::size_t& offset) const {
    [[maybe_unused]] const std::size_t oldOffset = offset;

    // For decode process, see surface_material.h
    // this data is accessed from uint16_t data[32], so data[n] refers to a pair of bytes.

    // Write an empty flags to stay consistent with the other materials.
    uint16_t flags = 0;

    // data[0]
    writeGPUHelperExplicit<2>(data, offset, flags);

    // data[1]
    writeGPUHelperExplicit<2>(data, offset, m_subsurfaceTransmittanceTextureIndex);

    // data[2]
    writeGPUHelperExplicit<2>(data, offset, m_subsurfaceThicknessTextureIndex);

    // data[3]
    writeGPUHelperExplicit<2>(data, offset, m_subsurfaceSingleScatteringAlbedoTextureIndex);

    // data[4]
    writeGPUHelper(data, offset, glm::packHalf1x16(m_subsurfaceVolumetricAnisotropy));

    // data[5-8]
    if (m_subsurfaceRadiusScale < 0.0f) { // Thin Opaque
      writeGPUHelper(data, offset, glm::packHalf1x16(m_subsurfaceVolumetricAttenuationCoefficient.x));
      writeGPUHelper(data, offset, glm::packHalf1x16(m_subsurfaceVolumetricAttenuationCoefficient.y));
      writeGPUHelper(data, offset, glm::packHalf1x16(m_subsurfaceVolumetricAttenuationCoefficient.z));

      assert(m_subsurfaceMeasurementDistance <= FLOAT16_MAX);
      writeGPUHelper(data, offset, glm::packHalf1x16(m_subsurfaceMeasurementDistance));
    } else { // SSS
      writeGPUHelper(data, offset, glm::packHalf1x16(m_subsurfaceTransmittanceColor.x));
      writeGPUHelper(data, offset, glm::packHalf1x16(m_subsurfaceTransmittanceColor.y));
      writeGPUHelper(data, offset, glm::packHalf1x16(m_subsurfaceTransmittanceColor.z));

      assert(m_subsurfaceRadiusScale <= FLOAT16_MAX);
      writeGPUHelper(data, offset, glm::packHalf1x16(m_subsurfaceRadiusScale));
    }

    // data[9-11]
    writeGPUHelper(data, offset, glm::packHalf1x16(m_subsurfaceSingleScatteringAlbedo.x));
    writeGPUHelper(data, offset, glm::packHalf1x16(m_subsurfaceSingleScatteringAlbedo.y));
    writeGPUHelper(data, offset, glm::packHalf1x16(m_subsurfaceSingleScatteringAlbedo.z));

    // data[12]
    writeGPUHelper(data, offset, glm::packHalf1x16(m_subsurfaceMaxSampleRadius));

    // data[13-31]
    writeGPUPadding<38>(data, offset);
  }

  bool operator==(const RtSubsurfaceMaterial& r) const {
    return m_cachedHash == r.m_cachedHash;
  }

  bool validate() const {
    return true;
  }

  XXH64_hash_t getHash() const {
    return m_cachedHash;
  }

  uint32_t getSubsurfaceTransmittanceTextureIndex() const {
    return m_subsurfaceTransmittanceTextureIndex;
  }

  uint32_t getSubsurfaceThicknessTextureIndex() const {
    return m_subsurfaceThicknessTextureIndex;
  }

  uint32_t getSubsurfaceSingleScatteringAlbedoTextureIndex() const {
    return m_subsurfaceSingleScatteringAlbedoTextureIndex;
  }

  float getSubsurfaceMeasurementDistance() const {
    return m_subsurfaceMeasurementDistance;
  }

  const Vector3& getSubsurfaceVolumetricScatteringAlbedo() const {
    return m_subsurfaceSingleScatteringAlbedo;
  }

  float getSubsurfaceVolumetricAnisotropy() const {
    return m_subsurfaceVolumetricAnisotropy;
  }

  const Vector3& getSubsurfaceVolumetricAttenuationCoefficient() const {
    return m_subsurfaceVolumetricAttenuationCoefficient;
  }

  float getSubsurfaceRadiusScale() const {
    return m_subsurfaceRadiusScale;
  }

  float getSubsurfaceMaxRadius() const {
    return m_subsurfaceMaxSampleRadius;
  }

private:
  struct HashStruct {
    uint32_t m_subsurfaceTransmittanceTextureIndex;
    uint32_t m_subsurfaceThicknessTextureIndex;
    uint32_t m_subsurfaceSingleScatteringAlbedoTextureIndex;
    Vector3 m_subsurfaceTransmittanceColor;
    float m_subsurfaceMeasurementDistance;
    Vector3 m_subsurfaceSingleScatteringAlbedo;
    float m_subsurfaceVolumetricAnisotropy;
    Vector3 m_subsurfaceVolumetricAttenuationCoefficient;
    float m_subsurfaceRadiusScale;
    float m_subsurfaceMaxSampleRadius;

    XXH64_hash_t calculateHash() {
      static_assert(sizeof(HashStruct) == sizeof(uint32_t) * 16);
      return XXH3_64bits(this, sizeof(HashStruct));
    }
  };

  void updateCachedHash() {
    HashStruct hashData = {
      m_subsurfaceTransmittanceTextureIndex,
      m_subsurfaceThicknessTextureIndex,
      m_subsurfaceSingleScatteringAlbedoTextureIndex,
      m_subsurfaceTransmittanceColor,
      m_subsurfaceMeasurementDistance,
      m_subsurfaceSingleScatteringAlbedo,
      m_subsurfaceVolumetricAnisotropy,
      m_subsurfaceVolumetricAttenuationCoefficient,
      m_subsurfaceRadiusScale,
      m_subsurfaceMaxSampleRadius };
    m_cachedHash = hashData.calculateHash();
  }

  // Thin Opaque Textures Index (Shared with SSS)
  uint32_t m_subsurfaceTransmittanceTextureIndex;
  uint32_t m_subsurfaceThicknessTextureIndex;
  uint32_t m_subsurfaceSingleScatteringAlbedoTextureIndex;

  // Thin Opaque Properties (Shared with SSS)
  Vector3 m_subsurfaceTransmittanceColor;
  float m_subsurfaceMeasurementDistance;
  Vector3 m_subsurfaceSingleScatteringAlbedo; // scatteringCoefficient / attenuationCoefficient
  float m_subsurfaceVolumetricAnisotropy;

  // Cache Volumetric Properties
  Vector3 m_subsurfaceVolumetricAttenuationCoefficient; // scatteringCoefficient + absorptionCoefficient
  // Currently no need to cache scattering and absorption coefficient for single scattering simulation

  // SSS properties using Diffusion Profile
  float m_subsurfaceRadiusScale;
  float m_subsurfaceMaxSampleRadius;

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

  RtSurfaceMaterial(const RtSubsurfaceMaterial& subsurfaceMaterial) :
    m_type { RtSurfaceMaterialType::Subsurface },
    m_subsurfaceMaterial { subsurfaceMaterial } {}

  RtSurfaceMaterial(const RtSurfaceMaterial& surfaceMaterial) :
    m_type{ surfaceMaterial.m_type } {
    switch (m_type) {
    default:
      assert(false);

      [[fallthrough]];
    case RtSurfaceMaterialType::Opaque:
      new (&m_opaqueSurfaceMaterial) RtOpaqueSurfaceMaterial{ surfaceMaterial.m_opaqueSurfaceMaterial };
      break;
    case RtSurfaceMaterialType::Translucent:
      new (&m_translucentSurfaceMaterial) RtTranslucentSurfaceMaterial{ surfaceMaterial.m_translucentSurfaceMaterial };
      break;
    case RtSurfaceMaterialType::RayPortal:
      new (&m_rayPortalSurfaceMaterial) RtRayPortalSurfaceMaterial{ surfaceMaterial.m_rayPortalSurfaceMaterial };
      break;
    case RtSurfaceMaterialType::Subsurface:
      new (&m_subsurfaceMaterial) RtSubsurfaceMaterial { surfaceMaterial.m_subsurfaceMaterial };
      break;
    }
  }

  ~RtSurfaceMaterial() {
    switch (m_type) {
    default:
      assert(false);

      [[fallthrough]];
    case RtSurfaceMaterialType::Opaque:
      m_opaqueSurfaceMaterial.~RtOpaqueSurfaceMaterial();
      break;
    case RtSurfaceMaterialType::Translucent:
      m_translucentSurfaceMaterial.~RtTranslucentSurfaceMaterial();
      break;
    case RtSurfaceMaterialType::RayPortal:
      m_rayPortalSurfaceMaterial.~RtRayPortalSurfaceMaterial();
      break;
    case RtSurfaceMaterialType::Subsurface:
      m_subsurfaceMaterial.~RtSubsurfaceMaterial();
      break;
    }
  }

  void writeGPUData(unsigned char* data, std::size_t& offset, uint16_t surfaceIndex) const {
    switch (m_type) {
    default:
      assert(false);

      [[fallthrough]];
    case RtSurfaceMaterialType::Opaque:
      m_opaqueSurfaceMaterial.writeGPUData(data, offset);
      break;
    case RtSurfaceMaterialType::Translucent:
      m_translucentSurfaceMaterial.writeGPUData(data, offset, surfaceIndex);
      break;
    case RtSurfaceMaterialType::RayPortal:
      m_rayPortalSurfaceMaterial.writeGPUData(data, offset);
      break;
    case RtSurfaceMaterialType::Subsurface:
      m_subsurfaceMaterial.writeGPUData(data, offset);
      break;
    }
  }

  bool validate() const {
    switch (m_type) {
    default:
      assert(false);

      [[fallthrough]];
    case RtSurfaceMaterialType::Opaque:
      return m_opaqueSurfaceMaterial.validate();
    case RtSurfaceMaterialType::Translucent:
      return m_translucentSurfaceMaterial.validate();
    case RtSurfaceMaterialType::RayPortal:
      return m_rayPortalSurfaceMaterial.validate();
    case RtSurfaceMaterialType::Subsurface:
      return m_subsurfaceMaterial.validate();
    }

    return false;
  }

  RtSurfaceMaterial& operator=(const RtSurfaceMaterial& rtSurfaceMaterial) {
    if (this != &rtSurfaceMaterial) {
      m_type = rtSurfaceMaterial.m_type;

      switch (rtSurfaceMaterial.m_type) {
      default:
        assert(false);

        [[fallthrough]];
      case RtSurfaceMaterialType::Opaque:
        m_opaqueSurfaceMaterial = rtSurfaceMaterial.m_opaqueSurfaceMaterial;
        break;
      case RtSurfaceMaterialType::Translucent:
        m_translucentSurfaceMaterial = rtSurfaceMaterial.m_translucentSurfaceMaterial;
        break;
      case RtSurfaceMaterialType::RayPortal:
        m_rayPortalSurfaceMaterial = rtSurfaceMaterial.m_rayPortalSurfaceMaterial;
        break;
      case RtSurfaceMaterialType::Subsurface:
        m_subsurfaceMaterial = rtSurfaceMaterial.m_subsurfaceMaterial;
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

      [[fallthrough]];
    case RtSurfaceMaterialType::Opaque:
      return m_opaqueSurfaceMaterial == rhs.m_opaqueSurfaceMaterial;
    case RtSurfaceMaterialType::Translucent:
      return m_translucentSurfaceMaterial == rhs.m_translucentSurfaceMaterial;
    case RtSurfaceMaterialType::RayPortal:
      return m_rayPortalSurfaceMaterial == rhs.m_rayPortalSurfaceMaterial;
    case RtSurfaceMaterialType::Subsurface:
      return m_subsurfaceMaterial == rhs.m_subsurfaceMaterial;
    }
  }

  XXH64_hash_t getHash() const {
    switch (m_type) {
    default:
      assert(false);

      [[fallthrough]];
    case RtSurfaceMaterialType::Opaque:
      return m_opaqueSurfaceMaterial.getHash();
    case RtSurfaceMaterialType::Translucent:
      return m_translucentSurfaceMaterial.getHash();
    case RtSurfaceMaterialType::RayPortal:
      return m_rayPortalSurfaceMaterial.getHash();
    case RtSurfaceMaterialType::Subsurface:
      return m_subsurfaceMaterial.getHash();
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
    RtSubsurfaceMaterial m_subsurfaceMaterial;
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
  Opaque,
  Translucent,
  RayPortal,
  Count,
  Invalid
};

// Note: For use with "Legacy" D3D9 material information
struct LegacyMaterialData {
  static OpaqueMaterialData createDefault();

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

  const Rc<DxvkSampler>& getSampler() const {
    return samplers[0];
  }

  const Rc<DxvkSampler>& getSampler2() const {
    return samplers[1];
  }

  const D3DMATERIAL9& getLegacyMaterial() const {
    return d3dMaterial;
  }

  inline const bool usesTexture() const {
    return ((getColorTexture().isValid()  && !getColorTexture().isImageEmpty()) ||
            (getColorTexture2().isValid() && !getColorTexture2().isImageEmpty()));
  }

  // A single place to define and handle conversions between legacy and raytraced materials
  template<typename T>
  T as() const;

  const void printDebugInfo(const char* name = "") const {
#ifdef REMIX_DEVELOPMENT
    Logger::warn(str::format(
      "LegacyMaterialData ", name, "\n",
      "  address: ", this, "\n",
      "  alphaTestEnabled: ", alphaTestEnabled, "\n",
      "  alphaTestReferenceValue: ", alphaTestReferenceValue, "\n",
      "  alphaTestCompareOp: ", alphaTestCompareOp, "\n",
      "  alphaBlendEnabled: ", blendMode.enableBlending, "\n",
      "  colorSrcFactor: ", blendMode.colorSrcFactor, "\n",
      "  colorDstFactor: ", blendMode.colorDstFactor, "\n",
      "  colorBlendOp: ", blendMode.colorBlendOp, "\n",
      "  alphaSrcFactor: ", blendMode.alphaSrcFactor, "\n",
      "  alphaDstFactor: ", blendMode.alphaDstFactor, "\n",
      "  alphaBlendOp: ", blendMode.alphaBlendOp, "\n",
      "  writeMask: ", blendMode.writeMask, "\n",
      "  textureColorArg1Source: ", static_cast<int>(textureColorArg1Source), "\n",
      "  textureColorArg2Source: ", static_cast<int>(textureColorArg2Source), "\n",
      "  textureColorOperation: ", static_cast<int>(textureColorOperation), "\n",
      "  textureAlphaArg1Source: ", static_cast<int>(textureAlphaArg1Source), "\n",
      "  textureAlphaArg2Source: ", static_cast<int>(textureAlphaArg2Source), "\n",
      "  textureAlphaOperation: ", static_cast<int>(textureAlphaOperation), "\n",
      "  tFactor: ", tFactor, "\n",
      // "  m_d3dMaterial.Diffuse: ", m_d3dMaterial.Diffuse, "\n",
      // "  m_d3dMaterial.Ambient: ", m_d3dMaterial.Ambient, "\n",
      // "  m_d3dMaterial.Specular: ", m_d3dMaterial.Specular, "\n",
      // "  m_d3dMaterial.Emissive: ", m_d3dMaterial.Emissive, "\n",
      // "  m_d3dMaterial.Power: ", m_d3dMaterial.Power, "\n",
      std::hex, "  m_colorTexture: 0x", colorTextures[0].getImageHash(), "\n",
      "  m_colorTexture2: 0x", colorTextures[1].getImageHash(), "\n",
      "  m_cachedHash: 0x", m_cachedHash, std::dec));
#endif
  }

  uint32_t getColorTextureSlot(uint32_t slot) const {
    return colorTextureSlot[slot];
  }

  bool alphaTestEnabled = false;
  uint8_t alphaTestReferenceValue = 0;
  VkCompareOp alphaTestCompareOp = VkCompareOp::VK_COMPARE_OP_ALWAYS;

  DxvkBlendMode blendMode;

  RtTextureArgSource diffuseColorSource= RtTextureArgSource::None;
  RtTextureArgSource specularColorSource = RtTextureArgSource::None;
  RtTextureArgSource textureColorArg1Source = RtTextureArgSource::Texture;
  RtTextureArgSource textureColorArg2Source = RtTextureArgSource::None;
  DxvkRtTextureOperation textureColorOperation = DxvkRtTextureOperation::Modulate;
  RtTextureArgSource textureAlphaArg1Source = RtTextureArgSource::Texture;
  RtTextureArgSource textureAlphaArg2Source = RtTextureArgSource::None;
  DxvkRtTextureOperation textureAlphaOperation = DxvkRtTextureOperation::SelectArg1;
  uint32_t tFactor = 0xffffffff;  // Value for D3DRS_TEXTUREFACTOR, default value of is opaque white
  D3DMATERIAL9 d3dMaterial = {};
  bool isTextureFactorBlend = false;

  void setHashOverride(XXH64_hash_t hash) {
    m_cachedHash = hash;
  }

private:
  friend class RtxContext;
  friend struct D3D9Rtx;
  friend class TerrainBaker;
  friend class SceneManager;
  friend struct RemixAPIPrivateAccessor;

  void updateCachedHash() {
    // Note: Currently only based on the color texture's data hash. This may have to be changed later to
    // incorporate more textures used to identify a material uniquely. Note this is not the same as the
    // plain data hash used by the RtSurfaceMaterial for storage in map-like data structures, but rather
    // one used to identify a material and compare to user-provided hashes.
    m_cachedHash = colorTextures[0].getImageHash();
  }

  const static uint32_t kMaxSupportedTextures = 2;
  TextureRef colorTextures[kMaxSupportedTextures] = {};
  Rc<DxvkSampler> samplers[kMaxSupportedTextures] = {};
  static_assert(kInvalidResourceSlot == 0 && "Below initialization of all array members is only valid for a value of 0.");
  uint32_t colorTextureSlot[kMaxSupportedTextures] = { kInvalidResourceSlot };

  XXH64_hash_t m_cachedHash = kEmptyHash;
};

struct MaterialData {
  bool m_ignored = false;

  using MaterialVariant = std::variant<
    OpaqueMaterialData,
    TranslucentMaterialData,
    RayPortalMaterialData
  >;

  // Using variants rather than a union here, due to the MaterialData containing nested members of Rc pointers.
  MaterialVariant m_data;

  std::optional<RtxParticleSystemDesc> m_particleSystem;

  // Verify that the variant and enum stay in sync
  static_assert(std::variant_size_v<MaterialVariant> == (size_t)MaterialDataType::Count, "Enum is out of sync, please check your change.");
  static_assert(std::is_same_v<std::variant_alternative_t<(size_t)MaterialDataType::Opaque,      MaterialVariant>, OpaqueMaterialData>,      "MaterialVariant[Opaque] must be OpaqueMaterialData, please check your change.");
  static_assert(std::is_same_v<std::variant_alternative_t<(size_t)MaterialDataType::Translucent, MaterialVariant>, TranslucentMaterialData>, "MaterialVariant[Translucent] must be TranslucentMaterialData, please check your change.");
  static_assert(std::is_same_v<std::variant_alternative_t<(size_t)MaterialDataType::RayPortal,   MaterialVariant>, RayPortalMaterialData>,   "MaterialVariant[RayPortal] must be RayPortalMaterialData, please check your change.");

  MaterialData(const OpaqueMaterialData& opaque, std::optional<RtxParticleSystemDesc> particleSystem = std::nullopt, bool ignored = false)
    : m_ignored { ignored }, m_data { opaque }, m_particleSystem { particleSystem } {}

  MaterialData(const TranslucentMaterialData& translucent, std::optional<RtxParticleSystemDesc> particleSystem = std::nullopt, bool ignored = false)
    : m_ignored { ignored }, m_data { translucent }, m_particleSystem { particleSystem } {}

  MaterialData(const RayPortalMaterialData& portal, std::optional<RtxParticleSystemDesc> particleSystem = std::nullopt)
    : m_data { portal }, m_particleSystem { particleSystem } { }

  bool getIgnored() const {
    return m_ignored;
  }

  MaterialDataType getType() const {
    // NOTE: relies on the variant index matching MaterialDataType
    return static_cast<MaterialDataType>(m_data.index());
  }

  XXH64_hash_t getHash() const {
    return std::visit([](auto const& mat) { return mat.getHash(); }, m_data);
  }

  const Rc<DxvkSampler>& getSamplerOverride() const {
    return std::visit([](auto const& mat) -> const Rc<DxvkSampler>& { return mat.getSamplerOverride(); }, m_data);
  }

  const OpaqueMaterialData& getOpaqueMaterialData() const {
    assert(std::holds_alternative<OpaqueMaterialData>(m_data));
    return std::get<OpaqueMaterialData>(m_data);
  }

  OpaqueMaterialData& getOpaqueMaterialData() {
    assert(std::holds_alternative<OpaqueMaterialData>(m_data));
    return std::get<OpaqueMaterialData>(m_data);
  }

  const TranslucentMaterialData& getTranslucentMaterialData() const {
    assert(std::holds_alternative<TranslucentMaterialData>(m_data));
    return std::get<TranslucentMaterialData>(m_data);
  }

  TranslucentMaterialData& getTranslucentMaterialData() {
    assert(std::holds_alternative<TranslucentMaterialData>(m_data));
    return std::get<TranslucentMaterialData>(m_data);
  }

  const RayPortalMaterialData& getRayPortalMaterialData() const {
    assert(std::holds_alternative<RayPortalMaterialData>(m_data));
    return std::get<RayPortalMaterialData>(m_data);
  }

  RayPortalMaterialData& getRayPortalMaterialData() {
    assert(std::holds_alternative<RayPortalMaterialData>(m_data));
    return std::get<RayPortalMaterialData>(m_data);
  }

  const RtxParticleSystemDesc* getParticleSystemDesc() const {
    return m_particleSystem.has_value() ? &m_particleSystem.value() : nullptr;
  }
  
  void mergeLegacyMaterial(const LegacyMaterialData& input) {
    std::visit([&](auto& mat) {
      using T = std::decay_t<decltype(mat)>;
      if constexpr (std::is_same_v<T, OpaqueMaterialData>) {
        OpaqueMaterialData tmp;
        tmp.getAlbedoOpacityTexture() = input.getColorTexture();
        if (auto s = input.getSampler().ptr()) {
          tmp.getFilterMode() = lss::Mdl::Filter::vkToMdl(s->info().magFilter);
          tmp.getWrapModeU() = lss::Mdl::WrapMode::vkToMdl(s->info().addressModeU);
          tmp.getWrapModeV() = lss::Mdl::WrapMode::vkToMdl(s->info().addressModeV);
        }
        mat.merge(tmp);
      } else if constexpr (std::is_same_v<T, TranslucentMaterialData>) {
        TranslucentMaterialData tmp;
        if (auto s = input.getSampler().ptr()) {
          tmp.getFilterMode() = lss::Mdl::Filter::vkToMdl(s->info().magFilter);
          tmp.getWrapModeU() = lss::Mdl::WrapMode::vkToMdl(s->info().addressModeU);
          tmp.getWrapModeV() = lss::Mdl::WrapMode::vkToMdl(s->info().addressModeV);
        }
        mat.merge(tmp);
      } else { 
        RayPortalMaterialData tmp;
        tmp.getMaskTexture() = input.getColorTexture();
        tmp.getMaskTexture2() = input.getColorTexture2();
        if (auto s = input.getSampler().ptr()) {
          tmp.getFilterMode() = lss::Mdl::Filter::vkToMdl(s->info().magFilter);
          tmp.getWrapModeU() = lss::Mdl::WrapMode::vkToMdl(s->info().addressModeU);
          tmp.getWrapModeV() = lss::Mdl::WrapMode::vkToMdl(s->info().addressModeV);
        }
        mat.merge(tmp);
      }
    }, m_data);
  }

#define POPULATE_SAMPLER_INFO(info, material) \
  info.magFilter = \
    lss::Mdl::Filter::mdlToVk(material.getFilterMode()); \
  info.minFilter = \
    lss::Mdl::Filter::mdlToVk(material.getFilterMode()); \
  info.addressModeU = \
    lss::Mdl::WrapMode::mdlToVk(material.getWrapModeU(), &info.borderColor); \
  info.addressModeV = \
    lss::Mdl::WrapMode::mdlToVk(material.getWrapModeV(), &info.borderColor);

  void populateSamplerInfo(DxvkSamplerCreateInfo& toPopulate) const {
    std::visit([&](auto const& mat) { POPULATE_SAMPLER_INFO(toPopulate, mat); }, m_data);
  }
#undef POPULATE_SAMPLER_INFO
};

} // namespace dxvk
