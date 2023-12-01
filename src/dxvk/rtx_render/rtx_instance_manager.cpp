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
#include <mutex>
#include <vector>
#include <assert.h>

#include "rtx_context.h"
#include "rtx_scene_manager.h"
#include "rtx_instance_manager.h"
#include "rtx_camera_manager.h"
#include "rtx_options.h"
#include "rtx_materials.h"
#include "rtx_opacity_micromap_manager.h"

#include "../d3d9/d3d9_state.h"
#include "rtx_matrix_helpers.h"
#include "dxvk_scoped_annotation.h"

#include "rtx/pass/common_binding_indices.h"
#include "rtx/concept/surface_material/surface_material_hitgroup.h"
#include "rtx/pass/instance_definitions.h"

namespace dxvk {
  
  static bool isMirrorTransform(const Matrix4& m) {
    // Note: Identify if the winding is inverted by checking if the z axis is ever flipped relative to what it's expected to be for clockwise vertices in a lefthanded space
    // (x cross y) through the series of transformations
    Vector3 x(m[0].data), y(m[1].data), z(m[2].data);
    return dot(cross(x, y), z) < 0;
  }

  static uint32_t determineInstanceFlags(const DrawCallState& drawCall, const Matrix4& worldToProjection, const RtSurface& surface) {
    // Determine if the view inverts face winding globally
    const bool worldToProjectionMirrored = isMirrorTransform(worldToProjection);
    
    // Note: Vulkan ray tracing defaults to defining the front face based on clockwise vertex order when viewed from a left-handed coordinate system. The front face
    // should therefore be flipped if a counterclockwise ordering is used in this normal case, or the inverse logic if the series of transformations for the object
    // inverts the winding order from the expectation.
    // See: https://www.khronos.org/registry/vulkan/specs/1.1-khr-extensions/html/chap33.html#ray-traversal-culling-face
    const bool drawClockwise = drawCall.getGeometryData().frontFace == VkFrontFace::VK_FRONT_FACE_CLOCKWISE;
    
    uint32_t flags = 0;

    // Note: Flip front face by setting the front face to counterclockwise, which is the opposite of Vulkan ray tracing's clockwise default.
    if (drawClockwise == worldToProjectionMirrored)
      flags |= VK_GEOMETRY_INSTANCE_TRIANGLE_FLIP_FACING_BIT_KHR;
    
    if (!RtxOptions::Get()->enableCulling())
      flags |= VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;

    // This check can be overridden by replacement assets.
    if (drawCall.getMaterialData().alphaBlendEnabled && !surface.alphaState.isDecal && !drawCall.getGeometryData().forceCullBit)
      flags |= VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;

    switch (drawCall.getGeometryData().cullMode) {
    case VkCullModeFlagBits::VK_CULL_MODE_NONE:
      flags |= VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
      break;
    case VkCullModeFlagBits::VK_CULL_MODE_FRONT_BIT:
      // Note: Invert front face flag once more if front face culling is desired to make the current front face the backface (as we simply assume that any culling
      // desired will be backface via gl_RayFlagsCullBackFacingTrianglesEXT which helps simplify GPU-side logic).
      flags ^= VK_GEOMETRY_INSTANCE_TRIANGLE_FLIP_FACING_BIT_KHR;
      break;
    case VkCullModeFlagBits::VK_CULL_MODE_BACK_BIT:
      // Default in shader (gl_RayFlagsCullBackFacingTrianglesEXT)
      break;
    case VkCullModeFlagBits::VK_CULL_MODE_FRONT_AND_BACK:
      assert(0); // this should already be filtered out up stack
      break;
    }

    return flags;
  }

  RtInstance::RtInstance(const uint64_t id, uint32_t instanceVectorId)
    : m_id(id)
    , m_instanceVectorId(instanceVectorId)
    , m_surfaceIndex(BINDING_INDEX_INVALID)
    , m_previousSurfaceIndex(BINDING_INDEX_INVALID) { }

  // Makes a copy of an instance
  RtInstance::RtInstance(const RtInstance& src, uint64_t id, uint32_t instanceVectorId)
    : m_id(id)
    , m_instanceVectorId(instanceVectorId)
    , surface(src.surface)
    , m_seenCameraTypes(src.m_seenCameraTypes)
    , m_materialType(src.m_materialType)
    , m_albedoOpacityTextureIndex(src.m_albedoOpacityTextureIndex)
    , m_secondaryOpacityTextureIndex(src.m_secondaryOpacityTextureIndex)
    , m_isAnimated(src.m_isAnimated)
    , m_opacityMicromapSourceHash(src.m_opacityMicromapSourceHash)
    , m_surfaceIndex(src.m_surfaceIndex)
    , m_previousSurfaceIndex(src.m_previousSurfaceIndex)
    , m_isHidden(src.m_isHidden)
    , m_isUnordered(src.m_isUnordered)
    , m_isPlayerModel(src.m_isPlayerModel)
    , m_linkedBlas(src.m_linkedBlas)
    , m_materialHash(src.m_materialHash)
    , m_materialDataHash(src.m_materialDataHash)
    , m_texcoordHash(src.m_texcoordHash)
    , m_vkInstance(src.m_vkInstance)
    , m_geometryFlags(src.m_geometryFlags)
    , m_objectToWorldMirrored(src.m_objectToWorldMirrored)
    , m_firstBillboard(src.m_firstBillboard)
    , m_billboardCount(src.m_billboardCount)
    , m_lastDecalOffsetVertexDataVersion(src.m_lastDecalOffsetVertexDataVersion)
    , m_currentDecalOffsetDifference(src.m_currentDecalOffsetDifference) {
    // Members for which state carry over is intentionally skipped
    /*
       m_isMarkedForGC
       m_isInsideFrustum
       m_frameLastUpdated
       m_frameCreated
       m_isCreatedByRenderer
       buildGeometry
       buildRange
     */
  }

  void RtInstance::setBlas(BlasEntry& blas) {
    m_linkedBlas = &blas;
  }

  bool RtInstance::setTransform(const Matrix4& objectToWorld) {
    surface.objectToWorld = objectToWorld;
    surface.normalObjectToWorld = transpose(inverse(Matrix3(objectToWorld)));
    surface.prevObjectToWorld = transpose(Matrix4(m_vkInstance.transform)); // Repurpose the old matrix embedded in the VK instance structure

    // The D3D matrix on input, needs to be transposed before feeding to the VK API (left/right handed conversion)
    // NOTE: VkTransformMatrixKHR is 4x3 matrix, and Matrix4 is 4x4
    {
      const auto t = transpose(objectToWorld);
      memcpy(&m_vkInstance.transform, &t, sizeof(VkTransformMatrixKHR));
    }

    // See if the transform has changed even a tiny bit.
    // The result is used for the 'isStatic' surface flag, which is in turn used to skip motion vector calculation
    // on the GPU. We need nonzero motion vectors on objects moving even slightly to make RTXDI temporal bias correction work.
    // This comparison is not robust if the transforms are reconstructed from baked object-to-view matrices,
    // but it works well e.g. in Portal. Even if it detects truly static objects as moving, that's fine because that will only
    // have a minor performance effect of calculation extra motion vectors.
    return memcmp(surface.prevObjectToWorld.data, surface.objectToWorld.data, sizeof(Matrix4)) != 0;
  }

  bool RtInstance::setCurrentTransform(const Matrix4& objectToWorld) {
    surface.objectToWorld = objectToWorld;
    surface.normalObjectToWorld = transpose(inverse(Matrix3(objectToWorld)));

    // The D3D matrix on input, needs to be transposed before feeding to the VK API (left/right handed conversion)
    // NOTE: VkTransformMatrixKHR is 4x3 matrix, and Matrix4 is 4x4
    {
      const auto t = transpose(objectToWorld);
      memcpy(&m_vkInstance.transform, &t, sizeof(VkTransformMatrixKHR));
    }

    // See the comment in setTransform(...)
    return memcmp(surface.prevObjectToWorld.data, surface.objectToWorld.data, sizeof(Matrix4)) != 0;
  }

  void RtInstance::setPrevTransform(const Matrix4& objectToWorld) {
    surface.prevObjectToWorld = objectToWorld;
  }

  void RtInstance::setFrameCreated(const uint32_t frameIndex) {
    m_frameCreated = frameIndex;
  }

  // Sets frame id of last update, if this is the first time the frame id is set
  // instance's per frame state is reset as well
  // Returns true if this is the first update this frame
  bool RtInstance::setFrameLastUpdated(const uint32_t frameIndex) {
    if (m_frameLastUpdated != frameIndex) {
      m_seenCameraTypes.clear();

      m_frameLastUpdated = frameIndex;

      return true;
    }

    return false;
  }

  void RtInstance::markForGarbageCollection() const {
    m_isMarkedForGC = true;
  }

  void RtInstance::markAsUnlinkedFromBlasEntryForGarbageCollection() const {
    m_isUnlinkedForGC = true;
  }

  void RtInstance::markAsInsideFrustum() const {
    m_isInsideFrustum = true;
  }

  void RtInstance::markAsOutsideFrustum() const {
    m_isInsideFrustum = false;
  }

  bool RtInstance::registerCamera(CameraType::Enum cameraType, uint32_t frameIndex) {
    bool settingNewCameraType = std::find(m_seenCameraTypes.begin(), m_seenCameraTypes.end(), cameraType) == m_seenCameraTypes.end();

    if (settingNewCameraType) 
      m_seenCameraTypes.push_back(cameraType);

    return settingNewCameraType;
  }

  bool RtInstance::isCameraRegistered(CameraType::Enum cameraType) const {
    return std::find(m_seenCameraTypes.begin(), m_seenCameraTypes.end(), cameraType) != m_seenCameraTypes.end();
  }

  void RtInstance::setCustomIndexBit(uint32_t oneBitMask, bool value) {
    m_vkInstance.instanceCustomIndex = setBit(m_vkInstance.instanceCustomIndex, value, oneBitMask);
  }

  bool RtInstance::getCustomIndexBit(uint32_t oneBitMask) const {
    return m_vkInstance.instanceCustomIndex & oneBitMask;
  }

  bool RtInstance::isViewModel() const { 
    return getCustomIndexBit(CUSTOM_INDEX_IS_VIEW_MODEL);
  }

  bool RtInstance::isViewModelNonReference() const {
    return m_vkInstance.mask != 0 && isViewModel();
  }

  bool RtInstance::isViewModelReference() const { 
    return m_vkInstance.mask == 0 && isViewModel();
    }

  bool RtInstance::isViewModelVirtual() const {
    return m_vkInstance.mask & OBJECT_MASK_VIEWMODEL_VIRTUAL;
  }

  InstanceManager::InstanceManager(DxvkDevice* device, ResourceCache* pResourceCache)
    : CommonDeviceObject(device)
    , m_pResourceCache(pResourceCache) {
    m_previousViewModelState = RtxOptions::ViewModel::enable();
    m_currentDecalOffsetIndex = RtxOptions::Decals::baseOffsetIndex();
  }

  InstanceManager::~InstanceManager() {
  }

  void InstanceManager::removeEventHandler(void* eventHandlerOwnerAddress) {
    for (auto eventIter = m_eventHandlers.begin(); eventIter != m_eventHandlers.end(); eventIter++) {
      if (eventIter->eventHandlerOwnerAddress == eventHandlerOwnerAddress) {
        m_eventHandlers.erase(eventIter);
        break;
      }
    }
  }

  void InstanceManager::clear() {
    for (RtInstance* instance : m_instances) {
      removeInstance(instance);
      delete instance;
    }

    m_instances.clear();
    m_viewModelCandidates.clear();
    m_playerModelInstances.clear();
  }  

  void InstanceManager::garbageCollection() {
    // Can be configured per game: 'rtx.numFramesToKeepInstances'
    const uint32_t numFramesToKeepInstances = RtxOptions::Get()->getNumFramesToKeepInstances();
    
    // Remove instances past their lifetime or marked for GC explicitly
    const uint32_t currentFrame = m_device->getCurrentFrameId();

    // Need to release all instances when ViewModel enablement changes
    // This is a big hammer but it's fine, it's a debugging feature
    const bool isViewModelEnabled = RtxOptions::ViewModel::enable();
    if (isViewModelEnabled != m_previousViewModelState) {
      for (auto* instance : m_instances) {
        removeInstance(instance);
        delete instance;
      }
      m_instances.clear();
      m_viewModelCandidates.clear();
      m_playerModelInstances.clear();
      m_previousViewModelState = isViewModelEnabled;
    }

    const bool forceGarbageCollection = (m_instances.size() >= RtxOptions::AntiCulling::Object::numObjectsToKeep());
    for (uint32_t i = 0; i < m_instances.size();) {
      // Must take a ref here since we'll be swapping
      RtInstance*& pInstance = m_instances[i];
      assert(pInstance != nullptr);

      const bool enableGarbageCollection =
        !RtxOptions::AntiCulling::Object::enable() || // It's always True if anti-culling is disabled
        (pInstance->m_isInsideFrustum) ||
        (pInstance->getBlas()->input.getSkinningState().numBones > 0) ||
        (pInstance->m_isAnimated) ||
        (pInstance->m_isPlayerModel);

      if (((forceGarbageCollection || enableGarbageCollection) &&
           pInstance->m_frameLastUpdated + numFramesToKeepInstances <= currentFrame) ||
          pInstance->m_isMarkedForGC) {
        // Note: Pop and swap for performance, index not incremented to process swapped instance on next iteration
        removeInstance(pInstance);

        // NOTE: pInstance is now the (previously) last element
        std::swap(pInstance, m_instances.back());

        m_instances[i]->m_instanceVectorId = i;

        delete m_instances.back();

        // Remove the last element
        m_instances.pop_back();
        continue;
      }
      ++i;
    }
  }

  void InstanceManager::onFrameEnd() {
    m_viewModelCandidates.clear();
    m_playerModelInstances.clear();
    m_currentDecalOffsetIndex = RtxOptions::Decals::baseOffsetIndex();
    resetSurfaceIndices();
    m_billboards.clear();
  }

  RtInstance* InstanceManager::processSceneObject(
    const CameraManager& cameraManager, const RayPortalManager& rayPortalManager,
    BlasEntry& blas, const DrawCallState& drawCall, const MaterialData& materialData, const RtSurfaceMaterial& material) {
    Matrix4 objectToWorld = drawCall.getTransformData().objectToWorld;
    Matrix4 worldToProjection = drawCall.getTransformData().viewToProjection * drawCall.getTransformData().worldToView;

    // An attempt to resolve cases where games pre-combine view and world matrices
    if (RtxOptions::Get()->resolvePreCombinedMatrices() &&
      isIdentityExact(drawCall.getTransformData().worldToView)) {
      const auto* referenceCamera = &cameraManager.getCamera(drawCall.cameraType);
      // Note: we may accept a data even from a prev frame, as we need any information to restore;
      // but if camera data is stale, it introduces an scene object transform's lag
      if (!referenceCamera->isValid(m_device->getCurrentFrameId()) &&
        !referenceCamera->isValid(m_device->getCurrentFrameId() - 1)) {
        referenceCamera = &cameraManager.getCamera(CameraType::Main);
      }
      objectToWorld = referenceCamera->getViewToWorld(false) * drawCall.getTransformData().objectToView;
      worldToProjection = drawCall.getTransformData().viewToProjection * referenceCamera->getWorldToView(false);
    }

    // Search for an existing instance matching our input
    RtInstance* currentInstance = findSimilarInstance(blas, material, objectToWorld, drawCall.cameraType, rayPortalManager);

    if (currentInstance == nullptr) {
      // No existing match - so need to create one
      currentInstance = addInstance(blas);
    }

    updateInstance(*currentInstance, cameraManager, blas, drawCall, materialData, material, objectToWorld, worldToProjection);
   
    return currentInstance;
  }

  RtSurface::AlphaState InstanceManager::calculateAlphaState(const DrawCallState& drawCall, const MaterialData& materialData, const RtSurfaceMaterial& material) {
    RtSurface::AlphaState out{};

    // Handle Alpha State for non-Opaque materials

    if (material.getType() == RtSurfaceMaterialType::Translucent) {
      // Note: Explicitly ensure translucent materials are not considered fully opaque (even though this is the
      // default in the alpha state).
      out.isFullyOpaque = false;

      return out;
    } else if (material.getType() != RtSurfaceMaterialType::Opaque) {
      return out;
    }

    assert(material.getType() == RtSurfaceMaterialType::Opaque);

    // Determine if the Legacy Alpha State should be used based on the material data
    // Note: The Material Data may be either Legacy or Opaque here, both use the Opaque Surface Material.

    bool useLegacyAlphaState = true;

    if (materialData.getType() == MaterialDataType::Opaque) {
      const auto& opaqueMaterialData = materialData.getOpaqueMaterialData();

      useLegacyAlphaState = opaqueMaterialData.getUseLegacyAlphaState();
    } else {
      assert(materialData.getType() == MaterialDataType::Legacy);
    }

    // Handle Alpha Test State

    // Note: Even if the Alpha Test enable flag is set, we consider it disabled if the actual test type is set to always.
    const bool forceAlphaTest = drawCall.getCategoryFlags().test(InstanceCategories::AlphaBlendToCutout);
    const bool alphaTestEnabled = forceAlphaTest || (AlphaTestType)drawCall.getMaterialData().alphaTestCompareOp != AlphaTestType::kAlways;

    // Note: Use the Opaque Material Data's alpha test state information directly if requested,
    // otherwise derive the alpha test state from the drawcall (via its legacy material data).
    if (forceAlphaTest) {
      out.alphaTestType = AlphaTestType::kGreater;
      out.alphaTestReferenceValue = static_cast<uint8_t>(RtxOptions::Get()->forceCutoutAlpha() * 255.0);
    } else if (!useLegacyAlphaState) {
      const auto& opaqueMaterialData = materialData.getOpaqueMaterialData();

      out.alphaTestType = opaqueMaterialData.getAlphaTestType();
      out.alphaTestReferenceValue = opaqueMaterialData.getAlphaTestReferenceValue();
    } else if (alphaTestEnabled) {
      out.alphaTestType = (AlphaTestType)drawCall.getMaterialData().alphaTestCompareOp;
      out.alphaTestReferenceValue = drawCall.getMaterialData().alphaTestReferenceValue;
    }

    // Handle Alpha Blend State

    bool blendEnabled = false;
    BlendType blendType = BlendType::kColor;
    bool invertedBlend = false;

    // Note: Use the Opaque Material Data's blend state information directly if requested,
    // otherwise derive the alpha blend state from the drawcall (via its legacy material data).
    if (forceAlphaTest) {
      blendEnabled = false;
    } else if (!useLegacyAlphaState) {
      const auto& opaqueMaterialData = materialData.getOpaqueMaterialData();

      blendEnabled = opaqueMaterialData.getBlendEnabled();
      blendType = opaqueMaterialData.getBlendType();
      invertedBlend = opaqueMaterialData.getInvertedBlend();
    } else if (drawCall.getMaterialData().alphaBlendEnabled) {
      const auto srcColorBlendFactor = drawCall.getMaterialData().srcColorBlendFactor;
      const auto dstColorBlendFactor = drawCall.getMaterialData().dstColorBlendFactor;
      const auto colorBlendOp = drawCall.getMaterialData().colorBlendOp;

      blendEnabled = true; // Note: Set to false later for cases which don't need it

      if (colorBlendOp == VkBlendOp::VK_BLEND_OP_ADD) {
        if (srcColorBlendFactor == VkBlendFactor::VK_BLEND_FACTOR_ONE && dstColorBlendFactor == VkBlendFactor::VK_BLEND_FACTOR_ZERO) {
          // Opaque Alias
          blendEnabled = false;
        } else if (srcColorBlendFactor == VkBlendFactor::VK_BLEND_FACTOR_SRC_ALPHA && dstColorBlendFactor == VkBlendFactor::VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA) {
          // Standard Alpha Blending
          blendType = BlendType::kAlpha;
          invertedBlend = false;
        } else if (srcColorBlendFactor == VkBlendFactor::VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA && dstColorBlendFactor == VkBlendFactor::VK_BLEND_FACTOR_SRC_ALPHA) {
          // Inverted Alpha Blending
          blendType = BlendType::kAlpha;
          invertedBlend = true;
        } else if (srcColorBlendFactor == VkBlendFactor::VK_BLEND_FACTOR_SRC_ALPHA && dstColorBlendFactor == VkBlendFactor::VK_BLEND_FACTOR_ONE) {
          // Standard Emissive Alpha Blending
          blendType = RtxOptions::enableEmissiveBlendModeTranslation() ? BlendType::kAlphaEmissive : BlendType::kAlpha;
          invertedBlend = false;
        } else if (srcColorBlendFactor == VkBlendFactor::VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA && dstColorBlendFactor == VkBlendFactor::VK_BLEND_FACTOR_ONE) {
          // Inverted Emissive Alpha Blending
          blendType = RtxOptions::enableEmissiveBlendModeTranslation() ? BlendType::kAlphaEmissive : BlendType::kAlpha;
          invertedBlend = true;
        } else if (srcColorBlendFactor == VkBlendFactor::VK_BLEND_FACTOR_ONE && dstColorBlendFactor == VkBlendFactor::VK_BLEND_FACTOR_SRC_ALPHA) {
          // Standard Reverse Emissive Alpha Blending
          blendType = RtxOptions::enableEmissiveBlendModeTranslation() ? BlendType::kReverseAlphaEmissive : BlendType::kReverseAlpha;
          invertedBlend = false;
        } else if (srcColorBlendFactor == VkBlendFactor::VK_BLEND_FACTOR_ONE && dstColorBlendFactor == VkBlendFactor::VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA) {
          // Inverted Reverse Emissive Alpha Blending
          blendType = RtxOptions::enableEmissiveBlendModeTranslation() ? BlendType::kReverseAlphaEmissive : BlendType::kReverseAlpha;
          invertedBlend = true;
        } else if (srcColorBlendFactor == VkBlendFactor::VK_BLEND_FACTOR_SRC_COLOR && dstColorBlendFactor == VkBlendFactor::VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR) {
          // Standard Color Blending
          blendType = BlendType::kColor;
          invertedBlend = false;
        } else if (srcColorBlendFactor == VkBlendFactor::VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR && dstColorBlendFactor == VkBlendFactor::VK_BLEND_FACTOR_SRC_COLOR) {
          // Inverted Color Blending
          blendType = BlendType::kColor;
          invertedBlend = true;
        } else if (srcColorBlendFactor == VkBlendFactor::VK_BLEND_FACTOR_SRC_COLOR && dstColorBlendFactor == VkBlendFactor::VK_BLEND_FACTOR_ONE) {
          // Standard Emissive Color Blending
          blendType = RtxOptions::enableEmissiveBlendModeTranslation() ? BlendType::kColorEmissive : BlendType::kColor;
          invertedBlend = false;
        } else if (srcColorBlendFactor == VkBlendFactor::VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR && dstColorBlendFactor == VkBlendFactor::VK_BLEND_FACTOR_ONE) {
          // Inverted Emissive Color Blending
          blendType = RtxOptions::enableEmissiveBlendModeTranslation() ? BlendType::kColorEmissive : BlendType::kColor;
          invertedBlend = true;
        } else if (srcColorBlendFactor == VkBlendFactor::VK_BLEND_FACTOR_ONE && dstColorBlendFactor == VkBlendFactor::VK_BLEND_FACTOR_SRC_COLOR) {
          // Standard Reverse Emissive Color Blending
          blendType = RtxOptions::enableEmissiveBlendModeTranslation() ? BlendType::kReverseColorEmissive : BlendType::kReverseColor;
          invertedBlend = false;
        } else if (srcColorBlendFactor == VkBlendFactor::VK_BLEND_FACTOR_ONE && dstColorBlendFactor == VkBlendFactor::VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR) {
          // Inverted Reverse Emissive Color Blending
          blendType = RtxOptions::enableEmissiveBlendModeTranslation() ? BlendType::kReverseColorEmissive : BlendType::kReverseColor;
          invertedBlend = true;
        } else if (srcColorBlendFactor == VkBlendFactor::VK_BLEND_FACTOR_ONE && dstColorBlendFactor == VkBlendFactor::VK_BLEND_FACTOR_ONE) {
          // Emissive Blending
          blendType = RtxOptions::enableEmissiveBlendModeTranslation() ? BlendType::kEmissive : BlendType::kColor;
          invertedBlend = false;
        } else if (
          (srcColorBlendFactor == VkBlendFactor::VK_BLEND_FACTOR_DST_COLOR && dstColorBlendFactor == VkBlendFactor::VK_BLEND_FACTOR_ZERO) ||
          (srcColorBlendFactor == VkBlendFactor::VK_BLEND_FACTOR_ZERO && dstColorBlendFactor == VkBlendFactor::VK_BLEND_FACTOR_SRC_COLOR)
          ) {
          // Standard Multiplicative Blending
          blendType = BlendType::kMultiplicative;
          invertedBlend = false;
        } else if (srcColorBlendFactor == VkBlendFactor::VK_BLEND_FACTOR_DST_COLOR && dstColorBlendFactor == VkBlendFactor::VK_BLEND_FACTOR_SRC_COLOR) {
          // Double Multiplicative Blending
          blendType = BlendType::kDoubleMultiplicative;
          invertedBlend = false;
        } else {
          blendEnabled = false;
        }
      } else {
        blendEnabled = false;
      }
    }

    // Special case for the player model eyes in Portal:
    // They are rendered with blending enabled but 1.0 is added to alpha from the texture.
    // Detect this case here and turn such geometry into non-alpha-blended, otherwise
    // the eyes end up in the unordered TLAS and are not rendered correctly.
    const auto& drawMaterialData = drawCall.getMaterialData();
    if (blendEnabled && blendType == BlendType::kAlpha && !invertedBlend &&
        drawMaterialData.textureAlphaOperation == DxvkRtTextureOperation::Add &&
        drawMaterialData.textureAlphaArg1Source == RtTextureArgSource::Texture &&
        drawMaterialData.textureAlphaArg2Source == RtTextureArgSource::TFactor &&
        (drawMaterialData.tFactor >> 24) == 0xff) {
      blendEnabled = false;
    }

    if (blendEnabled) {
      out.blendType = blendType;
      out.invertedBlend = invertedBlend;
      // Note: Emissive blend flag must match which blend types are expected to use emissive override in the shader to appear emissive.
      out.emissiveBlend = isBlendTypeEmissive(blendType);

      // Handle Particle/Decal Flags
      // Note: Particles/Decals currently require blending be enabled, be it through the game's original draw call (if legacy alpha state is used),
      // or through the manually specified alpha state.

      // Note: Particles are differentiated from typical objects with opacity by labeling their source material textures as being particle textures.
      out.isParticle = drawCall.testCategoryFlags(InstanceCategories::Particle);
      out.isDecal = drawCall.testCategoryFlags(DECAL_CATEGORY_FLAGS);
    } else {
      out.invertedBlend = false;
      out.emissiveBlend = false;
    }
    
    // Set the fully opaque flag
    // Note: Fully opaque surfaces can only be signaled when no blending or alpha testing is done as well as no translucency material wise is used.
    // This is important for signaling when to not use the opacity channel in materials when it is not being used for anything.

    out.isFullyOpaque = !blendEnabled && out.alphaTestType == AlphaTestType::kAlways; // use the blend/test type from the output, rather than legacy for this so replacements can override
    out.isBlendingDisabled = !blendEnabled;

    return out;
  }

  void InstanceManager::mergeInstanceHeuristics(RtInstance& instanceToModify, const DrawCallState& drawCall, const RtSurfaceMaterial& material, const RtSurface::AlphaState& alphaState) const {
    // "Opaqueness" takes priority!
    if (
      (alphaState.isFullyOpaque || alphaState.alphaTestType == AlphaTestType::kAlways) &&
      !(instanceToModify.surface.alphaState.isFullyOpaque || instanceToModify.surface.alphaState.alphaTestType == AlphaTestType::kAlways)
    ) {
      instanceToModify.surface.alphaState = alphaState;
    }

    // NOTE: In the future we could extend this with heuristics as needed...
  }

  RtInstance* InstanceManager::findSimilarInstance(const BlasEntry& blas, const RtSurfaceMaterial& material, const Matrix4& transform, CameraType::Enum cameraType, const RayPortalManager& rayPortalManager) {

    // Disable temporal correlation between instances so that duplicate instances are not created
    // should a developer option change instance enough for it not to match anymore
    if (RtxOptions::Get()->getDeveloperOptionsEnabled())
      return nullptr;

    struct SimilarInstanceResult {
      // If teleportMatrix is non-nullptr, then it is the teleport matrix via which the virtual version matches the subject transform
      const Matrix4* teleportMatrix = nullptr;
      RtInstance* instance = nullptr;

      void setInstance(RtInstance* _instance, const Matrix4* _teleportMatrix = nullptr) {
        teleportMatrix = _teleportMatrix;
        instance = _instance;
      }
    };

    SimilarInstanceResult foundResult;

    const uint32_t currentFrameIdx = m_device->getCurrentFrameId();

    const Vector3 worldPosition = Vector3(transform[3][0], transform[3][1], transform[3][2]);

    const float uniqueObjectDistanceSqr = RtxOptions::Get()->getUniqueObjectDistanceSqr();

    RtInstance* pSimilar = nullptr;
    float nearestDistSqr = FLT_MAX;

    // Search the BLAS for an instance matching ours
    for (const RtInstance* instance : blas.getLinkedInstances()) {
      
      if ((instance->m_frameLastUpdated == currentFrameIdx)) {
        // If the transform is an exact match and the instance has already been touched this frame,
        // then this is a second draw call on a single mesh.
        if (memcmp(&transform, &instance->getTransform(), sizeof(instance->getTransform())) == 0) {
          return const_cast<RtInstance*>(instance);
        }
      } else if (instance->m_materialHash == material.getHash()) {
        // Instance hasn't been touched yet this frame.

        const Vector3 prevInstanceWorldPosition = instance->getWorldPosition();

        const float distSqr = lengthSqr(prevInstanceWorldPosition - worldPosition);
        if (distSqr <= uniqueObjectDistanceSqr && distSqr < nearestDistSqr) {
          if (distSqr == 0.0f) {
            // Not going to find anything closer.
            return const_cast<RtInstance*>(instance);
          }
          nearestDistSqr = distSqr;
          foundResult.setInstance(const_cast<RtInstance*>(instance));
        }
      }
    }

    // For portal gun and other objects that were drawn in the ViewModel, need to check the
    // virtual version of the instance from previous frame.
    if (nearestDistSqr > 0.0f &&
        cameraType == CameraType::ViewModel && 
        RtxOptions::Get()->isRayPortalVirtualInstanceMatchingEnabled() ) {
      for (const RtInstance* instance : blas.getLinkedInstances()) {
        if (instance->m_frameLastUpdated != currentFrameIdx - 1 || 
            instance->m_materialHash != material.getHash()) {
          continue;
        }
        
        // Compare against virtual position of a predicted instance's position in the current frame
        const Vector3& prevPrevInstanceWorldPosition = instance->getPrevWorldPosition();
        const Vector3 prevInstanceWorldPosition = instance->getWorldPosition();
        Vector3 predictedInstanceWorldPosition = prevInstanceWorldPosition +
          (prevInstanceWorldPosition - prevPrevInstanceWorldPosition);
      
        // Check all portal pairs
        for (auto& rayPortalPair : rayPortalManager.getRayPortalPairInfos()) {
          if (rayPortalPair.has_value()) {
            for (uint32_t i = 0; i < 2; i++) {
              const auto& rayPortal = rayPortalPair->pairInfos[i];

              const Vector3 virtualPredictedInstanceWorldPosition =
                rayPortalManager.getVirtualPosition(predictedInstanceWorldPosition, rayPortal.portalToOpposingPortalDirection);

              // Distance of the object from the predicted virtual position of an instance
              const float virtualDistSqr = lengthSqr(virtualPredictedInstanceWorldPosition - worldPosition);

              // Is the instance is similar, and within range?  We already know the BLAS is shared, due to the for loop
              if (virtualDistSqr <= uniqueObjectDistanceSqr && virtualDistSqr < nearestDistSqr) {
                nearestDistSqr = virtualDistSqr;
                foundResult.setInstance(const_cast<RtInstance*>(instance), &rayPortal.portalToOpposingPortalDirection);
                if (virtualDistSqr == 0.0f) {
                  // Not going to find anything closer.
                  break;
                }
              }
            }
          }
        }
      }
    }

    // If the match was against a virtual equivalent of the instance from previous frame, 
    // update the instance's transform to that of the virtual one
    if (foundResult.teleportMatrix) {
      foundResult.instance->setCurrentTransform(*foundResult.teleportMatrix * foundResult.instance->getTransform());
    }

    return foundResult.instance; 
  }

  RtInstance* InstanceManager::addInstance(BlasEntry& blas) {
    const uint32_t currentFrameIdx = m_device->getCurrentFrameId();

    const uint32_t instanceIdx = m_instances.size();
    RtInstance* newInst = new RtInstance(m_nextInstanceId++, instanceIdx);
    m_instances.push_back(newInst);

    RtInstance* currentInstance = m_instances[instanceIdx];

    currentInstance->m_frameCreated = currentFrameIdx;
    
    // Set Instance Vulkan AS Instance information
    {
      currentInstance->m_vkInstance.mask = 0;
      currentInstance->m_vkInstance.flags = 0;
      currentInstance->m_vkInstance.instanceCustomIndex = 0;
      currentInstance->m_vkInstance.instanceShaderBindingTableRecordOffset = 0;
      currentInstance->setBlas(blas);
    }

    // Rest of the setup happens in updateInstance()

    // Notify events after instance has been added
    for (auto& event : m_eventHandlers)
      event.onInstanceAddedCallback(*currentInstance);

    // onInstanceAddedCallback will link current instance to the BLAS
    currentInstance->m_isUnlinkedForGC = false;

    return currentInstance;
  }

  // Creates a copy of an instance
  // If the copy is temporary and is not tracked via callbacks/externally, it doesn't need
  // a valid unique instance ID. In that case, set generateValidID to false to avoid overflowing the ID value
  RtInstance* InstanceManager::createInstanceCopy(const RtInstance& reference, bool generateValidID) {

    const uint32_t instanceIdx = m_instances.size();

    uint64_t id = generateValidID ? m_nextInstanceId++ : UINT64_MAX;
    RtInstance* newInstance = new RtInstance(reference, id, instanceIdx);
    newInstance->m_isCreatedByRenderer = true;
    m_instances.push_back(newInstance);

    return newInstance;
  }

  void InstanceManager::processInstanceBuffers(const BlasEntry& blas, RtInstance& currentInstance) const {
    currentInstance.surface.positionBufferIndex = blas.modifiedGeometryData.positionBufferIndex;
    currentInstance.surface.positionOffset = blas.modifiedGeometryData.positionBuffer.offsetFromSlice();
    currentInstance.surface.positionStride = blas.modifiedGeometryData.positionBuffer.stride();
    currentInstance.surface.normalBufferIndex = blas.modifiedGeometryData.normalBufferIndex;
    currentInstance.surface.normalOffset = blas.modifiedGeometryData.normalBuffer.offsetFromSlice();
    currentInstance.surface.normalStride = blas.modifiedGeometryData.normalBuffer.stride();
    currentInstance.surface.color0BufferIndex = blas.modifiedGeometryData.color0BufferIndex;
    currentInstance.surface.color0Offset = blas.modifiedGeometryData.color0Buffer.offsetFromSlice();
    currentInstance.surface.color0Stride = blas.modifiedGeometryData.color0Buffer.stride();
    currentInstance.surface.texcoordBufferIndex = blas.modifiedGeometryData.texcoordBufferIndex;
    currentInstance.surface.texcoordOffset = blas.modifiedGeometryData.texcoordBuffer.offsetFromSlice();
    currentInstance.surface.texcoordStride = blas.modifiedGeometryData.texcoordBuffer.stride();
    currentInstance.surface.previousPositionBufferIndex = blas.modifiedGeometryData.previousPositionBufferIndex;
    currentInstance.surface.indexBufferIndex = blas.modifiedGeometryData.indexBufferIndex;
    currentInstance.surface.indexStride = blas.modifiedGeometryData.indexBuffer.stride();
  }

  // Returns true if the instance was modified
  bool InstanceManager::applyDeveloperOptions(RtInstance& currentInstance, const DrawCallState& drawCall) {
    if (!RtxOptions::Get()->getDeveloperOptionsEnabled())
      return false;

    if ((
      currentInstance.m_instanceVectorId >= RtxOptions::Get()->getInstanceOverrideInstanceIdx() &&
      currentInstance.m_instanceVectorId < RtxOptions::Get()->getInstanceOverrideInstanceIdx() + RtxOptions::Get()->getInstanceOverrideInstanceIdxRange())) {

      if (RtxOptions::Get()->getInstanceOverrideSelectedPrintMaterialHash())
        Logger::info(str::format("Draw Call Material Hash: ", drawCall.getMaterialData().getHash()));

      // Apply world offset
      Vector3& worldOffset = RtxOptions::Get()->getOverrideWorldOffset();
      Matrix4 objectToWorld = currentInstance.getTransform();
      objectToWorld[3].xyz() += worldOffset;
      currentInstance.setCurrentTransform(objectToWorld);
      currentInstance.setPrevTransform(objectToWorld);

      return true;
    }

    return false;
  }

  // Updates the state of the instance with the draw call inputs
  // It handles multiple draw calls called for a same instance within a frame
  // To be called on every draw call
  void InstanceManager::updateInstance(RtInstance& currentInstance,
                                       const CameraManager& cameraManager,
                                       const BlasEntry& blas,
                                       const DrawCallState& drawCall,
                                       const MaterialData& materialData,
                                       const RtSurfaceMaterial& material,
                                       const Matrix4& transform,
                                       const Matrix4& worldToProjection) {
    currentInstance.m_categoryFlags = drawCall.getCategoryFlags();

    // setFrameLastUpdated() must be called first as it resets instance's state on a first call in a frame
    const bool isFirstUpdateThisFrame = currentInstance.setFrameLastUpdated(m_device->getCurrentFrameId());

    // These can change in the Runtime UI so need to check during update
    currentInstance.m_isHidden = currentInstance.testCategoryFlags(InstanceCategories::Hidden);
    currentInstance.m_isPlayerModel = currentInstance.testCategoryFlags(InstanceCategories::ThirdPersonPlayerModel);
    currentInstance.m_isWorldSpaceUI = currentInstance.testCategoryFlags(InstanceCategories::WorldUI);

    // Hide the sky instance since it is not raytraced.
    // Sky mesh and material are only good for capture and replacement purposes.
    if (drawCall.cameraType == CameraType::Sky) {
      currentInstance.m_isHidden = true;
    }

    // Register camera
    bool isNewCameraSet = currentInstance.registerCamera(drawCall.cameraType, m_device->getCurrentFrameId());

    const bool overridePreviousCameraUpdate = isNewCameraSet &&
      (drawCall.cameraType == CameraType::Main ||
       // Don't overwrite transform from when the instance was seen with the main camera
       !currentInstance.isCameraRegistered(CameraType::Main));

    const RtSurface::AlphaState alphaState = calculateAlphaState(drawCall, materialData, material);

    if (!isFirstUpdateThisFrame)
      // This is probably the same instance, being drawn twice!  Merge it
      mergeInstanceHeuristics(currentInstance, drawCall, material, alphaState);
    
    // Updates done only once a frame unless overriden due to an explicit state
    if (isFirstUpdateThisFrame || overridePreviousCameraUpdate) {

      if (isFirstUpdateThisFrame) {
        processInstanceBuffers(blas, currentInstance);

        currentInstance.m_materialType = material.getType();

        if (material.getType() == RtSurfaceMaterialType::Opaque) {
          currentInstance.m_albedoOpacityTextureIndex = material.getOpaqueSurfaceMaterial().getAlbedoOpacityTextureIndex();
          currentInstance.m_samplerIndex = material.getOpaqueSurfaceMaterial().getSamplerIndex();
        } else if (material.getType() == RtSurfaceMaterialType::RayPortal) {
          currentInstance.m_albedoOpacityTextureIndex = material.getRayPortalSurfaceMaterial().getMaskTextureIndex();
          currentInstance.m_samplerIndex = material.getRayPortalSurfaceMaterial().getSamplerIndex();
          currentInstance.m_secondaryOpacityTextureIndex = material.getRayPortalSurfaceMaterial().getMaskTextureIndex2();
          currentInstance.m_secondarySamplerIndex = material.getRayPortalSurfaceMaterial().getSamplerIndex2();
        }

        // Fetch the material from the cache
        m_pResourceCache->find(material, currentInstance.surface.surfaceMaterialIndex);

        currentInstance.m_materialDataHash = drawCall.getMaterialData().getHash();
        currentInstance.m_materialHash = material.getHash();
        currentInstance.m_texcoordHash = drawCall.getGeometryData().hashes[HashComponents::VertexTexcoord];
        currentInstance.m_indexHash = drawCall.getGeometryData().hashes[HashComponents::Indices];

        // Surface meta data
        currentInstance.surface.isEmissive = false;
        currentInstance.surface.isMatte = false;
        currentInstance.surface.textureColorArg1Source = drawCall.getMaterialData().textureColorArg1Source;
        currentInstance.surface.textureColorArg2Source = drawCall.getMaterialData().textureColorArg2Source;
        currentInstance.surface.textureColorOperation = drawCall.getMaterialData().textureColorOperation;
        currentInstance.surface.textureAlphaArg1Source = drawCall.getMaterialData().textureAlphaArg1Source;
        currentInstance.surface.textureAlphaArg2Source = drawCall.getMaterialData().textureAlphaArg2Source;
        currentInstance.surface.textureAlphaOperation = drawCall.getMaterialData().textureAlphaOperation;
        currentInstance.surface.texgenMode = drawCall.getTransformData().texgenMode; // NOTE: Make it material data...
        currentInstance.surface.tFactor = drawCall.getMaterialData().tFactor;
        currentInstance.surface.alphaState = alphaState;
        currentInstance.surface.isAnimatedWater = currentInstance.testCategoryFlags(InstanceCategories::AnimatedWater);
        currentInstance.surface.associatedGeometryHash = drawCall.getHash(RtxOptions::Get()->GeometryAssetHashRule);
        currentInstance.surface.isTextureFactorBlend = drawCall.getMaterialData().isTextureFactorBlend;
        currentInstance.surface.isMotionBlurMaskOut = currentInstance.testCategoryFlags(InstanceCategories::IgnoreMotionBlur);
        // Note: Skip the spritesheet adjustment logic in the surface interaction when using Ray Portal materials as this logic
        // is done later in the Surface Material Interaction (and doing it in both places will just double up the animation).
        currentInstance.surface.skipSurfaceInteractionSpritesheetAdjustment = (materialData.getType() == MaterialDataType::RayPortal);
        currentInstance.surface.isInsideFrustum = RtxOptions::AntiCulling::Object::enable() ? currentInstance.m_isInsideFrustum : true;

        currentInstance.surface.srcColorBlendFactor = drawCall.getMaterialData().srcColorBlendFactor;
        currentInstance.surface.dstColorBlendFactor = drawCall.getMaterialData().dstColorBlendFactor;
        currentInstance.surface.colorBlendOp = drawCall.getMaterialData().colorBlendOp;

        uint8_t spriteSheetRows = 0, spriteSheetCols = 0, spriteSheetFPS = 0;

        // Note: Extract spritesheet information from the associated material data as it ends up stored in the Surface
        // not in the Surface Material like most material information.
        switch (materialData.getType()) {
        case MaterialDataType::Opaque:
          spriteSheetRows = materialData.getOpaqueMaterialData().getSpriteSheetRows();
          spriteSheetCols = materialData.getOpaqueMaterialData().getSpriteSheetCols();
          spriteSheetFPS = materialData.getOpaqueMaterialData().getSpriteSheetFPS();

          break;
        case MaterialDataType::Translucent:
          spriteSheetRows = materialData.getTranslucentMaterialData().getSpriteSheetRows();
          spriteSheetCols = materialData.getTranslucentMaterialData().getSpriteSheetCols();
          spriteSheetFPS = materialData.getTranslucentMaterialData().getSpriteSheetFPS();

          break;
        case MaterialDataType::RayPortal:
          spriteSheetRows = materialData.getRayPortalMaterialData().getSpriteSheetRows();
          spriteSheetCols = materialData.getRayPortalMaterialData().getSpriteSheetCols();
          spriteSheetFPS = materialData.getRayPortalMaterialData().getSpriteSheetFPS();

          break;
        }

        currentInstance.surface.spriteSheetRows = spriteSheetRows;
        currentInstance.surface.spriteSheetCols = spriteSheetCols;
        currentInstance.surface.spriteSheetFPS = spriteSheetFPS;
        currentInstance.surface.objectPickingValue = drawCall.drawCallID;

        // For worldspace UI, we want to show the UI (unlit) in the world.  So configure the blend mode if blending is used accordingly.
        if (currentInstance.m_isWorldSpaceUI) {
          if (currentInstance.surface.alphaState.isBlendingDisabled) {
            currentInstance.surface.isEmissive = true;
          } else {
            currentInstance.surface.alphaState.emissiveBlend = true;
          }
        }
      }

      // Update transform
      {
        // Heuristic for MS5 - motion vectors on translucent surfaces cannot be trusted.  This will help with IQ, but need a longer term solution [TREX-634]
        const bool isMotionUnstable = material.getType() == RtSurfaceMaterialType::Translucent 
                                   || currentInstance.testCategoryFlags(InstanceCategories::Particle)
                                   || currentInstance.testCategoryFlags(InstanceCategories::WorldUI);

        const bool hasPreviousPositions = blas.modifiedGeometryData.previousPositionBuffer.defined() && !isMotionUnstable;
        const bool isFirstUpdateAfterCreation = currentInstance.isCreatedThisFrame(m_device->getCurrentFrameId()) && isFirstUpdateThisFrame;
        bool hasTransformChanged = false;

        // Note: objectToView is aliased on updates, since findSimilarInstance() doesn't discern it
        Matrix4 objectToWorld = transform;

        // Hack for TREX-2272. In Portal, in the GLaDOS chamber, the monitors show a countdown timer with background, and the digits and background are coplanar.
        // We cannot reliably determine the digits material because it's a dynamic texture rendered by vgui that contains all kinds of UI things.
        // So instead of offsetting the digits or making them live in unordered TLAS (either of which would solve the problem), we offset the screen background backwards.
        const float worldSpaceUiBackgroundOffset = RtxOptions::Get()->worldSpaceUiBackgroundOffset();
        if (worldSpaceUiBackgroundOffset != 0.f && currentInstance.testCategoryFlags(InstanceCategories::WorldMatte)) {
          objectToWorld[3] += objectToWorld[2] * worldSpaceUiBackgroundOffset;
        }

        // Update the transform based on what state we're in
        if (isFirstUpdateAfterCreation) {
          currentInstance.setCurrentTransform(objectToWorld);
          currentInstance.setPrevTransform(objectToWorld);
          hasTransformChanged = false;
        } else if (isFirstUpdateThisFrame) {
          hasTransformChanged = currentInstance.setTransform(objectToWorld);
        } else {
          hasTransformChanged = currentInstance.setCurrentTransform(objectToWorld);
        }

        currentInstance.surface.textureTransform = drawCall.getTransformData().textureTransform;

        currentInstance.surface.isStatic = !(hasTransformChanged || hasPreviousPositions) || material.getType() == RtSurfaceMaterialType::RayPortal;

        currentInstance.surface.isClipPlaneEnabled = drawCall.getTransformData().enableClipPlane;
        currentInstance.surface.clipPlane = drawCall.getTransformData().clipPlane;

        // Apply developer options
        if (isFirstUpdateThisFrame)
          applyDeveloperOptions(currentInstance, drawCall);

        // Inform the listeners
        for (auto& event : m_eventHandlers)
          event.onInstanceUpdatedCallback(currentInstance, material, hasTransformChanged, hasPreviousPositions);
      }
    }

    // We only have 1 hit shader.
    currentInstance.m_vkInstance.instanceShaderBindingTableRecordOffset = 0;

    // Update instance flags.
    // Note: this should happen on instance updates and not creation because the same geometry can be drawn
    // with different flags, and the instance manager can match an old instance of a geometry to a new one with different draw mode.
    currentInstance.m_vkInstance.flags = determineInstanceFlags(drawCall, worldToProjection, currentInstance.surface);

    // Update the geometry and instance flags
    if (
      !currentInstance.surface.alphaState.isFullyOpaque && currentInstance.surface.alphaState.isParticle ||
      // Note: include alpha blended geometry on the player model into the unordered TLAS. This is hacky as there might be
      // suitable geometry outside of the player model, but we don't have a way to distinguish it from alpha blended geometry
      // that should be alpha tested instead, like some metallic stairs in Portal -- those should be resolved normally.
      !currentInstance.surface.alphaState.isFullyOpaque && !currentInstance.surface.alphaState.isBlendingDisabled && currentInstance.m_isPlayerModel ||
      currentInstance.surface.alphaState.emissiveBlend
    ) {
      // Alpha-blended and emissive particles go to the separate "unordered" TLAS as non-opaque geometry
      currentInstance.m_geometryFlags = VK_GEOMETRY_NO_DUPLICATE_ANY_HIT_INVOCATION_BIT_KHR;
      currentInstance.m_isUnordered = true;
      // Unordered resolve only accumulates via any-hits and ignores opaque hits, therefore force 
      // the opaque hits resolve via OMMs to be turned into any-hits.
      // Note: this has unexpected effect even with OMM off and results in minor visual changes in Portal MF A DLSS test
      currentInstance.m_vkInstance.flags |= VK_GEOMETRY_INSTANCE_FORCE_NO_OPAQUE_BIT_KHR;
    } else if (material.getType() == RtSurfaceMaterialType::Opaque && !currentInstance.surface.alphaState.isFullyOpaque && currentInstance.surface.alphaState.isBlendingDisabled) {
      // Alpha-tested geometry goes to the primary TLAS as non-opaque geometry with potential duplicate hits.
      currentInstance.m_geometryFlags = 0;
    } else if (material.getType() == RtSurfaceMaterialType::Opaque && !currentInstance.surface.alphaState.isFullyOpaque) {
      // Alpha-blended geometry goes to the primary TLAS as non-opaque geometry with no duplicate hits.
      currentInstance.m_geometryFlags = VK_GEOMETRY_NO_DUPLICATE_ANY_HIT_INVOCATION_BIT_KHR;
      // Treat all non-transparent hits as any-hits
      currentInstance.m_vkInstance.flags |= VK_GEOMETRY_INSTANCE_FORCE_NO_OPAQUE_BIT_KHR;
    } else if (material.getType() == RtSurfaceMaterialType::Translucent) {
      // Translucent (e.g. glass) geometry goes to the primary TLAS as non-opaque geometry with no duplicate hits.
      currentInstance.m_geometryFlags = VK_GEOMETRY_NO_DUPLICATE_ANY_HIT_INVOCATION_BIT_KHR;
    } else if (material.getType() == RtSurfaceMaterialType::RayPortal) {
      // Portals go to the primary TLAS as opaque.
      currentInstance.m_geometryFlags = VK_GEOMETRY_OPAQUE_BIT_KHR;
    } else if (currentInstance.surface.alphaState.isDecal) {
      // Consider all decals as non opaque objects
      currentInstance.m_geometryFlags = 0;
    } else if (currentInstance.surface.isClipPlaneEnabled) {
      // Use non-opaque hits to process clip planes on visibility rays.
      // To handle cases when the same *static* object is used both with and without clip planes,
      // use the force bit to avoid BLAS confusion (because the geometry flags are baked into BLAS).
      currentInstance.m_geometryFlags = VK_GEOMETRY_OPAQUE_BIT_KHR;
      currentInstance.m_vkInstance.flags |= VK_GEOMETRY_INSTANCE_FORCE_NO_OPAQUE_BIT_KHR;
    } else {
      // All other fully opaques go to the primary TLAS as opaque.
      currentInstance.m_geometryFlags = VK_GEOMETRY_OPAQUE_BIT_KHR;
    }
    
    // Enable backface culling for Portals to avoid additional hits to the back of Portals
    if (material.getType() == RtSurfaceMaterialType::RayPortal) {
      currentInstance.m_vkInstance.flags &= ~VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
    }

    // Extra instance meta data needed for Opacity Micromap Manager 
    {
      switch (materialData.getType()) {
      case MaterialDataType::Opaque:
        currentInstance.m_isAnimated = materialData.getOpaqueMaterialData().getSpriteSheetFPS() != 0;
        break;
      case MaterialDataType::Translucent:
        currentInstance.m_isAnimated = materialData.getTranslucentMaterialData().getSpriteSheetFPS() != 0;
        break;
      case MaterialDataType::RayPortal:
        currentInstance.m_isAnimated = materialData.getRayPortalMaterialData().getSpriteSheetFPS() != 0;
        break;
      default:
        currentInstance.m_isAnimated = false;
        break;
      }
    }

    // Update mask
    {
      uint mask = isFirstUpdateThisFrame ? 0 : currentInstance.m_vkInstance.mask;

      if (currentInstance.m_isPlayerModel && drawCall.cameraType != CameraType::ViewModel) {
        mask |= OBJECT_MASK_PLAYER_MODEL;
        m_playerModelInstances.push_back(&currentInstance);
      } else {
        currentInstance.m_isPlayerModel = false;
        if (currentInstance.m_isUnordered && RtxOptions::Get()->isSeparateUnorderedApproximationsEnabled()) {
          // Separate set of mask bits for the unordered TLAS
          if (currentInstance.surface.alphaState.emissiveBlend)
            mask |= OBJECT_MASK_UNORDERED_ALL_EMISSIVE;
          else
            mask |= OBJECT_MASK_UNORDERED_ALL_BLENDED;
        }
        else {
          if (material.getType() == RtSurfaceMaterialType::Translucent) {
            // Translucent material
            mask |= OBJECT_MASK_TRANSLUCENT;
          } else if (material.getType() == RtSurfaceMaterialType::RayPortal) {
            // Portal
            mask |= OBJECT_MASK_PORTAL;
          } else {
            mask |= OBJECT_MASK_OPAQUE;
          }
        }
      }

      if (currentInstance.m_isHidden)
        mask = 0;

      currentInstance.m_vkInstance.mask = mask;
    }
    // This flag translates to a flip of VK_GEOMETRY_INSTANCE_TRIANGLE_FLIP_FACING_BIT_KHR when the instance
    // is a separate BLAS instance, and to nothing if it's a part of a merged BLAS.
    // The reason is in this bit of Vulkan spec:
    //     VK_GEOMETRY_INSTANCE_TRIANGLE_FLIP_FACING_BIT_KHR indicates that the facing determination for geometry in this instance
    //     is inverted. Because the facing is determined in object space, an instance transform does not change the winding,
    //     but a geometry transform does.
    // https://registry.khronos.org/vulkan/specs/1.3-extensions/man/html/VkGeometryInstanceFlagBitsNV.html 
    currentInstance.m_objectToWorldMirrored = isMirrorTransform(transform);

    // Offset decals along their normals.
    // Do this *after* the instance transform is updated above.
    if (alphaState.isDecal || currentInstance.m_isWorldSpaceUI) {
      // In the event this modifies the CPU draw call geometry, the change will be applied next frame.
      applyDecalOffsets(currentInstance, drawCall.getGeometryData());
    }

    currentInstance.m_billboardCount = 0;

    if (drawCall.cameraType == CameraType::ViewModel && !currentInstance.m_isHidden && isFirstUpdateThisFrame)
      m_viewModelCandidates.push_back(&currentInstance);

    if (RtxOptions::Get()->enableSeparateUnorderedApproximations() &&
        (drawCall.cameraType == CameraType::Main || drawCall.cameraType == CameraType::ViewModel) &&
        currentInstance.m_isUnordered &&
        !currentInstance.m_isHidden &&
        currentInstance.getVkInstance().mask != 0) {

      if (currentInstance.testCategoryFlags(InstanceCategories::Beam)) {
        createBeams(currentInstance);
      } else {
        createBillboards(currentInstance, cameraManager.getMainCamera().getDirection(false));
      }
    }
  }

  void InstanceManager::removeInstance(RtInstance* instance) {
    // In these cases we skip calling onInstanceDestroyed:
    //   Some view model and player instances are created in the renderer and don't have onInstanceAdded called,
    //   so not call onInstanceDestroyed either.
    if (instance->m_isCreatedByRenderer) {
      return;
    }

    for (auto& event : m_eventHandlers) {
      event.onInstanceDestroyedCallback(*instance);
    }
  }

  RtInstance* InstanceManager::createViewModelInstance(Rc<DxvkContext> ctx,
                                                       const RtInstance& reference,
                                                       const Matrix4d& perspectiveCorrection,
                                                       const Matrix4d& prevPerspectiveCorrection) {

    // Create a view model instance corresponding to the reference instance, for one frame 

    // Don't pollute global instance id with View Models since they're not tracked in game capturer
    const bool needValidGlobalInstanceId = false;

    RtInstance* viewModelInstance = createInstanceCopy(reference, needValidGlobalInstanceId);

    const uint32_t frameId = m_device->getCurrentFrameId();
    viewModelInstance->setFrameCreated(frameId);
    viewModelInstance->setFrameLastUpdated(frameId);
    viewModelInstance->m_vkInstance.mask = OBJECT_MASK_VIEWMODEL;
    viewModelInstance->setCustomIndexBit(CUSTOM_INDEX_IS_VIEW_MODEL, true);

    // View model instances are recreated every frame
    viewModelInstance->markForGarbageCollection();

    if (RtxOptions::ViewModel::perspectiveCorrection()) {
      // A transform that looks "correct" only from a main camera's point of view
      const auto corrected = perspectiveCorrection * reference.getTransform();
      const auto prevCorrected = prevPerspectiveCorrection * reference.getPrevTransform();

      auto isOrdinary = [](const Matrix4d& m) {
        auto isCloseTo = [](auto a, auto b) {
          return std::abs(a - b) < 0.001;
        };
        return isCloseTo(m[0][3], 0.0)
          && isCloseTo(m[1][3], 0.0)
          && isCloseTo(m[2][3], 0.0)
          && isCloseTo(m[3][3], 1.0);
      };

      // If matrices are not convoluted, don't modify the vertex data: just set the transforms directly
      if (isOrdinary(corrected) && isOrdinary(prevCorrected)) {
        viewModelInstance->setCurrentTransform(corrected);
        viewModelInstance->setPrevTransform(prevCorrected);
      } else {
        ONCE(Logger::info("[RTX-Compatibility-Info] Unexpected values in the perspective-corrected transform of a view model. Fallback to geometry modification"));
        // Only need to run this on BVH op (maybe this could be moved to geometry processing?)
        if (viewModelInstance->getBlas()->frameLastUpdated == frameId) {
          const auto worldToObject = inverse(reference.getTransform());
          const auto instancePositionTransform = worldToObject * perspectiveCorrection * reference.getTransform();

          ctx->getCommonObjects()->metaGeometryUtils().dispatchViewModelCorrection(ctx,
            viewModelInstance->getBlas()->modifiedGeometryData, instancePositionTransform);
        }
      }
    }

    // ViewModel should never be considered static
    viewModelInstance->surface.isStatic = false;

    // Note this is an instance copy of a input reference. It is unknown to the source engine, so we don't call onInstanceAdded callbacks for it
    // It also results in this instance not being linked to reference instance BLAS and thus not considered in findSimilarInstances' lookups
    // This is desired as ViewModel instances are not to be linked frame to frame

    return viewModelInstance;
  }

  void InstanceManager::createViewModelInstances(Rc<DxvkContext> ctx,
                                                 const CameraManager& cameraManager,
                                                 const RayPortalManager& rayPortalManager) {
    ScopedGpuProfileZone(ctx, "ViewModel");

    if (!RtxOptions::ViewModel::enable())
      return;

    if (!cameraManager.isCameraValid(CameraType::ViewModel))
      return;

    // If the first person player model is enabled, hide the view model.
    if (RtxOptions::Get()->playerModel.enableInPrimarySpace()) {
      for (auto* candidateInstance : m_viewModelCandidates) {
        candidateInstance->m_vkInstance.mask = 0;
      }
      return;
    }

    const RtCamera& camera = cameraManager.getMainCamera();
    const RtCamera& viewModelCamera = cameraManager.getCamera(CameraType::ViewModel);

    // Use the FOV (XY scaling) from the view-model matrix and the near/far planes (ZW scaling) from the main matrix.
    // The view-model camera has different near/far planes, so if that projection matrix is used naively,
    // the gun ends up being scaled up by a factor of 7 or so (in Portal).
    const auto& mainProjectionMatrix = camera.getViewToProjection();
    auto viewModelProjectionMatrix = viewModelCamera.getViewToProjection();
    viewModelProjectionMatrix[2][2] = mainProjectionMatrix[2][2];
    viewModelProjectionMatrix[2][3] = mainProjectionMatrix[2][3];
    viewModelProjectionMatrix[3][2] = mainProjectionMatrix[3][2];

    const auto& mainPreviousProjectionMatrix = camera.getPreviousViewToProjection();
    auto previousViewModelProjectionMatrix = viewModelCamera.getPreviousViewToProjection();
    previousViewModelProjectionMatrix[2][2] = mainPreviousProjectionMatrix[2][2];
    previousViewModelProjectionMatrix[2][3] = mainPreviousProjectionMatrix[2][3];
    previousViewModelProjectionMatrix[3][2] = mainPreviousProjectionMatrix[3][2];

    // Apply an extra scaling matrix to the view-space positions of view model to make it less likely to interact with world geometry.
    Matrix4d scaleMatrix {};
    scaleMatrix[0][0] = scaleMatrix[1][1] = scaleMatrix[2][2] = RtxOptions::ViewModel::scale();
    scaleMatrix[3][3] = 1.0;

    // Compute the view-model perspective correction matrix.
    // This expression (read right-to-left) is a solution to the following equation:
    //   (mainProjection * mainView * objectToWorld) * transformedPosition = (viewModelProjection * viewModelView * objectToWorld) * position
    // where 'position' is the original vertex data supplied by the game, and 'transformedPosition' is what we need to compute in order to make
    // the view model project into the same screen positions using the main camera.
    // The 'objectToWorld' matrices are applied later, in createViewModelInstance, because they're different per-instance.
    const auto perspectiveCorrection = camera.getViewToWorld(false) * (camera.getProjectionToView() * viewModelProjectionMatrix * scaleMatrix) * viewModelCamera.getWorldToView(false);
    const auto prevPerspectiveCorrection = camera.getPreviousViewToWorld(false) * (camera.getPreviousProjectionToView() * previousViewModelProjectionMatrix * scaleMatrix) * viewModelCamera.getPreviousWorldToView(false);

    // Create any valid view model instances from the list of candidates
    std::vector<RtInstance*> viewModelInstances;
    for (auto* candidateInstance : m_viewModelCandidates) {

      // Valid view model instances must be associated only with the view model camera
      if (candidateInstance->m_seenCameraTypes.size() != 1)
        continue;

      // Hide the reference instance since we'll create a separate instance for the view model 
      candidateInstance->m_vkInstance.mask = 0;

      // Tag the instance as ViewModel so it can be checked for it being a reference view model instance
      candidateInstance->setCustomIndexBit(CUSTOM_INDEX_IS_VIEW_MODEL, true);

      viewModelInstances.push_back(createViewModelInstance(ctx, *candidateInstance, perspectiveCorrection, prevPerspectiveCorrection));
    }

    // Create virtual instances for the view model instances
    createRayPortalVirtualViewModelInstances(viewModelInstances, cameraManager, rayPortalManager);
  }

  static bool isInsidePlayerModel(const Vector3& playerModelPosition, const Vector3& instancePosition) {
    const Vector3 playerToInstance = instancePosition - playerModelPosition;
    const float horizontalDistance = length(Vector2(playerToInstance.x, playerToInstance.y));
    const float verticalDistance = fabs(playerToInstance.z);

    // Distance thresholds determined experimentally to match the portal gun held in player's hands
    // but not match the gun on the pedestals.
    const float maxHorizontalDistance = RtxOptions::Get()->playerModel.horizontalDetectionDistance();
    const float maxVerticalDistance = RtxOptions::Get()->playerModel.verticalDetectionDistance();

    return (horizontalDistance <= maxHorizontalDistance) && (verticalDistance <= maxVerticalDistance);
  }

  void InstanceManager::filterPlayerModelInstances(const Vector3& playerModelPosition, const RtInstance* bodyInstance) {
    for (size_t i = 0; i < m_playerModelInstances.size(); ++i) {
      RtInstance* instance = m_playerModelInstances[i];

      // Don't compare the body to itself.
      if (instance == bodyInstance)
        continue;

      if (instance->m_isUnordered) {
        // Particles don't have a valid position in the instance matrix and often combine many particles
        // in one instance. So we rely on the analysis done for billboard creation earlier and see if the billboards
        // intersect with the player model.

        // Start assuming that the instance is actually part of the player model.
        bool isPlayerModelInstance = true;

        if (instance->m_billboardCount > 0) {
          // Check if the billboards are used as intersection primitives. 
          // Note: If one billboard is used as an intersection primitive, all of them are
          if (m_billboards[instance->m_firstBillboard].allowAsIntersectionPrimitive) {
            // If there are billboards, look at their centers, and if any of them are outside of the player model
            // limits, consider the entire instance non-player-model.
            // Opposite approach is possible, too, not entirely sure what's better.
            for (uint32_t billboardIndex = 0; billboardIndex < instance->m_billboardCount; ++billboardIndex) {
              const IntersectionBillboard& billboard = m_billboards[billboardIndex + instance->m_firstBillboard];
              if (!isInsidePlayerModel(playerModelPosition, billboard.center)) {
                isPlayerModelInstance = false;
                break;
              }
            }
          }
        }

        if (isPlayerModelInstance) {
          if (instance->m_billboardCount > 0) {
            // If this instance contains particles and is part of the player model,
            // assign the PLAYER_MODEL mask to its billboards and hide the original instance.
            for (uint32_t billboardIndex = 0; billboardIndex < instance->m_billboardCount; ++billboardIndex) {
              IntersectionBillboard& billboard = m_billboards[billboardIndex + instance->m_firstBillboard];
              billboard.instanceMask = OBJECT_MASK_PLAYER_MODEL;
            }

            instance->getVkInstance().mask = 0;
          }
        } else {
          // Remove the instance from the list to avoid creating virtual instances for it.
          m_playerModelInstances.erase(m_playerModelInstances.begin() + i);
          --i;
        }
      } else {
        const Vector3 instancePosition = instance->getTransform()[3].xyz();

        if (!isInsidePlayerModel(playerModelPosition, instancePosition)) {
          // Note: just use the OPAQUE flag here, which works for Portal with current assets.
          // Might want to apply more complex logic if that is insufficient one day.
          instance->getVkInstance().mask = OBJECT_MASK_OPAQUE;

          // Remove this instance from the player model list.
          m_playerModelInstances.erase(m_playerModelInstances.begin() + i);
          --i;
        }
      }
    }
  }

  void InstanceManager::detectIfPlayerModelIsVirtual(
    const CameraManager& cameraManager,
    const RayPortalManager& rayPortalManager,
    const Vector3& playerModelPosition,
    bool* out_PlayerModelIsVirtual,
    const SingleRayPortalDirectionInfo** out_NearPortalInfo,
    const SingleRayPortalDirectionInfo** out_FarPortalInfo) const {
    auto& rayPortalPair = *rayPortalManager.getRayPortalPairInfos().begin();

    *out_PlayerModelIsVirtual = false;
    int portalIndexForVirtualInstances = -1;

    if (rayPortalPair.has_value()) {

      // Estimate the position of the player model's eyes (where the camera normally is), ignoring crouching.
      // Note that in Portal, the player model is always upright, even if the player is flying out of a floor portal upside down.
      // This makes the detection of whether the player model is virtual more robust.

      Vector3 playerModelEyePosition = playerModelPosition;
      playerModelEyePosition.z += RtxOptions::Get()->playerModel.eyeHeight();

      // Find the portal that is closest to the model

      float distanceOfModelPortal = FLT_MAX;
      int playerModelNearPortalIndex = 0;

      for (int portalIndex = 0; portalIndex < 2; ++portalIndex) {
        const RayPortalInfo& portalInfo = rayPortalPair->pairInfos[portalIndex].entryPortalInfo;
        const float distanceToModel = length(portalInfo.centroid - playerModelEyePosition);
        if (distanceToModel < distanceOfModelPortal) {
          distanceOfModelPortal = distanceToModel;
          playerModelNearPortalIndex = portalIndex;
        }
      }

      const Vector3& camPos = cameraManager.getCamera(CameraType::Main).getPosition(/* freecam = */ false);

      // Find the portal that the imaginary player (i.e. a blob around the camera, or camera volume) is currently intersecting

      uint32_t cameraVolumePortalIntersectionMask = 0;

      for (uint i = 0; i < 2; i++) {
        const auto& rayPortal = rayPortalPair->pairInfos[i];
        const Vector3 dirToPortalCentroid = rayPortal.entryPortalInfo.centroid - camPos;

        // Approximate the player collision model with this capsule-like shape
        const float maximumNormalDistance = lerp(RtxOptions::Get()->playerModel.intersectionCapsuleRadius(),
                                                 RtxOptions::Get()->playerModel.intersectionCapsuleHeight(),
                                                 clamp(rayPortal.entryPortalInfo.planeNormal.z, 0.f, 1.f));

        // Test if that shape intersects with the portal and if the camera is in front of it
        const float planeDistanceNormal = -dot(dirToPortalCentroid, rayPortal.entryPortalInfo.planeNormal);
        const float planeDistanceX = dot(dirToPortalCentroid, rayPortal.entryPortalInfo.planeBasis[0]);
        const float planeDistanceY = dot(dirToPortalCentroid, rayPortal.entryPortalInfo.planeBasis[1]);
        const bool cameraVolumeIntersectsPortal = 0.f < planeDistanceNormal && planeDistanceNormal < maximumNormalDistance
          && fabs(planeDistanceX) < rayPortal.entryPortalInfo.planeHalfExtents.x
          && fabs(planeDistanceY) < rayPortal.entryPortalInfo.planeHalfExtents.y;

        if (cameraVolumeIntersectsPortal) {
          portalIndexForVirtualInstances = i;
          cameraVolumePortalIntersectionMask |= (1 << i);
        }
      }

      // If the camera volume intersects exactly one portal, and the player model is closer to another portal,
      // that must mean the game is rendering the model at the other side of a portal (i.e. the player model is virtual/ghost).
      // This excludes the case when the camera intersects both portals.
      // De-virtualize the player model using the same portal that was used to virtualize it.
      const int playerModelFarPortalIndex = !playerModelNearPortalIndex;
      // Additional heuristic that tells if the player model eyes become closer to the camera if it's de-virtualized.
      // Fixes false virtual player model detections when there is one portal on a wall and another on the floor right next to it,
      // and you stand between these portals (see TREX-2254).
      const float playerModelEyeDistanceToCamera = length(playerModelEyePosition - camPos);
      const Vector3 devirtualizedPlayerModelEyePosition = (rayPortalPair->pairInfos[playerModelNearPortalIndex].portalToOpposingPortalDirection * Vector4(playerModelEyePosition, 1.f)).xyz();
      const float devirtualizedPlayerModelEyeDistanceToCamera = length(devirtualizedPlayerModelEyePosition - camPos);
      if (cameraVolumePortalIntersectionMask == (1 << playerModelFarPortalIndex) && devirtualizedPlayerModelEyeDistanceToCamera < playerModelEyeDistanceToCamera) {
        *out_PlayerModelIsVirtual = true;
        portalIndexForVirtualInstances = !portalIndexForVirtualInstances;
      }
      // In other (regular) situations, if the camera volume intersects at least one volume, make sure to use
      // the same portal for virtual player model as the one used for the virtual view model,
      // to avoid inconsistencies in tracing.
      else if (m_virtualInstancePortalIndex >= 0 && portalIndexForVirtualInstances >= 0) {
        portalIndexForVirtualInstances = m_virtualInstancePortalIndex;
      }
    }

    *out_NearPortalInfo = (portalIndexForVirtualInstances >= 0) ? &rayPortalPair->pairInfos[portalIndexForVirtualInstances] : nullptr;
    *out_FarPortalInfo = (portalIndexForVirtualInstances >= 0) ? &rayPortalPair->pairInfos[!portalIndexForVirtualInstances] : nullptr;
  }

  void InstanceManager::createPlayerModelVirtualInstances(Rc<DxvkContext> ctx, const CameraManager& cameraManager, const RayPortalManager& rayPortalManager) {
    if (m_playerModelInstances.empty())
      return;

    // Sometimes, the game renders the player model on the other side of the portal
    // that is closest to the camera. To detect that, we look at the model position.
    // Here, we also detect the instances of the portal gun that are rendered in the world
    // using the same mesh and texture as the held portal gun but should not be considered
    // a part of the player model. Those are detected by comparing their position to the body.
    
    // Find the instance marked with the "playerBody" material
    const RtInstance* bodyInstance = nullptr;
    for (RtInstance* instance : m_playerModelInstances) {
      if (instance->testCategoryFlags(InstanceCategories::ThirdPersonPlayerBody))
        bodyInstance = instance;
    }

    if (!bodyInstance)
      return;

    // Get the position from the transform matrix - works for Portal
    Vector3 playerModelPosition = bodyInstance->getTransform()[3].xyz();

    // Detect instances that are too far away from the body, make them regular objects.
    // This fixes the guns placed on pedestals to be picked up.
    filterPlayerModelInstances(playerModelPosition, bodyInstance);

    // Detect if the player model rendered by the game is virtual or not
    bool playerModelIsVirtual = false;
    // Near portal is where the original instance is
    const SingleRayPortalDirectionInfo* nearPortalInfo = nullptr;
    // Far portal is where the cloned instance will be
    const SingleRayPortalDirectionInfo* farPortalInfo = nullptr;
    detectIfPlayerModelIsVirtual(cameraManager, rayPortalManager, playerModelPosition, &playerModelIsVirtual, &nearPortalInfo, &farPortalInfo);
        
    const uint32_t frameId = m_device->getCurrentFrameId();

    // Set up the math to offset the player model backwards if it's to be shown in primary space
    float backwardOffset = RtxOptions::Get()->playerModel.backwardOffset();
    if (!RtxOptions::Get()->playerModel.enableInPrimarySpace())
      backwardOffset = 0.f;

    const bool createVirtualInstances = RtxOptions::Get()->playerModel.enableVirtualInstances() && (nearPortalInfo != nullptr);

    // The loop below creates virtual instances and applies the offset. Exit if neither is necessary.
    if (!createVirtualInstances && backwardOffset == 0.f)
      return;

    // Calculate the offset vector
    Vector3 backwardOffsetVector = cameraManager.getMainCamera().getHorizontalForwardDirection();
    backwardOffsetVector *= -backwardOffset;

    if (playerModelIsVirtual && farPortalInfo) {
      // Transform the offset vector into portal space
      backwardOffsetVector = (farPortalInfo->portalToOpposingPortalDirection * Vector4(backwardOffsetVector, 0.f)).xyz();
    }

    const Matrix4 backwardOffsetMatrix {
      { 1.f, 0.f, 0.f, 0.f },
      { 0.f, 1.f, 0.f, 0.f },
      { 0.f, 0.f, 1.f, 0.f },
      Vector4(backwardOffsetVector, 1.f)
    };
    
    // Create virtual instances for player model instances that are close to portals.
    // Offset both real and virtual instances by backwardOffset units if enabled.
    for (RtInstance* originalInstance : m_playerModelInstances) {

      if (backwardOffset != 0.f) {
        // Offset the original instance
        originalInstance->setCurrentTransform(backwardOffsetMatrix* originalInstance->getTransform());

        // Offset the original instance particles
        for (uint32_t i = 0; i < originalInstance->m_billboardCount; ++i) {
          m_billboards[originalInstance->m_firstBillboard + i].center += backwardOffsetVector;
        }
      }

      if (!createVirtualInstances)
        continue;
      
      // Don't pollute global instance id with Player Models since they're not tracked in game capturer
      const bool needValidGlobalInstanceId = false;

      RtInstance* clonedInstance = createInstanceCopy(*originalInstance, needValidGlobalInstanceId);
      
      clonedInstance->setFrameCreated(frameId);
      clonedInstance->setFrameLastUpdated(frameId);

      // Cloned player model instances are recreated every frame
      clonedInstance->markForGarbageCollection();

      // Compute the instance masks for both original and cloned instances.
      // When the original instance is real (which is the case normally), the cloned one is virtual and located on the other side of a portal.
      // When the original instance is virtual (rendered by the game on the other side of a portal), the cloned one is not.
      const uint32_t originalInstanceMask = playerModelIsVirtual ? OBJECT_MASK_PLAYER_MODEL_VIRTUAL : OBJECT_MASK_PLAYER_MODEL;
      const uint32_t clonedInstanceMask = playerModelIsVirtual ? OBJECT_MASK_PLAYER_MODEL : OBJECT_MASK_PLAYER_MODEL_VIRTUAL;

      if (originalInstance->m_billboardCount > 0) {
        // If this is a translucent instance with billboards, clone the billboards and hide the original instance.
        
        // Allocate some billboard entries first
        clonedInstance->m_firstBillboard = m_billboards.size();
        clonedInstance->m_billboardCount = originalInstance->m_billboardCount;
        m_billboards.resize(m_billboards.size() + originalInstance->m_billboardCount);

        // Copy the billboards to the new location and patch them
        for (uint32_t i = 0; i < originalInstance->m_billboardCount; ++i) {
          IntersectionBillboard* originalBillboard = &m_billboards[originalInstance->m_firstBillboard + i];
          IntersectionBillboard* clonedBillboard = &m_billboards[clonedInstance->m_firstBillboard + i];

          *clonedBillboard = *originalBillboard;
          clonedBillboard->instance = clonedInstance;

          // Update the instance masks of both instances
          originalBillboard->instanceMask = originalInstanceMask;
          clonedBillboard->instanceMask = clonedInstanceMask;

          // Update the center.
          // The orientation is irrelevant because the GPU will re-derive it for each ray.
          clonedBillboard->center = (nearPortalInfo->portalToOpposingPortalDirection * Vector4(originalBillboard->center, 1.0f)).xyz();
        }

        // Hide the geometric instances but keep them in the list so that surface data is generated for them.
        originalInstance->m_vkInstance.mask = 0;
        clonedInstance->m_vkInstance.mask = 0;
      }
      else {
        // Update the instance masks of both instances
        originalInstance->m_vkInstance.mask = originalInstanceMask;
        clonedInstance->m_vkInstance.mask = clonedInstanceMask;
      }
      
      // Update cloned instance transforms given the reference and the portal transform
      {
        // Set current frame transform
        Matrix4 objectToWorld = nearPortalInfo->portalToOpposingPortalDirection * originalInstance->getTransform();
        clonedInstance->setCurrentTransform(objectToWorld);

        // Note: only static portals are supported, so we reuse current frame portal state 
        // We don't check for intersections in previous frame since virtual instance needs prevFrame transform set regardless
        Matrix4 prevObjectToWorld = nearPortalInfo->portalToOpposingPortalDirection * originalInstance->getPrevTransform();
        clonedInstance->setPrevTransform(prevObjectToWorld);
      }

      // Use a clip plane to make sure that the cloned instance doesn't stick through a slab
      // that the other portal might be placed on.
      clonedInstance->surface.isClipPlaneEnabled = true;
      clonedInstance->surface.clipPlane = Vector4(farPortalInfo->entryPortalInfo.planeNormal,
        -dot(farPortalInfo->entryPortalInfo.planeNormal, farPortalInfo->entryPortalInfo.centroid));
      // Use the FORCE_NO_OPAQUE flag to enable any-hit processing in the visiblity rays for this clipped instance.
      clonedInstance->m_vkInstance.flags |= VK_GEOMETRY_INSTANCE_FORCE_NO_OPAQUE_BIT_KHR;

      // Same clip plane logic for the original instance, only using the near portal.
      originalInstance->surface.isClipPlaneEnabled = true;
      originalInstance->surface.clipPlane = Vector4(nearPortalInfo->entryPortalInfo.planeNormal,
        -dot(nearPortalInfo->entryPortalInfo.planeNormal, nearPortalInfo->entryPortalInfo.centroid));
      originalInstance->m_vkInstance.flags |= VK_GEOMETRY_INSTANCE_FORCE_NO_OPAQUE_BIT_KHR;
    }
  }

  void InstanceManager::findPortalForVirtualInstances(const CameraManager& cameraManager, const RayPortalManager& rayPortalManager) {
    m_virtualInstancePortalIndex = -1;

    // Virtual instances for the view model and the player model are generated for the closest portal to the camera.

    static_assert(maxRayPortalCount == 2);
    auto& rayPortalPair = *rayPortalManager.getRayPortalPairInfos().begin();

    if (!rayPortalPair.has_value())
      return;

    const Vector3& camPos = cameraManager.getCamera(CameraType::Main).getPosition(/* freecam = */ false);

    const float kMaxDistanceToPortal = RtxOptions::ViewModel::rangeMeters() * RtxOptions::Get()->getMeterToWorldUnitScale();

    // Find the closest valid portal to generate the instances for since we can generate 
    // virtual instances only for one of the portals due to instance mask bit allocation.
    // This will result in missing virtual viewModel geo for some corner cases, 
    // such as when portals are close to each other in a corner arrangement
    float minDistanceToPortal = FLT_MAX;

    for (uint i = 0; i < 2; i++) {
      const auto& rayPortal = rayPortalPair->pairInfos[i];
      const Vector3 dirToPortalCentroid = rayPortal.entryPortalInfo.centroid - camPos;
      const float distanceToPortal = length(dirToPortalCentroid);

      if (distanceToPortal <= kMaxDistanceToPortal &&
          distanceToPortal < minDistanceToPortal) {
        minDistanceToPortal = distanceToPortal;
        m_virtualInstancePortalIndex = rayPortal.entryPortalInfo.portalIndex;
      }
    }

  }

  void InstanceManager::createRayPortalVirtualViewModelInstances(const std::vector<RtInstance*>& viewModelReferenceInstances,
                                                                 const CameraManager& cameraManager,
                                                                 const RayPortalManager& rayPortalManager) {
    // Early out if there is no eligible portal
    if (m_virtualInstancePortalIndex < 0)
      return;

    if (rayPortalManager.getRayPortalPairInfos().empty()) {
      assert(!"There must be a portal pair in createRayPortalVirtualViewModelInstances if m_virtualInstancePortalIndex is defined");
      return;
    }

    if (!RtxOptions::ViewModel::enableVirtualInstances())
      return;

    const SingleRayPortalDirectionInfo& closestPortalInfo = rayPortalManager.getRayPortalPairInfos()[0]->pairInfos[m_virtualInstancePortalIndex];
    
    const uint32_t frameId = m_device->getCurrentFrameId();

    // Create virtual instances for view model instances that are close to portals
    for (RtInstance* referenceInstance : viewModelReferenceInstances) {

      // Create a view model virtual instance corresponding to the view model instance, for one frame

      // Don't pollute global instance id with View Models since they're not tracked in game capturer
      const bool needValidGlobalInstanceId = false;

      RtInstance* virtualInstance = createInstanceCopy(*referenceInstance, needValidGlobalInstanceId);

      virtualInstance->setFrameCreated(frameId);
      virtualInstance->setFrameLastUpdated(frameId);

      // Virtual view model instances are recreated every frame
      virtualInstance->markForGarbageCollection();

      // Virtual instances are to be visible only in their corresponding portal spaces
      static_assert(maxRayPortalCount == 2);
      // View model virtual instance
      virtualInstance->m_vkInstance.mask = OBJECT_MASK_VIEWMODEL_VIRTUAL;
    
      // Update virtual instance transforms given the reference and the portal transform
      {
        // Set current frame transform
        Matrix4 objectToWorld = closestPortalInfo.portalToOpposingPortalDirection * referenceInstance->getTransform();
        virtualInstance->setCurrentTransform(objectToWorld);

        // Note: only static portals are supported, so we reuse current frame portal state 
        // We don't check for intersections in previous frame since virtual instance needs prevFrame transform set regardless
        Matrix4 prevObjectToWorld = closestPortalInfo.portalToOpposingPortalDirection * referenceInstance->getPrevTransform();
        virtualInstance->setPrevTransform(prevObjectToWorld);
      }

      // Note this is an instance copy of an input reference. It is unknown to the source engine, so we don't call onInstanceAdded callbacks for it
      // It also results in this instance not being linked to reference instance BLAS and thus not considered in findSimilarInstances' lookups
      // This is desired as ViewModel instances are not to be linked frame to frame
    }
  }

  void InstanceManager::resetSurfaceIndices() {
    for (auto instance : m_instances)
      instance->m_surfaceIndex = BINDING_INDEX_INVALID;
  }

  // This function goes over all decals and offsets each one along its normal.
  // The offset is different per-decal and generally grows with every draw call and every decal in a draw call,
  // only wrapping around to start offset index when some limit is reached.
  // This offsetting takes care of procedural decals that are entirely coplanar, which doesn't work with
  // ray tracing because we want to hit every decal with a closest-hit shader, and without offsets we can't do that.
  // Some map geometry has static decals that are tessellated as odd non-quad meshes, but they still need to be offset,
  // so the second part of this function takes care of that.
  void InstanceManager::applyDecalOffsets(RtInstance& instance, const RasterGeometry& geometryData) {
    if (RtxOptions::Decals::offsetMultiplierMeters() == 0.f) {
      return;
    }

    if (instance.testCategoryFlags(InstanceCategories::DecalNoOffset))
      return;

    constexpr int indicesPerTriangle = 3;

    // Check if this is a supported geometry first
    if (geometryData.indexCount < indicesPerTriangle || geometryData.indexBuffer.indexType() != VK_INDEX_TYPE_UINT16)
      return;

    const bool hasDecalBeenOffset = geometryData.hashes[HashComponents::VertexPosition] == instance.m_lastDecalOffsetVertexDataVersion;

    // Exit if this instance has already been processed in its current version and the decal offset paramterization matches that of the last time it was offset
    // to prevent applying offsets to the same geometry multiple times.
    // This fixes the chamber information panels in Portal when you reload the same map multiple times in a row.
    // TODO: Move this to geom utils, only do on build
    if (hasDecalBeenOffset) {
      // Apply the decal offset difference that was applied to this instance previously to the global offset index 
      m_currentDecalOffsetIndex += instance.m_currentDecalOffsetDifference
                                 + RtxOptions::Decals::offsetIndexIncreaseBetweenDrawCalls();
      if (m_currentDecalOffsetIndex > RtxOptions::Decals::maxOffsetIndex()) {
        m_currentDecalOffsetIndex = RtxOptions::Decals::baseOffsetIndex();
      }
      return;
    }

    const GeometryBufferData bufferData(geometryData);

    // Check if the necessary buffers exist
    if (!bufferData.indexData || !bufferData.positionData)
      return;

    const bool isSingleOffsetDecalBatch = instance.testCategoryFlags(InstanceCategories::DecalSingleOffset);
    const uint32_t currentOffsetDecalBatchStartIndex = m_currentDecalOffsetIndex;
    const float offsetMultiplier = RtxOptions::Decals::offsetMultiplierMeters() * RtxOptions::Get()->getMeterToWorldUnitScale();

    auto getNextOffset = [this, &instance, isSingleOffsetDecalBatch, offsetMultiplier]() {
      const float offset = m_currentDecalOffsetIndex * offsetMultiplier;

      // Increment decal index and wrap it around to avoid moving them too far away from walls
      if (!isSingleOffsetDecalBatch && ++m_currentDecalOffsetIndex > RtxOptions::Decals::maxOffsetIndex()) {
        m_currentDecalOffsetIndex = RtxOptions::Decals::baseOffsetIndex();
      }

      return offset;
    };

    if (instance.testCategoryFlags(InstanceCategories::DecalDynamic)) {
      // It's a dynamic decal. Find all triangle quads and offset each quad individually.
      int fanStartIndexOffset = 0;
      bool fanNormalFound = false;
      Vector3 normal;

      // Go over all quads in this draw call.
      // Note: decals are often batched into a few draw calls, and we want to offset each decal separately.
      for (int indexOffset = 0; indexOffset + indicesPerTriangle <= geometryData.indexCount; indexOffset += indicesPerTriangle) {
        // Load indices for the current triangle
        uint16_t indices[indicesPerTriangle];
        for (size_t idx = 0; idx < indicesPerTriangle; ++idx) {
          indices[idx] = bufferData.getIndex(idx + indexOffset);
        }

        if (!fanNormalFound) {
          // Load the triangle vertices
          Vector3 triangleVertices[indicesPerTriangle];
          for (int idx = 0; idx < indicesPerTriangle; ++idx) {
            triangleVertices[idx] = bufferData.getPosition(indices[idx]);
          }

          // Compute the edges
          const Vector3 xVector = triangleVertices[2] - triangleVertices[1];
          const Vector3 yVector = triangleVertices[1] - triangleVertices[0];

          // Compute the normal, set the valid flag if the triangle is not degenerate
          normal = cross(xVector, yVector);
          const float normalLength = length(normal);
          if (normalLength > 0.f) {
            normal /= normalLength;
            fanNormalFound = true;
          }
        }

        // Detect if this triangle is the last one in a triangle fan
        const bool endOfStream = indexOffset + indicesPerTriangle * 2 > geometryData.indexCount;
        const bool endOfFan = endOfStream ||
          (bufferData.getIndex(indexOffset + indicesPerTriangle) != indices[0]) ||
          (bufferData.getIndex(indexOffset + indicesPerTriangle + 1) != indices[2]);
        if (!endOfFan)
          continue;

        if (fanNormalFound) {
          // Compute the offset
          const Vector3 positionOffset = normal * getNextOffset();

          // Apply the offset to all vertices of the triangle fan
          bufferData.getPosition(bufferData.getIndex(fanStartIndexOffset)) += positionOffset;
          bufferData.getPosition(bufferData.getIndex(fanStartIndexOffset + 1)) += positionOffset;
          for (int i = fanStartIndexOffset; i <= indexOffset; i += indicesPerTriangle) {
            bufferData.getPosition(bufferData.getIndex(i + 2)) += positionOffset;
          }
        }

        fanStartIndexOffset = indexOffset + indicesPerTriangle;
        fanNormalFound = false;
      }
    }
    else {
      // Maybe it's a BSP decal with irregular geometry?
      Vector3 decalNormal;
      bool decalNormalFound = false;

      // This set contains all indices of vertices that are used in a planar decal. The topology is unknown,
      // so a set is necessary to avoid offsetting some vertices more than once.
      // Use a static set to avoid freeing and re-allocating its memory on each decal.
      // Note: this makes the function not thread-safe, but that's OK
      static std::unordered_set<uint16_t> planeIndices;

      // Go over all triangles and see if they are coplanar
      for (int indexOffset = 0; indexOffset + indicesPerTriangle <= geometryData.indexCount; indexOffset += indicesPerTriangle) {
        // Load the triangle vertices
        uint16_t triangleIndices[indicesPerTriangle];
        Vector3 worldVertices[indicesPerTriangle];
        for (size_t idx = 0; idx < indicesPerTriangle; ++idx) {
          triangleIndices[idx] = bufferData.getIndex(idx + indexOffset);
          worldVertices[idx] = bufferData.getPosition(triangleIndices[idx]);
        }

        // Compute the edges
        const Vector3 xVector = worldVertices[2] - worldVertices[1];
        const Vector3 yVector = worldVertices[1] - worldVertices[0];

        // Compute the normal, skip the triangle if it's degenerate
        Vector3 normal = cross(xVector, yVector);
        const float normalLength = length(normal);
        if (normalLength == 0.f)
          continue;
        normal /= normalLength;

        if (decalNormalFound) {
          // If this is not the first valid triangle, compare its normal to a previously found one
          const float dotNormals = dot(decalNormal, normal);
          constexpr float kDegreesToRadians = float(M_PI / 180.0);
          static const float kCosParallelThreshold = cos(5.f * kDegreesToRadians);

          // Not coplanar - offset the previous plane and reset
          if (dotNormals < kCosParallelThreshold) {
            const Vector3 positionOffset = normal * getNextOffset();

            for (uint16_t idx : planeIndices) {
              bufferData.getPosition(idx) += positionOffset;
            }

            planeIndices.clear();
            decalNormalFound = false;
          }
        }
        else {
          // If this is a valid triangle, store its normal and indices
          decalNormalFound = true;
          decalNormal = normal;
        }

        for (size_t idx = 0; idx < indicesPerTriangle; ++idx) {
          planeIndices.insert(triangleIndices[idx]);
        }
      }

      // Offset the last (or the only) plane at the end of the loop
      if (decalNormalFound) {
        const Vector3 positionOffset = decalNormal * getNextOffset();

        for (uint16_t idx : planeIndices) {
          bufferData.getPosition(idx) += positionOffset;
        }
      }

      planeIndices.clear();
    }

    // Record the geometry hash to mark this decal is offsetted
    instance.m_lastDecalOffsetVertexDataVersion = geometryData.hashes[HashComponents::VertexPosition];

    // Increment the decal index now if it is a single offset decal batch
    if (isSingleOffsetDecalBatch) {
      ++m_currentDecalOffsetIndex;
    }

    const int32_t currentDecalOffsetDifference = static_cast<int32_t>(m_currentDecalOffsetIndex) - currentOffsetDecalBatchStartIndex;

    // Set to wrap around limit if wrap around (i.e. negative offset index difference is seen) occured
    instance.m_currentDecalOffsetDifference = instance.m_currentDecalOffsetDifference < 1
      ? RtxOptions::Decals::maxOffsetIndex()
      : currentDecalOffsetDifference;

    // We're done processing all the batched decals for the current instance. 
    // Apply the custom offsetting between decal draw calls.
    // -1 since the offset index has already been incremented after calculating offset for the previous decal
    m_currentDecalOffsetIndex += RtxOptions::Decals::offsetIndexIncreaseBetweenDrawCalls() - 1;
    if (m_currentDecalOffsetIndex > RtxOptions::Decals::maxOffsetIndex()) {
      m_currentDecalOffsetIndex = RtxOptions::Decals::baseOffsetIndex();
    }
  }

  inline bool isFpSpecial(float x) {
    const uint32_t u = *(uint32_t*) &x;
    return (u & 0x7f800000) == 0x7f800000;
  }

  void InstanceManager::createBillboards(RtInstance& instance, const Vector3& cameraViewDirection)
  {
    const RasterGeometry& geometryData = instance.getBlas()->input.getGeometryData();

    constexpr uint32_t indicesPerQuad = 6;

    // Check if this is a supported geometry first
    if (geometryData.indexCount < indicesPerQuad || 
        (geometryData.indexCount % indicesPerQuad) != 0 ||
        geometryData.indexBuffer.indexType() != VK_INDEX_TYPE_UINT16 ||
        geometryData.topology != VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST)
      return;
    
    const GeometryBufferData bufferData(geometryData);

    // Check if the necessary buffers exist
    // Warning: do not generate billboards for instances without indices as other code sections using billboards expect indices to be present
    if (!bufferData.indexData || !bufferData.positionData || !bufferData.texcoordData)
      return;

    const bool hasNonIdentityTextureTransform = instance.surface.textureTransform != Matrix4();
    bool bSuccess = true;
    bool areAllBillboardsValidIntersectionCandidates = true;
    uint32_t billboardCount = 0;
    instance.m_firstBillboard = m_billboards.size();

    const Matrix4 instanceTransform = instance.getTransform();

    // Go over all quads in this draw call.
    // Note: decals are often batched into a few draw calls, and we want to offset each decal separately.
    for (int indexOffset = 0; indexOffset + indicesPerQuad <= geometryData.indexCount; indexOffset += indicesPerQuad) {
      // Load indices for a quad
      uint16_t indices[indicesPerQuad];
      for (size_t idx = 0; idx < indicesPerQuad; ++idx) {
        indices[idx] = bufferData.getIndex(idx + indexOffset);
      }


      // Make sure that these indices follow a known quad pattern: A, B, C, A, C, D
      // If they don't, we can't process this "quad" - so, cancel the whole instance.
      if (indices[0] != indices[3] || indices[2] != indices[4]) {
        ONCE(Logger::warn("[RTX] InstanceManager: detected unsupported quad index layout for billboard creation"));
        // This quad is incompatible altogether. Abort processing billboards for this instance and skip billboard processing for it
        bSuccess = false;
        break;
      }
      
      // Load data for a triangle
      Vector3 positions[3];
      Vector2 texcoords[4];
      uint8_t vertexOpacities8bit[4] = {};
      
      for (size_t idx = 0; idx < 3; ++idx) {
        const uint16_t currentIndex = indices[idx];

        Vector4 objectSpacePosition = Vector4(bufferData.getPosition(currentIndex), 1.0f);

        positions[idx] = (instanceTransform * objectSpacePosition).xyz();

        texcoords[idx] = bufferData.getTexCoord(currentIndex);

        if (hasNonIdentityTextureTransform)
          texcoords[idx] = (instance.surface.textureTransform * Vector4(texcoords[idx].x, texcoords[idx].y, 0.f, 1.f)).xy();

        if (bufferData.vertexColorData)
          vertexOpacities8bit[idx] = bufferData.getVertexColor(indices[idx]) >> 24;
      }

      // Load one vertex color - assuming that the entire billboard uses the same color
      uint32_t vertexColor = ~0u;
      if (bufferData.vertexColorData)
        vertexColor = bufferData.getVertexColor(indices[0]);

      // Compute the normal
      const Vector3 xVector { positions[2] - positions[1] };
      const Vector3 yVector { positions[1] - positions[0] };
      const Vector3 center { (positions[2] + positions[0]) * 0.5f };

      IntersectionBillboard billboard;

      const bool centerIsSpecial = isFpSpecial(center.x) || isFpSpecial(center.y) || isFpSpecial(center.z);
      if (centerIsSpecial) {
        areAllBillboardsValidIntersectionCandidates = false;
      }

      const float xLength = length(xVector);
      const float yLength = length(yVector);
      const float dotAxes = dot(xVector, yVector) / (xLength * yLength);
      // Note: This could probably be handled in a better way (like skipping this quad) rather than just assigning
      // a fallback normal, but this is simple enough.
      const Vector3 normal = safeNormalize(cross(xVector, yVector), Vector3(0.0f, 0.0f, 1.0f));
      const float normalDotCamera = dot(normal, cameraViewDirection);


      // Limit the set of particles that are turned into intersection primitives:
      // - Must be roughly square
      const bool isSquare = xLength <= yLength * 1.5f && yLength <= xLength * 1.5f;
      // - The original quad must have perpendicular sides
      const bool hasPerpendicularSides = fabs(dotAxes) < 0.01f;
      // - Must be in the camera view plane, i.e. only auto-oriented particles, not world-space ones
      //   (except player model particles, which are oriented towards the camera and not in the view plane)
      const bool isInViewPlane = fabs(normalDotCamera) > 0.99f;
      // Assume that all billboards on the player model are camera facing
      const bool isCameraFacing = instance.m_isPlayerModel;
      if (!isSquare || !hasPerpendicularSides || !isInViewPlane && !isCameraFacing) {
        areAllBillboardsValidIntersectionCandidates = false;
      }

      const Vector2 xVectorUV { texcoords[2] - texcoords[1] };
      const Vector2 yVectorUV { texcoords[1] - texcoords[0] };
      const Vector2 centerUV { (texcoords[2] + texcoords[0]) * 0.5f };

      // Fill in data for the quad's last/4th vertex
      texcoords[3] = bufferData.getTexCoord(indices[5]);
      if (bufferData.vertexColorData)
        vertexOpacities8bit[3] = bufferData.getVertexColor(indices[5]) >> 24;

      billboard.center = center;
      billboard.xAxis = xVector / xLength;
      billboard.width = xLength;
      billboard.yAxis = yVector / yLength;
      billboard.height = yLength;
      billboard.xAxisUV = xVectorUV * 0.5f;
      billboard.yAxisUV = yVectorUV * 0.5f;
      billboard.centerUV = centerUV;
      billboard.instance = &instance;
      billboard.vertexColor = vertexColor;
      billboard.instanceMask = instance.getVkInstance().mask & OBJECT_MASK_UNORDERED_ALL_INTERSECTION_PRIMITIVE;
      billboard.texCoordHash = XXH64(texcoords, sizeof(texcoords), kEmptyHash);
      billboard.vertexOpacityHash = XXH64(vertexOpacities8bit, sizeof(vertexOpacities8bit), kEmptyHash);
      billboard.allowAsIntersectionPrimitive = true;
      billboard.isBeam = false;
      billboard.isCameraFacing = isCameraFacing;
      m_billboards.push_back(billboard);
      ++billboardCount;
    }

    if (bSuccess) {
      instance.m_billboardCount = billboardCount;

      if (areAllBillboardsValidIntersectionCandidates) {
        // Update the instance mask to hide it from rays that look only for intersection billboards.
        instance.getVkInstance().mask &= OBJECT_MASK_UNORDERED_ALL_GEOMETRY;
      } else {
        // Disable the rest of the billboards as intersection primitives since only a single mask can be used
        // per instance
        for (uint32_t i = m_billboards.size() - instance.m_billboardCount; i < m_billboards.size(); i++) {
          IntersectionBillboard& billboard = m_billboards[i];
          billboard.allowAsIntersectionPrimitive = false;
        }
      }
    } else {
      // Revert the billboards that were created successfully before the first failure,
      // because one of the failed to be created
      m_billboards.erase(m_billboards.end() - billboardCount, m_billboards.end());
    }
  }

  void InstanceManager::createBeams(RtInstance& instance) {
    const RasterGeometry& geometryData = instance.getBlas()->input.getGeometryData();

    // Check if this is a supported geometry first
    if (geometryData.indexCount < 4 ||
        (geometryData.indexCount % 2) != 0 ||
        geometryData.indexBuffer.indexType() != VK_INDEX_TYPE_UINT16 ||
        geometryData.topology != VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP)
      return;

    const GeometryBufferData bufferData(geometryData);

    // Check if the necessary buffers exist
    if (!bufferData.indexData || !bufferData.positionData || !bufferData.texcoordData)
      return;

    // Extract the beams from the triangle strip.
    // Start by loading the first 2 indices.
    uint16_t indices[4];
    indices[0] = bufferData.getIndex(0);
    indices[1] = bufferData.getIndex(1);

    for (int index = 2; index < geometryData.indexCount - 1; index += 2) {
      // When there are multiple beams packed into one triangle strip, they are separated
      // by a pair of repeating indices, such as: (0 1 2 3) 3 4 (4 5 6 7)
      // We want to keep looking at indices until either the end of the strip is reached,
      // or until we detect such a repeating pair. In the latter case, we skip the pair
      // at the end of this loop.
      const bool endOfStrip = index >= geometryData.indexCount - 2;
      const bool restart = !endOfStrip && (bufferData.getIndex(index + 1) == bufferData.getIndex(index + 2));

      if (!endOfStrip && !restart)
        continue;

      // Load the indices of the last 2 vertices of the beam.
      indices[2] = bufferData.getIndex(index);
      indices[3] = bufferData.getIndex(index + 1);

      // Load the source data for the 4 vertices that define our beam.
      Vector3 positions[4];
      Vector2 texcoords[4];
      for (int i = 0; i < 4; ++i) {
        positions[i] = bufferData.getPosition(indices[i]);
        texcoords[i] = bufferData.getTexCoord(indices[i]);
      }

      // Load one vertex color - assuming that the entire beam uses the same color
      uint32_t vertexColor = ~0u;
      if (bufferData.vertexColorData)
        vertexColor = bufferData.getVertexColor(indices[0]);

      // Extract the beam cylinder axis, length and width from the vertices.
      // Note that the 4 vertices are not necessarily coplanar: the beam is tessellated
      // in the axial direction, and each segment is rotated separately to face the camera.
      // The vertices are laid out in a triangle strip order:
      //     0-2
      //  -- |/| --> axis
      //     1-3
      const Vector3 startPosition = (positions[0] + positions[1]) * 0.5f;
      const Vector3 endPosition = (positions[2] + positions[3]) * 0.5f;
      const float beamWidth = length(positions[1] - positions[0]);
      const float beamLength = length(endPosition - startPosition);

      // Fill out the billboard struct.
      IntersectionBillboard billboard;
      billboard.center = (startPosition + endPosition) * 0.5f;
      billboard.xAxis = normalize(positions[1] - positions[0]);
      billboard.width = beamWidth;
      billboard.yAxis = normalize(endPosition - startPosition);
      billboard.height = beamLength;
      billboard.xAxisUV = (texcoords[1] - texcoords[0]) * 0.5f;
      billboard.yAxisUV = (texcoords[2] - texcoords[0]) * 0.5f;
      billboard.centerUV = (texcoords[0] + texcoords[3]) * 0.5f;
      billboard.vertexColor = vertexColor;
      billboard.instanceMask = instance.getVkInstance().mask & OBJECT_MASK_UNORDERED_ALL_INTERSECTION_PRIMITIVE;
      billboard.instance = &instance;
      billboard.texCoordHash = 0;
      billboard.vertexOpacityHash = 0;
      billboard.allowAsIntersectionPrimitive = true;
      billboard.isBeam = true;
      m_billboards.push_back(billboard);

      // If there are enough vertices left in the strip to fit one more beam, after the separator pair,
      // skip the separator and load the first two indices of the next beam.
      if (index <= geometryData.indexCount - 8) {
        index += 4;
        indices[0] = bufferData.getIndex(index);
        indices[1] = bufferData.getIndex(index + 1);
      }
    }

    instance.getVkInstance().mask &= OBJECT_MASK_UNORDERED_ALL_GEOMETRY;

    // Note: setting the instance's billboardCount to 0 here because we don't need either of the uses of that count:
    // - Beams cannot be parts of a player model;
    // - Beams should not be split into quads for OMM reuse.
    instance.m_billboardCount = 0;
  }

  const XXH64_hash_t RtInstance::calculateAntiCullingHash() const {
    if (RtxOptions::AntiCulling::Object::enable()) {
      const Vector3 pos = getWorldPosition();
      const XXH64_hash_t posHash = XXH3_64bits(&pos, sizeof(pos));
      XXH64_hash_t antiCullingHash = XXH3_64bits_withSeed(&m_materialDataHash, sizeof(XXH64_hash_t), posHash);

      if (RtxOptions::AntiCulling::Object::hashInstanceWithBoundingBoxHash() &&
          RtxOptions::Get()->needsMeshBoundingBox()) {
        const AxisAlignedBoundingBox& boundingBox = getBlas()->input.getGeometryData().boundingBox;
        const XXH64_hash_t bboxHash = boundingBox.calculateHash();
        antiCullingHash = XXH3_64bits_withSeed(&bboxHash, sizeof(antiCullingHash), antiCullingHash);
      }
      return antiCullingHash;
    }

    return XXH64_hash_t();
  }
}  // namespace dxvk
