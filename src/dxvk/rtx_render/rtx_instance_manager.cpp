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
#include <assert.h>
#include <atomic>
#include <cstring>
#include <mutex>
#include <vector>

#include "rtx_context.h"
#include "rtx_scene_manager.h"
#include "rtx_instance_manager.h"
#include "rtx_camera_manager.h"
#include "rtx_options.h"
#include "rtx_materials.h"
#include "rtx_ray_portal_manager.h"
#include "rtx_terrain_baker.h"

#include "../d3d9/d3d9_state.h"
#include "rtx_matrix_helpers.h"
#include "dxvk_scoped_annotation.h"

#include "rtx/pass/common_binding_indices.h"
#include "rtx/concept/surface_material/surface_material_hitgroup.h"
#include "rtx/pass/instance_definitions.h"

namespace dxvk {

  namespace {
    std::atomic<uint64_t> s_nextRtInstanceCacheIdentity { 1 };

    uint64_t nextRtInstanceCacheIdentity() {
      return s_nextRtInstanceCacheIdentity.fetch_add(1, std::memory_order_relaxed);
    }

#ifndef NDEBUG
    constexpr int kDestroyedRtInstanceMemoryPattern = 0xDD;
#endif
  }
  
  static bool isMirrorTransform(const Matrix4& m) {
    // Note: Identify if the winding is inverted by checking if the z axis is ever flipped relative to what it's expected to be for clockwise vertices in a lefthanded space
    // (x cross y) through the series of transformations
    Vector3 x(m[0].data), y(m[1].data), z(m[2].data);
    return dot(cross(x, y), z) < 0;
  }

  static uint32_t determineInstanceFlags(const DrawCallState& drawCall, const RtSurface& surface) {
    // Determine if the view inverts face winding globally
    const Matrix4 worldToProjection = drawCall.getTransformData().viewToProjection * drawCall.getTransformData().worldToView;
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
    
    if (!RtxOptions::enableCulling())
      flags |= VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;

    // This check can be overridden by replacement assets.
    if (drawCall.getMaterialData().blendMode.enableBlending && !surface.alphaState.isDecal && !drawCall.getGeometryData().forceCullBit)
      flags |= VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;

    // Disable culling for baked terrain instances when the option is enabled
    // Terrain with back face culling enabled may flicker in some circumstances.  
    // Forcing the geometry to be double-sided fixes the flicker, but may be undesireable in some games.
    if (TerrainBaker::disableBackFaceCulling() && drawCall.testCategoryFlags(InstanceCategories::Terrain)) {
      flags |= VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
    }

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
    , m_cacheIdentity(nextRtInstanceCacheIdentity())
    , m_surfaceIndex(SURFACE_INDEX_INVALID)
    , m_previousSurfaceIndex(SURFACE_INDEX_INVALID) { }

  // Makes a copy of an instance
  RtInstance::RtInstance(const RtInstance& src, uint64_t id, uint32_t instanceVectorId)
    : m_id(id)
    , m_instanceVectorId(instanceVectorId)
    , m_cacheIdentity(nextRtInstanceCacheIdentity())
    , m_surfaceIndex(SURFACE_INDEX_INVALID)
    , m_previousSurfaceIndex(SURFACE_INDEX_INVALID) {
    copyInstanceDataFrom(src);
  }

#ifndef NDEBUG
  void RtInstance::operator delete(void* ptr) noexcept {
    if (ptr != nullptr) {
      std::memset(ptr, kDestroyedRtInstanceMemoryPattern, sizeof(RtInstance));
    }

    ::operator delete(ptr);
  }

  void RtInstance::operator delete(void* ptr, std::size_t) noexcept {
    RtInstance::operator delete(ptr);
  }
#endif

  // Ensure copyInstanceDataFrom copies all needed members when size changes, and update the object size check.
  // Note: The object has a different size on Debug builds. 
  //       Checking the non-Debug flavors is good enough for the sake of convenience of tracking just a single size.
 #if defined(DEBUG_OPTIMIZED) || defined(NDEBUG)
  namespace {
    template<int RtInstanceSize> struct CheckRtInstanceSize {
      // The second line of the build error should contain the new size of RtInstance in the template argument, i.e. `dxvk::CheckRtInstanceSize<newSize>`
      static_assert(RtInstanceSize == 768, "RtInstance size has changed.  Fix the copy constructor above this message, then update the expected size.");
    };
    CheckRtInstanceSize<sizeof(RtInstance)> _rtInstanceSizeTest;
  }
 #endif

  void RtInstance::setBlas(BlasEntry& blas) {
    m_linkedBlas = &blas;
  }

  void RtInstance::copyInstanceDataFrom(const RtInstance& src) {
    surface = src.surface;

    m_seenCameraTypes = src.m_seenCameraTypes;
    m_materialType = src.m_materialType;
    m_albedoOpacityTextureIndex = src.m_albedoOpacityTextureIndex;
    m_samplerIndex = src.m_samplerIndex;
    m_secondaryOpacityTextureIndex = src.m_secondaryOpacityTextureIndex;
    m_secondarySamplerIndex = src.m_secondarySamplerIndex;
    m_isAnimated = src.m_isAnimated;
    m_opacityMicromapInstanceData = src.m_opacityMicromapInstanceData;
    m_opacityMicromapInstanceData.resetCopiedRequestState();
    m_surfaceIndex = src.m_surfaceIndex;
    m_previousSurfaceIndex = src.m_previousSurfaceIndex;
    m_isHidden = src.m_isHidden;
    m_isPlayerModel = src.m_isPlayerModel;
    m_isWorldSpaceUI = src.m_isWorldSpaceUI;
    m_isUnordered = src.m_isUnordered;
    m_isObjectToWorldMirrored = src.m_isObjectToWorldMirrored;
    m_isSubsurface = src.m_isSubsurface;
    m_linkedBlas = src.m_linkedBlas;
    m_materialHash = src.m_materialHash;
    m_materialDataHash = src.m_materialDataHash;
    m_texcoordHash = src.m_texcoordHash;
    m_indexHash = src.m_indexHash;
    m_vkInstance = src.m_vkInstance;
    m_geometryFlags = src.m_geometryFlags;
    m_firstBillboard = src.m_firstBillboard;
    m_billboardCount = src.m_billboardCount;
    m_categoryFlags = src.m_categoryFlags;

    // Intentionally NOT synced (identity / lifecycle / per-build state):
    //   m_id, m_instanceVectorId, m_cacheIdentity, m_isMarkedForGC, m_isUnlinkedForGC,
    //   m_isInsideFrustum, m_frameLastUpdated, m_frameCreated,
    //   m_isCreatedByRenderer, m_spatialCacheHash,
    //   OMM request registration state,
    //   m_primInstanceOwner, buildGeometries, buildRanges,
    //   billboardIndices, indexOffsets, m_blasDirty,
    //   m_billboardGeometryDirty
  }

  void RtInstance::updateFromReference(const RtInstance& src, const bool preserveTransforms) {
    // Optionally preserve the persistent instance's corrected transforms so that
    // callers that apply an absolute corrected transform afterward can detect
    // whether the transform actually changed between frames.
    const Matrix4 savedObjectToWorld = surface.objectToWorld;
    const Matrix4 savedPrevObjectToWorld = surface.prevObjectToWorld;
    const Matrix3 savedNormalObjectToWorld = surface.normalObjectToWorld;
    const VkTransformMatrixKHR savedVkTransform = m_vkInstance.transform;

    copyInstanceDataFrom(src);

    if (preserveTransforms) {
      // Restore transforms; the caller applies the corrected transform afterward.
      surface.objectToWorld = savedObjectToWorld;
      surface.prevObjectToWorld = savedPrevObjectToWorld;
      surface.normalObjectToWorld = savedNormalObjectToWorld;

      // Restore VK instance transform (was overwritten by the m_vkInstance copy above).
      m_vkInstance.transform = savedVkTransform;
    }

    // Mark dirty so the incremental BLAS cache treats this instance as changed.
    m_blasDirty = true;
    m_billboardGeometryDirty = true;

  }

  void RtInstance::onTransformChanged() {
    // The D3D matrix on input, needs to be transposed before feeding to the VK API (left/right handed conversion)
    // NOTE: VkTransformMatrixKHR is 4x3 matrix, and Matrix4 is 4x4
    const auto t = transpose(surface.objectToWorld);
    memcpy(&m_vkInstance.transform, &t, sizeof(VkTransformMatrixKHR));
  }

  bool RtInstance::teleport(const Matrix4& objectToWorld) {
    surface.objectToWorld = objectToWorld;
    surface.normalObjectToWorld = transpose(inverse(Matrix3(surface.objectToWorld)));
    surface.prevObjectToWorld = objectToWorld;


    // The D3D matrix on input, needs to be transposed before feeding to the VK API (left/right handed conversion)
    // NOTE: VkTransformMatrixKHR is 4x3 matrix, and Matrix4 is 4x4
    const auto t = transpose(surface.objectToWorld);
    memcpy(&m_vkInstance.transform, &t, sizeof(VkTransformMatrixKHR));
    
    m_blasDirty = true;
    return false; // freshly teleported instances are always treated as still.
  }

  bool RtInstance::teleport(const Matrix4& objectToWorld, const Matrix4& prevObjectToWorld) {
    surface.objectToWorld = objectToWorld;
    surface.normalObjectToWorld = transpose(inverse(Matrix3(surface.objectToWorld)));
    surface.prevObjectToWorld = prevObjectToWorld;
    onTransformChanged();
    m_blasDirty = true;

    return memcmp(surface.prevObjectToWorld.data, surface.objectToWorld.data, sizeof(Matrix4)) != 0;
  }

  void RtInstance::teleportWithHistory(const Matrix4& oldToNew) {
    surface.objectToWorld = oldToNew * surface.objectToWorld;
    surface.normalObjectToWorld = transpose(inverse(Matrix3(surface.objectToWorld)));
    surface.prevObjectToWorld = oldToNew * surface.prevObjectToWorld;
    onTransformChanged();
    m_blasDirty = true;

    if (m_primInstanceOwner.isRoot(this)) {
      // this is the root of a replacement - need to update the transform history for all the instances in the replacement.
      for (size_t i = 0; i < m_primInstanceOwner.getReplacementInstance()->prims.size(); i++) {
        RtInstance* instance = m_primInstanceOwner.getReplacementInstance()->prims[i].getInstance();
        if (instance != nullptr && instance != this) {
          instance->teleportWithHistory(oldToNew);
        }
      }
    }
  }
  
  bool RtInstance::move(const Matrix4& objectToWorld) {
    surface.prevObjectToWorld = surface.objectToWorld;
    surface.objectToWorld = objectToWorld;
    surface.normalObjectToWorld = transpose(inverse(Matrix3(objectToWorld)));
    onTransformChanged();

    // See if the transform has changed even a tiny bit.
    // The result is used for the 'isStatic' surface flag, which is in turn used to skip motion vector calculation
    // on the GPU. We need nonzero motion vectors on objects moving even slightly to make RTXDI temporal bias correction work.
    // This comparison is not robust if the transforms are reconstructed from baked object-to-view matrices,
    // but it works well e.g. in Portal. Even if it detects truly static objects as moving, that's fine because that will only
    // have a minor performance effect of calculation extra motion vectors.
    return memcmp(surface.prevObjectToWorld.data, surface.objectToWorld.data, sizeof(Matrix4)) != 0;
  }

  bool RtInstance::moveAgain(const Matrix4& objectToWorld) {
    surface.objectToWorld = objectToWorld;
    surface.normalObjectToWorld = transpose(inverse(Matrix3(objectToWorld)));
    onTransformChanged();

    // See comment in move()
    return memcmp(surface.prevObjectToWorld.data, surface.objectToWorld.data, sizeof(Matrix4)) != 0;
  }

  void RtInstance::setFrameCreated(const uint32_t frameIndex) {
    m_frameCreated = frameIndex;
  }

  // Sets frame id of last update, if this is the first time the frame id is set
  // instance's per frame state is reset as well
  // Returns true if this is the first update this frame
  bool RtInstance::setFrameLastUpdated(const uint32_t frameIndex) {
    if (m_frameLastUpdated != frameIndex) {
      m_seenCameraTypes.clrAll();

      m_frameLastUpdated = frameIndex;

      return true;
    }

    return false;
  }

  void RtInstance::markForGarbageCollection() const {
    m_isMarkedForGC = true;
  }


  bool RtInstance::registerCamera(CameraType::Enum cameraType, uint32_t frameIndex) {
    const bool settingNewCameraType = !m_seenCameraTypes.test(cameraType);

    if (settingNewCameraType)
      m_seenCameraTypes.set(cameraType);

    return settingNewCameraType;
  }

  bool RtInstance::isCameraRegistered(CameraType::Enum cameraType) const {
    return m_seenCameraTypes.test(cameraType);
  }

  void RtInstance::setCustomIndexBit(uint32_t oneBitMask, bool value) {
    m_vkInstance.instanceCustomIndex = setBit(m_vkInstance.instanceCustomIndex, value, oneBitMask);
  }

  bool RtInstance::getCustomIndexBit(uint32_t oneBitMask) const {
    return m_vkInstance.instanceCustomIndex & oneBitMask;
  }

  bool RtInstance::isOpaque() const {
    return getMaterialType() == MaterialDataType::Opaque;
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

  void RtInstance::printDebugInfo() const {
#ifdef REMIX_DEVELOPMENT
    Logger::warn(str::format(
      "=== RtInstance Debug Info ===\n",
      "ID: ", m_id, "\n",
      "Vector Index: ", m_instanceVectorId, "\n",
      "Frame Created: ", m_frameCreated, "\n",
      "Frame Last Updated: ", m_frameLastUpdated, "\n",
      "Frame Age: ", getFrameAge(), "\n",
      "\n",
      "=== Transform Info ===\n",
      "World Position: (", getWorldPosition().x, ", ", getWorldPosition().y, ", ", getWorldPosition().z, ")\n",
      "Transform Matrix:\n",
      "  [", getTransform()[0][0], ", ", getTransform()[0][1], ", ", getTransform()[0][2], ", ", getTransform()[0][3], "]\n",
      "  [", getTransform()[1][0], ", ", getTransform()[1][1], ", ", getTransform()[1][2], ", ", getTransform()[1][3], "]\n",
      "  [", getTransform()[2][0], ", ", getTransform()[2][1], ", ", getTransform()[2][2], ", ", getTransform()[2][3], "]\n",
      "  [", getTransform()[3][0], ", ", getTransform()[3][1], ", ", getTransform()[3][2], ", ", getTransform()[3][3], "]\n",
      "Previous World Position: (", getPrevWorldPosition().x, ", ", getPrevWorldPosition().y, ", ", getPrevWorldPosition().z, ")\n",
      "\n",
      "=== BLAS Info ===\n",
      "Linked BLAS: ", m_linkedBlas ? "Valid" : "Null"));
    
    if (m_linkedBlas) {
      Logger::warn("=== BLAS Entry Debug Info ===");
      m_linkedBlas->printDebugInfo("(from RtInstance)");
      Logger::warn("=== End BLAS Entry Debug Info ===");
      
      // Print DrawCallState info
      Logger::warn("=== DrawCallState Debug Info ===");
      m_linkedBlas->input.printDebugInfo("(from RtInstance)");
      Logger::warn("=== End DrawCallState Debug Info ===");
    }
    
    // Print RtSurface info
    Logger::warn("=== RtSurface Debug Info ===");
    surface.printDebugInfo("(from RtInstance)");
    Logger::warn("=== End RtSurface Debug Info ===");
    
    Logger::warn(str::format(
      "=== Hash Info ===\n",
      "Material Hash: 0x", std::hex, m_materialHash, std::dec, "\n",
      "Material Data Hash: 0x", std::hex, m_materialDataHash, std::dec, "\n",
      "Texcoord Hash: 0x", std::hex, m_texcoordHash, std::dec, "\n",
      "Index Hash: 0x", std::hex, m_indexHash, std::dec, "\n",
      "\n",
      "=== Vulkan Instance Info ===\n",
      "VK Instance Mask: ", m_vkInstance.mask, "\n",
      "VK Instance Flags: ", m_vkInstance.flags, "\n",
      "VK Instance Custom Index: ", m_vkInstance.instanceCustomIndex, "\n",
      "VK Instance SBT Record Offset: ", m_vkInstance.instanceShaderBindingTableRecordOffset, "\n",
      "\n",
      "=== Material Info ===\n",
      "Material Type: ", static_cast<int>(m_materialType), "\n",
      "Albedo Opacity Texture Index: ", m_albedoOpacityTextureIndex, "\n",
      "Sampler Index: ", m_samplerIndex, "\n",
      "Secondary Opacity Texture Index: ", m_secondaryOpacityTextureIndex, "\n",
      "Secondary Sampler Index: ", m_secondarySamplerIndex, "\n",
      "\n",
      "=== Surface Info ===\n",
      "Surface Index: ", m_surfaceIndex, "\n",
      "Previous Surface Index: ", m_previousSurfaceIndex, "\n",
      "\n",
      "=== Billboard Info ===\n",
      "First Billboard Index: ", m_firstBillboard, "\n",
      "Billboard Count: ", m_billboardCount, "\n",
      "\n",
      "=== Geometry Info ===\n",
      "Geometry Flags: ", m_geometryFlags, "\n",
      "\n",
      "=== Boolean Flags ===\n",
      "Is Hidden: ", m_isHidden ? "true" : "false", "\n",
      "Is Player Model: ", m_isPlayerModel ? "true" : "false", "\n",
      "Is World Space UI: ", m_isWorldSpaceUI ? "true" : "false", "\n",
      "Is Unordered: ", m_isUnordered ? "true" : "false", "\n",
      "Is Object To World Mirrored: ", m_isObjectToWorldMirrored ? "true" : "false", "\n",
      "Is Created By Renderer: ", m_isCreatedByRenderer ? "true" : "false", "\n",
      "Is Animated: ", m_isAnimated ? "true" : "false", "\n",
      "Is Front Face Flipped: ", isFrontFaceFlipped ? "true" : "false", "\n",
      "\n",
      "=== Garbage Collection Flags ===\n",
      "Is Marked For GC: ", m_isMarkedForGC ? "true" : "false", "\n",
      "\n",
      "=== View Model Flags ===\n",
      "Is View Model: ", isViewModel() ? "true" : "false", "\n",
      "Is View Model Non Reference: ", isViewModelNonReference() ? "true" : "false", "\n",
      "Is View Model Reference: ", isViewModelReference() ? "true" : "false", "\n",
      "Is View Model Virtual: ", isViewModelVirtual() ? "true" : "false", "\n",
      "\n",
      "=== Category Info ===\n",
      "Category Flags: ", m_categoryFlags.raw(), "\n",
      "\n",
      "=== Camera Types ===\n",
      "Seen Camera Types Mask: ", m_seenCameraTypes.raw()));

    for (uint32_t type = 0; type < CameraType::Count; ++type) {
      if (m_seenCameraTypes.test(static_cast<CameraType::Enum>(type))) {
        Logger::warn(str::format("  Camera Type: ", type));
      }
    }
    
    Logger::warn(str::format(
      "\n=== Billboard Indices ===\n",
      "Billboard Indices Count: ", billboardIndices.size()));
    
    for (size_t i = 0; i < std::min(billboardIndices.size(), size_t(5)); ++i) {
      Logger::warn(str::format("  Billboard Index ", i, ": ", billboardIndices[i]));
    }
    if (billboardIndices.size() > 5) {
      Logger::warn(str::format("  ... and ", billboardIndices.size() - 5, " more"));
    }
    
    Logger::warn(str::format(
      "\n=== Index Offsets ===\n",
      "Index Offsets Count: ", indexOffsets.size()));
    
    for (size_t i = 0; i < std::min(indexOffsets.size(), size_t(5)); ++i) {
      Logger::warn(str::format("  Index Offset ", i, ": ", indexOffsets[i]));
    }
    if (indexOffsets.size() > 5) {
      Logger::warn(str::format("  ... and ", indexOffsets.size() - 5, " more"));
    }
    
    Logger::warn("=== End RtInstance Debug Info ===");
#endif
}

  InstanceManager::InstanceManager(DxvkDevice* device, ResourceCache* pResourceCache)
    : CommonDeviceObject(device)
    , m_pResourceCache(pResourceCache) {
  }

  InstanceManager::~InstanceManager() {
#ifndef NDEBUG
    releaseDestroyedInstanceQuarantine();
#endif
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
    notifySceneChanged();
    for (RtInstance* instance : m_instances) {
      removeInstance(instance);
      destroyInstanceAllocation(instance);
    }

    m_instances.clear();
    m_viewModelCandidates.clear();
    m_playerModelInstances.clear();

    // Persistent maps contain pointers into m_instances which are now deleted.
    m_persistentViewModelInstances.clear();
    m_persistentVirtualViewModelInstances.clear();
    m_persistentPlayerModelClones.clear();
  }  

  void InstanceManager::cleanupPersistentMap(
      std::unordered_map<RtInstance*, RtInstance*>& map,
      const std::unordered_set<RtInstance*>& activeReferences) {
    for (auto it = map.begin(); it != map.end(); ) {
      if (activeReferences.find(it->first) == activeReferences.end()) {
        // Reference is gone — mark the derived instance for GC.
        it->second->markForGarbageCollection();
        it = map.erase(it);
      } else {
        ++it;
      }
    }
  }

  void InstanceManager::erasePersistentMapEntries(RtInstance* dying) {
    auto eraseFromMap = [dying](std::unordered_map<RtInstance*, RtInstance*>& map) {
      // Fast O(1) check: is the dying instance a key (reference) in the map?
      auto it = map.find(dying);
      if (it != map.end()) {
        // The reference is being GC'd — mark the derived instance for GC
        // so it doesn't survive with a dangling m_linkedBlas pointer.
        // (m_isCreatedByRenderer prevents timeout-based GC, so we must mark explicitly.)
        it->second->markForGarbageCollection();
        map.erase(it);
        return;
      }
      // Slower O(n) check: is the dying instance a value (derived) in the map?
      for (it = map.begin(); it != map.end(); ++it) {
        if (it->second == dying) {
          map.erase(it);
          return;
        }
      }
    };
    eraseFromMap(m_persistentViewModelInstances);
    eraseFromMap(m_persistentVirtualViewModelInstances);
    eraseFromMap(m_persistentPlayerModelClones);
  }

#ifndef NDEBUG
  void InstanceManager::releaseDestroyedInstanceQuarantine() {
    while (!m_destroyedInstanceQuarantine.empty()) {
      ::operator delete(m_destroyedInstanceQuarantine.front());
      m_destroyedInstanceQuarantine.pop_front();
    }
  }
#endif

  void InstanceManager::destroyInstanceAllocation(RtInstance* instance) {
#ifndef NDEBUG
    instance->~RtInstance();
    std::memset(instance, kDestroyedRtInstanceMemoryPattern, sizeof(RtInstance));
    m_destroyedInstanceQuarantine.push_back(instance);
#else
    delete instance;
#endif
  }

  void InstanceManager::garbageCollection() {
    // All instance lifetimes are managed externally: tracked instances are marked
    // for GC by ReplacementInstance::clear(), ephemeral copies are marked on creation.
    for (uint32_t i = 0; i < m_instances.size();) {
      RtInstance*& pInstance = m_instances[i];
      assert(pInstance != nullptr);

      if (pInstance->m_isMarkedForGC) {
        notifySceneChanged();
        removeInstance(pInstance);

        // If this instance is tracked in a persistent map (as key or value),
        // remove the entry so we don't leave a dangling pointer.
        erasePersistentMapEntries(pInstance);

        // NOTE: pInstance is now the (previously) last element
        std::swap(pInstance, m_instances.back());
        m_instances[i]->m_instanceVectorId = i;
        destroyInstanceAllocation(m_instances.back());
        m_instances.pop_back();
        continue;
      }
      ++i;
    }
  }

  void InstanceManager::onFrameEnd() {
    m_viewModelCandidates.clear();
    m_playerModelInstances.clear();
    resetSurfaceIndices();
    m_billboards.clear();
    // reset decal counter
    m_decalSortOrderCounter = 0;
  }

  RtInstance* InstanceManager::processSceneObject(
    const CameraManager& cameraManager, const RayPortalManager& rayPortalManager,
    BlasEntry& blas, const DrawCallState& drawCall, MaterialData& materialData, RtInstance* existingInstance) {

    // If no existing instance is provided, this is a genuinely new draw call and we need to create a fresh instance.
    RtInstance* currentInstance = existingInstance;

    if (currentInstance == nullptr) {
      currentInstance = addInstance(blas);
    } else if (currentInstance->getBlas() != &blas) {
      // The BlasEntry changed — re-link the instance to the current one.
      BlasEntry* oldBlas = currentInstance->getBlas();
      if (oldBlas != nullptr) {
        oldBlas->unlinkInstance(currentInstance);
      }
      currentInstance->setBlas(blas);
      blas.linkInstance(currentInstance);
      currentInstance->m_blasDirty = true;
      notifySceneChanged();
    }

    updateInstance(*currentInstance, cameraManager, blas, drawCall, materialData);
   
    return currentInstance;
  }

  RtSurface::AlphaState InstanceManager::calculateAlphaState(const DrawCallState& drawCall, const MaterialData& materialData) {
    RtSurface::AlphaState out{};

    // Handle Alpha State for non-Opaque materials

    if (materialData.getType() == MaterialDataType::Translucent) {
      // Note: Explicitly ensure translucent materials are not considered fully opaque (even though this is the
      // default in the alpha state).
      out.isFullyOpaque = false;

      return out;
    } else if (materialData.getType() != MaterialDataType::Opaque) {
      return out;
    }

    // Determine if the Legacy Alpha State should be used based on the material data
    // Note: The Material Data may be either Legacy or Opaque here, both use the Opaque Surface Material.

    const auto& opaqueMaterialData = materialData.getOpaqueMaterialData();

    const bool useLegacyAlphaState = opaqueMaterialData.getUseLegacyAlphaState();

    // Handle Alpha Test State

    // Note: Even if the Alpha Test enable flag is set, we consider it disabled if the actual test type is set to always.
    const bool forceAlphaTest = drawCall.getCategoryFlags().test(InstanceCategories::AlphaBlendToCutout);
    const bool alphaTestEnabled = forceAlphaTest || (AlphaTestType)drawCall.getMaterialData().alphaTestCompareOp != AlphaTestType::kAlways;

    // Note: Use the Opaque Material Data's alpha test state information directly if requested,
    // otherwise derive the alpha test state from the drawcall (via its legacy material data).
    if (forceAlphaTest) {
      out.alphaTestType = AlphaTestType::kGreater;
      out.alphaTestReferenceValue = static_cast<uint8_t>(RtxOptions::forceCutoutAlpha() * 255.0);
    } else if (!useLegacyAlphaState) {
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
      blendEnabled = opaqueMaterialData.getBlendEnabled();
      blendType = opaqueMaterialData.getBlendType();
      invertedBlend = opaqueMaterialData.getInvertedBlend();
    } else if (drawCall.getMaterialData().blendMode.enableBlending) {
      const auto srcColorBlendFactor = drawCall.getMaterialData().blendMode.colorSrcFactor;
      const auto dstColorBlendFactor = drawCall.getMaterialData().blendMode.colorDstFactor;
      const auto colorBlendOp = drawCall.getMaterialData().blendMode.colorBlendOp;

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
          // Premultiplied Alpha vs Inverted Reverse Emissive Alpha Blending
          const auto srcAlphaBlendFactor = drawCall.getMaterialData().blendMode.alphaSrcFactor;
          const auto dstAlphaBlendFactor = drawCall.getMaterialData().blendMode.alphaDstFactor;
          const auto alphaBlendOp = drawCall.getMaterialData().blendMode.alphaBlendOp;
          const auto colorWriteMask = drawCall.getMaterialData().blendMode.writeMask;
          const bool alphaWritesDisabled = (colorWriteMask & VK_COLOR_COMPONENT_A_BIT) == 0;

          const bool looksPremultiplied =
            (alphaBlendOp == VkBlendOp::VK_BLEND_OP_ADD &&
             srcAlphaBlendFactor == VkBlendFactor::VK_BLEND_FACTOR_ONE &&
             dstAlphaBlendFactor == VkBlendFactor::VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA) ||
            alphaWritesDisabled;

          if (looksPremultiplied) {
            // Premultiplied Alpha (ONE, ONE_MINUS_SRC_ALPHA)
            blendType = RtxOptions::enableEmissiveBlendModeTranslation() ? BlendType::kAlphaEmissive : BlendType::kAlpha;
            invertedBlend = false;
          } else {
            // Inverted Reverse Emissive Alpha Blending
            blendType = RtxOptions::enableEmissiveBlendModeTranslation() ? BlendType::kReverseAlphaEmissive : BlendType::kReverseAlpha;
            invertedBlend = true;
          }
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
          // Emissive Color Blending: Src + Dst*(1-SrcColor) — bright source is emissive, dark is transparent
          blendType = RtxOptions::enableEmissiveBlendModeTranslation() ? BlendType::kColorEmissive : BlendType::kColor;
          invertedBlend = false;
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

  void InstanceManager::mergeInstanceHeuristics(RtInstance& instanceToModify, const DrawCallState& drawCall, const RtSurface::AlphaState& alphaState) const {
    // "Opaqueness" takes priority!
    if (
      (alphaState.isFullyOpaque || alphaState.alphaTestType == AlphaTestType::kAlways) &&
      !(instanceToModify.surface.alphaState.isFullyOpaque || instanceToModify.surface.alphaState.alphaTestType == AlphaTestType::kAlways)
    ) {
      instanceToModify.surface.alphaState = alphaState;
    }

    // NOTE: In the future we could extend this with heuristics as needed...
  }

  RtInstance* InstanceManager::addInstance(BlasEntry& blas) {
    const uint32_t currentFrameIdx = m_device->getCurrentFrameId();

    notifySceneChanged();

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

    return currentInstance;
  }

  // Creates a copy of an instance
  // If the copy is temporary and is not tracked via callbacks/externally, it doesn't need
  // a valid unique instance ID. In that case, set generateValidID to false to avoid overflowing the ID value
  RtInstance* InstanceManager::createInstanceCopy(const RtInstance& reference, bool generateValidID) {

    const uint32_t instanceIdx = m_instances.size();

    uint64_t id = generateValidID ? m_nextInstanceId++ : kInvalidInstanceId;
    RtInstance* newInstance = new RtInstance(reference, id, instanceIdx);
    newInstance->m_isCreatedByRenderer = true;
    m_instances.push_back(newInstance);
    notifySceneChanged();

    return newInstance;
  }

  void InstanceManager::processInstanceBuffers(const BlasEntry& blas, RtInstance& currentInstance) const {
    currentInstance.surface.positionBufferIndex = blas.modifiedGeometryData.positionBufferIndex;
    currentInstance.surface.positionOffset = blas.modifiedGeometryData.positionBuffer.offsetFromSlice();
    currentInstance.surface.positionStride = blas.modifiedGeometryData.positionBuffer.stride();
    currentInstance.surface.normalBufferIndex = blas.modifiedGeometryData.normalBufferIndex;
    currentInstance.surface.normalOffset = blas.modifiedGeometryData.normalBuffer.offsetFromSlice();
    currentInstance.surface.normalStride = blas.modifiedGeometryData.normalBuffer.stride();
    currentInstance.surface.normalFormat = blas.modifiedGeometryData.normalBuffer.vertexFormat();
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
    if (!RtxOptions::enableInstanceDebuggingTools()) {
      return false;
    }

    if ((
      currentInstance.m_instanceVectorId >= RtxOptions::instanceOverrideInstanceIdx() &&
      currentInstance.m_instanceVectorId < RtxOptions::instanceOverrideInstanceIdx() + RtxOptions::instanceOverrideInstanceIdxRange())) {

      if (RtxOptions::instanceOverrideSelectedInstancePrintMaterialHash())
        Logger::info(str::format("Draw Call Material Hash: ", drawCall.getMaterialData().getHash()));

      // Apply world offset
      Vector3 worldOffset = RtxOptions::instanceOverrideWorldOffset();
      currentInstance.teleportWithHistory(translationMatrix(worldOffset));
      notifySceneChanged();

      return true;
    }

    return false;
  }

  void InstanceManager::bindMaterial(RtInstance& instance, const RtSurfaceMaterial& material) {
    if (material.getType() == RtSurfaceMaterialType::Opaque) {
      instance.m_albedoOpacityTextureIndex = material.getOpaqueSurfaceMaterial().getAlbedoOpacityTextureIndex();
      instance.m_samplerIndex = material.getOpaqueSurfaceMaterial().getSamplerIndex();
    } else if (material.getType() == RtSurfaceMaterialType::RayPortal) {
      instance.m_albedoOpacityTextureIndex = material.getRayPortalSurfaceMaterial().getMaskTextureIndex();
      instance.m_samplerIndex = material.getRayPortalSurfaceMaterial().getSamplerIndex();
      instance.m_secondaryOpacityTextureIndex = material.getRayPortalSurfaceMaterial().getMaskTextureIndex2();
      instance.m_secondarySamplerIndex = material.getRayPortalSurfaceMaterial().getSamplerIndex2();
    }

    instance.m_vkInstance.instanceCustomIndex = (instance.m_vkInstance.instanceCustomIndex & ~(surfaceMaterialTypeMask << CUSTOM_INDEX_MATERIAL_TYPE_BIT));
    instance.m_vkInstance.instanceCustomIndex |= ((uint32_t)material.getType() << CUSTOM_INDEX_MATERIAL_TYPE_BIT);

    // Fetch the material from the cache
    m_pResourceCache->find(material, instance.surface.surfaceMaterialIndex);
  }

  // Updates the state of the instance with the draw call inputs
  // It handles multiple draw calls called for a same instance within a frame
  // To be called on every draw call
  void InstanceManager::updateInstance(RtInstance& currentInstance,
                                       const CameraManager& cameraManager,
                                       const BlasEntry& blas,
                                       const DrawCallState& drawCall,
                                       MaterialData& materialData) {
    const CategoryFlags previousCategoryFlags = currentInstance.m_categoryFlags;
    const uint8_t previousInstanceMask = currentInstance.m_vkInstance.mask;
    const uint32_t previousCustomIndexFlags = currentInstance.m_vkInstance.instanceCustomIndex & ~uint32_t(CUSTOM_INDEX_SURFACE_MASK);
    const uint32_t previousInstanceShaderBindingTableRecordOffset = currentInstance.m_vkInstance.instanceShaderBindingTableRecordOffset;
    const VkGeometryInstanceFlagsKHR previousInstanceFlags = currentInstance.m_vkInstance.flags;
    const bool previousUsesUnorderedApproximations = currentInstance.m_isUnordered;
    const bool previousIsSubsurface = currentInstance.m_isSubsurface;
    const VkGeometryFlagsKHR previousGeometryFlags = currentInstance.m_geometryFlags;
    const auto previousInstancesToObject = currentInstance.surface.instancesToObject;
    const size_t previousInstancesToObjectSize = previousInstancesToObject ? previousInstancesToObject->size() : 0;

    currentInstance.m_categoryFlags = drawCall.getCategoryFlags();
    currentInstance.surface.instancesToObject = drawCall.getTransformData().instancesToObject;

    // setFrameLastUpdated() must be called first as it resets instance's state on a first call in a frame
    const bool isFirstUpdateThisFrame = currentInstance.setFrameLastUpdated(m_device->getCurrentFrameId());

    // Full instance processing always goes through the dynamic draw path this frame.
    currentInstance.surface.isPreservePath = false;

    // These can change in the Runtime UI so need to check during update
    currentInstance.m_isHidden = currentInstance.testCategoryFlags(InstanceCategories::Hidden);
    currentInstance.m_isPlayerModel = currentInstance.testCategoryFlags(InstanceCategories::ThirdPersonPlayerModel);
    currentInstance.m_isWorldSpaceUI = currentInstance.testCategoryFlags(InstanceCategories::WorldUI);

    // Hide the sky instance since it is not raytraced.
    // Sky mesh and material are only good for capture and replacement purposes.
    if (drawCall.cameraType == CameraType::Sky) {
      currentInstance.m_isHidden = true;
    }

    // Snapshot whether this is a brand-new camera before the call to preserveInstance() at the
    // bottom (which always re-registers via RtInstance::registerCamera) so the override logic
    // below sees the "is this the first time we've seen this camera type?" state.
    const bool isNewCameraSet = !currentInstance.isCameraRegistered(drawCall.cameraType);

    const bool overridePreviousCameraUpdate = isNewCameraSet &&
      (drawCall.cameraType == CameraType::Main ||
       // Don't overwrite transform from when the instance was seen with the main camera
       !currentInstance.isCameraRegistered(CameraType::Main));

    const RtSurface::AlphaState alphaState = calculateAlphaState(drawCall, materialData);
    bool hasTransformChanged = false;
    bool hasPreviousPositions = false;

    if (!isFirstUpdateThisFrame) {
      // This is probably the same instance, being drawn twice!  Merge it
      mergeInstanceHeuristics(currentInstance, drawCall, alphaState);
    }
    
    // Updates done only once a frame unless overriden due to an explicit state
    if (isFirstUpdateThisFrame || overridePreviousCameraUpdate) {

      if (isFirstUpdateThisFrame) {
        processInstanceBuffers(blas, currentInstance);

        currentInstance.m_materialType = materialData.getType();

        const XXH64_hash_t materialInstanceHash = materialData.getHash();
        currentInstance.m_materialDataHash = drawCall.getMaterialData().getHash();
        currentInstance.surface.hasMaterialChanged = currentInstance.m_materialHash != kEmptyHash && currentInstance.m_materialHash != materialInstanceHash;
        currentInstance.m_materialHash = materialInstanceHash;

        if (currentInstance.surface.hasMaterialChanged) {
          notifySceneChanged();
        }

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
        currentInstance.surface.associatedGeometryHash = drawCall.getHash(RtxOptions::geometryAssetHashRule());
        currentInstance.surface.isTextureFactorBlend = drawCall.getMaterialData().isTextureFactorBlend;
        currentInstance.surface.isVertexColorBakedLighting = drawCall.getMaterialData().isVertexColorBakedLighting;
        currentInstance.surface.isMotionBlurMaskOut = currentInstance.testCategoryFlags(InstanceCategories::IgnoreMotionBlur);
        currentInstance.surface.ignoreTransparencyLayer = currentInstance.testCategoryFlags(InstanceCategories::IgnoreTransparencyLayer);

        // Note: Skip the spritesheet adjustment logic in the surface interaction when using Ray Portal materials as this logic
        // is done later in the Surface Material Interaction (and doing it in both places will just double up the animation).
        currentInstance.surface.skipSurfaceInteractionSpritesheetAdjustment = (currentInstance.m_materialType == MaterialDataType::RayPortal);

        currentInstance.surface.blendModeState = drawCall.getMaterialData().blendMode;

        if (drawCall.isEye()) {
          // assume that the texture transform has eye parameters encoded
          const Matrix4& texTransform = drawCall.getTransformData().textureTransform;
          RtEyeParams eyeParams{};
          eyeParams.eyeballOrigin = Vector3{ texTransform.data[0].w, texTransform.data[1].w, texTransform.data[2].w };
          eyeParams.eyeRightU = Vector3{ texTransform.data[0].x, texTransform.data[1].x, texTransform.data[2].x };
          eyeParams.eyeUpV = Vector3{ texTransform.data[0].y, texTransform.data[1].y, texTransform.data[2].y };
          currentInstance.surface.eyeParams = eyeParams;
        }

        uint8_t spriteSheetRows = 0, spriteSheetCols = 0, spriteSheetFPS = 0;
        materialData.getSpriteSheetData(currentInstance.surface.spriteSheetRows, currentInstance.surface.spriteSheetCols, currentInstance.surface.spriteSheetFPS);
        currentInstance.m_isAnimated = currentInstance.surface.spriteSheetFPS != 0;
        currentInstance.surface.objectPickingValue = drawCall.drawCallID;

        // Note: Extract spritesheet information from the associated material data as it ends up stored in the Surface
        // not in the Surface Material like most material information.
        switch (materialData.getType()) {
        case MaterialDataType::Opaque:
        {
          spriteSheetRows = materialData.getOpaqueMaterialData().getSpriteSheetRows();
          spriteSheetCols = materialData.getOpaqueMaterialData().getSpriteSheetCols();
          spriteSheetFPS = materialData.getOpaqueMaterialData().getSpriteSheetFPS();

          const bool useLegacyAlphaState = materialData.getOpaqueMaterialData().getUseLegacyAlphaState();

          if (currentInstance.m_isWorldSpaceUI) {
            // For worldspace UI, we want to show the UI (unlit) in the world.  So configure the blend mode if blending is used accordingly.
            materialData.getOpaqueMaterialData().setEnableEmission(true);
            materialData.getOpaqueMaterialData().setEmissiveIntensity(2.0f);
            materialData.getOpaqueMaterialData().setEmissiveColorTexture(materialData.getOpaqueMaterialData().getAlbedoOpacityTexture());
          } else if (currentInstance.surface.alphaState.emissiveBlend && RtxOptions::enableEmissiveBlendEmissiveOverride() && useLegacyAlphaState) {
            // If the user has decided to override the legacy alpha state, assume they know what they are doing and allow for explicit emission controls.
            materialData.getOpaqueMaterialData().setEnableEmission(true);
            materialData.getOpaqueMaterialData().setEmissiveIntensity(RtxOptions::emissiveBlendOverrideEmissiveIntensity());
            materialData.getOpaqueMaterialData().setEmissiveColorTexture(materialData.getOpaqueMaterialData().getAlbedoOpacityTexture());
          }

          currentInstance.m_isSubsurface = materialData.getOpaqueMaterialData().getSubsurfaceDiffusionProfile();

          break;
        }
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
        case MaterialDataType::Count:
        case MaterialDataType::Invalid:
          assert(0);
          break;
        }
      }

      // Update transform
      {
        // Heuristic for MS5 - motion vectors on translucent surfaces cannot be trusted.  This will help with IQ, but need a longer term solution [TREX-634]
        const bool isMotionUnstable = currentInstance.m_materialType == MaterialDataType::Translucent
                                   || currentInstance.testCategoryFlags(InstanceCategories::Particle)
                                   || currentInstance.testCategoryFlags(InstanceCategories::WorldUI);

        hasPreviousPositions = blas.modifiedGeometryData.previousPositionBuffer.defined() && !isMotionUnstable;
        const bool isFirstUpdateAfterCreation = currentInstance.isCreatedThisFrame(m_device->getCurrentFrameId()) && isFirstUpdateThisFrame;

        // Note: objectToView is aliased on updates, since findSimilarInstance() doesn't discern it
        Matrix4 objectToWorld = drawCall.getTransformData().objectToWorld;

        // Hack for TREX-2272. In Portal, in the GLaDOS chamber, the monitors show a countdown timer with background, and the digits and background are coplanar.
        // We cannot reliably determine the digits material because it's a dynamic texture rendered by vgui that contains all kinds of UI things.
        // So instead of offsetting the digits or making them live in unordered TLAS (either of which would solve the problem), we offset the screen background backwards.
        const float worldSpaceUiBackgroundOffset = RtxOptions::worldSpaceUiBackgroundOffset();
        if (worldSpaceUiBackgroundOffset != 0.f && currentInstance.testCategoryFlags(InstanceCategories::WorldMatte)) {
          objectToWorld[3] += objectToWorld[2] * worldSpaceUiBackgroundOffset;
        }

        // Update the transform based on what state we're in
        if (isFirstUpdateAfterCreation) {
          hasTransformChanged = currentInstance.teleport(objectToWorld);
        } else if (isFirstUpdateThisFrame) {
          hasTransformChanged = currentInstance.move(objectToWorld);
        } else {
          hasTransformChanged = currentInstance.moveAgain(objectToWorld);
        }

        if (hasTransformChanged) {
          notifySceneChanged();
          currentInstance.m_blasDirty = true;
        }

        currentInstance.surface.textureTransform = drawCall.getTransformData().textureTransform;

        currentInstance.surface.isStatic = !(hasTransformChanged || hasPreviousPositions) || currentInstance.m_materialType == MaterialDataType::RayPortal;

        currentInstance.surface.isClipPlaneEnabled = drawCall.getTransformData().enableClipPlane;
        currentInstance.surface.clipPlane = drawCall.getTransformData().clipPlane;

        // Apply developer options
        if (isFirstUpdateThisFrame)
          applyDeveloperOptions(currentInstance, drawCall);
      }
    }

    // We only have 1 hit shader.
    currentInstance.m_vkInstance.instanceShaderBindingTableRecordOffset = 0;

    // Update instance flags.
    // Note: this should happen on instance updates and not creation because the same geometry can be drawn
    // with different flags, and the instance manager can match an old instance of a geometry to a new one with different draw mode.
    currentInstance.m_vkInstance.flags = determineInstanceFlags(drawCall, currentInstance.surface);
    currentInstance.isFrontFaceFlipped = (currentInstance.m_vkInstance.flags & VK_GEOMETRY_INSTANCE_TRIANGLE_FLIP_FACING_BIT_KHR) != 0;

    // Apply the decal sort index for this instance so we can approximate order correctness on the GPU in AHS
    if (currentInstance.surface.alphaState.isDecal) {
      currentInstance.surface.decalSortOrder = m_decalSortOrderCounter++;
#if !NDEBUG
      if (m_decalSortOrderCounter > 255) {
        ONCE(Logger::err("Too many decals in this scene to sort correctly, may see some decal corruption issues."));
      }
#endif
    }

    // Update the geometry and instance flags
    if (currentInstance.isOpaque() && drawCall.isUsingRaytracedRenderTarget) {
      // render target texture - need this to be in the opaque pass, even if alphaState.isFullyOpaque is false.
      currentInstance.m_geometryFlags = VK_GEOMETRY_NO_DUPLICATE_ANY_HIT_INVOCATION_BIT_KHR;
    } else if (
      (!currentInstance.surface.alphaState.isFullyOpaque && currentInstance.surface.alphaState.isParticle) ||
      (currentInstance.surface.alphaState.isDecal) ||
      // Note: include alpha blended geometry on the player model into the unordered TLAS. This is hacky as there might be
      // suitable geometry outside of the player model, but we don't have a way to distinguish it from alpha blended geometry
      // that should be alpha tested instead, like some metallic stairs in Portal -- those should be resolved normally.
      (!currentInstance.surface.alphaState.isFullyOpaque && !currentInstance.surface.alphaState.isBlendingDisabled && currentInstance.m_isPlayerModel) ||
      currentInstance.surface.alphaState.emissiveBlend
    ) {
      // Alpha-blended and emissive particles go to the separate "unordered" TLAS as non-opaque geometry
      currentInstance.m_geometryFlags = VK_GEOMETRY_NO_DUPLICATE_ANY_HIT_INVOCATION_BIT_KHR;
      currentInstance.m_isUnordered = true;
      // Unordered resolve only accumulates via any-hits and ignores opaque hits, therefore force 
      // the opaque hits resolve via OMMs to be turned into any-hits.
      // Note: this has unexpected effect even with OMM off and results in minor visual changes in Portal MF A DLSS test
      currentInstance.m_vkInstance.flags |= VK_GEOMETRY_INSTANCE_FORCE_NO_OPAQUE_BIT_KHR;
    } else if (currentInstance.isOpaque() && !currentInstance.surface.alphaState.isFullyOpaque && currentInstance.surface.alphaState.isBlendingDisabled) {
      // Alpha-tested geometry goes to the primary TLAS as non-opaque geometry with potential duplicate hits.
      currentInstance.m_geometryFlags = 0;
    } else if (currentInstance.isOpaque() && !currentInstance.surface.alphaState.isFullyOpaque) {
      // Alpha-blended geometry goes to the primary TLAS as non-opaque geometry with no duplicate hits.
      currentInstance.m_geometryFlags = VK_GEOMETRY_NO_DUPLICATE_ANY_HIT_INVOCATION_BIT_KHR;
      // Treat all non-transparent hits as any-hits
      currentInstance.m_vkInstance.flags |= VK_GEOMETRY_INSTANCE_FORCE_NO_OPAQUE_BIT_KHR;
    } else if (currentInstance.m_materialType == MaterialDataType::Translucent) {
      // Translucent (e.g. glass) geometry goes to the primary TLAS as non-opaque geometry with no duplicate hits.
      currentInstance.m_geometryFlags = VK_GEOMETRY_NO_DUPLICATE_ANY_HIT_INVOCATION_BIT_KHR;
    } else if (currentInstance.m_materialType == MaterialDataType::RayPortal) {
      // Portals go to the primary TLAS as opaque.
      currentInstance.m_geometryFlags = VK_GEOMETRY_OPAQUE_BIT_KHR;
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
    if (currentInstance.m_materialType == MaterialDataType::RayPortal) {
      currentInstance.m_vkInstance.flags &= ~VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
    }

    // Update mask
    {
      uint mask = isFirstUpdateThisFrame ? 0 : currentInstance.m_vkInstance.mask;

      if (currentInstance.m_isPlayerModel && drawCall.cameraType != CameraType::ViewModel) {
        mask |= OBJECT_MASK_PLAYER_MODEL;
        // m_playerModelInstances re-registration is handled by preserveInstance() below.
      } else {
        currentInstance.m_isPlayerModel = false;
        if (currentInstance.m_isUnordered && RtxOptions::enableSeparateUnorderedApproximations()) {
          if (currentInstance.surface.alphaState.isDecal) {
            mask = OBJECT_MASK_UNORDERED_ALL_BLENDED;
          } else {
            // Separate set of mask bits for the unordered TLAS
            if (currentInstance.surface.alphaState.emissiveBlend)
              mask |= OBJECT_MASK_UNORDERED_ALL_EMISSIVE;
            else
              mask |= OBJECT_MASK_UNORDERED_ALL_BLENDED;
          }
        }
        else {
          if (currentInstance.m_materialType == MaterialDataType::Translucent) {
            // Translucent material
            mask |= OBJECT_MASK_TRANSLUCENT;
          } else if (currentInstance.m_materialType == MaterialDataType::RayPortal) {
            // Portal
            mask |= OBJECT_MASK_PORTAL;
          } else {
            mask |= currentInstance.surface.alphaState.isBlendingDisabled ? OBJECT_MASK_OPAQUE : OBJECT_MASK_ALPHA_BLEND;
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
    currentInstance.m_isObjectToWorldMirrored = isMirrorTransform(drawCall.getTransformData().objectToWorld);

    refreshBillboardsForCurrentFrame(currentInstance,
                                     drawCall.cameraType,
                                     cameraManager.getMainCamera().getDirection(false));
    const bool billboardsGotGenerated = currentInstance.m_billboardCount != 0;

    const auto currentInstancesToObject = currentInstance.surface.instancesToObject;
    const size_t currentInstancesToObjectSize = currentInstancesToObject ? currentInstancesToObject->size() : 0;
    const uint32_t currentCustomIndexFlags = currentInstance.m_vkInstance.instanceCustomIndex & ~uint32_t(CUSTOM_INDEX_SURFACE_MASK);
    const bool accelerationStructureKeyChanged =
      previousCategoryFlags.raw() != currentInstance.m_categoryFlags.raw() ||
      previousInstanceMask != currentInstance.m_vkInstance.mask ||
      previousCustomIndexFlags != currentCustomIndexFlags ||
      previousInstanceShaderBindingTableRecordOffset != currentInstance.m_vkInstance.instanceShaderBindingTableRecordOffset ||
      previousInstanceFlags != currentInstance.m_vkInstance.flags ||
      previousUsesUnorderedApproximations != currentInstance.m_isUnordered ||
      previousIsSubsurface != currentInstance.m_isSubsurface ||
      previousGeometryFlags != currentInstance.m_geometryFlags ||
      previousInstancesToObject.get() != currentInstancesToObject.get() ||
      previousInstancesToObjectSize != currentInstancesToObjectSize;

    if (accelerationStructureKeyChanged) {
      notifySceneChanged();
      currentInstance.m_blasDirty = true;
    }

    // Updates done only once a frame unless overriden due to an explicit state
    const bool fireEvents = isFirstUpdateThisFrame || overridePreviousCameraUpdate ||
                            (billboardsGotGenerated && RtxOptions::getEnableOpacityMicromap());

    // Hand off to the shared per-frame finalization (registers view-model / player-model
    // candidates and invokes onInstanceUpdated listeners). The preserve path calls this
    // directly with hasTransformChanged / hasPreviousPositions == false and a null materialData.
    preserveInstance(currentInstance, drawCall, &materialData,
                     hasTransformChanged, hasPreviousPositions, isFirstUpdateThisFrame, fireEvents);
  }

  void InstanceManager::registerViewModelCandidate(RtInstance& instance) {
    // Lazy-clear stale candidates if onFrameEnd() was skipped last frame (e.g. device loss on alt+tab).
    const uint32_t currentFrameId = m_device->getCurrentFrameId();
    if (m_viewModelCandidatesFrameId != currentFrameId) {
      m_viewModelCandidates.clear();
      m_viewModelCandidatesFrameId = currentFrameId;
    }
    m_viewModelCandidates.push_back(&instance);
  }

  void InstanceManager::registerPlayerModelInstance(RtInstance& instance) {
    // Lazy-clear stale instances if onFrameEnd() was skipped last frame (e.g. device loss on alt+tab).
    const uint32_t currentFrameId = m_device->getCurrentFrameId();
    if (m_playerModelInstancesFrameId != currentFrameId) {
      m_playerModelInstances.clear();
      m_playerModelInstancesFrameId = currentFrameId;
    }
    m_playerModelInstances.push_back(&instance);
  }

  void InstanceManager::preserveInstance(
      RtInstance& instance,
      const DrawCallState& drawCall,
      const MaterialData* materialData,
      bool hasTransformChanged,
      bool hasPreviousPositions,
      bool isFirstUpdateThisFrame,
      bool fireEvents) {
    // Camera registration. Idempotent (RtInstance::m_seenCameraTypes is cumulative and never
    // cleared), so calling it from updateInstance and again here is harmless. We need it on
    // the preserve path because that path bypasses updateInstance entirely.
    instance.registerCamera(drawCall.cameraType, m_device->getCurrentFrameId());

    // Re-register view-model candidates every frame; m_viewModelCandidates is cleared in
    // onFrameEnd, and createViewModelInstances() iterates the list later in the frame.
    if (drawCall.cameraType == CameraType::ViewModel && !instance.isHidden() && isFirstUpdateThisFrame) {
      registerViewModelCandidate(instance);
    }

    // Re-register player-model instances every frame. m_playerModelInstances is cleared
    // in onFrameEnd(), and filterPlayerModelInstances() / createPlayerModelVirtualInstances()
    // (run from SceneManager later in the frame) iterate this list to mask the geometric
    // instance and produce billboard intersection primitives + portal-space virtual clones.
    // Without this re-registration, the preserve path's player-model particles would lose
    // their billboard mask and never bind their cached OMM.
    if (instance.m_isPlayerModel && drawCall.cameraType != CameraType::ViewModel) {
      registerPlayerModelInstance(instance);
    }

    // Fire onInstanceUpdated so listeners with delayed per-instance work get a chance to run.
    // The OMM manager's needsToCalculateNumTexelsPerMicroTriangle path is only driven from
    // this callback, so without this dispatch instances that settle into the preserve path
    // before their OMM finishes baking would stall the OMM pipeline indefinitely.
    // hasTransformChanged / hasPreviousPositions default to false because on the preserve path
    // the RtInstance's transform and vertex buffers are reused as-is from the last dynamic update.
    if (fireEvents) {
      for (auto& event : m_eventHandlers) {
        event.onInstanceUpdatedCallback(instance, drawCall, materialData,
                                        hasTransformChanged, hasPreviousPositions, isFirstUpdateThisFrame);
      }
    }
  }

  void InstanceManager::removeInstance(RtInstance* instance) {
    // Always clean up replacement instance references, even for renderer-created instances
    // to avoid use-after-free bugs in ReplacementInstance.prims
    instance->getPrimInstanceOwner().setReplacementInstance(nullptr, ReplacementInstance::kInvalidReplacementIndex, instance, PrimInstance::Type::Instance);
    
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

    const uint32_t frameId = m_device->getCurrentFrameId();

    // Try to reuse a persistent view model instance for this reference.
    RtInstance* viewModelInstance = nullptr;
    auto it = m_persistentViewModelInstances.find(const_cast<RtInstance*>(&reference));
    if (it != m_persistentViewModelInstances.end()) {
      // Existing persistent instance — sync surface/material data from the
      // reference while preserving the corrected transform for change detection.
      viewModelInstance = it->second;
      viewModelInstance->updateFromReference(reference);
      notifySceneChanged();
    } else {
      // First time seeing this reference — create a new persistent instance.
      const bool needValidGlobalInstanceId = false;
      viewModelInstance = createInstanceCopy(reference, needValidGlobalInstanceId);
      viewModelInstance->setFrameCreated(frameId);
      m_persistentViewModelInstances[const_cast<RtInstance*>(&reference)] = viewModelInstance;
    }

    // Keep the instance alive (prevent GC) and mark it as current.
    viewModelInstance->m_isMarkedForGC = false;
    viewModelInstance->setFrameLastUpdated(frameId);
    viewModelInstance->m_vkInstance.mask = OBJECT_MASK_VIEWMODEL;
    viewModelInstance->setCustomIndexBit(CUSTOM_INDEX_IS_VIEW_MODEL, true);

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
        viewModelInstance->teleport(corrected, prevCorrected);
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
    viewModelInstance->surface.isPreservePath = false;

    return viewModelInstance;
  }

  void InstanceManager::createViewModelInstances(Rc<DxvkContext> ctx,
                                                 const CameraManager& cameraManager,
                                                 const RayPortalManager& rayPortalManager) {
    ScopedGpuProfileZone(ctx, "ViewModel");

    auto cleanupAllPersistentViewModelInstances = [this]() {
      for (auto& [ref, inst] : m_persistentViewModelInstances) {
        inst->markForGarbageCollection();
      }
      m_persistentViewModelInstances.clear();
    };

    if (!RtxOptions::ViewModel::enable()) {
      cleanupAllPersistentViewModelInstances();
      return;
    }

    if (!cameraManager.isCameraValid(CameraType::ViewModel)) {
      cleanupAllPersistentViewModelInstances();
      return;
    }

    // If the first person player model is enabled, hide the view model.
    if (RtxOptions::PlayerModel::enableInPrimarySpace()) {
      for (auto* candidateInstance : m_viewModelCandidates) {
        candidateInstance->m_vkInstance.mask = 0;
      }
      cleanupAllPersistentViewModelInstances();
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
    std::unordered_set<RtInstance*> activeViewModelReferences;
    for (auto* candidateInstance : m_viewModelCandidates) {

      // Valid view model instances must be associated only with the view model camera
      // Check: exactly one bit set (power-of-two check via raw bitmask)
      const auto seenMask = candidateInstance->m_seenCameraTypes.raw();
      if (seenMask == 0 || (seenMask & (seenMask - 1)) != 0)
        continue;

      // Hide the reference instance since we'll create a separate instance for the view model 
      candidateInstance->m_vkInstance.mask = 0;

      // Tag the instance as ViewModel so it can be checked for it being a reference view model instance
      candidateInstance->setCustomIndexBit(CUSTOM_INDEX_IS_VIEW_MODEL, true);

      activeViewModelReferences.insert(candidateInstance);
      viewModelInstances.push_back(createViewModelInstance(ctx, *candidateInstance, perspectiveCorrection, prevPerspectiveCorrection));
    }

    // Mark persistent view model instances whose references have disappeared for GC.
    cleanupPersistentMap(m_persistentViewModelInstances, activeViewModelReferences);

    // Create virtual instances for the view model instances
    createRayPortalVirtualViewModelInstances(viewModelInstances, cameraManager, rayPortalManager);
  }

  static bool isInsidePlayerModel(const Vector3& playerModelPosition, const Vector3& instancePosition) {
    const Vector3 playerToInstance = instancePosition - playerModelPosition;
    const float horizontalDistance = length(Vector2(playerToInstance.x, playerToInstance.y));
    const float verticalDistance = fabs(playerToInstance.z);

    // Distance thresholds determined experimentally to match the portal gun held in player's hands
    // but not match the gun on the pedestals.
    const float maxHorizontalDistance = RtxOptions::PlayerModel::horizontalDetectionDistance();
    const float maxVerticalDistance = RtxOptions::PlayerModel::verticalDetectionDistance();

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
      playerModelEyePosition.z += RtxOptions::PlayerModel::eyeHeight();

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
        const float maximumNormalDistance = lerp(RtxOptions::PlayerModel::intersectionCapsuleRadius(),
                                                 RtxOptions::PlayerModel::intersectionCapsuleHeight(),
                                                 clamp(rayPortal.entryPortalInfo.planeNormal.z, 0.f, 1.f));

        // Test if that shape intersects with the portal and if the camera is in front of it
        const float planeDistanceNormal = -dot(dirToPortalCentroid, rayPortal.entryPortalInfo.planeNormal);
        const float planeDistanceX = dot(dirToPortalCentroid, rayPortal.entryPortalInfo.planeBasis[0]);
        const float planeDistanceY = dot(dirToPortalCentroid, rayPortal.entryPortalInfo.planeBasis[1]);
        const bool cameraVolumeIntersectsPortal = 0.f < planeDistanceNormal && planeDistanceNormal < maximumNormalDistance
          && std::abs(planeDistanceX) < rayPortal.entryPortalInfo.planeHalfExtents.x
          && std::abs(planeDistanceY) < rayPortal.entryPortalInfo.planeHalfExtents.y;

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
    auto cleanupAllPersistentPlayerModelClones = [this]() {
      for (auto& [ref, inst] : m_persistentPlayerModelClones) {
        inst->markForGarbageCollection();
      }
      m_persistentPlayerModelClones.clear();
    };

    if (m_playerModelInstances.empty()) {
      cleanupAllPersistentPlayerModelClones();
      return;
    }

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

    if (!bodyInstance) {
      cleanupAllPersistentPlayerModelClones();
      return;
    }

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
    float backwardOffset = RtxOptions::PlayerModel::backwardOffset();

    const bool createVirtualInstances = RtxOptions::PlayerModel::enableVirtualInstances() && (nearPortalInfo != nullptr);

    // The loop below creates virtual instances and applies the offset. Exit if neither is necessary.
    if (!createVirtualInstances && backwardOffset == 0.f) {
      cleanupAllPersistentPlayerModelClones();
      return;
    }

    // Calculate the offset vector
    Vector3 backwardOffsetVector = cameraManager.getMainCamera().getHorizontalForwardDirection();
    backwardOffsetVector *= -backwardOffset;

    if (playerModelIsVirtual && farPortalInfo) {
      // Transform the offset vector into portal space
      backwardOffsetVector = (farPortalInfo->portalToOpposingPortalDirection * Vector4(backwardOffsetVector, 0.f)).xyz();
    }

    const Matrix4 backwardOffsetMatrix {
      Vector4{ 1.f, 0.f, 0.f, 0.f },
      Vector4{ 0.f, 1.f, 0.f, 0.f },
      Vector4{ 0.f, 0.f, 1.f, 0.f },
      Vector4(backwardOffsetVector, 1.f)
    };
    
    // Create or update virtual instances for player model instances that are close to portals.
    // Offset both real and virtual instances by backwardOffset units if enabled.
    std::unordered_set<RtInstance*> activePlayerModelReferences;
    for (RtInstance* originalInstance : m_playerModelInstances) {

      if (backwardOffset != 0.f) {
        // Offset the original instance
        originalInstance->teleportWithHistory(backwardOffsetMatrix);
        notifySceneChanged();

        // Offset the original instance particles
        for (uint32_t i = 0; i < originalInstance->m_billboardCount; ++i) {
          m_billboards[originalInstance->m_firstBillboard + i].center += backwardOffsetVector;
        }
      }

      if (!createVirtualInstances)
        continue;

      activePlayerModelReferences.insert(originalInstance);

      RtInstance* clonedInstance = nullptr;
      auto it = m_persistentPlayerModelClones.find(originalInstance);
      if (it != m_persistentPlayerModelClones.end()) {
        // Reuse existing persistent clone.
        clonedInstance = it->second;
        clonedInstance->updateFromReference(*originalInstance, /* preserveTransforms = */ false);
        notifySceneChanged();
      } else {
        // Create new persistent clone.
        const bool needValidGlobalInstanceId = false;
        clonedInstance = createInstanceCopy(*originalInstance, needValidGlobalInstanceId);
        clonedInstance->setFrameCreated(frameId);
        m_persistentPlayerModelClones[originalInstance] = clonedInstance;
      }

      clonedInstance->m_isMarkedForGC = false;
      clonedInstance->setFrameLastUpdated(frameId);

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
        clonedInstance->teleportWithHistory(nearPortalInfo->portalToOpposingPortalDirection);
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

    // Mark persistent player model clones whose references have disappeared for GC.
    cleanupPersistentMap(m_persistentPlayerModelClones, activePlayerModelReferences);
  }

  void InstanceManager::findPortalForVirtualInstances(const CameraManager& cameraManager, const RayPortalManager& rayPortalManager) {
    m_virtualInstancePortalIndex = -1;

    // Virtual instances for the view model and the player model are generated for the closest portal to the camera.

    static_assert(maxRayPortalCount == 2);
    auto& rayPortalPair = *rayPortalManager.getRayPortalPairInfos().begin();

    if (!rayPortalPair.has_value())
      return;

    const Vector3& camPos = cameraManager.getCamera(CameraType::Main).getPosition(/* freecam = */ false);

    const float kMaxDistanceToPortal = RtxOptions::ViewModel::rangeMeters() * RtxOptions::getMeterToWorldUnitScale();

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
    if (m_virtualInstancePortalIndex < 0) {
      // No portal in range — clean up any leftover persistent virtual view model instances.
      for (auto& [ref, inst] : m_persistentVirtualViewModelInstances) {
        inst->markForGarbageCollection();
      }
      m_persistentVirtualViewModelInstances.clear();
      return;
    }

    if (rayPortalManager.getRayPortalPairInfos().empty()) {
      assert(!"There must be a portal pair in createRayPortalVirtualViewModelInstances if m_virtualInstancePortalIndex is defined");
      return;
    }

    if (!RtxOptions::ViewModel::enableVirtualInstances()) {
      // Feature disabled — clean up persistent instances.
      for (auto& [ref, inst] : m_persistentVirtualViewModelInstances) {
        inst->markForGarbageCollection();
      }
      m_persistentVirtualViewModelInstances.clear();
      return;
    }

    const SingleRayPortalDirectionInfo& closestPortalInfo = rayPortalManager.getRayPortalPairInfos()[0]->pairInfos[m_virtualInstancePortalIndex];
    
    const uint32_t frameId = m_device->getCurrentFrameId();

    // Create or update virtual instances for view model instances that are close to portals
    std::unordered_set<RtInstance*> activeVirtualViewModelReferences;
    for (RtInstance* referenceInstance : viewModelReferenceInstances) {

      activeVirtualViewModelReferences.insert(referenceInstance);

      RtInstance* virtualInstance = nullptr;
      auto it = m_persistentVirtualViewModelInstances.find(referenceInstance);
      if (it != m_persistentVirtualViewModelInstances.end()) {
        // Reuse existing persistent instance.
        virtualInstance = it->second;
        virtualInstance->updateFromReference(*referenceInstance, /* preserveTransforms = */ false);
        notifySceneChanged();
      } else {
        // Create new persistent virtual instance.
        const bool needValidGlobalInstanceId = false;
        virtualInstance = createInstanceCopy(*referenceInstance, needValidGlobalInstanceId);
        virtualInstance->setFrameCreated(frameId);
        m_persistentVirtualViewModelInstances[referenceInstance] = virtualInstance;
      }

      virtualInstance->m_isMarkedForGC = false;
      virtualInstance->setFrameLastUpdated(frameId);

      // Virtual instances are to be visible only in their corresponding portal spaces
      static_assert(maxRayPortalCount == 2);
      virtualInstance->m_vkInstance.mask = OBJECT_MASK_VIEWMODEL_VIRTUAL;
    
      // Update virtual instance transforms given the reference and the portal transform
      {
        virtualInstance->teleportWithHistory(closestPortalInfo.portalToOpposingPortalDirection);
      }
    }

    // Mark persistent virtual view model instances whose references have disappeared for GC.
    cleanupPersistentMap(m_persistentVirtualViewModelInstances, activeVirtualViewModelReferences);
  }

  void InstanceManager::resetSurfaceIndices() {
    for (auto instance : m_instances)
      instance->m_surfaceIndex = SURFACE_INDEX_INVALID;
  }

  inline bool isFpSpecial(float x) {
    const uint32_t u = *(uint32_t*) &x;
    return (u & 0x7f800000) == 0x7f800000;
  }

  uint32_t InstanceManager::computeBillboardIntersectionPrimitiveMask(const RtInstance& instance) {
    // Player-model intersection primitives live in OBJECT_MASK_PLAYER_MODEL (and
    // OBJECT_MASK_PLAYER_MODEL_VIRTUAL on portal clones — overwritten later in
    // createPlayerModelVirtualInstances). See instance_definitions.h for the mask layout.
    if (instance.m_isPlayerModel) {
      return OBJECT_MASK_PLAYER_MODEL;
    }
    // Pick the _INTERSECTION_PRIMITIVE half of the BLENDED / EMISSIVE pair that matches
    // the instance's blend mode.
    if (instance.surface.alphaState.isDecal) {
      return OBJECT_MASK_UNORDERED_BLENDED_INTERSECTION_PRIMITIVE;
    }
    if (instance.surface.alphaState.emissiveBlend) {
      return OBJECT_MASK_UNORDERED_EMISSIVE_INTERSECTION_PRIMITIVE;
    }
    return OBJECT_MASK_UNORDERED_BLENDED_INTERSECTION_PRIMITIVE;
  }

  void InstanceManager::refreshBillboardsForCurrentFrame(RtInstance& currentInstance,
                                                         CameraType::Enum cameraType,
                                                         const Vector3& cameraViewDirection) {
    // m_billboards is cleared every frame in onFrameEnd, so reset the per-instance
    // count and let createBeams / createBillboards re-populate m_billboards and
    // re-stamp m_firstBillboard / m_billboardCount for this frame.
    const uint32_t previousBillboardCount = currentInstance.m_billboardCount;
    currentInstance.m_billboardCount = 0;

    // Note: instance.mask is not part of this guard. createBillboards() and createBeams()
    // intentionally clear bits from the mask (for player-model particles the mask ends up
    // at 0), so re-checking mask on the next frame would skip the very instances that
    // still need their billboards re-populated on the preserve path.
    if (!(RtxOptions::enableSeparateUnorderedApproximations() &&
          (cameraType == CameraType::Main || cameraType == CameraType::ViewModel) &&
          currentInstance.m_isUnordered &&
          !currentInstance.m_isHidden)) {
      return;
    }

    if (currentInstance.testCategoryFlags(InstanceCategories::Beam)) {
      createBeams(currentInstance);
    } else if (!currentInstance.surface.alphaState.isDecal) {
      createBillboards(currentInstance, cameraViewDirection);
    }

    if (currentInstance.m_billboardCount != previousBillboardCount) {
      currentInstance.m_blasDirty = true;
      currentInstance.m_billboardGeometryDirty = true;
    }
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
      const bool hasPerpendicularSides = std::abs(dotAxes) < 0.01f;
      // - Must be in the camera view plane, i.e. only auto-oriented particles, not world-space ones
      //   (except player model particles, which are oriented towards the camera and not in the view plane)
      const bool isInViewPlane = std::abs(normalDotCamera) > 0.99f;
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
      billboard.instanceMask = computeBillboardIntersectionPrimitiveMask(instance);
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
        // Triangle catches intersection-primitive rays as the fallback (since the billboards
        // can't this frame). OR in the matching _INTERSECTION_PRIMITIVE bits so the triangle
        // mask ends in the canonical "_GEOMETRY | _INTERSECTION_PRIMITIVE" state.
        instance.getVkInstance().mask |= computeBillboardIntersectionPrimitiveMask(instance);
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
      billboard.instanceMask = computeBillboardIntersectionPrimitiveMask(instance);
      billboard.instance = &instance;
      billboard.texCoordHash = 0;
      billboard.vertexOpacityHash = 0;
      billboard.allowAsIntersectionPrimitive = true;
      billboard.isBeam = true;
      billboard.isCameraFacing = false;
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
    if (RtxOptions::AntiCulling::isObjectAntiCullingEnabled()) {
      const Vector3 pos = getWorldPosition();
      const XXH64_hash_t posHash = XXH3_64bits(&pos, sizeof(pos));
      XXH64_hash_t antiCullingHash = XXH3_64bits_withSeed(&m_materialDataHash, sizeof(XXH64_hash_t), posHash);

      if (RtxOptions::AntiCulling::Object::hashInstanceWithBoundingBoxHash() &&
          RtxOptions::needsMeshBoundingBox()) {
        const AxisAlignedBoundingBox& boundingBox = getBlas()->input.getGeometryData().boundingBox;
        const XXH64_hash_t bboxHash = boundingBox.calculateHash();
        antiCullingHash = XXH3_64bits_withSeed(&bboxHash, sizeof(antiCullingHash), antiCullingHash);
      }
      return antiCullingHash;
    }

    return XXH64_hash_t();
  }
}  // namespace dxvk
