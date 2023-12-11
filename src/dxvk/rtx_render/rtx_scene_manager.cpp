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

#include "rtx_asset_replacer.h"
#include "rtx_scene_manager.h"
#include "rtx_opacity_micromap_manager.h"
#include "dxvk_device.h"
#include "dxvk_context.h"
#include "dxvk_buffer.h"
#include "rtx_context.h"
#include "rtx_options.h"
#include "rtx_terrain_baker.h"

#include <assert.h>

#include "../d3d9/d3d9_state.h"
#include "vulkan/vulkan_core.h"

#include "rtx_game_capturer.h"
#include "rtx_matrix_helpers.h"
#include "rtx_intersection_test.h"

#include "dxvk_scoped_annotation.h"
#include "rtx_lights_data.h"
#include "rtx_light_utils.h"

namespace dxvk {

  SceneManager::SceneManager(DxvkDevice* device)
    : CommonDeviceObject(device)
    , m_instanceManager(device, this)
    , m_accelManager(device)
    , m_lightManager(device)
    , m_rayPortalManager(device, this)
    , m_drawCallCache(device)
    , m_bindlessResourceManager(device)
    , m_volumeManager(device)
    , m_pReplacer(new AssetReplacer())
    , m_terrainBaker(new TerrainBaker())
    , m_cameraManager(device)
    , m_startTime(std::chrono::steady_clock::now()) {
    InstanceEventHandler instanceEvents(this);
    instanceEvents.onInstanceAddedCallback = [this](const RtInstance& instance) { onInstanceAdded(instance); };
    instanceEvents.onInstanceUpdatedCallback = [this](RtInstance& instance, const RtSurfaceMaterial& material, bool hasTransformChanged, bool hasVerticesChanged) { onInstanceUpdated(instance, material, hasTransformChanged, hasVerticesChanged); };
    instanceEvents.onInstanceDestroyedCallback = [this](const RtInstance& instance) { onInstanceDestroyed(instance); };
    m_instanceManager.addEventHandler(instanceEvents);
    
    if (env::getEnvVar("DXVK_RTX_CAPTURE_ENABLE_ON_FRAME") != "") {
      m_beginUsdExportFrameNum = stoul(env::getEnvVar("DXVK_RTX_CAPTURE_ENABLE_ON_FRAME"));
    }
    if (env::getEnvVar("DXVK_DENOISER_NRD_FRAME_TIME_MS") != "") {
      m_useFixedFrameTime = true;
    }
  }

  SceneManager::~SceneManager() {
  }

  bool SceneManager::areReplacementsLoaded() const {
    return m_pReplacer->areReplacementsLoaded();
  }

  bool SceneManager::areReplacementsLoading() const {
    return m_pReplacer->areReplacementsLoading();
  }

  const std::string SceneManager::getReplacementStatus() const {
    return m_pReplacer->getReplacementStatus();
  }

  // Returns wall time between start of app and current time.
  uint64_t SceneManager::getGameTimeSinceStartMS() {
    // Used in testing
    if (m_useFixedFrameTime) {
      const double deltaTimeMS = 1000.0 / 60.0; // Assume 60 fps

      return static_cast<uint64_t>(static_cast<double>(m_device->getCurrentFrameId()) * deltaTimeMS);
    }

    // TODO(TREX-1004) find a way to 'pause' this when a game is paused.
    // Note: steady_clock used here rather than system_clock as on Windows at least it uses a higher precision time source
    // (QueryPerformanceCounter rather than GetSystemTimePreciseAsFileTime), and additionally it is monotonic which is better
    // for this sort of game-based timekeeping (we don't care about NTP adjustments or other things that'd cause discontinuities).
    const auto currTime = std::chrono::steady_clock::now();
    const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(currTime - m_startTime);

    return elapsedMs.count();
  }

  void SceneManager::initialize(Rc<DxvkContext> ctx) {
    ScopedCpuProfileZone();
    m_pReplacer->initialize(ctx);

    auto& textureManager = m_device->getCommon()->getTextureManager();
    textureManager.initialize(ctx);
  }

  Vector3 SceneManager::getSceneUp() {
    return RtxOptions::Get()->zUp() ? Vector3(0.f, 0.f, 1.f) : Vector3(0.f, 1.f, 0.f);
  }

  Vector3 SceneManager::getSceneForward() {
    return RtxOptions::Get()->zUp() ? Vector3(0.f, 1.f, 0.f) : Vector3(0.f, 0.f, 1.f);
  }

  Vector3 SceneManager::calculateSceneRight() {
    return cross(getSceneForward(), getSceneUp());
  }

  Vector3 SceneManager::worldToSceneOrientedVector(const Vector3& worldVector) {
    return RtxOptions::Get()->zUp() ? worldVector : Vector3(worldVector.x, worldVector.z, worldVector.y);
  }

  Vector3 SceneManager::sceneToWorldOrientedVector(const Vector3& sceneVector) {
    // Same transform applies to and from
    return worldToSceneOrientedVector(sceneVector);
  }

  float SceneManager::getTotalMipBias() {
    auto& resourceManager = m_device->getCommon()->getResources();

    const bool temporalUpscaling = RtxOptions::Get()->isDLSSEnabled() || RtxOptions::Get()->isTAAEnabled();
    const float totalUpscaleMipBias = temporalUpscaling ? (log2(resourceManager.getUpscaleRatio()) + RtxOptions::Get()->upscalingMipBias()) : 0.0f;
    return totalUpscaleMipBias + RtxOptions::Get()->getNativeMipBias();
  }

  void SceneManager::clear(Rc<DxvkContext> ctx, bool needWfi) {
    ScopedCpuProfileZone();

    auto& textureManager = m_device->getCommon()->getTextureManager();

    // Only clear once after the scene disappears, to avoid adding a WFI on every frame through clear().
    if (needWfi) {
      if (ctx.ptr())
        ctx->flushCommandList();
      textureManager.synchronize(true);
      m_device->waitForIdle();
    }

    // We still need to clear caches even if the scene wasn't rendered
    m_bufferCache.clear();
    m_surfaceMaterialCache.clear();
    m_surfaceMaterialExtensionCache.clear();
    m_volumeMaterialCache.clear();
    
    // Called before instance manager's clear, so that it resets all tracked instances in Opacity Micromap manager at once
    if (m_opacityMicromapManager.get())
      m_opacityMicromapManager->clear();
    
    m_instanceManager.clear();
    m_lightManager.clear();
    m_rayPortalManager.clear();
    m_drawCallCache.clear();
    textureManager.clear();

    m_previousFrameSceneAvailable = false;
  }

  void SceneManager::garbageCollection() {
    ScopedCpuProfileZone();

    const size_t oldestFrame = m_device->getCurrentFrameId() - RtxOptions::Get()->numFramesToKeepGeometryData();
    auto blasEntryGarbageCollection = [&](auto& iter, auto& entries) -> void {
      if (iter->second.frameLastTouched < oldestFrame) {
        onSceneObjectDestroyed(iter->second);
        iter = entries.erase(iter);
      } else {
        ++iter;
      }
    };

    // Garbage collection for BLAS/Scene objects
    //
    // When anti-culling is enabled, we need to check if any instances are outside frustum. Because in such
    // case the life of the instances will be extended and we need to keep the BLAS as well.
    if (!RtxOptions::AntiCulling::Object::enable()) {
      auto& entries = m_drawCallCache.getEntries();
      if (m_device->getCurrentFrameId() > RtxOptions::Get()->numFramesToKeepGeometryData()) {
        for (auto& iter = entries.begin(); iter != entries.end(); ) {
          blasEntryGarbageCollection(iter, entries);
        }
      }
    }
    else { // Implement anti-culling BLAS/Scene object GC
      fast_unordered_cache<const RtInstance*> outsideFrustumInstancesCache;

      auto& entries = m_drawCallCache.getEntries();
      for (auto& iter = entries.begin(); iter != entries.end();) {
        bool isAllInstancesInCurrentBlasInsideFrustum = true;
        for (const RtInstance* instance : iter->second.getLinkedInstances()) {
          const Matrix4 objectToView = getCamera().getWorldToView(false) * instance->getTransform();

          bool isInsideFrustum = true;
          if (RtxOptions::Get()->needsMeshBoundingBox()) {
            const AxisAlignedBoundingBox& boundingBox = instance->getBlas()->input.getGeometryData().boundingBox;
            if (RtxOptions::AntiCulling::Object::enableHighPrecisionAntiCulling()) {
              isInsideFrustum = boundingBoxIntersectsFrustumSAT(
                getCamera(),
                boundingBox.minPos,
                boundingBox.maxPos,
                objectToView,
                RtxOptions::AntiCulling::Object::enableInfinityFarFrustum());
            } else {
              isInsideFrustum = boundingBoxIntersectsFrustum(getCamera().getFrustum(), boundingBox.minPos, boundingBox.maxPos, objectToView);
            }
          }
          else {
            // Fallback to check object center under view space
            isInsideFrustum = getCamera().getFrustum().CheckSphere(float3(objectToView[3][0], objectToView[3][1], objectToView[3][2]), 0);
          }

          // Only GC the objects inside the frustum to anti-frustum culling, this could cause significant performance impact
          // For the objects which can't be handled well with this algorithm, we will need game specific hash to force keeping them
          if (isInsideFrustum && !instance->testCategoryFlags(InstanceCategories::IgnoreAntiCulling)) {
            instance->markAsInsideFrustum();
          } else {
            instance->markAsOutsideFrustum();
            isAllInstancesInCurrentBlasInsideFrustum = false;

            // Anti-Culling GC extension:
            // Eliminate duplicated instances that are outside of the game frustum.
            // This is used to handle cases:
            //   1. The game frustum is different to our frustum
            //   2. The game culling method is NOT frustum culling

            const XXH64_hash_t antiCullingHash = instance->calculateAntiCullingHash();

            auto it = outsideFrustumInstancesCache.find(antiCullingHash);
            if (it == outsideFrustumInstancesCache.end()) {
              // No duplication, just cache the current instance
              outsideFrustumInstancesCache[antiCullingHash] = instance;
            } else {
              const RtInstance* cachedInstance = it->second;
              if (instance->getId() != cachedInstance->getId()) {
                // Only keep the instance that is latest updated
                if (instance->getFrameLastUpdated() < cachedInstance->getFrameLastUpdated()) {
                  instance->markAsInsideFrustum();
                } else {
                  cachedInstance->markAsInsideFrustum();
                  it->second = instance;
                }
              }
            }
          }
        }

        // If all instances in current BLAS are inside the frustum, then use original GC logic to recycle BLAS Objects
        if (isAllInstancesInCurrentBlasInsideFrustum &&
            m_device->getCurrentFrameId() > RtxOptions::Get()->numFramesToKeepGeometryData()) {
          blasEntryGarbageCollection(iter, entries);
        } else { // If any instances are outside of the frustum in current BLAS, we need to keep the entity
          ++iter;
        }
      }
    }

    // Perform GC on the other managers
    auto& textureManager = m_device->getCommon()->getTextureManager();
    textureManager.garbageCollection();
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

    const size_t vertexStride = (input.isVertexDataInterleaved() && input.areFormatsGpuFriendly()) ? input.positionBuffer.stride() : RtxGeometryUtils::computeOptimalVertexStride(input);

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
        DxvkBufferCreateInfo info = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
        info.usage = VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
        info.stages = VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR;
        info.access = VK_ACCESS_TRANSFER_WRITE_BIT;

        info.size = align(output.indexCount * indexStride, CACHE_LINE_SIZE);
        output.indexCacheBuffer = m_device->createBuffer(info, memoryProperty, DxvkMemoryStats::Category::RTXAccelerationStructure);

        if (!RtxGeometryUtils::cacheIndexDataOnGPU(ctx, input, output)) {
          ONCE(Logger::err("processGeometryInfo: failed to cache index data on GPU"));
          return ObjectCacheState::kInvalid;
        }

        output.indexBuffer = RaytraceBuffer(DxvkBufferSlice(output.indexCacheBuffer), 0, indexStride, indexBufferType);

        info.size = align(vertexBufferSize, CACHE_LINE_SIZE);
        output.historyBuffer[0] = m_device->createBuffer(info, memoryProperty, DxvkMemoryStats::Category::RTXAccelerationStructure);

        RtxGeometryUtils::cacheVertexDataOnGPU(ctx, input, output);

        break;
      }
      case ObjectCacheState::kUpdateBVH: {
        bool invalidateHistory = false;

        // Stride changed, so we must recreate the previous buffer and use identical data
        if (output.historyBuffer[0]->info().size != align(vertexStride * input.vertexCount, CACHE_LINE_SIZE)) {
          auto desc = output.historyBuffer[0]->info();
          desc.size = align(vertexStride * input.vertexCount, CACHE_LINE_SIZE);
          output.historyBuffer[0] = m_device->createBuffer(desc, memoryProperty, DxvkMemoryStats::Category::RTXAccelerationStructure);

          // Invalidate the current buffer
          output.historyBuffer[1] = nullptr;

          // Mark this object for realignment
          invalidateHistory = true;
        }

        // Use the previous updates vertex data for previous position lookup
        std::swap(output.historyBuffer[0], output.historyBuffer[1]);

        if (output.historyBuffer[0].ptr() == nullptr) {
          // First frame this object has been dynamic need to allocate a 2nd frame of data to preserve history.
          output.historyBuffer[0] = m_device->createBuffer(output.historyBuffer[1]->info(), memoryProperty, DxvkMemoryStats::Category::RTXAccelerationStructure);
        } 

        RtxGeometryUtils::cacheVertexDataOnGPU(ctx, input, output);

        // Sometimes, we need to invalidate history, do that here by copying the current buffer to the previous..
        if (invalidateHistory) {
          ctx->copyBuffer(output.historyBuffer[1], 0, output.historyBuffer[0], 0, output.historyBuffer[1]->info().size);
        }

        // Assign the previous buffer using the last slice (copy most params from the position, just change buffer)
        output.previousPositionBuffer = RaytraceBuffer(DxvkBufferSlice(output.historyBuffer[1], 0, output.positionBuffer.length()), output.positionBuffer.offsetFromSlice(), output.positionBuffer.stride(), output.positionBuffer.vertexFormat());
        break;
      }
    }

    // Update buffers in the cache
    updateBufferCache(output);

    // Finalize our modified geometry data to the output
    inOutGeometry = output;

    return result;
  }


  void SceneManager::onFrameEnd(Rc<DxvkContext> ctx) {
    ScopedCpuProfileZone();
    if (m_enqueueDelayedClear) {
      clear(ctx, true);
      m_enqueueDelayedClear = false;
    }

    m_cameraManager.onFrameEnd();
    m_instanceManager.onFrameEnd();
    m_previousFrameSceneAvailable = true;

    m_bufferCache.clear();

    m_terrainBaker->onFrameEnd(ctx);
    
    m_activePOMCount = 0;
  }

  void SceneManager::onFrameEndNoRTX() {
    m_cameraManager.onFrameEnd();
  }

  std::unordered_set<XXH64_hash_t> uniqueHashes;


  void SceneManager::submitDrawState(Rc<DxvkContext> ctx, const DrawCallState& input, const MaterialData* overrideMaterialData) {
    ScopedCpuProfileZone();
    const uint32_t kBufferCacheLimit = kSurfaceInvalidBufferIndex - 10; // Limit for unique buffers minus some padding
    if (m_bufferCache.getTotalCount() >= kBufferCacheLimit && m_bufferCache.getActiveCount() >= kBufferCacheLimit) {
      ONCE(Logger::info("[RTX-Compatibility-Info] This application is pushing more unique buffers than is currently supported - some objects may not raytrace."));
      return;
    }

    if (m_fog.mode == D3DFOG_NONE && input.getFogState().mode != D3DFOG_NONE) {
      m_fog = input.getFogState();
    }

    // Get Material and Mesh replacements
    // NOTE: Next refactor we move this into a material manager
    std::optional<MaterialData> replacementMaterial {};
    if (overrideMaterialData == nullptr) {
      MaterialData* pReplacementMaterial = m_pReplacer->getReplacementMaterial(input.getMaterialData().getHash());
      if (pReplacementMaterial != nullptr) {
        // Make a copy
        replacementMaterial.emplace(MaterialData(*pReplacementMaterial));
        // merge in the input material from game
        replacementMaterial->mergeLegacyMaterial(input.getMaterialData());
        // mark material as replacement so we know how to handle sampler state
        replacementMaterial->setReplacement();
        // bind as a material override for this draw
        overrideMaterialData = &replacementMaterial.value();
      }
    }

    const XXH64_hash_t activeReplacementHash = input.getHash(RtxOptions::Get()->GeometryAssetHashRule);
    std::vector<AssetReplacement>* pReplacements = m_pReplacer->getReplacementsForMesh(activeReplacementHash);

    // TODO (REMIX-656): Remove this once we can transition content to new hash
    if ((RtxOptions::Get()->GeometryHashGenerationRule & rules::LegacyAssetHash0) == rules::LegacyAssetHash0) {
      if (!pReplacements) {
        const XXH64_hash_t legacyHash = input.getHashLegacy(rules::LegacyAssetHash0);
        pReplacements = m_pReplacer->getReplacementsForMesh(legacyHash);
        if (RtxOptions::Get()->logLegacyHashReplacementMatches() && pReplacements && uniqueHashes.find(legacyHash) == uniqueHashes.end()) {
          uniqueHashes.insert(legacyHash);
          Logger::info(str::format("[Legacy-Hash-Replacement] Found a mesh referenced from legacyHash0: ", std::hex, legacyHash, ", new hash: ", std::hex, activeReplacementHash));
        }
      }
    }

    if ((RtxOptions::Get()->GeometryHashGenerationRule & rules::LegacyAssetHash1) == rules::LegacyAssetHash1) {
      if (!pReplacements) {
        const XXH64_hash_t legacyHash = input.getHashLegacy(rules::LegacyAssetHash1);
        pReplacements = m_pReplacer->getReplacementsForMesh(legacyHash);
        if (RtxOptions::Get()->logLegacyHashReplacementMatches() && pReplacements && uniqueHashes.find(legacyHash) == uniqueHashes.end()) {
          uniqueHashes.insert(legacyHash);
          Logger::info(str::format("[Legacy-Hash-Replacement] Found a mesh referenced from legacyHash1: ", std::hex, legacyHash, ", new hash: ", std::hex, activeReplacementHash));
        }
      }
    }

    // Check if a Ray Portal override is needed
    std::optional<MaterialData> rayPortalMaterialData {};
    size_t rayPortalTextureIndex;

    if (RtxOptions::Get()->getRayPortalTextureIndex(input.getMaterialData().getHash(), rayPortalTextureIndex)) {
      assert(rayPortalTextureIndex < maxRayPortalCount);
      assert(rayPortalTextureIndex < std::numeric_limits<uint8_t>::max());

      // Mask texture is required for Portal
      const bool materialHasMaskTexture = input.getMaterialData().getColorTexture2().isValid();

      if (materialHasMaskTexture) {
        const TextureRef& texture2 = input.getMaterialData().getColorTexture2();

        if (overrideMaterialData == nullptr) {
          // Note: Color texture used as mask texture for the Ray Portal
          rayPortalMaterialData.emplace(RayPortalMaterialData { input.getMaterialData().getColorTexture(), texture2, static_cast<uint8_t>(rayPortalTextureIndex), 1, 1, 0, 0.f,true, 1.f, 0, 0, 0 });

          // Note: A bit dirty but since we use a pointer to the material data in processDrawCallState, we need a pointer to this locally created one on the
          // stack in a place that doesn't go out of scope without actually allocating any heap memory.
          overrideMaterialData = &*rayPortalMaterialData;
        }
      }
    }

    // Detect meshes that would have unstable hashes due to the vertex hash using vertex data from a shared vertex buffer.
    // TODO: Once the vertex hash only uses vertices referenced by the index buffer, this should be removed.
    const bool highlightUnsafeAnchor = RtxOptions::Get()->getHighlightUnsafeAnchorModeEnabled() &&
        input.getGeometryData().indexBuffer.defined() && input.getGeometryData().vertexCount > input.getGeometryData().indexCount;
    if (highlightUnsafeAnchor) {
      static MaterialData sHighlightMaterialData(OpaqueMaterialData(TextureRef(), TextureRef(), TextureRef(), TextureRef(), TextureRef(), TextureRef(), TextureRef(), TextureRef(), TextureRef(), TextureRef(),
          0.f, 1.f, Vector3(0.2f, 0.2f, 0.2f), 1.0f, 0.1f, 0.1f, Vector3(0.46f, 0.26f, 0.31f), true, 1, 1, 0, false, false, 200.f, true, false, BlendType::kAlpha, false, AlphaTestType::kAlways, 0, 0.0f, Vector3(), 0.0f, Vector3(), 0.0f,
          lss::Mdl::Filter::Nearest, lss::Mdl::WrapMode::Repeat, lss::Mdl::WrapMode::Repeat));
      overrideMaterialData = &sHighlightMaterialData;
    }

    uint64_t instanceId = UINT64_MAX;
    if (pReplacements != nullptr) {
      instanceId = drawReplacements(ctx, &input, pReplacements, overrideMaterialData);
    } else {
      instanceId = processDrawCallState(ctx, input, overrideMaterialData);
    }
  }

  void SceneManager::createEffectLight(Rc<DxvkContext> ctx, const DrawCallState& input, const RtInstance* instance) {
    const float effectLightIntensity = RtxOptions::Get()->getEffectLightIntensity();
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
    Vector4 worldPos = getCamera().getViewToWorld(false) * renderingPos;

    RtLightShaping shaping;
    shaping.enabled = false;
    const float lightRadius = std::max(RtxOptions::Get()->getEffectLightRadius(), 1e-3f);
    const float surfaceArea = 4.f * kPi * lightRadius * lightRadius;
    const float radianceFactor = 1e5f * effectLightIntensity / surfaceArea;
    const Vector3 lightPosition = Vector3(worldPos.x, worldPos.y, worldPos.z);
    Vector3 lightRadiance;
    if (RtxOptions::Get()->getEffectLightPlasmaBall()) {
      // Todo: Make these options more configurable via config options.
      const double timeMilliseconds = static_cast<double>(getGameTimeSinceStartMS());
      const double animationPhase = sin(timeMilliseconds * 0.006) * 0.5 + 0.5;
      lightRadiance = lerp(Vector3(1.f, 0.921f, 0.738f), Vector3(1.f, 0.521f, 0.238f), animationPhase) * radianceFactor;
    } else {
      const D3DCOLORVALUE originalColor = input.getMaterialData().getLegacyMaterial().Diffuse;
      lightRadiance = Vector3(originalColor.r, originalColor.g, originalColor.b) * radianceFactor;
    }

    RtLight rtLight(RtSphereLight(lightPosition, lightRadiance, lightRadius, shaping));
    rtLight.isDynamic = true;

    m_lightManager.addLight(rtLight, input, RtLightAntiCullingType::MeshReplacement);
  }

  uint64_t SceneManager::drawReplacements(Rc<DxvkContext> ctx, const DrawCallState* input, const std::vector<AssetReplacement>* pReplacements, const MaterialData* overrideMaterialData) {
    ScopedCpuProfileZone();
    uint64_t rootInstanceId = UINT64_MAX;
    // Detect replacements of meshes that would have unstable hashes due to the vertex hash using vertex data from a shared vertex buffer.
    // TODO: Once the vertex hash only uses vertices referenced by the index buffer, this should be removed.
    const bool highlightUnsafeReplacement = RtxOptions::Get()->getHighlightUnsafeReplacementModeEnabled() &&
        input->getGeometryData().indexBuffer.defined() && input->getGeometryData().vertexCount > input->getGeometryData().indexCount;
    if (!pReplacements->empty() && (*pReplacements)[0].includeOriginal) {
      DrawCallState newDrawCallState(*input);
      newDrawCallState.categories = (*pReplacements)[0].categories.applyCategoryFlags(newDrawCallState.categories);
      rootInstanceId = processDrawCallState(ctx, newDrawCallState, overrideMaterialData);
    }
    for (auto&& replacement : *pReplacements) {
      if (replacement.type == AssetReplacement::eMesh) {
        DrawCallTransforms transforms = input->getTransformData();
        
        transforms.objectToWorld = transforms.objectToWorld * replacement.replacementToObject;
        transforms.objectToView = transforms.objectToView * replacement.replacementToObject;
        
        // Mesh replacements dont support these.
        transforms.textureTransform = Matrix4();
        transforms.texgenMode = TexGenMode::None;

        DrawCallState newDrawCallState(*input);
        newDrawCallState.geometryData = replacement.geometry->data; // Note: Geometry Data replaced
        newDrawCallState.transformData = transforms;
        newDrawCallState.categories = replacement.categories.applyCategoryFlags(newDrawCallState.categories);

        // Note: Material Data replaced if a replacement is specified in the Mesh Replacement
        if (replacement.materialData != nullptr) {
          overrideMaterialData = replacement.materialData;
        }
        if (highlightUnsafeReplacement) {
          static MaterialData sHighlightMaterialData(OpaqueMaterialData(TextureRef(), TextureRef(), TextureRef(), TextureRef(), TextureRef(), TextureRef(), TextureRef(), TextureRef(), TextureRef(), TextureRef(),
              0.f, 1.f, Vector3(0.2f, 0.2f, 0.2f), 1.f, 0.1f, 0.1f, Vector3(1.f, 0.f, 0.f), true, 1, 1, 0, false, false, 200.f, true, false, BlendType::kAlpha, false, AlphaTestType::kAlways, 0, 0.0f, Vector3(), 0.0f, Vector3(), 0.0f,
              lss::Mdl::Filter::Nearest, lss::Mdl::WrapMode::Repeat, lss::Mdl::WrapMode::Repeat));
          if (getGameTimeSinceStartMS() / 200 % 2 == 0) {
            overrideMaterialData = &sHighlightMaterialData;
          }
        }
        uint64_t instanceId = processDrawCallState(ctx, newDrawCallState, overrideMaterialData);
        if (rootInstanceId == UINT64_MAX) {
          rootInstanceId = instanceId;
        }
      }
    }
    for (auto&& replacement : *pReplacements) {
      if (replacement.type == AssetReplacement::eLight) {
        if (rootInstanceId == UINT64_MAX) {
          // TODO(TREX-1141) if we refactor instancing to depend on the pre-replacement drawcall instead
          // of the fully processed draw call, we can remove this requirement.
          Logger::err(str::format(
              "Light prims anchored to a mesh replacement must also include actual meshes.  mesh hash: ",
              std::hex, input->getHash(RtxOptions::Get()->GeometryHashGenerationRule)
          ));
          break;
        }
        if (replacement.lightData.has_value()) {
          RtLight localLight = replacement.lightData->toRtLight();
          localLight.setRootInstanceId(rootInstanceId);
          localLight.applyTransform(input->getTransformData().objectToWorld);
          m_lightManager.addLight(localLight, *input, RtLightAntiCullingType::MeshReplacement);
        }
      }
    }

    return rootInstanceId;
  }

  void SceneManager::clearFogState() {
    m_fog = FogState();
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

  SceneManager::ObjectCacheState SceneManager::onSceneObjectAdded(Rc<DxvkContext> ctx, const DrawCallState& drawCallState, BlasEntry* pBlas) {
    // This is a new object.
    ObjectCacheState result = processGeometryInfo<true>(ctx, drawCallState, pBlas->modifiedGeometryData);
    
    assert(result == ObjectCacheState::KBuildBVH);

    pBlas->frameLastUpdated = m_device->getCurrentFrameId();

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

    if (result == ObjectCacheState::kUpdateBVH)
      pBlas->frameLastUpdated = m_device->getCurrentFrameId();
    
    pBlas->clearMaterialCache();
    pBlas->input = drawCallState; // cache the draw state for the next time.
    return result;
  }
  
  void SceneManager::onSceneObjectDestroyed(const BlasEntry& blas) {
    for (const RtInstance* instance : blas.getLinkedInstances()) {
      instance->markForGarbageCollection();
      instance->markAsUnlinkedFromBlasEntryForGarbageCollection();
    }
  }

  void SceneManager::onInstanceAdded(const RtInstance& instance) {
    BlasEntry* pBlas = instance.getBlas();
    if (pBlas != nullptr) {
      pBlas->linkInstance(&instance);
    }
  }

  void SceneManager::onInstanceUpdated(RtInstance& instance, const RtSurfaceMaterial& material, const bool hasTransformChanged, const bool hasVerticesChanged) {
    auto capturer = m_device->getCommon()->capturer();
    if (hasTransformChanged) {
      capturer->setInstanceUpdateFlag(instance, GameCapturer::InstFlag::XformUpdate);
    }

    if (hasVerticesChanged) {
      capturer->setInstanceUpdateFlag(instance, GameCapturer::InstFlag::PositionsUpdate);
      capturer->setInstanceUpdateFlag(instance, GameCapturer::InstFlag::NormalsUpdate);
    }

    // This is a ray portal!
    if (material.getType() == RtSurfaceMaterialType::RayPortal) {
      BlasEntry* pBlas = instance.getBlas();
      m_rayPortalManager.processRayPortalData(instance, material);
    }
  }

  void SceneManager::onInstanceDestroyed(const RtInstance& instance) {
    BlasEntry* pBlas = instance.getBlas();
    // Some BLAS were cleared in the SceneManager::garbageCollection().
    // When a BLAS is destroyed, all instances that linked to it will be automatically unlinked. In such case we don't need to
    // call onInstanceDestroyed to double unlink the instances.
    // Note: This case often happens when BLAS are destroyed faster than instances. (e.g. numFramesToKeepGeometryData >= numFramesToKeepInstances)
    if (pBlas != nullptr && !instance.isUnlinkedForGC()) {
      pBlas->unlinkInstance(&instance);
    }
  }

  // Helper to populate the texture cache with this resource (and patch sampler if required for texture)
  void SceneManager::trackTexture(Rc<DxvkContext> ctx, TextureRef inputTexture, uint32_t& textureIndex, bool hasTexcoords, bool allowAsync) {
    // If no texcoords, no need to bind the texture
    if (!hasTexcoords) {
      ONCE(Logger::info(str::format("[RTX-Compatibility-Info] Trying to bind a texture to a mesh without UVs.  Was this intended?")));
      return;
    }

    auto& textureManager = m_device->getCommon()->getTextureManager();
    textureManager.addTexture(ctx, inputTexture, allowAsync, textureIndex);
  }

  uint64_t SceneManager::processDrawCallState(Rc<DxvkContext> ctx, const DrawCallState& drawCallState, const MaterialData* overrideMaterialData) {
    ScopedCpuProfileZone();
    const bool usingOverrideMaterial = overrideMaterialData != nullptr;
    const MaterialData& renderMaterialData =
      usingOverrideMaterial ? *overrideMaterialData : drawCallState.getMaterialData();
    if (renderMaterialData.getIgnored()) {
      return UINT64_MAX;
    }
    ObjectCacheState result = ObjectCacheState::kInvalid;
    BlasEntry* pBlas = nullptr;
    if (m_drawCallCache.get(drawCallState, &pBlas) == DrawCallCache::CacheState::kExisted) {
      result = onSceneObjectUpdated(ctx, drawCallState, pBlas);
    } else {
      result = onSceneObjectAdded(ctx, drawCallState, pBlas);
    }

    // Update the input state, so we always have a reference to the original draw call state
    pBlas->frameLastTouched = m_device->getCurrentFrameId();

    if (drawCallState.getSkinningState().numBones > 0 &&
        drawCallState.getGeometryData().numBonesPerVertex > 0 &&
        (result == ObjectCacheState::KBuildBVH || result == ObjectCacheState::kUpdateBVH)) {
      m_device->getCommon()->metaGeometryUtils().dispatchSkinning(drawCallState, pBlas->modifiedGeometryData);
      pBlas->frameLastUpdated = pBlas->frameLastTouched;
    }
    
    assert(pBlas != nullptr);
    assert(result != ObjectCacheState::kInvalid);

    // Note: Use either the specified override Material Data or the original draw calls state's Material Data to create a Surface Material if no override is specified
    const auto renderMaterialDataType = renderMaterialData.getType();
    std::optional<RtSurfaceMaterial> surfaceMaterial{};

    const bool hasTexcoords = drawCallState.hasTextureCoordinates();

    // We're going to use this to create a modified sampler for replacement textures.
    // Legacy and replacement materials should follow same filtering but due to lack of override capability per texture
    // legacy textures use original sampler to stay true to the original intent while replacements use more advanced filtering
    // for better quality by default.
    Rc<DxvkSampler> originalSampler = drawCallState.getMaterialData().getSampler(); // convenience variable for debug
    Rc<DxvkSampler> sampler = originalSampler;
    const bool isLegacyMaterial = (renderMaterialDataType == MaterialDataType::Legacy);
    // If the original sampler if valid and the new rendering material is not legacy type
    // go ahead with patching and maybe merging the sampler states
    if(originalSampler != nullptr && !isLegacyMaterial) {
      DxvkSamplerCreateInfo samplerInfo = originalSampler->info(); // Use sampler create info struct as convenience
      // Only merge prior to patching if this is a replacement material
      if(renderMaterialData.isReplacement()) { 
        renderMaterialData.populateSamplerInfo(samplerInfo);
      }
      sampler = patchSampler(samplerInfo.magFilter,
                             samplerInfo.addressModeU, samplerInfo.addressModeV, samplerInfo.addressModeW,
                             samplerInfo.borderColor);
    }
    uint32_t samplerIndex = trackSampler(sampler);

    if (isLegacyMaterial || renderMaterialDataType == MaterialDataType::Opaque) {
      uint32_t albedoOpacityTextureIndex = kSurfaceMaterialInvalidTextureIndex;
      uint32_t normalTextureIndex = kSurfaceMaterialInvalidTextureIndex;
      uint32_t tangentTextureIndex = kSurfaceMaterialInvalidTextureIndex;
      uint32_t heightTextureIndex = kSurfaceMaterialInvalidTextureIndex;
      uint32_t roughnessTextureIndex = kSurfaceMaterialInvalidTextureIndex;
      uint32_t metallicTextureIndex = kSurfaceMaterialInvalidTextureIndex;
      uint32_t emissiveColorTextureIndex = kSurfaceMaterialInvalidTextureIndex;
      uint32_t subsurfaceMaterialIndex = kSurfaceMaterialInvalidTextureIndex;
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
      float displaceIn = 1.0f;

      Vector3 subsurfaceTransmittanceColor(0.0f, 0.0f, 0.0f);
      float subsurfaceMeasurementDistance = 0.0f;
      Vector3 subsurfaceSingleScatteringAlbedo(0.0f, 0.0f, 0.0f);
      float subsurfaceVolumetricAnisotropy = 0.0f;

      constexpr Vector4 kWhiteModeAlbedo = Vector4(0.7f, 0.7f, 0.7f, 1.0f);

      if (renderMaterialDataType == MaterialDataType::Legacy) {
        // Todo: In the future this path will construct a LegacySurfaceMaterial, for now it simply uses
        // the OpaqueSurfaceMaterial path until we have a more established legacy material model in place.

        const auto& legacyMaterialData = renderMaterialData.getLegacyMaterialData();

        const LegacyMaterialDefaults& defaults = RtxOptions::Get()->legacyMaterial;
        anisotropy = defaults.anisotropy();
        emissiveIntensity = defaults.emissiveIntensity();
        albedoOpacityConstant = Vector4(defaults.albedoConstant(), defaults.opacityConstant());
        roughnessConstant = defaults.roughnessConstant();
        metallicConstant = defaults.metallicConstant();

        // Override these for legacy materials
        emissiveColorConstant = defaults.emissiveColorConstant();
        enableEmissive = defaults.enableEmissive();

        if (RtxOptions::Get()->getWhiteMaterialModeEnabled()) {
          albedoOpacityConstant = kWhiteModeAlbedo;
          metallicConstant = 0.f;
          roughnessConstant = 1.f;
        } else {
          if (defaults.useAlbedoTextureIfPresent()) {
            // NOTE: Do not patch original sampler to preserve filtering behavior of the legacy material
            trackTexture(ctx, legacyMaterialData.getColorTexture(), albedoOpacityTextureIndex, hasTexcoords);
          }
        }

        if (RtxOptions::Get()->getHighlightLegacyModeEnabled()) {
          enableEmissive = true;
          // Flash every 20 frames, bright
          emissiveIntensity = (sin((float) m_device->getCurrentFrameId()/20) + 1.f) * 2.f;
          emissiveColorConstant = Vector3(1, 0, 0); // Red
        }
        // Todo: Incorporate this and the color texture into emissive conditionally
        // emissiveColorTextureIndex != kSurfaceMaterialInvalidTextureIndex ? 100.0f

        thinFilmEnable = defaults.enableThinFilm();
        alphaIsThinFilmThickness = defaults.alphaIsThinFilmThickness();
        thinFilmThicknessConstant = defaults.thinFilmThicknessConstant();
      } else if (renderMaterialDataType == MaterialDataType::Opaque) {
        const auto& opaqueMaterialData = renderMaterialData.getOpaqueMaterialData();

        if (RtxOptions::Get()->getWhiteMaterialModeEnabled()) {
          albedoOpacityConstant = kWhiteModeAlbedo;
          metallicConstant = 0.f;
          roughnessConstant = 1.f;
        } else {
          trackTexture(ctx, opaqueMaterialData.getAlbedoOpacityTexture(), albedoOpacityTextureIndex, hasTexcoords);
          trackTexture(ctx, opaqueMaterialData.getRoughnessTexture(), roughnessTextureIndex, hasTexcoords);
          trackTexture(ctx, opaqueMaterialData.getMetallicTexture(), metallicTextureIndex, hasTexcoords);

          albedoOpacityConstant.xyz() = opaqueMaterialData.getAlbedoConstant();
          albedoOpacityConstant.w = opaqueMaterialData.getOpacityConstant();
          metallicConstant = opaqueMaterialData.getMetallicConstant();
          roughnessConstant = opaqueMaterialData.getRoughnessConstant();
        }

        trackTexture(ctx, opaqueMaterialData.getNormalTexture(), normalTextureIndex, hasTexcoords);
        trackTexture(ctx, opaqueMaterialData.getTangentTexture(), tangentTextureIndex, hasTexcoords);
        trackTexture(ctx, opaqueMaterialData.getHeightTexture(), heightTextureIndex, hasTexcoords);
        trackTexture(ctx, opaqueMaterialData.getEmissiveColorTexture(), emissiveColorTextureIndex, hasTexcoords);

        emissiveIntensity = opaqueMaterialData.getEmissiveIntensity();
        emissiveColorConstant = opaqueMaterialData.getEmissiveColorConstant();
        enableEmissive = opaqueMaterialData.getEnableEmission();
        anisotropy = opaqueMaterialData.getAnisotropyConstant();
        
        thinFilmEnable = opaqueMaterialData.getEnableThinFilm();
        alphaIsThinFilmThickness = opaqueMaterialData.getAlphaIsThinFilmThickness();
        thinFilmThicknessConstant = opaqueMaterialData.getThinFilmThicknessConstant();
        displaceIn = opaqueMaterialData.getDisplaceIn();

        if (heightTextureIndex != kSurfaceMaterialInvalidTextureIndex && displaceIn > 0.0f) {
          ++m_activePOMCount;
        }

        subsurfaceMeasurementDistance = opaqueMaterialData.getSubsurfaceMeasurementDistance() * RtxOptions::SubsurfaceScattering::surfaceThicknessScale();

        if (RtxOptions::SubsurfaceScattering::enableTextureMaps()) {
          trackTexture(ctx, opaqueMaterialData.getSubsurfaceThicknessTexture(), subsurfaceThicknessTextureIndex, hasTexcoords);
        }

        if (RtxOptions::SubsurfaceScattering::enableThinOpaque() &&
            (subsurfaceMeasurementDistance > 0.0f || subsurfaceTransmittanceTextureIndex != kSurfaceMaterialInvalidTextureIndex)) {
          subsurfaceTransmittanceColor = opaqueMaterialData.getSubsurfaceTransmittanceColor();
          subsurfaceSingleScatteringAlbedo = opaqueMaterialData.getSubsurfaceSingleScatteringAlbedo();
          subsurfaceVolumetricAnisotropy = opaqueMaterialData.getSubsurfaceVolumetricAnisotropy();

          if (RtxOptions::SubsurfaceScattering::enableTextureMaps()) {
            trackTexture(ctx, opaqueMaterialData.getSubsurfaceTransmittanceTexture(), subsurfaceTransmittanceTextureIndex, hasTexcoords);
            trackTexture(ctx, opaqueMaterialData.getSubsurfaceSingleScatteringAlbedoTexture(), subsurfaceSingleScatteringAlbedoTextureIndex, hasTexcoords);
          }

          const RtSubsurfaceMaterial subsurfaceMaterial(
            subsurfaceTransmittanceTextureIndex, subsurfaceThicknessTextureIndex, subsurfaceSingleScatteringAlbedoTextureIndex,
            subsurfaceTransmittanceColor, subsurfaceMeasurementDistance, subsurfaceSingleScatteringAlbedo, subsurfaceVolumetricAnisotropy);
          subsurfaceMaterialIndex = m_surfaceMaterialExtensionCache.track(subsurfaceMaterial);
        }
      }

      const RtOpaqueSurfaceMaterial opaqueSurfaceMaterial{
        albedoOpacityTextureIndex, normalTextureIndex,
        tangentTextureIndex, heightTextureIndex, roughnessTextureIndex,
        metallicTextureIndex, emissiveColorTextureIndex,
        anisotropy, emissiveIntensity,
        albedoOpacityConstant,
        roughnessConstant, metallicConstant,
        emissiveColorConstant, enableEmissive,
        thinFilmEnable, alphaIsThinFilmThickness,
        thinFilmThicknessConstant, samplerIndex, displaceIn,
        subsurfaceMaterialIndex
      };

      surfaceMaterial.emplace(opaqueSurfaceMaterial);
    } else if (renderMaterialDataType == MaterialDataType::Translucent) {
      const auto& translucentMaterialData = renderMaterialData.getTranslucentMaterialData();

      uint32_t normalTextureIndex = kSurfaceMaterialInvalidTextureIndex;
      uint32_t transmittanceTextureIndex = kSurfaceMaterialInvalidTextureIndex;
      uint32_t emissiveColorTextureIndex = kSurfaceMaterialInvalidTextureIndex;

      trackTexture(ctx, translucentMaterialData.getNormalTexture(), normalTextureIndex, hasTexcoords);
      trackTexture(ctx, translucentMaterialData.getTransmittanceTexture(), transmittanceTextureIndex, hasTexcoords);
      trackTexture(ctx, translucentMaterialData.getEmissiveColorTexture(), emissiveColorTextureIndex, hasTexcoords);

      float refractiveIndex = translucentMaterialData.getRefractiveIndex();
      Vector3 transmittanceColor = translucentMaterialData.getTransmittanceColor();
      float transmittanceMeasureDistance = translucentMaterialData.getTransmittanceMeasurementDistance();
      Vector3 emissiveColorConstant = translucentMaterialData.getEmissiveColorConstant();
      bool enableEmissive = translucentMaterialData.getEnableEmission();
      float emissiveIntensity = translucentMaterialData.getEmissiveIntensity();
      bool isThinWalled = translucentMaterialData.getEnableThinWalled();
      float thinWallThickness = translucentMaterialData.getThinWallThickness();
      bool useDiffuseLayer = translucentMaterialData.getEnableDiffuseLayer();

      const RtTranslucentSurfaceMaterial translucentSurfaceMaterial{
        normalTextureIndex, transmittanceTextureIndex, emissiveColorTextureIndex,
        refractiveIndex,
        transmittanceMeasureDistance, transmittanceColor,
        enableEmissive, emissiveIntensity, emissiveColorConstant,
        isThinWalled, thinWallThickness, useDiffuseLayer, samplerIndex
      };

      surfaceMaterial.emplace(translucentSurfaceMaterial);
    } else if (renderMaterialDataType == MaterialDataType::RayPortal) {
      const auto& rayPortalMaterialData = renderMaterialData.getRayPortalMaterialData();

      uint32_t maskTextureIndex = kSurfaceMaterialInvalidTextureIndex;
      trackTexture(ctx, rayPortalMaterialData.getMaskTexture(), maskTextureIndex, hasTexcoords, false);
      uint32_t maskTextureIndex2 = kSurfaceMaterialInvalidTextureIndex;
      trackTexture(ctx, rayPortalMaterialData.getMaskTexture2(), maskTextureIndex2, hasTexcoords, false);

      uint32_t samplerIndex2 = trackSampler(drawCallState.getMaterialData().getSampler2());

      uint8_t rayPortalIndex = rayPortalMaterialData.getRayPortalIndex();
      float rotationSpeed = rayPortalMaterialData.getRotationSpeed();
      bool enableEmissive = rayPortalMaterialData.getEnableEmission();
      float emissiveIntensity = rayPortalMaterialData.getEmissiveIntensity();

      const RtRayPortalSurfaceMaterial rayPortalSurfaceMaterial{
        maskTextureIndex, maskTextureIndex2, rayPortalIndex,
        rotationSpeed, enableEmissive, emissiveIntensity, samplerIndex, samplerIndex2
      };

      surfaceMaterial.emplace(rayPortalSurfaceMaterial);
    }
    assert(surfaceMaterial.has_value());
    assert(surfaceMaterial->validate());

    // Cache this
    uint32_t surfaceMaterialIndex = m_surfaceMaterialCache.track(*surfaceMaterial);

    RtInstance* instance = m_instanceManager.processSceneObject(m_cameraManager, m_rayPortalManager, *pBlas, drawCallState, renderMaterialData, *surfaceMaterial);

    // Check if a light should be created for this Material
    if (instance && RtxOptions::Get()->shouldConvertToLight(drawCallState.getMaterialData().getHash())) {
      createEffectLight(ctx, drawCallState, instance);
    }

    // for highlighting: find a surface material index for a given legacy texture hash
    // the requests are loose, may expand to many frames to suppress flickering
    // NOTE: (!overrideMaterialData) -- to ignore replacements for now, as
    // there might be multiple surface material indices for a single legacy texture hash,
    // so highlighting involves a lot of flickering; need a better solution that
    // can handle multiple surface material indices
    if (!overrideMaterialData) {
      std::lock_guard lock{ m_highlighting.mutex };
      if (auto h = m_highlighting.findSurfaceForLegacyTextureHash) {
        if (*h == drawCallState.getMaterialData().getColorTexture().getImageHash() ||
            *h == drawCallState.getMaterialData().getColorTexture2().getImageHash()) {
          m_highlighting.finalSurfaceMaterialIndex = surfaceMaterialIndex;
          m_highlighting.finalWasUpdatedFrameId = m_device->getCurrentFrameId();
          m_highlighting.findSurfaceForLegacyTextureHash = {};
        }
      }
    }

    // if requested, find a legacy texture for a given surface material index
    {
      std::lock_guard lock{ m_findLegacyTextureMutex };
      if (m_findLegacyTexture) {
        if (m_findLegacyTexture->targetSurfMaterialIndex == surfaceMaterialIndex) {
          XXH64_hash_t legacyTextureHash = drawCallState.getMaterialData().getColorTexture().getImageHash();
          m_findLegacyTexture->promise.set_value(legacyTextureHash);
          // value is set, clean up
          m_findLegacyTexture = {};
        }
      }
    }

    return instance ? instance->getId() : UINT64_MAX;
  }

  std::future<XXH64_hash_t> SceneManager::findLegacyTextureHashBySurfaceMaterialIndex(uint32_t surfaceMaterialIndex) {
    std::lock_guard lock{ m_findLegacyTextureMutex };
    if (m_findLegacyTexture) {
      // if previous promise was not satisfied, force it to end with any value; and clean it up
      m_findLegacyTexture->promise.set_value(kEmptyHash);
      m_findLegacyTexture = {};
    }
    m_findLegacyTexture = PromisedSurfMaterialIndex {
      /* .targetSurfMaterialIndex = */ surfaceMaterialIndex,
      /* .promise = */ {},
    };
    return m_findLegacyTexture->promise.get_future();
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
      RtxOptions::Get()->getAnisotropicFilteringEnabled());
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
    if (pReplacements) {
      const Matrix4 lightTransform = LightUtils::getLightTransform(light);

      // TODO(TREX-1091) to implement meshes as light replacements, replace the below loop with a call to drawReplacements.
      for (auto&& replacement : *pReplacements) {
        if (replacement.type == AssetReplacement::eLight && replacement.lightData.has_value()) {
          LightData replacementLight = replacement.lightData.value();
          // Merge the d3d9 light into replacements based on overrides
          replacementLight.merge(light);
          // Convert to runtime light
          RtLight rtReplacementLight = replacementLight.toRtLight(&rtLight);
          // Transform the replacement light by the legacy light
          if (replacementLight.relativeTransform()) {
            rtReplacementLight.applyTransform(lightTransform);
          }

          // Setup Light Replacement for Anti-Culling
          if (RtxOptions::AntiCulling::Light::enable() && rtLight.getType() == RtLightType::Sphere) {
            // Apply the light
            m_lightManager.addLight(rtReplacementLight, RtLightAntiCullingType::LightReplacement);
          } else {
            // Apply the light
            m_lightManager.addLight(rtReplacementLight, RtLightAntiCullingType::Ignore);
          }
        } else {
          assert(false); // We don't support meshes as children of lights yet.
        }
      }
    } else {
      // This is a light coming from the game directly, so use the appropriate API for filter rules
      m_lightManager.addGameLight(light.Type, rtLight);
    }
  }

  void SceneManager::prepareSceneData(Rc<DxvkContext> ctx, DxvkBarrierSet& execBarriers, const float frameTimeSecs) {
    ScopedGpuProfileZone(ctx, "Build Scene");

    // Needs to happen before garbageCollection to avoid destroying dynamic lights
    m_lightManager.dynamicLightMatching();

    garbageCollection();
    
    auto& textureManager = m_device->getCommon()->getTextureManager();
    m_bindlessResourceManager.prepareSceneData(ctx, textureManager.getTextureTable(), getBufferTable(), getSamplerTable());

    // If there are no instances, we should do nothing!
    if (m_instanceManager.getActiveCount() == 0) {
      // Clear the ray portal data before the next frame
      m_rayPortalManager.clear();
      return;
    }

    m_rayPortalManager.prepareSceneData(ctx, frameTimeSecs);
    // Note: only main camera needs to be teleportation corrected as only that one is used for ray tracing & denoising
    m_rayPortalManager.fixCameraInBetweenPortals(m_cameraManager.getCamera(CameraType::Main));
    m_rayPortalManager.fixCameraInBetweenPortals(m_cameraManager.getCamera(CameraType::ViewModel));
    m_rayPortalManager.createVirtualCameras(m_cameraManager);
    const bool didTeleport = m_rayPortalManager.detectTeleportationAndCorrectCameraHistory(
      m_cameraManager.getCamera(CameraType::Main),
      m_cameraManager.isCameraValid(CameraType::ViewModel) ? &m_cameraManager.getCamera(CameraType::ViewModel) : nullptr);

    if (m_cameraManager.isCameraCutThisFrame()) {
      // Ignore camera cut events on teleportation so we don't flush the caches
      if (!didTeleport) {
        Logger::info(str::format("Camera cut detected on frame ", m_device->getCurrentFrameId()));
        m_enqueueDelayedClear = true;
      }
    }

    if (m_pReplacer->checkForChanges(ctx)) {
      // Delay release of textures to the end of the frame, when all commands are executed.
      m_enqueueDelayedClear = true;
    }

    // Initialize/remove opacity micromap manager
    if (RtxOptions::Get()->getEnableOpacityMicromap()) {
      if (!m_opacityMicromapManager.get() || 
          // Reset the manager on camera cuts
          m_enqueueDelayedClear) {
        if (m_opacityMicromapManager.get())
          m_instanceManager.removeEventHandler(m_opacityMicromapManager.get());

        m_opacityMicromapManager = std::make_unique<OpacityMicromapManager>(m_device);
        m_instanceManager.addEventHandler(m_opacityMicromapManager->getInstanceEventHandler());
        Logger::info("[RTX] Opacity Micromap: enabled");
      }
    } else if (m_opacityMicromapManager.get()) {
      m_instanceManager.removeEventHandler(m_opacityMicromapManager.get());
      m_opacityMicromapManager = nullptr;
      Logger::info("[RTX] Opacity Micromap: disabled");
    }

    m_instanceManager.findPortalForVirtualInstances(m_cameraManager, m_rayPortalManager);
    m_instanceManager.createViewModelInstances(ctx, m_cameraManager, m_rayPortalManager);
    m_instanceManager.createPlayerModelVirtualInstances(ctx, m_cameraManager, m_rayPortalManager);

    m_accelManager.mergeInstancesIntoBlas(ctx, execBarriers, textureManager.getTextureTable(), m_cameraManager, m_instanceManager, m_opacityMicromapManager.get(), frameTimeSecs);

    // Call on the other managers to prepare their GPU data for the current scene
    m_accelManager.prepareSceneData(ctx, execBarriers, m_instanceManager);
    m_lightManager.prepareSceneData(ctx, m_cameraManager);

    // Build the TLAS
    m_accelManager.buildTlas(ctx);

    // Todo: These updates require a lot of temporary buffer allocations and memcopies, ideally we should memcpy directly into a mapped pointer provided by Vulkan,
    // but we have to create a buffer to pass to DXVK's updateBuffer for now.
    {
      // Allocate the instance buffer and copy its contents from host to device memory
      DxvkBufferCreateInfo info = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
      info.usage = VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
      info.stages = VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR;
      info.access = VK_ACCESS_TRANSFER_WRITE_BIT;

      // Surface Material buffer
      if (m_surfaceMaterialCache.getTotalCount() > 0) {
        ScopedGpuProfileZone(ctx, "updateSurfaceMaterials");
        const auto surfaceMaterialsGPUSize = m_surfaceMaterialCache.getTotalCount() * kSurfaceMaterialGPUSize;

        info.size = align(surfaceMaterialsGPUSize, kBufferAlignment);
        info.usage |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        if (m_surfaceMaterialBuffer == nullptr || info.size > m_surfaceMaterialBuffer->info().size) {
          m_surfaceMaterialBuffer = m_device->createBuffer(info, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, DxvkMemoryStats::Category::RTXBuffer);
        }

        std::size_t dataOffset = 0;
        std::vector<unsigned char> surfaceMaterialsGPUData(surfaceMaterialsGPUSize);

        for (auto&& surfaceMaterial : m_surfaceMaterialCache.getObjectTable()) {
          surfaceMaterial.writeGPUData(surfaceMaterialsGPUData.data(), dataOffset);
        }

        assert(dataOffset == surfaceMaterialsGPUSize);
        assert(surfaceMaterialsGPUData.size() == surfaceMaterialsGPUSize);

        ctx->writeToBuffer(m_surfaceMaterialBuffer, 0, surfaceMaterialsGPUData.size(), surfaceMaterialsGPUData.data());
      }

      // Surface Material Extension Buffer
      if (m_surfaceMaterialExtensionCache.getTotalCount() > 0) {
        ScopedGpuProfileZone(ctx, "updateSurfaceMaterialExtensions");
        const auto surfaceMaterialExtensionsGPUSize = m_surfaceMaterialExtensionCache.getTotalCount() * kSurfaceMaterialGPUSize;

        info.size = align(surfaceMaterialExtensionsGPUSize, kBufferAlignment);
        info.usage |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        if (m_surfaceMaterialExtensionBuffer == nullptr || info.size > m_surfaceMaterialExtensionBuffer->info().size) {
          m_surfaceMaterialExtensionBuffer = m_device->createBuffer(info, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, DxvkMemoryStats::Category::RTXBuffer);
        }

        std::size_t dataOffset = 0;
        std::vector<unsigned char> surfaceMaterialExtensionsGPUData(surfaceMaterialExtensionsGPUSize);

        for (auto&& surfaceMaterialExtension : m_surfaceMaterialExtensionCache.getObjectTable()) {
          surfaceMaterialExtension.writeGPUData(surfaceMaterialExtensionsGPUData.data(), dataOffset);
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
          m_volumeMaterialBuffer = m_device->createBuffer(info, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, DxvkMemoryStats::Category::RTXBuffer);
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
    capturer->step(ctx, frameTimeSecs);

    // Clear the ray portal data before the next frame
    m_rayPortalManager.clear();
  }

  void SceneManager::requestHighlighting(std::variant<uint32_t, XXH64_hash_t> surfaceMaterialIndexOrLegacyTextureHash,
                                         HighlightColor color,
                                         uint32_t frameId) {
    std::lock_guard lock{ m_highlighting.mutex };
    if (auto surfaceMaterialIndex = std::get_if<uint32_t>(&surfaceMaterialIndexOrLegacyTextureHash)) {
      m_highlighting.finalSurfaceMaterialIndex = *surfaceMaterialIndex;
      m_highlighting.finalWasUpdatedFrameId = frameId;
    } else if (auto legacyTextureHash = std::get_if<XXH64_hash_t>(&surfaceMaterialIndexOrLegacyTextureHash)) {
      m_highlighting.findSurfaceForLegacyTextureHash = *legacyTextureHash;
    }
    m_highlighting.color = color;
  }

  std::optional<std::pair<uint32_t, HighlightColor>> SceneManager::accessSurfaceMaterialIndexToHighlight(uint32_t frameId) {
    std::lock_guard lock{ m_highlighting.mutex };
    if (m_highlighting.finalSurfaceMaterialIndex) {
      if (Highlighting::keepRequest(m_highlighting.finalWasUpdatedFrameId, frameId)) {
        return std::pair{ *m_highlighting.finalSurfaceMaterialIndex, m_highlighting.color };
      }
    }
    return {};
  }

  void SceneManager::submitExternalDraw(Rc<DxvkContext> ctx, ExternalDrawState&& state) {
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

    {
      state.drawCall.materialData.samplers[0] = m_externalSampler;
      state.drawCall.materialData.samplers[1] = m_externalSampler;
    }
    {
      const RtCamera& rtCamera = ctx->getCommonObjects()->getSceneManager().getCameraManager()
        .getCamera(state.cameraType);
      state.drawCall.transformData.worldToView = Matrix4 { rtCamera.getWorldToView() };
      state.drawCall.transformData.viewToProjection = Matrix4 { rtCamera.getViewToProjection() };
      state.drawCall.transformData.objectToView = state.drawCall.transformData.worldToView * state.drawCall.transformData.objectToWorld;
    }

    for (const RasterGeometry& submesh : m_pReplacer->accessExternalMesh(state.mesh)) {
      state.drawCall.geometryData = submesh;
      state.drawCall.geometryData.cullMode = state.doubleSided ? VK_CULL_MODE_NONE : VK_CULL_MODE_BACK_BIT;

      const MaterialData* material = m_pReplacer->accessExternalMaterial(submesh.externalMaterial);
      if (material != nullptr) {
        state.drawCall.materialData.setHashOverride(material->getHash());
      }

      processDrawCallState(ctx, state.drawCall, material);
    }
  }

}  // namespace nvvk
