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
#include "rtx_texturemanager.h"
#include "../../util/thread.h"
#include "../../util/rc/util_rc_ptr.h"
#include "dxvk_context.h"
#include "dxvk_device.h"
#include "dxvk_scoped_annotation.h"
#include <chrono>

#include "rtx_texture.h"
#include "rtx_io.h"

namespace dxvk {
  void RtxTextureManager::work(Rc<ManagedTexture>& texture, Rc<DxvkContext>& ctx, Rc<DxvkCommandList>& cmd) {
#ifdef WITH_RTXIO
    if (m_kickoff || m_dropRequests) {
      if (RtxIo::enabled()) {
        RtxIo::get().flush(!m_dropRequests);
      }
      m_kickoff = false;
    }
#endif

    const bool alwaysWait = RtxOptions::Get()->alwaysWaitForAsyncTextures();

    // Wait until the next frame since the texture's been queued for upload, to relieve some pressure from frames
    // where many new textures are created by the game. In that case, texture uploads slow down the main and CS threads,
    // thus making the frame longer.
    // Note: RTX IO will manage dispatches on its own and does not need to be cooled down.
    if (!RtxIo::enabled()) {
      while (!m_dropRequests && !hasStopped() && !alwaysWait && texture->frameQueuedForUpload >= m_device->getCurrentFrameId())
        Sleep(1);
    }

    if (m_dropRequests) {
      texture->state = ManagedTexture::State::kFailed;
      texture->demote();
    } else
      uploadTexture(texture, ctx);
  }

  RtxTextureManager::RtxTextureManager(const Rc<DxvkDevice>& device)
    : RenderProcessor(device.ptr(), "rtx-texture-manager")
    , m_device(device) {
  }

  RtxTextureManager::~RtxTextureManager() {
  }

  void RtxTextureManager::scheduleTextureUpload(TextureRef& texture, Rc<DxvkContext>& immediateContext, bool allowAsync) {
    const Rc<ManagedTexture>& managedTexture = texture.getManagedTexture();
    if (managedTexture.ptr() == nullptr)
      return;

    switch (managedTexture->state) {
    case ManagedTexture::State::kVidMem:
      if (texture.finalizePendingPromotion()) {
        // Texture reached its final destination
        return;
      }
      break;
    case ManagedTexture::State::kQueuedForUpload:
#ifdef WITH_RTXIO
      if (RtxIo::enabled()) {
        if (RtxIo::get().isComplete(managedTexture->completionSyncpt)) {
          managedTexture->state = ManagedTexture::State::kVidMem;
          texture.finalizePendingPromotion();
        }
      }
#endif
      return;
    case ManagedTexture::State::kFailed:
    case ManagedTexture::State::kHostMem:
      // We need to schedule an upload
      break;
    }

    int preloadMips = allowAsync ? calcPreloadMips(managedTexture->futureImageDesc.mipLevels) : managedTexture->futureImageDesc.mipLevels;

    if (RtxIo::enabled()) {
      // When we get here with a texture in VID mem, the texture is considered already preloaded with RTXIO
      preloadMips = managedTexture->state != ManagedTexture::State::kVidMem ? preloadMips : 0;
    }

    if (preloadMips) {
      try {
        assert(managedTexture->linearImageDataSmallMips);

        int largestMipToPreload = managedTexture->futureImageDesc.mipLevels - uint32_t(preloadMips);
        if (largestMipToPreload < managedTexture->numLargeMips && managedTexture->linearImageDataLargeMips == nullptr) {
          TextureUtils::loadTexture(managedTexture, m_device, immediateContext, TextureUtils::MemoryAperture::HOST, TextureUtils::MipsToLoad::LowMips);
        }
        
        TextureUtils::promoteHostToVid(m_device, immediateContext, managedTexture, largestMipToPreload);
      } catch(const DxvkError& e) {
        managedTexture->state = ManagedTexture::State::kFailed;
        Logger::err("Failed to create image for VidMem promotion!");
        Logger::err(e.message());
        return;
      }
    }
    
    const bool asyncUpload = (preloadMips < managedTexture->futureImageDesc.mipLevels);
    if (asyncUpload) {
      managedTexture->state = ManagedTexture::State::kQueuedForUpload;
      managedTexture->frameQueuedForUpload = m_device->getCurrentFrameId();

      RenderProcessor::add(std::move(managedTexture));
    } else {
      // if we're not queueing for upload, make sure we don't hang on to low mip data
      if (managedTexture->linearImageDataLargeMips) {
        managedTexture->linearImageDataLargeMips.reset();
      }
    }
  }

  void RtxTextureManager::unloadTexture(const Rc<ManagedTexture>& texture) {
    texture->demote();
  }

  void RtxTextureManager::synchronize(bool dropRequests) {
    ScopedCpuProfileZone();
    
    m_dropRequests = dropRequests;

    RenderProcessor::sync();

    m_dropRequests = false;
  }

  void RtxTextureManager::kickoff() {
    if (m_itemsPending == 0) {
      m_kickoff = true;
      m_condOnAdd.notify_one();
    }
  }

  int RtxTextureManager::calcPreloadMips(int mipLevels) {
    if (RtxOptions::Get()->enableAsyncTextureUpload()) {
      return clamp(RtxOptions::Get()->asyncTextureUploadPreloadMips(), 0, mipLevels);
    } else {
      return mipLevels;
    }
  }

  void RtxTextureManager::uploadTexture(const Rc<ManagedTexture>& texture, Rc<DxvkContext>& ctx) {
    ScopedCpuProfileZone();

    if (texture->state != ManagedTexture::State::kQueuedForUpload)
      return;

    try {
      if (!RtxIo::enabled()) {
        assert(texture->numLargeMips > 0);
        assert(!texture->linearImageDataLargeMips);
      }

      TextureUtils::loadTexture(texture, m_device, ctx, TextureUtils::MemoryAperture::HOST, TextureUtils::MipsToLoad::LowMips);

      if (!RtxIo::enabled()) {
        TextureUtils::promoteHostToVid(m_device, ctx, texture);
        ctx->flushCommandList();
        texture->linearImageDataLargeMips.reset();
      }
    } catch (const DxvkError& e) {
      texture->state = ManagedTexture::State::kFailed;
      Logger::err("Failed to finish texture promotion to VidMem!");
      Logger::err(e.message());
    }
  }

  Rc<ManagedTexture> RtxTextureManager::preloadTexture(const Rc<AssetData>& assetData,
    ColorSpace colorSpace, const Rc<DxvkContext>& context, bool forceLoad) {

    const XXH64_hash_t hash = assetData->hash();

    auto it = m_textures.find(hash);
    if (it != m_textures.end()) {
      return it->second;
    }

    auto texture = TextureUtils::createTexture(assetData, colorSpace);

    TextureUtils::loadTexture(texture,
      m_device,
      context,
      TextureUtils::MemoryAperture::HOST,
      forceLoad ? TextureUtils::MipsToLoad::All : TextureUtils::MipsToLoad::HighMips,
      m_minimumMipLevel);

    // The content suggested we keep this texture always loaded, never demote.
    texture->canDemote = !forceLoad;

    return m_textures.emplace(hash, texture).first->second;
  }

  void RtxTextureManager::releaseTexture(const Rc<ManagedTexture>& texture) {
    if (texture != nullptr) {
      unloadTexture(texture);

      m_textures.erase(texture->assetData->hash());
    }
  }

  void RtxTextureManager::demoteTexturesFromVidmem() {
    for (const auto& pair : m_textures) {
      unloadTexture(pair.second);
    }
  }

  uint32_t RtxTextureManager::updateMipMapSkipLevel(const Rc<DxvkContext>& context) {
    const unsigned int MiBPerGiB = 1024;
    RtxContext* rtxContext = dynamic_cast<RtxContext*>(context.ptr());

    // Check and reserve GPU memory

    VkPhysicalDeviceMemoryProperties memory = m_device->adapter()->memoryProperties();
    DxvkAdapterMemoryInfo memHeapInfo = m_device->adapter()->getMemoryHeapInfo();
    VkDeviceSize availableMemorySizeMib = 0;
    for (uint32_t i = 0; i < memory.memoryHeapCount; i++) {
      bool isDeviceLocal = memory.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT;
      if (!isDeviceLocal) {
        continue;
      }

      VkDeviceSize memSizeMib = memHeapInfo.heaps[i].memoryBudget >> 20;
      VkDeviceSize memUsedMib = memHeapInfo.heaps[i].memoryAllocated >> 20;

      availableMemorySizeMib = std::max(memSizeMib - memUsedMib, availableMemorySizeMib);
    }

    if (rtxContext && !rtxContext->getResourceManager().isResourceReady()) {
      // Reserve space for various non-texture GPU resources (buffers, etc)

      const auto adaptiveResolutionReservedGPUMemoryMiB =
        static_cast<std::int32_t>(RtxOptions::Get()->adaptiveResolutionReservedGPUMemoryGiB() * MiBPerGiB);

      // Note: int32_t used for clamping behavior on underflow.
      availableMemorySizeMib = std::max(static_cast<std::int32_t>(availableMemorySizeMib) - adaptiveResolutionReservedGPUMemoryMiB, 0);
    }

    // Check and reserve CPU memory

    uint64_t availableSystemMemorySizeByte;
    if (dxvk::env::getAvailableSystemPhysicalMemory(availableSystemMemorySizeByte)) {
      // Reserve space for non-texture CPU resources
      // Note: This is done as this function is invoked during initialization, and the game may not have loaded other data yet
      // which would cause the textures to occasionally starve the rest of Remix for resources if some is not reserved.
      // TODO: The OpacityMicromapMemoryManager also allocate memory adaptively and it may eat up the memory
      // saved here. Need to figure out a way to control global memory consumption.

      const auto adaptiveResolutionReservedCPUMemoryMiB =
        static_cast<std::int32_t>(RtxOptions::Get()->adaptiveResolutionReservedCPUMemoryGiB() * MiBPerGiB);
      // Note: int32_t used for clamping behavior on underflow.
      const VkDeviceSize assetReservedSizeMib =
        std::max(static_cast<std::int32_t>(availableSystemMemorySizeByte >> 20u) - adaptiveResolutionReservedCPUMemoryMiB, 0);

      availableMemorySizeMib = std::min(availableMemorySizeMib, assetReservedSizeMib);
    }

    // Determine the minimum mip level to load

    unsigned int assetSizeMib = RtxOptions::Get()->assetEstimatedSizeGiB() * MiBPerGiB;
    for (m_minimumMipLevel = 0; assetSizeMib > availableMemorySizeMib && m_minimumMipLevel < 2; m_minimumMipLevel++) {
      // Skip one more mip map level
      // Note: Removing the top-most mip reduces memory consumption by 25% if mips are assumed to be an infinite geometric
      // series. In reality though they are not as they terminate at a 1x1 mip, so this is actually a conservative estimate
      // which will overestimate how much memory a texture will take at times (fairly accurate to the ideal solution though
      // across most mip levels except the very highest few where the missing contribution becomes significant).
      assetSizeMib /= 4;
    }

    return m_minimumMipLevel;
  }

}
