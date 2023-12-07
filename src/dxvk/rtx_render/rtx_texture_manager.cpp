/*
* Copyright (c) 2022-2023, NVIDIA CORPORATION. All rights reserved.
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
#include "rtx_texture_manager.h"
#include "../../util/thread.h"
#include "../../util/rc/util_rc_ptr.h"
#include "dxvk_context.h"
#include "dxvk_device.h"
#include "dxvk_scoped_annotation.h"
#include <chrono>

#include "rtx_texture.h"
#include "rtx_io.h"

namespace dxvk {
  constexpr VkDeviceSize MiBPerGiB = 1024;
  // Remix needs at least two frames to completely evict the demoted textures.
  // After a global texture demotion event, texture manager will delay future texture promotions
  // by this number of frames to make sure the previously used memory is released and there
  // will be no overcommit.
  constexpr uint32_t kPromotionDelayFrames = 2;

  void RtxTextureManager::work(Rc<ManagedTexture>& texture, Rc<DxvkContext>& ctx) {
    if (m_dropRequests) {
      flushRtxIo(false);
    }

    const bool alwaysWait = RtxOptions::Get()->alwaysWaitForAsyncTextures();

    // Wait until the next frame since the texture's been queued for upload, to relieve some pressure from frames
    // where many new textures are created by the game. In that case, texture uploads slow down the main and CS threads,
    // thus making the frame longer.
    // Note: RTX IO will manage dispatches on its own and does not need to be cooled down.
    if (!RtxIo::enabled()) {
      while (!m_dropRequests && !hasStopped() && !alwaysWait && texture->frameQueuedForUpload >= m_pDevice->getCurrentFrameId())
        Sleep(1);
    }

    if (m_dropRequests) {
      texture->state = ManagedTexture::State::kFailed;
      texture->demote();
    } else {
      loadTexture(texture, ctx);
    }
  }

  RtxTextureManager::RtxTextureManager(DxvkDevice* device)
    : RenderProcessor(device, "rtx-texture-manager")
    , m_pDevice(device) {
  }

  RtxTextureManager::~RtxTextureManager() {
  }

  void RtxTextureManager::initialize(const Rc<DxvkContext>& ctx) {
    // Kick off upload thread
    RenderProcessor::start();
  }

  static ManagedTexture::State processManagedTextureState(const TextureRef& texture) {
    const Rc<ManagedTexture>& managedTexture = texture.getManagedTexture();
    if (managedTexture == nullptr)
      return ManagedTexture::State::kUnknown;

    switch (managedTexture->state) {
    case ManagedTexture::State::kQueuedForUpload:
#ifdef WITH_RTXIO
      if (RtxIo::enabled()) {
        if (RtxIo::get().isComplete(managedTexture->completionSyncpt)) {
          managedTexture->state = ManagedTexture::State::kVidMem;
        }
      }
#endif
      break;
    case ManagedTexture::State::kFailed:
      managedTexture->demote();
      break;
    }

    return managedTexture->state;
  }

  void RtxTextureManager::scheduleTextureLoad(TextureRef& texture, Rc<DxvkContext>& immediateContext, bool allowAsync) {
    if (m_pDevice->getCurrentFrameId() < m_promotionStartFrame) {
      return;
    }

    const auto managedState = processManagedTextureState(texture);

    if (managedState == ManagedTexture::State::kVidMem) {
      // We have texture in vidmem - attempt to finalize the promotion so that it can be used for rendering
      if (texture.finalizePendingPromotion()) {
        // Texture reached its final destination
        return;
      }
    } else if (managedState == ManagedTexture::State::kQueuedForUpload) {
      // Texture is still in-flight
      return;
    }

    const Rc<ManagedTexture>& managedTexture = texture.getManagedTexture();
    if (managedTexture == nullptr)
      return;

    if (m_itemsPending == 0) {
      // We're about to start a batch
      m_batchStartTime = dxvk::high_resolution_clock::now();
    }

    // Note: if texture was not preloaded or was demoted then we must preload it now.
    // The preloaded content remains in host memory after demotion so this would be
    // a fast upload operation. Suboptimal textures are never preloaded and so do not stay
    // in host memory.
    if (managedTexture->minPreloadedMip < 0 && !isTextureSuboptimal(managedTexture)) {
      const int largestMipToPreload = managedTexture->mipCount - calcPreloadMips(managedTexture->mipCount);
      TextureUtils::loadTexture(managedTexture, immediateContext, true, largestMipToPreload);

      if (texture.finalizePendingPromotion()) {
        // We're done.
        return;
      }
    }

    // If texture is still not preloaded disallow async load and do it in-sync.
    // This is likely a suboptimal texture.
    allowAsync = allowAsync && managedTexture->minPreloadedMip > 0;

    managedTexture->state = ManagedTexture::State::kQueuedForUpload;
    managedTexture->frameQueuedForUpload = m_pDevice->getCurrentFrameId();

    if (!allowAsync) {
      loadTexture(managedTexture, immediateContext);
    } else {
      RenderProcessor::add(std::move(managedTexture));
    }
  }

  void RtxTextureManager::synchronize(bool dropRequests) {
    ScopedCpuProfileZone();
    
    m_dropRequests = dropRequests;

    RenderProcessor::sync();

    m_dropRequests = false;

    flushRtxIo(false);
  }

  void RtxTextureManager::kickoff() {
    if (m_itemsPending == 0) {
      m_kickoff = true;
      m_condOnAdd.notify_one();
    } else if (m_preloadInflight) {
      // If there's any preloads in-flight dispatch the RTX IO job immediately to improve
      // visual responsiveness when image assets are processed asynchronously.
      flushRtxIo(false);
      m_preloadInflight = false;
    }

    m_pDevice->statCounters().setCtr(DxvkStatCounter::RtxTexturesInFlight, m_itemsPending.load());
  }

  void RtxTextureManager::finalizeAllPendingTexturePromotions() {
    ScopedCpuProfileZone();
    for (auto& texture : m_textureCache.getObjectTable()) {
      if (texture.isPromotable()) {
        if (processManagedTextureState(texture) == ManagedTexture::State::kVidMem) {
          // We have texture in vidmem - attempt to finalize the promotion
          if (!texture.finalizePendingPromotion()) {
#ifdef _DEBUG
            Logger::debug(str::format("Unable to finalize pending promotion for ",
                                      texture.getManagedTexture()->assetData->info().filename));
#endif
          }
        }
      }
    }
  }

  void RtxTextureManager::addTexture(Rc<DxvkContext>& immediateContext, TextureRef inputTexture, bool allowAsync, uint32_t& textureIndexOut) {
    // If theres valid texture backing this ref, then skip
    if (!inputTexture.isValid())
      return;

    // Track this texture
    textureIndexOut = m_textureCache.track(inputTexture);

    // Fetch the texture object from cache
    TextureRef& cachedTexture = m_textureCache.at(textureIndexOut);

    // If there is a pending promotion, schedule it
    if (cachedTexture.isPromotable()) {
      scheduleTextureLoad(cachedTexture, immediateContext, allowAsync);
    }

    cachedTexture.frameLastUsed = m_pDevice->getCurrentFrameId();
  }


  void RtxTextureManager::demoteAllTextures() {
    ScopedCpuProfileZone();

    for (const auto& pair : m_assetHashToTextures) {
      pair.second->demote();
    }
  }

  void RtxTextureManager::clear() {
    ScopedCpuProfileZone();

    demoteAllTextures();

    m_textureCache.clear();

    // Reset texture budget.
    m_textureBudgetMib = 0;

    // Throttle future promotions so that the cleared textures
    // have time to move out and release memory, but do not delay
    // promotions for the first frame to have stable image tests.
    if (m_pDevice->getCurrentFrameId() > 0) {
      m_promotionStartFrame = m_pDevice->getCurrentFrameId() + kPromotionDelayFrames;
    }
  }

  void RtxTextureManager::garbageCollection() {
    ScopedCpuProfileZone();
    // Demote high res material textures
    if (m_pDevice->getCurrentFrameId() > RtxOptions::Get()->numFramesToKeepMaterialTextures()) {
      const size_t oldestFrame = m_pDevice->getCurrentFrameId() - RtxOptions::Get()->numFramesToKeepMaterialTextures();

      for (auto& texture : m_textureCache.getObjectTable()) {
        const bool isDemotable = texture.getManagedTexture() != nullptr && texture.getManagedTexture()->canDemote;
        if (isDemotable){
          const bool shouldEvict = (texture.frameLastUsed < oldestFrame);
          if (shouldEvict) {
            texture.demote();
          }
        }
      }
    }
  }

  int RtxTextureManager::calcPreloadMips(int mipLevels) {
    if (RtxOptions::Get()->enableAsyncTextureUpload()) {
      return clamp(RtxOptions::Get()->asyncTextureUploadPreloadMips(), 0, mipLevels);
    } else {
      return mipLevels;
    }
  }

  XXH64_hash_t RtxTextureManager::getUniqueKey() {
    static uint64_t ID;
    XXH64_hash_t key;

    do {
#ifdef _DEBUG
      assert(ID + 1 > ID && "Texture hash key id rollover detected!");
#endif
      ++ID;
      key = XXH3_64bits(&ID, sizeof(ID));
    } while (key == kInvalidTextureKey);

    return key;
  }

  void RtxTextureManager::loadTexture(const Rc<ManagedTexture>& texture, Rc<DxvkContext>& ctx) {
    ScopedCpuProfileZone();

    if (texture->state != ManagedTexture::State::kQueuedForUpload)
      return;

    if (m_textureBudgetMib == 0) {
      updateMemoryBudgets(ctx);
    }

    try {
      uint32_t largestMipToLoad = 0;

      const uint32_t kPercentageOfBudgetConsideredSpilling = 75;
      const VkDeviceSize spillMib = overBudgetMib(kPercentageOfBudgetConsideredSpilling);

      // If we're over budget, aggressively limit the texture resolution for new textures, every 512Mib we go over budget
      if (spillMib) {
        const uint32_t kReduceMipsEveryMib = 512;
        largestMipToLoad += spillMib / kReduceMipsEveryMib;
      }

      TextureUtils::loadTexture(texture, ctx, false, largestMipToLoad);

#ifdef _DEBUG
      Logger::debug(str::format("Loaded texture ", texture->assetData->hash(), " at ",
                                texture->assetData->info().filename, " largest level ", largestMipToLoad));
#endif

      if (!RtxIo::enabled()) {
        ctx->flushCommandList();
      }
    } catch (const DxvkError& e) {
      texture->state = ManagedTexture::State::kFailed;
      Logger::err("Failed to load texture!");
      Logger::err(e.message());
    }
  }

  VkDeviceSize RtxTextureManager::overBudgetMib(VkDeviceSize percentageOfBudget) const {
    // Get the current memory usage for material textures
    VkDeviceSize currentUsageMib = 0;
    for (uint32_t i = 0; i < m_pDevice->adapter()->memoryProperties().memoryHeapCount; i++) {
      bool isDeviceLocal = m_pDevice->adapter()->memoryProperties().memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT;
      if (isDeviceLocal) {
        currentUsageMib += m_pDevice->getMemoryStats(i).usedByCategory(DxvkMemoryStats::Category::RTXMaterialTexture) >> 20;
      }
    }

    VkDeviceSize budgetMib = m_textureBudgetMib * percentageOfBudget / 100;

    // If we're under budget, great
    if (currentUsageMib < budgetMib)
      return 0;

    // Return the spill overbudget
    return (currentUsageMib - budgetMib);
  }

  bool RtxTextureManager::isTextureSuboptimal(const Rc<ManagedTexture>& texture) const {
    const auto& extent = texture->assetData->info().extent;

    // Large textures that have only a single mip level are considered suboptimal since they
    // may cause high pressure on memory and/or cause hitches when loaded at runtime.
    const bool result = texture->mipCount == 1 && (extent.width * extent.height >= 512 * 512);

    if (result) {
      Logger::warn(str::format("A suboptimal replacement texture detected: ",
                                    texture->assetData->info().filename,
                                    "! Please make sure all replacement textures have mip-maps."));
    }

    return result;
  }

  Rc<ManagedTexture> RtxTextureManager::preloadTextureAsset(const Rc<AssetData>& assetData,
    ColorSpace colorSpace, const Rc<DxvkContext>& context, bool forceLoad) {

    const XXH64_hash_t hash = assetData->hash();

    auto it = m_assetHashToTextures.find(hash);
    if (it != m_assetHashToTextures.end()) {
      return it->second;
    }

    {
      VkFormatProperties properties = m_pDevice->adapter()->formatProperties(assetData->info().format);

      if (!(properties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) ||
          !(properties.optimalTilingFeatures & VK_FORMAT_FEATURE_TRANSFER_DST_BIT)) {
        std::ostringstream formatStr;
        formatStr << assetData->info().format;

        Logger::err(str::format(
          "Ignoring replacement texture with unsupported format [", formatStr.str(), "]: ",
          assetData->info().filename));
        return {};
      }
    }

    // Create managed texture
    auto texture = TextureUtils::createTexture(assetData, colorSpace);

    // Skip suboptimal textures
    const bool skipPreload = isTextureSuboptimal(texture);

    // Preload texture contents
    if (!skipPreload || forceLoad) {
      const int largestMipToPreload = forceLoad ? 0 : texture->mipCount - calcPreloadMips(texture->mipCount);
      TextureUtils::loadTexture(texture, context, !forceLoad, largestMipToPreload);

      // Execute the command list asap to improve visual responsiveness when
      // replacements are processed asynchronously
      if (!RtxIo::enabled()) {
        context->flushCommandList();
      }

      m_preloadInflight |= !forceLoad;

#ifdef _DEBUG
      Logger::debug(str::format(forceLoad ? "Loaded" : "Preloaded", " texture ", hash, " at ",
                                assetData->info().filename, " largest level ", largestMipToPreload));
#endif
    }

    // The content suggested we keep this texture always loaded, never demote.
    texture->canDemote = !forceLoad;

    return m_assetHashToTextures.emplace(hash, texture).first->second;
  }

  void RtxTextureManager::updateMemoryBudgets(const Rc<DxvkContext>& context) {
    // Check and reserve GPU memory

    VkPhysicalDeviceMemoryProperties memory = m_pDevice->adapter()->memoryProperties();
    DxvkAdapterMemoryInfo memHeapInfo = m_pDevice->adapter()->getMemoryHeapInfo();
    VkDeviceSize availableMemorySizeMib = 0;
    for (uint32_t i = 0; i < memory.memoryHeapCount; i++) {
      bool isDeviceLocal = memory.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT;
      if (!isDeviceLocal) {
        continue;
      }

      VkDeviceSize remixFreeMemMib = (m_pDevice->getMemoryStats(i).totalAllocated() >> 20) -
        (m_pDevice->getMemoryStats(i).totalUsed() >> 20);

      VkDeviceSize memBudgetMib = memHeapInfo.heaps[i].memoryBudget >> 20;
      VkDeviceSize memUsedMib = (memHeapInfo.heaps[i].memoryAllocated >> 20) - remixFreeMemMib;

      if (memBudgetMib > memUsedMib) {
        availableMemorySizeMib = std::max(memBudgetMib - memUsedMib, availableMemorySizeMib);
      }
    }

    if (!context->getCommonObjects()->getResources().isResourceReady()) {
      // Reserve space for various non-texture GPU resources (buffers, etc)

      const auto adaptiveResolutionReservedGPUMemoryMiB =
        static_cast<std::int32_t>(RtxOptions::Get()->adaptiveResolutionReservedGPUMemoryGiB() * MiBPerGiB);

      // Note: int32_t used for clamping behavior on underflow.
      availableMemorySizeMib = std::max(static_cast<std::int32_t>(availableMemorySizeMib) - adaptiveResolutionReservedGPUMemoryMiB, 0);
    }

    m_textureBudgetMib = availableMemorySizeMib * budgetPercentageOfAvailableVram() / 100;
  }

  void RtxTextureManager::flushRtxIo(bool async) {
#ifdef WITH_RTXIO
    if (RtxIo::enabled()) {
      RtxIo::get().flush(async);
    }
#endif
  }

  bool RtxTextureManager::wakeWorkerCondition() {
    if (m_kickoff) {
      flushRtxIo(true);
      m_kickoff = false;

      // Report batch duration when all items have been processed.
      // We want to capture rtxio flush time so we check time after a flush in a kickoff event
      // that is fired at a frame boundary when the number of pending items drops to zero.

      constexpr auto zeroTimePoint = dxvk::high_resolution_clock::time_point(dxvk::high_resolution_clock::duration(0));

      if (m_itemsPending == 0 && m_batchStartTime != zeroTimePoint) {
        m_lastBatchDuration = dxvk::high_resolution_clock::now() - m_batchStartTime;
        m_batchStartTime = zeroTimePoint;

#ifdef _DEBUG
        Logger::debug(str::format("Texture manager batch time: ",
          std::chrono::duration_cast<std::chrono::milliseconds>(m_lastBatchDuration).count(), "ms"));
#endif

        m_pDevice->statCounters().setCtr(DxvkStatCounter::RtxLastTextureBatchDuration,
          std::chrono::duration_cast<std::chrono::milliseconds>(m_lastBatchDuration).count());
      }
    }
    return RenderProcessor::wakeWorkerCondition();
  }

}
