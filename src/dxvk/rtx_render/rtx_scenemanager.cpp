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
#include "rtx_scenemanager.h"
#include "rtx_opacity_micromap_manager.h"
#include "dxvk_device.h"
#include "dxvk_context.h"
#include "dxvk_buffer.h"
#include "rtx_context.h"
#include "rtx_options.h"

#include <assert.h>

#include "../d3d9/d3d9_state.h"
#include "vulkan/vulkan_core.h"

#include "rtx_game_capturer.h"
#include "rtx_matrix_helpers.h"
#include "rtx_intersection_test_helpers.h"

#include "dxvk_scoped_annotation.h"

namespace dxvk {

  SceneManager::SceneManager(Rc<DxvkDevice> device)
    : m_device(device)
    , m_instanceManager(device, this)
    , m_accelManager(device)
    , m_lightManager(device)
    , m_rayPortalManager(device, this)
    , m_drawCallCache(device)
    , m_bindlessResourceManager(device)
    , m_volumeManager(device)
    , m_pReplacer(new AssetReplacer(device))
    , m_gameCapturer(new GameCapturer(*this, device->getCommon()->metaExporter()))
    , m_cameraManager(device)
    , m_startTime(std::chrono::system_clock::now()) {
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
  uint32_t SceneManager::getGameTimeSinceStartMS() {
    // Used in testing
    if (m_useFixedFrameTime) {
      float deltaTimeMS = 1000.f / 60; // Assume 60 fps
      return (uint32_t) (m_device->getCurrentFrameId() * deltaTimeMS);
    }

    // TODO(TREX-1004) find a way to 'pause' this when a game is paused.
    auto currTime = std::chrono::system_clock::now();

    auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(currTime - m_startTime);

    return static_cast<uint32_t>(elapsedMs.count());
  }

  void SceneManager::initialize(Rc<DxvkContext> ctx) {
    ScopedCpuProfileZone();
    m_pReplacer->initialize(ctx);
  }

  void SceneManager::clear(Rc<DxvkContext> ctx, bool needWfi) {
    ScopedCpuProfileZone();

    // Only clear once after the scene disappears, to avoid adding a WFI on every frame through clear().
    if (needWfi)
    {
      if (ctx.ptr())
        ctx->flushCommandList();
      m_device->getCommon()->getTextureManager().synchronize(true);
      m_device->waitForIdle();
    }

    // We still need to clear caches even if the scene wasn't rendered
    m_textureCache.clear(); 
    m_bufferCache.clear();
    m_surfaceMaterialCache.clear();
    m_volumeMaterialCache.clear();
    m_device->getCommon()->getTextureManager().demoteTexturesFromVidmem();
    
    // Called before instance manager's clear, so that it resets all tracked instances in Opacity Micromap manager at once
    if (m_opacityMicromapManager.get())
      m_opacityMicromapManager->clear();
    
    m_instanceManager.clear();
    m_lightManager.clear();
    m_rayPortalManager.clear();
    m_drawCallCache.clear();

    m_previousFrameSceneAvailable = false;
  }

  void SceneManager::garbageCollection() {
    ScopedCpuProfileZone();
    // Garbage collection for BLAS/Scene objects
    if (!RtxOptions::Get()->enableAntiCulling())
    {
      if (m_device->getCurrentFrameId() > RtxOptions::Get()->numFramesToKeepGeometryData()) {
        auto& entries = m_drawCallCache.getEntries();
        const size_t oldestFrame = m_device->getCurrentFrameId() - RtxOptions::Get()->numFramesToKeepGeometryData();
        for (auto& iter = entries.begin(); iter != entries.end(); ) {
          if (iter->second.frameLastTouched < oldestFrame) {
            onSceneObjectDestroyed(iter->second, iter->first);
            iter = entries.erase(iter);
          } else {
            ++iter;
          }
        }
      }
    }
    else { // Implement anti-culling object GC
      auto& entries = m_drawCallCache.getEntries();
      for (auto& iter = entries.begin(); iter != entries.end(); ++iter) {
        for (const RtInstance* instance : iter->second.getLinkedInstances()) {
          // No need to do frustum check for instances under the keeping threshold
          const uint32_t numFramesToKeepInstances = RtxOptions::Get()->getNumFramesToKeepInstances();
          const uint32_t currentFrame = m_device->getCurrentFrameId();
          if (instance->getFrameLastUpdated() + numFramesToKeepInstances > currentFrame) {
            continue;
          }

          const Matrix4 objectToView = m_cameraManager.getMainCamera().getWorldToView(false) * instance->getBlas()->input.getTransformData().objectToWorld;

          bool isInsideFrustum = true;
          if (instance->getBlas()->input.getGeometryData().futureBoundingBox.valid()) {
            const AxisAlignBoundingBox boundingBox = instance->getBlas()->input.getGeometryData().boundingBox;
            isInsideFrustum = boundingBoxIntersectsFrustum(m_cameraManager.getMainCamera().getFrustum(), boundingBox.minPos, boundingBox.maxPos, objectToView);
          }
          else {
            // Fallback to check object center under view space
            isInsideFrustum = m_cameraManager.getMainCamera().getFrustum().CheckSphere(float3(objectToView[3][0], objectToView[3][1], objectToView[3][2]), 0);
          }

          // Only GC the objects inside the frustum to anti-frustum culling, this could cause significant performance impact
          // For the objects which can't be handled well with this algorithm, we will need game specific hash to force keeping them
          if (isInsideFrustum && !RtxOptions::Get()->isAntiCullingTexture(instance->getMaterialDataHash())) {
            instance->markAsInsideFrustum();
          } else {
            instance->markAsOutsideFrustum();
          }
        }
      }
    }

    // Demote high res material textures
    if (m_device->getCurrentFrameId() > RtxOptions::Get()->numFramesToKeepMaterialTextures()) {
      const size_t oldestFrame = m_device->getCurrentFrameId() - RtxOptions::Get()->numFramesToKeepMaterialTextures();
      auto& entries = m_textureCache.getObjectTable();
      for (auto& iter = entries.begin(); iter != entries.end(); iter++) {
        const bool isDemotable = iter->getManagedTexture() != nullptr && iter->getManagedTexture()->canDemote;
        if (isDemotable && iter->frameLastUsed < oldestFrame) {
          iter->demote();
        }
      }
    }

    // Perform GC on the other managers
    m_instanceManager.garbageCollection();
    m_accelManager.garbageCollection();
    m_lightManager.garbageCollection();
    m_rayPortalManager.garbageCollection();
  }

  void SceneManager::destroy() {
  }

  template<bool isNew>
  SceneManager::ObjectCacheState SceneManager::processGeometryInfo(Rc<DxvkContext> ctx, Rc<DxvkCommandList> cmd, const DrawCallState& drawCallState, RaytraceGeometry& inOutGeometry) {
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
    updateBufferCache(inOutGeometry, output);

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

    if (RtxOptions::Get()->resetBufferCacheOnEveryFrame())
      m_bufferCache.clear();

    m_materialTextureSampler = nullptr;
  }

  std::unordered_set<XXH64_hash_t> uniqueHashes;


  void SceneManager::submitDrawState(Rc<DxvkContext> ctx, Rc<DxvkCommandList> cmd, const DrawCallState& input) {
    ScopedCpuProfileZone();
    const uint32_t kBufferCacheLimit = kSurfaceInvalidBufferIndex - 10; // Limit for unique buffers minus some padding
    if (m_bufferCache.getTotalCount() >= kBufferCacheLimit && m_bufferCache.getActiveCount() >= kBufferCacheLimit) {
      Logger::info("[RTX-Compatibility-Info] This application is pushing more unique buffers than is currently supported - some objects may not raytrace.");
      return;
    }

    if (m_fog.mode == D3DFOG_NONE && input.getFogState().mode != D3DFOG_NONE) {
      m_fog = input.getFogState();
    }

    // Check if a any camera data requires processing
    const bool cameraCut = m_cameraManager.processCameraData(input);

    // Skip objects with an unknown camera
    if (m_cameraManager.getLastSetCameraType() == CameraType::Unknown &&
        RtxOptions::Get()->getSkipObjectsWithUnknownCamera())
      return;

    // Get Material and Mesh replacements
    // NOTE: Next refactor we move this into a material manager
    const MaterialData* overrideMaterialData = m_pReplacer->getReplacementMaterial(input.getMaterialData().getHash());

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
    std::optional<MaterialData> rayPortalMaterialData{};
    size_t rayPortalTextureIndex;

    if (RtxOptions::Get()->getRayPortalTextureIndex(input.getMaterialData().getHash(), rayPortalTextureIndex)) {
      assert(rayPortalTextureIndex < maxRayPortalCount);
      assert(rayPortalTextureIndex < std::numeric_limits<uint8_t>::max());
      
      // Mask texture is required for Portal
      const bool materialHasMaskTexture = input.getMaterialData().getColorTexture2().isValid();

      if (materialHasMaskTexture) {
        const TextureRef& texture2 = input.getMaterialData().getColorTexture2();

        if (overrideMaterialData != nullptr) {
          assert(overrideMaterialData->getType() == MaterialDataType::RayPortal);
          const RayPortalMaterialData& data = overrideMaterialData->getRayPortalMaterialData();
          rayPortalMaterialData.emplace(RayPortalMaterialData { data.getMaskTexture(), texture2, data.getRayPortalIndex(), data.getSpriteSheetRows(), data.getSpriteSheetCols(), data.getSpriteSheetFPS(), data.getRotationSpeed(), true, data.getEmissiveIntensity() });
        } else {
          // Note: Color texture used as mask texture for the Ray Portal
          rayPortalMaterialData.emplace(RayPortalMaterialData { input.getMaterialData().getColorTexture(), texture2, static_cast<uint8_t>(rayPortalTextureIndex), 1, 1, 0, 0.f, true, 1.f });
        }

        // Note: A bit dirty but since we use a pointer to the material data in processDrawCallState, we need a pointer to this locally created one on the
        // stack in a place that doesn't go out of scope without actually allocating any heap memory.
        overrideMaterialData = &*rayPortalMaterialData;
      }
    }

    // Detect meshes that would have unstable hashes due to the vertex hash using vertex data from a shared vertex buffer.
    // TODO: Once the vertex hash only uses vertices referenced by the index buffer, this should be removed.
    const bool highlightUnsafeAnchor = RtxOptions::Get()->getHighlightUnsafeAnchorModeEnabled() &&
        input.getGeometryData().indexBuffer.defined() && input.getGeometryData().vertexCount > input.getGeometryData().indexCount;
    if (highlightUnsafeAnchor) {
      static MaterialData sHighlightMaterialData(OpaqueMaterialData(TextureRef(), TextureRef(), TextureRef(), TextureRef(), TextureRef(), TextureRef(), 
          0.f, 1.f, Vector4(0.2f, 0.2f, 0.2f, 1.0f), 0.1f, 0.1f, Vector3(0.46f, 0.26f, 0.31f), true, 1, 1, 0, false, false, 200.f, true, false, BlendType::kAlpha, false, AlphaTestType::kAlways, 0));
      overrideMaterialData = &sHighlightMaterialData;
    }

    if (RtxOptions::Get()->highlightedTexture() != kEmptyHash)
    {
      auto isHighlighted = [](const TextureRef &t){
        return RtxOptions::Get()->highlightedTexture() == t.getImageHash();
      };

      if (isHighlighted(input.getMaterialData().getColorTexture()) || isHighlighted(input.getMaterialData().getColorTexture2())) {
        static MaterialData sHighlightMaterialData(OpaqueMaterialData(TextureRef(), TextureRef(), TextureRef(), TextureRef(), TextureRef(), TextureRef(),
            0.f, 1.f, Vector4(0.2f, 0.2f, 0.2f, 1.f), 0.1f, 0.1f, Vector3(0.f, 1.f, 0.f), true, 1, 1, 0, false, false, 200.f, true, false, BlendType::kAlpha, false, AlphaTestType::kAlways, 0));
        if (getGameTimeSinceStartMS() / 200 % 2 == 0) {
          overrideMaterialData = &sHighlightMaterialData;
        }
      }
    }

    uint64_t instanceId = UINT64_MAX;
    if (pReplacements != nullptr) {
      instanceId = drawReplacements(ctx, cmd, &input, pReplacements, overrideMaterialData);
    } else {
      instanceId = processDrawCallState(ctx, cmd, input, overrideMaterialData);
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
    Vector4 worldPos = m_cameraManager.getMainCamera().getViewToWorld(false) * renderingPos;

    RtLightShaping shaping;
    shaping.enabled = false;
    const float lightRadius = std::max(RtxOptions::Get()->getEffectLightRadius(), 1e-3f);
    const float surfaceArea = 4.f * kPi * lightRadius * lightRadius;
    const float radianceFactor = 1e5f * effectLightIntensity / surfaceArea;
    const Vector3 lightPosition = Vector3(worldPos.x, worldPos.y, worldPos.z);
    Vector3 lightRadiance;
    if (RtxOptions::Get()->getEffectLightPlasmaBall()) {
      const double timeMilliseconds = getGameTimeSinceStartMS();
      const double animationPhase = sin(timeMilliseconds * 0.006) * 0.5 + 0.5;
      lightRadiance = lerp(Vector3(1.f, 0.921f, 0.738f), Vector3(1.f, 0.521f, 0.238f), animationPhase) * radianceFactor;
    } else {
      const D3DCOLORVALUE originalColor = input.getMaterialData().getLegacyMaterial().Diffuse;
      lightRadiance = Vector3(originalColor.r, originalColor.g, originalColor.b) * radianceFactor;
    }

    RtLight rtLight(RtSphereLight(lightPosition, lightRadiance, lightRadius, shaping));
    rtLight.isDynamic = true;

    m_lightManager.addLight(rtLight, input);
  }

  uint64_t SceneManager::drawReplacements(Rc<DxvkContext> ctx, Rc<DxvkCommandList> cmd, const DrawCallState* input, const std::vector<AssetReplacement>* pReplacements, const MaterialData* overrideMaterialData) {
    ScopedCpuProfileZone();
    uint64_t rootInstanceId = UINT64_MAX;
    // Detect replacements of meshes that would have unstable hashes due to the vertex hash using vertex data from a shared vertex buffer.
    // TODO: Once the vertex hash only uses vertices referenced by the index buffer, this should be removed.
    const bool highlightUnsafeReplacement = RtxOptions::Get()->getHighlightUnsafeReplacementModeEnabled() &&
        input->getGeometryData().indexBuffer.defined() && input->getGeometryData().vertexCount > input->getGeometryData().indexCount;
    if (!pReplacements->empty() && (*pReplacements)[0].includeOriginal) {
      rootInstanceId = processDrawCallState(ctx, cmd, *input, overrideMaterialData);
    }
    for (auto&& replacement : *pReplacements) {
      if (replacement.type == AssetReplacement::eMesh) {
        DrawCallTransforms transforms = input->getTransformData();
        
        transforms.objectToWorld = transforms.objectToWorld * replacement.replacementToObject;
        transforms.objectToView = transforms.objectToView * replacement.replacementToObject;
        
        const DrawCallState newDrawCallState{
          *replacement.geometryData, // Note: Geometry Data replaced
          input->getMaterialData(), // Note: Original legacy material data preserved
          transforms,
          input->getSkinningState(),
          input->getFogState(),
          input->getStencilEnabledState()
        };

        // Note: Material Data replaced if a replacement is specified in the Mesh Replacement
        if (replacement.materialData != nullptr) {
          overrideMaterialData = replacement.materialData;
        }
        if (highlightUnsafeReplacement) {
          static MaterialData sHighlightMaterialData(OpaqueMaterialData(TextureRef(), TextureRef(), TextureRef(), TextureRef(), TextureRef(), TextureRef(), 
              0.f, 1.f, Vector4(0.2f, 0.2f, 0.2f, 1.f), 0.1f, 0.1f, Vector3(1.f, 0.f, 0.f), true, 1, 1, 0, false, false, 200.f, true, false, BlendType::kAlpha, false, AlphaTestType::kAlways, 0));
          if (getGameTimeSinceStartMS() / 200 % 2 == 0) {
            overrideMaterialData = &sHighlightMaterialData;
          }
        }
        uint64_t instanceId = processDrawCallState(ctx, cmd, newDrawCallState, overrideMaterialData);
        if (rootInstanceId == UINT64_MAX) {
          rootInstanceId = instanceId;
        }
      } else {
        if (rootInstanceId == UINT64_MAX) {
          // TODO(TREX-1141) if we refactor instancing to depend on the pre-replacement drawcall instead
          // of the fully processed draw call, we can remove this requirement.
          Logger::err(str::format(
              "Light prims attached to replacement meshes must come after a mesh prim.  mesh hash: ",
              std::hex, input->getHash(RtxOptions::Get()->GeometryHashGenerationRule)
          ));
          continue;
        }
        RtLight localLight(replacement.lightData);
        localLight.setRootInstanceId(rootInstanceId);
        localLight.applyTransform(input->getTransformData().objectToWorld);
        m_lightManager.addLight(localLight);
      }
    }

    return rootInstanceId;
  }

  void SceneManager::clearFogState() {
    m_fog = FogState();
  }
  
  void SceneManager::freeBufferCache(const RaytraceGeometry& geoData) {
    ScopedCpuProfileZone();
    if (geoData.indexBuffer.defined()) {
      m_bufferCache.removeRef(geoData.indexBuffer);
    }
    if (geoData.normalBuffer.defined()) {
      m_bufferCache.removeRef(geoData.normalBuffer);
    }
    if (geoData.color0Buffer.defined()) {
      m_bufferCache.removeRef(geoData.color0Buffer);
    }
    if (geoData.texcoordBuffer.defined()) {
      m_bufferCache.removeRef(geoData.texcoordBuffer);
    }
    if (geoData.positionBuffer.defined()) {
      m_bufferCache.removeRef(geoData.positionBuffer);
    }
    if (geoData.previousPositionBuffer.defined()) {
      m_bufferCache.removeRef(geoData.previousPositionBuffer);
    }
  }

  void SceneManager::updateBufferCache(const RaytraceGeometry& oldGeoData, RaytraceGeometry& newGeoData) {
    ScopedCpuProfileZone();
    if (RtxOptions::Get()->resetBufferCacheOnEveryFrame()) {
      if (newGeoData.indexBuffer.defined())
        newGeoData.indexBufferIndex = m_bufferCache.addRef(newGeoData.indexBuffer);
      else
        newGeoData.indexBufferIndex = kSurfaceInvalidBufferIndex;

      if (newGeoData.normalBuffer.defined())
        newGeoData.normalBufferIndex = m_bufferCache.addRef(newGeoData.normalBuffer);
      else
        newGeoData.normalBufferIndex = kSurfaceInvalidBufferIndex;

      if (newGeoData.color0Buffer.defined())
        newGeoData.color0BufferIndex = m_bufferCache.addRef(newGeoData.color0Buffer);
      else
        newGeoData.color0BufferIndex = kSurfaceInvalidBufferIndex;

      if (newGeoData.texcoordBuffer.defined())
        newGeoData.texcoordBufferIndex = m_bufferCache.addRef(newGeoData.texcoordBuffer);
      else
        newGeoData.texcoordBufferIndex = kSurfaceInvalidBufferIndex;

      if (newGeoData.positionBuffer.defined())
        newGeoData.positionBufferIndex = m_bufferCache.addRef(newGeoData.positionBuffer);
      else
        newGeoData.positionBufferIndex = kSurfaceInvalidBufferIndex;

      if (newGeoData.previousPositionBuffer.defined())
        newGeoData.previousPositionBufferIndex = m_bufferCache.addRef(newGeoData.previousPositionBuffer);
      else
        newGeoData.previousPositionBufferIndex = kSurfaceInvalidBufferIndex;
    }
    else {
      // If buffers have changed, free the old buffer and track the new one
      if (oldGeoData.indexBuffer != newGeoData.indexBuffer) {
        if (newGeoData.indexBuffer.defined())
          newGeoData.indexBufferIndex = m_bufferCache.addRef(newGeoData.indexBuffer);
        if (oldGeoData.indexBuffer.defined())
          m_bufferCache.removeRef(oldGeoData.indexBuffer);
      } else {
        newGeoData.indexBufferIndex = oldGeoData.indexBufferIndex;
      }

      if (oldGeoData.normalBuffer != newGeoData.normalBuffer) {
        if (newGeoData.normalBuffer.defined())
          newGeoData.normalBufferIndex = m_bufferCache.addRef(newGeoData.normalBuffer);
        if (oldGeoData.normalBuffer.defined())
          m_bufferCache.removeRef(oldGeoData.normalBuffer);
      } else {
        newGeoData.normalBufferIndex = oldGeoData.normalBufferIndex;
      }

      if (oldGeoData.color0Buffer != newGeoData.color0Buffer) {
        if (newGeoData.color0Buffer.defined())
          newGeoData.color0BufferIndex = m_bufferCache.addRef(newGeoData.color0Buffer);
        if (oldGeoData.color0Buffer.defined())
          m_bufferCache.removeRef(oldGeoData.color0Buffer);
      } else {
        newGeoData.color0BufferIndex = oldGeoData.color0BufferIndex;
      }

      if (oldGeoData.texcoordBuffer != newGeoData.texcoordBuffer) {
        if (newGeoData.texcoordBuffer.defined())
          newGeoData.texcoordBufferIndex = m_bufferCache.addRef(newGeoData.texcoordBuffer);
        if (oldGeoData.texcoordBuffer.defined())
          m_bufferCache.removeRef(oldGeoData.texcoordBuffer);
      } else {
        newGeoData.texcoordBufferIndex = oldGeoData.texcoordBufferIndex;
      }

      if (oldGeoData.positionBuffer != newGeoData.positionBuffer) {
        if (newGeoData.positionBuffer.defined())
          newGeoData.positionBufferIndex = m_bufferCache.addRef(newGeoData.positionBuffer);
        if (oldGeoData.positionBuffer.defined())
          m_bufferCache.removeRef(oldGeoData.positionBuffer);
      } else {
        newGeoData.positionBufferIndex = oldGeoData.positionBufferIndex;
      }

      if (oldGeoData.previousPositionBuffer != newGeoData.previousPositionBuffer) {
        if (newGeoData.previousPositionBuffer.defined())
          newGeoData.previousPositionBufferIndex = m_bufferCache.addRef(newGeoData.previousPositionBuffer);
        if (oldGeoData.previousPositionBuffer.defined())
          m_bufferCache.removeRef(oldGeoData.previousPositionBuffer);
      } else {
        newGeoData.previousPositionBufferIndex = oldGeoData.previousPositionBufferIndex;
      }
    }
  }

  SceneManager::ObjectCacheState SceneManager::onSceneObjectAdded(Rc<DxvkContext> ctx, Rc<DxvkCommandList> cmd, const DrawCallState& drawCallState, BlasEntry* pBlas) {
    // This is a new object.
    ObjectCacheState result = processGeometryInfo<true>(ctx, cmd, drawCallState, pBlas->modifiedGeometryData);
    
    assert(result == ObjectCacheState::KBuildBVH);

    pBlas->frameLastUpdated = m_device->getCurrentFrameId();

    return result;
  }
  
  SceneManager::ObjectCacheState SceneManager::onSceneObjectUpdated(Rc<DxvkContext> ctx, Rc<DxvkCommandList> cmd, const DrawCallState& drawCallState, BlasEntry* pBlas) {
    if (pBlas->frameLastTouched == m_device->getCurrentFrameId()) {
      pBlas->cacheMaterial(drawCallState.getMaterialData());
      return SceneManager::ObjectCacheState::kUpdateInstance;
    }

    // TODO: If mesh is static, no need to do any of the below, just use the existing modifiedGeometryData and set result to kInstanceUpdate.
    ObjectCacheState result = processGeometryInfo<false>(ctx, cmd, drawCallState, pBlas->modifiedGeometryData);

    // We dont expect to hit the rebuild path here - since this would indicate an index buffer or other topological change, and that *should* trigger a new scene object (since the hash would change)
    assert(result != ObjectCacheState::KBuildBVH);

    if (result == ObjectCacheState::kUpdateBVH)
      pBlas->frameLastUpdated = m_device->getCurrentFrameId();
    
    pBlas->clearMaterialCache();
    pBlas->input = drawCallState; // cache the draw state for the next time.
    return result;
  }
  
  void SceneManager::onSceneObjectDestroyed(const BlasEntry& blas, const XXH64_hash_t& hash) {
    if (!RtxOptions::Get()->resetBufferCacheOnEveryFrame())
      freeBufferCache(blas.modifiedGeometryData);

    for (const RtInstance* instance : blas.getLinkedInstances()) {
      instance->markForGarbageCollection();
    }
  }

  void SceneManager::onInstanceAdded(const RtInstance& instance) {
    BlasEntry* pBlas = instance.getBlas();
    if (pBlas != nullptr) {
      pBlas->linkInstance(&instance);
    }
  }

  void SceneManager::onInstanceUpdated(RtInstance& instance, const RtSurfaceMaterial& material, const bool hasTransformChanged, const bool hasVerticesChanged) {
    if (hasTransformChanged) {
      m_gameCapturer->setInstanceUpdateFlag(instance, GameCapturer::InstFlag::XformUpdate);
    }

    if (hasVerticesChanged) {
      m_gameCapturer->setInstanceUpdateFlag(instance, GameCapturer::InstFlag::PositionsUpdate);
      m_gameCapturer->setInstanceUpdateFlag(instance, GameCapturer::InstFlag::NormalsUpdate);
    }

    // This is a ray portal!
    if (material.getType() == RtSurfaceMaterialType::RayPortal) {
      BlasEntry* pBlas = instance.getBlas();
      m_rayPortalManager.processRayPortalData(instance, material);
    }
  }

  void SceneManager::onInstanceDestroyed(const RtInstance& instance) {
    BlasEntry* pBlas = instance.getBlas();
    if (pBlas != nullptr) {
      pBlas->unlinkInstance(&instance);
    }
  }

  // Helper to populate the texture cache with this resource (and patch sampler if required for texture)
  void SceneManager::trackTexture(Rc<DxvkContext> ctx, TextureRef inputTexture, uint32_t& textureIndex, bool hasTexcoords, bool patchSampler, bool allowAsync) {
    // If no texcoords, no need to bind the texture
    if (!hasTexcoords) {
      ONCE(Logger::info(str::format("[RTX-Compatibility-Info] Trying to bind a texture to a mesh without UVs.  Was this intended?")));
      return;
    }

    // If theres valid texture backing this ref, then skip
    if (!inputTexture.isValid())
      return;

    // Track this texture
    textureIndex = m_textureCache.track(inputTexture);

    // Fetch the texture object from cache
    TextureRef& cachedTexture = m_textureCache.at(textureIndex);

    // If there is a pending promotion, schedule its upload
    if (cachedTexture.isPromotable()) {
      Rc<DxvkContext> dxvkCtx = ctx;
      m_device->getCommon()->getTextureManager().scheduleTextureUpload(cachedTexture, dxvkCtx, allowAsync);
    }

    if (patchSampler) {
      // Patch the sampler entry
      cachedTexture.sampler = m_materialTextureSampler;
    }

    cachedTexture.frameLastUsed = ctx->getDevice()->getCurrentFrameId();
  }

  uint64_t SceneManager::processDrawCallState(Rc<DxvkContext> ctx, Rc<DxvkCommandList> cmd, const DrawCallState& drawCallState, const MaterialData* overrideMaterialData) {
    ScopedCpuProfileZone();
    const MaterialData& renderMaterialData = overrideMaterialData != nullptr ? *overrideMaterialData : drawCallState.getMaterialData();
    if (renderMaterialData.getIgnored()) {
      return UINT64_MAX;
    }
    ObjectCacheState result = ObjectCacheState::kInvalid;
    BlasEntry* pBlas = nullptr;
    if (m_drawCallCache.get(drawCallState, &pBlas) == DrawCallCache::CacheState::kExisted) {
      result = onSceneObjectUpdated(ctx, cmd, drawCallState, pBlas);
    } else {
      result = onSceneObjectAdded(ctx, cmd, drawCallState, pBlas);
    }

    // Update the input state, so we always have a reference to the original draw call state
    pBlas->frameLastTouched = m_device->getCurrentFrameId();

    if (drawCallState.getSkinningState().numBones > 0 && (result == ObjectCacheState::KBuildBVH || result == ObjectCacheState::kUpdateBVH)) {
      m_device->getCommon()->metaGeometryUtils().dispatchSkinning(cmd, ctx, drawCallState, pBlas->modifiedGeometryData);
      pBlas->frameLastUpdated = pBlas->frameLastTouched;
    }
    
    assert(pBlas != nullptr);
    assert(result != ObjectCacheState::kInvalid);

    if (!m_materialTextureSampler.ptr()) {
      auto& resourceManager = m_device->getCommon()->getResources();

      // Create a sampler to account for DLSS lod bias and any custom filtering overrides the user has set
      const bool temporalUpscaling = RtxOptions::Get()->isDLSSEnabled() || RtxOptions::Get()->isTAAEnabled();
      const float totalUpscaleMipBias = temporalUpscaling ? (log2(resourceManager.getUpscaleRatio()) + RtxOptions::Get()->upscalingMipBias()) : 0.0f;
      const float totalTipBias = totalUpscaleMipBias + RtxOptions::Get()->getNativeMipBias();

      m_materialTextureSampler = resourceManager.getSampler(VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_LINEAR, VK_SAMPLER_ADDRESS_MODE_REPEAT, totalTipBias, RtxOptions::Get()->getAnisotropicFilteringEnabled());
    }

    // Note: Use either the specified override Material Data or the original draw calls state's Material Data to create a Surface Material if no override is specified
    const auto renderMaterialDataType = renderMaterialData.getType();
    std::optional<RtSurfaceMaterial> surfaceMaterial{};

    const bool hasTexcoords = drawCallState.hasTextureCoordinates();

    if (renderMaterialDataType == MaterialDataType::Legacy || renderMaterialDataType == MaterialDataType::Opaque) {
      uint32_t albedoOpacityTextureIndex = kSurfaceMaterialInvalidTextureIndex;
      uint32_t normalTextureIndex = kSurfaceMaterialInvalidTextureIndex;
      uint32_t tangentTextureIndex = kSurfaceMaterialInvalidTextureIndex;
      uint32_t roughnessTextureIndex = kSurfaceMaterialInvalidTextureIndex;
      uint32_t metallicTextureIndex = kSurfaceMaterialInvalidTextureIndex;
      uint32_t emissiveColorTextureIndex = kSurfaceMaterialInvalidTextureIndex;

      float anisotropy;
      float emissiveIntensity;
      Vector4 albedoOpacityConstant;
      float roughnessConstant;
      float metallicConstant;
      Vector3 emissiveColorConstant;
      bool enableEmissive;
      uint8_t spriteSheetRows;
      uint8_t spriteSheetCols;
      uint8_t spriteSheetFPS;
      bool thinFilmEnable = false;
      bool alphaIsThinFilmThickness = false;
      float thinFilmThicknessConstant = 0.0f;

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
          if(defaults.useAlbedoTextureIfPresent())
            trackTexture(ctx, legacyMaterialData.getColorTexture(), albedoOpacityTextureIndex, hasTexcoords, false); // NOTE: Do not patch original sampler
        }

        if (RtxOptions::Get()->getHighlightLegacyModeEnabled()) {
          enableEmissive = true;
          // Flash every 20 frames, bright
          emissiveIntensity = (sin((float) m_device->getCurrentFrameId()/20) + 1.f) * 2.f;
          emissiveColorConstant = Vector3(1, 0, 0); // Red
        }
        // Todo: Incorporate this and the color texture into emissive conditionally
        // emissiveColorTextureIndex != kSurfaceMaterialInvalidTextureIndex ? 100.0f

        spriteSheetRows = RtxOptions::Get()->getSharedMaterialDefaults().SpriteSheetRows;
        spriteSheetCols = RtxOptions::Get()->getSharedMaterialDefaults().SpriteSheetCols;
        spriteSheetFPS = RtxOptions::Get()->getSharedMaterialDefaults().SpriteSheetFPS;

        thinFilmEnable = defaults.enableThinFilm();
        alphaIsThinFilmThickness = defaults.alphaIsThinFilmThickness();
        thinFilmThicknessConstant = defaults.thinFilmThicknessConstant();
      } else if (renderMaterialDataType == MaterialDataType::Opaque) {
        const auto& opaqueMaterialData = renderMaterialData.getOpaqueMaterialData();

        anisotropy = RtxOptions::Get()->getOpaqueMaterialDefaults().Anisotropy;
        albedoOpacityConstant = RtxOptions::Get()->getOpaqueMaterialDefaults().AlbedoOpacityConstant;
        roughnessConstant = RtxOptions::Get()->getOpaqueMaterialDefaults().RoughnessConstant;
        metallicConstant = RtxOptions::Get()->getOpaqueMaterialDefaults().MetallicConstant;
        emissiveColorConstant = RtxOptions::Get()->getOpaqueMaterialDefaults().EmissiveColorConstant;
        
        enableEmissive = RtxOptions::Get()->getSharedMaterialDefaults().EnableEmissive;
        emissiveIntensity = RtxOptions::Get()->getSharedMaterialDefaults().EmissiveIntensity;

        if (RtxOptions::Get()->getWhiteMaterialModeEnabled()) {
          albedoOpacityConstant = kWhiteModeAlbedo;
          metallicConstant = 0.f;
          roughnessConstant = 1.f;
        } else {
          trackTexture(ctx, opaqueMaterialData.getAlbedoOpacityTexture(), albedoOpacityTextureIndex, hasTexcoords);
          trackTexture(ctx, opaqueMaterialData.getRoughnessTexture(), roughnessTextureIndex, hasTexcoords);
          trackTexture(ctx, opaqueMaterialData.getMetallicTexture(), metallicTextureIndex, hasTexcoords);

          albedoOpacityConstant = opaqueMaterialData.getAlbedoOpacityConstant();
          metallicConstant = opaqueMaterialData.getMetallicConstant();
          roughnessConstant = opaqueMaterialData.getRoughnessConstant();
        }

        trackTexture(ctx, opaqueMaterialData.getNormalTexture(), normalTextureIndex, hasTexcoords);
        trackTexture(ctx, opaqueMaterialData.getTangentTexture(), tangentTextureIndex, hasTexcoords);
        trackTexture(ctx, opaqueMaterialData.getEmissiveColorTexture(), emissiveColorTextureIndex, hasTexcoords);

        emissiveIntensity = opaqueMaterialData.getEmissiveIntensity();
        emissiveColorConstant = opaqueMaterialData.getEmissiveColorConstant();
        enableEmissive = opaqueMaterialData.getEnableEmission();
        anisotropy = opaqueMaterialData.getAnisotropy();
        spriteSheetRows = opaqueMaterialData.getSpriteSheetRows();
        spriteSheetCols = opaqueMaterialData.getSpriteSheetCols();
        spriteSheetFPS = opaqueMaterialData.getSpriteSheetFPS();
        
        thinFilmEnable = opaqueMaterialData.getEnableThinFilm();
        alphaIsThinFilmThickness = opaqueMaterialData.getAlphaIsThinFilmThickness();
        thinFilmThicknessConstant = opaqueMaterialData.getThinFilmThicknessConstant();
      }

      const RtOpaqueSurfaceMaterial opaqueSurfaceMaterial{
        albedoOpacityTextureIndex, normalTextureIndex,
        tangentTextureIndex, roughnessTextureIndex,
        metallicTextureIndex, emissiveColorTextureIndex,
        anisotropy, emissiveIntensity,
        albedoOpacityConstant,
        roughnessConstant, metallicConstant,
        emissiveColorConstant, enableEmissive,
        spriteSheetRows, spriteSheetCols, spriteSheetFPS,
        thinFilmEnable, alphaIsThinFilmThickness, thinFilmThicknessConstant
      };

      surfaceMaterial.emplace(opaqueSurfaceMaterial);
    } else if (renderMaterialDataType == MaterialDataType::Translucent) {
      const auto& translucentMaterialData = renderMaterialData.getTranslucentMaterialData();

      uint32_t normalTextureIndex = kSurfaceMaterialInvalidTextureIndex;
      float refractiveIndex = RtxOptions::Get()->getTranslucentMaterialDefaults().RefractiveIndex;
      uint32_t transmittanceTextureIndex = kSurfaceMaterialInvalidTextureIndex;
      Vector3 transmittanceColor = RtxOptions::Get()->getTranslucentMaterialDefaults().TransmittanceColor;
      float transmittanceMeasureDistance = RtxOptions::Get()->getTranslucentMaterialDefaults().TransmittanceMeasurementDistance;
      Vector3 emissiveColorConstant = RtxOptions::Get()->getTranslucentMaterialDefaults().EmissiveColorConstant;
      bool isThinWalled = RtxOptions::Get()->getTranslucentMaterialDefaults().ThinWalled;
      float thinWallThickness = RtxOptions::Get()->getTranslucentMaterialDefaults().ThinWallThickness;
      bool useDiffuseLayer = RtxOptions::Get()->getTranslucentMaterialDefaults().UseDiffuseLayer;

      bool enableEmissive = RtxOptions::Get()->getSharedMaterialDefaults().EnableEmissive;
      float emissiveIntensity = RtxOptions::Get()->getSharedMaterialDefaults().EmissiveIntensity;

      trackTexture(ctx, translucentMaterialData.getNormalTexture(), normalTextureIndex, hasTexcoords);

      refractiveIndex = translucentMaterialData.getRefractiveIndex();

      trackTexture(ctx, translucentMaterialData.getTransmittanceTexture(), transmittanceTextureIndex, hasTexcoords);

      transmittanceColor = translucentMaterialData.getTransmittanceColor();
      transmittanceMeasureDistance = translucentMaterialData.getTransmittanceMeasurementDistance();
      emissiveColorConstant = translucentMaterialData.getEmissiveColorConstant();
      enableEmissive = translucentMaterialData.getEnableEmission();
      emissiveIntensity = translucentMaterialData.getEmissiveIntensity();;
      isThinWalled = translucentMaterialData.getIsThinWalled();
      thinWallThickness = translucentMaterialData.getThinWallThickness();
      useDiffuseLayer = translucentMaterialData.getUseDiffuseLayer();

      const RtTranslucentSurfaceMaterial translucentSurfaceMaterial{
        normalTextureIndex, refractiveIndex,
        transmittanceMeasureDistance, transmittanceTextureIndex, transmittanceColor,
        enableEmissive, emissiveIntensity, emissiveColorConstant,
        isThinWalled, thinWallThickness, useDiffuseLayer
      };

      surfaceMaterial.emplace(translucentSurfaceMaterial);
    } else if (renderMaterialDataType == MaterialDataType::RayPortal) {
      const auto& rayPortalMaterialData = renderMaterialData.getRayPortalMaterialData();

      uint32_t maskTextureIndex = kSurfaceMaterialInvalidTextureIndex;
      trackTexture(ctx, rayPortalMaterialData.getMaskTexture(), maskTextureIndex, hasTexcoords, true, false);
      uint32_t maskTextureIndex2 = kSurfaceMaterialInvalidTextureIndex;
      trackTexture(ctx, rayPortalMaterialData.getMaskTexture2(), maskTextureIndex2, hasTexcoords, true, false);

      uint8_t rayPortalIndex = rayPortalMaterialData.getRayPortalIndex();
      uint8_t spriteSheetRows = rayPortalMaterialData.getSpriteSheetRows();
      uint8_t spriteSheetCols = rayPortalMaterialData.getSpriteSheetCols();
      uint8_t spriteSheetFPS = rayPortalMaterialData.getSpriteSheetFPS();
      float rotationSpeed = rayPortalMaterialData.getRotationSpeed();
      bool enableEmissive = rayPortalMaterialData.getEnableEmission();
      float emissiveIntensity = rayPortalMaterialData.getEmissiveIntensity();
      const RtRayPortalSurfaceMaterial rayPortalSurfaceMaterial(maskTextureIndex, maskTextureIndex2, rayPortalIndex, spriteSheetRows, spriteSheetCols, spriteSheetFPS, rotationSpeed, enableEmissive, emissiveIntensity);

      surfaceMaterial.emplace(rayPortalSurfaceMaterial);
    }

    assert(surfaceMaterial.has_value());

    // Cache this
    m_surfaceMaterialCache.track(*surfaceMaterial);

    RtInstance* instance = m_instanceManager.processSceneObject(m_cameraManager, m_rayPortalManager, *pBlas, drawCallState, renderMaterialData, *surfaceMaterial);

    // Check if a light should be created for this Material
    if (instance && RtxOptions::Get()->shouldConvertToLight(drawCallState.getMaterialData().getHash())) {
      createEffectLight(ctx, drawCallState, instance);
    }

    return instance ? instance->getId() : UINT64_MAX;
  }

  void SceneManager::finalizeAllPendingTexturePromotions() {
    ScopedCpuProfileZone();
    for (auto& texture : m_textureCache.getObjectTable())
      if (texture.isPromotable())
        texture.finalizePendingPromotion();
  }

  void SceneManager::addLight(const D3DLIGHT9& light) {
    ScopedCpuProfileZone();
    // Attempt to convert the D3D9 light to RT

    std::optional<RtLight> rtLight = RtLight::TryCreate(light);

    // Note: Skip adding this light if it is somehow malformed such that it could not be created.
    if (!rtLight) {
      return;
    }

    const std::vector<AssetReplacement>* pReplacements = m_pReplacer->getReplacementsForLight((*rtLight).getInitialHash());
    if (pReplacements) {
      // TODO(TREX-1091) to implement meshes as light replacements, replace the below loop with a call to drawReplacements.
      for (auto&& replacement : *pReplacements) {
        if (replacement.type == AssetReplacement::eLight) {          
          m_lightManager.addLight(replacement.lightData);
        } else {
          assert(false); // We don't support meshes as children of lights yet.
        }
      }
    } else {
      // This is a light coming from the game directly, so use the appropriate API for filter rules
      m_lightManager.addGameLight(light.Type, *rtLight);
    }
  }

  void SceneManager::prepareSceneData(Rc<DxvkContext> ctx, Rc<DxvkCommandList> cmdList, DxvkBarrierSet& execBarriers, const float frameTimeSecs) {
    ScopedGpuProfileZone(ctx, "Build Scene");

    // Needs to happen before garbageCollection to avoid destroying dynamic lights
    m_lightManager.dynamicLightMatching();

    garbageCollection();
    
    m_bindlessResourceManager.prepareSceneData(cmdList, getTextureTable(), getBufferTable());

    // If there are no instances, we should do nothing!
    if (m_instanceManager.getActiveCount() == 0) {
      // Clear the ray portal data before the next frame
      m_rayPortalManager.clear();
      return;
    }


    m_rayPortalManager.prepareSceneData(ctx, frameTimeSecs);
    // Note: only main camera needs to be teleportation corrected as only that one is used for ray tracing & denoising
    m_rayPortalManager.fixCameraInBetweenPortals(m_cameraManager.getMainCamera());
    m_rayPortalManager.createVirtualCameras(m_cameraManager);
    const bool didTeleport = m_rayPortalManager.detectTeleportationAndCorrectCameraHistory(m_cameraManager.getMainCamera());

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
    m_instanceManager.createViewModelInstances(ctx, cmdList, m_cameraManager, m_rayPortalManager);
    m_instanceManager.createPlayerModelVirtualInstances(ctx, m_cameraManager, m_rayPortalManager);

    m_accelManager.mergeInstancesIntoBlas(ctx, cmdList, execBarriers, m_textureCache.getObjectTable(), m_cameraManager, m_instanceManager, m_opacityMicromapManager.get(), frameTimeSecs);

    // Call on the other managers to prepare their GPU data for the current scene
    m_accelManager.prepareSceneData(ctx, cmdList, execBarriers, m_instanceManager);
    m_lightManager.prepareSceneData(ctx, m_cameraManager);

    // Build the TLAS
    m_accelManager.buildTlas(ctx, cmdList);

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

        int materialID = 0;
        for (auto&& surfaceMaterial : m_surfaceMaterialCache.getObjectTable()) {
          surfaceMaterial.writeGPUData(surfaceMaterialsGPUData.data(), dataOffset);
          materialID++;
        }

        assert(dataOffset == surfaceMaterialsGPUSize);
        assert(surfaceMaterialsGPUData.size() == surfaceMaterialsGPUSize);

        ctx->updateBuffer(m_surfaceMaterialBuffer, 0, surfaceMaterialsGPUData.size(), surfaceMaterialsGPUData.data());
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

        ctx->updateBuffer(m_volumeMaterialBuffer, 0, volumeMaterialsGPUData.size(), volumeMaterialsGPUData.data());
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
    m_device->statCounters().setCtr(DxvkStatCounter::RtxTextureCount, m_textureCache.getActiveCount());
    m_device->statCounters().setCtr(DxvkStatCounter::RtxInstanceCount, m_instanceManager.getActiveCount());
    m_device->statCounters().setCtr(DxvkStatCounter::RtxSurfaceMaterialCount, m_surfaceMaterialCache.getActiveCount());
    m_device->statCounters().setCtr(DxvkStatCounter::RtxVolumeMaterialCount, m_volumeMaterialCache.getActiveCount());
    m_device->statCounters().setCtr(DxvkStatCounter::RtxLightCount, m_lightManager.getActiveCount());
    
    if (m_device->getCurrentFrameId() == m_beginUsdExportFrameNum) {
      m_gameCapturer->toggleMultiFrameCapture();
    }
    m_gameCapturer->step(ctx, frameTimeSecs);

    // Clear the ray portal data before the next frame
    m_rayPortalManager.clear();
  }
  
  bool SceneManager::isGameCapturerIdle() const { return m_gameCapturer->isIdle(); }

  void SceneManager::triggerUsdCapture() const {
    m_gameCapturer->startNewSingleFrameCapture();
  }
}  // namespace nvvk
