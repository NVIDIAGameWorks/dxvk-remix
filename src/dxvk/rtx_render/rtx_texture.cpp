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
    RtxIo::Handle        assetFile) {
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

        assetData->placement(layer, 0, n, src.offset, src.size);

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
      assetData->placement(layer, 0, 0, offset, size);
      // TODO: optimize
      for (uint32_t n = 1; n < desc.mipLevels; n++) {
        uint64_t levelOffset;
        size_t levelSize;
        assetData->placement(layer, 0, n, levelOffset, levelSize);
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

  static Rc<DxvkImageView> loadTextureRtxIo(
    const Rc<ManagedTexture>&  texture,
    const Rc<DxvkContext>&     ctx,
    const DxvkImageCreateInfo& desc,
    const bool                 isPreloading) {
#ifdef WITH_RTXIO
    const Rc<DxvkDevice>& device = ctx->getDevice();

    auto& rtxio = RtxIo::get();

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

    Rc<DxvkImageView> view = device->createImageView(image, viewInfo);

    RtxIo::Handle file;
    if (rtxio.openFile(texture->assetData->info().filename, &file)) {
      uint64_t completionSyncpt = 0;

      for (uint32_t layer = 0; layer < desc.numLayers; layer++) {
        completionSyncpt = scheduleImageLayerUpdateRtxIo(image, layer,
          texture->assetData, file);
      }

      if (completionSyncpt) {
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

    return view;
#endif
    return nullptr;
  }

  // At the moment RTX IO can only load mip tail all at once.
  // We need to adjust the number of texture mips if it is less
  // than the number of mips in the asset mip tail.
  // This should not happen since in this case we'll use the preloaded assets,
  // adding a check and a warning message for future proofing.
  static void validateMipTailRtxIo(
    Rc<ManagedTexture>&  texture,
    DxvkImageCreateInfo& createInfo,
    int                  baseLevel) {

    // Check if we're dealing with the mip tail first.
    if (texture->assetData->info().looseLevels != 0) {
      return;
    }

    // Reset base level to get the original asset data
    texture->assetData->setMinLevel(0);

    const uint32_t numTailMips = texture->mipCount - texture->assetData->info().looseLevels;

    if (createInfo.mipLevels < numTailMips) {
      ONCE(Logger::warn(str::format("[RTXIO-Compatibility-Info] The number of mip levels in texture (",
        createInfo.mipLevels, ") is smaller than the number of mip levels in the asset mip tail (",
        numTailMips, ")! Adjusting texture creation description.")));

      // Set asset base level to the first tail level
      baseLevel = texture->assetData->info().looseLevels;

      // Adjust image create info
      createInfo.extent = texture->assetData->info().extent;
      createInfo.mipLevels = texture->assetData->info().mipLevels;
    }

    // Restore base level
    texture->assetData->setMinLevel(baseLevel);
  }

  Rc<DxvkImageView> loadTextureToVidmem(
        const Rc<ManagedTexture>&  texture,
        const Rc<DxvkContext>&     ctx,
        const DxvkImageCreateInfo& desc,
        const bool                 isPreloading) {
    const Rc<DxvkDevice>& device = ctx->getDevice();

    auto& assetData = *texture->assetData;
    auto& assetInfo = assetData.info();

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
    viewInfo.numLevels = desc.mipLevels;
    viewInfo.minLayer = 0;
    viewInfo.numLayers = desc.numLayers;
    viewInfo.format = desc.format;

    Rc<DxvkImageView> view = device->createImageView(image, viewInfo);
    texture->state = ManagedTexture::State::kVidMem;

    return view;
  }

  void ManagedTexture::demote() {
    if (canDemote && (state == ManagedTexture::State::kVidMem || state == ManagedTexture::State::kFailed)) {
      // Evict large image
      allMipsImageView = nullptr;
      completionSyncpt = ~0;

      if (!RtxIo::enabled()) {
        // RTXIO path does not evict small images
        smallMipsImageView = nullptr;
        state = ManagedTexture::State::kInitialized;
        minPreloadedMip = -1;
      }
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
    texture->uniqueKey = RtxTextureManager::getUniqueKey();
    texture->state = ManagedTexture::State::kInitialized;

    return texture;
  }

  void TextureUtils::loadTexture(Rc<ManagedTexture> texture, const Rc<DxvkContext>& ctx, const bool isPreloading, int minimumMipLevel) {
    ScopedCpuProfileZone();

    if (!isPreloading) {
      // Apply config overrides if we're not preloading
      if (RtxOptions::Get()->forceHighResolutionReplacementTextures()) {
        minimumMipLevel = 0;
      } else if (RtxOptions::Get()->enableAdaptiveResolutionReplacementTextures()) {
        minimumMipLevel = std::max<int>(minimumMipLevel, RtxOptions::Get()->minReplacementTextureMipMapLevel());
      } else {
        minimumMipLevel = RtxOptions::Get()->minReplacementTextureMipMapLevel();
      }
    }

    // Adjust the asset data view if necessary
    const int baseLevel = std::min(minimumMipLevel, texture->mipCount - 1);
    texture->assetData->setMinLevel(baseLevel);

    if (isPreloading) {
      texture->minPreloadedMip = baseLevel;
    }

    if (!isPreloading && texture->minPreloadedMip >= 0 && texture->minPreloadedMip <= baseLevel) {
      // The texture has been preloaded and the preloaded levels are larger
      // or equal to the base level of the incoming request.
      // There's no point in loading same texture again, we can just
      // set all mips view to the small mips view.
      texture->allMipsImageView = texture->smallMipsImageView;
      return;
    }

    // Adjust image create info
    texture->futureImageDesc.extent = texture->assetData->info().extent;
    texture->futureImageDesc.mipLevels = texture->assetData->info().mipLevels;

    Rc<DxvkImageView> viewTarget;

    if (RtxIo::enabled()) {
      validateMipTailRtxIo(texture, texture->futureImageDesc, baseLevel);
      viewTarget = loadTextureRtxIo(texture, ctx, texture->futureImageDesc, isPreloading);
    } else {
      viewTarget = loadTextureToVidmem(texture, ctx, texture->futureImageDesc, isPreloading);
    }

    if (isPreloading) {
      texture->smallMipsImageView = viewTarget;

      if (texture->minPreloadedMip == 0) {
        // If texture was fully loaded, set all mips view as well and skip future load.
        texture->allMipsImageView = viewTarget;
      }
    } else {
      texture->allMipsImageView = viewTarget;

      // Get rid of cached higher res mips. Wipe caches of images that skipped
      // the preload phase, but keep the fully preloaded images.
      const uint32_t firstLevelToKeep = texture->minPreloadedMip < 0 ?
        texture->mipCount : texture->minPreloadedMip;

      for (uint32_t level = 0; level < firstLevelToKeep; level++) {
        texture->assetData->evictCache(0, level);
      }
    }

    // Release asset source to keep the number of open file low
    texture->assetData->releaseSource();
  }
} // namespace dxvk
