/*
* Copyright (c) 2021-2022, NVIDIA CORPORATION. All rights reserved.
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
#include "rtx_asset_exporter.h"
#include "rtx_types.h"
#include "rtx_context.h"
#include "../../util/sync/sync_signal.h"
#include "../dxvk_device.h"
#include "../dxvk_context.h"
#include "../dxvk_buffer.h"
#include <gli/gli.hpp>
#include <gli/convert.hpp>
#include <gli/save.hpp>
#include <string>
#include <charconv>
#include <functional>

namespace {

  bool shouldUseBlit(VkFormat format) {
    switch (format) {

    case VK_FORMAT_B8G8R8A8_UNORM:
    case VK_FORMAT_B8G8R8A8_SRGB:
      return true;

    default:
      return false;
    }
  }

  VkFormat normalizeTargetFormat(VkFormat format) {
    switch (format) {

    case VK_FORMAT_B8G8R8A8_UNORM:
      return VK_FORMAT_R8G8B8A8_UNORM;

    case VK_FORMAT_B8G8R8A8_SRGB:
      return VK_FORMAT_R8G8B8A8_SRGB;

    default:
      return format;
    }
  }

  // Note: Converts "unusual" DDS formats which are often not well-supported by visualization and editing tools to
  // more compatible formats.
  gli::format unusualToStandardFormat(const gli::format f) {
    switch (f) {
    case gli::format::FORMAT_RGBA4_UNORM_PACK16:
    case gli::format::FORMAT_BGRA4_UNORM_PACK16:
    case gli::format::FORMAT_RGB5A1_UNORM_PACK16:
    case gli::format::FORMAT_BGR5A1_UNORM_PACK16:
    case gli::format::FORMAT_A1RGB5_UNORM_PACK16:
      return gli::format::FORMAT_RGBA8_UNORM_PACK8;
    case gli::format::FORMAT_R5G6B5_UNORM_PACK16:
      return gli::format::FORMAT_B5G6R5_UNORM_PACK16;
    default:
      return f;
    }
  }

  VkFormat gliFormatToVk(gli::format format) {
    return static_cast<VkFormat>(format);
  }

  VkExtent3D gliExtentToVk(gli::extent2d ext) {
    return VkExtent3D {
      static_cast<uint32_t>(ext.x),
      static_cast<uint32_t>(ext.y),
      1
    };
  }
}

namespace dxvk {

  void AssetExporter::waitForAllExportsToComplete(const float numSecsToWait) {

    if (m_numExportsInFlight > 0) {
      Logger::info(str::format("RTX: Waiting for ", m_numExportsInFlight, " asset exports to complete"));

      auto startTime = std::chrono::system_clock::now();
      while (m_numExportsInFlight > 0 &&
             std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now() - startTime).count() < numSecsToWait) {
        Sleep(1);
      }

      if (m_numExportsInFlight > 0)
        Logger::err(str::format("RTX: Timed-out waiting on all asset exports to complete"));
    }
  }

  void AssetExporter::exportImage(Rc<DxvkContext> ctx, const std::string& filename, Rc<DxvkImage> image, bool thumbnail/* = false*/) {
    // NOTE: Should use a mutex here...
    {
      std::lock_guard lock(m_readbackSignalMutex);
      if (m_readbackSignal == nullptr) {
        m_readbackSignal = new sync::Fence(m_signalValue);
      }
    }

    m_numExportsInFlight++;

    // We want to retain most of the src image state
    DxvkImageCreateInfo srcDesc = image->info();
    DxvkImageCreateInfo dstDesc = image->info();

    // NOTE: Some image formats arent well supported by DDS tools, like B8G8R8A8...  So we might want to blit rather than copy for such formats.
    const bool useBlit = shouldUseBlit(srcDesc.format);

    // TODO: Just use blit for swizzle conversion?
    gli::swizzles swizzle = gli::swizzles(gli::SWIZZLE_RED, gli::SWIZZLE_GREEN, gli::SWIZZLE_BLUE, gli::SWIZZLE_ALPHA);
    if (dstDesc.format == VK_FORMAT_A4R4G4B4_UNORM_PACK16_EXT) {
      dstDesc.format = VK_FORMAT_B4G4R4A4_UNORM_PACK16;
      swizzle = gli::swizzles(gli::SWIZZLE_BLUE, gli::SWIZZLE_GREEN, gli::SWIZZLE_RED, gli::SWIZZLE_ALPHA);
    }

    dstDesc.format = normalizeTargetFormat(dstDesc.format);

    // Detect changes in GLI since we're casting the VK format to GLI
    assert(gli::format::FORMAT_LAST >= (gli::format)dstDesc.format);
    const gli::format outFormat = (gli::format)dstDesc.format;

    if (thumbnail) {
      // Some default parameters for thumbnails
      dstDesc.extent.width = 512;
      dstDesc.extent.height = 512;
      dstDesc.extent.depth = 1;
      dstDesc.mipLevels = 1;
      assert(!gli::is_compressed(outFormat));
    }

    // NOTE: Only supporting non-array Textures for now.
    assert(dstDesc.numLayers == 1);

    const uint32_t numMipLevels = dstDesc.mipLevels;

    Rc<DxvkImage>* pBlitTemps = useBlit ? new Rc<DxvkImage>[numMipLevels] : nullptr;
    Rc<DxvkImage>* pBlitDests = new Rc<DxvkImage>[numMipLevels];

    // Push a copy operation to the GPU; get that GPU data in CPU addressable space!
    for (uint32_t level = 0; level < numMipLevels; ++level) {
      const VkImageSubresourceLayers srcSubresourceLayers = { imageFormatInfo(srcDesc.format)->aspectMask, level, 0, 1 };
      const VkImageSubresourceLayers dstSubresourceLayers = { imageFormatInfo(dstDesc.format)->aspectMask, 0,     0, 1 };

      const VkExtent3D srcExtent = srcDesc.extent;
      const VkExtent3D dstExtent = thumbnail ? dstDesc.extent : image->mipLevelExtent(level);

      if (useBlit)
      {
        DxvkImageCreateInfo desc = dstDesc;
        desc.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        desc.stages = VK_PIPELINE_STAGE_TRANSFER_BIT;
        desc.access = VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT;
        desc.tiling = VK_IMAGE_TILING_OPTIMAL;
        desc.layout = VK_IMAGE_LAYOUT_UNDEFINED;
        desc.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        desc.mipLevels = 1;
        desc.numLayers = 1;
        desc.extent = dstExtent;

        // Temp image to blit into (pBlitDests is linear, so we can only copy into)
        pBlitTemps[level] = ctx->getDevice()->createImage(desc, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, DxvkMemoryStats::Category::RTXMaterialTexture, "exportImage blit temp");
      }

      {
        // Modify the state we need for reading on the CPU
        DxvkImageCreateInfo desc = dstDesc;
        desc.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        desc.stages = VK_PIPELINE_STAGE_TRANSFER_BIT;
        desc.access = VK_ACCESS_TRANSFER_WRITE_BIT;
        desc.tiling = VK_IMAGE_TILING_LINEAR;
        desc.layout = VK_IMAGE_LAYOUT_UNDEFINED;
        desc.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        desc.mipLevels = 1; // Can only export 1 mip level at a time.
        desc.numLayers = 1; // Can only export 1 layer at a time.
        desc.extent = dstExtent;

        // Make the image where we'll copy the GPU resource to CPU accessible mem
        pBlitDests[level] = ctx->getDevice()->createImage(desc, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, DxvkMemoryStats::Category::RTXMaterialTexture, "exportimage blit dest");
      }

      VkOffset3D srcOffset = VkOffset3D { 0,0,0 };
      if (thumbnail) {
        // Center the thumbail on the image
        srcOffset = VkOffset3D { int32_t(srcExtent.width) / 2 - int32_t(dstExtent.width) / 2,
                                 int32_t(srcExtent.height) / 2 - int32_t(dstExtent.height) / 2,
                                 0 };
      }

      if (useBlit) {
        VkComponentMapping identityMap = { VK_COMPONENT_SWIZZLE_IDENTITY,VK_COMPONENT_SWIZZLE_IDENTITY,VK_COMPONENT_SWIZZLE_IDENTITY,VK_COMPONENT_SWIZZLE_IDENTITY };

        VkImageBlit region = {};
        region.srcSubresource = srcSubresourceLayers;
        region.srcOffsets[0] = srcOffset;
        region.srcOffsets[1] = { srcOffset.x + int32_t(dstExtent.width), 
                                 srcOffset.y + int32_t(dstExtent.height), 
                                 srcOffset.z + int32_t(dstExtent.depth) };
        region.dstSubresource = dstSubresourceLayers;
        region.dstOffsets[0] = { 0,0,0 };
        region.dstOffsets[1] = { int32_t(dstExtent.width), int32_t(dstExtent.height), int32_t(dstExtent.depth) };

        ctx->changeImageLayout(pBlitTemps[level], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

        // Blit src to temp
        ctx->blitImage(pBlitTemps[level], identityMap, image, identityMap, region, VK_FILTER_NEAREST);

        ctx->changeImageLayout(pBlitTemps[level], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        ctx->changeImageLayout(pBlitDests[level], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

        // Copy temp to system memory
        ctx->copyImage(pBlitDests[level], dstSubresourceLayers, VkOffset3D { 0,0,0 }, pBlitTemps[level], dstSubresourceLayers, VkOffset3D { 0,0,0 }, dstExtent);
      }
      else
      {
        ctx->changeImageLayout(pBlitDests[level], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

        // Blit src to system memory
        ctx->copyImage(pBlitDests[level], dstSubresourceLayers, VkOffset3D { 0,0,0 }, image, srcSubresourceLayers, srcOffset, dstExtent);
      }
    }

    ctx->emitMemoryBarrier(0,
      VK_PIPELINE_STAGE_TRANSFER_BIT,
      VK_ACCESS_TRANSFER_WRITE_BIT,
      VK_PIPELINE_STAGE_HOST_BIT,
      VK_ACCESS_HOST_READ_BIT);

    // Sync point, before writing to disk, we must wait on GPU
    const uint64_t syncValue = ++m_signalValue;
    ctx->signal(m_readbackSignal, syncValue);

    const gli::extent3d outExtent = { dstDesc.extent.width, dstDesc.extent.height, 1 };

    // Push texture header to the GLI container
    gli::texture2d exportTex(outFormat, outExtent, dstDesc.mipLevels, swizzle);

    // Spawn a thread so we dont sync with the GPU here...(remember, GPU runs async with CPU!).  
    // NOTE: A task scheduler will probably be better longterm here
    dxvk::thread exporterThread([this, device = ctx->getDevice(), pBlitDests, pBlitTemps, syncValue, filename, exportTex = std::move(exportTex)] {
      // Stall until the GPU has completed its copy to system memory (GPU->CPU)
      this->m_readbackSignal->wait(syncValue);

      const DxvkFormatInfo* formatInfo = imageFormatInfo(gliFormatToVk(exportTex.format()));

      for (uint32_t level = 0; level < exportTex.levels(); ++level) {
        const Rc<DxvkImage>& image = pBlitDests[level];

        // Calculate the Subresource Layout for the Image

        VkImageSubresource subresource;
        subresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        subresource.mipLevel = level;
        subresource.arrayLayer = 0;
        const VkSubresourceLayout subresourceLayout = image->querySubresourceLayout(subresource);

        // Get destination and source pointers for writing/reading

        void* pDst = (void*)exportTex.data(exportTex.base_layer(), exportTex.base_face(), level);
        const void* pSrc = image->mapPtr(0);

        const VkExtent3D levelExtent = gliExtentToVk(exportTex.extent(level));
        const VkExtent3D elementCount = util::computeBlockCount(levelExtent, formatInfo->blockSize);
        const uint32_t rowPitch = elementCount.width * formatInfo->elementSize;
        const uint32_t layerPitch = rowPitch * elementCount.height;

        util::packImageData(pDst, pSrc, subresourceLayout.rowPitch, subresourceLayout.arrayPitch,
                            rowPitch, layerPitch, VK_IMAGE_TYPE_2D, levelExtent, 1, formatInfo,
                            subresource.aspectMask);
      }

      // Write our file, converting its format first if nessecary
      bool success = false;
      auto const standardizedFormat = unusualToStandardFormat(exportTex.format());
      if (standardizedFormat != exportTex.format()) {
        success = gli::save(gli::convert(exportTex, standardizedFormat), filename);
      } else {
        success = gli::save(exportTex, filename);
      }

      if (!success) {
        Logger::err(str::format("RTX: Failed to write texture \"", filename, "\""));
      }

      delete[] pBlitTemps;
      delete[] pBlitDests;
      m_numExportsInFlight--;
    });
    exporterThread.detach();
  }

  void AssetExporter::exportBuffer(Rc<DxvkContext> ctx, const DxvkBufferSlice& buffer, BufferCallback bufferCallback) {
    // NOTE: Should use a mutex here...
    {
      std::lock_guard lock(m_readbackSignalMutex);
      if (m_readbackSignal == nullptr) {
        m_readbackSignal = new sync::Fence(m_signalValue);
      }
    }

    m_numExportsInFlight++;

    // We want to retain most of the src image state
    // NOTE: Some image formats arent well supported by DDS tools, like B8G8R8A8...  So we might want to blit rather than copy for such formats.
    DxvkBufferCreateInfo desc = buffer.bufferInfo();

    // Modify the state we need for reading on the CPU
    desc.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    desc.stages = VK_PIPELINE_STAGE_TRANSFER_BIT;
    desc.access = VK_ACCESS_TRANSFER_WRITE_BIT;
    desc.size = buffer.length();

    // Make the image where we'll copy the GPU resource to CPU accessible mem
    Rc<DxvkBuffer> bufferDest = ctx->getDevice()->createBuffer(desc, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT, DxvkMemoryStats::Category::RTXBuffer);

    ctx->copyBuffer(bufferDest, VkDeviceSize{0}, buffer.buffer(), buffer.offset(), desc.size);

    ctx->emitMemoryBarrier(0,
      VK_PIPELINE_STAGE_TRANSFER_BIT,
      VK_ACCESS_TRANSFER_WRITE_BIT,
      VK_PIPELINE_STAGE_HOST_BIT,
      VK_ACCESS_HOST_READ_BIT);

    // Sync point, before writing to disk, we must wait on GPU
    const uint64_t syncValue = ++m_signalValue;
    ctx->signal(m_readbackSignal, syncValue);
    auto asyncWaitThenCallback = [this, cDestBuffer = bufferDest, syncValue](BufferCallback bufferCallback) {
      // Stall until the GPU has completed its copy to system memory (GPU->CPU)
      this->m_readbackSignal->wait(syncValue);
      bufferCallback(cDestBuffer);
      m_numExportsInFlight--;
    };
    std::thread(asyncWaitThenCallback, bufferCallback).detach();
  }

  void AssetExporter::generateSceneThumbnail(Rc<DxvkContext> ctx, const std::string& dir, const std::string& filename) {
    auto& resourceManager = ctx->getCommonObjects()->getResources();
    auto finalOutput = resourceManager.getRaytracingOutput().m_finalOutput.image;

    env::createDirectory(dir);

    exportImage(ctx, str::format(dir, filename, ".dds"), finalOutput, true);
  }

  void AssetExporter::bakeSkyProbe(Rc<DxvkContext> ctx, const std::string& dir, const std::string& filename) {
    auto& resourceManager = ctx->getCommonObjects()->getResources();

    auto skyprobeView = resourceManager.getSkyProbe(ctx);

    const auto skyprobeExt = skyprobeView.image->info().extent;
    const uint32_t equatorLength = std::min(skyprobeExt.width * 4, 16384u);
    const VkExtent3D latlongExt { equatorLength, equatorLength / 2, 1 };

    auto latlong = resourceManager.createImageResource(ctx, "sky probe latlong", latlongExt, VK_FORMAT_R16G16B16A16_SFLOAT);

    const auto transform = RtxOptions::Get()->isZUp() ? RtxImageUtils::LatLongTransform::ZUp2OpenEXR : RtxImageUtils::LatLongTransform::None;

    ctx->getCommonObjects()->metaImageUtils().cubemapToLatLong(ctx, skyprobeView.view, latlong.view, transform);

    dumpImageToFile(ctx, dir, filename, latlong.image);
  }

} // namespace dxvk