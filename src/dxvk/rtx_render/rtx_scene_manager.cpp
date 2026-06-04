/*
* Copyright (c) 2021-2026, NVIDIA CORPORATION. All rights reserved.
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
#include <limits>
#include <mutex>
#include <vector>

#include "rtx_asset_replacer.h"
#include "rtx_scene_manager.h"
#include "rtx_opacity_micromap_manager.h"
#include "dxvk_device.h"
#include "dxvk_context.h"
#include "dxvk_buffer.h"
#include "rtx_context.h"
#include "rtx_options.h"
#include "rtx_terrain_baker.h"
#include "rtx_texture_manager.h"
#include "rtx_texture.h"
#include "rtx_xess.h"

#include <assert.h>

#include "../d3d9/d3d9_state.h"
#include "vulkan/vulkan_core.h"

#include "rtx_game_capturer.h"
#include "rtx_matrix_helpers.h"
#include "dxvk_scoped_annotation.h"
#include "rtx_lights_data.h"
#include "rtx_light_utils.h"

#include "../util/util_global_time.h"
#include "../util/util_struct_hash.h"

#include "rtx/pass/particles/particle_system_common.h"

namespace {
  // helper function to ensure generating spatialMapHash for external draws is done the same way in multiple places.
  XXH64_hash_t spatialMapHashForExternalDrawMesh(remixapi_MeshHandle mesh) {
    const uintptr_t meshId = reinterpret_cast<uintptr_t>(mesh);
    return XXH3_64bits(&meshId, sizeof(meshId));
  }
} // namespace

namespace dxvk {

  // Compute a hash that can be used to check if an external draw is identical to the previous frame's draw.
  XXH64_hash_t ExternalDrawState::computeExternalDrawIdentityHash() const {
    struct ExternalDrawIdentityHashData {
      uintptr_t meshId;
      XXH64_hash_t materialHash;
      XXH64_hash_t boneHash;
      CameraType::Enum cameraType;
      uint32_t categoriesRaw;
      XXH64_hash_t particleDescHash;
      XXH64_hash_t gpuInstancingHash;
      TexGenMode texgenMode;
      uint8_t usesVertexShader;
      uint8_t usesPixelShader;
      uint8_t zWriteEnable;
      uint8_t zEnable;
      uint8_t skyAutoDetected;
      uint8_t _pad0;
      uint8_t _pad1;
      Matrix4 objectToWorld;
      Matrix4 textureTransform;
    };

    ExternalDrawIdentityHashData data{};

    const DrawCallTransforms& transforms = drawCall.getTransformData();
    data.meshId = reinterpret_cast<uintptr_t>(mesh);
    data.materialHash = drawCall.getMaterialData().getHash();
    data.boneHash = drawCall.getSkinningState().boneHash;
    data.cameraType = cameraType;
    data.categoriesRaw = categories.raw();

    if (optionalParticleDesc.has_value()) {
      data.particleDescHash = optionalParticleDesc->calcHash();
    }

    if (!gpuInstancingTransforms.empty()) {
      data.gpuInstancingHash = XXH3_64bits(
          gpuInstancingTransforms.data(),
          gpuInstancingTransforms.size() * sizeof(Matrix4));
    }

    data.texgenMode = transforms.texgenMode;
    data.usesVertexShader = drawCall.usesVertexShader ? 1u : 0u;
    data.usesPixelShader = drawCall.usesPixelShader ? 1u : 0u;
    data.zWriteEnable = drawCall.zWriteEnable ? 1u : 0u;
    data.zEnable = drawCall.zEnable ? 1u : 0u;
    data.skyAutoDetected = drawCall.skyAutoDetected ? 1u : 0u;
    data.objectToWorld = transforms.objectToWorld;
    data.textureTransform = transforms.textureTransform;

    return hashStructByMemory(data,
        &ExternalDrawIdentityHashData::meshId,
        &ExternalDrawIdentityHashData::materialHash,
        &ExternalDrawIdentityHashData::boneHash,
        &ExternalDrawIdentityHashData::cameraType,
        &ExternalDrawIdentityHashData::categoriesRaw,
        &ExternalDrawIdentityHashData::particleDescHash,
        &ExternalDrawIdentityHashData::gpuInstancingHash,
        &ExternalDrawIdentityHashData::texgenMode,
        &ExternalDrawIdentityHashData::usesVertexShader,
        &ExternalDrawIdentityHashData::usesPixelShader,
        &ExternalDrawIdentityHashData::zWriteEnable,
        &ExternalDrawIdentityHashData::zEnable,
        &ExternalDrawIdentityHashData::skyAutoDetected,
        &ExternalDrawIdentityHashData::_pad0,
        &ExternalDrawIdentityHashData::_pad1,
        &ExternalDrawIdentityHashData::objectToWorld,
        &ExternalDrawIdentityHashData::textureTransform);
  }

  SceneManager::SceneManager(DxvkDevice* device)
    : CommonDeviceObject(device)
    , m_instanceManager(device, this)
    , m_accelManager(device)
    , m_lightManager(device)
    , m_graphManager()
    , m_rayPortalManager(device, this)
    , m_drawCallCache(device)
    , m_drawCallTracker(device)
    , m_bindlessResourceManager(device)
    , m_pReplacer(new AssetReplacer())
    , m_terrainBaker(new TerrainBaker())
    , m_cameraManager(device)
    , m_uniqueObjectSearchDistance(RtxOptions::uniqueObjectDistance()) {
    InstanceEventHandler instanceEvents(this);
    instanceEvents.onInstanceAddedCallback = [this](RtInstance& instance) { onInstanceAdded(instance); };
    instanceEvents.onInstanceUpdatedCallback = [this](RtInstance& instance, const DrawCallState& drawCall, const MaterialData* material, bool hasTransformChanged, bool hasVerticesChanged, bool isFirstUpdateThisFrame) { onInstanceUpdated(instance, drawCall, material, hasTransformChanged, hasVerticesChanged, isFirstUpdateThisFrame); };
    instanceEvents.onInstanceDestroyedCallback = [this](RtInstance& instance) { onInstanceDestroyed(instance); };
    m_instanceManager.addEventHandler(instanceEvents);
    
    if (env::getEnvVar("DXVK_RTX_CAPTURE_ENABLE_ON_FRAME") != "") {
      m_beginUsdExportFrameNum = stoul(env::getEnvVar("DXVK_RTX_CAPTURE_ENABLE_ON_FRAME"));
    }
  }

  SceneManager::~SceneManager() {
  }

  bool SceneManager::areAllReplacementsLoaded() const {
    return m_pReplacer->areAllReplacementsLoaded();
  }

  std::vector<Mod::State> SceneManager::getReplacementStates() const {
    return m_pReplacer->getReplacementStates();
  }

  void SceneManager::initialize(Rc<DxvkContext> ctx) {
    ScopedCpuProfileZone();
    m_pReplacer->initialize(ctx);
  }

  void SceneManager::logStatistics() {
    if (m_opacityMicromapManager.get()) {
      m_opacityMicromapManager->logStatistics();
    }
  }

  Vector3 SceneManager::getSceneUp() {
    return RtxOptions::zUp() ? Vector3(0.f, 0.f, 1.f) : Vector3(0.f, 1.f, 0.f);
  }

  Vector3 SceneManager::getSceneForward() {
    return RtxOptions::zUp() ? Vector3(0.f, 1.f, 0.f) : Vector3(0.f, 0.f, 1.f);
  }

  Vector3 SceneManager::calculateSceneRight() {
    const Vector3 up = SceneManager::getSceneUp();
    const Vector3 forward = SceneManager::getSceneForward();
    return RtxOptions::leftHandedCoordinateSystem() ? cross(up, forward) : cross(forward, up);
  }

  Vector3 SceneManager::worldToSceneOrientedVector(const Vector3& worldVector) {
    return RtxOptions::zUp() ? worldVector : Vector3(worldVector.x, worldVector.z, worldVector.y);
  }

  Vector3 SceneManager::sceneToWorldOrientedVector(const Vector3& sceneVector) {
    // Same transform applies to and from
    return worldToSceneOrientedVector(sceneVector);
  }

  float SceneManager::getTotalMipBias() {
    auto& resourceManager = m_device->getCommon()->getResources();
  
    const bool temporalUpscaling = RtxOptions::isDLSSOrRayReconstructionEnabled() || RtxOptions::isXeSSEnabled() || RtxOptions::isTAAEnabled();
    
    float totalUpscaleMipBias = 0.0f;
    
    if (temporalUpscaling) {
      if (RtxOptions::isXeSSEnabled()) {
        // XeSS uses the new formula from the XeSS developer guide
        totalUpscaleMipBias = -log2(resourceManager.getUpscaleRatio());
        
        // Add XeSS-specific mip bias when XeSS is active
        DxvkXeSS& xess = m_device->getCommon()->metaXeSS();
        if (xess.isActive()) {
          float xessMipBias = xess.calcRecommendedMipBias();
          totalUpscaleMipBias += xessMipBias;
        }
      } else {
        // Restore original behavior for DLSS, TAA, and other upscalers
        totalUpscaleMipBias = log2(resourceManager.getUpscaleRatio()) + RtxOptions::upscalingMipBias();
      }
    }
    
    return totalUpscaleMipBias + RtxOptions::nativeMipBias();
  }

  float SceneManager::getCalculatedUpscalingMipBias() {
    auto& resourceManager = m_device->getCommon()->getResources();
    
    const bool temporalUpscaling = RtxOptions::isXeSSEnabled();
    if (!temporalUpscaling) {
      return 0.0f;
    }
    
    float calculatedUpscalingBias = -log2(resourceManager.getUpscaleRatio());
    return calculatedUpscalingBias;
  }

  void SceneManager::clear(Rc<DxvkContext> ctx, bool needWfi) {
    ScopedCpuProfileZone();

    auto& textureManager = m_device->getCommon()->getTextureManager();

    // Only clear once after the scene disappears, to avoid adding a WFI on every frame through clear().
    if (needWfi) {
      if (ctx.ptr())
        ctx->flushCommandList();
      m_device->waitForIdle();
    }

    // We still need to clear caches even if the scene wasn't rendered
    m_bufferCache.clear();
    m_surfaceMaterialCache.clear();
    m_preCreationSurfaceMaterialMap.clear();
    m_surfaceMaterialExtensionCache.clear();
    m_volumeMaterialCache.clear();
    
    // Clear ReplacementInstances first: their destructors call clear() which
    // accesses prims[] to mark entities for GC and clear back-pointers.
    // Entities must still be alive at this point.
    m_drawCallTracker.clear();

    // Called before instance manager's clear, so that it resets all tracked instances in Opacity Micromap manager at once
    if (m_opacityMicromapManager.get())
      m_opacityMicromapManager->clear();

    // Invalidate AccelManager's bucket cache before InstanceManager::clear() deletes
    // every RtInstance. The cache holds raw RtInstance* in m_cachedBuckets[].instances /
    // .surfaces and m_instanceBucketIndex, and the next frame's mergeInstancesIntoBlas
    // dirty check would dereference those (now-freed) pointers. The per-instance
    // onInstanceDestroyed -> removeInstanceFromBucketCache hook only patches the index
    // map, not the vectors, so a bulk reset must drop the cache wholesale.
    m_accelManager.clear();

    m_instanceManager.clear();
    m_lightManager.clear();
    m_graphManager.clear();
    m_rayPortalManager.clear();
    m_drawCallCache.clear();
    textureManager.clear();
    m_textureCacheGenerationValidForPreserve = textureManager.getTextureCacheGeneration();

    m_previousFrameSceneAvailable = false;
    m_startInMediumMaterialIndex = SURFACE_INDEX_INVALID;
    m_fogStartInMediumMaterialIndex_inCache = kInvalidMaterialCacheIndex;
    m_externalStartInMediumMaterialIndex_inCache = kInvalidMaterialCacheIndex;
    m_startInMediumMaterialIndex_inCache = kInvalidMaterialCacheIndex;
    m_lastResolvedStartInMediumMaterialIndexInCache = kInvalidMaterialCacheIndex;
    m_lastUploadedStartInMediumMaterialIndexInCache = kInvalidMaterialCacheIndex;
  }

  void SceneManager::garbageCollection() {
    ScopedCpuProfileZone();

    // BlasEntry GC: remove entries not touched recently.
    // Only GC entries with no linked instances — instances still reference the BlasEntry
    // for TLAS build, and destroying it would cause a one-frame visibility gap.
    if (m_device->getCurrentFrameId() > RtxOptions::numFramesToKeepGeometryData()) {
      const size_t oldestFrame = m_device->getCurrentFrameId() - RtxOptions::numFramesToKeepGeometryData();
      auto& entries = m_drawCallCache.getEntries();
      for (auto iter = entries.begin(); iter != entries.end(); ) {
        if (iter->second.frameLastTouched < oldestFrame &&
            iter->second.getLinkedInstances().empty()) {
          iter = entries.erase(iter);
        } else {
          ++iter;
        }
      }
    }

    // ReplacementInstance GC: marks owned instances/lights for GC
    // and clears their back-pointers while they are still alive.
    m_drawCallTracker.garbageCollectReplacementInstances(
        getCamera(), m_isAntiCullingSupported);

    // Instance/light GC: removes entities marked for GC by ReplacementInstance::clear()
    // or marked on creation (ephemeral copies). Back-pointers are already null.
    m_instanceManager.garbageCollection();
    m_accelManager.garbageCollection();
    m_lightManager.garbageCollection(getCamera());
    m_rayPortalManager.garbageCollection();
  }

  void SceneManager::onDestroy() {
    m_accelManager.onDestroy();
    if (m_opacityMicromapManager) {
      m_opacityMicromapManager->onDestroy();
    }
  }

  template<bool isNew>
  SceneManager::ObjectCacheState SceneManager::processGeometryInfo(Rc<DxvkContext> ctx, const DrawCallState& drawCallState, RaytraceGeometry& inOutGeometry) {
    ScopedCpuProfileZone();
    ObjectCacheState result = ObjectCacheState::KBuildBVH;
    const RasterGeometry& input = drawCallState.getGeometryData();

    // Determine the optimal object state for this geometry
    if (!isNew) {
      // This is a geometry we've seen before, that requires updating
      //  'inOutGeometry' has valid historical data
      if (input.hashes[HashComponents::Indices] == inOutGeometry.hashes[HashComponents::Indices]) {
        // Check if the vertex positions have changed, requiring a BVH refit
        if (input.hashes[HashComponents::VertexPosition] == inOutGeometry.hashes[HashComponents::VertexPosition]
         && input.hashes[HashComponents::VertexShader] == inOutGeometry.hashes[HashComponents::VertexShader]
         && drawCallState.getSkinningState().boneHash == inOutGeometry.lastBoneHash) {
          result = ObjectCacheState::kUpdateInstance;
        } else {
          result = ObjectCacheState::kUpdateBVH;
        }
      }
    }

    // Copy the input directly to the output as a starting point for our modified geometry data
    RaytraceGeometry output = inOutGeometry;

    output.lastBoneHash = drawCallState.getSkinningState().boneHash;

    // Update draw parameters
    output.cullMode = input.cullMode;
    output.frontFace = input.frontFace;

    // Copy the hashes over
    output.hashes = input.hashes;

    if (!input.positionBuffer.defined()) {
      ONCE(Logger::err("processGeometryInfo: no position data on input detected"));
      return ObjectCacheState::kInvalid;
    }

    if (input.vertexCount == 0) {
      ONCE(Logger::err("processGeometryInfo: input data is violating some assumptions"));
      return ObjectCacheState::kInvalid;
    }

    // Set to 1 if inspection of the GeometryData structures contents on CPU is desired
    #define DEBUG_GEOMETRY_MEMORY 0
    constexpr VkMemoryPropertyFlags memoryProperty = DEBUG_GEOMETRY_MEMORY ? (VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) : VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

    // Assume we won't need this, and update the value if required
    output.previousPositionBuffer = RaytraceBuffer();

    // When the SmoothNormals category is set and the input has no normals, force the interleaved
    // vertex layout to include space for normals. The smooth normals compute pass will fill them in later.
    const bool needsSmoothNormals = drawCallState.categories.test(InstanceCategories::SmoothNormals);
    const bool forceNormals = needsSmoothNormals && !input.normalBuffer.defined();

    // When smooth normals state changes (added or removed), promote to kUpdateBVH so the vertex
    // data is re-interleaved and the smooth normals dispatch runs (or original normals are restored).
    if (needsSmoothNormals != output.smoothNormalsApplied && result == ObjectCacheState::kUpdateInstance) {
      result = ObjectCacheState::kUpdateBVH;
    }
    if (!needsSmoothNormals) {
      output.smoothNormalsApplied = false;
    }

    // If forceNormals is true, we can't use the fast "already interleaved" path since
    // we need to change the layout to include normal space.
    const size_t vertexStride = (input.isVertexDataInterleaved() && input.areFormatsGpuFriendly() && !forceNormals)
      ? input.positionBuffer.stride()
      : RtxGeometryUtils::computeOptimalVertexStride(input, forceNormals);

    switch (result) {
      case ObjectCacheState::KBuildBVH: {
        // Set up the ideal vertex params, if input vertices are interleaved, it's safe to assume the positionBuffer stride is the vertex stride
        output.vertexCount = input.vertexCount;

        const size_t vertexBufferSize = output.vertexCount * vertexStride;

        // Set up the ideal index params
        output.indexCount = input.isTopologyRaytraceReady() ? input.indexCount : RtxGeometryUtils::getOptimalTriangleListSize(input);
        const VkIndexType indexBufferType = input.isTopologyRaytraceReady() ? input.indexBuffer.indexType() : RtxGeometryUtils::getOptimalIndexFormat(output.vertexCount);
        const size_t indexStride = (indexBufferType == VK_INDEX_TYPE_UINT16) ? 2 : 4;

        // Make sure we're not stomping something else...
        assert(output.indexCacheBuffer == nullptr && output.historyBuffer[0] == nullptr);

        // Create a index buffer and vertex buffer we can use for raytracing.
        DxvkBufferCreateInfo info;
        info.usage = VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
        info.stages = VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR;
        info.access = VK_ACCESS_TRANSFER_WRITE_BIT;

        info.size = align(output.indexCount * indexStride, CACHE_LINE_SIZE);
        output.indexCacheBuffer = m_device->createBuffer(info, memoryProperty, DxvkMemoryStats::Category::RTXAccelerationStructure, "Index Cache Buffer");

        if (!RtxGeometryUtils::cacheIndexDataOnGPU(ctx, input, output)) {
          ONCE(Logger::err("processGeometryInfo: failed to cache index data on GPU"));
          return ObjectCacheState::kInvalid;
        }

        output.indexBuffer = RaytraceBuffer(DxvkBufferSlice(output.indexCacheBuffer), 0, indexStride, indexBufferType);

        info.size = align(vertexBufferSize, CACHE_LINE_SIZE);
        output.historyBuffer[0] = m_device->createBuffer(info, memoryProperty, DxvkMemoryStats::Category::RTXAccelerationStructure, "Geometry Buffer");

        RtxGeometryUtils::cacheVertexDataOnGPU(ctx, input, output, forceNormals);

        break;
      }
      case ObjectCacheState::kUpdateBVH: {
        bool invalidateHistory = false;

        // Stride changed, so we must recreate the previous buffer and use identical data
        if (output.historyBuffer[0]->info().size != align(vertexStride * input.vertexCount, CACHE_LINE_SIZE)) {
          auto desc = output.historyBuffer[0]->info();
          desc.size = align(vertexStride * input.vertexCount, CACHE_LINE_SIZE);
          output.historyBuffer[0] = m_device->createBuffer(desc, memoryProperty, DxvkMemoryStats::Category::RTXAccelerationStructure, "Geometry Buffer");

          // Invalidate the current buffer
          output.historyBuffer[1] = nullptr;

          // Mark this object for realignment
          invalidateHistory = true;
        }

        // Use the previous updates vertex data for previous position lookup
        std::swap(output.historyBuffer[0], output.historyBuffer[1]);

        if (output.historyBuffer[0].ptr() == nullptr) {
          // First frame this object has been dynamic need to allocate a 2nd frame of data to preserve history.
          output.historyBuffer[0] = m_device->createBuffer(output.historyBuffer[1]->info(), memoryProperty, DxvkMemoryStats::Category::RTXAccelerationStructure, "Geometry Buffer");
        } 

        RtxGeometryUtils::cacheVertexDataOnGPU(ctx, input, output, forceNormals);

        // Sometimes, we need to invalidate history, do that here by copying the current buffer to the previous..
        if (invalidateHistory) {
          ctx->copyBuffer(output.historyBuffer[1], 0, output.historyBuffer[0], 0, output.historyBuffer[1]->info().size);
        }

        // Assign the previous buffer using the last slice (copy most params from the position, just change buffer)
        output.previousPositionBuffer = RaytraceBuffer(DxvkBufferSlice(output.historyBuffer[1], 0, output.positionBuffer.length()), output.positionBuffer.offsetFromSlice(), output.positionBuffer.stride(), output.positionBuffer.vertexFormat());
        break;
      }
      default:
        break;
    }

    // Update color buffer in BVH with DrawCallState
    // The user can disable/enable color buffer for specific materials, so we manually sync the DrawCallState and BVH here to keep the color buffer in BVH updated.
    // Note, we don't setup kUpdateBVH because it's too waste to update all buffers if only the color buffer needs to be updated.
    if (output.color0Buffer.defined() && !drawCallState.geometryData.color0Buffer.defined()) {
      // Remove the color buffer in BVH if the color buffer from drawcall is removed by ignoreBakedLighting
      output.color0Buffer = RaytraceBuffer();
    } else if (!output.color0Buffer.defined() && drawCallState.geometryData.color0Buffer.defined()) {
      // Write the color buffer back to BVH if the color buffer is enabled again
      const DxvkBufferSlice slice = DxvkBufferSlice(output.historyBuffer[0]);
      const auto& colorBuffer = drawCallState.geometryData.color0Buffer;
      output.color0Buffer = RaytraceBuffer(slice, colorBuffer.offsetFromSlice(), colorBuffer.stride(), colorBuffer.vertexFormat());
    }

    // Update buffers in the cache
    updateBufferCache(output);

    // Finalize our modified geometry data to the output
    inOutGeometry = output;

    return result;
  }


  void SceneManager::onFrameEnd(Rc<DxvkContext> ctx, bool raytracedThisFrame) {
    ScopedCpuProfileZone();

    // Commit this frame's texture registrations for preserve next frame. Must run before
    // manageTextureVram(), which may clear the cache and bump the generation so the
    // following frame takes the dynamic path for every draw call.
    m_textureCacheGenerationValidForPreserve =
        m_device->getCommon()->getTextureManager().getTextureCacheGeneration();

    manageTextureVram();

    if (m_enqueueDelayedClear || m_pReplacer->checkForChanges(ctx)) {
      clear(ctx, true);
      m_enqueueDelayedClear = false;
    }

    m_cameraManager.onFrameEnd();
    m_instanceManager.onFrameEnd();
    m_previousFrameSceneAvailable = raytracedThisFrame && RtxOptions::enablePreviousTLAS();

    m_bufferCache.clear();
    if (raytracedThisFrame) {
      std::lock_guard lock { m_drawCallMeta.mutex };
      const uint8_t curTick = m_drawCallMeta.ticker;
      const uint8_t nextTick = (m_drawCallMeta.ticker + 1) % m_drawCallMeta.MaxTicks;

      m_drawCallMeta.ready[curTick] = true;

      m_drawCallMeta.infos[nextTick].clear();
      m_drawCallMeta.ready[nextTick] = false;
      m_drawCallMeta.ticker = nextTick;
    }

    m_terrainBaker->onFrameEnd(ctx);

    if (m_opacityMicromapManager) {
      m_opacityMicromapManager->onFrameEnd();
    }
    
    m_activePOMCount = 0;
    m_startInMediumMaterialIndex = SURFACE_INDEX_INVALID;
    m_fogStartInMediumMaterialIndex_inCache = UINT32_MAX;
    m_startInMediumMaterialIndex_inCache = UINT32_MAX;

    if (m_uniqueObjectSearchDistance != RtxOptions::uniqueObjectDistance()) {
      m_uniqueObjectSearchDistance = RtxOptions::uniqueObjectDistance();
      m_drawCallTracker.rebuildSpatialMaps(m_uniqueObjectSearchDistance * 2.f);
    }

    // Not currently safe to cache these across frames (due to texture indices and rtx options potentially changing)
    m_preCreationSurfaceMaterialMap.clear();

    m_thinOpaqueMaterialExist = false;
    m_sssMaterialExist = false;

    // execute graph updates after all garbage collection is complete (to avoid updating graphs that will just be deleted)
    // RtxOptions will still be pending, so any changes to them will apply next frame.
    if (raytracedThisFrame){
      m_graphManager.update(ctx);
    }

    // Clear replacement material hashes before the next frame.  These are used by components, so must clear after graphManager updates.
    clearFrameReplacementMaterialHashes();
    
    // Clear mesh hashes before the next frame.  These are used by components, so must clear after graphManager updates.
    clearFrameMeshHashes();
    
    // Reset the fog state to get it re-discovered on the next frame
    ImGUI::SetFogStates(m_fogStates, m_fog.getHash());
    m_fog = FogState();
    m_fogStates.clear();
  }

  std::unordered_set<XXH64_hash_t> uniqueHashes;


  void SceneManager::submitDrawState(Rc<DxvkContext> ctx, const DrawCallState& input, const MaterialData* overrideMaterialData) {
    ScopedCpuProfileZone();
    if (m_bufferCache.getTotalCount() >= kBufferCacheLimit && m_bufferCache.getActiveCount() >= kBufferCacheLimit) {
      ONCE(Logger::info("[RTX-Compatibility-Info] This application is pushing more unique buffers than is currently supported - some objects may not raytrace."));
      return;
    }

    if (input.getFogState().mode != D3DFOG_NONE) {
      XXH64_hash_t fogHash = input.getFogState().getHash();
      if (m_fogStates.find(fogHash) == m_fogStates.end()) {
        // Only do anything if we haven't seen this fog before.
        m_fogStates[fogHash] = input.getFogState();

        MaterialData* pFogReplacement = m_pReplacer->getReplacementMaterial(fogHash);
        if (pFogReplacement) {
          // Track this replacement material hash for hash checking
          trackReplacementMaterialHash(fogHash);
          // Fog has been replaced by a translucent material to start the camera in,
          // meaning that it was being used to indicate 'underwater' or something similar.
          if (pFogReplacement->getType() != MaterialDataType::Translucent) {
            Logger::warn(str::format("Fog replacement materials must be translucent.  Ignoring material for ", std::hex, m_fog.getHash()));
          } else {
            uint32_t id = UINT32_MAX;
            createSurfaceMaterial(*pFogReplacement, input, &id);
            assert(id != UINT32_MAX);
            m_fogStartInMediumMaterialIndex_inCache = id;
          }
        } else if (m_fog.mode == D3DFOG_NONE) {
          // render the first unreplaced fog.
          m_fog = input.getFogState();
        }
      }
    }


    const XXH64_hash_t activeReplacementHash = input.getHash(RtxOptions::geometryAssetHashRule());
    
    // Track this mesh hash for mesh hash checking
    trackMeshHash(activeReplacementHash);

    std::vector<AssetReplacement>* pReplacements = m_pReplacer->getReplacementsForMesh(activeReplacementHash);

    // TODO (REMIX-656): Remove this once we can transition content to new hash
    if ((RtxOptions::geometryHashGenerationRule() & rules::LegacyAssetHash0) == rules::LegacyAssetHash0) {
      if (!pReplacements) {
        const XXH64_hash_t legacyHash = input.getHashLegacy(rules::LegacyAssetHash0);
        trackMeshHash(legacyHash);
        pReplacements = m_pReplacer->getReplacementsForMesh(legacyHash);
        if (RtxOptions::logLegacyHashReplacementMatches() && pReplacements && uniqueHashes.find(legacyHash) == uniqueHashes.end()) {
          uniqueHashes.insert(legacyHash);
          Logger::info(str::format("[Legacy-Hash-Replacement] Found a mesh referenced from legacyHash0: ", std::hex, legacyHash, ", new hash: ", std::hex, activeReplacementHash));
        }
      }
    }

    if ((RtxOptions::geometryHashGenerationRule() & rules::LegacyAssetHash1) == rules::LegacyAssetHash1) {
      if (!pReplacements) {
        const XXH64_hash_t legacyHash = input.getHashLegacy(rules::LegacyAssetHash1);
        trackMeshHash(legacyHash);
        pReplacements = m_pReplacer->getReplacementsForMesh(legacyHash);
        if (RtxOptions::logLegacyHashReplacementMatches() && pReplacements && uniqueHashes.find(legacyHash) == uniqueHashes.end()) {
          uniqueHashes.insert(legacyHash);
          Logger::info(str::format("[Legacy-Hash-Replacement] Found a mesh referenced from legacyHash1: ", std::hex, legacyHash, ", new hash: ", std::hex, activeReplacementHash));
        }
      }
    }

    ReplacementInstance* replacementInstance = m_drawCallTracker.findOrCreateReplacementInstance(
        input, m_rayPortalManager, overrideMaterialData);

    const uint32_t currentFrameId = m_device->getCurrentFrameId();
    const bool secondSubmissionThisFrame = (replacementInstance->frameLastSeen == currentFrameId);

    // Preserve path: L1 means this draw still matches the same ReplacementInstance by identity; replacer reload is
    // expected to clear the draw-call tracker (see SceneManager::clear), so we do not re-verify mesh prims or
    // activeReplacements pointers every frame. If a path exists where replacements bind without a clear, use dynamic
    // (drawReplacements) for that transition — drawReplacements already reconciles activeReplacements and prims.
    //
    // Static path reuses each prim's BlasEntry::modifiedGeometryData as-is. If another draw earlier this frame
    // already entered DrawCallCache::get and re-bound a sibling-topology BlasEntry to its own data (kUpdateBVH),
    // the cached buffers no longer correspond to this draw — fall back to dynamic so DrawCallCache::get's
    // "frameLastTouched skip" allocates a fresh BlasEntry and processSceneObject re-links the instance.
    auto blasAlreadyTouchedByOtherDraw = [replacementInstance, currentFrameId]() -> bool {
      for (const auto& prim : replacementInstance->prims) {
        RtInstance* inst = prim.getInstance();
        if (inst == nullptr) {
          continue;
        }
        BlasEntry* pBlas = inst->getBlas();
        if (pBlas == nullptr) {
          continue;
        }
        if (pBlas->frameLastTouched == currentFrameId) {
          return true;
        }
      }
      return false;
    };

    // Replacements with attached particle systems require processDrawCallState's particle-system wiring,
    // which the preserve path doesn't replicate. Outer ParticleEmitter check only covers the input draw call
    // (not replacement-attached emitters), so check here too.
    auto anyReplacementHasParticleSystem = [pReplacements]() -> bool {
      if (pReplacements == nullptr) {
        return false;
      }
      for (const auto& rep : *pReplacements) {
        if (rep.particleSystem.has_value()) {
          return true;
        }
      }
      return false;
    };

    // The RI's prims must already be wired up for this exact replacements vector. drawReplacements
    // re-initializes prims when activeReplacements changes (e.g. async replacement load completes
    // after the RI was created without replacements, or hot-reload changes the replacement set).
    // The preserve path has no equivalent reinitialization, so fall back to dynamic for that transition.
    const bool activeReplacementsMatch =
        replacementInstance->activeReplacements == pReplacements;

    // Terrain draws share a per-frame override OpaqueMaterialData built from the
    // TerrainBaker cascade set. Cascade images don't carry a stable identity hash,
    // so MaterialData::getHash() doesn't change when the cascade set grows or shrinks
    // (e.g. when the normal cascade is first added on top of an albedo-only set on
    // an earlier frame). The preserve path would then reuse the surfaceMaterialIndex
    // computed before the new cascade textures existed, leaving the secondary
    // texture slots invalid. Force the dynamic path on any frame in which a cascade
    // image was created or resized; subsequent stable frames take the preserve path
    // with the freshly built RtSurfaceMaterial.
    const bool terrainCascadesJustChanged =
        input.getCategoryFlags().test(InstanceCategories::Terrain) &&
        m_terrainBaker->cascadeCompositionChangedThisFrame();

    const bool cachedTexturesValidForPreserve =
        m_device->getCommon()->getTextureManager().getTextureCacheGeneration() ==
        m_textureCacheGenerationValidForPreserve;

    const bool usePreservePath =
        RtxOptions::enablePreservePath() &&
        replacementInstance->dirtyFlags.isClear() &&
        !secondSubmissionThisFrame &&
        !input.getCategoryFlags().test(InstanceCategories::ParticleEmitter) &&
        !RtxOptions::shouldConvertToLight(input.getMaterialData().getHash()) &&
        !blasAlreadyTouchedByOtherDraw() &&
        !anyReplacementHasParticleSystem() &&
        activeReplacementsMatch &&
        !terrainCascadesJustChanged &&
        cachedTexturesValidForPreserve;


    if (usePreservePath) {
      preserveReplacementInstance(ctx, input, pReplacements, replacementInstance);
    } else {
      MaterialData renderMaterialData = determineMaterialData(overrideMaterialData, input);
      if (!activeReplacementsMatch) {
        replacementInstance->clear();
      }
      // Create / process: full geometry cache and instance update.
      if (pReplacements != nullptr) {
        drawReplacements(ctx, &input, pReplacements, renderMaterialData, replacementInstance);
      } else {
        RtInstance* existingInstance = (replacementInstance->prims.size() > 0)
            ? replacementInstance->prims[0].getInstance() : nullptr;

        RtInstance* instance = processDrawCallState(ctx, input, renderMaterialData,
            existingInstance, nullptr);
        if (instance != nullptr) {
          if (replacementInstance->root.getUntyped() == nullptr) {
            replacementInstance->setup(PrimInstance(instance, PrimInstance::Type::Instance), 1, nullptr);
          }
          if (replacementInstance->prims[0].getUntyped() != instance) {
            instance->getPrimInstanceOwner().setReplacementInstance(replacementInstance, 0, instance,
                PrimInstance::Type::Instance);
          }
        }
      }
    }

    replacementInstance->frameLastSeen = currentFrameId;
    replacementInstance->categoryFlags = input.getCategoryFlags().raw();
    replacementInstance->isSkinned = input.getSkinningState().numBones > 0;

    // Cache this submission's texture-coordinate projection so that next frame's
    // computeDirtyFlags can detect drift. Writing here (after either path has run)
    // mirrors how objectToWorld is updated downstream of the dirty-flag check.
    replacementInstance->textureTransform = input.getTransformData().textureTransform;
    replacementInstance->texgenMode = input.getTransformData().texgenMode;

    // For standalone draw calls, store the object-space bounding box for anti-culling.
    // For replacement draw calls, the aggregate AABB is computed inside drawReplacements.
    if (pReplacements == nullptr) {
      const auto& geoBBox = input.getGeometryData().boundingBox;
      if (geoBBox.isValid()) {
        replacementInstance->geometryBoundingBox = geoBBox;
        replacementInstance->objectToWorld = input.getTransformData().objectToWorld;
      }
    }
  }

  MaterialData SceneManager::determineMaterialData(const MaterialData* overrideMaterialData, const DrawCallState& input) {
    ScopedCpuProfileZone();
    // First see if we have an explicit override
    if (overrideMaterialData != nullptr) {
      return *overrideMaterialData;
    } 

    // test if any direct material replacements exist
    MaterialData* pReplacementMaterial = m_pReplacer->getReplacementMaterial(input.getMaterialData().getHash());
    if (pReplacementMaterial != nullptr) {
      // Make a copy - dont modify the replacement data.
      MaterialData renderMaterialData = *pReplacementMaterial;
      // merge in the input material from game
      renderMaterialData.mergeLegacyMaterial(input.getMaterialData());
      return renderMaterialData;
    }

    // Check if a Ray Portal override is needed
    size_t rayPortalTextureIndex;
    if (RtxOptions::getRayPortalTextureIndex(input.getMaterialData().getHash(), rayPortalTextureIndex)) {
      assert(rayPortalTextureIndex < maxRayPortalCount);
      assert(rayPortalTextureIndex < (std::numeric_limits<uint8_t>::max)());

      MaterialData renderMaterialData = input.getMaterialData().as<RayPortalMaterialData>();
      renderMaterialData.getRayPortalMaterialData().setRayPortalIndex(rayPortalTextureIndex);
      return renderMaterialData;
    }

    // Standard legacy material conversion
    return input.getMaterialData().as<OpaqueMaterialData>();
  }

  void SceneManager::createEffectLight(Rc<DxvkContext> ctx, const DrawCallState& input, const RtInstance* instance) {
    const float effectLightIntensity = RtxOptions::effectLightIntensity();
    if (effectLightIntensity <= 0.f)
      return;

    const RasterGeometry& geometryData = input.getGeometryData();

    const GeometryBufferData bufferData(geometryData);
    
    if (!bufferData.indexData && geometryData.indexCount > 0 || !bufferData.positionData)
      return;

    // Find centroid of point cloud.
    Vector3 centroid = Vector3();
    uint32_t counter = 0;
    if (geometryData.indexCount > 0) {
      for (uint32_t i = 0; i < geometryData.indexCount; i++) {
        const uint16_t index = bufferData.getIndex(i);
        centroid += bufferData.getPosition(index);
        ++counter;
      }
    } else {
      for (uint32_t i = 0; i < geometryData.vertexCount; i++) {
        centroid += bufferData.getPosition(i);
        ++counter;
      }
    }
    centroid /= (float) counter;
    
    const Vector4 renderingPos = input.getTransformData().objectToView * Vector4(centroid.x, centroid.y, centroid.z, 1.0f);
    // Note: False used in getViewToWorld since the renderingPos of the object is defined with respect to the game's object to view
    // matrix, not our freecam's, and as such we want to convert it back to world space using the matching matrix.
    const Vector4 worldPos{ getCamera().getViewToWorld(false) * Vector4d{ renderingPos } };

    RtLightShaping shaping{};

    float lightRadius = std::max(RtxOptions::effectLightRadius(), 1e-3f);
    const Vector3 lightPosition { worldPos.x, worldPos.y, worldPos.z };
    Vector3 lightRadiance;
    if (RtxOptions::effectLightPlasmaBall()) {
      // Todo: Make these options more configurable via config options.
      const double timeMilliseconds = static_cast<double>(GlobalTime::get().absoluteTimeMs());
      const double animationPhase = sin(timeMilliseconds * 0.006) * 0.5 + 0.5;
      lightRadiance = lerp(Vector3(1.f, 0.921f, 0.738f), Vector3(1.f, 0.521f, 0.238f), animationPhase);
    } else {
      const D3DCOLORVALUE originalColor = input.getMaterialData().getLegacyMaterial().Diffuse;
      lightRadiance = Vector3(originalColor.r, originalColor.g, originalColor.b) * RtxOptions::effectLightColor();
    }
    const float surfaceArea = 4.f * kPi * lightRadius * lightRadius;
    const float radianceFactor = 1e5f * effectLightIntensity / surfaceArea;
    lightRadiance *= radianceFactor;

    RtLight rtLight(RtSphereLight(lightPosition, lightRadiance, lightRadius, shaping));
    rtLight.isDynamic = true;

    m_lightManager.addLight(rtLight, input, RtLightAntiCullingType::MeshReplacement);
  }

  std::optional<DrawCallState> SceneManager::buildReplacementMeshDrawCallState(
      const DrawCallState& input,
      const AssetReplacement& replacement) {
    if (replacement.includeOriginal) {
      DrawCallState newDrawCallState(input);
      newDrawCallState.categories = replacement.categories.applyCategoryFlags(newDrawCallState.categories);
      return newDrawCallState;
    }

    if (replacement.type == AssetReplacement::eMesh) {
      DrawCallTransforms transforms = input.getTransformData();
      transforms.objectToWorld = transforms.objectToWorld * replacement.replacementToObject;
      transforms.objectToView = transforms.objectToView * replacement.replacementToObject;
      if (replacement.instancesToObject && !replacement.instancesToObject->empty()) {
        transforms.instancesToObject = replacement.instancesToObject;
      } else {
        transforms.instancesToObject = nullptr;
      }
      // Mesh replacements don't support these.
      transforms.textureTransform = Matrix4();
      transforms.texgenMode = TexGenMode::None;

      DrawCallState newDrawCallState(input);
      newDrawCallState.geometryData = replacement.geometry->data; // Note: Geometry Data replaced
      newDrawCallState.transformData = transforms;
      newDrawCallState.categories = replacement.categories.applyCategoryFlags(newDrawCallState.categories);
      return newDrawCallState;
    }

    return std::nullopt;
  }

  void SceneManager::drawReplacements(Rc<DxvkContext> ctx, const DrawCallState* input, const std::vector<AssetReplacement>* pReplacements, MaterialData& renderMaterialData, ReplacementInstance* replacementInstance) {
    ScopedCpuProfileZone();
    if (pReplacements == nullptr) {
      assert(false && "pReplacements should never be nullptr here");
      ONCE(Logger::err("pReplacements should never be nullptr in SceneManager::drawReplacements"));
      return;
    }
    // If the index contains an RtInstance, get a pointer to it.
    auto getExistingInstance = [replacementInstance](size_t idx) -> RtInstance* {
      if (replacementInstance->prims.size() <= idx) {
        return nullptr;
      }
      return replacementInstance->prims[idx].getInstance();
    };

    for (size_t i = 0; i < pReplacements->size(); i++) {
      auto& replacement = (*pReplacements)[i];
      RtInstance* instance = nullptr;

      std::optional<DrawCallState> newDrawCallState = SceneManager::buildReplacementMeshDrawCallState(*input, replacement);
      if (newDrawCallState.has_value()) {
        // Note: Material Data replaced if a replacement is specified in the Mesh Replacement.
        // Only meaningful when geometry is replaced (eMesh); the includeOriginal branch keeps the
        // game's original material data.
        if (!replacement.includeOriginal && replacement.type == AssetReplacement::eMesh && replacement.materialData != nullptr) {
          renderMaterialData = *replacement.materialData;
        }

        const RtxParticleSystemDesc* pParticleSystemDesc = replacement.particleSystem.has_value() ? &replacement.particleSystem.value() : nullptr;
        instance = processDrawCallState(ctx, *newDrawCallState, renderMaterialData, getExistingInstance(i), pParticleSystemDesc);
      }

      if (instance != nullptr) {
        if (replacementInstance->root.getUntyped() == nullptr) {
          // This is the first time this replacementInstance is used, and the first mesh drawn
          //  as part of this replacementInstance, so invoke setup and set the root.
          replacementInstance->setup(PrimInstance(instance, PrimInstance::Type::Instance), pReplacements->size(), pReplacements);
          instance->getPrimInstanceOwner().setReplacementInstance(replacementInstance, i, instance, PrimInstance::Type::Instance);
        } else if (replacementInstance->prims[i].getUntyped() != instance) {
          instance->getPrimInstanceOwner().setReplacementInstance(replacementInstance, i, instance, PrimInstance::Type::Instance);
        }
      }
    }

    processReplacementLights(input, pReplacements, replacementInstance);
    processReplacementGraphs(ctx, input, pReplacements, replacementInstance);

    replacementInstance->recalculateBoundingBox(
        input->getTransformData().objectToWorld,
        &input->getGeometryData().boundingBox);
  }
  
  void SceneManager::processReplacementLights(
      const DrawCallState* input, const std::vector<AssetReplacement>* pReplacements,
      ReplacementInstance* replacementInstance) {
    ScopedCpuProfileZone();
    for (size_t i = 0; i < pReplacements->size(); i++) {
      auto&& replacement = (*pReplacements)[i];
      if (replacement.type == AssetReplacement::eLight) {
        if (replacementInstance->root.getUntyped() == nullptr) {
          Logger::err(str::format(
              "Light prims anchored to a mesh replacement must also include actual meshes.  mesh hash: ",
              std::hex, input->getHash(RtxOptions::geometryAssetHashRule())
          ));
          break;
        }
        if (replacement.lightData.has_value()) {
          RtLight objectSpaceLight = replacement.lightData->toRtLight();

          // Transform to world space for the actual light creation
          RtLight localLight = objectSpaceLight;
          localLight.applyTransform(input->getTransformData().objectToWorld);

          RtLight* existingLight = (replacementInstance->prims.size() > i)
              ? replacementInstance->prims[i].getLight() : nullptr;
          if (existingLight != nullptr) {
            if (existingLight->getPrimInstanceOwner().getReplacementInstance() != replacementInstance) {
              ONCE(assert(false && "light in a replacementInstance believes it is owned by a different replacementInstance."));
            }
            m_lightManager.updateExternallyTrackedLight(existingLight, localLight);
          } else {
            RtLight* newLight = m_lightManager.createExternallyTrackedLight(localLight);
            newLight->getPrimInstanceOwner().setReplacementInstance(replacementInstance, i, newLight, PrimInstance::Type::Light);
          }
        }
      }
    }
  }

  void SceneManager::processReplacementGraphs(
      Rc<DxvkContext> ctx, const DrawCallState* input, const std::vector<AssetReplacement>* pReplacements,
      ReplacementInstance* replacementInstance) {
    ScopedCpuProfileZone();
    for (size_t i = 0; i < pReplacements->size(); i++) {
      auto&& replacement = (*pReplacements)[i];
      if (replacement.type == AssetReplacement::eGraph) {
        bool hasGraph = (replacementInstance->prims.size() > i) &&
                        (replacementInstance->prims[i].getGraph() != nullptr);
        if (!hasGraph) {
          if (!replacement.graphState.has_value()) {
            Logger::err(str::format(
                "Graph prims missing graph state in mesh replacement.  mesh hash: ",
                std::hex, input->getHash(RtxOptions::geometryAssetHashRule())
            ));
            break;
          }
          GraphInstance* graphInstance = m_graphManager.addInstance(ctx, replacement.graphState.value());
          if (graphInstance) {
            graphInstance->getPrimInstanceOwner().setReplacementInstance(replacementInstance, i, graphInstance, PrimInstance::Type::Graph);
          }
        }
      }
    }
  }

  void SceneManager::updateBufferCache(RaytraceGeometry& newGeoData) {
    ScopedCpuProfileZone();
    if (newGeoData.indexBuffer.defined()) {
      newGeoData.indexBufferIndex = m_bufferCache.track(newGeoData.indexBuffer);
    } else {
      newGeoData.indexBufferIndex = kSurfaceInvalidBufferIndex;
    }

    if (newGeoData.normalBuffer.defined()) {
      newGeoData.normalBufferIndex = m_bufferCache.track(newGeoData.normalBuffer);
    } else {
      newGeoData.normalBufferIndex = kSurfaceInvalidBufferIndex;
    }

    if (newGeoData.color0Buffer.defined()) {
      newGeoData.color0BufferIndex = m_bufferCache.track(newGeoData.color0Buffer);
    } else {
      newGeoData.color0BufferIndex = kSurfaceInvalidBufferIndex;
    }

    if (newGeoData.texcoordBuffer.defined()) {
      newGeoData.texcoordBufferIndex = m_bufferCache.track(newGeoData.texcoordBuffer);
    } else {
      newGeoData.texcoordBufferIndex = kSurfaceInvalidBufferIndex;
    }

    if (newGeoData.positionBuffer.defined()) {
      newGeoData.positionBufferIndex = m_bufferCache.track(newGeoData.positionBuffer);
    } else {
      newGeoData.positionBufferIndex = kSurfaceInvalidBufferIndex;
    }

    if (newGeoData.previousPositionBuffer.defined()) {
      newGeoData.previousPositionBufferIndex = m_bufferCache.track(newGeoData.previousPositionBuffer);
    } else {
      newGeoData.previousPositionBufferIndex = kSurfaceInvalidBufferIndex;
    }
  }

  void SceneManager::preserveInstance(
      RtInstance& instance,
      const DrawCallState* pInput) {
    ScopedCpuProfileZone();
    BlasEntry* pBlas = instance.getBlas();
    if (pBlas == nullptr) {
      return;
    }

    instance.setFrameLastUpdated(m_device->getCurrentFrameId());

    // Preserve path: keep RtInstance surface/material/transform/mask state from the last dynamic update.
    // Only refresh per-frame buffer-cache indices (and BLAS touch / texture lifetime) so GPU addresses stay valid.

    // Preserve / anti-culled draw: positions are unchanged this frame, so there is no meaningful
    // previousPosition data. Clear it to match processGeometryInfo's kUpdateInstance behavior
    // (it always clears, then only kUpdateBVH re-points it at historyBuffer[1]); without this,
    // a stale buffer left over from the last kUpdateBVH frame would feed into motion vectors.
    pBlas->modifiedGeometryData.previousPositionBuffer = RaytraceBuffer();

    // Buffer indices are per-frame (m_bufferCache is cleared in onFrameEnd),
    // so re-register geometry buffers and copy fresh indices to the surface.
    updateBufferCache(pBlas->modifiedGeometryData);
    m_instanceManager.processInstanceBuffers(*pBlas, instance);

    pBlas->frameLastTouched = m_device->getCurrentFrameId();

    // Surface material and texture indices are generally stable across frames.
    // If textureManager::clear() is called, the texture cache generation will change,
    // and the draw calls will take the dynamic path the next frame.
    // Refresh texture streaming on the preserve path: fetchNoisyMipCounts clears m_related each
    // GC, so we must repeat preserveTexture(TextureRef,...) (not just associate). Opaque subsurface-extension maps are
    // not in RtSurfaceMaterial::forEachTextureIndex — include those explicitly. Material graph and
    // extension cache are scene-owned; leader stamp resolution stays here (RtxTextureManager::preserveTexture).
    const uint32_t surfaceMatIdx = instance.surface.surfaceMaterialIndex;
    if (surfaceMatIdx < m_surfaceMaterialCache.getTotalCount()) {
      auto& textureManager = m_device->getCommon()->getTextureManager();
      const RtSurfaceMaterial& surfaceMat = m_surfaceMaterialCache.getObjectTable()[surfaceMatIdx];
      const RtOpaqueSurfaceMaterial* pOpaqueMat = nullptr;
      uint16_t leaderStamp = SAMPLER_FEEDBACK_INVALID;
      if (surfaceMat.getType() == RtSurfaceMaterialType::Opaque) {
        pOpaqueMat = &surfaceMat.getOpaqueSurfaceMaterial();
        leaderStamp = pOpaqueMat->getSamplerFeedbackStamp();
        if (leaderStamp == SAMPLER_FEEDBACK_INVALID) {
          const uint32_t albedoIdx = pOpaqueMat->getAlbedoOpacityTextureIndex();
          const auto& textureTable = textureManager.getTextureTable();
          if (albedoIdx < textureTable.size()) {
            const Rc<ManagedTexture>& albedoMt = textureTable[albedoIdx].getManagedTexture();
            if (albedoMt != nullptr) {
              leaderStamp = albedoMt->m_samplerFeedbackStamp;
            }
          }
        }
      }

      const auto touchTexture = [&](uint32_t texIdx) {
        textureManager.preserveTexture(texIdx, leaderStamp);
      };

      surfaceMat.forEachTextureIndex(touchTexture);

      if (pOpaqueMat != nullptr) {
        // Per-frame aggregate flags/counts that createSurfaceMaterial sets on the
        // dynamic path are reset each frame in onFrameEnd. The preserve path skips
        // createSurfaceMaterial, so without this call a frame where every POM /
        // SSS / thin-opaque draw is preserved would leave the aggregates at their
        // reset value and silently disable POM (constants.pomMode is gated on
        // getActivePOMCount() > 0) or the SSS pipeline branches.
        accumulateOpaqueMaterialAggregates(*pOpaqueMat);

        const uint32_t subsurfaceIdx = pOpaqueMat->getSubsurfaceMaterialIndex();
        if (subsurfaceIdx != SURFACE_INDEX_INVALID &&
            subsurfaceIdx < m_surfaceMaterialExtensionCache.getTotalCount()) {
          m_surfaceMaterialExtensionCache.getObjectTable()[subsurfaceIdx].forEachTextureIndex(touchTexture);
        }
      }
    }

    // Ray Portal refresh on the preserve path. RayPortalManager::clear() wipes m_rayPortalInfos
    // every frame in endFrame, so processRayPortalData must repopulate the slot for any portal
    // instance still present. The cached RtSurfaceMaterial supplies the RayPortalIndex and the
    // resource-cache key processRayPortalData needs.
    if (instance.getMaterialType() == MaterialDataType::RayPortal &&
        surfaceMatIdx < m_surfaceMaterialCache.getTotalCount()) {
      const RtSurfaceMaterial& surfaceMaterial = m_surfaceMaterialCache.getObjectTable()[surfaceMatIdx];
      if (surfaceMaterial.getType() == RtSurfaceMaterialType::RayPortal) {
        m_rayPortalManager.processRayPortalData(instance, surfaceMaterial);
      }
    }

    // m_billboards is cleared every frame in InstanceManager::onFrameEnd, so re-run the
    // billboard step to repopulate it and refresh m_firstBillboard / m_billboardCount for
    // this frame's unordered TLAS. pInput is null on the anti-culling preserve path;
    // those instances are off-screen and don't need portal-space billboards.
    if (pInput != nullptr && m_cameraManager.isCameraValid(CameraType::Main)) {
      m_instanceManager.refreshBillboardsForCurrentFrame(
          instance,
          pInput->cameraType,
          m_cameraManager.getMainCamera().getDirection(false));
    }
  }

  void SceneManager::syncPreservedReplacementMeshesState(
      const DrawCallState& input,
      const std::vector<AssetReplacement>* pReplacements,
      ReplacementInstance* replacementInstance) {
    if (pReplacements == nullptr) {
      return;
    }
    // Push this frame's per-replacement DrawCallState into each prim's BlasEntry::input.
    // Preserve path: RtInstance state is unchanged from the last dynamic update; only BLAS
    // input tracks remix state. The construction matches drawReplacements() (both share
    // buildReplacementMeshDrawCallState) so dynamic and preserve paths feed identical
    // DrawCallStates into the BlasEntry.
    for (size_t i = 0; i < pReplacements->size(); i++) {
      if (replacementInstance->prims.size() <= i) {
        break;
      }
      RtInstance* instance = replacementInstance->prims[i].getInstance();
      if (instance == nullptr) {
        continue;
      }
      BlasEntry* pBlas = instance->getBlas();
      if (pBlas == nullptr) {
        continue;
      }
      std::optional<DrawCallState> newDrawCallState =
          SceneManager::buildReplacementMeshDrawCallState(input, (*pReplacements)[i]);
      if (newDrawCallState.has_value()) {
        pBlas->input = *newDrawCallState;
      }
    }
  }

  void SceneManager::preserveReplacementInstance(
      Rc<DxvkContext> ctx,
      const DrawCallState& input,
      const std::vector<AssetReplacement>* pReplacements,
      ReplacementInstance* replacementInstance) {
    ScopedCpuProfileZone();
    // Refresh BlasEntry::input with this frame's draw state BEFORE dispatching preserveInstance.
    // refreshBillboardsForCurrentFrame -> createBeams / createBillboards consult
    // pBlas->input.getGeometryData() and call mapPtr() on its RasterBuffer slices to read
    // CPU-side positions/texcoords. If pBlas->input is left over from a previous frame, those
    // slices may reference vertex memory that has been recycled (notably with useSharedHeap=off
    // in the bridge), giving zero-fill positions and tripping the normalize() assert in
    // createBeams. Refreshing here keeps the dynamic and preserve paths feeding the same
    // frame's geometry into billboard / beam creation.
    if (pReplacements != nullptr) {
      replacementInstance->activeReplacements = pReplacements;
      syncPreservedReplacementMeshesState(input, pReplacements, replacementInstance);
    } else if (replacementInstance->prims.size() > 0) {
      RtInstance* inst = replacementInstance->prims[0].getInstance();
      if (inst != nullptr && inst->getBlas() != nullptr) {
        inst->getBlas()->input = input;
      }
    }

    // Re-register per-frame buffer cache indices (same order as dynamic path: buffers resolved first).
    // No MaterialData is threaded through: SceneManager::preserveInstance reads the cached
    // RtSurfaceMaterial via surfaceMaterialIndex (Ray Portals included), and InstanceManager
    // event handlers contract for a null material on the preserve path.
    for (auto& prim : replacementInstance->prims) {
      RtInstance* instance = prim.getInstance();
      if (instance != nullptr) {
        instance->surface.isPreservePath = true;
        preserveInstance(*instance, &input);
        m_instanceManager.preserveInstance(*instance, input, nullptr);
      }
    }
    replacementInstance->recalculateBoundingBox(
        input.getTransformData().objectToWorld, &input.getGeometryData().boundingBox);
  }

  SceneManager::ObjectCacheState SceneManager::onSceneObjectAdded(Rc<DxvkContext> ctx, const DrawCallState& drawCallState, BlasEntry* pBlas) {
    // This is a new object.
    ObjectCacheState result = processGeometryInfo<true>(ctx, drawCallState, pBlas->modifiedGeometryData);
    
    assert(result == ObjectCacheState::KBuildBVH);

    pBlas->frameLastUpdated = m_device->getCurrentFrameId();
    m_instanceManager.notifySceneChanged();

    return result;
  }
  
  SceneManager::ObjectCacheState SceneManager::onSceneObjectUpdated(Rc<DxvkContext> ctx, const DrawCallState& drawCallState, BlasEntry* pBlas) {
    if (pBlas->frameLastTouched == m_device->getCurrentFrameId()) {
      pBlas->cacheMaterial(drawCallState.getMaterialData());
      return SceneManager::ObjectCacheState::kUpdateInstance;
    }

    // TODO: If mesh is static, no need to do any of the below, just use the existing modifiedGeometryData and set result to kInstanceUpdate.
    ObjectCacheState result = processGeometryInfo<false>(ctx, drawCallState, pBlas->modifiedGeometryData);

    // We dont expect to hit the rebuild path here - since this would indicate an index buffer or other topological change, and that *should* trigger a new scene object (since the hash would change)
    assert(result != ObjectCacheState::KBuildBVH);

    if (result == ObjectCacheState::kUpdateBVH) {
      pBlas->frameLastUpdated = m_device->getCurrentFrameId();
      m_instanceManager.notifySceneChanged();
    }
    
    pBlas->clearMaterialCache();
    pBlas->input = drawCallState; // cache the draw state for the next time.
    return result;
  }
  
  void SceneManager::onInstanceAdded(RtInstance& instance) {
    BlasEntry* pBlas = instance.getBlas();
    if (pBlas != nullptr) {
      pBlas->linkInstance(&instance);
    }
  }

  void SceneManager::onInstanceUpdated(RtInstance& instance, const DrawCallState& drawCall, const MaterialData* material, const bool hasTransformChanged, const bool hasVerticesChanged, const bool isFirstUpdateThisFrame) {
    // The preserve path passes a null material and reuses RtSurfaceMaterial via
    // RtInstance::surface.surfaceMaterialIndex; the dynamic path always provides a material.
    if (instance.surface.isPreservePath) {
      assert(material == nullptr);
      return;
    }
    assert(material != nullptr);
    auto capturer = m_device->getCommon()->capturer();
    if (hasTransformChanged) {
      capturer->setInstanceUpdateFlag(instance, GameCapturer::InstFlag::XformUpdate);
    }

    if (hasVerticesChanged) {
      capturer->setInstanceUpdateFlag(instance, GameCapturer::InstFlag::PositionsUpdate);
      capturer->setInstanceUpdateFlag(instance, GameCapturer::InstFlag::NormalsUpdate);
    }
    
    // Create and bind the RT material
    const RtSurfaceMaterial& surfaceMaterial = createSurfaceMaterial(*material, drawCall);

    if(isFirstUpdateThisFrame) {
      m_instanceManager.bindMaterial(instance, surfaceMaterial);
    }

    // Update portal
    if (surfaceMaterial.getType() == RtSurfaceMaterialType::RayPortal) {
      m_rayPortalManager.processRayPortalData(instance, surfaceMaterial);
    }
  }

  void SceneManager::onInstanceDestroyed(RtInstance& instance) {
    // Evict from the AccelManager bucket cache to prevent stale pointer ABA issues.
    m_accelManager.removeInstanceFromBucketCache(&instance);

    BlasEntry* pBlas = instance.getBlas();
    if (pBlas != nullptr) {
      pBlas->unlinkInstance(&instance);
    }
  }

  // Helper to populate the texture cache with this resource (and patch sampler if required for texture)
  void SceneManager::trackTexture(const TextureRef &inputTexture,
                                  uint32_t& textureIndex,
                                  bool hasTexcoords,
                                  bool async,
                                  uint16_t samplerFeedbackStamp) {
    // If no texcoords, no need to bind the texture
    if (!hasTexcoords) {
      ONCE(Logger::info(str::format("[RTX-Compatibility-Info] Trying to bind a texture to a mesh without UVs.  Was this intended?")));
      return;
    }

    auto& textureManager = m_device->getCommon()->getTextureManager();
    textureManager.addTexture(inputTexture, samplerFeedbackStamp, async, textureIndex);
  }

  RtInstance* SceneManager::processDrawCallState(Rc<DxvkContext> ctx, const DrawCallState& drawCallState, MaterialData& renderMaterialData, RtInstance* existingInstance, const RtxParticleSystemDesc* pParticleSystemDesc) {
    ScopedCpuProfileZone();

    if (renderMaterialData.getIgnored()) {
      return nullptr;
    }

    ObjectCacheState result = ObjectCacheState::kInvalid;
    BlasEntry* pBlas = nullptr;
    if (m_drawCallCache.get(drawCallState, &pBlas) == DrawCallCache::CacheState::kExisted) {
      result = onSceneObjectUpdated(ctx, drawCallState, pBlas);
    } else {
      result = onSceneObjectAdded(ctx, drawCallState, pBlas);
    }
    
    assert(pBlas != nullptr);
    assert(result != ObjectCacheState::kInvalid);

    // Update the input state, so we always have a reference to the original draw call state
    pBlas->frameLastTouched = m_device->getCurrentFrameId();

    // Generate smooth normals for geometry that is flagged via the SmoothNormals texture category.
    // This is useful for older D3D9 games where geometry may lack smooth normals, especially
    // when using the VertexShader Capture mechanism. The smooth normals are computed on the GPU
    // from the triangle mesh (area-weighted) and written into the normal buffer.
    // Only dispatch on BVH build/update — for static geometry, positions don't change so
    // the normals computed on the first pass remain valid for subsequent frames.
    if (drawCallState.categories.test(InstanceCategories::SmoothNormals) &&
        (result == ObjectCacheState::KBuildBVH || result == ObjectCacheState::kUpdateBVH)) {
      m_device->getCommon()->metaGeometryUtils().dispatchSmoothNormals(ctx, drawCallState.getGeometryData(), pBlas->modifiedGeometryData);
      pBlas->modifiedGeometryData.smoothNormalsApplied = true;
      pBlas->frameLastUpdated = pBlas->frameLastTouched;
      m_instanceManager.notifySceneChanged();
    }

    if (drawCallState.getSkinningState().numBones > 0 &&
        drawCallState.getGeometryData().numBonesPerVertex > 0 &&
        (result == ObjectCacheState::KBuildBVH || result == ObjectCacheState::kUpdateBVH)) {
      m_device->getCommon()->metaGeometryUtils().dispatchSkinning(drawCallState, pBlas->modifiedGeometryData);
      pBlas->frameLastUpdated = pBlas->frameLastTouched;
      m_instanceManager.notifySceneChanged();
    }

    // Note: The material data can be modified in instance manager
    RtInstance* instance = m_instanceManager.processSceneObject(m_cameraManager, m_rayPortalManager, *pBlas, drawCallState, renderMaterialData, existingInstance);

    // Check if a light should be created for this Material
    if (instance && RtxOptions::shouldConvertToLight(drawCallState.getMaterialData().getHash())) {
      createEffectLight(ctx, drawCallState, instance);
    }

    const bool objectPickingActive = m_device->getCommon()->getResources().getRaytracingOutput()
      .m_primaryObjectPicking.isValid();

    if (objectPickingActive && instance && g_allowMappingLegacyHashToObjectPickingValue) {
      auto meta = DrawCallMetaInfo {};
      {
        XXH64_hash_t h;
        h = drawCallState.getMaterialData().getColorTexture().getImageHash();
        if (h != kEmptyHash) {
          meta.legacyTextureHash = h;
        }
        h = drawCallState.getMaterialData().getColorTexture2().getImageHash();
        if (h != kEmptyHash) {
          meta.legacyTextureHash2 = h;
        }
      }

      {
        std::lock_guard lock { m_drawCallMeta.mutex };
        auto [iter, isNew] = m_drawCallMeta.infos[m_drawCallMeta.ticker].emplace(instance->surface.objectPickingValue, meta);
        ONCE_IF_FALSE(isNew, Logger::warn(
          "Found multiple draw calls with the same \'objectPickingValue\'. "
          "Ignoring further MetaInfo-s, some objects might be not be available through object picking"));
      }
    }

    // Priority ordering for particle system descriptors is: Mesh, Material, Texture.  This matches the implementation in toolkit.
    // By this point, pParticleSystemDesc will contain the information from a mesh replacement (if one exists), so we just handle
    // materials replacements, and texture taggin categories below.
    RtxParticleSystemDesc globalParticleDesc; // Storage for global desc if needed
    if (!pParticleSystemDesc) {
      pParticleSystemDesc = renderMaterialData.getParticleSystemDesc();
    }
    if (!pParticleSystemDesc && drawCallState.categories.test(InstanceCategories::ParticleEmitter)) {
      globalParticleDesc = RtxParticleSystemManager::createGlobalParticleSystemDesc();
      pParticleSystemDesc = &globalParticleDesc;
    }
    if (instance && pParticleSystemDesc) {
      RtxParticleSystemManager& particleSystem = device()->getCommon()->metaParticleSystem();
      particleSystem.spawnParticles(ctx.ptr(), *pParticleSystemDesc, instance->getVectorIdx(), drawCallState, renderMaterialData);

      if (pParticleSystemDesc->hideEmitter) {
        instance->setHidden(true);
      }
    }

    return instance; 
  }

  const RtSurfaceMaterial& SceneManager::createSurfaceMaterial(const MaterialData& renderMaterialData,
                                                               const DrawCallState& drawCallState,
                                                               uint32_t* out_indexInCache) {
    ScopedCpuProfileZone();
    const bool hasTexcoords = drawCallState.hasTextureCoordinates();
    const auto renderMaterialDataType = renderMaterialData.getType();

    // We're going to use this to create a modified sampler for replacement textures.
    // Legacy and replacement materials should follow same filtering but due to lack of override capability per texture
    // legacy textures use original sampler to stay true to the original intent while replacements use more advanced filtering
    // for better quality by default.
    const Rc<DxvkSampler>& samplerOverride = renderMaterialData.getSamplerOverride();
    Rc<DxvkSampler> sampler = samplerOverride;
    // If the original sampler if valid and there isnt an override sampler
    // go ahead with patching and maybe merging the sampler states
    if (samplerOverride == nullptr && drawCallState.getMaterialData().getSampler().ptr() != nullptr) {
      DxvkSamplerCreateInfo samplerInfo = drawCallState.getMaterialData().getSampler()->info(); // Use sampler create info struct as convenience
      renderMaterialData.populateSamplerInfo(samplerInfo);

      sampler = patchSampler(samplerInfo.magFilter,
                             samplerInfo.addressModeU, samplerInfo.addressModeV, samplerInfo.addressModeW,
                             samplerInfo.borderColor);
    }
    if (drawCallState.isEye()) {
      // force eye whites and iris to not repeat
      sampler = patchSampler(
        VK_FILTER_LINEAR,
        VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        {}
      );
    }
    uint32_t samplerIndex = trackSampler(sampler);
    uint32_t samplerIndex2 = UINT32_MAX;
    if (renderMaterialDataType == MaterialDataType::RayPortal) {
      samplerIndex2 = trackSampler(drawCallState.getMaterialData().getSampler2());
    }

    XXH64_hash_t preCreationHash = renderMaterialData.getHash();
    preCreationHash = XXH64(&samplerIndex, sizeof(samplerIndex), preCreationHash);
    preCreationHash = XXH64(&samplerIndex2, sizeof(samplerIndex2), preCreationHash);
    preCreationHash = XXH64(&hasTexcoords, sizeof(hasTexcoords), preCreationHash);
    preCreationHash = XXH64(&drawCallState.isUsingRaytracedRenderTarget, sizeof(drawCallState.isUsingRaytracedRenderTarget), preCreationHash);

    // For Opaque materials, fold in a bitmask of which texture slots are populated. MaterialData::getHash()
    // sums TextureRef::getImageHash() across slots, but render-target-backed TextureRefs (notably
    // TerrainBaker cascades) have a zero image hash, so two materials that differ only in cascade
    // composition (e.g. "albedo only" vs "albedo + normal + roughness") collide on getHash() and the
    // per-frame cache would serve the earlier "albedo only" entry to later draws that have valid
    // secondary cascades.
    if (renderMaterialDataType == MaterialDataType::Opaque) {
      const auto& opaqueMaterialData = renderMaterialData.getOpaqueMaterialData();
      uint32_t texturePresenceMask = 0;
      texturePresenceMask |= opaqueMaterialData.getAlbedoOpacityTexture().isImageEmpty()             ? 0u : (1u << 0);
      texturePresenceMask |= opaqueMaterialData.getSecondaryTexture().isImageEmpty()                 ? 0u : (1u << 1);
      texturePresenceMask |= opaqueMaterialData.getNormalTexture().isImageEmpty()                    ? 0u : (1u << 2);
      texturePresenceMask |= opaqueMaterialData.getTangentTexture().isImageEmpty()                   ? 0u : (1u << 3);
      texturePresenceMask |= opaqueMaterialData.getHeightTexture().isImageEmpty()                    ? 0u : (1u << 4);
      texturePresenceMask |= opaqueMaterialData.getRoughnessTexture().isImageEmpty()                 ? 0u : (1u << 5);
      texturePresenceMask |= opaqueMaterialData.getMetallicTexture().isImageEmpty()                  ? 0u : (1u << 6);
      texturePresenceMask |= opaqueMaterialData.getEmissiveColorTexture().isImageEmpty()             ? 0u : (1u << 7);
      texturePresenceMask |= opaqueMaterialData.getSubsurfaceTransmittanceTexture().isImageEmpty()   ? 0u : (1u << 8);
      texturePresenceMask |= opaqueMaterialData.getSubsurfaceThicknessTexture().isImageEmpty()       ? 0u : (1u << 9);
      texturePresenceMask |= opaqueMaterialData.getSubsurfaceSingleScatteringAlbedoTexture().isImageEmpty() ? 0u : (1u << 10);
      texturePresenceMask |= opaqueMaterialData.getSubsurfaceRadiusTexture().isImageEmpty()          ? 0u : (1u << 11);
      preCreationHash = XXH64(&texturePresenceMask, sizeof(texturePresenceMask), preCreationHash);
    }

    auto iter = m_preCreationSurfaceMaterialMap.find(preCreationHash);
    if (iter != m_preCreationSurfaceMaterialMap.end()) {
      if (out_indexInCache) {
        *out_indexInCache = iter->second;
      }
      return m_surfaceMaterialCache.at(iter->second);
    }

    std::optional<RtSurfaceMaterial> surfaceMaterial;

    if (renderMaterialDataType == MaterialDataType::Opaque || drawCallState.isUsingRaytracedRenderTarget) {
      uint32_t albedoOpacityTextureIndex = kSurfaceMaterialInvalidTextureIndex;
      uint32_t secondaryTextureIndex = kSurfaceMaterialInvalidTextureIndex;
      uint32_t normalTextureIndex = kSurfaceMaterialInvalidTextureIndex;
      uint32_t tangentTextureIndex = kSurfaceMaterialInvalidTextureIndex;
      uint32_t heightTextureIndex = kSurfaceMaterialInvalidTextureIndex;
      uint32_t roughnessTextureIndex = kSurfaceMaterialInvalidTextureIndex;
      uint32_t metallicTextureIndex = kSurfaceMaterialInvalidTextureIndex;
      uint32_t emissiveColorTextureIndex = kSurfaceMaterialInvalidTextureIndex;
      uint32_t subsurfaceMaterialIndex = SURFACE_INDEX_INVALID;
      uint32_t subsurfaceTransmittanceTextureIndex = kSurfaceMaterialInvalidTextureIndex;
      uint32_t subsurfaceThicknessTextureIndex = kSurfaceMaterialInvalidTextureIndex;
      uint32_t subsurfaceSingleScatteringAlbedoTextureIndex = kSurfaceMaterialInvalidTextureIndex;

      float anisotropy;
      float emissiveIntensity;
      Vector4 albedoOpacityConstant;
      float roughnessConstant;
      float metallicConstant;
      Vector3 emissiveColorConstant;
      bool enableEmissive;
      bool thinFilmEnable = false;
      bool alphaIsThinFilmThickness = false;
      float thinFilmThicknessConstant = 0.0f;
      float displaceIn = 0.0f;
      float displaceOut = 0.0f;
      bool isUsingRaytracedRenderTarget = drawCallState.isUsingRaytracedRenderTarget;
      uint16_t samplerFeedbackStamp = SAMPLER_FEEDBACK_INVALID;

      Vector3 subsurfaceTransmittanceColor(0.0f, 0.0f, 0.0f);
      float subsurfaceMeasurementDistance = 0.0f;
      Vector3 subsurfaceSingleScatteringAlbedo(0.0f, 0.0f, 0.0f);
      float subsurfaceVolumetricAnisotropy = 0.0f;

      float subsurfaceRadiusScale = 0.0f;
      float subsurfaceMaxSampleRadius = 0.0f;

      bool ignoreAlphaChannel = false;

      constexpr Vector4 kWhiteModeAlbedo = Vector4(0.7f, 0.7f, 0.7f, 1.0f);

      const auto& opaqueMaterialData = renderMaterialData.getOpaqueMaterialData();

      if (RtxOptions::useWhiteMaterialMode()) {
        albedoOpacityConstant = kWhiteModeAlbedo;
        metallicConstant = 0.f;
        roughnessConstant = 1.f;
      } else {
        if (opaqueMaterialData.getAlbedoOpacityTexture().getManagedTexture() != nullptr) {
          samplerFeedbackStamp = opaqueMaterialData.getAlbedoOpacityTexture().getManagedTexture()->m_samplerFeedbackStamp;
        }

        trackTexture(opaqueMaterialData.getAlbedoOpacityTexture(), albedoOpacityTextureIndex, hasTexcoords, true, samplerFeedbackStamp);
        trackTexture(opaqueMaterialData.getRoughnessTexture(), roughnessTextureIndex, hasTexcoords, true, samplerFeedbackStamp);
        trackTexture(opaqueMaterialData.getMetallicTexture(), metallicTextureIndex, hasTexcoords, true, samplerFeedbackStamp);
        trackTexture(opaqueMaterialData.getSecondaryTexture(), secondaryTextureIndex, hasTexcoords, true, samplerFeedbackStamp);

        albedoOpacityConstant.xyz() = opaqueMaterialData.getAlbedoConstant();
        albedoOpacityConstant.w = opaqueMaterialData.getOpacityConstant();
        metallicConstant = opaqueMaterialData.getMetallicConstant();
        roughnessConstant = opaqueMaterialData.getRoughnessConstant();
      }

      trackTexture(opaqueMaterialData.getNormalTexture(), normalTextureIndex, hasTexcoords, true, samplerFeedbackStamp);
      trackTexture(opaqueMaterialData.getTangentTexture(), tangentTextureIndex, hasTexcoords, true, samplerFeedbackStamp);
      trackTexture(opaqueMaterialData.getHeightTexture(), heightTextureIndex, hasTexcoords, true, samplerFeedbackStamp);
      trackTexture(opaqueMaterialData.getEmissiveColorTexture(), emissiveColorTextureIndex, hasTexcoords, true, samplerFeedbackStamp);

      emissiveIntensity = opaqueMaterialData.getEmissiveIntensity() * RtxOptions::emissiveIntensity();
      emissiveColorConstant = opaqueMaterialData.getEmissiveColorConstant();
      enableEmissive = opaqueMaterialData.getEnableEmission();
      anisotropy = opaqueMaterialData.getAnisotropyConstant();
        
      thinFilmEnable = opaqueMaterialData.getEnableThinFilm();
      alphaIsThinFilmThickness = opaqueMaterialData.getAlphaIsThinFilmThickness();
      thinFilmThicknessConstant = opaqueMaterialData.getThinFilmThicknessConstant();
      displaceIn = opaqueMaterialData.getDisplaceIn();
      displaceOut = opaqueMaterialData.getDisplaceOut();

      ignoreAlphaChannel = opaqueMaterialData.getIgnoreAlphaChannel();

      subsurfaceMeasurementDistance = opaqueMaterialData.getSubsurfaceMeasurementDistance() * RtxOptions::SubsurfaceScattering::surfaceThicknessScale();

      const bool isSubsurfaceScatteringDiffusionProfile = opaqueMaterialData.getSubsurfaceDiffusionProfile();

      if ((RtxOptions::SubsurfaceScattering::enableThinOpaque()       && subsurfaceMeasurementDistance > 0.0f) ||
          (RtxOptions::SubsurfaceScattering::enableDiffusionProfile() && isSubsurfaceScatteringDiffusionProfile)) {

        subsurfaceTransmittanceColor = opaqueMaterialData.getSubsurfaceTransmittanceColor();
        subsurfaceVolumetricAnisotropy = opaqueMaterialData.getSubsurfaceVolumetricAnisotropy();

        if (isSubsurfaceScatteringDiffusionProfile) {
          // NOTE: reuse of the variable!
          subsurfaceSingleScatteringAlbedo = opaqueMaterialData.getSubsurfaceRadius(); 
          subsurfaceMaxSampleRadius = std::max(0.F, opaqueMaterialData.getSubsurfaceMaxSampleRadius());
          subsurfaceRadiusScale = std::max(opaqueMaterialData.getSubsurfaceRadiusScale(), 1e-5f);
          assert(subsurfaceRadiusScale > 0);
        } else /* if thin opaque */ {
          assert(subsurfaceMeasurementDistance > 0);

          subsurfaceSingleScatteringAlbedo = opaqueMaterialData.getSubsurfaceSingleScatteringAlbedo();
          subsurfaceMaxSampleRadius = 0;
          subsurfaceRadiusScale = -1;
          assert(subsurfaceRadiusScale < 0);  // if < 0, then shaders assume that
                                              // this material is not SubsurfaceScatter, but just SingleScatter
                                              // same here, but <0.F
        }

        if (RtxOptions::SubsurfaceScattering::enableTextureMaps()) {
          trackTexture(opaqueMaterialData.getSubsurfaceTransmittanceTexture(), subsurfaceTransmittanceTextureIndex, hasTexcoords, true, samplerFeedbackStamp);

          if (isSubsurfaceScatteringDiffusionProfile) {
            // NOTE: reuse of 'subsurfaceSingleScatteringAlbedoTextureIndex' variable!
            trackTexture(opaqueMaterialData.getSubsurfaceRadiusTexture(), subsurfaceSingleScatteringAlbedoTextureIndex, hasTexcoords, true, samplerFeedbackStamp);
          } else {
            trackTexture(opaqueMaterialData.getSubsurfaceSingleScatteringAlbedoTexture(), subsurfaceSingleScatteringAlbedoTextureIndex, hasTexcoords, true, samplerFeedbackStamp);
            trackTexture(opaqueMaterialData.getSubsurfaceThicknessTexture(), subsurfaceThicknessTextureIndex, hasTexcoords, true, samplerFeedbackStamp);
          }
        }

        const auto subsurfaceMaterial = RtSubsurfaceMaterial{
          subsurfaceTransmittanceTextureIndex,
          subsurfaceThicknessTextureIndex,
          subsurfaceSingleScatteringAlbedoTextureIndex,
          subsurfaceTransmittanceColor,
          subsurfaceMeasurementDistance,
          subsurfaceSingleScatteringAlbedo,
          subsurfaceVolumetricAnisotropy,
          subsurfaceRadiusScale,
          subsurfaceMaxSampleRadius,
        };
        subsurfaceMaterialIndex = m_surfaceMaterialExtensionCache.track(subsurfaceMaterial);
      }

      const RtOpaqueSurfaceMaterial opaqueSurfaceMaterial{
        albedoOpacityTextureIndex, normalTextureIndex,
        tangentTextureIndex, heightTextureIndex, roughnessTextureIndex,
        metallicTextureIndex, emissiveColorTextureIndex,
        anisotropy, emissiveIntensity,
        albedoOpacityConstant,
        roughnessConstant, metallicConstant,
        emissiveColorConstant, enableEmissive,
        ignoreAlphaChannel, thinFilmEnable, alphaIsThinFilmThickness,
        thinFilmThicknessConstant, samplerIndex, displaceIn, displaceOut, 
        subsurfaceMaterialIndex, isUsingRaytracedRenderTarget,
        samplerFeedbackStamp,
        secondaryTextureIndex
      };

      accumulateOpaqueMaterialAggregates(opaqueSurfaceMaterial);

      surfaceMaterial.emplace(opaqueSurfaceMaterial);
    } else if (renderMaterialDataType == MaterialDataType::Translucent) {
      surfaceMaterial.emplace(createTranslucentSurfaceMaterial(renderMaterialData.getTranslucentMaterialData(), samplerIndex, hasTexcoords));
    } else if (renderMaterialDataType == MaterialDataType::RayPortal) {
      const auto& rayPortalMaterialData = renderMaterialData.getRayPortalMaterialData();

      uint32_t maskTextureIndex = kSurfaceMaterialInvalidTextureIndex;
      trackTexture(rayPortalMaterialData.getMaskTexture(), maskTextureIndex, hasTexcoords, false);
      uint32_t maskTextureIndex2 = kSurfaceMaterialInvalidTextureIndex;
      trackTexture(rayPortalMaterialData.getMaskTexture2(), maskTextureIndex2, hasTexcoords, false);

      uint8_t rayPortalIndex = rayPortalMaterialData.getRayPortalIndex();
      float rotationSpeed = rayPortalMaterialData.getRotationSpeed();
      bool enableEmissive = rayPortalMaterialData.getEnableEmission();
      float emissiveIntensity = rayPortalMaterialData.getEmissiveIntensity() * RtxOptions::emissiveIntensity();

      const RtRayPortalSurfaceMaterial rayPortalSurfaceMaterial{
        maskTextureIndex, maskTextureIndex2, rayPortalIndex,
        rotationSpeed, enableEmissive, emissiveIntensity, samplerIndex, samplerIndex2
      };

      surfaceMaterial.emplace(rayPortalSurfaceMaterial);
    }

    assert(surfaceMaterial.has_value());
    assert(surfaceMaterial->validate());

    // Cache this
    const uint32_t index = m_surfaceMaterialCache.track(*surfaceMaterial);
    m_preCreationSurfaceMaterialMap[preCreationHash] = index;
    if (out_indexInCache) {
      *out_indexInCache = index;
    }
    return m_surfaceMaterialCache.at(index);
  }

  void SceneManager::accumulateOpaqueMaterialAggregates(const RtOpaqueSurfaceMaterial& opaqueMat) {
    if (opaqueMat.hasValidDisplacement()) {
      ++m_activePOMCount;
    }

    const uint32_t subsurfaceIdx = opaqueMat.getSubsurfaceMaterialIndex();
    if (subsurfaceIdx == SURFACE_INDEX_INVALID ||
        subsurfaceIdx >= m_surfaceMaterialExtensionCache.getTotalCount()) {
      return;
    }
    const RtSurfaceMaterial& extMat = m_surfaceMaterialExtensionCache.getObjectTable()[subsurfaceIdx];
    if (extMat.getType() != RtSurfaceMaterialType::Subsurface) {
      return;
    }
    // createSurfaceMaterial's SSS/thin-opaque branch encodes
    // isSubsurfaceScatteringDiffusionProfile into the sign of radiusScale:
    //   true  → radiusScale clamped to >= 1e-5f (strictly > 0) → SSS material
    //   false → radiusScale = -1 (strictly < 0)               → thin-opaque material
    // (the asserts there enforce the sign, and the GPU side in rtx_materials.h
    // branches on the same convention). The sign of radiusScale is therefore the
    // cached form of the SSS-vs-thin-opaque selector and can be inspected here
    // without re-reading the original MaterialData.
    const float radiusScale = extMat.getSubsurfaceMaterial().getSubsurfaceRadiusScale();
    if (radiusScale > 0.0f) {
      m_sssMaterialExist = true;
    } else if (radiusScale < 0.0f) {
      m_thinOpaqueMaterialExist = true;
    }
  }

  RtTranslucentSurfaceMaterial SceneManager::createTranslucentSurfaceMaterial(const TranslucentMaterialData& translucentMaterialData,
                                                                              uint32_t samplerIndex,
                                                                              bool hasTexcoords) {
    uint32_t normalTextureIndex = kSurfaceMaterialInvalidTextureIndex;
    uint32_t transmittanceTextureIndex = kSurfaceMaterialInvalidTextureIndex;
    uint32_t emissiveColorTextureIndex = kSurfaceMaterialInvalidTextureIndex;

    trackTexture(translucentMaterialData.getNormalTexture(), normalTextureIndex, hasTexcoords);
    trackTexture(translucentMaterialData.getTransmittanceTexture(), transmittanceTextureIndex, hasTexcoords);
    trackTexture(translucentMaterialData.getEmissiveColorTexture(), emissiveColorTextureIndex, hasTexcoords);

    return RtTranslucentSurfaceMaterial{
      normalTextureIndex,
      transmittanceTextureIndex,
      emissiveColorTextureIndex,
      translucentMaterialData.getRefractiveIndex() * std::clamp(TranslucentMaterialOptions::refractiveIndexScale(), 0.0f, 3.0f),
      translucentMaterialData.getTransmittanceMeasurementDistance(),
      translucentMaterialData.getTransmittanceColor(),
      translucentMaterialData.getEnableEmission(),
      translucentMaterialData.getEmissiveIntensity() * RtxOptions::emissiveIntensity(),
      translucentMaterialData.getEmissiveColorConstant(),
      translucentMaterialData.getEnableThinWalled(),
      translucentMaterialData.getThinWallThickness(),
      translucentMaterialData.getEnableDiffuseLayer(),
      samplerIndex
    };
  }

  Rc<DxvkSampler> SceneManager::getOrCreateExternalSampler() {
    if (m_externalSampler == nullptr) {
      auto s = DxvkSamplerCreateInfo {};
      {
        s.magFilter = VK_FILTER_LINEAR;
        s.minFilter = VK_FILTER_LINEAR;
        s.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        s.mipmapLodBias = 0.f;
        s.mipmapLodMin = 0.f;
        s.mipmapLodMax = 0.f;
        s.useAnisotropy = VK_FALSE;
        s.maxAnisotropy = 1.f;
        s.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        s.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        s.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        s.compareToDepth = VK_FALSE;
        s.compareOp = VK_COMPARE_OP_NEVER;
        s.borderColor = VkClearColorValue {};
        s.usePixelCoord = VK_FALSE;
      }
      m_externalSampler = m_device->createSampler(s);
    }

    return m_externalSampler;
  }

  void SceneManager::setExternalStartInMediumMaterial(const MaterialData& translucentMaterial) {
    assert(translucentMaterial.getType() == MaterialDataType::Translucent);

    const auto samplerIndex = trackSampler(getOrCreateExternalSampler());
    const auto surfaceMaterial = RtSurfaceMaterial(
      createTranslucentSurfaceMaterial(translucentMaterial.getTranslucentMaterialData(), samplerIndex, true));

    m_externalStartInMediumMaterialIndex_inCache = m_surfaceMaterialCache.track(surfaceMaterial);
  }

  void SceneManager::clearExternalStartInMediumMaterial() {
    m_externalStartInMediumMaterialIndex_inCache = UINT32_MAX;
  }

  void SceneManager::setStartInMediumMaterial(const MaterialData& translucentMaterial) {
    assert(translucentMaterial.getType() == MaterialDataType::Translucent);
    std::lock_guard lock { m_startInMediumMaterialMutex };
    m_pendingClearStartInMediumMaterial = false;
    m_pendingStartInMediumMaterial = translucentMaterial;
  }

  void SceneManager::clearStartInMediumMaterial() {
    std::lock_guard lock { m_startInMediumMaterialMutex };
    m_pendingStartInMediumMaterial.reset();
    m_pendingClearStartInMediumMaterial = true;
  }

  std::optional<XXH64_hash_t> SceneManager::findLegacyTextureHashByObjectPickingValue(uint32_t objectPickingValue) {
    std::lock_guard lock { m_drawCallMeta.mutex };

    auto tryFindIn = [](const std::unordered_map<ObjectPickingValue, DrawCallMetaInfo>& table, ObjectPickingValue toFind)
      -> std::optional<XXH64_hash_t> {
      auto found = table.find(toFind);
      if (found != table.end()) {
        const DrawCallMetaInfo& meta = found->second;
        if (meta.legacyTextureHash != kEmptyHash) {
          return meta.legacyTextureHash;
        }
      }
      return std::nullopt;
    };

    const int ticksToCheck[] = {
      m_drawCallMeta.ticker, // current tick
      (m_drawCallMeta.ticker + m_drawCallMeta.MaxTicks - 1) % m_drawCallMeta.MaxTicks, // prev tick
    };
    for (int tick : ticksToCheck) {
      if (m_drawCallMeta.ready[tick]) {
        if (auto h = tryFindIn(m_drawCallMeta.infos[tick], objectPickingValue)) {
          return h;
        }
      }
    }
    return std::nullopt;
  }

  std::vector<ObjectPickingValue> SceneManager::gatherObjectPickingValuesByTextureHash(XXH64_hash_t texHash) {
    std::lock_guard lock { m_drawCallMeta.mutex };
    assert(texHash != kEmptyHash);

    const int ticksToCheck[] = {
      m_drawCallMeta.ticker, // current tick
      (m_drawCallMeta.ticker + m_drawCallMeta.MaxTicks - 1) % m_drawCallMeta.MaxTicks, // prev tick
    };

    auto correspondingValues = std::vector<ObjectPickingValue> {};
    for (int tick : ticksToCheck) {
      if (m_drawCallMeta.ready[tick]) {
        for (const auto& [pickingValue, meta] : m_drawCallMeta.infos[tick]) {
          if (texHash == meta.legacyTextureHash) {
            correspondingValues.push_back(pickingValue);
          } else if (texHash == meta.legacyTextureHash2) {
            correspondingValues.push_back(pickingValue);
          }
        }
        break;
      }
    }
    return correspondingValues;
  }

  SceneManager::SamplerIndex SceneManager::trackSampler(Rc<DxvkSampler> sampler) {
    if (sampler == nullptr) {
      ONCE(Logger::warn("Found a null sampler. Fallback to linear-repeat"));
      sampler = patchSampler(
        VK_FILTER_LINEAR,
        VK_SAMPLER_ADDRESS_MODE_REPEAT,
        VK_SAMPLER_ADDRESS_MODE_REPEAT,
        VK_SAMPLER_ADDRESS_MODE_REPEAT,
        VkClearColorValue {});
    }
    return m_samplerCache.track(sampler);
  }

  Rc<DxvkSampler> SceneManager::patchSampler( const VkFilter filterMode,
                                              const VkSamplerAddressMode addressModeU,
                                              const VkSamplerAddressMode addressModeV,
                                              const VkSamplerAddressMode addressModeW,
                                              const VkClearColorValue borderColor) {
    auto& resourceManager = m_device->getCommon()->getResources();
    // Create a sampler to account for DLSS lod bias and any custom filtering overrides the user has set
    return resourceManager.getSampler(
      filterMode,
      VK_SAMPLER_MIPMAP_MODE_LINEAR,
      addressModeU,
      addressModeV,
      addressModeW,
      borderColor,
      getTotalMipBias(),
      RtxOptions::useAnisotropicFiltering());
  }

  void SceneManager::addLight(const D3DLIGHT9& light) {
    ScopedCpuProfileZone();
    // Attempt to convert the D3D9 light to RT

    std::optional<LightData> lightData = LightData::tryCreate(light);

    // Note: Skip adding this light if it is somehow malformed such that it could not be created.
    if (!lightData.has_value()) {
      return;
    }

    const RtLight rtLight = lightData->toRtLight();
    const std::vector<AssetReplacement>* pReplacements = m_pReplacer->getReplacementsForLight(rtLight.getInitialHash());

    // Build identity hash from the light's stable hash + position. Used by
    // both the replacement and the toggle-off cleanup paths below; must stay
    // in sync between them so they target the same RI.
    const XXH64_hash_t lightAssetHash = rtLight.getInitialHash();
    const Vector3 lightPos = rtLight.getPosition();
    const XXH64_hash_t lightIdHash = XXH64(&lightPos, sizeof(Vector3), lightAssetHash);

    if (pReplacements) {
      const Matrix4 lightTransform = LightUtils::getLightTransform(light);

      // Build identity hash from the light's stable hash + position
      const XXH64_hash_t lightAssetHash = rtLight.getInitialHash();
      const Vector3 lightPos = rtLight.getPosition();
      XXH64_hash_t lightIdHash = lightAssetHash;
      lightIdHash = XXH64(&lightPos, sizeof(Vector3), lightIdHash);

      const ReplacementInstance::LookupKey lightKey { lightIdHash, lightAssetHash, kEmptyHash, kEmptyHash, lightPos, lightTransform };
      ReplacementInstance* replacementInstance = m_drawCallTracker.findOrCreateReplacementInstance(lightKey);

      // Reinitialize the RI if the prim count doesn't match the replacement count.
      // This handles the transition from unreplaced (1 prim) to replaced (N prims)
      // when replacements finish loading asynchronously.
      if (replacementInstance->root.getUntyped() != nullptr &&
          replacementInstance->prims.size() != pReplacements->size()) {
        replacementInstance->clear();
      }

      // All lights in a light replacement are externally tracked, with their
      // lifecycle managed by the ReplacementInstance. This unifies root and sub-light
      // handling: create on first frame, update on subsequent frames.
      // TODO(TREX-1091) to implement meshes as light replacements, replace the below loop with a call to drawReplacements.
      const bool needsBBoxUpdate = replacementInstance->boundingBoxDirty;
      AxisAlignedBoundingBox litBBox;
      for (size_t i = 0; i < pReplacements->size(); i++) {
        const auto& replacement = (*pReplacements)[i];
        if (replacement.type == AssetReplacement::eLight && replacement.lightData.has_value()) {
          LightData replacementLight = replacement.lightData.value();

          // Merge the d3d9 light into replacements based on overrides.
          // Must happen before AABB extraction: some entries (e.g. the translated
          // original game light) have Unknown lightType and zero position/radius
          // until merged with the d3d9 light.
          replacementLight.merge(light);

          // Convert to runtime light
          RtLight rtReplacementLight = replacementLight.toRtLight();

          if (needsBBoxUpdate) {
            const Vector3 pos = rtReplacementLight.getPosition();
            float lightRadius = 0.f;
            if (rtReplacementLight.getType() == RtLightType::Sphere) {
              lightRadius = rtReplacementLight.getSphereLight().getRadius();
            }
            for (uint32_t j = 0; j < 3; j++) {
              litBBox.minPos[j] = std::min(litBBox.minPos[j], pos[j] - lightRadius);
              litBBox.maxPos[j] = std::max(litBBox.maxPos[j], pos[j] + lightRadius);
            }
          }

          // Transform the replacement light by the legacy light
          if (replacementLight.relativeTransform()) {
            rtReplacementLight.applyTransform(lightTransform);
          }

          RtLight* existingLight = (replacementInstance->prims.size() > i)
              ? replacementInstance->prims[i].getLight() : nullptr;
          if (existingLight != nullptr) {
            m_lightManager.updateExternallyTrackedLight(existingLight, rtReplacementLight);
          } else {
            RtLight* newLight = m_lightManager.createExternallyTrackedLight(rtReplacementLight);
            if (newLight != nullptr) {
              if (replacementInstance->prims.empty()) {
                replacementInstance->setup(PrimInstance(newLight, PrimInstance::Type::Light), pReplacements->size(), pReplacements);
              }
              newLight->getPrimInstanceOwner().setReplacementInstance(replacementInstance, i, newLight, PrimInstance::Type::Light);
              if (replacementInstance->root.getUntyped() == nullptr) {
                replacementInstance->root = PrimInstance(newLight, PrimInstance::Type::Light);
              }
            }
          }
        } else {
          assert(false); // We don't support meshes as children of lights yet.
        }
      }

      replacementInstance->frameLastSeen = m_device->getCurrentFrameId();
      replacementInstance->objectToWorld = lightTransform;
      if (needsBBoxUpdate) {
        if (litBBox.isValid()) {
          replacementInstance->lightBoundingBox = litBBox;
        }
        replacementInstance->boundingBoxDirty = false;
      }
    } else {
      // If this light previously had a replacement (e.g. enableReplacementLights
      // was just toggled off), the externally-tracked replacement lights are
      // still alive in LightManager -- they have no frame-age GC; their
      // lifecycle is owned by the RI. Tear down the RI so its prims get marked
      // for GC; otherwise the user sees the replacement lights and the original
      // game light rendering simultaneously until DrawCallTracker collects the
      // RI ~numFramesToKeepInstances frames later.
      if (ReplacementInstance* stale = m_drawCallTracker.findReplacementInstanceByIdentity(lightIdHash)) {
        if (stale->activeReplacements != nullptr) {
          stale->clear();
        }
      }

      // This is a light coming from the game directly, so use the appropriate API for filter rules
      m_lightManager.addGameLight(light.Type, rtLight);
    }
  }

  void SceneManager::prepareSceneData(Rc<RtxContext> ctx, DxvkBarrierSet& execBarriers) {
    ScopedGpuProfileZone(ctx, "Build Scene");

  #ifdef REMIX_DEVELOPMENT
    if (m_device->getCurrentFrameId() == RtxOptions::dumpAllInstancesOnFrame()) {
      // Print all RtInstances for debugging
      printAllRtInstances();
    }
  #endif

    // Needs to happen before garbageCollection to avoid destroying dynamic lights
    m_lightManager.dynamicLightMatching();

    garbageCollection();

    // Re-register buffers, textures, and materials for anti-culled instances.
    // These instances survived GC but the game didn't submit draw calls for them
    // this frame, so their per-frame table indices (buffer cache, material cache)
    // are stale. Without this, they would render with wrong geometry or textures.
    {
      const uint32_t currentFrameId = m_device->getCurrentFrameId();
      for (auto& ri : m_drawCallTracker.getReplacementInstances()) {
        if (ri->frameLastSeen == currentFrameId) {
          continue;
        }
        for (auto& prim : ri->prims) {
          RtInstance* instance = prim.getInstance();
          if (instance != nullptr) {
            preserveInstance(*instance);
          }
        }
      }
    }

    m_graphManager.applySceneOverrides(ctx);

    m_terrainBaker->prepareSceneData(ctx);

    auto& textureManager = m_device->getCommon()->getTextureManager();
    m_bindlessResourceManager.prepareSceneData(ctx, textureManager.getTextureTable(), getBufferTable(), getSamplerTable());

    // If there are no instances, we should do nothing!
    if (m_instanceManager.getActiveCount() == 0) {
      // Clear the ray portal data before the next frame
      m_rayPortalManager.clear();
      return;
    }

    m_rayPortalManager.prepareSceneData(ctx);
    // Note: only main camera needs to be teleportation corrected as only that one is used for ray tracing & denoising
    m_rayPortalManager.fixCameraInBetweenPortals(m_cameraManager.getCamera(CameraType::Main));
    m_rayPortalManager.fixCameraInBetweenPortals(m_cameraManager.getCamera(CameraType::ViewModel));
    m_rayPortalManager.createVirtualCameras(m_cameraManager);
    const bool didTeleport = m_rayPortalManager.detectTeleportationAndCorrectCameraHistory(
      m_cameraManager.getCamera(CameraType::Main),
      m_cameraManager.isCameraValid(CameraType::ViewModel) ? &m_cameraManager.getCamera(CameraType::ViewModel) : nullptr);

    {
      const uint32_t previousStartInMediumMaterialIndexInCache = m_lastResolvedStartInMediumMaterialIndexInCache;
      std::optional<MaterialData> pendingStartInMediumMaterial;
      uint32_t persistentStartInMediumMaterialIndexInCache = kInvalidMaterialCacheIndex;
      bool clearStartInMediumMaterial = false;
      {
        std::lock_guard lock { m_startInMediumMaterialMutex };
        clearStartInMediumMaterial = m_pendingClearStartInMediumMaterial;
        m_pendingClearStartInMediumMaterial = false;
        if (m_pendingStartInMediumMaterial.has_value()) {
          pendingStartInMediumMaterial = std::move(m_pendingStartInMediumMaterial);
          m_pendingStartInMediumMaterial.reset();
        }
      }

      if (clearStartInMediumMaterial) {
        m_persistentStartInMediumMaterial.reset();
      }

      if (pendingStartInMediumMaterial.has_value()) {
        m_persistentStartInMediumMaterial = std::move(pendingStartInMediumMaterial);
      }

      if (m_persistentStartInMediumMaterial.has_value()) {
        assert(m_persistentStartInMediumMaterial->getType() == MaterialDataType::Translucent);
        const auto samplerIndex = trackSampler(getOrCreateExternalSampler());
        const auto surfaceMaterial = RtSurfaceMaterial(
          createTranslucentSurfaceMaterial(m_persistentStartInMediumMaterial->getTranslucentMaterialData(), samplerIndex, true));
        persistentStartInMediumMaterialIndexInCache = m_surfaceMaterialCache.track(surfaceMaterial);
      }

      m_startInMediumMaterialIndex_inCache = m_externalStartInMediumMaterialIndex_inCache != kInvalidMaterialCacheIndex
        ? m_externalStartInMediumMaterialIndex_inCache
        : persistentStartInMediumMaterialIndexInCache != kInvalidMaterialCacheIndex
          ? persistentStartInMediumMaterialIndexInCache
          : m_fogStartInMediumMaterialIndex_inCache;

      if (m_startInMediumMaterialIndex_inCache != kInvalidMaterialCacheIndex &&
          m_startInMediumMaterialIndex_inCache >= m_surfaceMaterialCache.getObjectTable().size()) {
        Logger::debug(str::format(
          "[RTX] Ignoring stale camera medium material cache index ", m_startInMediumMaterialIndex_inCache,
          " on frame ", m_device->getCurrentFrameId(),
          "; surfaceMaterialCacheSize=", m_surfaceMaterialCache.getObjectTable().size()));
        m_startInMediumMaterialIndex_inCache = kInvalidMaterialCacheIndex;
      }

      if (m_startInMediumMaterialIndex_inCache != previousStartInMediumMaterialIndexInCache) {
        Logger::debug(str::format(
          "[RTX] View history invalidated due to camera medium change on frame ", m_device->getCurrentFrameId(),
          ": previousStartInMediumInCache=", previousStartInMediumMaterialIndexInCache,
          ", currentStartInMediumInCache=", m_startInMediumMaterialIndex_inCache,
          ", cleared=", clearStartInMediumMaterial ? "true" : "false",
          ", pendingSet=", pendingStartInMediumMaterial.has_value() ? "true" : "false"));
        m_cameraManager.getMainCamera().invalidateViewHistory(m_device->getCurrentFrameId());
      }
      m_lastResolvedStartInMediumMaterialIndexInCache = m_startInMediumMaterialIndex_inCache;
    }

    if (m_cameraManager.isCameraCutThisFrame()) {
      // Ignore camera cut events on teleportation so we don't flush the caches
      if (!didTeleport) {
        Logger::info(str::format("Camera cut detected on frame ", m_device->getCurrentFrameId()));
        m_enqueueDelayedClear = true;
      }
    }

    // Initialize/remove opacity micromap manager
    if (RtxOptions::getEnableOpacityMicromap()) {
      if (!m_opacityMicromapManager.get() || 
          // Reset the manager on camera cuts
          m_enqueueDelayedClear) {
        if (m_opacityMicromapManager.get())
          m_instanceManager.removeEventHandler(m_opacityMicromapManager.get());

        m_opacityMicromapManager = std::make_unique<OpacityMicromapManager>(m_device);
        m_instanceManager.addEventHandler(m_opacityMicromapManager->getInstanceEventHandler());
        // Seed candidates with instances that were added before the event handler was registered
        m_opacityMicromapManager->seedCandidates(m_instanceManager.getInstanceTable());
        Logger::info("[RTX] Opacity Micromap: enabled");
      }
    } else if (m_opacityMicromapManager.get()) {
      m_accelManager.invalidateOpacityMicromapBindings();
      m_instanceManager.notifySceneChanged();
      m_instanceManager.removeEventHandler(m_opacityMicromapManager.get());
      m_opacityMicromapManager = nullptr;
      Logger::info("[RTX] Opacity Micromap: disabled");
    }

    RtxParticleSystemManager& particles = m_device->getCommon()->metaParticleSystem();
    particles.simulate(ctx.ptr());

    m_instanceManager.findPortalForVirtualInstances(m_cameraManager, m_rayPortalManager);
    m_instanceManager.createViewModelInstances(ctx, m_cameraManager, m_rayPortalManager);
    m_instanceManager.createPlayerModelVirtualInstances(ctx, m_cameraManager, m_rayPortalManager);

    m_accelManager.mergeInstancesIntoBlas(ctx, execBarriers, textureManager.getTextureTable(), m_cameraManager, m_instanceManager, m_opacityMicromapManager.get());

    // Call on the other managers to prepare their GPU data for the current scene
    m_accelManager.prepareSceneData(ctx, execBarriers, m_instanceManager);
    m_lightManager.prepareSceneData(ctx, m_cameraManager);

    // Upload surface material buffer BEFORE the GPU culling dispatch so the
    // compute shader can copy template material entries to per-instance slots.
    // For PointInstancer duplicate entries we skip writeGPUData and advance past
    // the gap — the GPU shader will fill those slots.
    //
    // When the scene is unchanged (fast-skip path in mergeInstancesIntoBlas),
    // the surface order and material data are normally identical to last frame.
    // Baked terrain materials are updated independently of acceleration-structure
    // scene generation, so keep their surface-material upload live.
    const bool startInMediumStateChanged = m_startInMediumMaterialIndex_inCache != m_lastUploadedStartInMediumMaterialIndexInCache;
    const bool updateSurfaceMaterials =
      !m_accelManager.wasSceneUnchangedThisFrame() ||
      TerrainBaker::needsTerrainBaking() ||
      startInMediumStateChanged;
    if (updateSurfaceMaterials) {
      DxvkBufferCreateInfo matInfo;
      matInfo.usage = VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT
        | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR
        | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
      matInfo.stages = VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR
        | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
      matInfo.access = VK_ACCESS_TRANSFER_WRITE_BIT;

      if (m_surfaceMaterialCache.getTotalCount() > 0) {
        ScopedGpuProfileZone(ctx, "updateSurfaceMaterials");
        // Note: We duplicate the materials in the buffer so we don't have to do pointer chasing on the GPU
        size_t surfaceMaterialsGPUSize = m_accelManager.getSurfaceCount() * kSurfaceMaterialGPUSize;
        const uint32_t expectedSurfaceMaterialEntries = m_accelManager.getSurfaceCount()
          + (m_startInMediumMaterialIndex_inCache != kInvalidMaterialCacheIndex ? 1u : 0u);
        if (m_startInMediumMaterialIndex_inCache != kInvalidMaterialCacheIndex) {
          surfaceMaterialsGPUSize += kSurfaceMaterialGPUSize;
        }

        matInfo.size = align(surfaceMaterialsGPUSize, kBufferAlignment);
        if (m_surfaceMaterialBuffer == nullptr || matInfo.size > m_surfaceMaterialBuffer->info().size) {
          m_surfaceMaterialBuffer = m_device->createBuffer(matInfo, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, DxvkMemoryStats::Category::RTXBuffer, "Surface Material Buffer");
        }

        std::size_t dataOffset = 0;
        uint32_t surfaceIndex = 0;
        std::vector<unsigned char> surfaceMaterialsGPUData(surfaceMaterialsGPUSize);
        for (auto&& pInstance : m_accelManager.getOrderedInstances()) {
          // For PointInstancer duplicates (entries beyond the template), skip
          // writeGPUData — the GPU culling shader copies the template material.
          const auto& surf = pInstance->surface;
          if (surf.instancesToObject != nullptr &&
              surf.surfaceIndexOfFirstInstance != SIZE_MAX &&
              surfaceIndex > surf.surfaceIndexOfFirstInstance) {
            dataOffset += kSurfaceMaterialGPUSize;
          } else {
            assert(surf.surfaceMaterialIndex < m_surfaceMaterialCache.getObjectTable().size());
            auto&& surfaceMaterial = m_surfaceMaterialCache.getObjectTable()[surf.surfaceMaterialIndex];
            surfaceMaterial.writeGPUData(surfaceMaterialsGPUData.data(), dataOffset, surfaceIndex);
          }
          surfaceIndex++;
        }

        if (m_startInMediumMaterialIndex_inCache != kInvalidMaterialCacheIndex) {
          auto&& surfaceMaterial = m_surfaceMaterialCache.getObjectTable()[m_startInMediumMaterialIndex_inCache];
          surfaceMaterial.writeGPUData(surfaceMaterialsGPUData.data(), dataOffset, surfaceIndex);
          m_startInMediumMaterialIndex = surfaceIndex;
          surfaceIndex++;
        } else {
          m_startInMediumMaterialIndex = SURFACE_INDEX_INVALID;
        }

        assert(surfaceIndex == expectedSurfaceMaterialEntries);
        assert(dataOffset == surfaceMaterialsGPUSize);
        assert(surfaceMaterialsGPUData.size() == surfaceMaterialsGPUSize);
        m_lastUploadedStartInMediumMaterialIndexInCache = m_startInMediumMaterialIndex_inCache;

        ctx->writeToBuffer(m_surfaceMaterialBuffer, 0, surfaceMaterialsGPUData.size(), surfaceMaterialsGPUData.data());
      }
    } else {
      m_startInMediumMaterialIndex = m_startInMediumMaterialIndex_inCache != kInvalidMaterialCacheIndex
        ? m_accelManager.getSurfaceCount()
        : SURFACE_INDEX_INVALID;
    }

    // GPU-driven PointInstancer culling: overwrites visible instance placeholders
    // in m_vkInstanceBuffer with proper transforms and masks, copies per-instance
    // surface and material data from templates. Must run after prepareSceneData
    // (which uploads placeholders) and before buildTlas.
    m_accelManager.dispatchPointInstancerCulling(ctx, m_cameraManager, m_surfaceMaterialBuffer);

    // Build the TLAS
    m_accelManager.buildTlas(ctx);

    // Todo: These updates require a lot of temporary buffer allocations and memcopies, ideally we should memcpy directly into a mapped pointer provided by Vulkan,
    // but we have to create a buffer to pass to DXVK's updateBuffer for now.
    // Skip when scene is unchanged — buffers from last frame are still valid.
    if (!m_accelManager.wasSceneUnchangedThisFrame()) {
      // Allocate the instance buffer and copy its contents from host to device memory
      DxvkBufferCreateInfo info;
      info.usage = VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
      info.stages = VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR;
      info.access = VK_ACCESS_TRANSFER_WRITE_BIT;

      // Surface Material Extension Buffer
      if (m_surfaceMaterialExtensionCache.getTotalCount() > 0) {
        ScopedGpuProfileZone(ctx, "updateSurfaceMaterialExtensions");
        const auto surfaceMaterialExtensionsGPUSize = m_surfaceMaterialExtensionCache.getTotalCount() * kSurfaceMaterialGPUSize;

        info.size = align(surfaceMaterialExtensionsGPUSize, kBufferAlignment);
        info.usage |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        if (m_surfaceMaterialExtensionBuffer == nullptr || info.size > m_surfaceMaterialExtensionBuffer->info().size) {
          m_surfaceMaterialExtensionBuffer = m_device->createBuffer(info, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, DxvkMemoryStats::Category::RTXBuffer, "Surface Material Extension Buffer");
        }

        std::size_t dataOffset = 0;
        std::vector<unsigned char> surfaceMaterialExtensionsGPUData(surfaceMaterialExtensionsGPUSize);

        uint32_t surfaceIndex = 0;
        for (auto&& surfaceMaterialExtension : m_surfaceMaterialExtensionCache.getObjectTable()) {
          surfaceMaterialExtension.writeGPUData(surfaceMaterialExtensionsGPUData.data(), dataOffset, surfaceIndex);
          surfaceIndex++;
        }

        assert(dataOffset == surfaceMaterialExtensionsGPUSize);
        assert(surfaceMaterialExtensionsGPUData.size() == surfaceMaterialExtensionsGPUSize);

        ctx->writeToBuffer(m_surfaceMaterialExtensionBuffer, 0, surfaceMaterialExtensionsGPUData.size(), surfaceMaterialExtensionsGPUData.data());
      }

      // Volume Material buffer
      if (m_volumeMaterialCache.getTotalCount() > 0) {
        ScopedGpuProfileZone(ctx, "updateVolumeMaterials");
        const auto volumeMaterialsGPUSize = m_volumeMaterialCache.getTotalCount() * kVolumeMaterialGPUSize;

        info.size = align(volumeMaterialsGPUSize, kBufferAlignment);
        info.usage |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        if (m_volumeMaterialBuffer == nullptr || info.size > m_volumeMaterialBuffer->info().size) {
          m_volumeMaterialBuffer = m_device->createBuffer(info, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, DxvkMemoryStats::Category::RTXBuffer, "Volume Material Buffer");
        }

        std::size_t dataOffset = 0;
        std::vector<unsigned char> volumeMaterialsGPUData(volumeMaterialsGPUSize);

        for (auto&& volumeMaterial : m_volumeMaterialCache.getObjectTable()) {
          volumeMaterial.writeGPUData(volumeMaterialsGPUData.data(), dataOffset);
        }

        assert(dataOffset == volumeMaterialsGPUSize);
        assert(volumeMaterialsGPUData.size() == volumeMaterialsGPUSize);

        ctx->writeToBuffer(m_volumeMaterialBuffer, 0, volumeMaterialsGPUData.size(), volumeMaterialsGPUData.data());
      }
    }

    ctx->emitMemoryBarrier(0,
      VK_PIPELINE_STAGE_TRANSFER_BIT,
      VK_ACCESS_TRANSFER_WRITE_BIT,
      VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
      VK_ACCESS_SHADER_READ_BIT);

    // Update stats
    m_device->statCounters().setCtr(DxvkStatCounter::RtxBlasCount, AccelManager::getBlasCount());
    m_device->statCounters().setCtr(DxvkStatCounter::RtxBufferCount, m_bufferCache.getActiveCount());
    m_device->statCounters().setCtr(DxvkStatCounter::RtxTextureCount, textureManager.getTextureTable().size());
    m_device->statCounters().setCtr(DxvkStatCounter::RtxInstanceCount, m_instanceManager.getActiveCount());
    m_device->statCounters().setCtr(DxvkStatCounter::RtxSurfaceMaterialCount, m_surfaceMaterialCache.getActiveCount());
    m_device->statCounters().setCtr(DxvkStatCounter::RtxSurfaceMaterialExtensionCount, m_surfaceMaterialExtensionCache.getActiveCount());
    m_device->statCounters().setCtr(DxvkStatCounter::RtxVolumeMaterialCount, m_volumeMaterialCache.getActiveCount());
    m_device->statCounters().setCtr(DxvkStatCounter::RtxLightCount, m_lightManager.getActiveCount());
    m_device->statCounters().setCtr(DxvkStatCounter::RtxSamplers, m_samplerCache.getActiveCount());

    auto capturer = m_device->getCommon()->capturer();
    if (m_device->getCurrentFrameId() == m_beginUsdExportFrameNum) {
      capturer->triggerNewCapture();
    }
    capturer->step(ctx, ctx->getCommonObjects()->getLastKnownWindowHandle());

    // Clear the ray portal data before the next frame
    m_rayPortalManager.clear();

    // Check Anti-Culling Support:
    // When the game doesn't set up the View Matrix, we must disable Anti-Culling to prevent visual corruption.
    m_isAntiCullingSupported = (getCamera().getViewToWorld() != Matrix4d());
  }

  static_assert(std::is_same_v< decltype(RtSurface::objectPickingValue), ObjectPickingValue>);

  void SceneManager::submitExternalDraw(Rc<DxvkContext> ctx, ExternalDrawState&& state) {
    Rc<DxvkSampler> externalSampler = getOrCreateExternalSampler();

    {
      state.drawCall.materialData.samplers[0] = externalSampler;
      state.drawCall.materialData.samplers[1] = externalSampler;
    }
    {
      const RtCamera& rtCamera = ctx->getCommonObjects()->getSceneManager().getCameraManager()
        .getCamera(state.cameraType);
      state.drawCall.transformData.worldToView = Matrix4 { rtCamera.getWorldToView() };
      state.drawCall.transformData.viewToProjection = Matrix4 { rtCamera.getViewToProjection() };
      state.drawCall.transformData.objectToView = state.drawCall.transformData.worldToView * state.drawCall.transformData.objectToWorld;
    }

    if (!state.gpuInstancingTransforms.empty()) {
      state.drawCall.transformData.instancesToObject =
        std::make_shared<const std::vector<Matrix4>>(std::move(state.gpuInstancingTransforms));
    }

    const auto& submeshes = m_pReplacer->accessExternalMesh(state.mesh);

    const XXH64_hash_t identityHash = state.computeExternalDrawIdentityHash();
    const XXH64_hash_t spatialMapHash = spatialMapHashForExternalDrawMesh(state.mesh);
    const Matrix4& xform = state.drawCall.transformData.objectToWorld;
    const XXH64_hash_t matHash = state.drawCall.materialData.getHash();
    const Vector3 worldPos = xform[3].xyz();

    const ReplacementInstance::LookupKey externalKey { identityHash, spatialMapHash, matHash, kEmptyHash, worldPos, xform };
    ReplacementInstance* replacementInstance = m_drawCallTracker.findOrCreateReplacementInstance(externalKey);

    AxisAlignedBoundingBox geometryBBox;

    for (size_t i = 0; i < submeshes.size(); i++) {
      state.drawCall.geometryData = submeshes[i];
      state.drawCall.geometryData.cullMode = state.doubleSided ? VK_CULL_MODE_NONE : VK_CULL_MODE_BACK_BIT;

      const MaterialData* material = m_pReplacer->accessExternalMaterial(submeshes[i].externalMaterial);
      if (material != nullptr) {
        state.drawCall.materialData.setHashOverride(material->getHash());
      } 

      const RtxParticleSystemDesc* pParticles = nullptr;
      if (state.optionalParticleDesc.has_value()) {
        pParticles = &state.optionalParticleDesc.value();
      }

      RtInstance* existingInstance = (replacementInstance->prims.size() > i)
          ? replacementInstance->prims[i].getInstance() : nullptr;

      RtInstance* instance = processDrawCallState(ctx, state.drawCall,
          material != nullptr ? MaterialData(*material) : LegacyMaterialData().as<OpaqueMaterialData>(),
          existingInstance, pParticles);

      if (instance != nullptr) {
        if (replacementInstance->root.getUntyped() == nullptr) {
          replacementInstance->setup(PrimInstance(instance, PrimInstance::Type::Instance), submeshes.size(), nullptr);
        }
        if (replacementInstance->prims.size() > i &&
            replacementInstance->prims[i].getUntyped() != instance) {
          instance->getPrimInstanceOwner().setReplacementInstance(replacementInstance, i, instance,
              PrimInstance::Type::Instance);
        }
      }

      geometryBBox.unionWith(submeshes[i].boundingBox);
    }

    replacementInstance->frameLastSeen = m_device->getCurrentFrameId();

    if (geometryBBox.isValid()) {
      replacementInstance->geometryBoundingBox = geometryBBox;
      replacementInstance->objectToWorld = xform;
    }
  }

  void SceneManager::destroyExternalMesh(remixapi_MeshHandle handle) {
    if (handle) {
      m_drawCallTracker.removeReplacementInstancesWithSpatialMapHash(
          spatialMapHashForExternalDrawMesh(handle));
      m_pReplacer->destroyExternalMesh(handle);
    }
  }

  namespace {
    bool ifTrue_andThenSetFalse(std::atomic_bool& atomicBool) {
      bool expected = true;
      if (atomicBool.compare_exchange_strong(expected, false)) {
        return true;
      }
      return false;
    }
  } // unnamed

  void SceneManager::requestTextureVramFree() {
    m_forceFreeTextureMemory.store(true);
  }

  void SceneManager::requestVramCompaction() {
    m_forceFreeUnusedDxvkAllocatorChunks.store(true);
  }

  void SceneManager::manageTextureVram() {
    bool freeUnused = false;
    bool freeTextures = false;
    {
      if (ifTrue_andThenSetFalse(m_forceFreeTextureMemory)) {
        freeTextures = true;
        freeUnused = true;
      }
      if (ifTrue_andThenSetFalse(m_forceFreeUnusedDxvkAllocatorChunks)) {
        freeUnused = true;
      }
    }

    if (freeTextures) {
      m_device->getCommon()->getTextureManager().clear();

      if (m_opacityMicromapManager) {
        m_opacityMicromapManager->clear();
      }
    }

    if (freeUnused) {
      // DXVK doesnt free chunks for us by default (its high water mark) so force release some memory back to the system here.
      m_device->getCommon()->memoryManager().freeUnusedChunks();
    }
  }

  void SceneManager::printAllRtInstances() {
  #ifdef REMIX_DEVELOPMENT
    
    const auto& instances = m_instanceManager.getInstanceTable();
    Logger::info(str::format("=== Printing all RtInstances (", instances.size(), " total) ==="));
    
    for (size_t i = 0; i < instances.size(); ++i) {
      const RtInstance* instance = instances[i];
      if (instance != nullptr) {
        Logger::info(str::format("Instance ", i, ":"));
        instance->printDebugInfo();
      } else {
        Logger::warn(str::format("Instance ", i, ": nullptr"));
      }
    }
    
    Logger::info("=== End RtInstances Print ===");
  #endif
  }

  void SceneManager::trackReplacementMaterialHash(XXH64_hash_t materialHash) {
    if (materialHash != kEmptyHash) {
      m_currentFrameReplacementMaterialHashes[materialHash]++;
    }
  }

  bool SceneManager::isReplacementMaterialHashUsedThisFrame(XXH64_hash_t materialHash) const {
    return m_currentFrameReplacementMaterialHashes.find(materialHash) != m_currentFrameReplacementMaterialHashes.end();
  }

  uint32_t SceneManager::getReplacementMaterialHashUsageCount(XXH64_hash_t materialHash) const {
    auto it = m_currentFrameReplacementMaterialHashes.find(materialHash);
    return (it != m_currentFrameReplacementMaterialHashes.end()) ? it->second : 0;
  }

  void SceneManager::clearFrameReplacementMaterialHashes() {
    m_currentFrameReplacementMaterialHashes.clear();
  }

  void SceneManager::trackMeshHash(XXH64_hash_t meshHash) {
    if (meshHash != kEmptyHash) {
      m_currentFrameMeshHashes[meshHash]++;
    }
  }

  bool SceneManager::isMeshHashUsedThisFrame(XXH64_hash_t meshHash) const {
    return m_currentFrameMeshHashes.find(meshHash) != m_currentFrameMeshHashes.end();
  }

  uint32_t SceneManager::getMeshHashUsageCount(XXH64_hash_t meshHash) const {
    auto it = m_currentFrameMeshHashes.find(meshHash);
    return (it != m_currentFrameMeshHashes.end()) ? it->second : 0;
  }

  void SceneManager::clearFrameMeshHashes() {
    m_currentFrameMeshHashes.clear();
  }

}  // namespace dxvk
