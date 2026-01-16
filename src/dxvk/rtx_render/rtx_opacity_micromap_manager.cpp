/*
* Copyright (c) 2022-2025, NVIDIA CORPORATION. All rights reserved.
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
#include "rtx_opacity_micromap_manager.h"

#include "rtx.h"
#include "rtx_context.h"
#include "dxvk_device.h"
#include "dxvk_scoped_annotation.h"
#include "rtx_options.h"
#include "rtx_hash_collision_detection.h"
#include "rtx_texture_manager.h"

#include "rtx_imgui.h"

#include "../util/util_globaltime.h"

#include "rtx/pass/common_binding_indices.h"

// #define VALIDATION_MODE

#ifdef VALIDATION_MODE
#define omm_validation_assert(x) assert(x)
#else
#define omm_validation_assert(x)
#endif

const VkDeviceSize kBufferAlignment = 16;
const VkDeviceSize kBufferInBlasUsageAlignment = 256;

namespace dxvk {
  DxvkOpacityMicromap::DxvkOpacityMicromap(DxvkDevice& device) : m_vkd(device.vkd()) { }

  DxvkOpacityMicromap::~DxvkOpacityMicromap() {
    if (opacityMicromap != VK_NULL_HANDLE) {
      m_vkd->vkDestroyMicromapEXT(m_vkd->device(), opacityMicromap, nullptr);
      opacityMicromap = VK_NULL_HANDLE;
    }

    opacityMicromapTriangleIndexBuffer = nullptr;
    opacityMicromapBuffer = nullptr;
  }

  OpacityMicromapCacheItem::OpacityMicromapCacheItem() {
    // Default constructor is needed for [] access into OMM cache, but it must not be called
    // for a case when the cache item is not already present in the cache
    assert(0 && "Invalid state. Default constructor for OpacityMicromapCacheItem should never be called.");
    Logger::err("[RTX Opacity Micromap] Encountered inconsistent state. Default constructor for OpacityMicromapCacheItem should never be called.");
  }

  OpacityMicromapCacheItem::OpacityMicromapCacheItem(DxvkDevice& device,
                                                     OpacityMicromapCacheState _cacheState,
                                                     const uint32_t inputSubdivisionLevel,
                                                     const bool enableVertexAndTextureOperations,
                                                     uint32_t currentFrameIndex,
                                                     std::list<XXH64_hash_t>::iterator _leastRecentlyUsedListIter,
                                                     std::list<XXH64_hash_t>::iterator _cacheStateListIter,
                                                     const OmmRequest& ommRequest)
    : cacheState(_cacheState)
    , lastUseFrameIndex(currentFrameIndex)
    , leastRecentlyUsedListIter(_leastRecentlyUsedListIter)
    , cacheStateListIter(_cacheStateListIter)
    , isUnprocessedCacheStateListIterValid(true)
    , numTriangles(ommRequest.numTriangles)
    , ommFormat(ommRequest.ommFormat) {
    useVertexAndTextureOperations = enableVertexAndTextureOperations;
    const uint32_t maxSubdivisionLevel =
      ommFormat == VkOpacityMicromapFormatEXT::VK_OPACITY_MICROMAP_FORMAT_2_STATE_EXT
      ? device.properties().extOpacityMicromapProperties.maxOpacity2StateSubdivisionLevel
      : device.properties().extOpacityMicromapProperties.maxOpacity4StateSubdivisionLevel;
    subdivisionLevel = std::min(inputSubdivisionLevel, maxSubdivisionLevel);
  }

  bool OpacityMicromapCacheItem::isCompatibleWithOmmRequest(const OmmRequest& ommRequest) {
    return ommRequest.ommFormat == ommFormat &&
           ommRequest.numTriangles == numTriangles;
  }

  VkDeviceSize OpacityMicromapCacheItem::getDeviceSize() const {
    return blasOmmBuffersDeviceSize + arrayBufferDeviceSize;
  }

  OpacityMicromapMemoryManager::OpacityMicromapMemoryManager(DxvkDevice* device)
    : CommonDeviceObject(device)
    , m_memoryProperties(device->adapter()->memoryProperties()) {
    // +1 to account for OMMs used in a previous TLAS
    const uint32_t kMaxFramesOMMResourcesAreUsed = kMaxFramesInFlight + (RtxOptions::enablePreviousTLAS() ? 1u : 0u);

    for (uint32_t i = 0; i < kMaxFramesOMMResourcesAreUsed; i++) {
      m_pendingReleaseSize.push_front(0);
    }
  }

  void OpacityMicromapMemoryManager::onFrameStart() {
    VkDeviceSize sizeToRelease = std::min(m_pendingReleaseSize.back(), m_used);
    m_pendingReleaseSize.pop_back();
    m_pendingReleaseSize.push_front(0);

    m_used -= sizeToRelease;
  }

  void OpacityMicromapMemoryManager::registerVidmemFreeSize() {
    // Gather runtime vidmem stats
    VkDeviceSize vidmemSize = 0;
    VkDeviceSize vidmemUsedSize = 0;

    DxvkAdapterMemoryInfo memHeapInfo = m_device->adapter()->getMemoryHeapInfo();
    DxvkMemoryAllocator& memoryManager = m_device->getCommon()->memoryManager();
    const VkPhysicalDeviceMemoryProperties& memoryProperties = memoryManager.getMemoryProperties();

    for (uint32_t i = 0; i < memoryProperties.memoryHeapCount; i++) {
      if (memoryProperties.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) {
        vidmemSize += memHeapInfo.heaps[i].memoryBudget;
        vidmemUsedSize += memHeapInfo.heaps[i].memoryAllocated;
      }
    }

    m_vidmemFreeSize = vidmemSize - std::min(vidmemUsedSize, vidmemSize);
  }

  void OpacityMicromapMemoryManager::updateMemoryBudget(Rc<DxvkContext> ctx) {

    // Gather runtime vidmem stats
    VkDeviceSize vidmemSize = 0;
    DxvkAdapterMemoryInfo memHeapInfo = m_device->adapter()->getMemoryHeapInfo();
    DxvkMemoryAllocator& memoryManager = m_device->getCommon()->memoryManager();
    const VkPhysicalDeviceMemoryProperties& memoryProperties = memoryManager.getMemoryProperties();

    for (uint32_t i = 0; i < memoryProperties.memoryHeapCount; i++) {
      if (memoryProperties.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) {
        vidmemSize += memHeapInfo.heaps[i].memoryBudget;
      }
    }

    // Consider the smalleer free VidMem size reported now and at the end of last frame.
    // End of last mem stats often account for more intra frame allocations, but updateMemoryBudget()
    // is called at the start of the frame to adjust OMM budget before any OMM allocs happen in the frame
    const VkDeviceSize prevEndOfFrameVidmemFreeSize = m_vidmemFreeSize;
    registerVidmemFreeSize();
    if (prevEndOfFrameVidmemFreeSize != kInvalidDeviceSize) {
      m_vidmemFreeSize = std::min(m_vidmemFreeSize, prevEndOfFrameVidmemFreeSize);
    }

    double maxVidmemSizePercentage = static_cast<double>(OpacityMicromapOptions::Cache::maxVidmemSizePercentage());

    // Halve the max budget when using a low mem GPU
    if (RtxOptions::lowMemoryGpu()) {
      maxVidmemSizePercentage /= 2;
    }

    // Calculate a new budget given the runtime vidmem stats

    VkDeviceSize maxBudget =
      std::min(static_cast<VkDeviceSize>(maxVidmemSizePercentage * vidmemSize),
              static_cast<VkDeviceSize>(OpacityMicromapOptions::Cache::maxBudgetSizeMB()) * 1024 * 1024);

    const VkDeviceSize hardMinFreeVidmemToNotAllocate = static_cast<VkDeviceSize>(OpacityMicromapOptions::Cache::minFreeVidmemMBToNotAllocate()) * 1024 * 1024;
    const VkDeviceSize softMinFreeVidmemToNotAllocate = hardMinFreeVidmemToNotAllocate + static_cast<VkDeviceSize>(OpacityMicromapOptions::Cache::freeVidmemMBBudgetBuffer()) * 1024 * 1024;
    
    m_prevBudget = m_budget;

    // Recalculate budget if free memory dropped below the hard limit or is over the soft limit
    if (m_vidmemFreeSize < hardMinFreeVidmemToNotAllocate || m_vidmemFreeSize > softMinFreeVidmemToNotAllocate) {
      m_budget = std::min(m_vidmemFreeSize - std::min(softMinFreeVidmemToNotAllocate, m_vidmemFreeSize) + static_cast<VkDeviceSize>(m_used), maxBudget);
    }

    if (m_budget < static_cast<VkDeviceSize>(OpacityMicromapOptions::Cache::minBudgetSizeMB()) * 1024 * 1024) {
      m_budget = 0;
    }
  
    if (m_budget != m_prevBudget && m_budget == 0) {
      ONCE(Logger::info("[RTX Opacity Micromap] Free Vidmem dropped below a limit. Setting budget to 0."));
    }

    // Invalidate m_vidmemFreeSize to make sure we use it only when it was set at the end of the frame again
    m_vidmemFreeSize = kInvalidDeviceSize;
  }

  bool OpacityMicromapMemoryManager::allocate(VkDeviceSize size) {
    if (size > getAvailable()) {
      ONCE(Logger::info(str::format("[RTX Opacity Micromap] Out of memory budget. Requested: ", size, " bytes. Free: ", getAvailable(), " bytes, Budget: ", getBudget(), " bytes")));
      return false;
    }

    m_used += size;

    return true;
  }

  VkDeviceSize OpacityMicromapMemoryManager::getAvailable() const {
    return m_budget - std::min(m_used, m_budget);
  }

  void OpacityMicromapMemoryManager::release(VkDeviceSize size) {
    m_pendingReleaseSize.back() += size;
  }

  void OpacityMicromapMemoryManager::releaseAll() {
    release(m_used);
  }

  VkDeviceSize OpacityMicromapMemoryManager::getPrevBudget() const {
    return m_prevBudget;
  }

  float OpacityMicromapMemoryManager::calculateUsageRatio() const {
    return m_used / static_cast<float>(m_budget);
  }

  VkDeviceSize OpacityMicromapMemoryManager::calculatePendingAvailableSize() const {
    return std::min(getAvailable() + calculatePendingReleasedSize(), m_budget);
  }

  VkDeviceSize OpacityMicromapMemoryManager::calculatePendingReleasedSize() const {
    VkDeviceSize totalSizeToRelease = 0;
    for (auto& sizeToRelease : m_pendingReleaseSize)
      totalSizeToRelease += sizeToRelease;

    return totalSizeToRelease;
  }

  VkDeviceSize OpacityMicromapMemoryManager::getNextPendingReleasedSize() const {
    return m_pendingReleaseSize.back();
  }

  Rc<DxvkBuffer> OpacityMicromapManager::getScratchMemory(const size_t requiredScratchAllocSize) {
    if (m_scratchBuffer == nullptr || m_scratchBuffer->info().size < requiredScratchAllocSize) {
      DxvkBufferCreateInfo bufferCreateInfo {};
      bufferCreateInfo.size = requiredScratchAllocSize;
      bufferCreateInfo.access = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
      bufferCreateInfo.stages = VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR;
      bufferCreateInfo.usage = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
      m_scratchBuffer = device()->createBuffer(bufferCreateInfo, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, DxvkMemoryStats::Category::RTXAccelerationStructure, "OMM Scratch");
    }

    return m_scratchBuffer;
  }

  OpacityMicromapManager::OpacityMicromapManager(DxvkDevice* device)
    : CommonDeviceObject(device)
    , m_memoryManager(device) {
  }

  OpacityMicromapManager::~OpacityMicromapManager() { 
#ifdef VALIDATION_MODE
    // Delink instances so that the assert on cache data destruction doesn't trigger
    for (auto& sourceData : m_cachedSourceData) {
      sourceData.second.setInstance(nullptr, m_instanceOmmRequests, *this);
    }
#endif
  }

  void OpacityMicromapManager::onDestroy() {
  }

  OmmRequest::OmmRequest(const RtInstance& _instance, const InstanceManager& instanceManager, uint32_t _quadSliceIndex)
    : instance(_instance)
    , quadSliceIndex(_quadSliceIndex) {

    OpacityMicromapHashSourceData hashSourceData;

    // Fill material properties
    {
      hashSourceData.materialHash = instance.getMaterialHash();
      hashSourceData.alphaState = instance.surface.alphaState;
      hashSourceData.textureColorArg1Source = instance.surface.textureColorArg1Source;
      hashSourceData.textureColorArg2Source = instance.surface.textureColorArg2Source;
      hashSourceData.textureColorOperation = instance.surface.textureColorOperation;
      hashSourceData.textureAlphaArg1Source = instance.surface.textureAlphaArg1Source;
      hashSourceData.textureAlphaArg2Source = instance.surface.textureAlphaArg2Source;
      hashSourceData.textureAlphaOperation = instance.surface.textureAlphaOperation;
      hashSourceData.tFactorAlpha = instance.surface.tFactor >> 24;
      hashSourceData.textureTransform = instance.surface.textureTransform;
    }

    if (isBillboardOmmRequest()) {
      hashSourceData.numTriangles = 2;

      const IntersectionBillboard& billboard = instanceManager.getBillboards()[instance.getFirstBillboardIndex() + quadSliceIndex];
      hashSourceData.texCoordHash = billboard.texCoordHash;
      hashSourceData.vertexOpacityHash = billboard.vertexOpacityHash;

      // Index hash is not explicitly included for billboards as it's already part of texcoordHash,
      // which is generated using actual triangle order in a billboard quad
    }
    else {
      hashSourceData.numTriangles = instance.getBlas()->modifiedGeometryData.calculatePrimitiveCount();
      hashSourceData.texCoordHash = instance.getTexcoordHash();
      hashSourceData.indexHash = instance.getIndexHash();
      // ToDo add vertexOpacityHash
    }

    // Select OmmFormat for the OMM request
    {
      hashSourceData.ommFormat = VK_OPACITY_MICROMAP_FORMAT_4_STATE_EXT;

      auto& alphaState = instance.surface.alphaState;

      if (OpacityMicromapOptions::Building::allow2StateOpacityMicromaps() && (
        isBillboardOmmRequest() ||
        (!alphaState.isFullyOpaque && (alphaState.isParticle || alphaState.isDecal)) || alphaState.emissiveBlend))
        hashSourceData.ommFormat = VK_OPACITY_MICROMAP_FORMAT_2_STATE_EXT;

      if (OpacityMicromapOptions::Building::force2StateOpacityMicromaps())
        hashSourceData.ommFormat = VK_OPACITY_MICROMAP_FORMAT_2_STATE_EXT;
    }

    if (OpacityMicromapOptions::Cache::hashInstanceIndexOnly()) {
      ommSrcHash = instance.getId();
    }
    else { // Generate a hash from the gathered source data
      ommSrcHash = XXH3_64bits(&hashSourceData, sizeof(hashSourceData));

      HashCollisionDetection::registerHashedSourceData(ommSrcHash, static_cast<void*>(&hashSourceData), HashSourceDataCategory::OpacityMicromap);
    }

    numTriangles = hashSourceData.numTriangles;
    ommFormat = hashSourceData.ommFormat;
  }

  OpacityMicromapManager::CachedSourceData::~CachedSourceData() {
    omm_validation_assert(!instance && "Instance has not been unlinked");
  }

  void OpacityMicromapManager::CachedSourceData::initialize(const OmmRequest& ommRequest, fast_unordered_cache<InstanceOmmRequests>& instanceOmmRequests, OpacityMicromapManager& ommManager) {
    setInstance(&ommRequest.instance, instanceOmmRequests, ommManager);

    numTriangles = ommRequest.numTriangles;

    if (ommRequest.isBillboardOmmRequest()) {
      // ToDo: add compiler check support to ensure the right values are specified here
      triangleOffset = 2 * ommRequest.quadSliceIndex;
    } else {
      triangleOffset = 0;
    }
  }

  void OpacityMicromapManager::CachedSourceData::setInstance(const RtInstance* newInstance, fast_unordered_cache<InstanceOmmRequests>& instanceOmmRequests, OpacityMicromapManager& ommManager, bool deleteParentInstanceIfEmpty) {
    omm_validation_assert(instance != newInstance && "Redundant call setting the same instance twice.");

    if (instance && newInstance) {
      setInstance(nullptr, instanceOmmRequests, ommManager, deleteParentInstanceIfEmpty);
    }

    if (newInstance) {
      OpacityMicromapInstanceData& newOmmInstanceData = getOmmInstanceData(*newInstance);

      instanceOmmRequests[newOmmInstanceData.ommSrcHash].numActiveRequests += 1;

      // Request numTexelsPerMicroTriangle to be calculated.
      // Note: this may get set to true even after the data was calculated,
      //   but that is OK as the data will not be calculated twice 
      //   since it's checked for being available first then
      newOmmInstanceData.needsToCalculateNumTexelsPerMicroTriangle = true;
    }
    // instance should always be valid at this point, but let's check on previous instance being actually valid before unlinking it
    else if (instance) {
      auto instanceOmmRequestsIter = instanceOmmRequests.find(getOpacityMicromapHash(*instance));
      omm_validation_assert(instanceOmmRequestsIter->second.numActiveRequests > 0);
      instanceOmmRequestsIter->second.numActiveRequests -= 1;
      if (deleteParentInstanceIfEmpty && instanceOmmRequestsIter->second.numActiveRequests == 0) {
        instanceOmmRequests.erase(instanceOmmRequestsIter);
      }

      ommManager.onInstanceUnlinked(*instance);
    }

    instance = newInstance;
  }

  void OpacityMicromapManager::onInstanceUnlinked(const RtInstance& instance) {
    OpacityMicromapInstanceData& ommInstanceData = getOmmInstanceData(instance);

    // Make sure to set the request to false, since the calculations are throttled 
    // and it's possible the calculation doesn't complete prior to instance being unlinked 
    // (i.e. due to linked OMM cache items getting destroyed)
    ommInstanceData.needsToCalculateNumTexelsPerMicroTriangle = false;

    // Delete staging numTexelsPerMicroTriangle data associated with the instance
    if (useStagingNumTexelsPerMicroTriangleObject(instance)) {
      m_numTexelsPerMicroTriangleStaging.erase(&instance);
    } else {
      omm_validation_assert(m_numTexelsPerMicroTriangleStaging.find(&instance) == m_numTexelsPerMicroTriangleStaging.end());
    }
  }

  void OpacityMicromapManager::destroyOmmData(OpacityMicromapCache::iterator ommCacheItemIter, bool destroyParentInstanceOmmRequestContainer) {
    const XXH64_hash_t ommSrcHash = ommCacheItemIter->first;
    OpacityMicromapCacheItem& ommCacheItem = ommCacheItemIter->second;
    const OpacityMicromapCacheState ommCacheState = ommCacheItem.cacheState;

#ifdef VALIDATION_MODE
    Logger::warn(str::format("[RTX Opacity Micromap] Destroying ", ommSrcHash, " on thread_id ", std::this_thread::get_id()));
#endif

    switch (ommCacheState) {
    case OpacityMicromapCacheState::eStep0_Unprocessed:
    case OpacityMicromapCacheState::eStep1_Baking:
      // Note the iterator may be invalid if the cache state list element was
      // already destroyed when source data was unlinked
      if (ommCacheItem.isUnprocessedCacheStateListIterValid) {
        m_unprocessedList.erase(ommCacheItem.cacheStateListIter);
        ommCacheItem.isUnprocessedCacheStateListIterValid = false;
      }
      m_numTexelsPerMicroTriangle.erase(ommSrcHash);
      break;
    case OpacityMicromapCacheState::eStep2_Baked:
      m_bakedList.erase(ommCacheItem.cacheStateListIter);
      break;
    case OpacityMicromapCacheState::eStep3_Built:
      m_builtList.erase(ommCacheItem.cacheStateListIter);
      break;
    case OpacityMicromapCacheState::eStep4_Ready:
      break;
    default:
      omm_validation_assert(0);
      break;
    }

    if (ommCacheState <= OpacityMicromapCacheState::eStep2_Baked)
      deleteCachedSourceData(ommSrcHash, ommCacheState, destroyParentInstanceOmmRequestContainer);

    m_leastRecentlyUsedList.erase(ommCacheItemIter->second.leastRecentlyUsedListIter);
    m_memoryManager.release(ommCacheItemIter->second.getDeviceSize());
    m_ommCache.erase(ommCacheItemIter);
  }

  void OpacityMicromapManager::destroyOmmData(XXH64_hash_t ommSrcHash) {
    destroyOmmData(m_ommCache.find(ommSrcHash));
  }

  void OpacityMicromapManager::destroyInstance(const RtInstance& instance, bool forceDestroy) {
    // Don't destroy the container as it's being used to iterate through below
    const bool destroyParentInstanceOmmRequestContainer = false;

    auto destroyCachedData = [&](XXH64_hash_t ommSrcHash) {
      auto ommCacheIterator = m_ommCache.find(ommSrcHash);

      // Unknown element, ignore it
      if (ommCacheIterator == m_ommCache.end())
        return;

      OpacityMicromapCacheItem& ommCacheItem = ommCacheIterator->second;
      const OpacityMicromapCacheState ommCacheState = ommCacheItem.cacheState;

      if (!forceDestroy) {
        switch (ommCacheState) {
          // Continue with destruction of unbaked items
        case OpacityMicromapCacheState::eStep0_Unprocessed:
          break;

          // If the OMM data has been at least partially baked keep it in the cache
        case OpacityMicromapCacheState::eStep1_Baking:
          // Remove partially baked OMM items from to be baked list until a new instance is linked with it again
          if (ommCacheItem.isUnprocessedCacheStateListIterValid) {
            m_unprocessedList.erase(ommCacheItem.cacheStateListIter);
            ommCacheItem.isUnprocessedCacheStateListIterValid = false;
            deleteCachedSourceData(ommSrcHash, ommCacheState, destroyParentInstanceOmmRequestContainer);
          }
          return;
        case OpacityMicromapCacheState::eStep2_Baked:
        case OpacityMicromapCacheState::eStep3_Built:
        case OpacityMicromapCacheState::eStep4_Ready:
          return;

        default:
          // Continue with destruction
          omm_validation_assert(0);
          break;
        }
      }

      // Note: invalidates the omm cache iterator
      destroyOmmData(ommCacheIterator, destroyParentInstanceOmmRequestContainer);
    };

    m_numTexelsPerMicroTriangleStaging.erase(&instance);

    // Destroy all OMM requests associated with the instance
    XXH64_hash_t ommSrcHash = getOpacityMicromapHash(instance);
    if (ommSrcHash != kEmptyHash) {
      auto instanceOmmRequestsIter = m_instanceOmmRequests.find(ommSrcHash);

      if (instanceOmmRequestsIter != m_instanceOmmRequests.end()) {
        static_assert(destroyParentInstanceOmmRequestContainer == false);
        for (auto& ommRequest : instanceOmmRequestsIter->second.ommRequests)
          destroyCachedData(ommRequest.ommSrcHash);

        m_instanceOmmRequests.erase(instanceOmmRequestsIter);
      }
    }
  }

  uint32_t OpacityMicromapManager::calculateNumMicroTriangles(uint16_t subdivisionLevel) {
    return static_cast<uint32_t>(round(pow(4, subdivisionLevel)));
  }

  void OpacityMicromapManager::clear() {
    m_unprocessedList.clear();
    m_bakedList.clear();
    m_builtList.clear();

    m_leastRecentlyUsedList.clear();
    m_ommCache.clear();

#ifdef VALIDATION_MODE
    // Delink instances so that the assert on cache data destruction doesn't trigger
    for (auto& sourceData : m_cachedSourceData) {
      sourceData.second.setInstance(nullptr, m_instanceOmmRequests, *this);
    }
#endif
    m_cachedSourceData.clear();
    m_ommBuildRequestStatistics.clear();

    m_numTexelsPerMicroTriangleStaging.clear();
    m_numTexelsPerMicroTriangle.clear();

    m_instanceOmmRequests.clear();

    m_memoryManager.releaseAll();
    m_amountOfMemoryMissing = 0;

    // There's no need to clear m_blackListedList
  }

  void OpacityMicromapManager::showImguiSettings() const {

    const static ImGuiSliderFlags sliderFlags = ImGuiSliderFlags_AlwaysClamp;

#define ADVANCED(x) if (OpacityMicromapOptions::showAdvancedOptions()) x

    RemixGui::Checkbox("Show Advanced Settings", &OpacityMicromapOptions::showAdvancedOptionsObject());
    RemixGui::Checkbox("Enable Binding", &OpacityMicromapOptions::enableBindingObject());
    ADVANCED(RemixGui::Checkbox("Enable Baking Arrays", &OpacityMicromapOptions::enableBakingArraysObject()));
    ADVANCED(RemixGui::Checkbox("Enable Building", &OpacityMicromapOptions::enableBuildingObject()));

    RemixGui::Checkbox("Reset Every Frame", &OpacityMicromapOptions::enableResetEveryFrameObject());

    // Stats
    if (RemixGui::CollapsingHeader("Statistics", ImGuiTreeNodeFlags_DefaultOpen)) {
      ImGui::Indent();
      ImGui::Text("# Bound/Requested OMMs: %d/%d", m_numBoundOMMs, m_numRequestedOMMBindings);
      ADVANCED(ImGui::Text("# Staged Requested Items: %d", m_ommBuildRequestStatistics.size()));
      ADVANCED(ImGui::Text("# Unprocessed Items: %d", m_unprocessedList.size()));
      ADVANCED(ImGui::Text("# Baked Items: %d", m_bakedList.size()));
      ADVANCED(ImGui::Text("# Built Items: %d", m_builtList.size()));
      ADVANCED(ImGui::Text("# Cache Items: %d", m_ommCache.size()));
      ADVANCED(ImGui::Text("# Black Listed Items: %d", m_blackListedList.size()));
      ImGui::Text("VRAM usage/budget [MB]: %d/%d", m_memoryManager.getUsed() / (1024 * 1024), m_memoryManager.getBudget() / (1024 * 1024));

      ADVANCED(ImGui::Text("# Baked uTriagles [million]: %.1f", m_numMicroTrianglesBaked / 1e6));

      ADVANCED(ImGui::Text("# Built uTriagles [million]: %.1f", m_numMicroTrianglesBuilt / 1e6));
      ImGui::Unindent();
    }

    ADVANCED(
      if (RemixGui::CollapsingHeader("Scene")) {
        ImGui::Indent();
        ImGui::Unindent();
      });

    if (RemixGui::CollapsingHeader("Cache")) {
      ImGui::Indent();
      RemixGui::DragFloat("Budget: Max Vidmem Size %", &OpacityMicromapOptions::Cache::maxVidmemSizePercentageObject(), 0.001f, 0.0f, 1.f, "%.3f", sliderFlags);
      ADVANCED(RemixGui::DragInt("Budget: Min Required Size [MB]", &OpacityMicromapOptions::Cache::minBudgetSizeMBObject(), 8.f, 0, 256 * 1024, "%d", sliderFlags));
      RemixGui::DragInt("Budget: Max Allowed Size [MB]", &OpacityMicromapOptions::Cache::maxBudgetSizeMBObject(), 8.f, 0, 256 * 1024, "%d", sliderFlags);
      RemixGui::DragInt("Budget: Min Vidmem Free To Not Allocate [MB]", &OpacityMicromapOptions::Cache::minFreeVidmemMBToNotAllocateObject(), 16.f, 0, 256 * 1024, "%d", sliderFlags);
      ADVANCED(RemixGui::DragInt("Min Usage Frame Age Before Eviction", &OpacityMicromapOptions::Cache::minUsageFrameAgeBeforeEvictionObject(), 1.f, 0, 60 * 3600, "%d", sliderFlags));
      ADVANCED(RemixGui::Checkbox("Hash Instance Index Only", &OpacityMicromapOptions::Cache::hashInstanceIndexOnlyObject()));
      ImGui::Unindent();
    }


    if (RemixGui::CollapsingHeader("Requests Filter")) {
      ImGui::Indent();
      RemixGui::Checkbox("Enable Filtering", &OpacityMicromapOptions::BuildRequests::filteringObject());
      RemixGui::Checkbox("Animated Instances", &OpacityMicromapOptions::BuildRequests::enableAnimatedInstancesObject());
      RemixGui::Checkbox("Particles", &OpacityMicromapOptions::BuildRequests::enableParticlesObject());
      ADVANCED(RemixGui::Checkbox("Custom Filters for Billboards", &OpacityMicromapOptions::BuildRequests::customFiltersForBillboardsObject()));

      ADVANCED(RemixGui::DragInt("Max Staged Requests", &OpacityMicromapOptions::BuildRequests::maxRequestsObject(), 1.f, 1, 1000 * 1000, "%d", sliderFlags));
      // ToDo: we don't support setting this to 0 at the moment, should revisit later
      ADVANCED(RemixGui::DragInt("Min Instance Frame Age", &OpacityMicromapOptions::BuildRequests::minInstanceFrameAgeObject(), 1.f, 0, 200, "%d", sliderFlags));
      ADVANCED(RemixGui::DragInt("Min Num Frames Requested", &OpacityMicromapOptions::BuildRequests::minNumFramesRequestedObject(), 1.f, 0, 200, "%d", sliderFlags));
      ADVANCED(RemixGui::DragInt("Max Request Frame Age", &OpacityMicromapOptions::BuildRequests::maxRequestFrameAgeObject(), 1.f, 0, 60 * 3600, "%d", sliderFlags));
      ADVANCED(RemixGui::DragInt("Min Num Requests", &OpacityMicromapOptions::BuildRequests::minNumRequestsObject(), 1.f, 1, 1000, "%d", sliderFlags));
      ImGui::Unindent();
    }

    if (RemixGui::CollapsingHeader("Building")) {
      ImGui::Indent();

      RemixGui::Checkbox("Split Billboard Geometry", &OpacityMicromapOptions::Building::splitBillboardGeometryObject());
      RemixGui::DragInt("Max Allowed Billboards Per Instance To Split", &OpacityMicromapOptions::Building::maxAllowedBillboardsPerInstanceToSplitObject(), 1.f, 0, 4096, "%d", sliderFlags);

      // Note: 2 is minimum to ensure # micro triangle size is a multiple of 1 byte to ensure cross triangle alignment requirement
      RemixGui::DragInt("Subdivision Level", &OpacityMicromapOptions::Building::subdivisionLevelObject(), 1.f, 2, 11, "%d", sliderFlags);
      ADVANCED(RemixGui::Checkbox("Vertex, Texture Ops & Emissive Blending", &OpacityMicromapOptions::Building::enableVertexAndTextureOperationsObject()));
      ADVANCED(RemixGui::Checkbox("Allow 2 State Opacity Micromaps", &OpacityMicromapOptions::Building::allow2StateOpacityMicromapsObject()));
      ADVANCED(RemixGui::Checkbox("Force 2 State Opacity Micromaps", &OpacityMicromapOptions::Building::force2StateOpacityMicromapsObject()));

      ADVANCED(RemixGui::DragFloat("Decals: Min Resolve Transparency Threshold", &OpacityMicromapOptions::Building::decalsMinResolveTransparencyThresholdObject(), 0.001f, 0.0f, 1.f, "%.3f", sliderFlags));

      ADVANCED(RemixGui::DragInt("Max # of uTriangles to Bake [Million per Second]", &OpacityMicromapOptions::Building::maxMicroTrianglesToBakeMillionPerSecondObject(), 1.f, 1, 65536, "%d", sliderFlags));
      ADVANCED(RemixGui::DragInt("Max # of uTriangles to Build [Million per Second]", &OpacityMicromapOptions::Building::maxMicroTrianglesToBuildMillionPerSecondObject(), 1.f, 1, 65536, "%d", sliderFlags));
      ADVANCED(RemixGui::DragInt("# Frames with High Workload Multiplier at Start", &OpacityMicromapOptions::Building::numFramesAtStartToBuildWithHighWorkloadObject(), 1.f, 0, 100000, "%d", sliderFlags));
      ADVANCED(RemixGui::DragInt("High Workload Multiplier", &OpacityMicromapOptions::Building::highWorkloadMultiplierObject(), 1.f, 1, 1000, "%d", sliderFlags));

      if (RemixGui::CollapsingHeader("Conservative Estimation")) {
        ImGui::Indent();
        RemixGui::Checkbox("Enable", &OpacityMicromapOptions::Building::ConservativeEstimation::enableObject());
        ADVANCED(RemixGui::DragInt("Max Texel Taps Per uTriangle", &OpacityMicromapOptions::Building::ConservativeEstimation::maxTexelTapsPerMicroTriangleObject(), 16.f, 1, 256 * 256, "%d", sliderFlags);
        ImGui::Unindent());
      }

      ImGui::Unindent();
    }
  }

  void OpacityMicromapManager::logStatistics() const {
    Logger::info(str::format(
      "[RTX Opacity Micromap] Statistics:\n",
      "\t# Bound/Requested OMMs: ", m_numBoundOMMs, "/", m_numRequestedOMMBindings, "\n",
      "\t# Staged Requested Items: ", m_ommBuildRequestStatistics.size(), "\n",
      "\t# Unprocessed Items: ", m_unprocessedList.size(), "\n",
      "\t# Baked Items: ", m_bakedList.size(), "\n",
      "\t# Built Items: ", m_builtList.size(), "\n",
      "\t# Cache Items: ", m_ommCache.size(), "\n",
      "\t# Black Listed Items: ", m_blackListedList.size(), "\n",
      "\tVRAM usage/budget [MB]: ", m_memoryManager.getUsed() / (1024 * 1024), "/", m_memoryManager.getBudget() / (1024 * 1024)));
  }

  bool OpacityMicromapManager::checkIsOpacityMicromapSupported(DxvkDevice& device) {
    bool isOpacityMicromapSupported = device.extensions().khrSynchronization2 &&
                                      device.extensions().extOpacityMicromap;

    if (RtxOptions::areValidationLayersEnabled() && isOpacityMicromapSupported) {
      Logger::warn(str::format("[RTX] Opacity Micromap vendor extension is not compatible with VK Validation Layers. Disabling Opacity Micromap extension."));
      isOpacityMicromapSupported = false;
    }

    return isOpacityMicromapSupported;
  }


  InstanceEventHandler OpacityMicromapManager::getInstanceEventHandler() {
    InstanceEventHandler instanceEvents(this);
    instanceEvents.onInstanceAddedCallback = [this](const RtInstance& instance) { onInstanceAdded(instance); };
    instanceEvents.onInstanceAddedCallback = [this](const RtInstance& instance) { onInstanceAdded(instance); };
    instanceEvents.onInstanceUpdatedCallback = [this](const RtInstance& instance, const DrawCallState& drawCall, const MaterialData& material, bool hasTransformChanged, bool hasVerticesChanged, bool isFirstUpdateThisFrame) { onInstanceUpdated(instance, drawCall, material, hasTransformChanged, hasVerticesChanged, isFirstUpdateThisFrame); };
    instanceEvents.onInstanceDestroyedCallback = [this](const RtInstance& instance) { onInstanceDestroyed(instance); };
    return instanceEvents;
  }

  void OpacityMicromapManager::onInstanceAdded(const RtInstance& instance) {
    // Do nothing, intra-frame submission OMM work is done on onInstanceUpdated()
  }

  // Requires registerOpacityMicromapBuildRequest() to be be called prior to this in a frame
  bool OpacityMicromapManager::usesOpacityMicromap(const RtInstance& instance) {
    const OpacityMicromapInstanceData& ommInstanceData = instance.getOpacityMicromapInstanceData();

    return ommInstanceData.usesOMM;
  }

  bool OpacityMicromapManager::usesSplitBillboardOpacityMicromap(const RtInstance& instance) {
    return
      OpacityMicromapOptions::Building::splitBillboardGeometry() &&
      // ToDo: this should be "> 1" since it is wasteful to split 1 billboard geos 
      // but doing so prevents OMM getting applied to a particle for portal gun diode on top,
      // so leaving it at "> 0" for now
      instance.getBillboardCount() > 0 &&
      instance.getBillboardCount() <= OpacityMicromapOptions::Building::maxAllowedBillboardsPerInstanceToSplit();
  }

  bool OpacityMicromapManager::useStagingNumTexelsPerMicroTriangleObject(const RtInstance& instance) {
    return instance.getFrameAge() == 0 && OpacityMicromapManager::usesSplitBillboardOpacityMicromap(instance);
  }

  XXH64_hash_t OpacityMicromapManager::getOpacityMicromapHash(const RtInstance& instance) {
    const OpacityMicromapInstanceData& ommInstanceData = instance.getOpacityMicromapInstanceData();
    return ommInstanceData.ommSrcHash;
  }

  OpacityMicromapInstanceData::OpacityMicromapInstanceData()
    : usesOMM(false)
    , needsToCalculateNumTexelsPerMicroTriangle(false) { }

  OpacityMicromapInstanceData& OpacityMicromapManager::getOmmInstanceData(const RtInstance& instance) {
    // OMM Instance Data is managed by OMM manager but stored in an instance to avoid indirect lookups. 
    // RtInstance is generally passed via const reference into OMM as often nothing else needs to be modified,
    // but OMM manager still needs to be able to modify the OMM instance data. So we remove the constness here
    return const_cast<OpacityMicromapInstanceData&>(instance.getOpacityMicromapInstanceData());
  }

  void OpacityMicromapManager::onInstanceUpdated(const RtInstance& instance,
                                                 const DrawCallState& /*drawCall*/,
                                                 const MaterialData& /*material*/,
                                                 const bool hasTransformChanged,
                                                 const bool hasVerticesChanged,
                                                 const bool isFirstUpdateThisFrame) {
    ScopedCpuProfileZone();

    // Skip calculating data needed for new OMMs if there's not enough memory to build any OMM request
    if (!m_hasEnoughMemoryToPotentiallyGenerateAnOmm) {
      return;
    }

    OpacityMicromapInstanceData& ommInstanceData = getOmmInstanceData(instance);

    // OMMs for billboards are built on a first frame they are seen if OMM budget permits 
    // and since such instances often have 1 frame lifetime, the buffers need to be available in that first frame
    if (useStagingNumTexelsPerMicroTriangleObject(instance)) {
      ommInstanceData.needsToCalculateNumTexelsPerMicroTriangle = true;
    }

    // Calculate num texels per micro triangle if requested.
    // This is calculated inline on a draw call submission timeline since
    // a draw call may try to block on an access to a buffer 
    // that was also used in an earlier draw call
    // but if the earlier draw call places a ref on the buffer for OMM to keep it around for latter use, 
    // it will block the draw call submission thread until that ref is lifted.
    // Calculating the data inline here avoids that.
    if (ommInstanceData.needsToCalculateNumTexelsPerMicroTriangle) {
      calculateNumTexelsPerMicroTriangle(instance);
    }
  }
    
  // Calculates number of texels that cover a micro triangle in a triangle.
  // This matches the texcoord span done for conservative opacity estimation during OMM triangle array baking.
  // Returns UINT32_MAX if number of texels exceeds the maximum allowed value
  uint32_t OpacityMicromapManager::calculateNumTexelsPerMicroTriangle(
    Vector2 triangleTexcoords[3],
    float rcpNumMicroTrianglesAlongEdge,
    Vector2 textureResolution) {

    // For the sake of simplicity, we only calculate number of texels needed for a first micro triangle in the triangle. 
    // Even though the micro triangles have the same UV area, the number of texels covering it may be different 
    // between them depending on how their texcoords fit into texel bounds cutoffs, but the variability should be 
    // small enough for OMM's purposes of estimating number of texels needed in a micro triangle when calculating baking costs.

    // Calculate micro triangle texcoords
    Vector2 texcoords[3];
    texcoords[0] = triangleTexcoords[0];
    texcoords[1] = triangleTexcoords[0] + rcpNumMicroTrianglesAlongEdge * (triangleTexcoords[1] - triangleTexcoords[0]);
    texcoords[2] = triangleTexcoords[0] + rcpNumMicroTrianglesAlongEdge * (triangleTexcoords[2] - triangleTexcoords[0]);

    // Find texcoord bbox for the micro triangle
    Vector2 texcoordsMin(FLT_MAX, FLT_MAX);
    Vector2 texcoordsMax(-FLT_MAX, -FLT_MAX);
    for (uint32_t i = 0; i < 3; i++) {
      texcoordsMin = min(texcoords[i], texcoordsMin);
      texcoordsMax = max(texcoords[i], texcoordsMax);
    }

    // Find the sampling index bbox for the micro triangle.
    // Align the bbox to actual texel centers that fully cover the bbox.
    // Align with a top left texel relative to the bbox min.
    // Add epsilon to avoid host underestimating sampling footprint due to float precision errors. 
    // 0.001 should generally be large enough.
    // Should the underestimation still occur, the shader will fall back to a conservative value for a micro triangle.
    const float kEpsilon = 0.001f;
    const float kHalfTexelOffset = 0.5f + kEpsilon;
    const Vector2 texcoordsIndexMin = doFloor(texcoordsMin * textureResolution - Vector2{ kHalfTexelOffset });
    // Align with a bottom right pixel relative to the bbox max
    const Vector2 texcoordsIndexMax = doFloor(texcoordsMax * textureResolution + Vector2{ kHalfTexelOffset });

    // Calculate number of texels in the given texcoord bbox.
    // +1: include the end point of the bbox
    const Vector2 texelSampleDims = texcoordsIndexMax - texcoordsIndexMin + Vector2{ 1.0f };
    const uint32_t numTexelsPerMicroTriangle =
      static_cast<uint32_t>(std::min<float>(round(texelSampleDims.x * texelSampleDims.y), static_cast<float>(UINT32_MAX)));

    return numTexelsPerMicroTriangle;
  }

  void OpacityMicromapManager::calculateNumTexelsPerMicroTriangle(
    NumTexelsPerMicroTriangleCalculationData& numTexelsPerMicroTriangle,
    const RtInstance& instance,
    const uint32_t numTriangles) {

    const RasterGeometry& geometryData = instance.getBlas()->input.getGeometryData();

    if (geometryData.topology != VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST) {
      ONCE(Logger::info("[RTX Opacity Micromap] Instance has non triangle list topology. This is only partially supported. Falling back to a conservative max value for estimated numTexelsPerMicroTriangle instead."));
      numTexelsPerMicroTriangle.result.resize(numTriangles, OpacityMicromapOptions::Building::ConservativeEstimation::maxTexelTapsPerMicroTriangle());
      numTexelsPerMicroTriangle.status = OmmResult::Success;
      return;
    }


    if (!OpacityMicromapOptions::Building::ConservativeEstimation::enable()) {
      numTexelsPerMicroTriangle.result.resize(numTriangles, 1);
      numTexelsPerMicroTriangle.status = OmmResult::Success;
      return;
    }

    const GeometryBufferData bufferData(geometryData);
    const bool hasNonIdentityTextureTransform = instance.surface.textureTransform != Matrix4();
    const bool usesIndices = geometryData.usesIndices();
    const bool has16bitIndices = usesIndices ? geometryData.indexBuffer.indexType() == VK_INDEX_TYPE_UINT16 : false;
    const uint32_t subdivisionLevel = OpacityMicromapOptions::Building::subdivisionLevel();

    // Retrieve opacity texture's resolution
    const RtxTextureManager& textureManager = m_device->getCommon()->getTextureManager();
    const TextureRef& opacityTexture = textureManager.getTextureTable()[instance.getAlbedoOpacityTextureIndex()];

    // Opacity texture is not available, this can happen when DLSS is turned off.
    if (!opacityTexture.getImageView()) {
      return;
    }

    const VkExtent3D& opacityTextureExtent = opacityTexture.getImageView()->imageInfo().extent;
    Vector2 opacityTextureResolution(
      static_cast<float>(opacityTextureExtent.width),
      static_cast<float>(opacityTextureExtent.height));

    // Calculate number of texel footprint per micro triangle for all triangles
    {
      const uint32_t kNumIndicesPerTriangle = 3;
      const float rcpNumMicroTrianglesPerEdge = 1.f / (1 << subdivisionLevel);
      const uint32_t kMaxTexelTapsPerMicroTriangle =
        static_cast<uint32_t>(
          std::min<int32_t>(
            OpacityMicromapOptions::Building::ConservativeEstimation::maxTexelTapsPerMicroTriangle(),
            static_cast<int32_t>(UINT16_MAX)));

      // Check if the required buffers are available
      if (!bufferData.texcoordData) {
        ONCE(Logger::warn(str::format("[RTX Opacity Micromap] Texcoord data is unavailable for calculateNumTexelsPerMicroTriangle(). Falling back to a conservative max value for estimated numTexelsPerMicroTriangle instead.")));
        numTexelsPerMicroTriangle.result.resize(numTriangles, OpacityMicromapOptions::Building::ConservativeEstimation::maxTexelTapsPerMicroTriangle());
        numTexelsPerMicroTriangle.status = OmmResult::Success;
        return;
      }
      
      // Resize the vector to the target size when processing the data for the instance for the first time
      if (numTexelsPerMicroTriangle.numTrianglesCalculated == 0) {
        numTexelsPerMicroTriangle.result.resize(numTriangles);
      }

      // Go over all triangles calculating texel footprint per micro triangle
      // Note: don't issue "break" from the for loop as the logic depends on the for loop's increment statement executing for every iteration
      for (uint32_t& iTriangle = numTexelsPerMicroTriangle.numTrianglesCalculated; 
           iTriangle < numTriangles && m_numTrianglesToCalculateForNumTexelsPerMicroTriangle > 0;
           iTriangle++, m_numTrianglesToCalculateForNumTexelsPerMicroTriangle--) {
        Vector2 texcoords[3];
        uint32_t indexOffset = iTriangle * kNumIndicesPerTriangle;

        // Get triangle's texcoords
        for (uint32_t i = 0; i < kNumIndicesPerTriangle; i++) {
          uint32_t index =
            usesIndices
            ? has16bitIndices
              ? bufferData.getIndex(i + indexOffset)
              : bufferData.getIndex32(i + indexOffset)
            : i + indexOffset;

          texcoords[i] = bufferData.getTexCoord(index);

          if (hasNonIdentityTextureTransform) {
            texcoords[i] = (instance.surface.textureTransform * Vector4(texcoords[i].x, texcoords[i].y, 0.f, 1.f)).xy();
          }
        }

        uint32_t iNumTexelsPerMicroTriangle = calculateNumTexelsPerMicroTriangle(texcoords, rcpNumMicroTrianglesPerEdge, opacityTextureResolution);

        if (iNumTexelsPerMicroTriangle > kMaxTexelTapsPerMicroTriangle) {
          iNumTexelsPerMicroTriangle = 0;
        }

        numTexelsPerMicroTriangle.result[iTriangle] = static_cast<uint16_t>(iNumTexelsPerMicroTriangle);

        numTexelsPerMicroTriangle.numTrianglesWithinTexelBudget += iNumTexelsPerMicroTriangle != 0;
      }
    }

    // Not all triangles got calculated yet
    if (numTexelsPerMicroTriangle.numTrianglesCalculated != numTexelsPerMicroTriangle.result.size()) {
      return;
    }

    // Check the ratio of how many triangles benefit from OMM triangle arrays
    {
      const float percentageOfTrianglesWithinTexelBudget =
        numTexelsPerMicroTriangle.numTrianglesWithinTexelBudget / static_cast<float>(numTexelsPerMicroTriangle.numTrianglesCalculated);

      if (percentageOfTrianglesWithinTexelBudget >= OpacityMicromapOptions::Building::ConservativeEstimation::minValidOMMTrianglesInMeshPercentage()) {
        numTexelsPerMicroTriangle.status = OmmResult::Success;
      } else {
        ONCE(Logger::info("[RTX Opacity Micromap] Instance requires more texel taps to resolve opacity than allowed."));
        numTexelsPerMicroTriangle.status = OmmResult::Rejected;
      }
    }
  }

  void OpacityMicromapManager::calculateNumTexelsPerMicroTriangle(const RtInstance& instance) {
    ScopedCpuProfileZone();

    if (m_numTrianglesToCalculateForNumTexelsPerMicroTriangle == 0) {
      return;
    }

    const RasterGeometry& geometryData = instance.getBlas()->input.getGeometryData();
    const uint32_t numTriangles = geometryData.calculatePrimitiveCount();
    const uint32_t numTrianglesModifiedGeometry = instance.getBlas()->modifiedGeometryData.calculatePrimitiveCount();

    if (numTriangles != numTrianglesModifiedGeometry || numTriangles == 0) {
      ONCE(Logger::info("[RTX Opacity Micromap] Found unsupported instance type. Input and mofified geometry have different or 0 primitive counts."));
      return;
    }

    // Technically, we could generate OMMs without opacity texture present, but it's not currently supported
    if (instance.getAlbedoOpacityTextureIndex() == kSurfaceMaterialInvalidTextureIndex) {
      return;
    }

    const XXH64_hash_t ommSrcHash = getOpacityMicromapHash(instance);

    // Create an object to store the result.
    // Ultimately the result should be stored per Omm hash, but if the hash not been calculated yet
    // it is stored in the staging unordered map per instance
    bool hasInsertedNewObject;
    NumTexelsPerMicroTriangleCalculationData* numTexelsPerMicroTriangle;
    if (ommSrcHash != kEmptyHash) {
      // Using piecewise_construct to construct in-place with an empty constructor for the object
      auto elementIter = m_numTexelsPerMicroTriangle.emplace(
        std::piecewise_construct, std::make_tuple(ommSrcHash), std::make_tuple());
      hasInsertedNewObject = elementIter.second;
      numTexelsPerMicroTriangle = &elementIter.first->second;
    } else {
      // Using piecewise_construct to construct in-place with an empty constructor for the object
      auto elementIter = m_numTexelsPerMicroTriangleStaging.emplace(
        std::piecewise_construct, std::make_tuple(&instance), std::make_tuple());
      hasInsertedNewObject = elementIter.second;
      numTexelsPerMicroTriangle = &elementIter.first->second;
      omm_validation_assert(hasInsertedNewObject &&
                            "Invalid state. This should not be scheduled to be calculated for an instance that already has the result.");
    }

    // The result has been already calculated for this instance
    if (numTexelsPerMicroTriangle->status != OmmResult::DependenciesUnavailable) {
      return;
    }

    calculateNumTexelsPerMicroTriangle(*numTexelsPerMicroTriangle, instance, numTriangles);

    // The calculation is complete
    if (numTexelsPerMicroTriangle->status != OmmResult::DependenciesUnavailable) {
      OpacityMicromapInstanceData& ommInstanceData = getOmmInstanceData(instance);
      ommInstanceData.needsToCalculateNumTexelsPerMicroTriangle = false;
    }
  }

  void OpacityMicromapManager::onInstanceDestroyed(const RtInstance& instance) {
    destroyInstance(instance);
  }

  bool OpacityMicromapManager::calculateInstanceUsesOpacityMicromap(const RtInstance& instance) {
    // Texcoord data is required
    if (instance.getTexcoordHash() == kEmptyHash ||
        // Texgen mode check excludes baked terrain as well
        instance.surface.texgenMode != TexGenMode::None) {
      ONCE(Logger::info("[RTX Opacity Micromap] Instance does not have compatible texture coordinates. Ignoring the Opacity Micromap request."));
      return false;
    }

    if (instance.testCategoryFlags(InstanceCategories::IgnoreOpacityMicromap) ||
        instance.testCategoryFlags(InstanceCategories::IgnoreAlphaChannel)) {
      return false;
    }

    if ((instance.getMaterialType() != MaterialDataType::Opaque &&
         instance.getMaterialType() != MaterialDataType::RayPortal)) {
      return false;
    }

    // Technically, we could generate OMMs without opacity texture present, but it's not currently supported
    // and likely not a commonly useful scenario. This check may already be implicitly covered by 
    // getTexcoordHash() being empty but it's not clear if it's guaranteed.
    if (instance.getAlbedoOpacityTextureIndex() == kSurfaceMaterialInvalidTextureIndex) {
      return false;
    }

    const RasterGeometry& geometryData = instance.getBlas()->input.getGeometryData();
    const uint32_t numTriangles = geometryData.calculatePrimitiveCount();
    const uint32_t numTrianglesModifiedGeometry = instance.getBlas()->modifiedGeometryData.calculatePrimitiveCount();

    if (numTriangles != numTrianglesModifiedGeometry || numTriangles == 0 || numTriangles == UINT32_MAX) {
      ONCE(Logger::warn("[RTX Opacity Micromap] Found unsupported instance type. Input and mofified geometry have different or 0 primitive counts. Ignoring the instance."));
      return false;
    }

    bool useOpacityMicromap = false;

    auto& surface = instance.surface;
    auto& alphaState = instance.surface.alphaState;

    // Find valid OMM candidates
    if ((!alphaState.isFullyOpaque && alphaState.isParticle) || alphaState.emissiveBlend) {
      // Alpha-blended and emissive particles
      useOpacityMicromap = true;
    } else if (instance.isOpaque() &&
               !instance.surface.alphaState.isFullyOpaque && 
               instance.surface.alphaState.isBlendingDisabled) {
      // Alpha-tested geometry
      useOpacityMicromap = true;
    } else if (instance.isOpaque() && !alphaState.isFullyOpaque) {
      useOpacityMicromap = true;
    } else if (instance.getMaterialType() == MaterialDataType::RayPortal) {
      useOpacityMicromap = true;
    }

    // Filter by OMM settings
    {
      useOpacityMicromap &= !instance.isAnimated() || OpacityMicromapOptions::BuildRequests::enableAnimatedInstances();
      useOpacityMicromap &= !alphaState.isParticle || OpacityMicromapOptions::BuildRequests::enableParticles();
    }

    // Check if it needs per uTriangle opacity data
    if (useOpacityMicromap) {
      // ToDo: cover all cases to avoid OMM generation unnecessarily
      if (alphaState.alphaTestType == AlphaTestType::kAlways && alphaState.blendType == BlendType::kAlpha) {
        float tFactorAlpha = ((surface.tFactor >> 24) & 0xff) / 255.f;
        switch (surface.textureAlphaOperation) {
        case DxvkRtTextureOperation::SelectArg1:
          if (surface.textureAlphaArg1Source == RtTextureArgSource::TFactor)
            useOpacityMicromap &= tFactorAlpha > RtxOptions::resolveTransparencyThreshold();
          break;
        case DxvkRtTextureOperation::SelectArg2:
          if (surface.textureAlphaArg2Source == RtTextureArgSource::TFactor)
            useOpacityMicromap &= tFactorAlpha > RtxOptions::resolveTransparencyThreshold();
          break;
        default:
          // This code currently only optimizes a couple of common cases.
          break;
        }
      }
    }

    return useOpacityMicromap;
  }

  void OpacityMicromapManager::onBlasBuild(Rc<DxvkContext> ctx) {
    addBarriersForBuiltOMMs(ctx);
  }

  static bool isIndexOfFullyResidentTexture(uint32_t index, const std::vector<TextureRef>& textures) {
    if (index == BINDING_INDEX_INVALID) {
      return false;
    }
    const TextureRef& tex = textures[index];

    const ManagedTexture* managed = tex.getManagedTexture().ptr();
    if (!managed) {
      return tex.getImageView() != nullptr;
    }

    // TODO: determine many mips are needed for OMM
    constexpr auto REQUIRED_MIP_COUNT_FOR_OMM = 4;
    return managed->hasUploadedMips(REQUIRED_MIP_COUNT_FOR_OMM, false);
  }

  bool OpacityMicromapManager::areInstanceTexturesResident(const RtInstance& instance, const std::vector<TextureRef>& textures) const {
    // Opacity map not loaded yet
    if (!isIndexOfFullyResidentTexture(instance.getAlbedoOpacityTextureIndex(), textures))
      return false;

    // RayPortal materials use two opacity maps, see if the second one is already loaded
    if (instance.getMaterialType() == MaterialDataType::RayPortal &&
        !isIndexOfFullyResidentTexture(instance.getSecondaryOpacityTextureIndex(), textures))
      return false;

    return true;
  }

  void OpacityMicromapManager::updateSourceHash(RtInstance& instance, XXH64_hash_t ommSrcHash) {
    XXH64_hash_t prevOmmSrcHash = getOpacityMicromapHash(instance);

    if (prevOmmSrcHash != kEmptyHash && ommSrcHash != prevOmmSrcHash) {
      // Valid source hash changed, deassociate instance from the previous hash
      // Note: this will delete non-hash dependent per instance OMM data as well, 
      // which may not be necessary, but we cannot determine that right now
      destroyInstance(instance);
    }

    OpacityMicromapInstanceData& ommInstanceData = getOmmInstanceData(instance);
    ommInstanceData.ommSrcHash = ommSrcHash;
  }

  fast_unordered_cache<OpacityMicromapManager::CachedSourceData>::iterator OpacityMicromapManager::registerCachedSourceData(const OmmRequest& ommRequest) {

    auto sourceDataIter = m_cachedSourceData.insert({ ommRequest.ommSrcHash, CachedSourceData() }).first;
    CachedSourceData& sourceData = sourceDataIter->second;

    sourceData.initialize(ommRequest, m_instanceOmmRequests, *this);

    if (sourceData.numTriangles == 0) {
      ONCE(Logger::warn("[RTX Opacity Micromap] Input geometry has 0 triangles. Ignoring the build request."));
      // Unlink the instance
      sourceData.setInstance(nullptr, m_instanceOmmRequests, *this);
      m_cachedSourceData.erase(sourceDataIter);
 
      return m_cachedSourceData.end();
    }

    return sourceDataIter;
  }

  void OpacityMicromapManager::deleteCachedSourceData(fast_unordered_cache<OpacityMicromapManager::CachedSourceData>::iterator sourceDataIter, OpacityMicromapCacheState ommCacheState, bool destroyParentInstanceOmmRequestContainer) {
    if (ommCacheState <= OpacityMicromapCacheState::eStep1_Baking)
      sourceDataIter->second.setInstance(nullptr, m_instanceOmmRequests, *this, destroyParentInstanceOmmRequestContainer);
    m_cachedSourceData.erase(sourceDataIter);
  }

  void OpacityMicromapManager::deleteCachedSourceData(XXH64_hash_t ommSrcHash, OpacityMicromapCacheState ommCacheState, bool destroyParentInstanceOmmRequestContainer) {
    auto sourceDataIter = m_cachedSourceData.find(ommSrcHash);
    if (sourceDataIter != m_cachedSourceData.end())
      deleteCachedSourceData(sourceDataIter, ommCacheState, destroyParentInstanceOmmRequestContainer);
  }

  // Returns true if a new OMM build request was accepted
  bool OpacityMicromapManager::addNewOmmBuildRequest(RtInstance& instance, const OmmRequest& ommRequest) {
    
    // Prevent host getting overloaded
    if (m_ommBuildRequestStatistics.size() >= OpacityMicromapOptions::BuildRequests::maxRequests())
      return false;

    XXH64_hash_t ommSrcHash = ommRequest.ommSrcHash;

    // Check if the request passes OMM build request filter settings
    {
      // Ignore black listed OMM source hashes
      if (m_blackListedList.find(ommSrcHash) != m_blackListedList.end()) {
        return false;
      }
  
      if (OpacityMicromapOptions::BuildRequests::filtering()) {
        uint32_t minInstanceFrameAge = OpacityMicromapOptions::BuildRequests::minInstanceFrameAge();
        uint32_t minNumRequests = OpacityMicromapOptions::BuildRequests::minNumRequests();
        uint32_t minNumFramesRequested = OpacityMicromapOptions::BuildRequests::minNumFramesRequested();

        if (usesSplitBillboardOpacityMicromap(instance) && OpacityMicromapOptions::BuildRequests::customFiltersForBillboards()) {
          // Lower the filter requirements for billboards since they are dynamic.
          // But still we want to avoid baking billboards that do not get reused for now
          minInstanceFrameAge = 0;
          minNumRequests = 2;
          minNumFramesRequested = 0;
        }

        if (!ommRequest.isBillboardOmmRequest()) {
          const uint32_t currentFrameIndex = m_device->getCurrentFrameId();

          OMMBuildRequestStatistics& ommBuildRequestStatistics = m_ommBuildRequestStatistics[ommSrcHash];
          ommBuildRequestStatistics.numTimesRequested = 1 + std::min<uint16_t>(UINT16_MAX - 1, ommBuildRequestStatistics.numTimesRequested);

          if (currentFrameIndex != ommBuildRequestStatistics.lastRequestFrameId) {
            ommBuildRequestStatistics.lastRequestFrameId = currentFrameIndex;
            ommBuildRequestStatistics.numFramesRequested = 1 + std::min<uint16_t>(UINT16_MAX - 1, minNumFramesRequested);
          }

          if (instance.getFrameAge() < minInstanceFrameAge)
            return false;

          if (ommBuildRequestStatistics.numTimesRequested < minNumRequests ||
              ommBuildRequestStatistics.numFramesRequested < minNumFramesRequested)
            return false;

          // Request passed the check, don't track statistics for it no more
          m_ommBuildRequestStatistics.erase(ommSrcHash);
        }
      }
    }

    std::list<XXH64_hash_t>::iterator cacheStateListIter;
    if (!insertToUnprocessedList(ommRequest, cacheStateListIter))
      return false;

    // Place the element to the end of the LRU list, and thus marking it as most recent 
    m_leastRecentlyUsedList.emplace_back(ommSrcHash);
    auto lastElementIterator = std::next(m_leastRecentlyUsedList.end(), -1);
    m_ommCache.emplace(
      std::piecewise_construct,
      std::forward_as_tuple(ommSrcHash),
      std::forward_as_tuple(*m_device, OpacityMicromapCacheState::eStep0_Unprocessed, OpacityMicromapOptions::Building::subdivisionLevel(), 
                            OpacityMicromapOptions::Building::enableVertexAndTextureOperations(), m_device->getCurrentFrameId(),
                            lastElementIterator, cacheStateListIter, ommRequest));

    return true;
  }
  
  bool OpacityMicromapManager::insertToUnprocessedList(const OmmRequest& ommRequest, std::list<XXH64_hash_t>::iterator& cacheStateListIter) {
    XXH64_hash_t ommSrcHash = ommRequest.ommSrcHash;

    auto sourceDataIter = registerCachedSourceData(ommRequest);

    if (sourceDataIter == m_cachedSourceData.end())
      return false;

    CachedSourceData& sourceData = sourceDataIter->second;

    // Billboard requests go to the end since they are expected to be changed at high frequency and trigger a lot of builds.
    // Therefore, we want to prioritize building ommRequests that passed standard OMM registration filter tests first
    if (!ommRequest.isBillboardOmmRequest()) {
      // Add the OMM request to the unprocessed list according to the numTriangle count in an ascending order 
      // so that requests with least triangles are processed first and thus with lower overall latency
      for (auto itemIter = m_unprocessedList.begin(); itemIter != m_unprocessedList.end(); itemIter++) {

        XXH64_hash_t itemOmmSrcHash = *itemIter;

        CachedSourceData& itemSourceData = m_cachedSourceData[itemOmmSrcHash];

        if (sourceData.numTriangles < itemSourceData.numTriangles ||
            // insert in front of any billboard requests
            usesSplitBillboardOpacityMicromap(*itemSourceData.getInstance())) {
          cacheStateListIter = m_unprocessedList.insert(itemIter, ommSrcHash);
          return true;
        }
      }
    }

    m_unprocessedList.emplace_back(ommSrcHash);
    cacheStateListIter = std::prev(m_unprocessedList.end());

    return true;
  }

  void OpacityMicromapManager::generateInstanceOmmRequests(RtInstance& instance, 
                                                           const InstanceManager& instanceManager, 
                                                           std::vector<OmmRequest>& ommRequests) {

    const bool usesSplitBillboardOMM = usesSplitBillboardOpacityMicromap(instance);
    const uint32_t numOmmRequests = std::max(usesSplitBillboardOMM ? instance.getBillboardCount() : 1u, 1u);
    ommRequests.reserve(numOmmRequests);
    XXH64_hash_t ommSrcHash;  // Compound hash for the instance

    // Create all OmmRequest objects corresponding to the instance
    if (usesSplitBillboardOMM) {
      const uint32_t numTriangles = instance.getBlas()->modifiedGeometryData.calculatePrimitiveCount();
      assert((numTriangles & 1) == 0 &&
             "Only compound omms consisting of multiples of quads are supported");

      std::vector<XXH64_hash_t> ommSrcHashes;
      ommSrcHashes.reserve(numOmmRequests);

      for (uint32_t i = 0; i < instance.getBillboardCount(); i++) {
        OmmRequest ommRequest(instance, instanceManager, i);

        // Only track unique omm requests
        if (std::find(ommSrcHashes.begin(), ommSrcHashes.end(), ommRequest.ommSrcHash) == ommSrcHashes.end()) {
          ommSrcHashes.push_back(ommRequest.ommSrcHash);
          ommRequests.emplace_back(std::move(ommRequest));
        }
      }

      ommSrcHash = XXH3_64bits_withSeed(ommSrcHashes.data(), ommSrcHashes.size() * sizeof(ommSrcHashes[0]), kEmptyHash);

    } else {
      ommRequests.emplace_back(instance, instanceManager);
      ommSrcHash = ommRequests[0].ommSrcHash;
    }

    updateSourceHash(instance, ommSrcHash);
  }

  bool OpacityMicromapManager::registerOpacityMicromapBuildRequest(RtInstance& instance,
                                                                   const InstanceManager& instanceManager,
                                                                   const std::vector<TextureRef>& textures) {
    ScopedCpuProfileZone();

    // Skip processing if there's no available memory backing
    if (m_memoryManager.getBudget() == 0) {
      return false;
    }

    OpacityMicromapInstanceData& ommInstanceData = getOmmInstanceData(instance);
    ommInstanceData.usesOMM = calculateInstanceUsesOpacityMicromap(instance);

    if (!ommInstanceData.usesOMM) {
      return false;
    }

    if (!areInstanceTexturesResident(instance, textures)) {
      return false;
    }

    // Ignore non-reference view model instance requests for adding new OMM requests.
    // Their OMM data will be generated via OMM requests for reference ViewModel instances.
    // The reason why they cannot be registered for building is that instance manager 
    // does not call destroyInstance callbacks when they are destroyed. Also reference instances
    // are kept across frames which is more fitting for OMM generation with a per frame building budget.
    if (instance.isViewModelNonReference()) {
      return false;
    }

    InstanceOmmRequests ommRequests;

    generateInstanceOmmRequests(instance, instanceManager, ommRequests.ommRequests);

    // Bookkeep the requests now so that they can be released should any registers fail below
    m_instanceOmmRequests.emplace(getOpacityMicromapHash(instance), ommRequests);

    bool allRegistersSucceeded = true;

    // Register all omm requests for the instance
    for (auto& ommRequest : ommRequests.ommRequests)
      allRegistersSucceeded &= registerOmmRequestInternal(instance, ommRequest);

    // Purge the instance omm requests that didn't end up with any active omm requests
    // ToDo: should avoid adding into the list in the first place as this happens for ommRequests that
    //   have already been completed as well
    auto instanceOmmRequestsIter = m_instanceOmmRequests.find(getOpacityMicromapHash(instance));
    if (instanceOmmRequestsIter->second.numActiveRequests == 0)
      m_instanceOmmRequests.erase(instanceOmmRequestsIter);
      
    return allRegistersSucceeded;
  }

  bool OpacityMicromapManager::registerOmmRequestInternal(RtInstance& instance, const OmmRequest& ommRequest) {

    XXH64_hash_t ommSrcHash = ommRequest.ommSrcHash;

    if (ommSrcHash == kEmptyHash) {
      ONCE(Logger::warn("[RTX Opacity Micromap] Build source instance has an invalid hash. Ignoring the build request."));
      return false;
    }

    auto ommCacheIterator = m_ommCache.find(ommSrcHash);

    // OMM request is not yet known
    if (ommCacheIterator == m_ommCache.end()) {
      return addNewOmmBuildRequest(instance, ommRequest);
    } else {

      auto& ommCacheItem = ommCacheIterator->second;

      // Check OMM request's parametrization matches that of the cached omm data
      // in case of an OMM hash collision
      if (!ommCacheItem.isCompatibleWithOmmRequest(ommRequest)) {
        ONCE(Logger::warn("[RTX Opacity Micromap] Found a cached Opacity Micromap with same hash but with incompatible parametrization. Black listing the Opacity Micromap hash."));
        m_blackListedList.insert(ommSrcHash);
        destroyOmmData(ommSrcHash);
        return false;
      }

      if (ommCacheItem.cacheState == OpacityMicromapCacheState::eStep1_Baking) {
        auto sourceDataIter = m_cachedSourceData.find(ommSrcHash);

        // Source data has been unlinked and removed from unprocessed list, try adding it back to the unprocessed list
        if (sourceDataIter == m_cachedSourceData.end()) {
          ommCacheItem.isUnprocessedCacheStateListIterValid = insertToUnprocessedList(ommRequest, ommCacheItem.cacheStateListIter);
          return ommCacheItem.isUnprocessedCacheStateListIterValid;
        }
      }
    }

    return true;
  }

  XXH64_hash_t OpacityMicromapManager::tryBindOpacityMicromap(Rc<DxvkContext> ctx,
                                                              const RtInstance& instance, uint32_t billboardIndex,
                                                              VkAccelerationStructureGeometryKHR& targetGeometry,
                                                              const InstanceManager& instanceManager) {
    ScopedCpuProfileZone();
    
    // Skip trying to bind an OMM if the budget is 0 since no OMMs can exist
    if (m_memoryManager.getBudget() == 0) {
      return kEmptyHash;
    }

    if (!usesOpacityMicromap(instance)) {
      return kEmptyHash;
    }

    return bindOpacityMicromap(ctx, instance, billboardIndex, targetGeometry, instanceManager);
  }
  
  XXH64_hash_t OpacityMicromapManager::bindOpacityMicromap(Rc<DxvkContext> ctx,
                                                           const RtInstance& instance, 
                                                           uint32_t billboardIndex,
                                                           VkAccelerationStructureGeometryKHR& targetGeometry,
                                                           const InstanceManager& instanceManager) {
    m_numRequestedOMMBindings++;

    if (!OpacityMicromapOptions::enableBinding())
      return kEmptyHash;

    // ToDo: avoid fixing up the index here
    billboardIndex =
      usesSplitBillboardOpacityMicromap(instance) ? billboardIndex : OmmRequest::kInvalidIndex;
    const OmmRequest ommRequest(instance, instanceManager, billboardIndex);

    auto ommCacheItemIter = m_ommCache.find(ommRequest.ommSrcHash);

    // OMM is not available in the cache
    if (ommCacheItemIter == m_ommCache.end())
      return kEmptyHash;

    bool boundOMM = false;
    OpacityMicromapCacheItem& ommCacheItem = ommCacheItemIter->second;
    const OpacityMicromapCacheState ommCacheState = ommCacheItem.cacheState;

    // Check OMM request's parametrization matches that of the cached omm data
    // in case of an OMM hash collision
    if (!ommCacheItem.isCompatibleWithOmmRequest(ommRequest)) {
      ONCE(Logger::warn("[RTX Opacity Micromap] Found a cached Opacity Microamp with a matching hash but with an incompatible parametrization. Discarding Opacity Micromap binding request."));
      return kEmptyHash;
    }

    ommCacheItem.lastUseFrameIndex = m_device->getCurrentFrameId();

    // Make the item most recently used
    m_leastRecentlyUsedList.splice(m_leastRecentlyUsedList.end(), m_leastRecentlyUsedList, ommCacheItem.leastRecentlyUsedListIter);

    // Bind OMM if the data is ready
    switch (ommCacheState) {
    case OpacityMicromapCacheState::eStep0_Unprocessed:
    case OpacityMicromapCacheState::eStep1_Baking:
    case OpacityMicromapCacheState::eStep2_Baked:
      // OMM data is not yet ready
      break;

    case OpacityMicromapCacheState::eStep3_Built:
    case OpacityMicromapCacheState::eStep4_Ready:
    {
      targetGeometry.geometry.triangles.pNext = &ommCacheItem.blasOmmBuffers->blasDesc;
      boundOMM = true;
      m_numBoundOMMs++;

      // Track the lifetime of the used buffers
      ctx->getCommandList()->trackResource<DxvkAccess::Read>(ommCacheItem.blasOmmBuffers);
      m_boundOMMs.push_back(ommCacheItem.blasOmmBuffers);
      break;
    }
    case OpacityMicromapCacheState::eUnknown:
      assert(false && "eUnknown OpacityMicromapCacheState in OpacityMicromapManager::bindOpacityMicromap");
    }

    if (ommCacheState == OpacityMicromapCacheState::eStep3_Built)
      m_boundOmmsRequireSynchronization = true;

    return boundOMM ? ommRequest.ommSrcHash : kEmptyHash;
  }

  void OpacityMicromapManager::addBarriersForBuiltOMMs(Rc<DxvkContext> ctx) {

    if (m_boundOmmsRequireSynchronization) {

      // Add a barrier blocking on OMM builds
      {
        VkMemoryBarrier2 memoryBarrier = { VK_STRUCTURE_TYPE_MEMORY_BARRIER_2, NULL,
          VK_PIPELINE_STAGE_2_MICROMAP_BUILD_BIT_EXT, VK_ACCESS_2_MICROMAP_WRITE_BIT_EXT,
          VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, VK_ACCESS_2_MICROMAP_READ_BIT_EXT };
        VkDependencyInfo dependencyInfo = { VK_STRUCTURE_TYPE_DEPENDENCY_INFO };

        dependencyInfo.memoryBarrierCount = 1;
        dependencyInfo.pMemoryBarriers = &memoryBarrier;

        ctx->getCommandList()->vkCmdPipelineBarrier2KHR(&dependencyInfo);
      }

      // All built instances have been synchronized, remove them from the built list
      {
        for (auto& ommSrcHash : m_builtList) {
          m_ommCache[ommSrcHash].cacheState = OpacityMicromapCacheState::eStep4_Ready;
        }
        m_builtList.clear();
      }

      m_boundOmmsRequireSynchronization = false;
    }
  }

  template <typename IndexType>
  void calculateMicromapTriangleArrayBufferSizes(uint32_t numAllocatedTriangles,
                                                 uint32_t& triangleArrayBufferSize,
                                                 uint32_t& triangleIndexBufferSize) {
    triangleArrayBufferSize = numAllocatedTriangles * sizeof(VkMicromapTriangleEXT);
    triangleIndexBufferSize = numAllocatedTriangles * sizeof(IndexType);
  };

  template <typename IndexType>
  OpacityMicromapManager::OmmResult initializeOpacityMicromapTriangleArrayBuffers(
    DxvkDevice* device,
    Rc<DxvkContext> ctx,
    VkOpacityMicromapFormatEXT ommFormat,
    uint16_t subdivisionLevel,
    uint32_t numTriangles,
    uint32_t opacityMicromapPerTriangleBufferSize,
    Rc<DxvkBuffer>& triangleArrayBuffer,
    Rc<DxvkBuffer>& triangleIndexBuffer) {

    uint32_t triangleArrayBufferSize;
    uint32_t triangleIndexBufferSize;
    calculateMicromapTriangleArrayBufferSizes<IndexType>(numTriangles, triangleArrayBufferSize, triangleIndexBufferSize);

    // Create buffers
    {
      DxvkBufferCreateInfo ommBufferInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
      ommBufferInfo.usage = VK_BUFFER_USAGE_MICROMAP_BUILD_INPUT_READ_ONLY_BIT_EXT | 
        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
      ommBufferInfo.access = VK_ACCESS_TRANSFER_WRITE_BIT;
      ommBufferInfo.requiredAlignmentOverride = 256;
      ommBufferInfo.size = triangleArrayBufferSize;
      triangleArrayBuffer = device->createBuffer(ommBufferInfo, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, DxvkMemoryStats::Category::RTXOpacityMicromap, "OMM triangle array buffer");
      
      if (triangleArrayBuffer == nullptr) {
        ONCE(Logger::warn(str::format("[RTX - Opacity Micromap] Failed to allocate triangle buffers due to m_device->createBuffer() failing to allocate a buffer for size: ", ommBufferInfo.size)));
        return OpacityMicromapManager::OmmResult::OutOfMemory;
      }

      ommBufferInfo.usage |= VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
      
      ommBufferInfo.size = triangleIndexBufferSize;
      triangleIndexBuffer = device->createBuffer(ommBufferInfo, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, DxvkMemoryStats::Category::RTXOpacityMicromap, "OMM triangle index buffer");

      if (triangleIndexBuffer == nullptr) {
        ONCE(Logger::warn(str::format("[RTX - Opacity Micromap] Failed to allocate triangle buffers due to m_device->createBuffer() failing to allocate a buffer for size: ", ommBufferInfo.size)));
        return OpacityMicromapManager::OmmResult::OutOfMemory;
      }
    }

    // Micromap triangle buffer desc
    VkMicromapTriangleEXT micromapTriangleDescTemplate;
    micromapTriangleDescTemplate.dataOffset = 0;           // Offset in opacityMicromapBuffer
    micromapTriangleDescTemplate.format = ommFormat;
    micromapTriangleDescTemplate.subdivisionLevel = subdivisionLevel;

    std::vector<VkMicromapTriangleEXT> hostTriangleArrayBuffer(numTriangles);
    std::vector<IndexType> hostTriangleIndexBuffer(numTriangles);
    IndexType* ommIndex = hostTriangleIndexBuffer.data();

    for (IndexType i = 0; i < numTriangles; i++) {
      hostTriangleArrayBuffer[i] = micromapTriangleDescTemplate;
      hostTriangleArrayBuffer[i].dataOffset = i * opacityMicromapPerTriangleBufferSize;

      *(ommIndex++) = i;
    }

    ctx->writeToBuffer(triangleArrayBuffer, 0, triangleArrayBufferSize, hostTriangleArrayBuffer.data());
    ctx->writeToBuffer(triangleIndexBuffer, 0, triangleIndexBufferSize, hostTriangleIndexBuffer.data());

    return OpacityMicromapManager::OmmResult::Success;
  }

  void OpacityMicromapManager::calculateMicromapBuildInfo(
    VkMicromapUsageEXT& ommUsageGroup,
    VkMicromapBuildInfoEXT& ommBuildInfo,
    VkMicromapBuildSizesInfoEXT& sizeInfo) {
    ommBuildInfo = { VK_STRUCTURE_TYPE_MICROMAP_BUILD_INFO_EXT };
    sizeInfo = { VK_STRUCTURE_TYPE_MICROMAP_BUILD_SIZES_INFO_EXT };

    // Get prebuild info
    ommBuildInfo.type = VK_MICROMAP_TYPE_OPACITY_MICROMAP_EXT;
    ommBuildInfo.flags = 0;
    ommBuildInfo.mode = VK_BUILD_MICROMAP_MODE_BUILD_EXT;
    ommBuildInfo.dstMicromap = VK_NULL_HANDLE;
    ommBuildInfo.usageCountsCount = 1;
    ommBuildInfo.pUsageCounts = &ommUsageGroup;
    ommBuildInfo.data.deviceAddress = 0ull;
    ommBuildInfo.triangleArray.deviceAddress = 0ull;
    ommBuildInfo.triangleArrayStride = 0;

    m_device->vkd()->vkGetMicromapBuildSizesEXT(m_device->vkd()->device(), VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &ommBuildInfo, &sizeInfo);
  }

  void OpacityMicromapManager::calculateRequiredVRamSize(
    uint32_t numTriangles,
    uint16_t subdivisionLevel,
    VkOpacityMicromapFormatEXT ommFormat,
    VkIndexType triangleIndexType,
    VkDeviceSize& arrayBufferDeviceSize,
    VkDeviceSize& blasOmmBuffersDeviceSize) {
    const uint32_t numMicroTrianglesPerTriangle = calculateNumMicroTriangles(subdivisionLevel);
    const uint32_t numMicroTriangles = numTriangles * numMicroTrianglesPerTriangle;
    const uint8_t numOpacityMicromapBitsPerMicroTriangle = ommFormat == VK_OPACITY_MICROMAP_FORMAT_2_STATE_EXT ? 1 : 2;
    const uint32_t opacityMicromapPerTriangleBufferSize = dxvk::util::ceilDivide(numMicroTrianglesPerTriangle * numOpacityMicromapBitsPerMicroTriangle, 8);
    const uint32_t opacityMicromapBufferSize = numTriangles * opacityMicromapPerTriangleBufferSize;

    // Account for any alignments at start and the end of buffers
    arrayBufferDeviceSize = opacityMicromapBufferSize + 2 * kBufferAlignment;

    // Fill out VkMicromapUsageEXT with size information
    // For now all triangles are in the same micromap group
    VkMicromapUsageEXT ommUsageGroup = {};
    ommUsageGroup.count = numTriangles;
    ommUsageGroup.subdivisionLevel = subdivisionLevel;
    ommUsageGroup.format = ommFormat;

    // Get micromap prebuild info
    VkMicromapBuildInfoEXT ommBuildInfo = { VK_STRUCTURE_TYPE_MICROMAP_BUILD_INFO_EXT };
    VkMicromapBuildSizesInfoEXT sizeInfo = { VK_STRUCTURE_TYPE_MICROMAP_BUILD_SIZES_INFO_EXT };
    calculateMicromapBuildInfo(ommUsageGroup, ommBuildInfo, sizeInfo);

    uint32_t triangleArrayBufferSize;
    uint32_t triangleIndexBufferSize;
    if (triangleIndexType == VK_INDEX_TYPE_UINT16)
      calculateMicromapTriangleArrayBufferSizes<uint16_t>(numTriangles, triangleArrayBufferSize, triangleIndexBufferSize);
    else
      calculateMicromapTriangleArrayBufferSizes<uint32_t>(numTriangles, triangleArrayBufferSize, triangleIndexBufferSize);

    // Account for any alignments at start and the end of buffers
    blasOmmBuffersDeviceSize =
      triangleArrayBufferSize + 2 * kBufferAlignment +
      triangleIndexBufferSize + 2 * kBufferInBlasUsageAlignment +
      sizeInfo.micromapSize + 2 * kBufferInBlasUsageAlignment;
  }

  OpacityMicromapManager::OmmResult OpacityMicromapManager::getNumTexelsPerMicroTriangle(
    const RtInstance& instance,
    NumTexelsPerMicroTriangle** numTexelsPerMicroTriangle) {

    // Note: this is not expected to be called for non-reference instances which
    // goes along the design choice of non-reference OMM instances not being used for generating OMMs
    omm_validation_assert(!instance.isViewModelNonReference());

    NumTexelsPerMicroTriangleCalculationData* numTexelsPerMicroTriangleCalculationData;

    // Look up the object holding the data
    if (useStagingNumTexelsPerMicroTriangleObject(instance)) {
      auto numTexelsPerMicroTriangleStagingIter = m_numTexelsPerMicroTriangleStaging.find(&instance);

      if (numTexelsPerMicroTriangleStagingIter == m_numTexelsPerMicroTriangleStaging.end()) {
        return OmmResult::DependenciesUnavailable;
      }

      numTexelsPerMicroTriangleCalculationData = &numTexelsPerMicroTriangleStagingIter->second;
    } else {
      auto numTexelsPerMicroTriangleIter = m_numTexelsPerMicroTriangle.find(getOpacityMicromapHash(instance));

      if (numTexelsPerMicroTriangleIter == m_numTexelsPerMicroTriangle.end()) {
        return OmmResult::DependenciesUnavailable;
      }

      numTexelsPerMicroTriangleCalculationData = &numTexelsPerMicroTriangleIter->second;
    }

    *numTexelsPerMicroTriangle = &numTexelsPerMicroTriangleCalculationData->result;
    return numTexelsPerMicroTriangleCalculationData->status;
  }

  OpacityMicromapManager::OmmResult OpacityMicromapManager::bakeOpacityMicromapArray(
    Rc<DxvkContext> ctx,
    XXH64_hash_t ommSrcHash,
    OpacityMicromapCacheItem& ommCacheItem,
    CachedSourceData& sourceData,
    const std::vector<TextureRef>& textures,
    uint32_t& availableBakingBudget) {
    
    const RtInstance& instance = *sourceData.getInstance();

    if (!areInstanceTexturesResident(instance, textures)) {
      return OmmResult::DependenciesUnavailable;
    }

    // Check if the data has already been calculated
    NumTexelsPerMicroTriangle* numTexelsPerMicroTriangle;
    const OmmResult texelBudgetCheckResult = getNumTexelsPerMicroTriangle(instance, &numTexelsPerMicroTriangle);
    if (texelBudgetCheckResult != OmmResult::Success) {
      // If the instance hasn't been updated this frame, it means it's kept around by other means 
      // and NumTexelsPerMicroTriangle won't be able to be generated since the draw calls for it are no longer being issued.
      // Therefore, let's get rid of the instance being linked to OMMs. We can't call destroyInstance() from within baking call stack, 
      // since multiple OMM items linked to it may get purged because of it and baking iterates through a list of OMMs.
      // Instead queue up the instance destruction
      if (instance.getFrameLastUpdated() != m_device->getCurrentFrameId()) {
        m_instancesToDestroy.push_back(&instance);
      }
      return texelBudgetCheckResult;
    }

    BlasEntry& blasEntry = *instance.getBlas();

    const uint32_t numTriangles = sourceData.numTriangles;
    const uint32_t numMicroTrianglesPerTriangle = calculateNumMicroTriangles(ommCacheItem.subdivisionLevel);
    const uint32_t numMicroTriangles = numTriangles * numMicroTrianglesPerTriangle;
    const uint8_t numOpacityMicromapBitsPerMicroTriangle = ommCacheItem.ommFormat == VK_OPACITY_MICROMAP_FORMAT_2_STATE_EXT ? 1 : 2;
    const uint32_t opacityMicromapPerTriangleBufferSize = dxvk::util::ceilDivide(numMicroTrianglesPerTriangle * numOpacityMicromapBitsPerMicroTriangle, 8);
    const uint32_t opacityMicromapBufferSize = numTriangles * opacityMicromapPerTriangleBufferSize;

    omm_validation_assert((usesSplitBillboardOpacityMicromap(instance) || numTriangles == instance.getBlas()->input.getGeometryData().calculatePrimitiveCount()) &&
                          instance.getBlas()->input.getGeometryData().calculatePrimitiveCount() ==
                          instance.getBlas()->modifiedGeometryData.calculatePrimitiveCount() &&
                          "Number of triangles must match and be consistent");

    // Preallocate all the device memory needed to build the OMM item
    if (ommCacheItem.getDeviceSize() == 0)
    {
      VkDeviceSize arrayBufferDeviceSize;
      VkDeviceSize blasOmmBuffersDeviceSize;

      const VkIndexType triangleIndexType = numTriangles <= UINT16_MAX ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32;
      calculateRequiredVRamSize(numTriangles, ommCacheItem.subdivisionLevel, ommCacheItem.ommFormat, triangleIndexType,
                                arrayBufferDeviceSize, blasOmmBuffersDeviceSize);

      VkDeviceSize requiredDeviceSize = arrayBufferDeviceSize + blasOmmBuffersDeviceSize;

      if (!m_memoryManager.allocate(requiredDeviceSize)) {
        m_amountOfMemoryMissing += requiredDeviceSize;
        return OmmResult::OutOfMemory;
      }

      ommCacheItem.arrayBufferDeviceSize = arrayBufferDeviceSize;
      ommCacheItem.blasOmmBuffersDeviceSize = blasOmmBuffersDeviceSize;
    }

    // Create micromap buffer
    if (!ommCacheItem.ommArrayBuffer.ptr())
    {
      DxvkBufferCreateInfo ommBufferInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
      ommBufferInfo.usage = VK_BUFFER_USAGE_MICROMAP_BUILD_INPUT_READ_ONLY_BIT_EXT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
      ommBufferInfo.access = VK_ACCESS_SHADER_WRITE_BIT;
      ommBufferInfo.size = opacityMicromapBufferSize;
      ommBufferInfo.requiredAlignmentOverride = 256;
      ommCacheItem.ommArrayBuffer = m_device->createBuffer(ommBufferInfo, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, DxvkMemoryStats::Category::RTXOpacityMicromap, "OMM micromap buffer");

      if (ommCacheItem.ommArrayBuffer == nullptr) {
        ONCE(Logger::warn(str::format("[RTX - Opacity Micromap] Failed to allocate OMM array buffer due to m_device->createBuffer() failing to allocate a buffer for size: ", ommBufferInfo.size)));
        return OmmResult::OutOfMemory;
      }
    }

    // Generate OMM array
    {
      RtxGeometryUtils::BakeOpacityMicromapDesc desc(*numTexelsPerMicroTriangle);
      desc.subdivisionLevel = ommCacheItem.subdivisionLevel;
      desc.numMicroTrianglesPerTriangle = calculateNumMicroTriangles(ommCacheItem.subdivisionLevel);
      desc.ommFormat = ommCacheItem.ommFormat;
      desc.surfaceIndex = instance.getSurfaceIndex();
      desc.materialType = instance.getMaterialType();
      desc.applyVertexAndTextureOperations = ommCacheItem.useVertexAndTextureOperations;
      desc.useConservativeEstimation = OpacityMicromapOptions::Building::ConservativeEstimation::enable();
      desc.conservativeEstimationMaxTexelTapsPerMicroTriangle = OpacityMicromapOptions::Building::ConservativeEstimation::maxTexelTapsPerMicroTriangle();
      desc.numTriangles = numTriangles;
      desc.triangleOffset = sourceData.triangleOffset;
      desc.resolveTransparencyThreshold = RtxOptions::resolveTransparencyThreshold();
      desc.resolveOpaquenessThreshold = RtxOptions::resolveOpaquenessThreshold();
      desc.costPerTexelTapPerMicroTriangleBudget = OpacityMicromapOptions::Building::costPerTexelTapPerMicroTriangleBudget();

      // Overrides
      if (instance.surface.alphaState.isDecal)
        desc.resolveTransparencyThreshold = std::max(desc.resolveTransparencyThreshold, OpacityMicromapOptions::Building::decalsMinResolveTransparencyThreshold());

      const auto& samplers = ctx->getCommonObjects()->getSceneManager().getSamplerTable();
            
      // Bake micro triangles
      do {
        ctx->getCommonObjects()->metaGeometryUtils().dispatchBakeOpacityMicromap(
          ctx, instance, blasEntry.modifiedGeometryData,
          textures, samplers, instance.getAlbedoOpacityTextureIndex(), instance.getSamplerIndex(), instance.getSecondaryOpacityTextureIndex(), instance.getSecondarySamplerIndex(),
          desc, ommCacheItem.bakingState, availableBakingBudget, ommCacheItem.ommArrayBuffer);

        if (OpacityMicromapOptions::Building::enableUnlimitedBakingAndBuildingBudgets()) {
          availableBakingBudget = UINT32_MAX;

          // There are more micro triangles to bake
          if (ommCacheItem.bakingState.numMicroTrianglesBaked < ommCacheItem.bakingState.numMicroTrianglesToBake) {
            continue;
          }
        }

        // Exit the loop
        break;
      } while (true);

      ctx->getCommandList()->trackResource<DxvkAccess::Write>(ommCacheItem.ommArrayBuffer);
    }

    m_numMicroTrianglesBaked += ommCacheItem.bakingState.numMicroTrianglesBakedInLastBake;

    return OmmResult::Success;
  }

  OpacityMicromapManager::OmmResult OpacityMicromapManager::buildOpacityMicromap(
    Rc<DxvkContext> ctx,
    XXH64_hash_t ommSrcHash,
    OpacityMicromapCacheItem& ommCacheItem,
    VkMicromapUsageEXT& ommUsageGroup,
    VkMicromapBuildInfoEXT& ommBuildInfo,
    uint32_t& maxMicroTrianglesToBuild,
    bool forceBuild) {
    
    auto sourceDataIter = m_cachedSourceData.find(ommSrcHash);
    omm_validation_assert(sourceDataIter != m_cachedSourceData.end());
    CachedSourceData& sourceData = sourceDataIter->second;

    const uint32_t numMicroTrianglesPerTriangle = calculateNumMicroTriangles(ommCacheItem.subdivisionLevel);
    const uint32_t numTriangles = sourceData.numTriangles;
    const uint32_t numMicroTriangles = numTriangles * numMicroTrianglesPerTriangle;

    // OMM builds are at per OMM item granularity
    if (!forceBuild && numMicroTriangles > maxMicroTrianglesToBuild)
      return OmmResult::OutOfBudget;

    const uint8_t numOpacityMicromapBitsPerMicroTriangle = ommCacheItem.ommFormat == VK_OPACITY_MICROMAP_FORMAT_2_STATE_EXT ? 1 : 2;
    const uint32_t opacityMicromapPerTriangleBufferSize = dxvk::util::ceilDivide(numMicroTrianglesPerTriangle * numOpacityMicromapBitsPerMicroTriangle, 8);
    const uint32_t opacityMicromapBufferSize = numTriangles * opacityMicromapPerTriangleBufferSize;
    const VkIndexType triangleIndexType = numTriangles <= UINT16_MAX ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32;
    const uint32_t numBytesPerIndexElement = triangleIndexType == VK_INDEX_TYPE_UINT16 ? 2 : 4;
    ommCacheItem.blasOmmBuffers = new DxvkOpacityMicromap(*m_device);

    // Micromap forward definitions
    Rc<DxvkBuffer> triangleArrayBuffer;     // VkMicromapTriangleEXT per triangle
    
    // Fill out VkMicromapUsageEXT with size information
    // For now all triangles are in the same micromap group
    ommUsageGroup = {};
    ommUsageGroup.count = numTriangles;
    ommUsageGroup.subdivisionLevel = ommCacheItem.subdivisionLevel;
    ommUsageGroup.format = ommCacheItem.ommFormat;

    // Get micromap prebuild info
    ommBuildInfo = {};
    ommBuildInfo.sType = VK_STRUCTURE_TYPE_MICROMAP_BUILD_INFO_EXT;
    VkMicromapBuildSizesInfoEXT sizeInfo = { VK_STRUCTURE_TYPE_MICROMAP_BUILD_SIZES_INFO_EXT };
    calculateMicromapBuildInfo(ommUsageGroup, ommBuildInfo, sizeInfo);

    // Initialize micromap triangle index buffers
    {
      OmmResult result;
      if (triangleIndexType == VK_INDEX_TYPE_UINT16)
        result = initializeOpacityMicromapTriangleArrayBuffers<uint16_t>(
          m_device, ctx, ommCacheItem.ommFormat, ommCacheItem.subdivisionLevel, numTriangles, opacityMicromapPerTriangleBufferSize,
          triangleArrayBuffer, ommCacheItem.blasOmmBuffers->opacityMicromapTriangleIndexBuffer);
      else
        result = initializeOpacityMicromapTriangleArrayBuffers<uint32_t>(
          m_device, ctx, ommCacheItem.ommFormat, ommCacheItem.subdivisionLevel, numTriangles, opacityMicromapPerTriangleBufferSize,
          triangleArrayBuffer, ommCacheItem.blasOmmBuffers->opacityMicromapTriangleIndexBuffer);

      if (result != OmmResult::Success)
        return result;
    }

    // Create micromap
    {
      // Create buffer
      DxvkBufferCreateInfo ommBufferInfo = { VK_STRUCTURE_TYPE_MICROMAP_CREATE_INFO_EXT };
      ommBufferInfo.usage = VK_BUFFER_USAGE_MICROMAP_STORAGE_BIT_EXT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
      // ToDo: revisit. Access should be VK_ACCESS_2_MICROMAP_WRITE_BIT_EXT, but the EXT flag is not compatible here
      // The access is covered by a proper VkMemoryBarrier2 later
      ommBufferInfo.access = VK_ACCESS_MEMORY_WRITE_BIT;
      ommBufferInfo.size = sizeInfo.micromapSize;
      ommBufferInfo.requiredAlignmentOverride = 256;
      ommCacheItem.blasOmmBuffers->opacityMicromapBuffer = m_device->createBuffer(ommBufferInfo, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, DxvkMemoryStats::Category::RTXOpacityMicromap, "OMM micromap");

      if (ommCacheItem.blasOmmBuffers->opacityMicromapBuffer == nullptr) {
        ONCE(Logger::warn(str::format("[RTX - Opacity Micromap] Failed to build a micromap due to m_device->createBuffer() failing to allocate a buffer for size: ", ommBufferInfo.size)));
        return OmmResult::OutOfMemory;
      }

      // Create micromap
      VkMicromapCreateInfoEXT maCreateInfo = { VK_STRUCTURE_TYPE_MICROMAP_CREATE_INFO_EXT };
      maCreateInfo.createFlags = 0;
      maCreateInfo.buffer = ommCacheItem.blasOmmBuffers->opacityMicromapBuffer->getBufferRaw();
      maCreateInfo.offset = 0;
      maCreateInfo.size = sizeInfo.micromapSize;
      maCreateInfo.type = VK_MICROMAP_TYPE_OPACITY_MICROMAP_EXT;
      maCreateInfo.deviceAddress = 0ull;

      if (vkFailed(m_device->vkd()->vkCreateMicromapEXT(m_device->vkd()->device(), &maCreateInfo, nullptr, &ommCacheItem.blasOmmBuffers->opacityMicromap))) {
        ONCE(Logger::warn("[RTX Opacity Micromap] Failed to build a micromap. Ignoring the build request."));
        return OmmResult::Failure;
      }
    }
    
    // Calculate the required the scratch memory
    const size_t scratchAlignment = m_device->properties().khrDeviceAccelerationStructureProperties.minAccelerationStructureScratchOffsetAlignment;
    const size_t requiredScratchAllocSize = align(sizeInfo.buildScratchSize, scratchAlignment);

    // Build the array with vkBuildMicromapsEXT
    {
      // Fill in the pointers we didn't have at size query
      ommBuildInfo.dstMicromap = ommCacheItem.blasOmmBuffers->opacityMicromap;
      ommBuildInfo.data.deviceAddress = ommCacheItem.ommArrayBuffer->getDeviceAddress();
      assert(ommBuildInfo.data.deviceAddress % 256 == 0);
      ommBuildInfo.triangleArray.deviceAddress = triangleArrayBuffer->getDeviceAddress();
      assert(ommBuildInfo.triangleArray.deviceAddress % 256 == 0);
      ommBuildInfo.scratchData.deviceAddress = getScratchMemory(align(m_scratchMemoryUsedThisFrame + requiredScratchAllocSize, scratchAlignment))->getDeviceAddress() + m_scratchMemoryUsedThisFrame;
      assert(ommBuildInfo.scratchData.deviceAddress % scratchAlignment == 0);
      m_scratchMemoryUsedThisFrame += requiredScratchAllocSize;
      ommBuildInfo.triangleArrayStride = sizeof(VkMicromapTriangleEXT);
      
      ctx->getCommandList()->trackResource<DxvkAccess::Read>(ommCacheItem.ommArrayBuffer);
      ctx->getCommandList()->trackResource<DxvkAccess::Read>(triangleArrayBuffer);
      ctx->getCommandList()->trackResource<DxvkAccess::Write>(m_scratchBuffer);

      // Release OMM array memory as it's no longer needed after the build
      {
        m_memoryManager.release(ommCacheItem.arrayBufferDeviceSize);
        ommCacheItem.arrayBufferDeviceSize = 0;
        ommCacheItem.ommArrayBuffer = nullptr;
      }
    }

    // Update the BLAS desc with the built micromap
    {
      VkAccelerationStructureTrianglesOpacityMicromapEXT& ommBlasDesc = ommCacheItem.blasOmmBuffers->blasDesc;
      ommBlasDesc = VkAccelerationStructureTrianglesOpacityMicromapEXT { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_TRIANGLES_OPACITY_MICROMAP_EXT };
      ommBlasDesc.micromap = ommCacheItem.blasOmmBuffers->opacityMicromap;
      ommBlasDesc.indexType = triangleIndexType;
      ommBlasDesc.indexBuffer.deviceAddress = ommCacheItem.blasOmmBuffers->opacityMicromapTriangleIndexBuffer->getDeviceAddress();
      ommBlasDesc.indexStride = numBytesPerIndexElement;
      ommBlasDesc.baseTriangle = 0;
    }
  
    // Track the lifetime of all the build buffers needed for BLAS, including non-ref counted .opacityMicromap
    ctx->getCommandList()->trackResource<DxvkAccess::Write>(ommCacheItem.blasOmmBuffers);

    m_numMicroTrianglesBuilt += numMicroTriangles;
    maxMicroTrianglesToBuild -= std::min(numMicroTriangles, maxMicroTrianglesToBuild);

    // Source data is no longer needed
    deleteCachedSourceData(sourceDataIter, ommCacheItem.cacheState, true);

#ifdef VALIDATION_MODE
    Logger::warn(str::format("[RTX Opacity Micromap] m_cachedSourceData.erase(", ommSrcHash, ") by thread_id ", std::this_thread::get_id()));
#endif
    
    return OmmResult::Success;
  }

  void OpacityMicromapManager::bakeOpacityMicromapArrays(Rc<DxvkContext> ctx,
                                                         const std::vector<TextureRef>& textures,
                                                         uint32_t& availableBakingBudget) {

    if (!OpacityMicromapOptions::enableBakingArrays())
      return;

#ifdef VALIDATION_MODE
    for (auto iter0 = m_unprocessedList.begin(); iter0 != m_unprocessedList.end(); iter0++) {
      auto iter1 = iter0;
      iter1++;
      for (; iter1 != m_unprocessedList.end(); iter1++) {
        if (*iter1 == *iter0) {
          omm_validation_assert(0 && "Duplicate entries found in a list");
        }
      }
    }
    for (auto iter0 = m_cachedSourceData.begin(); iter0 != m_cachedSourceData.end(); iter0++) {
      OpacityMicromapCacheItem& ommCacheItem = m_ommCache[iter0->first];
      if ((ommCacheItem.cacheState <= OpacityMicromapCacheState::eStep0_Unprocessed) &&
           iter0->second.getInstance() == nullptr)
        omm_validation_assert(0 && "Instance is null at unexpected stage");
    }
#endif

    ScopedGpuProfileZone(ctx, "Bake Opacity Micromap Arrays");

    if (OpacityMicromapOptions::Building::enableUnlimitedBakingAndBuildingBudgets()) {
      availableBakingBudget = UINT32_MAX;
    }

    for (auto ommSrcHashIter = m_unprocessedList.begin(); ommSrcHashIter != m_unprocessedList.end() && availableBakingBudget > 0; ) {
      XXH64_hash_t ommSrcHash = *ommSrcHashIter;

#ifdef VALIDATION_MODE
      Logger::warn(str::format("[RTX Opacity Micromap] Baking ", ommSrcHash, " on thread_id ", std::this_thread::get_id()));
#endif

      auto sourceDataIter = m_cachedSourceData.find(ommSrcHash);
      auto cacheItemIter = m_ommCache.find(ommSrcHash);

      if (sourceDataIter == m_cachedSourceData.end() || cacheItemIter == m_ommCache.end()) {
        // Note: this shouldn't be hit anymore as it was triggered by destroying an instance
        // on a baking failure and destroying source data for all OMMs associated with that instance.
        // That included OMMs that were still in the unordered list. Now just the failed OMM gets destroyed.
        assert(0 && "OMM inconsistent state");
        ONCE(Logger::err("[RTX Opacity Micromap] Encountered inconsistent state. Opacity Micromap item listed for baking is missing required state data. Skipping it."));
        // First update the iterator, then destroy any omm data associated with it
        ommSrcHashIter++;
        destroyOmmData(ommSrcHash);
        continue;
      }

      CachedSourceData& sourceData = sourceDataIter->second;
      OpacityMicromapCacheItem& ommCacheItem = cacheItemIter->second;
      ommCacheItem.cacheState = OpacityMicromapCacheState::eStep1_Baking;

      OmmResult result = bakeOpacityMicromapArray(ctx, ommSrcHash, ommCacheItem, sourceData, textures, availableBakingBudget);

      if (result == OmmResult::Success) {
        // Use >= as the number of baked micro triangles is aligned up
        if (ommCacheItem.bakingState.numMicroTrianglesBaked >= ommCacheItem.bakingState.numMicroTrianglesToBake) {

          // Unlink the referenced RtInstance
          sourceData.setInstance(nullptr, m_instanceOmmRequests, *this);

          m_numTexelsPerMicroTriangle.erase(ommSrcHash);

          // Move the item from the unprocessed list to the end of the baked list
          ommCacheItem.cacheState = OpacityMicromapCacheState::eStep2_Baked;
          auto ommSrcHashIterToMove = ommSrcHashIter++;
          m_bakedList.splice(m_bakedList.end(), m_unprocessedList, ommSrcHashIterToMove);
          ommCacheItem.isUnprocessedCacheStateListIterValid = false;
        }
        else {
          // Do nothing, else path means all the budget has been used up and thus the loop will exit due to availableBakingBudget == 0
          //   so don't need to increment the iterator
          if (OpacityMicromapOptions::Building::enableUnlimitedBakingAndBuildingBudgets()) {
            ONCE(Logger::err("[RTX Opacity Micromap] Failed to fully bake an Opacity Micromap due to budget limits even with unlimited budgetting enabled."));
          }
        }
      } else if (result == OmmResult::OutOfMemory) {
        // Do nothing, try the next one
        ommSrcHashIter++;
        ONCE(Logger::debug("[RTX Opacity Micromap] Baking Opacity Micromap Array failed as ran out of memory."));
      } else if (result == OmmResult::DependenciesUnavailable) {
        // Textures not available - try the next one
        ommSrcHashIter++;
      } else if (result == OmmResult::Failure || 
                 result == OmmResult::Rejected) {
        if (result == OmmResult::Failure) {
          ONCE(Logger::warn(str::format("[RTX Opacity Micromap] Baking Opacity Micromap Array failed for hash ", ommSrcHash, ". Ignoring and black listing the hash.")));
        }
#ifdef VALIDATION_MODE
        Logger::warn(str::format("[RTX Opacity Micromap] Baking Opacity Micromap Array failed for hash ", ommSrcHash, ". Ignoring and black listing the hash."));
#endif
        // Baking failed, ditch the OMM data
        // First update the iterator, then remove the element
        ommSrcHashIter++;
        destroyOmmData(cacheItemIter);
        m_blackListedList.insert(ommSrcHash);
      } else { // OutOfBudget
        omm_validation_assert(0 && "Should not be hit");
        ommSrcHashIter++;
      }
#ifdef VALIDATION_MODE
      Logger::warn(str::format("[RTX Opacity Micromap] ~Baking ", ommSrcHash, " on thread_id ", std::this_thread::get_id()));
#endif
    }

    if (OpacityMicromapOptions::Building::enableUnlimitedBakingAndBuildingBudgets()) {
      availableBakingBudget = UINT32_MAX;
    }
  }

  void OpacityMicromapManager::buildOpacityMicromapsInternal(Rc<DxvkContext> ctx,
                                                             uint32_t& maxMicroTrianglesToBuild) {

    if (!OpacityMicromapOptions::enableBuilding())
      return;

#ifdef VALIDATION_MODE
    for (auto iter0 = m_bakedList.begin(); iter0 != m_bakedList.end(); iter0++) {
      auto iter1 = iter0;
      iter1++;
      for (; iter1 != m_bakedList.end(); iter1++) {
        if (*iter1 == *iter0) {
          omm_validation_assert(0 && "Duplicate entries found in a list");
        }
      }
      for (auto iter2 = m_unprocessedList.begin(); iter2 != m_unprocessedList.end(); iter2++) {
        if (*iter2 == *iter0) {
          omm_validation_assert(0 && "Two lists contain same OMM src hash");
        }
      }
    }
#endif

    ScopedGpuProfileZone(ctx, "Build Opacity Micromaps");

    // Pre-allocate the arrays because build infos include pointers to usage groups,
    // and reallocating vectors would invalidate these pointers
    const uint32_t maxBuildItems = m_bakedList.size();
    std::vector<VkMicromapUsageEXT> micromapUsageGroups(maxBuildItems);
    std::vector<VkMicromapBuildInfoEXT> micromapBuildInfos(maxBuildItems);
    uint32_t buildItemCount = 0;

    if (OpacityMicromapOptions::Building::enableUnlimitedBakingAndBuildingBudgets()) {
      maxMicroTrianglesToBuild = UINT32_MAX;
    }

    // Force at least one build since a build can't be split across frames even if doesn't fit within the budget
    // They're cheap regardless, so it should be fine.
    bool forceOmmBuild = maxMicroTrianglesToBuild > 0;  

    for (auto ommSrcHashIter = m_bakedList.begin(); ommSrcHashIter != m_bakedList.end() && maxMicroTrianglesToBuild > 0; ) {
      XXH64_hash_t ommSrcHash = *ommSrcHashIter;
#ifdef VALIDATION_MODE
      Logger::warn(str::format("[RTX Opacity Micromap] Building ", ommSrcHash, " on thread_id ", std::this_thread::get_id()));
#endif
      auto ommCacheItemIter = m_ommCache.find(ommSrcHash);
      OpacityMicromapCacheItem& ommCacheItem = ommCacheItemIter->second;

      OmmResult result = buildOpacityMicromap(ctx, *ommSrcHashIter, ommCacheItem, micromapUsageGroups[buildItemCount],
                                              micromapBuildInfos[buildItemCount], maxMicroTrianglesToBuild, forceOmmBuild);
      
      if (result == OmmResult::Success) {
        ommCacheItem.cacheState = OpacityMicromapCacheState::eStep3_Built;
        // Move the item from the baked list to the end of the built list
        auto ommSrcHashIterToMove = ommSrcHashIter++;
        m_builtList.splice(m_builtList.end(), m_bakedList, ommSrcHashIterToMove);
        ++buildItemCount;

        forceOmmBuild = false;
      }
      else if (result == OmmResult::Failure) {
#ifdef VALIDATION_MODE
        ONCE(Logger::warn(str::format("[RTX Opacity Micromap] Building Opacity Micromap failed for hash ", ommSrcHash, ".Ignoring and black listing the hash.")));
#endif
        // Building failed, ditch the OMM data
        // First update the iterator, then remove the element
        ommSrcHashIter++;
        destroyOmmData(ommCacheItemIter);
        m_blackListedList.insert(ommSrcHash);
      } else if (result == OmmResult::OutOfBudget) {
        // Do nothing, continue onto the next
        ommSrcHashIter++;

        if (OpacityMicromapOptions::Building::enableUnlimitedBakingAndBuildingBudgets()) {
          ONCE(Logger::err("[RTX Opacity Micromap] Failed to fully build an Opacity Micromap due to budget limits even with unlimited budgetting enabled."));
        }
      } else if (result == OmmResult::OutOfMemory) {
        // Do nothing, try the next one
        ommSrcHashIter++;
        ONCE(Logger::warn("[RTX Opacity Micromap] Building Opacity Micromap Array failed as it ran out of memory."));
      } else {
        omm_validation_assert(0 && "Should not be hit");
        ommSrcHashIter++;
      }
#ifdef VALIDATION_MODE
      Logger::warn(str::format("[RTX Opacity Micromap] ~Building ", ommSrcHash, " on thread_id ", std::this_thread::get_id()));
#endif

      if (OpacityMicromapOptions::Building::enableUnlimitedBakingAndBuildingBudgets()) {
        maxMicroTrianglesToBuild = UINT32_MAX;
      }
    }

    if (buildItemCount > 0) {
      // Add a barrier needed for Micromap build reading the triangleArrayBuffer's and triangleIndexBuffer's
      {
        VkMemoryBarrier2 memoryBarrier = { VK_STRUCTURE_TYPE_MEMORY_BARRIER_2, nullptr,
          VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
          VK_PIPELINE_STAGE_2_MICROMAP_BUILD_BIT_EXT, VK_ACCESS_SHADER_READ_BIT };
        VkDependencyInfo dependencyInfo = { VK_STRUCTURE_TYPE_DEPENDENCY_INFO };

        dependencyInfo.memoryBarrierCount = 1;
        dependencyInfo.pMemoryBarriers = &memoryBarrier;

        ctx->getCommandList()->vkCmdPipelineBarrier2KHR(&dependencyInfo);
      }

      // Build the micromaps
      ctx->getCommandList()->vkCmdBuildMicromapsEXT(buildItemCount, micromapBuildInfos.data());
    }
  }

  void OpacityMicromapManager::onFrameStart(Rc<DxvkContext> ctx) {
    ScopedCpuProfileZone();
    const uint32_t currentFrameIndex = m_device->getCurrentFrameId();

    m_numBoundOMMs = 0;
    m_numRequestedOMMBindings = 0;
    m_scratchMemoryUsedThisFrame = 0;

    // Clear caches if we need to rebuild OMMs
    {
      bool forceRebuildOMMs = OpacityMicromapOptions::enableResetEveryFrame();
      forceRebuildOMMs |= hasValueChanged(OpacityMicromapOptions::Building::ConservativeEstimation::enable(), 
                                          m_prevConservativeEstimationEnable);
      forceRebuildOMMs |= hasValueChanged(OpacityMicromapOptions::Building::ConservativeEstimation::maxTexelTapsPerMicroTriangle(), 
                                          m_prevConservativeEstimationMaxTexelTapsPerMicroTriangle);
      forceRebuildOMMs |= hasValueChanged(OpacityMicromapOptions::Building::ConservativeEstimation::minValidOMMTrianglesInMeshPercentage(),
                                          m_prevConservativeEstimationMinValidOMMTrianglesInMeshPercentage);
      forceRebuildOMMs |= hasValueChanged(OpacityMicromapOptions::Building::subdivisionLevel(), 
                                          m_prevBuildingSubdivisionLevel);
      forceRebuildOMMs |= hasValueChanged(OpacityMicromapOptions::Building::enableVertexAndTextureOperations(), 
                                          m_prevBuildingEnableVertexAndTextureOperations);

      if (forceRebuildOMMs) {
        clear();
        // Reset the black listed list as well since black listing depends on the settings
        m_blackListedList.clear();
      }
    }

    // Purge obsolete OMM build requests
    for (auto statIter = m_ommBuildRequestStatistics.begin(); statIter != m_ommBuildRequestStatistics.end();) {
      const uint32_t requestAge = currentFrameIndex - statIter->second.lastRequestFrameId;

      // Increment the iterator before any deletion
      auto currentStatIter = statIter++;

      if (requestAge > OpacityMicromapOptions::BuildRequests::maxRequestFrameAge())
        m_ommBuildRequestStatistics.erase(currentStatIter);
    }
    
    // Account for OMM usage in BLASes in a previous TLAS
    // Tag the previously bound OMMs as used in this frame as well
    if (RtxOptions::enablePreviousTLAS()) {
      for (auto& previousFrameBoundOMM : m_boundOMMs)
        ctx->getCommandList()->trackResource<DxvkAccess::Read>(previousFrameBoundOMM);
    }
    m_boundOMMs.clear();

    // Update memory management
    {
      m_memoryManager.updateMemoryBudget(ctx);

      if (m_memoryManager.getBudget() != 0) {
        const bool hasVRamBudgetDecreased = m_memoryManager.getBudget() < m_memoryManager.getPrevBudget();

        // Adjust missing memory if the budget is oversubscribed
        if (hasVRamBudgetDecreased) {
          const VkDeviceSize used = m_memoryManager.getUsed();
          const VkDeviceSize budget = m_memoryManager.getBudget();

          m_amountOfMemoryMissing = std::max(m_amountOfMemoryMissing, std::max(used, budget) - budget);
        }

        // Adjust missing memory amount by an amount that's already pending to be made available
        m_amountOfMemoryMissing -= std::min(m_amountOfMemoryMissing, m_memoryManager.calculatePendingAvailableSize());

        // LRU cache eviction
        if (m_amountOfMemoryMissing > 0) {

          // Start evicting least recently used items 
          for (auto lruOmmSrcHashIter = m_leastRecentlyUsedList.begin();
               lruOmmSrcHashIter != m_leastRecentlyUsedList.end() && m_amountOfMemoryMissing > m_memoryManager.calculatePendingAvailableSize();
               ) {
            auto cacheItemIter = m_ommCache.find(*lruOmmSrcHashIter);
            if (cacheItemIter == m_ommCache.end()) {
              auto iterToDelete = lruOmmSrcHashIter;
              // Increment the iterator before any deletion
              lruOmmSrcHashIter++;
              ONCE(Logger::err("[RTX] Failed to find Opacity Micromap cache entry on LRU eviction"));
              m_leastRecentlyUsedList.erase(iterToDelete);
              continue;
            }

            const uint32_t cacheItemUsageFrameAge = currentFrameIndex - cacheItemIter->second.lastUseFrameIndex;

            // Stop eviction once an item is recent enough
            if (cacheItemUsageFrameAge < OpacityMicromapOptions::Cache::minUsageFrameAgeBeforeEviction() &&
              // Force eviction if the VRAM budget decreased to speed fitting into the budget up
              !hasVRamBudgetDecreased)
              break;

            // Increment the iterator before any deletion
            lruOmmSrcHashIter++;

            destroyOmmData(cacheItemIter);
          }
        }
      } else { // budget == 0
        if (m_memoryManager.getPrevBudget() > 0) {
          clear();
        }
      }

      m_amountOfMemoryMissing = 0;

      // Call Memory Manager's onFrameStart last since any evicted buffers above 
      // were not used in this frame and thus should go to a pending release queue of the last frame
      m_memoryManager.onFrameStart();

      // Require at least 1MB (selected ad-hoc to cover at least a quad) of free budget to allow processing of new OMM items
      m_hasEnoughMemoryToPotentiallyGenerateAnOmm =
        m_memoryManager.getAvailable() >= 1 * 1024 * 1024;

      m_numMicroTrianglesBaked = 0;
      m_numMicroTrianglesBuilt = 0;
    }
  }

  void OpacityMicromapManager::onFrameEnd() {
    // Staging results are only needed for one frame, so purge them
    m_numTexelsPerMicroTriangleStaging.clear();

    m_numTrianglesToCalculateForNumTexelsPerMicroTriangle =
      OpacityMicromapOptions::Building::ConservativeEstimation::maxTrianglesToCalculateTexelDensityForPerFrame();

    // Register amount of free vidmem at the end of the frame to account for any intra-frame allocations.
    // This will be then used next frame to adjust budgeting
    m_memoryManager.registerVidmemFreeSize();
  }
  
  void OpacityMicromapManager::onFinishedBuilding() {
    // Release the scratch memory so it can be reused by rest of the frame.
    m_scratchBuffer = nullptr;
  }

  bool OpacityMicromapManager::isActive() const {
    return m_memoryManager.getBudget() > 0;
  }

  void OpacityMicromapManager::buildOpacityMicromaps(Rc<DxvkContext> ctx,
                                                     const std::vector<TextureRef>& textures,
                                                     uint32_t lastCameraCutFrameId) {

    // Get the workload scale in respect to 60 Hz for a given frame time.
    // 60 Hz is the baseline since that's what the per-second budgets have been parametrized at in RtxOptions
    const float kFrameTime60Hz = 1 / 60.f;
    const float frameTimeSecs = GlobalTime::get().deltaTime();
    float workloadScalePerSecond = frameTimeSecs / kFrameTime60Hz;

    // Modulate the scale for practical FPS range (i.e. <25, 200>) to even out the OMM's per frame percentage performance overhead
    {
      // Scale set to balance evening out performance overhead across FPS as well as not to stray too 
      // far from linear scaling so as not to slow down baking at very high FPS too much

      // Apply non-linear scaling only to an FPS range <25, 200> to avoid pow(t, x) blowing scaling out of proportion
      // Linear scaling will result in less overhead per frame for below 25 FPS, and in more overhead over 200 FPS
      if (frameTimeSecs >= 1 / 200.f && frameTimeSecs <= 1 / 25.f) {
        workloadScalePerSecond = powf(workloadScalePerSecond, 1.28f);
      } else if (frameTimeSecs > 1 / 25.f) {
        workloadScalePerSecond *= 1.278f; // == non-linear scale multiplier at 25 FPS
      } else {
        workloadScalePerSecond *= 0.714f; // == non-linear scale multiplier at 200 FPS
      }
    }

    // Convert the modulated workload scale back to frameTimeSecs's/per second base
    // since that's how the per-second budgets are expressed and can be multiplied with
    // to get the budget to use in this frame
    const float secondToFrameBudgetScale = workloadScalePerSecond * kFrameTime60Hz;

    // Initialize per frame budgets
    float numMillionMicroTrianglesToBakeAvailable = OpacityMicromapOptions::Building::maxMicroTrianglesToBakeMillionPerSecond() * secondToFrameBudgetScale;
    float numMillionMicroTrianglesToBuildAvailable = OpacityMicromapOptions::Building::maxMicroTrianglesToBuildMillionPerSecond() * secondToFrameBudgetScale;

    if (m_device->getCurrentFrameId() - lastCameraCutFrameId < OpacityMicromapOptions::Building::numFramesAtStartToBuildWithHighWorkload()) {
      numMillionMicroTrianglesToBakeAvailable *= OpacityMicromapOptions::Building::highWorkloadMultiplier();
      numMillionMicroTrianglesToBuildAvailable *= OpacityMicromapOptions::Building::highWorkloadMultiplier();
    }

    float fNumMicroTrianglesToBakeAvailable = numMillionMicroTrianglesToBakeAvailable * 1e6f;
    uint32_t numMicroTrianglesToBakeAvailable = fNumMicroTrianglesToBakeAvailable < UINT32_MAX ? static_cast<uint32_t>(fNumMicroTrianglesToBakeAvailable) : UINT32_MAX;
    float fNumMicroTrianglesToBuildAvailable = numMillionMicroTrianglesToBuildAvailable * 1e6f;
    uint32_t numMicroTrianglesToBuildAvailable = fNumMicroTrianglesToBuildAvailable < UINT32_MAX ? static_cast<uint32_t>(fNumMicroTrianglesToBuildAvailable) : UINT32_MAX;

    // Generate opacity micromaps
    if (!m_unprocessedList.empty() || !m_bakedList.empty()) {
      ScopedGpuProfileZone(ctx, "Process Opacity Micromaps");

      bakeOpacityMicromapArrays(ctx, textures, numMicroTrianglesToBakeAvailable);
      buildOpacityMicromapsInternal(ctx, numMicroTrianglesToBuildAvailable);

      // Purge instances queued for deletion
      for (const RtInstance* instance : m_instancesToDestroy) {
        destroyInstance(*instance);
      }
      m_instancesToDestroy.clear();
    }
  }
}  // namespace dxvk
