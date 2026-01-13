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
#include "rtx_texture_manager.h"

namespace dxvk {

#ifdef WITH_RTXIO
  // Helper to schedule image layer update with RTXIO.
  // The entire layer mip-chain will be updated with the data starting from
  // the asset assetBaseMip.
  static uint64_t scheduleImageLayerUpdateRtxIo(
    const Rc<DxvkImage>& image,
    const int            layer,
    const uint32_t       mipLevels_begin,
    const uint32_t       mipLevels_end, // non-inclusive
    const Rc<AssetData>& assetData,
    RtxIo::Handle        assetFile) {
    auto& rtxio = RtxIo::get();
    const auto& assetInfo = assetData->info();

    // The number of mip levels in the tail data blob.
    // For loose dds files this will be always 0.
    const uint32_t tailMipLevels = assetInfo.mininumLevelsToUpload;
    assert(mipLevels_begin < mipLevels_end);
    assert(mipLevels_end - mipLevels_begin == image->info().mipLevels);

    uint64_t completionSyncpt = 0;

    if (assetInfo.compression != AssetCompression::None) {
      RtxIo::FileSource src { assetFile, 0, 0, true };
      RtxIo::ImageDest dst { image, static_cast<uint16_t>(layer), 0, 1 };

      // For compressed images we need to load loose mip levels
      // one-by-one except the mip levels in the tail blob.
      for (uint32_t n = mipLevels_begin; n < mipLevels_end; n++) {
        const bool isTail = mipLevels_end - n <= tailMipLevels;

        assetData->placement(layer, 0, n, src.offset, src.size);

        dst.startMip = n - mipLevels_begin;
        dst.count = isTail ? tailMipLevels : 1;

        completionSyncpt = rtxio.enqueueRead(dst, src);

        if (isTail) {
          break;
        }
      }
    } else {
      uint64_t offset;
      size_t size;
      assetData->placement(layer, 0, mipLevels_begin, offset, size);
      // TODO: optimize
      for (uint32_t n = mipLevels_begin + 1; n < mipLevels_end; n++) {
        uint64_t levelOffset;
        size_t levelSize;
        assetData->placement(layer, 0, n, levelOffset, levelSize);
        size += levelSize;
      }

      RtxIo::FileSource src { assetFile, offset, size, false };
      RtxIo::ImageDest dst {
        image,
        static_cast<uint16_t>(layer),
        0, static_cast<uint16_t>(mipLevels_end - mipLevels_begin)
      };

      completionSyncpt = rtxio.enqueueRead(dst, src);
    }

    return completionSyncpt;
  }
#endif

  void loadTextureRtxIo(const Rc<ManagedTexture>& texture,
                        const Rc<DxvkImageView>& dstImage,
                        const uint32_t mipLevels_begin,
                        const uint32_t mipLevels_end /* non-inclusive */ ) {
#ifdef WITH_RTXIO
    auto& rtxio = RtxIo::get();
    
    RtxIo::Handle file;
    if (rtxio.openFile(texture->m_assetData->info().filename, &file)) {
      uint64_t completionSyncpt = 0;

      for (uint32_t layer = 0; layer < dstImage->info().numLayers; layer++) {
        completionSyncpt = scheduleImageLayerUpdateRtxIo(dstImage->image(),
                                                         layer,
                                                         mipLevels_begin,
                                                         mipLevels_end,
                                                         texture->m_assetData,
                                                         file);
      }

      if (completionSyncpt) {
        assert(texture->m_state == ManagedTexture::State::kQueuedForUpload);
        texture->m_completionSyncpt = completionSyncpt;
      }
    }
#endif
  }

  Rc<ManagedTexture> TextureUtils::createTexture(const Rc<AssetData>& assetData, ColorSpace colorSpace)
  {
    Rc<ManagedTexture> texture = new ManagedTexture();

    texture->m_assetData = assetData;
    texture->m_colorSpace = colorSpace;
    texture->m_uniqueKey = RtxTextureManager::getUniqueKey();
    texture->m_state = ManagedTexture::State::kInitialized;

    return texture;
  }

  DxvkImageCreateInfo ManagedTexture::imageCreateInfo() const {
    const AssetInfo& assetInfo = m_assetData->info();

    // The nvtt_exporter tool used for png->dds conversion in the TREX export cannot specify SRGB, so we rely on the USD color space
    // setting, and override the format here.  Only applies to BC* formats, since that's all the png->dds conversion flow will generate.
    VkFormat format = m_colorSpace == ColorSpace::FORCE_BC_SRGB ? TextureUtils::toSRGB(assetInfo.format) : assetInfo.format;

    DxvkImageCreateInfo desc{};
    desc.type = VK_IMAGE_TYPE_2D;
    desc.format = format;
    desc.flags = 0;
    desc.sampleCount = VK_SAMPLE_COUNT_1_BIT;
    desc.extent = assetInfo.extent;
    desc.numLayers = assetInfo.numLayers;
    desc.mipLevels = assetInfo.mipLevels;
    desc.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    desc.stages =
      VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT
      | VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR;
    desc.access = VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;
    desc.tiling = VK_IMAGE_TILING_OPTIMAL;
    desc.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    return desc;
  }

  uint16_t clampMipCountToAvailable(const Rc<AssetData>& assetData, uint32_t targetMipCount) {
    if (targetMipCount == 0) {
      return 0;
    }

    assert(assetData.ptr());
    assert(assetData->info().mininumLevelsToUpload > 0);
    assert(assetData->info().mipLevels > 0);
    assert(assetData->info().mininumLevelsToUpload <= assetData->info().mipLevels);

    const uint32_t maxMips = assetData->info().mipLevels;
    const uint32_t minMips = std::clamp(assetData->info().mininumLevelsToUpload, 1u, maxMips);

    return std::clamp(targetMipCount, minMips, maxMips);
  }

  bool ManagedTexture::hasUploadedMips(uint32_t requiredMips, bool exact) const {
    assert(m_currentMip_begin <= m_currentMip_end);
    const uint32_t uploaded = m_currentMip_end - m_currentMip_begin;

    return exact ? (clampMipCountToAvailable(m_assetData, uploaded) == clampMipCountToAvailable(m_assetData, requiredMips))
                 : (clampMipCountToAvailable(m_assetData, uploaded) >= clampMipCountToAvailable(m_assetData, requiredMips));
  }

  void ManagedTexture::requestMips(uint32_t requiredMips) {
    static_assert(MAX_MIPS <= std::numeric_limits<decltype(m_requestedMips)::value_type>::max());
    assert(requiredMips <= MAX_MIPS);

    if (!m_canDemote) {
      requiredMips = MAX_MIPS;
    }

    m_requestedMips = uint8_t(std::clamp<uint32_t>(requiredMips, 1, MAX_MIPS));
  }

  std::pair<uint16_t, uint16_t> ManagedTexture::calcRequiredMips_BeginEnd() const {
    const uint32_t mipCountToLoad = clampMipCountToAvailable(m_assetData, uint32_t(m_requestedMips));

    const uint32_t mip_end = m_assetData->info().mipLevels;
    const uint16_t mip_begin = uint16_t(mip_end - mipCountToLoad);

    return { mip_begin, mip_end };
  }

} // namespace dxvk
