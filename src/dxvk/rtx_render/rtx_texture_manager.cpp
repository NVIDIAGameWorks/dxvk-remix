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

#include "dxvk_device.h"
#include "rtx_texture_manager.h"
#include "../../util/thread.h"
#include "../../util/rc/util_rc_ptr.h"
#include "dxvk_context.h"
#include "dxvk_scoped_annotation.h"
#include <chrono>

#include "rtx_asset_data_manager.h"
#include "rtx_bindless_resource_manager.h"
#include "rtx_file_watch.h"
#include "rtx_texture.h"
#include "rtx_io.h"
#include "rtx_staging_ring.h"


namespace dxvk {


  size_t g_streamedTextures_budgetBytes = 0;
  size_t g_streamedTextures_usedBytes   = 0;

  constexpr size_t Megabytes = 1024 * 1024;

  static size_t calcTextureMemoryBudget_Megabytes(DxvkDevice* device);
  
  // Returns current VRAM usage by material textures in bytes
  static size_t calcCurrentTextureUsageBytes(DxvkDevice* device) {
    size_t usageBytes = 0;
    for (uint32_t i = 0; i < device->adapter()->memoryProperties().memoryHeapCount; i++) {
      const bool isDeviceLocal = device->adapter()->memoryProperties().memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT;
      if (isDeviceLocal) {
        usageBytes += device->getMemoryStats(i).usedByCategory(DxvkMemoryStats::Category::RTXMaterialTexture);
      }
    }
    return usageBytes;
  }
  
  // Returns texture memory budget in bytes
  static size_t calcTextureMemoryBudgetBytes(DxvkDevice* device) {
    return calcTextureMemoryBudget_Megabytes(device) * Megabytes;
  }
  
  // Returns true if current texture VRAM usage exceeds budget
  static bool isOverTextureBudget(DxvkDevice* device) {
    return calcCurrentTextureUsageBytes(device) > calcTextureMemoryBudgetBytes(device);
  }

  static size_t stagingBufferSize_Bytes() {
    return std::max(32, RtxOptions::TextureManager::stagingBufferSizeMiB()) * 1024 * 1024;
  }


#define SAMPLER_FEEDBACK_RELATED_PER_TEX 8


  namespace {

    // A staging buffer slice that holds the data of a single mip.
    struct ReadyToCopyMip {
      DxvkBufferSlice srcBuffer;
      VkExtent3D      mipExtent;
      uint32_t        mipLevel;
    };

    // A range of mips to copy to the 'dstTexture'.
    // So that the Vulkan thread would only call 'vkCmdCopyBufferToImage'.
    struct ReadyToCopy {
      Rc<ManagedTexture>          dstTexture;
      std::vector<ReadyToCopyMip> mips; // TODO: fixed size instead of dynamic std::vector
      uint16_t                    mip_begin;
      uint16_t                    mip_end;
      RtxStagingRing*             stagingbuf;
    };

    // A range of mips to copy to the 'dstTexture'.
    // A specialization for RTXIO.
    struct ReadyToCopy_RTXIO {
      Rc<ManagedTexture>  dstTexture;
      Rc<DxvkImageView>   rtxioDst;
      uint16_t            mip_begin;
      uint16_t            mip_end;
    };


    // Size in bytes required to upload a range of mips for the given asset.
    size_t calcSizeForAsset(
      const AssetData& asset,
      const uint32_t mipLevels_begin,
      const uint32_t mipLevels_end /* non-inclusive */ ) {
      const DxvkFormatInfo* formatInfo = imageFormatInfo(asset.info().format);

      size_t resultSize = 0;
      for (uint32_t level = mipLevels_begin; level < mipLevels_end; ++level) {
        const VkExtent3D levelExtent = util::computeMipLevelExtent(asset.info().extent, level);

        // Align image extent to a full block. This is necessary in
        // case the image size is not a multiple of the block size.
        VkExtent3D elementCount = util::computeBlockCount(levelExtent, formatInfo->blockSize);
        elementCount.depth *= asset.info().numLayers;

        // Allocate staging buffer memory for the image data. The
        // pixels or blocks will be tightly packed within the buffer.
        resultSize += dxvk::align(
          formatInfo->elementSize * util::flattenImageExtent(elementCount),
          CACHE_LINE_SIZE);
      }
      return resultSize;
    }


    // Fill staging buffer with the data from an asset.
    std::vector<ReadyToCopyMip> dmaCopyDataToStaging(
      const DxvkBufferSlice& stagingDst,
      AssetData& asset,
      const uint32_t mipLevels_begin,
      const uint32_t mipLevels_end /* non-inclusive */) {

      assert(mipLevels_begin < mipLevels_end);
      assert(stagingDst.length() >= calcSizeForAsset(asset, mipLevels_begin, mipLevels_end));

      auto readyMips = std::vector<ReadyToCopyMip>{};
      readyMips.reserve(mipLevels_end - mipLevels_begin);

      // Upload data through a staging buffer. Special care needs to
      // be taken when dealing with compressed image formats: Rather
      // than copying pixels, we'll be copying blocks of pixels.
      const DxvkFormatInfo* formatInfo = imageFormatInfo(asset.info().format);

      size_t levelByteOffset = 0;
      for (uint32_t level = mipLevels_begin; level < mipLevels_end; ++level) {
        const void* levelData = asset.data(0, level);
        if (levelData == nullptr) {
          return {};
        }

        const VkExtent3D levelExtent = util::computeMipLevelExtent(asset.info().extent, level);

        // Align image extent to a full block. This is necessary in
        // case the image size is not a multiple of the block size.
        VkExtent3D elementCount = util::computeBlockCount(levelExtent, formatInfo->blockSize);
        elementCount.depth *= asset.info().numLayers;

        const uint32_t pitchPerRow   = elementCount.width * formatInfo->elementSize;
        const uint32_t pitchPerLayer = pitchPerRow * elementCount.height;

        // Allocate staging buffer memory for the image data. The
        // pixels or blocks will be tightly packed within the buffer.
        auto levelByteSize = dxvk::align(
          formatInfo->elementSize * util::flattenImageExtent(elementCount),
          CACHE_LINE_SIZE);

        assert(levelByteOffset % CACHE_LINE_SIZE == 0);
        auto dstSlice = stagingDst.subSlice(levelByteOffset, levelByteSize);


        util::packImageData(
         dstSlice.mapPtr(0),
          levelData,
          elementCount,
          formatInfo->elementSize,
          pitchPerRow,
          pitchPerLayer);
        levelByteOffset += levelByteSize;


        asset.evictCache(0, level);

        readyMips.push_back(ReadyToCopyMip{
          /* .srcBuffer = */ dstSlice,
          /* .mipExtent = */ levelExtent,
          /* .mipLevel  = */ level - mipLevels_begin,
        });
      }

      return readyMips;
    }


    template<
      typename Allocator,
      std::enable_if_t<std::is_same_v<Allocator, RtxStagingRing> || 
                       std::is_same_v<Allocator, DxvkStagingBuffer>, int> = 0
    >
    ReadyToCopy makeStagingForTextureAsset(Allocator& allocator, const Rc<ManagedTexture>& tex) {
      ScopedCpuProfileZone();

      const auto [mip_begin, mip_end] = tex->calcRequiredMips_BeginEnd();

      DxvkBufferSlice stagingDst = allocator.alloc(
        CACHE_LINE_SIZE, 
        calcSizeForAsset(*tex->m_assetData, mip_begin, mip_end)
      );
      if (!stagingDst.defined()) {
        return {};
      }

      auto ready = ReadyToCopy{
        /* .dstTexture = */ tex,
        /* .mips       = */ dmaCopyDataToStaging(stagingDst,
                                                 *tex->m_assetData,
                                                 mip_begin,
                                                 mip_end),
        /* .mip_begin  = */ mip_begin,
        /* .mip_end    = */ mip_end,
        /* .stagingbuf = */ (RtxStagingRing*)(std::is_same_v<Allocator, RtxStagingRing> ? (void*)&allocator : nullptr),
      };

      // Release asset source to keep the number of open file low
      tex->m_assetData->releaseSource();
      return ready;
    }


    const char* makeDebugTextureName(const char* filename) {
      if (filename) {
        const char* lastSlash     = strrchr(filename, '/');
        const char* lastBackslash = strrchr(filename, '\\');
        const char* lastSeparator = (lastSlash > lastBackslash) ? lastSlash : lastBackslash;
        if (lastSeparator) {
          assert(*(lastSeparator + 1) != '\0');
          return lastSeparator + 1;
        }
      }
      return filename;
    }


    Rc<DxvkImageView> allocDeviceImage(
      DxvkDevice* device,
      const Rc<AssetData>& asset,
      DxvkImageCreateInfo desc,
      const uint32_t mipLevels_begin,
      const uint32_t mipLevels_end // non-inclusive
    ) {
      assert(mipLevels_begin < mipLevels_end);
      const uint32_t mipLevels = mipLevels_end - mipLevels_begin;

      desc.extent = util::computeMipLevelExtent(asset->info().extent, mipLevels_begin);
      desc.mipLevels = mipLevels;
      desc.layout = VK_IMAGE_LAYOUT_UNDEFINED;

#ifndef NDEBUG
      const char* debugName = makeDebugTextureName(asset->info().filename);
#else
      const char* debugName = "material texture";
#endif

      Rc<DxvkImage> imgAlloc = device->createImage(
        desc,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        DxvkMemoryStats::Category::RTXMaterialTexture,
        debugName);

      DxvkImageViewCreateInfo viewInfo{};
      {
        viewInfo.type = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
        viewInfo.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.minLevel = 0;
        viewInfo.numLevels = mipLevels;
        viewInfo.minLayer = 0;
        viewInfo.numLayers = desc.numLayers;
        viewInfo.format = desc.format;
      }
      return device->createImageView(imgAlloc, viewInfo);
    }


    void copyStagingToDevice(DxvkContext* ctx,
                             DxvkBarrierSet& execBarriers,
                             DxvkBarrierSet& execAcquires,
                             const ReadyToCopy& ready) {
      Rc<DxvkCommandList> cmd = ctx->getCommandList();

      const Rc<DxvkImage>& image = ready.dstTexture->m_currentMipView->image();

      if (image->info().layout == VK_IMAGE_LAYOUT_UNDEFINED) {
        ctx->changeImageLayout(image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
      }

      for (const ReadyToCopyMip& level : ready.mips) {

        const auto subresources = VkImageSubresourceLayers{
          VK_IMAGE_ASPECT_COLOR_BIT,
          level.mipLevel,
          0,
          image->info().numLayers,
        };
        assert(image->info().numLayers == ready.dstTexture->m_assetData->info().numLayers); // paranoia

        const auto stagingHandle = level.srcBuffer.getSliceHandle();

        // from DxvkContext::updateImage

        // Prepare the image layout. If the given extent covers
        // the entire image, we may discard its previous contents.
        auto subresourceRange = vk::makeSubresourceRange(subresources);
        {
          subresourceRange.aspectMask = image->formatInfo()->aspectMask;
        }

        ctx->prepareImage(execBarriers, image, subresourceRange);

        if (execBarriers.isImageDirty(image, subresourceRange, DxvkAccess::Write)) {
          execBarriers.recordCommands(cmd);
        }

        // Initialize the image if the entire subresource is covered
        VkImageLayout imageLayoutInitial = image->info().layout;
        VkImageLayout imageLayoutTransfer = image->pickLayout(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

        if (image->isFullSubresource(subresources, level.mipExtent)) {
          imageLayoutInitial = VK_IMAGE_LAYOUT_UNDEFINED;
        }

        if (imageLayoutTransfer != imageLayoutInitial) {
          execAcquires.accessImage(
            image,
            subresourceRange,
            imageLayoutInitial,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            0,
            imageLayoutTransfer,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_ACCESS_TRANSFER_WRITE_BIT);
        }

        execAcquires.recordCommands(cmd);

        // Copy contents of the staging buffer into the image.
        // Since our source data is tightly packed, we do not
        // need to specify any strides.
        VkBufferImageCopy region;
        {
          region.bufferOffset = stagingHandle.offset;
          region.bufferRowLength = 0;
          region.bufferImageHeight = 0;
          region.imageSubresource = subresources;
          region.imageOffset = { 0, 0, 0 };
          region.imageExtent = level.mipExtent;
        }

        cmd->cmdCopyBufferToImage(
          DxvkCmdBuffer::ExecBuffer,
          stagingHandle.handle,
          image->handle(),
          imageLayoutTransfer,
          1,
          &region);

        // Transition image back into its optimal layout
        execBarriers.accessImage(
          image,
          subresourceRange,
          imageLayoutTransfer,
          VK_PIPELINE_STAGE_TRANSFER_BIT,
          VK_ACCESS_TRANSFER_WRITE_BIT,
          image->info().layout,
          image->info().stages,
          image->info().access);

        cmd->trackResource<DxvkAccess::Read>(level.srcBuffer.buffer());
      }

      cmd->trackResource<DxvkAccess::Write>(ready.dstTexture->m_currentMipView);
    }


    void flushRtxIo(bool async) {
#ifdef WITH_RTXIO
      if (RtxIo::enabled()) {
        RtxIo::get().flush(async);
      }
#endif
    }


    template<typename T, typename Hasher, typename Predicate>
    void erase_if(std::unordered_set<T, Hasher>& target, Predicate eraseIfTrue) {
      for (auto first = target.begin(), last = target.end(); first != last;) {
        if (eraseIfTrue(*first)) {
          first = target.erase(first);
        } else {
          ++first;
        }
      }
    }


    template<typename T, typename Predicate>
    void erase_ifv(std::vector<T>& target, Predicate eraseIfTrue) {
      auto it = std::remove_if(target.begin(), target.end(), eraseIfTrue);
      auto r = target.end() - it;
      target.erase(it, target.end());
    }

    
    struct RcHasher { size_t operator()(const Rc<ManagedTexture>& s) const { return (size_t)s.ptr(); } };

  } // unnamed namespace


  // AsyncRunner begin


  // Spawns a low-priority thread that loads files, allocates the staging memory for them with a fixed-size allocator,
  // and returns ready-to-copy mip-chains to the Vulkan thread.
  // Enforces strong limits on allocator and amount of textures sent to Vulkan thread, to avoid stutter.
  struct AsyncRunner {

    static constexpr uint32_t MAX_TEXTURE_UPLOADS_PER_FRAME = 32;

    explicit AsyncRunner(const Rc<DxvkDevice>& device)
      : m_ringbuf{ device, stagingBufferSize_Bytes() }
      , m_synchronousAlloc{ device, 4 * Megabytes }
      , m_thread{ dxvk::thread{ [this] { this->asyncLoop(); } } }
    {
      m_thread.set_priority(ThreadPriority::Lowest);
    }

    ~AsyncRunner() {
      if (!m_requiresShutdown.load()) {
        auto l = std::unique_lock{ m_texturesToProcess_mutex };
        m_requiresShutdown.store(true);
        m_texturesToProcess_cond.notify_one();
      }
      if (m_thread.joinable()) {
        m_thread.join();
      }
    }

    AsyncRunner(const AsyncRunner&) = delete;
    AsyncRunner(AsyncRunner&&) noexcept = delete;
    AsyncRunner& operator=(const AsyncRunner&) = delete;
    AsyncRunner& operator=(AsyncRunner&&) noexcept = delete;

    void queueAdd(const Rc<ManagedTexture>& tex, bool allowAsync);
    std::vector<ReadyToCopy> retrieveReadyToUploadTextures();

    dxvk::mutex m_assetInfoMutex{};

  private:
    void asyncLoop(); // boilerplate

  private:
    // has a limited budget, returns nothing if fails
    RtxStagingRing            m_ringbuf;

    // assumed to have an unlimited budget, never fails
    DxvkStagingBuffer         m_synchronousAlloc;

    std::atomic<bool>         m_requiresShutdown;
    dxvk::thread              m_thread;

    dxvk::mutex               m_texturesToProcess_mutex;
    dxvk::condition_variable  m_texturesToProcess_cond;
    std::unordered_set<Rc<ManagedTexture>, RcHasher> m_texturesToProcess; // TODO: remove set?

    dxvk::mutex               m_readyTextures_mutex;
    dxvk::condition_variable  m_readyTextures_cond;
    std::vector<ReadyToCopy>  m_readyTextures;
  };


  void AsyncRunner::queueAdd(const Rc<ManagedTexture>& tex, bool async) {
    assert(tex->m_state == ManagedTexture::State::kQueuedForUpload);
    if (async) {
      assert(!m_requiresShutdown.load());
      auto l = std::unique_lock{ m_texturesToProcess_mutex };
      m_texturesToProcess.emplace(tex);
      m_texturesToProcess_cond.notify_one();
    } else {
      auto l = std::unique_lock{ m_readyTextures_mutex };
      // In CI, we need to overwrite existing ready requests, never wait even a frame for textures that are kQueuedForUpload
      if (!RtxOptions::asyncAssetLoading() && tex->m_state == ManagedTexture::State::kQueuedForUpload) {
        erase_ifv(m_readyTextures, [&tex](const ReadyToCopy& r) { return r.dstTexture == tex; });
        tex->m_state = ManagedTexture::State::kVidMem;
      }
      m_readyTextures.push_back(makeStagingForTextureAsset(m_synchronousAlloc, tex));
      assert(m_readyTextures.back().dstTexture.ptr());
    }
  }


  std::vector<ReadyToCopy> AsyncRunner::retrieveReadyToUploadTextures() {
    auto l = std::unique_lock{ m_readyTextures_mutex };

    std::vector<ReadyToCopy> c = std::move(m_readyTextures);
    m_readyTextures_cond.notify_one();
    return c;
  }


  void AsyncRunner::asyncLoop() {
    env::setThreadName("rtx-texture-async");
    try {
      while (true) {
        if (m_requiresShutdown.load()) {
          break;
        }

        Rc<ManagedTexture> itemToProcess{};
        {
          auto l = std::unique_lock{ m_texturesToProcess_mutex };

          m_texturesToProcess_cond.wait(l, [this]() {
            return !m_texturesToProcess.empty(); // proceed if non-empty
          });

          if (m_requiresShutdown.load()) {
            break;
          }

          if (!m_texturesToProcess.empty()) {
            itemToProcess = std::move(*m_texturesToProcess.begin());
            m_texturesToProcess.erase(m_texturesToProcess.begin());
          }
        }

        if (!itemToProcess.ptr()) {
          continue;
        }

        // wait a bit, to not over-commit texture uploads in a single frame
        {
          auto l = std::unique_lock{ m_readyTextures_mutex };
          m_readyTextures_cond.wait(l, [this]() { return m_readyTextures.size() < MAX_TEXTURE_UPLOADS_PER_FRAME; });
        }

        ReadyToCopy ready;
        {
          {
            // we need to lock, as 'makeStagingForTextureAsset' need to access 'ManagedTexture::m_assetData'
            // which may be modified by other threads (e.g. file hot reload)
            auto lockAssetInfo = std::unique_lock{ m_assetInfoMutex };

            // NOTE: using a ring buffer to alloc staging memory;
            //       it has a high chance of alloc fail -- until other threads return
            //       memory back to the ring buffer (i.e. after finishing staging->vidmem copy)
            ready = makeStagingForTextureAsset(m_ringbuf, itemToProcess);
          }

          while (!ready.dstTexture.ptr()) {
            // alloc failed, retry after wait
            this_thread::yield();

            // repeat
            auto lockAssetInfo = std::unique_lock{ m_assetInfoMutex };
            ready = makeStagingForTextureAsset(m_ringbuf, itemToProcess);
          }
        }

        {
          auto l = std::unique_lock{ m_readyTextures_mutex };
          m_readyTextures.push_back(std::move(ready));
        }
      }
    } catch (const DxvkError& e) {
      Logger::err(str::format("Exception on rtx-texture-async thread!"));
      Logger::err(e.message());
    }
  }


  // AsyncRunner end


  // AsyncRunner_RTXIO begin

  
#ifdef WITH_RTXIO
  // Spawns a separate thread that sends requests to RTXIO library,
  // needed to avoid situations when RTXIO blocks the Vulkan thread (calling thread).
  struct AsyncRunner_RTXIO {
    AsyncRunner_RTXIO(const AsyncRunner_RTXIO&) = delete;
    AsyncRunner_RTXIO(AsyncRunner_RTXIO&&) noexcept = delete;
    AsyncRunner_RTXIO& operator=(const AsyncRunner_RTXIO&) = delete;
    AsyncRunner_RTXIO& operator=(AsyncRunner_RTXIO&&) noexcept = delete;

    
    explicit AsyncRunner_RTXIO(const Rc<DxvkDevice>& device)
      : m_device{ device }
      , m_thread{ dxvk::thread{ [this] { this->asyncLoop(); } } }
      , m_texturesToProcess_count{ 0 }
      , m_requiresSyncFlush{ false }
    {
      m_thread.set_priority(ThreadPriority::Lowest);
    }


    ~AsyncRunner_RTXIO() {
      if (!m_requiresShutdown.load()) {
        auto l = std::unique_lock{ m_texturesToProcess_mutex };
        m_requiresShutdown.store(true);
        m_texturesToProcess_cond.notify_one();
      }
      if (m_thread.joinable()) {
        m_thread.join();
      }
    }


    void syncPoint(bool blockUntilQueueIsEmpty) {
      if (blockUntilQueueIsEmpty) {
        while (m_texturesToProcess_count.load() != 0) {
          _mm_pause();
        }
      }

      if (blockUntilQueueIsEmpty || m_requiresSyncFlush) {
        // If there is any non-async request in flight,
        // dispatch the RTX IO job synchronously
        flushRtxIo(false);
        m_requiresSyncFlush = false;
      }
    }


    void queueAdd(const Rc<ManagedTexture>& tex, bool async) {
      assert(!m_requiresShutdown.load());
      assert(tex->m_state == ManagedTexture::State::kQueuedForUpload);
      if (!async) {
        m_requiresSyncFlush = true;
      }
      auto l = std::unique_lock{ m_texturesToProcess_mutex };
      m_texturesToProcess.emplace(tex);
      m_texturesToProcess_count = uint32_t(m_texturesToProcess.size());
      m_texturesToProcess_cond.notify_one();
    }


    void asyncLoop() {
      env::setThreadName("rtx-texture-async-rtxio");
      try {
        while (true) {
          if (m_requiresShutdown.load()) {
            break;
          }
          if (!RtxIo::enabled()) {
            break;
          }

          Rc<ManagedTexture> itemToProcess{};
          {
            auto l = std::unique_lock{ m_texturesToProcess_mutex };

            while (m_texturesToProcess.empty()) {
              m_texturesToProcess_cond.wait(l);

              l.unlock();
              flushRtxIo(true);
              l.lock();
            }

            if (m_requiresShutdown.load()) {
              break;
            }

            if (!m_texturesToProcess.empty()) {
              itemToProcess = std::move(*m_texturesToProcess.begin());
              m_texturesToProcess.erase(m_texturesToProcess.begin());
              m_texturesToProcess_count = uint32_t(m_texturesToProcess.size());
            }
          }

          const auto [mip_begin, mip_end] = itemToProcess->calcRequiredMips_BeginEnd();
          auto rtxioDst = allocDeviceImage(m_device.ptr(),
                                           itemToProcess->m_assetData,
                                           itemToProcess->imageCreateInfo(),
                                           mip_begin,
                                           mip_end);
          loadTextureRtxIo(itemToProcess, rtxioDst, mip_begin, mip_end);

          {
            auto l = std::unique_lock{ m_waitingList_mutex };
            m_waitingList.push_back(ReadyToCopy_RTXIO{
              /* .dstTexture = */ itemToProcess,
              /* .rtxioDst   = */ rtxioDst,
              /* .mip_begin  = */ mip_begin,
              /* .mip_end    = */ mip_end,
            });
          }
        }
      } catch (const DxvkError& e) {
        Logger::err(str::format("Exception on rtx-texture-async-rtxio thread!"));
        Logger::err(e.message());
      }
    }


    bool finalizeReadyRtxioTextures(DxvkContext* ctx) {
      auto l_canBeRemovedFromWaiting = [ctx](ReadyToCopy_RTXIO& ready) -> bool {
        Rc<ManagedTexture>& tex = ready.dstTexture;
        if (tex->m_state != ManagedTexture::State::kQueuedForUpload) {
          return true;
        }
        if (!RtxIo::get().isComplete(tex->m_completionSyncpt)) {
          return false;
        }
        if (ctx) {
          if (ready.rtxioDst->image()->info().layout == VK_IMAGE_LAYOUT_UNDEFINED) {
            ctx->changeImageLayout(ready.rtxioDst->image(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
          }
        }
        tex->m_currentMipView   = ready.rtxioDst;
        tex->m_currentMip_begin = ready.mip_begin;
        tex->m_currentMip_end   = ready.mip_end;
        tex->m_state = ManagedTexture::State::kVidMem;
        return true;
      };

      assert(dxvk::this_thread::get_id() != m_thread.get_id());
      if (!RtxIo::enabled()) {
        return false;
      }

      {
        auto l = std::unique_lock{ m_waitingList_mutex };

        auto newend = std::remove_if(m_waitingList.begin(),
                                     m_waitingList.end(),
                                     l_canBeRemovedFromWaiting);
        const bool anyPromoted = (newend != m_waitingList.end());
        m_waitingList.erase(newend, m_waitingList.end());

        if (!m_waitingList.empty()) {
          // if anything is being waited, flushRtxIo on m_thread
          m_texturesToProcess_cond.notify_one();
        }
        return anyPromoted;
      }
    }

  private:
    Rc<DxvkDevice>                  m_device;
    std::atomic<bool>               m_requiresShutdown;
    dxvk::thread                    m_thread;

    dxvk::mutex                     m_texturesToProcess_mutex;
    dxvk::condition_variable        m_texturesToProcess_cond;
    std::unordered_set<Rc<ManagedTexture>, RcHasher> m_texturesToProcess; // TODO: remove set?
    std::atomic<uint32_t>           m_texturesToProcess_count;

    dxvk::mutex                     m_waitingList_mutex;
    std::vector<ReadyToCopy_RTXIO>  m_waitingList;

    bool m_requiresSyncFlush; // read-write only from a client thread
  };
#endif


  // AsyncRunner_RTXIO end


  // For each ManagedTexture, keep the data returned by the sampler feedback.
  // It smoothly accumulates the mipcount, trying to avoid spikes,
  // so that it can be further used to request a texture load from a disk.
  struct FeedbackAccum {
    uint32_t  frame;          // Frame when 'mipcount' member was changed.
    float     avgMipcount;    // A running average of all seen mipcount-s.
                              // Used as a smooth fallback, when there's VRAM budget pressure.
    uint8_t   mipcount;       // The max mipcount seen across frames.
  };

  namespace {
    float calcResolutionAndHistoryWeightForTexture(const FeedbackAccum& x, const uint32_t& curframe) {
      uint32_t framediff = (curframe >= x.frame) ? curframe - x.frame : 0;
      float fr_weight = 1.F / (1.F + framediff);

      // need relative mip count? divide by asset_mipcount?
      float mip_weight = float(std::min(x.mipcount, MAX_MIPS)) / float(MAX_MIPS);

      assert(fr_weight >= 0 && fr_weight <= 1);
      assert(mip_weight >= 0 && mip_weight <= 1);

      return (2.0f * mip_weight) + (1.0f * fr_weight);
    }
  } // unnamed namespace


  constexpr VkDeviceSize MiBPerGiB = 1024;
  // Remix needs at least two frames to completely evict the demoted textures.
  // After a global texture demotion event, texture manager will delay future texture promotions
  // by this number of frames to make sure the previously used memory is released and there
  // will be no overcommit.
  constexpr uint32_t kPromotionDelayFrames = 2;

  RtxTextureManager::RtxTextureManager(DxvkDevice* pDevice)
    : CommonDeviceObject(pDevice)
    , m_asyncThread{}
    , m_asyncThread_rtxio{}
  {
    m_sf.m_related = new uint16_t[SAMPLER_FEEDBACK_MAX_TEXTURE_COUNT * SAMPLER_FEEDBACK_RELATED_PER_TEX];
    m_sf.m_noisyMipcount = new uint8_t[SAMPLER_FEEDBACK_MAX_TEXTURE_COUNT]{ /* zero-init */ };
    m_sf.m_accumulatedMipcount = new FeedbackAccum[SAMPLER_FEEDBACK_MAX_TEXTURE_COUNT]{ /* zero-init */ };
    m_sf.m_cachedAssetMipcount = new uint8_t[SAMPLER_FEEDBACK_MAX_TEXTURE_COUNT]{ /* zero-init */ };
    m_sf.m_cachedGpubuf = new uint32_t[SAMPLER_FEEDBACK_MAX_TEXTURE_COUNT]; // NO zero-init

    static_assert(SAMPLER_FEEDBACK_INVALID == UINT16_MAX, "must be 0xFF for memset");
    memset(m_sf.m_related, 0xFF, SAMPLER_FEEDBACK_MAX_TEXTURE_COUNT * SAMPLER_FEEDBACK_RELATED_PER_TEX * sizeof(m_sf.m_related[0]));

    FileWatch::get().beginThread(this);
  }

  void RtxTextureManager::startAsync() {
    if (RtxIo::enabled()) {
      m_asyncThread_rtxio = new AsyncRunner_RTXIO{ m_device };
    } else {
      m_asyncThread = new AsyncRunner{ m_device };
    }
  }

  RtxTextureManager::~RtxTextureManager() {
    FileWatch::get().endThread();

    delete m_sf.m_cachedGpubuf;
    delete m_sf.m_cachedAssetMipcount;
    delete m_sf.m_accumulatedMipcount;
    delete m_sf.m_noisyMipcount;
    delete m_sf.m_related;
    delete m_asyncThread;
    delete m_asyncThread_rtxio;
  }

  static ManagedTexture::State processManagedTextureState(ManagedTexture* tex) {
    assert(tex != nullptr);
    if (tex->m_state == ManagedTexture::State::kFailed) {
      tex->requestMips(0);
      return ManagedTexture::State::kFailed;
    }
    return tex->m_state;
  }

  void RtxTextureManager::scheduleTextureLoad(const Rc<ManagedTexture>& texture, bool async, bool forceUnload) {
    if (!texture.ptr()) {
      return;
    }

    const auto managedState = processManagedTextureState(texture.ptr());
    if (managedState == ManagedTexture::State::kQueuedForUpload) {
      // Texture is in the async thread processing queue, leave it
      return;
    }
    // If not forced, we can early-out if mip count is already satisfied
    if (!forceUnload) {
      if (managedState == ManagedTexture::State::kVidMem) {
        // If uploaded to GPU, check if requested amount of mips is the same
        if (texture->hasUploadedMips(texture->m_requestedMips, true)) {
          return;
        }
      }
    }

    if (RtxOptions::alwaysWaitForAsyncTextures()) {
      async = false;
    }

    texture->m_state = ManagedTexture::State::kQueuedForUpload;
    if (m_asyncThread) {
      m_asyncThread->queueAdd(texture, async);
    } else if (m_asyncThread_rtxio) {
      m_asyncThread_rtxio->queueAdd(texture, async);
    } else {
      assert(0);
    }
  }

  void RtxTextureManager::submitTexturesToDeviceLocal(DxvkContext* ctx,
                                                      DxvkBarrierSet& execBarriers,
                                                      DxvkBarrierSet& execAcquires) {
    ScopedCpuProfileZoneN("Textures: upload to device");

    if (m_asyncThread) {
      for (const ReadyToCopy& ready : m_asyncThread->retrieveReadyToUploadTextures()) {
        const Rc<ManagedTexture>& tex = ready.dstTexture;

        tex->m_currentMipView = allocDeviceImage(m_device,
                                                 tex->m_assetData,
                                                 tex->imageCreateInfo(),
                                                 ready.mip_begin,
                                                 ready.mip_end);
        tex->m_currentMip_begin = ready.mip_begin;
        tex->m_currentMip_end   = ready.mip_end;

        copyStagingToDevice(ctx, execBarriers, execAcquires, ready);
        if (ready.stagingbuf) {
          ready.stagingbuf->onSliceSubmitToCmd();
        }

        tex->m_state = ManagedTexture::State::kVidMem;
      }
    } else if (m_asyncThread_rtxio) {
      m_asyncThread_rtxio->syncPoint(RtxOptions::alwaysWaitForAsyncTextures());
      m_asyncThread_rtxio->finalizeReadyRtxioTextures(ctx);
    }
  }

  void RtxTextureManager::addTexture(const TextureRef& inputTexture, uint16_t associatedFeedbackStamp, bool async, uint32_t& textureIndexOut) {
    // If theres valid texture backing this ref, then skip
    if (!inputTexture.isValid()) {
      return;
    }

    // Track this texture to make a linear table for this frame
    textureIndexOut = m_textureCache.track(inputTexture);

    const Rc<ManagedTexture>& tex = m_textureCache.at(textureIndexOut).getManagedTexture();
    if (tex == nullptr) {
      return;
    }

    const auto curframe = m_device->getCurrentFrameId();
    tex->m_frameLastUsed = curframe;

    // If async is not allowed, schedule immediately on this thread, and never demote
    if (!async || RtxOptions::TextureManager::neverDowngradeTextures()) {
      tex->m_canDemote = false;
      tex->requestMips(MAX_MIPS);
      scheduleTextureLoad(tex, false);
      return;
    }

    bool streamableWithVariableMips = m_sf.associate(
      RtxOptions::TextureManager::samplerFeedbackEnable() ? associatedFeedbackStamp : SAMPLER_FEEDBACK_INVALID,
      tex->m_samplerFeedbackStamp
    );
    if (!streamableWithVariableMips) {
      // If mip-specific streaming is NOT possible, then the 'm_frameLastUsed' heuristic is used,
      // i.e. if N frames has passed for a texture that was not used in a scene, then remove it from VRAM.
      return;
    }
    tex->m_frameLastUsedForSamplerFeedback = curframe;
  }

  void RtxTextureManager::clear() {
    ScopedCpuProfileZone();

    // Only demote textures when we're actually over budget.
    // If we're under budget, there's no reason to demote - keep textures in VRAM
    // so they're ready when the scene returns (e.g., after closing a menu).
    if (isOverTextureBudget(m_device)) {
      manageBudgetWithPriority();
    }

    m_textureCache.clear();
  }

  void RtxTextureManager::requestHotReload(const Rc<ManagedTexture>& tex) {
    if (!RtxOptions::TextureManager::hotReload()) {
      return;
    }
    if (!m_asyncThread) {
      ONCE(Logger::err("filewatch: hot reload is not available with RTX IO. Only raw native filesystem is supported."));
      return;
    }
    if (!tex.ptr()) {
      return;
    }
    auto lockRequestsList = std::unique_lock{ m_hotreloadMutex };
    m_hotreloadRequests.insert(tex);
  }

  namespace {
    struct FileReadLock {
      explicit FileReadLock(const char* filepath) {
        m_handle = CreateFileA(
          filepath, //
          GENERIC_READ,
          FILE_SHARE_READ, // others can read
          nullptr,
          OPEN_EXISTING,
          FILE_ATTRIBUTE_NORMAL,
          nullptr
        );
      }
      ~FileReadLock() {
        if (m_handle && m_handle != INVALID_HANDLE_VALUE) {
          CloseHandle(m_handle);
        }
      }
      bool otherProcessIsWriting() const {
        return !m_handle || m_handle == INVALID_HANDLE_VALUE;
      }
    private:
      HANDLE m_handle{};
    };
  }

  void RtxTextureManager::processAllHotReloadRequests() {
    if (!RtxOptions::TextureManager::hotReload()) {
      return;
    }
    if (!m_asyncThread) {
      return;
    }
    auto lockRequestsList = std::unique_lock{ m_hotreloadMutex };
    if (m_hotreloadRequests.empty()) {
      return;
    }

    // stall async texture streaming thread while this thread
    // is patching up the file header information ('ManagedTexture::m_assetData')
    auto lockAssetInfo = std::unique_lock{ m_asyncThread->m_assetInfoMutex };

    // iterate destructively
    for (auto it = m_hotreloadRequests.begin(); it != m_hotreloadRequests.end();) {
      const Rc<ManagedTexture>& tex = *it;

      const char* filepath = tex->m_assetData->info().filename;
      if (!filepath || filepath[0] == '\0') {
        it = m_hotreloadRequests.erase(it); // pop request
        continue;
      }

      // try lock the file for reading, to ensure that other processes are not writing into it
      FileReadLock fileReadLock = FileReadLock{ filepath };
      if (fileReadLock.otherProcessIsWriting()) {
        // skip and try to check request in the next frame,
        // i.e. wait for full file write from other process
        // NOTE: this keeps the request in 'm_hotreloadRequests'
        ++it;
        continue;
      }

      // NOTE: FileReadLock opened a file FILE_SHARE_READ, so asset manager's 'fopen' call should succeed
      auto newAssetData = AssetDataManager::get().findAsset(filepath, true);
      if (!newAssetData.ptr()) {
        // file doesn't exist
        it = m_hotreloadRequests.erase(it); // pop request
        continue;
      }

      // replace information about the file
      tex->m_assetData = newAssetData;
      tex->requestMips(0);
      constexpr bool forceUnload = true; // do not check current mip count
      scheduleTextureLoad(tex, false, forceUnload );

      it = m_hotreloadRequests.erase(it); // pop request
    }
  }

  void RtxTextureManager::prepareSamplerFeedback(DxvkContext* ctx) {
    auto& res = ctx->getCommonObjects()->getResources().getRaytracingOutput();

    // reset device-local sampler feedback buffer
    const auto bytesToClear = m_sf.m_idToTexture_count.load() * sizeof(uint32_t);

    // Note: Only clear the buffer when a non-zero clear size is requested (as Vulkan
    // does not allow for zero-sized clears).
    if (bytesToClear != 0) {
      ctx->clearBuffer(
        res.m_samplerFeedbackDevice,
        0, 
        bytesToClear,
        UINT32_MAX  // to find min during the rendering
      );
    }
  }

  void RtxTextureManager::copySamplerFeedbackToHost(DxvkContext* ctx) {
    auto& res = ctx->getCommonObjects()->getResources().getRaytracingOutput();

    const auto frameid = m_device->getCurrentFrameId();

    const auto curframe = frameid % std::size(res.m_samplerFeedbackReadback);
    const auto prevframe = (frameid + (std::size(res.m_samplerFeedbackReadback) - 1)) % std::size(res.m_samplerFeedbackReadback);
    const auto bytesToCopy = m_sf.m_idToTexture_count.load() * sizeof(uint32_t);

    // Note: Only copy the buffer when a non-zero copy size is requested (as Vulkan
    // does not allow for zero-sized copies).
    if (bytesToCopy != 0) {
      ctx->copyBuffer(
        res.m_samplerFeedbackReadback[curframe],
        0,
        res.m_samplerFeedbackDevice,
        0,
        bytesToCopy);
    }

    {
      const uint32_t* gpuAccessedMips_safeToReadOnHost =
        (const uint32_t*)res.m_samplerFeedbackReadback[prevframe]->mapPtr(0); // approximately...

      garbageCollection(gpuAccessedMips_safeToReadOnHost);
    }
  }

  bool SamplerFeedback::associate(uint16_t stampWithList, uint16_t stampToAdd) {
    assert(stampToAdd != SAMPLER_FEEDBACK_INVALID);
    if (stampWithList == SAMPLER_FEEDBACK_INVALID) {
      return false;
    }
    // no need to add to itself
    if (stampWithList == stampToAdd) {
      return true;
    }
    uint16_t* listOfRelatedStamps = &m_related[ptrdiff_t{ stampWithList } * SAMPLER_FEEDBACK_RELATED_PER_TEX];
    for (uint8_t i = 0; i < SAMPLER_FEEDBACK_RELATED_PER_TEX; i++) {
      // if entry exists
      if (listOfRelatedStamps[i] == stampToAdd) {
        return true;
      }
      // add, if not
      if (listOfRelatedStamps[i] == SAMPLER_FEEDBACK_INVALID) {
        listOfRelatedStamps[i] = stampToAdd;
        return true;
      }
    }
    return false;
  }

  uint32_t SamplerFeedback::fetchNoisyMipCounts(const uint32_t* src_gpubuf) {
    uint32_t textureCount;
    {
      auto ls = std::unique_lock{ m_idToTexture_mutex };

      textureCount = uint32_t(m_idToTexture.size());
      if (textureCount == 0) {
        return 0;
      }

      // optimize access to asset's mipcount, to avoid pointer chase via ManagedTexture,
      // improves cache efficiency
      if (m_cachedAssetMipcount_length != textureCount) {
        memset(m_cachedAssetMipcount, 0, textureCount * sizeof(m_cachedAssetMipcount[0]));
        m_cachedAssetMipcount_length = uint32_t(textureCount);
        for (uint32_t stamp = 0; stamp < textureCount; stamp++) {
          const Rc<ManagedTexture>& tex = m_idToTexture[stamp];
          assert(stamp == tex->m_samplerFeedbackStamp);
          if (tex.ptr()) {
            m_cachedAssetMipcount[stamp] = uint8_t(std::min(tex->m_assetData->info().mipLevels, uint32_t(MAX_MIPS)));
          }
        }
      }
    }

    // Shadow memory to reduce random access via DMA
    memcpy(m_cachedGpubuf, src_gpubuf, textureCount * sizeof(uint32_t));

    // Reset to zero to find a max value for each texture in 'src_gpubuf'
    memset(m_noisyMipcount, 0, textureCount * sizeof(m_noisyMipcount[0]));
    for (uint32_t stamp = 0; stamp < textureCount; stamp++) {
      uint8_t newMipCount;
      {
        const uint32_t assetMipCount = m_cachedAssetMipcount[stamp];

        const uint32_t mipAccessed = std::min(m_cachedGpubuf[stamp], assetMipCount);
        newMipCount = uint8_t(assetMipCount - mipAccessed);
      }
      m_noisyMipcount[stamp] = std::max(m_noisyMipcount[stamp], newMipCount);
    }

    // A single stamp can be associated with many other stamps (SAMPLER_FEEDBACK_RELATED_PER_TEX).
    // (Because sampler feedback is decided on an albedo texture,
    // so expand the mip count of albedo onto roughness, emissive and other textures)
    for (uint32_t stamp = 0; stamp < textureCount; stamp++) {
      uint16_t* listOfRelatedStamps = &m_related[stamp * SAMPLER_FEEDBACK_RELATED_PER_TEX];
      if (listOfRelatedStamps[0] == SAMPLER_FEEDBACK_INVALID) {
        continue;
      }
      const auto newMipCount = m_noisyMipcount[stamp];

      for (uint8_t i = 0; i < SAMPLER_FEEDBACK_RELATED_PER_TEX; i++) {
        uint16_t stampOfRelated = listOfRelatedStamps[i];
        if (stampOfRelated == SAMPLER_FEEDBACK_INVALID) {
          break; // end of list
        }
        m_noisyMipcount[stampOfRelated] = std::max(m_noisyMipcount[stampOfRelated], newMipCount);
      }
    }

    static_assert(SAMPLER_FEEDBACK_INVALID == UINT16_MAX, "must be 0xFF for memset");
    memset(m_related, 0xFF, textureCount * SAMPLER_FEEDBACK_RELATED_PER_TEX * sizeof(m_related[0]));

    return textureCount;
  }

#define FRAMES_TO_DETAIN 60

  void SamplerFeedback::accumulateMipCounts(uint32_t len, uint32_t curframe, bool canReset) {
    for (uint32_t stamp = 0; stamp < len; stamp++) {
      FeedbackAccum& accum = m_accumulatedMipcount[stamp];

      const auto new_mipcount = m_noisyMipcount[stamp];

      if (new_mipcount != 0) {
        if (new_mipcount >= accum.mipcount) {
          accum.frame = curframe;
          accum.mipcount = new_mipcount;
        }
      }

      accum.avgMipcount = (float(new_mipcount) + accum.avgMipcount) * 0.5f;

      if (canReset) {
        assert(curframe >= accum.frame);
        if (curframe - accum.frame > FRAMES_TO_DETAIN) {
          accum.frame = curframe;
          accum.mipcount = (uint8_t)std::clamp(accum.avgMipcount, 0.f, float(MAX_MIPS));
        }
      }
    }
  }

  void RtxTextureManager::garbageCollection(const uint32_t* gpuAccessedMips) {
    ScopedCpuProfileZone();

    const auto curframe = m_device->getCurrentFrameId();
    const auto numFramesToKeepMaterialTextures = RtxOptions::numFramesToKeepMaterialTextures();

    uint32_t sfTextureCount = m_sf.fetchNoisyMipCounts(gpuAccessedMips);
    m_sf.accumulateMipCounts(sfTextureCount, curframe, m_wasTextureBudgetPressure);
    m_wasTextureBudgetPressure = false;

    
    static auto prioritylist = std::vector<ManagedTexture*>{};
    static auto checkonlyframes = std::vector<ManagedTexture*>{};
    {
      prioritylist.clear();
      checkonlyframes.clear();
      auto ls = std::unique_lock{ m_sf.m_idToTexture_mutex };
      for (const auto& tex : m_sf.m_idToTexture) {
        assert(tex != nullptr);
        if (tex != nullptr && tex->m_canDemote) {
          if ((tex->m_frameLastUsedForSamplerFeedback != UINT32_MAX) && (curframe - tex->m_frameLastUsedForSamplerFeedback < 2)) {
            assert(tex->m_samplerFeedbackStamp != SAMPLER_FEEDBACK_INVALID);
            prioritylist.push_back(tex.ptr());
          } else {
            checkonlyframes.push_back(tex.ptr());
          }
        }
      }
    }


    // For no-sampler-feedback textures, don't use the prioritization and budgeting (for now),
    // as we can't predict how draw call textures (sky, terrain, etc) are used
    for (ManagedTexture* tex : checkonlyframes) {
      assert(tex && tex->m_canDemote);
      tex->requestMips(
        (tex->m_frameLastUsed != UINT32_MAX) && (curframe - tex->m_frameLastUsed <= numFramesToKeepMaterialTextures)
        ? MAX_MIPS
        : 0);
      scheduleTextureLoad(tex, true);
    }

    
    // For sampler-feedback textures, make a list, so that the low priority textures are at the end.
    // If full list doesn't fit into the budget, demote the low priority ones.
    {
      auto l_sort = [curframe, this](const ManagedTexture* a, const ManagedTexture* b) {
        assert(a && b);
        float weightA = calcResolutionAndHistoryWeightForTexture(m_sf.m_accumulatedMipcount[a->m_samplerFeedbackStamp], curframe);
        float weightB = calcResolutionAndHistoryWeightForTexture(m_sf.m_accumulatedMipcount[b->m_samplerFeedbackStamp], curframe);

        if (std::abs(weightA - weightB) < 0.00001f) {
          return (a->m_samplerFeedbackStamp < b->m_samplerFeedbackStamp); // stable fallback, if too similar
        }
        return weightA > weightB;
      };
      std::sort(prioritylist.begin(), prioritylist.end(), l_sort);
    }
    {
      const size_t budgetBytes = calcTextureMemoryBudgetBytes(m_device);
      size_t       usedBytes   = 0;
      for (ManagedTexture* tex : prioritylist) {
        assert(tex && tex->m_canDemote && tex->m_samplerFeedbackStamp != SAMPLER_FEEDBACK_INVALID);
        // for low memory GPUs we should do our best to not blow through all memory, lower the highest quality mip level
        // need to account for textures that dont have more than 1 mip level here too.
        const uint32_t allmipcount = tex->m_assetData->info().mipLevels - ((RtxOptions::lowMemoryGpu() && tex->m_assetData->info().mipLevels > 0) ? 1u : 0u);

        uint32_t mipc = m_sf.m_accumulatedMipcount[tex->m_samplerFeedbackStamp].mipcount;
        mipc = std::min(mipc, allmipcount);

        // TODO: potential bottleneck
        size_t byteSize = calcSizeForAsset(*tex->m_assetData, allmipcount - mipc, allmipcount);

        if (usedBytes + byteSize <= budgetBytes) {
          usedBytes += byteSize;
          tex->requestMips(mipc);
        } else {
          // doesn't fit => demote
          tex->requestMips(0);
          m_wasTextureBudgetPressure = true;
        }

        scheduleTextureLoad(tex, true);
      }
      assert(usedBytes <= budgetBytes);

      // for debug report
      g_streamedTextures_budgetBytes = budgetBytes;
      g_streamedTextures_usedBytes   = usedBytes;
    }
  }

  void RtxTextureManager::manageBudgetWithPriority() {
    ScopedCpuProfileZone();
    
    const size_t budgetBytes = calcTextureMemoryBudgetBytes(m_device);
    size_t currentUsage = calcCurrentTextureUsageBytes(m_device);
    
    if (currentUsage <= budgetBytes) {
      return;  // Already under budget
    }
    
    auto demoteTexture = [&](ManagedTexture* tex) {
      const uint32_t allmipcount = tex->m_assetData->info().mipLevels;
      const uint32_t currentMips = tex->m_requestedMips.load();
      
      if (currentMips > 0) {
        const size_t oldSize = calcSizeForAsset(*tex->m_assetData, allmipcount - currentMips, allmipcount);
        tex->requestMips(0);
        scheduleTextureLoad(tex, false);
        currentUsage -= oldSize;
        m_wasTextureBudgetPressure = true;
      }
    };
    
    // Demote textures that were previously rendered (old scene textures).
    // Textures with m_frameLastUsed == UINT32_MAX are newly loaded and haven't been
    // rendered yet - these are needed for the incoming scene and should be preserved.
    {
      auto ls = std::unique_lock{ m_sf.m_idToTexture_mutex };
      for (const auto& tex : m_sf.m_idToTexture) {
        if (currentUsage <= budgetBytes) {
          break;
        }
        if (tex != nullptr && tex->m_canDemote && tex->m_frameLastUsed != UINT32_MAX) {
          demoteTexture(tex.ptr());
        }
      }
    }
    
    // Update debug stats
    g_streamedTextures_budgetBytes = budgetBytes;
    g_streamedTextures_usedBytes = currentUsage;
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

  bool warnIfTextureSuboptimal(const AssetData& assetData) {
    const auto& extent = assetData.info().extent;

    // Large textures that have only a single mip level are considered suboptimal since they
    // may cause high pressure on memory and/or cause hitches when loaded at runtime.
    const bool result = (assetData.info().mipLevels == 1) && (extent.width * extent.height >= 512 * 512);
    if (result) {
      Logger::warn(str::format("A suboptimal replacement texture detected: ",
                                    assetData.info().filename,
                                    "! Please make sure all replacement textures have mip-maps."));
    }
    return result;
  }

  // TODO: handle large textures that exceed STAGING_BUDGET (e.g. 8k textures),
  //       at the moment, if this function returns true, the texture will not be considered for texture streaming
  bool WAR_doesAssetFitIntoFixedAllocator(const AssetData& assetData) {
    size_t byteSize = calcSizeForAsset(assetData, 0, assetData.info().mipLevels);
    if (byteSize <= stagingBufferSize_Bytes()) {
      return true;
    }
    Logger::err(str::format(
      "Texture (",
      assetData.info().extent.width,
      "x",
      assetData.info().extent.height,
      ") doesn't fit into STAGING memory for streaming (TEXTURE=",
      byteSize / Megabytes,
      "MB, but STAGING=",
      stagingBufferSize_Bytes() / Megabytes,
      "MB)"
      ". Forcing synchronous upload, disabling texture streaming on: ",
      assetData.info().filename));
    return false;
  }

  Rc<ManagedTexture> RtxTextureManager::preloadTextureAsset(const Rc<AssetData>& assetData,
                                                            ColorSpace colorSpace,
                                                            bool forceLoad) {

    const XXH64_hash_t hash = assetData->hash();

    {
      auto l = std::unique_lock{ m_assetHashToTextures_mutex };

      auto it = m_assetHashToTextures.find(hash);
      if (it != m_assetHashToTextures.end()) {
        // Is this truly the same asset?
        if (it->second->m_assetData->info().matches(assetData->info())) {
          return it->second;
        }

        // Else, clear out the old
        m_assetHashToTextures.erase(it);
      }
    }

    {
      VkFormatProperties properties = m_device->adapter()->formatProperties(assetData->info().format);

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

    warnIfTextureSuboptimal(*assetData);
    if (m_asyncThread && !WAR_doesAssetFitIntoFixedAllocator(*assetData)) {
      forceLoad = true;
    }

    Rc<ManagedTexture> texture = TextureUtils::createTexture(assetData, colorSpace);
    if (forceLoad) {
      texture->m_canDemote = false;
      texture->requestMips(MAX_MIPS);
      scheduleTextureLoad(texture, false);
    } else {
      texture->m_canDemote = true;
      texture->requestMips(1);
      scheduleTextureLoad(texture, false);
    }

    {
      auto ls = std::unique_lock{ m_sf.m_idToTexture_mutex };

      if (m_sf.m_idToTexture.size() < SAMPLER_FEEDBACK_MAX_TEXTURE_COUNT) {
        texture->m_samplerFeedbackStamp = m_sf.m_idToTexture_count.fetch_add(1);
        m_sf.m_idToTexture.push_back(texture);
      } else {
        assert(0);
        Logger::err("Sampler feedback stamp overflow!");
        texture->m_samplerFeedbackStamp = SAMPLER_FEEDBACK_INVALID;
      }
    }

    {
      FileWatch::get().watchTexture(texture);

      auto l = std::unique_lock{ m_assetHashToTextures_mutex };
      return m_assetHashToTextures.emplace(hash, texture).first->second;
    }
  }

  static size_t calcTextureMemoryBudget_Megabytes(DxvkDevice* device) {
    constexpr int MinBudgetMib = 32;

    if (RtxOptions::TextureManager::fixedBudgetEnable()) {
      return std::max(RtxOptions::TextureManager::fixedBudgetMiB(), MinBudgetMib);
    }


    // How much VRAM is free to use
    VkDeviceSize availableMemorySizeMib = 0;
    const VkPhysicalDeviceMemoryProperties memory = device->adapter()->memoryProperties();
    const DxvkAdapterMemoryInfo memHeapInfo = device->adapter()->getMemoryHeapInfo();
    for (uint32_t i = 0; i < memory.memoryHeapCount; i++) {
      bool isDeviceLocal = memory.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT;
      if (!isDeviceLocal) {
        continue;
      }

      VkDeviceSize remixFreeMemMib =
        (device->getMemoryStats(i).totalAllocated() >> 20) -
        (device->getMemoryStats(i).totalUsed() >> 20);

      VkDeviceSize memBudgetMib = memHeapInfo.heaps[i].memoryBudget >> 20;
      VkDeviceSize memUsedMib = (memHeapInfo.heaps[i].memoryAllocated >> 20) - remixFreeMemMib;

      if (memBudgetMib > memUsedMib) {
        availableMemorySizeMib = std::max(memBudgetMib - memUsedMib, availableMemorySizeMib);
      }
    }
    if (!device->getCommon()->getResources().isResourceReady()) {
      // Reserve space for various non-texture GPU resources (buffers, etc)

      const auto adaptiveResolutionReservedGPUMemoryMiB =
        static_cast<std::int32_t>(RtxOptions::adaptiveResolutionReservedGPUMemoryGiB() * MiBPerGiB);

      // Note: int32_t used for clamping behavior on underflow.
      availableMemorySizeMib = std::max(static_cast<std::int32_t>(availableMemorySizeMib) - adaptiveResolutionReservedGPUMemoryMiB, 0);
    }


    // How much VRAM is already allocated for existing textures
    VkDeviceSize currentUsageMib = 0;
    for (uint32_t i = 0; i < device->adapter()->memoryProperties().memoryHeapCount; i++) {
      bool isDeviceLocal = device->adapter()->memoryProperties().memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT;
      if (isDeviceLocal) {
        currentUsageMib += device->getMemoryStats(i).usedByCategory(DxvkMemoryStats::Category::RTXMaterialTexture) >> 20;
      }
    }


    // NOTE: the percentage needs to be a portion of the WHOLE range, not only the available mem
    VkDeviceSize wholeTextureBudgetMib = availableMemorySizeMib + currentUsageMib;


    float percentage = std::clamp(RtxOptions::TextureManager::budgetPercentageOfAvailableVram(), 0, 100) / 100.f;
    return std::max(size_t(wholeTextureBudgetMib * percentage), size_t(MinBudgetMib));
  }
}
