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
#include "rtx_utils.h"
#include "dxvk_context_state.h"
#include "dxvk_scoped_annotation.h"
#include "rtx_texture.h"
#include "rtx_context.h"
#include "rtx_options.h"
#include "dxvk_device.h"
#include "rtx_io.h"

namespace dxvk {

#ifdef WITH_RTXIO
  // Helper to schedule image layer update with RTXIO.
  // The entire layer mip-chain will be updated with the data starting from
  // the asset assetBaseMip.
  static uint64_t scheduleImageLayerUpdateRtxIo(
    const Rc<DxvkImage>& image,
    int                  layer,
    const Rc<AssetData>& assetData,
    RtxIo::Handle        assetFile,
    int                  assetBaseMip) {
    auto& rtxio = RtxIo::get();
    const auto& desc = image->info();
    const auto& assetInfo = assetData->info();

    // The number of mip levels in the tail data blob.
    // For loose dds files this will be always 0.
    const int tailMipLevels = assetInfo.mipLevels - assetInfo.looseLevels;

    uint64_t completionSyncpt = 0;

    if (assetInfo.compression != AssetCompression::None) {
      RtxIo::FileSource src { assetFile, 0, 0, true };
      RtxIo::ImageDest dst { image, static_cast<uint16_t>(layer), 0, 1 };

      // For compressed images we need to load loose mip levels
      // one-by-one except the mip levels in the tail blob.
      for (uint32_t n = 0; n < desc.mipLevels; n++) {
        const bool isTail = desc.mipLevels - n <= tailMipLevels;

        assetData->placement(layer, 0, n + assetBaseMip, src.offset, src.size);

        dst.startMip = n;
        dst.count = isTail ? tailMipLevels : 1;

        completionSyncpt = rtxio.enqueueRead(dst, src);

        if (isTail) {
          break;
        }
      }
    } else {
      uint64_t offset;
      size_t size;
      assetData->placement(layer, 0, assetBaseMip, offset, size);
      // TODO: optimize
      for (uint32_t n = 1; n < desc.mipLevels; n++) {
        uint64_t levelOffset;
        size_t levelSize;
        assetData->placement(layer, 0, n + assetBaseMip, levelOffset, levelSize);
        size += levelSize;
      }

      RtxIo::FileSource src { assetFile, offset, size, false };
      RtxIo::ImageDest dst {
        image,
        static_cast<uint16_t>(layer),
        0, static_cast<uint16_t>(desc.mipLevels)
      };

      completionSyncpt = rtxio.enqueueRead(dst, src);
    }

    return completionSyncpt;
  }
#endif

  static void loadTextureRtxIo(
    const Rc<ManagedTexture>& texture,
    const Rc<DxvkDevice>&     device,
    TextureUtils::MipsToLoad  mipsToLoad) {
#ifdef WITH_RTXIO
    const bool isPreloading = mipsToLoad == TextureUtils::MipsToLoad::HighMips;

    auto& rtxio = RtxIo::get();
    auto desc = texture->futureImageDesc;

    const int numMipsToLoad = isPreloading
      ? RtxTextureManager::calcPreloadMips(desc.mipLevels) : desc.mipLevels;
    assert(numMipsToLoad > 0);

    const int firstMip = desc.mipLevels - numMipsToLoad;
    const int lastMip = desc.mipLevels - 1;

    desc.mipLevels = numMipsToLoad;
    desc.extent.width >>= firstMip;
    desc.extent.height >>= firstMip;
    Rc<DxvkImage> image = device->createImage(desc, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
      DxvkMemoryStats::Category::RTXMaterialTexture, "material texture");

    // Make DxvkImageView
    DxvkImageViewCreateInfo viewInfo;
    viewInfo.type = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
    viewInfo.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.minLevel = 0;
    viewInfo.numLevels = desc.mipLevels;
    viewInfo.minLayer = 0;
    viewInfo.numLayers = desc.numLayers;
    viewInfo.format = desc.format;

    if (isPreloading) {
      texture->smallMipsImageView = device->createImageView(image, viewInfo);
    } else {
      texture->allMipsImageView = device->createImageView(image, viewInfo);
    }

    RtxIo::Handle file;
    if (rtxio.openFile(texture->assetData->info().filename, &file)) {
      uint64_t completionSyncpt = 0;

      for (uint32_t layer = 0; layer < desc.numLayers; layer++) {
        completionSyncpt = scheduleImageLayerUpdateRtxIo(image, layer,
          texture->assetData, file, firstMip);
      }

      if (completionSyncpt) {
        texture->minUploadedMip = firstMip;

        if (isPreloading) {
          // TODO(iterentiev): in pre-loading phase we load into smallMipsImage, however, with async asset
          // loading the same managed texture can be in the loading load phase at the same time. Using single
          // state and syncpoint to syncronize two images may lead to a race. The asset pre-load will be
          // finalized anyways so it is not neccessary to do syncpoint syncronization here. However, in a
          // realworld we'd want to do proper timeline sync in future.
          texture->state = ManagedTexture::State::kVidMem;
          texture->completionSyncpt = ~0;
        } else {
          texture->state = ManagedTexture::State::kQueuedForUpload;
          texture->completionSyncpt = completionSyncpt;
        }
      }
    }
#endif
  }

  void TextureUtils::loadTextureToVidmem(Rc<ManagedTexture> texture, const Rc<DxvkDevice>& device, const Rc<DxvkContext>& ctx) {
    auto& assetData = *texture->assetData;
    auto& assetInfo = assetData.info();
    const auto& desc = texture->futureImageDesc;

    const DxvkFormatInfo* formatInfo = imageFormatInfo(assetInfo.format);

    Rc<DxvkImage> image = device->createImage(desc, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, DxvkMemoryStats::Category::RTXMaterialTexture, "material texture");

    // copy image data from disk
    for (uint32_t level = 0; level < assetInfo.mipLevels; ++level) {
      const VkExtent3D levelExtent = util::computeMipLevelExtent(assetInfo.extent, level);
      const VkExtent3D elementCount = util::computeBlockCount(levelExtent, formatInfo->blockSize);
      const uint32_t rowPitch = elementCount.width * formatInfo->elementSize;
      const uint32_t layerPitch = rowPitch * elementCount.height;

      ctx->updateImage(image,
        VkImageSubresourceLayers {
          VK_IMAGE_ASPECT_COLOR_BIT,
          level,
          0,
          assetInfo.numLayers
        },
        VkOffset3D { 0, 0, 0 },
        levelExtent,
        assetData.data(0, level),
        rowPitch, layerPitch);
    }

    // Make DxvkImageView
    DxvkImageViewCreateInfo viewInfo;
    viewInfo.type = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
    viewInfo.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.minLevel = 0;
    viewInfo.numLevels = assetInfo.mipLevels;
    viewInfo.minLayer = 0;
    viewInfo.numLayers = assetInfo.numLayers;
    viewInfo.format = desc.format;

    texture->allMipsImageView = device->createImageView(image, viewInfo);
    texture->state = ManagedTexture::State::kVidMem;
    texture->minUploadedMip = 0;
    texture->uniqueKey = RtxTextureManager::getUniqueKey();
  }

  void TextureUtils::loadTextureToHostStagingBuffer(Rc<ManagedTexture> texture, const Rc<DxvkDevice>& device, MipsToLoad mipsToLoad) {
    auto& assetData = *texture->assetData;
    auto& assetInfo = assetData.info();
    const auto& desc = texture->futureImageDesc;

    // Upload data through a staging buffer. Special care needs to
    // be taken when dealing with compressed image formats: Rather
    // than copying pixels, we'll be copying blocks of pixels.
    const DxvkFormatInfo* formatInfo = imageFormatInfo(assetInfo.format);

    int firstMip, lastMip;
    const int preloadMips = RtxTextureManager::calcPreloadMips(desc.mipLevels);
    assert(preloadMips > 0);
    const int firstPreloadMip = desc.mipLevels - preloadMips;

    const bool loadLowMips = (mipsToLoad == MipsToLoad::LowMips || mipsToLoad == MipsToLoad::All);
    const bool loadHighMips = (mipsToLoad == MipsToLoad::HighMips || mipsToLoad == MipsToLoad::All);
    
    switch (mipsToLoad) {
    case MipsToLoad::LowMips:
      // if we're loading low mips, we should have already loaded the high mips
      assert(texture->linearImageDataSmallMips);
      firstMip = 0;
      lastMip = firstPreloadMip - 1;
      break;

    case MipsToLoad::HighMips:
      firstMip = firstPreloadMip;
      lastMip = desc.mipLevels - 1;
      break;

    case MipsToLoad::All:
      firstMip = 0;
      lastMip = desc.mipLevels - 1;
      break;
    }

    // First get total size of staging buffer
    size_t totalSize = 0;
    for (uint32_t level = firstMip; level <= lastMip; ++level) {
      const VkExtent3D levelExtent = util::computeMipLevelExtent(assetInfo.extent, level);
      const VkExtent3D elementCount = util::computeBlockCount(levelExtent, formatInfo->blockSize);
      const size_t levelSize = formatInfo->elementSize * util::flattenImageExtent(elementCount);
      totalSize += align(levelSize, CACHE_LINE_SIZE);
    }

    // Allocate staging resource
    std::shared_ptr<uint8_t> pStagingData(new uint8_t[totalSize], std::default_delete<uint8_t[]>());
    uintptr_t pBaseDst = (uintptr_t) pStagingData.get();

    // Copy the data to staging
    for (uint32_t level = firstMip; level <= lastMip; ++level) {
      const VkExtent3D levelExtent = util::computeMipLevelExtent(assetInfo.extent, level);
      const VkExtent3D elementCount = util::computeBlockCount(levelExtent, formatInfo->blockSize);
      const uint32_t rowPitch = elementCount.width * formatInfo->elementSize;
      const uint32_t layerPitch = rowPitch * elementCount.height;

      // Copy to the correct offset
      util::packImageData((void*) pBaseDst, assetData.data(0, level), elementCount, formatInfo->elementSize, rowPitch, layerPitch);

      const size_t levelSize = formatInfo->elementSize * util::flattenImageExtent(elementCount);
      pBaseDst += align(levelSize, CACHE_LINE_SIZE);
    }

    if (loadLowMips) {
      texture->linearImageDataLargeMips = pStagingData;
    }

    if (loadHighMips) {
      texture->linearImageDataSmallMips = pStagingData;
      texture->numLargeMips = desc.mipLevels - preloadMips;
      texture->minUploadedMip = desc.mipLevels;
      texture->state = ManagedTexture::State::kHostMem;
    }
  }

  void TextureUtils::promoteHostToVid(const Rc<DxvkDevice>& device, const Rc<DxvkContext>& ctx, const Rc<ManagedTexture>& texture, uint32_t minMipLevel) {
    ScopedGpuProfileZone(ctx, "promoteHostToVid");

    if (texture->state == ManagedTexture::State::kVidMem) {
      Logger::warn("Tried to promote texture from HOST to VID, but it was already in VID...");
      return;
    }

    const DxvkImageCreateInfo& desc = texture->futureImageDesc;

    // Make the DxvkImage if it doesn't exist yet
    Rc<DxvkImage> image;

    // If this is a vid upgrade (low --> all mips) then use the existing memory
    if (texture->smallMipsImageView.ptr())
      image = texture->smallMipsImageView->image();

    // If this is a first time promotion, then allocate vid memory
    if (!image.ptr())
      image = device->createImage(desc, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, DxvkMemoryStats::Category::RTXMaterialTexture, "material texture");

    size_t currentOffsetLow = 0;
    size_t currentOffsetHigh = 0;
    // copy image data to GPU
    for (uint32_t level = 0; level < desc.mipLevels; ++level) {
      const DxvkFormatInfo* formatInfo = imageFormatInfo(desc.format);

      const VkImageSubresourceLayers subresourceLayers = { formatInfo->aspectMask, level, 0, 1 };
      
      const VkExtent3D elementCount = util::computeBlockCount(image->mipLevelExtent(level), formatInfo->blockSize);

      if (level >= minMipLevel && level < texture->minUploadedMip) {
        const uint32_t rowPitch = elementCount.width * formatInfo->elementSize;
        const uint32_t layerPitch = rowPitch * elementCount.height;

        uint8_t* ptr;
        if (level < texture->numLargeMips) {
          assert(texture->linearImageDataLargeMips);
          ctx->uploadImage(image, subresourceLayers, texture->linearImageDataLargeMips.get() + currentOffsetLow, rowPitch, layerPitch);
        } else {
          assert(texture->linearImageDataSmallMips);
          ctx->uploadImage(image, subresourceLayers, texture->linearImageDataSmallMips.get() + currentOffsetHigh, rowPitch, layerPitch);
        }
      }

      if (level < texture->numLargeMips) {
        currentOffsetLow += align(formatInfo->elementSize * util::flattenImageExtent(elementCount), CACHE_LINE_SIZE);
      } else {
        currentOffsetHigh += align(formatInfo->elementSize * util::flattenImageExtent(elementCount), CACHE_LINE_SIZE);
      }
    }

    DxvkImageViewCreateInfo viewInfo;
    viewInfo.type = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
    viewInfo.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.minLevel = minMipLevel;
    viewInfo.numLevels = desc.mipLevels - minMipLevel;
    viewInfo.minLayer = 0;
    viewInfo.numLayers = 1;
    viewInfo.format = desc.format;

    texture->minUploadedMip = minMipLevel;

    if (minMipLevel == 0) {
      texture->allMipsImageView = device->createImageView(image, viewInfo);
      texture->state = ManagedTexture::State::kVidMem;
    } else {
      texture->smallMipsImageView = device->createImageView(image, viewInfo);
    }
  }

  Rc<ManagedTexture> TextureUtils::createTexture(const Rc<AssetData>& assetData, ColorSpace colorSpace) {
    Rc<ManagedTexture> texture = new ManagedTexture();

    auto& assetInfo = assetData->info();

    // The nvtt_exporter tool used for png->dds conversion in the TREX export cannot specify SRGB, so we rely on the USD color space
    // setting, and override the format here.  Only applies to BC* formats, since that's all the png->dds conversion flow will generate.
    VkFormat format = colorSpace == ColorSpace::FORCE_BC_SRGB ? toSRGB(assetInfo.format) : assetInfo.format;

    // Initialize image create info
    DxvkImageCreateInfo& desc = texture->futureImageDesc;
    desc.type = VK_IMAGE_TYPE_2D;
    desc.format = format;
    desc.flags = 0;
    desc.sampleCount = VK_SAMPLE_COUNT_1_BIT;
    desc.extent = assetInfo.extent;
    desc.numLayers = assetInfo.numLayers;
    desc.mipLevels = assetInfo.mipLevels;
    desc.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    desc.stages = VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR;
    desc.access = VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;
    desc.tiling = VK_IMAGE_TILING_OPTIMAL;
    desc.layout = VK_IMAGE_LAYOUT_GENERAL;

    texture->mipCount = assetInfo.mipLevels;
    texture->assetData = new ImageAssetDataView(assetData, 0);
    texture->minUploadedMip = 0;
    texture->uniqueKey = RtxTextureManager::getUniqueKey();
    texture->state = ManagedTexture::State::kInitialized;

    return texture;
  }

  void TextureUtils::loadTexture(Rc<ManagedTexture> texture, const Rc<DxvkDevice>& device, const Rc<DxvkContext>& ctx,
                                 const MemoryAperture mem, MipsToLoad mipsToLoad, int minimumMipLevel) {
    ScopedCpuProfileZone();

    // Todo: Currently the options in this function must remain constant at runtime so that the minimum mip level calculations here stay
    // the same. This is not ideal as ideally these options should be able to be changed at runtime to allow for new textures loaded after
    // the point to take advantage of the new textures. This should be improved to make this function less fragile and make the options
    // less confusing (as some of these options are changed as "texture quality" settings in the user graphics settings menu, but will not
    // take effect until Remix is restarted).

    // Fetch the pre-configured minimum mip level
    if (minimumMipLevel >= 0) {
      texture->largestMipLevel = minimumMipLevel;
    } else {
      minimumMipLevel = texture->largestMipLevel;
    }

    // Figure out the actual minimum mip level
    if (RtxOptions::Get()->getInitialForceHighResolutionReplacementTextures()) {
      minimumMipLevel = 0;
    } else if (RtxOptions::Get()->getInitialEnableAdaptiveResolutionReplacementTextures()) {
      minimumMipLevel = std::max(minimumMipLevel, static_cast<int>(RtxOptions::Get()->getInitialMinReplacementTextureMipMapLevel()));
    } else {
      minimumMipLevel = RtxOptions::Get()->getInitialMinReplacementTextureMipMapLevel();
    }

    // Adjust the asset data view if necessary
    if (minimumMipLevel != 0 && texture->mipCount > 1) {
      const int baseLevel = std::min(minimumMipLevel, (int) texture->mipCount - 1);
      static_cast<ImageAssetDataView*>(&*texture->assetData)->setMinLevel(baseLevel);
    }

    // Adjust image create info
    texture->futureImageDesc.extent = texture->assetData->info().extent;
    texture->futureImageDesc.mipLevels = texture->assetData->info().mipLevels;

    if (RtxIo::enabled()) {
      loadTextureRtxIo(texture, device, mipsToLoad);
    } else if (mem == MemoryAperture::HOST) {
      loadTextureToHostStagingBuffer(texture, device, mipsToLoad);
    } else {
      assert(mem == MemoryAperture::VID);
      loadTextureToVidmem(texture, device, ctx);
    }

    texture->assetData->evictCache();
  }
} // namespace dxvk
