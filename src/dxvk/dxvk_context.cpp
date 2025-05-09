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
#include <cstring>
#include <vector>
#include <utility>

#include "dxvk_device.h"
#include "dxvk_context.h"
#include "../d3d9/d3d9_state.h"
#include "../d3d9/d3d9_spec_constants.h"

namespace dxvk {
  DxvkContext::DxvkContext(const Rc<DxvkDevice>& device)
    : m_device(device),
    m_common(&device->m_objects),
    m_sdmaAcquires(DxvkCmdBuffer::SdmaBuffer),
    m_sdmaBarriers(DxvkCmdBuffer::SdmaBuffer),
    m_initBarriers(DxvkCmdBuffer::InitBuffer),
    m_execAcquires(DxvkCmdBuffer::ExecBuffer),
    m_execBarriers(DxvkCmdBuffer::ExecBuffer),
    m_gfxBarriers(DxvkCmdBuffer::ExecBuffer),
    m_queryManager(m_common->queryPool()),
    m_staging     (device, StagingBufferSize) {
    if (m_device->features().extRobustness2.nullDescriptor)
      m_features.set(DxvkContextFeature::NullDescriptors);
    if (m_device->features().extExtendedDynamicState.extendedDynamicState)
      m_features.set(DxvkContextFeature::ExtendedDynamicState);

    // Init framebuffer info with default render pass in case
    // the app does not explicitly bind any render targets
    m_state.om.framebufferInfo = makeFramebufferInfo(m_state.om.renderTargets);
  }


  DxvkContext::~DxvkContext() {

  }

  // NV-DXVK start: DLFG integration
  bool DxvkContext::isDLFGEnabled() const {
    ScopedCpuProfileZone();
    return m_common->metaNGXContext().supportsDLFG() && DxvkDLFG::enable() && !m_common->metaDLFG().hasDLFGFailed();
  }

  uint32_t DxvkContext::dlfgInterpolatedFrameCount() const {
    return isDLFGEnabled() ? m_common->metaDLFG().getInterpolatedFrameCount() : 0;
  }

  uint32_t DxvkContext::dlfgMaxSupportedInterpolatedFrameCount() const {
    return m_common->metaNGXContext().supportsDLFG() ? m_common->metaNGXContext().dlfgMaxInterpolatedFrames() : 0;
  }
  // NV-DXVK end

  void DxvkContext::beginRecording(const Rc<DxvkCommandList>& cmdList) {
    ScopedCpuProfileZone();
    m_cmd = cmdList;
    m_cmd->beginRecording();

    // Mark all resources as untracked
    m_vbTracked.clear();
    m_rcTracked.clear();

    // The current state of the internal command buffer is
    // undefined, so we have to bind and set up everything
    // before any draw or dispatch command is recorded.
    m_flags.clr(
      DxvkContextFlag::GpRenderPassBound,
      DxvkContextFlag::GpXfbActive);

    m_flags.set(
      DxvkContextFlag::GpDirtyFramebuffer,
      DxvkContextFlag::GpDirtyPipeline,
      DxvkContextFlag::GpDirtyPipelineState,
      DxvkContextFlag::GpDirtyResources,
      DxvkContextFlag::GpDirtyVertexBuffers,
      DxvkContextFlag::GpDirtyIndexBuffer,
      DxvkContextFlag::GpDirtyXfbBuffers,
      DxvkContextFlag::GpDirtyBlendConstants,
      DxvkContextFlag::GpDirtyStencilRef,
      DxvkContextFlag::GpDirtyViewport,
      DxvkContextFlag::GpDirtyDepthBias,
      DxvkContextFlag::GpDirtyDepthBounds,
      DxvkContextFlag::CpDirtyPipeline,
      DxvkContextFlag::CpDirtyPipelineState,
      DxvkContextFlag::CpDirtyResources,
      DxvkContextFlag::RpDirtyPipeline,
      DxvkContextFlag::RpDirtyPipelineState,
      DxvkContextFlag::RpDirtyResources,
      DxvkContextFlag::DirtyDrawBuffer);
  }



  Rc<DxvkCommandList> DxvkContext::endRecording() {
    ScopedCpuProfileZone();
    this->spillRenderPass(true);
    this->flushSharedImages();

    m_sdmaBarriers.recordCommands(m_cmd);
    m_initBarriers.recordCommands(m_cmd);
    m_execBarriers.recordCommands(m_cmd);

    m_cmd->endRecording();
    return std::exchange(m_cmd, nullptr);
  }


  void DxvkContext::flushCommandList() {
    ScopedCpuProfileZone();
    m_device->submitCommandList(
      this->endRecording(),
      VK_NULL_HANDLE,
      VK_NULL_HANDLE);

    this->beginRecording(
      m_device->createCommandList());

    // NV-DXVK start: early submit heuristics for memcpy work
    m_bytesCopiedInCurrentCmdlist = 0;
    // NV-DXVK end
  }


  void DxvkContext::beginQuery(const Rc<DxvkGpuQuery>& query) {
    ScopedCpuProfileZone();
    m_queryManager.enableQuery(m_cmd, query);
  }


  void DxvkContext::endQuery(const Rc<DxvkGpuQuery>& query) {
    ScopedCpuProfileZone();
    m_queryManager.disableQuery(m_cmd, query);
  }


  void DxvkContext::bindRenderTargets(
    const DxvkRenderTargets& targets) {
    ScopedCpuProfileZone();
    // Set up default render pass ops
    m_state.om.renderTargets = targets;

    this->resetRenderPassOps(
      m_state.om.renderTargets,
      m_state.om.renderPassOps);

    if (!m_state.om.framebufferInfo.hasTargets(targets)) {
      // Create a new framebuffer object next
      // time we start rendering something
      m_flags.set(DxvkContextFlag::GpDirtyFramebuffer);
    }
    else {
      // Don't redundantly spill the render pass if
      // the same render targets are bound again
      m_flags.clr(DxvkContextFlag::GpDirtyFramebuffer);
    }
  }


  void DxvkContext::bindDrawBuffers(
    const DxvkBufferSlice& argBuffer,
    const DxvkBufferSlice& cntBuffer) {
    ScopedCpuProfileZone();
    m_state.id.argBuffer = argBuffer;
    m_state.id.cntBuffer = cntBuffer;

    m_flags.set(DxvkContextFlag::DirtyDrawBuffer);
  }


  void DxvkContext::bindIndexBuffer(
    const DxvkBufferSlice& buffer,
    VkIndexType           indexType) {
    ScopedCpuProfileZone();
    if (!m_state.vi.indexBuffer.matchesBuffer(buffer))
      m_vbTracked.clr(MaxNumVertexBindings);

    m_state.vi.indexBuffer = buffer;
    m_state.vi.indexType = indexType;

    m_flags.set(DxvkContextFlag::GpDirtyIndexBuffer);
  }


  void DxvkContext::bindResourceBuffer(
    uint32_t              slot,
    const DxvkBufferSlice& buffer) {
    ScopedCpuProfileZone();
    bool needsUpdate = !m_rc[slot].bufferSlice.matchesBuffer(buffer);

    if (likely(needsUpdate))
      m_rcTracked.clr(slot);
    else
      needsUpdate = m_rc[slot].bufferSlice.length() != buffer.length();

    if (likely(needsUpdate)) {
      m_flags.set(
        DxvkContextFlag::CpDirtyResources,
        DxvkContextFlag::GpDirtyResources,
        DxvkContextFlag::RpDirtyResources);
    }
    else {
      m_flags.set(
        DxvkContextFlag::CpDirtyDescriptorBinding,
        DxvkContextFlag::GpDirtyDescriptorBinding,
        DxvkContextFlag::RpDirtyDescriptorBinding);
    }

    m_rc[slot].bufferSlice = buffer;
  }


  void DxvkContext::bindResourceView(
    uint32_t              slot,
    const Rc<DxvkImageView>& imageView,
    const Rc<DxvkBufferView>& bufferView) {
    ScopedCpuProfileZone();
    m_rc[slot].imageView = imageView;
    m_rc[slot].bufferView = bufferView;
    m_rc[slot].bufferSlice = bufferView != nullptr
      ? bufferView->slice()
      : DxvkBufferSlice();
    m_rcTracked.clr(slot);

    m_flags.set(
      DxvkContextFlag::CpDirtyResources,
      DxvkContextFlag::GpDirtyResources,
      DxvkContextFlag::RpDirtyResources);
  }


  void DxvkContext::bindResourceSampler(
    uint32_t              slot,
    const Rc<DxvkSampler>& sampler) {
    ScopedCpuProfileZone();
    m_rc[slot].sampler = sampler;
    m_rcTracked.clr(slot);

    m_flags.set(
      DxvkContextFlag::CpDirtyResources,
      DxvkContextFlag::GpDirtyResources,
      DxvkContextFlag::RpDirtyResources);
  }


  // NV-DXVK start: adding support for AS
  void DxvkContext::bindAccelerationStructure(
    uint32_t              slot,
    const Rc<DxvkAccelStructure> accelStructure) {
    ScopedCpuProfileZone();
    m_rc[slot].tlas = accelStructure->getAccelStructure();
    m_rcTracked.clr(slot);

    m_cmd->trackResource<DxvkAccess::Read>(accelStructure);

    m_flags.set(
      DxvkContextFlag::CpDirtyResources,
      DxvkContextFlag::GpDirtyResources,
      DxvkContextFlag::RpDirtyResources);
  }


  void DxvkContext::bindRaytracingPipelineShaders(
    const DxvkRaytracingPipelineShaders& shaders) {
    ScopedCpuProfileZone();

    m_state.rp.shaders = shaders;

    m_flags.set(
      DxvkContextFlag::RpDirtyPipeline,
      DxvkContextFlag::RpDirtyPipelineState,
      DxvkContextFlag::RpDirtyResources);
  }
  // NV-DXVK end


  void DxvkContext::bindShader(
    VkShaderStageFlagBits stage,
    const Rc<DxvkShader>& shader) {
    ScopedCpuProfileZone();
    Rc<DxvkShader>* shaderStage;

    switch (stage) {
    case VK_SHADER_STAGE_VERTEX_BIT:                  shaderStage = &m_state.gp.shaders.vs;  break;
    case VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT:    shaderStage = &m_state.gp.shaders.tcs; break;
    case VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT: shaderStage = &m_state.gp.shaders.tes; break;
    case VK_SHADER_STAGE_GEOMETRY_BIT:                shaderStage = &m_state.gp.shaders.gs;  break;
    case VK_SHADER_STAGE_FRAGMENT_BIT:                shaderStage = &m_state.gp.shaders.fs;  break;
    case VK_SHADER_STAGE_COMPUTE_BIT:                 shaderStage = &m_state.cp.shaders.cs;  break;
    default: return;
    }

    *shaderStage = shader;

    if (stage == VK_SHADER_STAGE_COMPUTE_BIT) {
      m_flags.set(
        DxvkContextFlag::CpDirtyPipeline,
        DxvkContextFlag::CpDirtyPipelineState,
        DxvkContextFlag::CpDirtyResources);
    }
    else {
      m_flags.set(
        DxvkContextFlag::GpDirtyPipeline,
        DxvkContextFlag::GpDirtyPipelineState,
        DxvkContextFlag::GpDirtyResources);
    }
  }


  void DxvkContext::bindVertexBuffer(
    uint32_t              binding,
    const DxvkBufferSlice& buffer,
    uint32_t              stride) {
    ScopedCpuProfileZone();
    if (!m_state.vi.vertexBuffers[binding].matchesBuffer(buffer))
      m_vbTracked.clr(binding);

    m_state.vi.vertexBuffers[binding] = buffer;
    m_flags.set(DxvkContextFlag::GpDirtyVertexBuffers);

    if (unlikely(!buffer.defined())
      && unlikely(!m_features.test(DxvkContextFeature::NullDescriptors)))
      stride = 0;

    if (unlikely(m_state.vi.vertexStrides[binding] != stride)) {
      m_state.vi.vertexStrides[binding] = stride;
      m_flags.set(DxvkContextFlag::GpDirtyPipelineState);
    }
  }


  void DxvkContext::bindXfbBuffer(
    uint32_t              binding,
    const DxvkBufferSlice& buffer,
    const DxvkBufferSlice& counter) {
    ScopedCpuProfileZone();
    if (!m_state.xfb.buffers[binding].matches(buffer)
      || !m_state.xfb.counters[binding].matches(counter)) {
      m_state.xfb.buffers[binding] = buffer;
      m_state.xfb.counters[binding] = counter;

      m_flags.set(DxvkContextFlag::GpDirtyXfbBuffers);
    }
  }


  void DxvkContext::blitImage(
    const Rc<DxvkImage>&        dstImage,
    const VkComponentMapping&   dstMapping,
    const Rc<DxvkImage>&        srcImage,
    const VkComponentMapping&   srcMapping,
    const VkImageBlit&          region,
          VkFilter              filter) {
    ScopedCpuProfileZone();
    this->spillRenderPass(true);
    this->prepareImage(m_execBarriers, dstImage, vk::makeSubresourceRange(region.dstSubresource));
    this->prepareImage(m_execBarriers, srcImage, vk::makeSubresourceRange(region.srcSubresource));

    auto mapping = util::resolveSrcComponentMapping(dstMapping, srcMapping);

    bool canUseFb = (srcImage->info().usage & VK_IMAGE_USAGE_SAMPLED_BIT)
      && (dstImage->info().usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)
      && ((dstImage->info().flags & VK_IMAGE_CREATE_2D_ARRAY_COMPATIBLE_BIT)
        || (dstImage->info().type != VK_IMAGE_TYPE_3D));

    bool useFb = dstImage->info().sampleCount != VK_SAMPLE_COUNT_1_BIT
      || !util::isIdentityMapping(mapping);

    if (!useFb) {
      this->blitImageHw(
        dstImage, srcImage,
        region, filter);
    }
    else if (canUseFb) {
      this->blitImageFb(
        dstImage, srcImage,
        region, mapping, filter);
    }
    else {
      Logger::err("DxvkContext: Unsupported blit operation");
    }
  }


  void DxvkContext::changeImageLayout(
    const Rc<DxvkImage>& image,
    VkImageLayout         layout) {
    ScopedCpuProfileZone();
    if (image->info().layout != layout) {
      this->spillRenderPass(true);

      VkImageSubresourceRange subresources = image->getAvailableSubresources();

      this->prepareImage(m_execBarriers, image, subresources);

      if (m_execBarriers.isImageDirty(image, subresources, DxvkAccess::Write))
        m_execBarriers.recordCommands(m_cmd);

      m_execBarriers.accessImage(image, subresources,
        image->info().layout,
        image->info().stages, 0,
        layout,
        image->info().stages,
        image->info().access);

      image->setLayout(layout);

      m_cmd->trackResource<DxvkAccess::Write>(image);
    }
  }


  void DxvkContext::clearBuffer(
    const Rc<DxvkBuffer>&       buffer,
          VkDeviceSize          offset,
          VkDeviceSize          length,
          uint32_t              value) {
    ScopedCpuProfileZone();
    this->spillRenderPass(true);
    
    length = align(length, sizeof(uint32_t));

    // NV-DXVK start: Extra safety against common clearBuffer misuse (Caught by validation layers too)
    // Note: Offset/length must be divisible by 4, and length must be non-zero when not VK_WHOLE_SIZE
    assert((offset % 4ULL) == 0ULL);
    assert((length % 4ULL) == 0ULL);
    assert(length == VK_WHOLE_SIZE || length != 0ULL);
    // NV-DXVK end

    auto slice = buffer->getSliceHandle(offset, length);

    if (m_execBarriers.isBufferDirty(slice, DxvkAccess::Write))
      m_execBarriers.recordCommands(m_cmd);

    m_cmd->cmdFillBuffer(
      slice.handle,
      slice.offset,
      slice.length,
      value);

    m_execBarriers.accessBuffer(slice,
      VK_PIPELINE_STAGE_TRANSFER_BIT,
      VK_ACCESS_TRANSFER_WRITE_BIT,
      buffer->info().stages,
      buffer->info().access);

    m_cmd->trackResource<DxvkAccess::Write>(buffer);
  }


  void DxvkContext::clearBufferView(
    const Rc<DxvkBufferView>&   bufferView,
          VkDeviceSize          offset,
          VkDeviceSize          length,
          VkClearColorValue     value) {
    ScopedCpuProfileZone();
    this->spillRenderPass(true);
    this->unbindComputePipeline();

    // The view range might have been invalidated, so
    // we need to make sure the handle is up to date
    bufferView->updateView();

    auto bufferSlice = bufferView->getSliceHandle();

    if (m_execBarriers.isBufferDirty(bufferSlice, DxvkAccess::Write))
      m_execBarriers.recordCommands(m_cmd);

    // Query pipeline objects to use for this clear operation
    DxvkMetaClearPipeline pipeInfo = m_common->metaClear().getClearBufferPipeline(
      imageFormatInfo(bufferView->info().format)->flags);

    // Create a descriptor set pointing to the view
    VkBufferView viewObject = bufferView->handle();

    // NV-DXVK start: use EXT_debug_utils
    VkDescriptorSet descriptorSet = allocateDescriptorSet(pipeInfo.dsetLayout, "DxvkContext::clearBufferView");
    // NV-DXVK end

    VkWriteDescriptorSet descriptorWrite;
    descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrite.pNext = nullptr;
    descriptorWrite.dstSet = descriptorSet;
    descriptorWrite.dstBinding = 0;
    descriptorWrite.dstArrayElement = 0;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER;
    descriptorWrite.pImageInfo = nullptr;
    descriptorWrite.pBufferInfo = nullptr;
    descriptorWrite.pTexelBufferView = &viewObject;
    m_cmd->updateDescriptorSets(1, &descriptorWrite);

    // Prepare shader arguments
    DxvkMetaClearArgs pushArgs = { };
    pushArgs.clearValue = value;
    pushArgs.offset = VkOffset3D{ int32_t(offset), 0, 0 };
    pushArgs.extent = VkExtent3D{ uint32_t(length), 1, 1 };

    VkExtent3D workgroups = util::computeBlockCount(
      pushArgs.extent, pipeInfo.workgroupSize);

    m_cmd->cmdBindPipeline(
      VK_PIPELINE_BIND_POINT_COMPUTE,
      pipeInfo.pipeline);
    m_cmd->cmdBindDescriptorSet(
      VK_PIPELINE_BIND_POINT_COMPUTE,
      pipeInfo.pipeLayout, descriptorSet,
      0, nullptr);
    m_cmd->cmdPushConstants(
      pipeInfo.pipeLayout,
      VK_SHADER_STAGE_COMPUTE_BIT,
      0, sizeof(pushArgs), &pushArgs);
    m_cmd->cmdDispatch(
      workgroups.width,
      workgroups.height,
      workgroups.depth);

    m_execBarriers.accessBuffer(bufferSlice,
      VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
      VK_ACCESS_SHADER_WRITE_BIT,
      bufferView->bufferInfo().stages,
      bufferView->bufferInfo().access);

    m_cmd->trackResource<DxvkAccess::None>(bufferView);
    m_cmd->trackResource<DxvkAccess::Write>(bufferView->buffer());
  }


  void DxvkContext::clearColorImage(
    const Rc<DxvkImage>&            image,
    const VkClearColorValue&        value,
    const VkImageSubresourceRange&  subresources) {
    ScopedCpuProfileZone();
    this->spillRenderPass(false);

    VkImageLayout imageLayoutClear = image->pickLayout(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    this->initializeImage(image, subresources,
      imageLayoutClear,
      VK_PIPELINE_STAGE_TRANSFER_BIT,
      VK_ACCESS_TRANSFER_WRITE_BIT);

    m_execAcquires.recordCommands(m_cmd);

    m_cmd->cmdClearColorImage(image->handle(),
      imageLayoutClear, &value, 1, &subresources);

    m_execBarriers.accessImage(image, subresources,
      imageLayoutClear,
      VK_PIPELINE_STAGE_TRANSFER_BIT,
      VK_ACCESS_TRANSFER_WRITE_BIT,
      image->info().layout,
      image->info().stages,
      image->info().access);

    m_cmd->trackResource<DxvkAccess::Write>(image);
  }


  void DxvkContext::clearDepthStencilImage(
    const Rc<DxvkImage>& image,
    const VkClearDepthStencilValue& value,
    const VkImageSubresourceRange&  subresources) {
    ScopedCpuProfileZone();
    this->spillRenderPass(false);
    
    m_execBarriers.recordCommands(m_cmd);

    VkImageLayout imageLayoutClear = image->pickLayout(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    this->initializeImage(image, subresources,
      imageLayoutClear,
      VK_PIPELINE_STAGE_TRANSFER_BIT,
      VK_ACCESS_TRANSFER_WRITE_BIT);

    m_execAcquires.recordCommands(m_cmd);

    m_cmd->cmdClearDepthStencilImage(image->handle(),
      imageLayoutClear, &value, 1, &subresources);

    m_execBarriers.accessImage(
      image, subresources,
      imageLayoutClear,
      VK_PIPELINE_STAGE_TRANSFER_BIT,
      VK_ACCESS_TRANSFER_WRITE_BIT,
      image->info().layout,
      image->info().stages,
      image->info().access);

    m_cmd->trackResource<DxvkAccess::Write>(image);
  }


  void DxvkContext::clearCompressedColorImage(
    const Rc<DxvkImage>&            image,
    const VkImageSubresourceRange&  subresources) {
    ScopedCpuProfileZone();
    this->spillRenderPass(false);

    VkImageLayout layout = image->pickLayout(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    this->initializeImage(image, subresources, layout,
      VK_PIPELINE_STAGE_TRANSFER_BIT,
      VK_ACCESS_TRANSFER_WRITE_BIT);

    m_execAcquires.recordCommands(m_cmd);

    auto formatInfo = image->formatInfo();

    for (auto aspects = formatInfo->aspectMask; aspects; ) {
      auto aspect = vk::getNextAspect(aspects);
      auto extent = image->mipLevelExtent(subresources.baseMipLevel);
      auto elementSize = formatInfo->elementSize;

      if (formatInfo->flags.test(DxvkFormatFlag::MultiPlane)) {
        auto plane = &formatInfo->planes[vk::getPlaneIndex(aspect)];
        extent.width  /= plane->blockSize.width;
        extent.height /= plane->blockSize.height;
        elementSize = plane->elementSize;
      }

      // Allocate enough staging buffer memory to fit one
      // single subresource, then dispatch multiple copies
      VkExtent3D blockCount = util::computeBlockCount(extent, formatInfo->blockSize);
      VkDeviceSize dataSize = util::flattenImageExtent(blockCount) * elementSize;
      
      auto zeroBuffer = createZeroBuffer(dataSize);
      auto zeroHandle = zeroBuffer->getSliceHandle();

      for (uint32_t level = 0; level < subresources.levelCount; level++) {
        VkOffset3D offset = VkOffset3D { 0, 0, 0 };
        VkExtent3D extent = image->mipLevelExtent(subresources.baseMipLevel + level);

        if (formatInfo->flags.test(DxvkFormatFlag::MultiPlane)) {
          auto plane = &formatInfo->planes[vk::getPlaneIndex(aspect)];
          extent.width  /= plane->blockSize.width;
          extent.height /= plane->blockSize.height;
        }

        for (uint32_t layer = 0; layer < subresources.layerCount; layer++) {
          VkBufferImageCopy region;
          region.bufferOffset       = zeroHandle.offset;
          region.bufferRowLength    = 0;
          region.bufferImageHeight  = 0;
          region.imageSubresource   = vk::makeSubresourceLayers(
            vk::pickSubresource(subresources, level, layer));
          region.imageSubresource.aspectMask = aspect;
          region.imageOffset        = offset;
          region.imageExtent        = extent;

          m_cmd->cmdCopyBufferToImage(DxvkCmdBuffer::ExecBuffer,
            zeroHandle.handle, image->handle(), layout, 1, &region);
        }
      }

      m_cmd->trackResource<DxvkAccess::Read>(zeroBuffer);
    }

    m_execBarriers.accessImage(
      image, subresources, layout,
      VK_PIPELINE_STAGE_TRANSFER_BIT,
      VK_ACCESS_TRANSFER_WRITE_BIT,
      image->info().layout,
      image->info().stages,
      image->info().access);

    m_cmd->trackResource<DxvkAccess::Write>(image);
  }


  void DxvkContext::clearRenderTarget(
    const Rc<DxvkImageView>& imageView,
    VkImageAspectFlags    clearAspects,
    VkClearValue          clearValue) {
    ScopedCpuProfileZone();
    // Make sure the color components are ordered correctly
    if (clearAspects & VK_IMAGE_ASPECT_COLOR_BIT) {
      clearValue.color = util::swizzleClearColor(clearValue.color,
        util::invertComponentMapping(imageView->info().swizzle));
    }

    // Check whether the render target view is an attachment
    // of the current framebuffer and is included entirely.
    // If not, we need to create a temporary framebuffer.
    int32_t attachmentIndex = -1;
    
    if (m_state.om.framebufferInfo.isFullSize(imageView))
      attachmentIndex = m_state.om.framebufferInfo.findAttachment(imageView);

    if (attachmentIndex < 0) {
      // Suspend works here because we'll end up with one of these scenarios:
      // 1) The render pass gets ended for good, in which case we emit barriers
      // 2) The clear gets folded into render pass ops, so the layout is correct
      // 3) The clear gets executed separately, in which case updateFramebuffer
      //    will indirectly emit barriers for the given render target.
      // If there is overlap, we need to explicitly transition affected attachments.
      this->spillRenderPass(true);
      this->prepareImage(m_execBarriers, imageView->image(), imageView->subresources(), false);
    } else if (!m_state.om.framebufferInfo.isWritable(attachmentIndex, clearAspects)) {
      // We cannot inline clears if the clear aspects are not writable
      this->spillRenderPass(true);
    }

    if (m_flags.test(DxvkContextFlag::GpRenderPassBound)) {
      uint32_t colorIndex = std::max(0, m_state.om.framebufferInfo.getColorAttachmentIndex(attachmentIndex));

      VkClearAttachment clearInfo;
      clearInfo.aspectMask      = clearAspects;
      clearInfo.colorAttachment = colorIndex;
      clearInfo.clearValue      = clearValue;

      VkClearRect clearRect;
      clearRect.rect.offset.x       = 0;
      clearRect.rect.offset.y       = 0;
      clearRect.rect.extent.width   = imageView->mipLevelExtent(0).width;
      clearRect.rect.extent.height  = imageView->mipLevelExtent(0).height;
      clearRect.baseArrayLayer      = 0;
      clearRect.layerCount          = imageView->info().numLayers;

      m_cmd->cmdClearAttachments(1, &clearInfo, 1, &clearRect);
    } else
      this->deferClear(imageView, clearAspects, clearValue);
  }


  void DxvkContext::clearImageView(
    const Rc<DxvkImageView>& imageView,
    VkOffset3D            offset,
    VkExtent3D            extent,
    VkImageAspectFlags    aspect,
    VkClearValue          value) {
    ScopedCpuProfileZone();
    const VkImageUsageFlags viewUsage = imageView->info().usage;

    if (aspect & VK_IMAGE_ASPECT_COLOR_BIT) {
      value.color = util::swizzleClearColor(value.color,
        util::invertComponentMapping(imageView->info().swizzle));
    }

    if (viewUsage & (VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT))
      this->clearImageViewFb(imageView, offset, extent, aspect, value);
    else if (viewUsage & VK_IMAGE_USAGE_STORAGE_BIT)
      this->clearImageViewCs(imageView, offset, extent, value);
  }


  void DxvkContext::copyBuffer(
    const Rc<DxvkBuffer>&       dstBuffer,
          VkDeviceSize          dstOffset,
    const Rc<DxvkBuffer>&       srcBuffer,
          VkDeviceSize          srcOffset,
          VkDeviceSize          numBytes) {
    // When overwriting small buffers, we can allocate a new slice in order to
    // avoid suspending the current render pass or inserting barriers. The source
    // buffer must be read-only since otherwise we cannot schedule the copy early.
    bool srcIsReadOnly = DxvkBarrierSet::getAccessTypes(srcBuffer->info().access) == DxvkAccess::Read;
    bool replaceBuffer = srcIsReadOnly && this->tryInvalidateDeviceLocalBuffer(dstBuffer, numBytes);

    auto srcSlice = srcBuffer->getSliceHandle(srcOffset, numBytes);
    auto dstSlice = dstBuffer->getSliceHandle(dstOffset, numBytes);

    if (!replaceBuffer) {
      this->spillRenderPass(true);

      if (m_execBarriers.isBufferDirty(srcSlice, DxvkAccess::Read)
       || m_execBarriers.isBufferDirty(dstSlice, DxvkAccess::Write))
        m_execBarriers.recordCommands(m_cmd);
    }

    // NV-DXVK start: Extra safety against common copyBuffer misuse (Caught by validation layers too)
    // Note: Copy buffer region size must not be zero
    assert(dstSlice.length != 0ULL);
    // NV-DXVK end

    DxvkCmdBuffer cmdBuffer = replaceBuffer
      ? DxvkCmdBuffer::InitBuffer
      : DxvkCmdBuffer::ExecBuffer;

    VkBufferCopy bufferRegion;
    bufferRegion.srcOffset = srcSlice.offset;
    bufferRegion.dstOffset = dstSlice.offset;
    bufferRegion.size = dstSlice.length;

    m_cmd->cmdCopyBuffer(cmdBuffer,
      srcSlice.handle, dstSlice.handle, 1, &bufferRegion);

    auto& barriers = replaceBuffer
      ? m_initBarriers
      : m_execBarriers;

    barriers.accessBuffer(srcSlice,
      VK_PIPELINE_STAGE_TRANSFER_BIT,
      VK_ACCESS_TRANSFER_READ_BIT,
      srcBuffer->info().stages,
      srcBuffer->info().access);

    barriers.accessBuffer(dstSlice,
      VK_PIPELINE_STAGE_TRANSFER_BIT,
      VK_ACCESS_TRANSFER_WRITE_BIT,
      dstBuffer->info().stages,
      dstBuffer->info().access);

    m_cmd->trackResource<DxvkAccess::Write>(dstBuffer);
    m_cmd->trackResource<DxvkAccess::Read>(srcBuffer);

    // NV-DXVK start: early submit heuristics for memcpy work
    recordGPUMemCopy(numBytes);
    // NV-DXVK end
  }


  void DxvkContext::copyBufferRegion(
    const Rc<DxvkBuffer>& dstBuffer,
    VkDeviceSize          dstOffset,
    VkDeviceSize          srcOffset,
    VkDeviceSize          numBytes) {
    ScopedCpuProfileZone();
    VkDeviceSize loOvl = std::max(dstOffset, srcOffset);
    VkDeviceSize hiOvl = std::min(dstOffset, srcOffset) + numBytes;

    if (hiOvl > loOvl) {
      DxvkBufferCreateInfo bufInfo;
      bufInfo.size = numBytes;
      bufInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT
        | VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
      bufInfo.stages = VK_PIPELINE_STAGE_TRANSFER_BIT;
      bufInfo.access = VK_ACCESS_TRANSFER_WRITE_BIT
        | VK_ACCESS_TRANSFER_READ_BIT;

      auto tmpBuffer = m_device->createBuffer(
        bufInfo, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, DxvkMemoryStats::Category::AppBuffer, "copyBufferRegion");

      VkDeviceSize tmpOffset = 0;

      this->copyBuffer(tmpBuffer, tmpOffset, dstBuffer, srcOffset, numBytes);
      this->copyBuffer(dstBuffer, dstOffset, tmpBuffer, tmpOffset, numBytes);
    }
    else {
      this->copyBuffer(dstBuffer, dstOffset, dstBuffer, srcOffset, numBytes);
    }

    // NV-DXVK start: early submit heuristics for memcpy work    
    recordGPUMemCopy(numBytes);
    // NV-DXVK end
  }


  void DxvkContext::copyBufferToImage(
    const Rc<DxvkImage>&        dstImage,
          VkImageSubresourceLayers dstSubresource,
          VkOffset3D            dstOffset,
          VkExtent3D            dstExtent,
    const Rc<DxvkBuffer>&       srcBuffer,
          VkDeviceSize          srcOffset,
          VkDeviceSize          rowAlignment,
          VkDeviceSize          sliceAlignment) {
    ScopedCpuProfileZone();
    this->spillRenderPass(true);
    this->prepareImage(m_execBarriers, dstImage, vk::makeSubresourceRange(dstSubresource));

    auto srcSlice = srcBuffer->getSliceHandle(srcOffset, 0);

    // We may copy to only one aspect at a time, but pipeline
    // barriers need to have all available aspect bits set
    auto dstFormatInfo = dstImage->formatInfo();

    auto dstSubresourceRange = vk::makeSubresourceRange(dstSubresource);
    dstSubresourceRange.aspectMask = dstFormatInfo->aspectMask;

    if (m_execBarriers.isImageDirty(dstImage, dstSubresourceRange, DxvkAccess::Write)
      || m_execBarriers.isBufferDirty(srcSlice, DxvkAccess::Read))
      m_execBarriers.recordCommands(m_cmd);

    // Initialize the image if the entire subresource is covered
    VkImageLayout dstImageLayoutInitial = dstImage->info().layout;
    VkImageLayout dstImageLayoutTransfer = dstImage->pickLayout(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    if (dstImage->isFullSubresource(dstSubresource, dstExtent))
      dstImageLayoutInitial = VK_IMAGE_LAYOUT_UNDEFINED;

    if (dstImageLayoutTransfer != dstImageLayoutInitial) {
      m_execAcquires.accessImage(
        dstImage, dstSubresourceRange,
        dstImageLayoutInitial,
        VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
        dstImageLayoutTransfer,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_ACCESS_TRANSFER_WRITE_BIT);
    }

    m_execAcquires.recordCommands(m_cmd);

    this->copyImageBufferData<true>(DxvkCmdBuffer::ExecBuffer, dstImage, dstSubresource,
      dstOffset, dstExtent, dstImageLayoutTransfer, srcSlice, rowAlignment, sliceAlignment);

    m_execBarriers.accessImage(
      dstImage, dstSubresourceRange,
      dstImageLayoutTransfer,
      VK_PIPELINE_STAGE_TRANSFER_BIT,
      VK_ACCESS_TRANSFER_WRITE_BIT,
      dstImage->info().layout,
      dstImage->info().stages,
      dstImage->info().access);

    m_execBarriers.accessBuffer(srcSlice,
      VK_PIPELINE_STAGE_TRANSFER_BIT,
      VK_ACCESS_TRANSFER_READ_BIT,
      srcBuffer->info().stages,
      srcBuffer->info().access);

    m_cmd->trackResource<DxvkAccess::Write>(dstImage);
    m_cmd->trackResource<DxvkAccess::Read>(srcBuffer);

    // NV-DXVK start: early submit heuristics for memcpy work
    recordGPUMemCopy(dstExtent.width * dstExtent.height);
    // NV-DXVK end
  }


  void DxvkContext::copyImage(
    const Rc<DxvkImage>&        dstImage,
          VkImageSubresourceLayers dstSubresource,
          VkOffset3D            dstOffset,
    const Rc<DxvkImage>&        srcImage,
          VkImageSubresourceLayers srcSubresource,
          VkOffset3D            srcOffset,
          VkExtent3D            extent) {
    ScopedCpuProfileZone();
    this->spillRenderPass(true);

    if (this->copyImageClear(dstImage, dstSubresource, dstOffset, extent, srcImage, srcSubresource))
      return;

    this->prepareImage(m_execBarriers, dstImage, vk::makeSubresourceRange(dstSubresource));
    this->prepareImage(m_execBarriers, srcImage, vk::makeSubresourceRange(srcSubresource));

    bool useFb = dstSubresource.aspectMask != srcSubresource.aspectMask;

    if (m_device->perfHints().preferFbDepthStencilCopy) {
      useFb |= (dstSubresource.aspectMask == (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT))
        && (dstImage->info().usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)
        && (srcImage->info().usage & VK_IMAGE_USAGE_SAMPLED_BIT);
    }

    if (!useFb) {
      this->copyImageHw(
        dstImage, dstSubresource, dstOffset,
        srcImage, srcSubresource, srcOffset,
        extent);
    }
    else {
      this->copyImageFb(
        dstImage, dstSubresource, dstOffset,
        srcImage, srcSubresource, srcOffset,
        extent);
    }

    // NV-DXVK start: early submit heuristics for memcpy work
    recordGPUMemCopy(dstImage->formatInfo()->elementSize * util::flattenImageExtent(extent));
    // NV-DXVK end
  }


  void DxvkContext::copyImageRegion(
    const Rc<DxvkImage>& dstImage,
    VkImageSubresourceLayers dstSubresource,
    VkOffset3D            dstOffset,
    VkOffset3D            srcOffset,
    VkExtent3D            extent) {
    ScopedCpuProfileZone();
    VkOffset3D loOvl = {
      std::max(dstOffset.x, srcOffset.x),
      std::max(dstOffset.y, srcOffset.y),
      std::max(dstOffset.z, srcOffset.z) };

    VkOffset3D hiOvl = {
      std::min(dstOffset.x, srcOffset.x) + int32_t(extent.width),
      std::min(dstOffset.y, srcOffset.y) + int32_t(extent.height),
      std::min(dstOffset.z, srcOffset.z) + int32_t(extent.depth) };

    bool overlap = hiOvl.x > loOvl.x
      && hiOvl.y > loOvl.y
      && hiOvl.z > loOvl.z;

    if (overlap) {
      DxvkImageCreateInfo imgInfo;
      imgInfo.type = dstImage->info().type;
      imgInfo.format = dstImage->info().format;
      imgInfo.flags = 0;
      imgInfo.sampleCount = dstImage->info().sampleCount;
      imgInfo.extent = extent;
      imgInfo.numLayers = dstSubresource.layerCount;
      imgInfo.mipLevels = 1;
      imgInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT
        | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
      imgInfo.stages = VK_PIPELINE_STAGE_TRANSFER_BIT;
      imgInfo.access = VK_ACCESS_TRANSFER_WRITE_BIT
        | VK_ACCESS_TRANSFER_READ_BIT;
      imgInfo.tiling = dstImage->info().tiling;
      imgInfo.layout = VK_IMAGE_LAYOUT_GENERAL;

      // NV-DXVK start: add debug names to VkImage objects
      auto tmpImage = m_device->createImage(
        imgInfo, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, DxvkMemoryStats::Category::AppTexture, "copyImageRegion tmp");
      // NV-DXVK end

      VkImageSubresourceLayers tmpSubresource;
      tmpSubresource.aspectMask = dstSubresource.aspectMask;
      tmpSubresource.mipLevel = 0;
      tmpSubresource.baseArrayLayer = 0;
      tmpSubresource.layerCount = dstSubresource.layerCount;

      VkOffset3D tmpOffset = { 0, 0, 0 };

      this->copyImage(
        tmpImage, tmpSubresource, tmpOffset,
        dstImage, dstSubresource, srcOffset,
        extent);

      this->copyImage(
        dstImage, dstSubresource, dstOffset,
        tmpImage, tmpSubresource, tmpOffset,
        extent);
    }
    else {
      this->copyImage(
        dstImage, dstSubresource, dstOffset,
        dstImage, dstSubresource, srcOffset,
        extent);
    }

    // NV-DXVK start: early submit heuristics for memcpy work
    recordGPUMemCopy(dstImage->formatInfo()->elementSize * util::flattenImageExtent(extent));
    // NV-DXVK end
  }


  void DxvkContext::copyImageToBuffer(
    const Rc<DxvkBuffer>&       dstBuffer,
          VkDeviceSize          dstOffset,
          VkDeviceSize          rowAlignment,
          VkDeviceSize          sliceAlignment,
    const Rc<DxvkImage>&        srcImage,
          VkImageSubresourceLayers srcSubresource,
          VkOffset3D            srcOffset,
          VkExtent3D            srcExtent) {
    ScopedCpuProfileZone();
    this->spillRenderPass(true);
    this->prepareImage(m_execBarriers, srcImage, vk::makeSubresourceRange(srcSubresource));

    auto dstSlice = dstBuffer->getSliceHandle(dstOffset, 0);

    // We may copy to only one aspect of a depth-stencil image,
    // but pipeline barriers need to have all aspect bits set
    auto srcFormatInfo = srcImage->formatInfo();

    auto srcSubresourceRange = vk::makeSubresourceRange(srcSubresource);
    srcSubresourceRange.aspectMask = srcFormatInfo->aspectMask;

    if (m_execBarriers.isImageDirty(srcImage, srcSubresourceRange, DxvkAccess::Write)
      || m_execBarriers.isBufferDirty(dstSlice, DxvkAccess::Write))
      m_execBarriers.recordCommands(m_cmd);

    // Select a suitable image layout for the transfer op
    VkImageLayout srcImageLayoutTransfer = srcImage->pickLayout(VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

    m_execAcquires.accessImage(
      srcImage, srcSubresourceRange,
      srcImage->info().layout,
      VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
      srcImageLayoutTransfer,
      VK_PIPELINE_STAGE_TRANSFER_BIT,
      VK_ACCESS_TRANSFER_READ_BIT);

    m_execAcquires.recordCommands(m_cmd);
    
    this->copyImageBufferData<false>(DxvkCmdBuffer::ExecBuffer, srcImage, srcSubresource,
      srcOffset, srcExtent, srcImageLayoutTransfer, dstSlice, rowAlignment, sliceAlignment);
    
    m_execBarriers.accessImage(
      srcImage, srcSubresourceRange,
      srcImageLayoutTransfer,
      VK_PIPELINE_STAGE_TRANSFER_BIT,
      VK_ACCESS_TRANSFER_READ_BIT,
      srcImage->info().layout,
      srcImage->info().stages,
      srcImage->info().access);

    m_execBarriers.accessBuffer(dstSlice,
      VK_PIPELINE_STAGE_TRANSFER_BIT,
      VK_ACCESS_TRANSFER_WRITE_BIT,
      dstBuffer->info().stages,
      dstBuffer->info().access);

    m_cmd->trackResource<DxvkAccess::Write>(dstBuffer);
    m_cmd->trackResource<DxvkAccess::Read>(srcImage);

    // NV-DXVK start: early submit heuristics for memcpy work
    recordGPUMemCopy(srcImage->formatInfo()->elementSize * util::flattenImageExtent(srcExtent));
    // NV-DXVK end
  }


  void DxvkContext::copyDepthStencilImageToPackedBuffer(
    const Rc<DxvkBuffer>&       dstBuffer,
          VkDeviceSize          dstBufferOffset,
          VkOffset2D            dstOffset,
          VkExtent2D            dstExtent,
    const Rc<DxvkImage>&        srcImage,
          VkImageSubresourceLayers srcSubresource,
          VkOffset2D            srcOffset,
          VkExtent2D            srcExtent,
          VkFormat              format) {
    ScopedCpuProfileZone();
    this->spillRenderPass(true);
    this->prepareImage(m_execBarriers, srcImage, vk::makeSubresourceRange(srcSubresource));

    this->unbindComputePipeline();

    // Retrieve compute pipeline for the given format
    auto pipeInfo = m_common->metaPack().getPackPipeline(format);

    if (!pipeInfo.pipeHandle)
      return;

    // Create one depth view and one stencil view
    DxvkImageViewCreateInfo dViewInfo;
    dViewInfo.type = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    dViewInfo.format = srcImage->info().format;
    dViewInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
    dViewInfo.aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
    dViewInfo.minLevel = srcSubresource.mipLevel;
    dViewInfo.numLevels = 1;
    dViewInfo.minLayer = srcSubresource.baseArrayLayer;
    dViewInfo.numLayers = srcSubresource.layerCount;

    DxvkImageViewCreateInfo sViewInfo = dViewInfo;
    sViewInfo.aspect = VK_IMAGE_ASPECT_STENCIL_BIT;

    Rc<DxvkImageView> dView = m_device->createImageView(srcImage, dViewInfo);
    Rc<DxvkImageView> sView = m_device->createImageView(srcImage, sViewInfo);

    // Create a descriptor set for the pack operation
    VkImageLayout layout = srcImage->pickLayout(VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);

    DxvkMetaPackDescriptors descriptors;
    descriptors.dstBuffer  = dstBuffer->getDescriptor(dstBufferOffset, VK_WHOLE_SIZE).buffer;
    descriptors.srcDepth   = dView->getDescriptor(VK_IMAGE_VIEW_TYPE_2D_ARRAY, layout).image;
    descriptors.srcStencil = sView->getDescriptor(VK_IMAGE_VIEW_TYPE_2D_ARRAY, layout).image;

    // NV-DXVK start: use EXT_debug_utils
    VkDescriptorSet dset = allocateDescriptorSet(pipeInfo.dsetLayout, "DxvkContext::copyDepthStencilImageToPackedBuffer");
    // NV-DXVK end
    m_cmd->updateDescriptorSetWithTemplate(dset, pipeInfo.dsetTemplate, &descriptors);

    // Since this is a meta operation, the image may be
    // in a different layout and we have to transition it
    auto subresourceRange = vk::makeSubresourceRange(srcSubresource);

    if (m_execBarriers.isImageDirty(srcImage, subresourceRange, DxvkAccess::Write))
      m_execBarriers.recordCommands(m_cmd);

    if (srcImage->info().layout != layout) {
      m_execAcquires.accessImage(
        srcImage, subresourceRange,
        srcImage->info().layout,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
        layout,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_ACCESS_SHADER_READ_BIT);

      m_execAcquires.recordCommands(m_cmd);
    }

    // Execute the actual pack operation
    DxvkMetaPackArgs args;
    args.srcOffset = srcOffset;
    args.srcExtent = srcExtent;
    args.dstOffset = dstOffset;
    args.dstExtent = dstExtent;

    m_cmd->cmdBindPipeline(
      VK_PIPELINE_BIND_POINT_COMPUTE,
      pipeInfo.pipeHandle);

    m_cmd->cmdBindDescriptorSet(
      VK_PIPELINE_BIND_POINT_COMPUTE,
      pipeInfo.pipeLayout, dset,
      0, nullptr);

    m_cmd->cmdPushConstants(
      pipeInfo.pipeLayout,
      VK_SHADER_STAGE_COMPUTE_BIT,
      0, sizeof(args), &args);

    m_cmd->cmdDispatch(
      (srcExtent.width + 7) / 8,
      (srcExtent.height + 7) / 8,
      srcSubresource.layerCount);

    m_execBarriers.accessImage(
      srcImage, subresourceRange, layout,
      VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
      VK_ACCESS_SHADER_READ_BIT,
      srcImage->info().layout,
      srcImage->info().stages,
      srcImage->info().access);

    m_execBarriers.accessBuffer(
      dstBuffer->getSliceHandle(),
      VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
      VK_ACCESS_SHADER_WRITE_BIT,
      dstBuffer->info().stages,
      dstBuffer->info().access);

    m_cmd->trackResource<DxvkAccess::None>(dView);
    m_cmd->trackResource<DxvkAccess::None>(sView);

    m_cmd->trackResource<DxvkAccess::Write>(dstBuffer);
    m_cmd->trackResource<DxvkAccess::Read>(srcImage);
  }
  
  
  void DxvkContext::copyPackedBufferImage(
    const Rc<DxvkBuffer>&       dstBuffer,
          VkDeviceSize          dstBufferOffset,
          VkOffset3D            dstOffset,
          VkExtent3D            dstSize,
    const Rc<DxvkBuffer>&       srcBuffer,
          VkDeviceSize          srcBufferOffset,
          VkOffset3D            srcOffset,
          VkExtent3D            srcSize,
          VkExtent3D            extent,
          VkDeviceSize          elementSize) {
    ScopedCpuProfileZone();
    this->spillRenderPass(true);
    this->unbindComputePipeline();

    auto dstBufferSlice = dstBuffer->getSliceHandle(dstBufferOffset, elementSize * util::flattenImageExtent(dstSize));
    auto srcBufferSlice = srcBuffer->getSliceHandle(srcBufferOffset, elementSize * util::flattenImageExtent(srcSize));

    if (m_execBarriers.isBufferDirty(dstBufferSlice, DxvkAccess::Write)
     || m_execBarriers.isBufferDirty(srcBufferSlice, DxvkAccess::Read))
      m_execBarriers.recordCommands(m_cmd);

    // We'll use texel buffer views with an appropriately
    // sized integer format to perform the copy
    VkFormat format = VK_FORMAT_UNDEFINED;

    switch (elementSize) {
      case  1: format = VK_FORMAT_R8_UINT; break;
      case  2: format = VK_FORMAT_R16_UINT; break;
      case  4: format = VK_FORMAT_R32_UINT; break;
      case  8: format = VK_FORMAT_R32G32_UINT; break;
      case 12: format = VK_FORMAT_R32G32B32_UINT; break;
      case 16: format = VK_FORMAT_R32G32B32A32_UINT; break;
    }

    if (!format) {
      Logger::err(str::format("DxvkContext: copyPackedBufferImage: Unsupported element size ", elementSize));
      return;
    }

    DxvkBufferViewCreateInfo viewInfo;
    viewInfo.format = format;
    viewInfo.rangeOffset = dstBufferOffset;
    viewInfo.rangeLength = dstBufferSlice.length;
    Rc<DxvkBufferView> dstView = m_device->createBufferView(dstBuffer, viewInfo);

    viewInfo.rangeOffset = srcBufferOffset;
    viewInfo.rangeLength = srcBufferSlice.length;
    Rc<DxvkBufferView> srcView;

    if (srcBuffer == dstBuffer
     && srcBufferSlice.offset < dstBufferSlice.offset + dstBufferSlice.length
     && srcBufferSlice.offset + srcBufferSlice.length > dstBufferSlice.offset) {
      // Create temporary copy in case of overlapping regions
      DxvkBufferCreateInfo bufferInfo;
      bufferInfo.size   = srcBufferSlice.length;
      bufferInfo.usage  = VK_BUFFER_USAGE_TRANSFER_DST_BIT
                        | VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT;
      bufferInfo.stages = VK_PIPELINE_STAGE_TRANSFER_BIT
                        | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
      bufferInfo.access = VK_ACCESS_TRANSFER_WRITE_BIT
                        | VK_ACCESS_SHADER_READ_BIT;
      Rc<DxvkBuffer> tmpBuffer = m_device->createBuffer(
        bufferInfo, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, DxvkMemoryStats::Category::AppBuffer, "copyPackedBufferImage");

      auto tmpBufferSlice = tmpBuffer->getSliceHandle();

      VkBufferCopy copyRegion;
      copyRegion.srcOffset = srcBufferSlice.offset;
      copyRegion.dstOffset = tmpBufferSlice.offset;
      copyRegion.size = tmpBufferSlice.length;

      m_cmd->cmdCopyBuffer(DxvkCmdBuffer::ExecBuffer,
        srcBufferSlice.handle, tmpBufferSlice.handle,
        1, &copyRegion);

      emitMemoryBarrier(0,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_ACCESS_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_ACCESS_SHADER_READ_BIT);

      viewInfo.rangeOffset = 0;
      srcView = m_device->createBufferView(tmpBuffer, viewInfo);

      m_cmd->trackResource<DxvkAccess::Write>(tmpBuffer);
    } else {
      srcView = m_device->createBufferView(srcBuffer, viewInfo);
    }

    auto pipeInfo = m_common->metaCopy().getCopyBufferImagePipeline();
    VkDescriptorSet descriptorSet = allocateDescriptorSet(pipeInfo.dsetLayout);

    std::array<VkWriteDescriptorSet, 2> descriptorWrites;

    std::array<std::pair<VkDescriptorType, VkBufferView>, 2> descriptorInfos = {{
      { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, dstView->handle() },
      { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, srcView->handle() },
    }};

    for (uint32_t i = 0; i < descriptorWrites.size(); i++) {
      auto write = &descriptorWrites[i];
      auto info = &descriptorInfos[i];

      write->sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      write->pNext = nullptr;
      write->dstSet = descriptorSet;
      write->dstBinding = i;
      write->dstArrayElement = 0;
      write->descriptorCount = 1;
      write->descriptorType = info->first;
      write->pImageInfo = nullptr;
      write->pBufferInfo = nullptr;
      write->pTexelBufferView = &info->second;
    }

    m_cmd->updateDescriptorSets(descriptorWrites.size(), descriptorWrites.data());

    DxvkCopyBufferImageArgs args = { };
    args.dstOffset = dstOffset;
    args.srcOffset = srcOffset;
    args.extent = extent;
    args.dstSize = { dstSize.width, dstSize.height };
    args.srcSize = { srcSize.width, srcSize.height };

    m_cmd->cmdBindPipeline(
      VK_PIPELINE_BIND_POINT_COMPUTE,
      pipeInfo.pipeHandle);
    
    m_cmd->cmdBindDescriptorSet(
      VK_PIPELINE_BIND_POINT_COMPUTE,
      pipeInfo.pipeLayout, descriptorSet,
      0, nullptr);
    
    m_cmd->cmdPushConstants(
      pipeInfo.pipeLayout,
      VK_SHADER_STAGE_COMPUTE_BIT,
      0, sizeof(args), &args);
    
    m_cmd->cmdDispatch(
      (extent.width  + 7) / 8,
      (extent.height + 7) / 8,
      extent.depth);
    
    m_execBarriers.accessBuffer(
      dstView->getSliceHandle(),
      VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
      VK_ACCESS_SHADER_WRITE_BIT,
      dstBuffer->info().stages,
      dstBuffer->info().access);

    m_execBarriers.accessBuffer(
      srcView->getSliceHandle(),
      VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
      VK_ACCESS_SHADER_READ_BIT,
      srcBuffer->info().stages,
      srcBuffer->info().access);

    // Track all involved resources
    m_cmd->trackResource<DxvkAccess::Write>(dstBuffer);
    m_cmd->trackResource<DxvkAccess::Read>(srcBuffer);

    m_cmd->trackResource<DxvkAccess::None>(dstView);
    m_cmd->trackResource<DxvkAccess::None>(srcView);
  }


  void DxvkContext::copyPackedBufferToDepthStencilImage(
    const Rc<DxvkImage>&        dstImage,
          VkImageSubresourceLayers dstSubresource,
          VkOffset2D            dstOffset,
          VkExtent2D            dstExtent,
    const Rc<DxvkBuffer>&       srcBuffer,
          VkDeviceSize          srcBufferOffset,
          VkOffset2D            srcOffset,
          VkExtent2D            srcExtent,
          VkFormat              format) {
    ScopedCpuProfileZone();
    this->spillRenderPass(true);
    this->prepareImage(m_execBarriers, dstImage, vk::makeSubresourceRange(dstSubresource));

    this->unbindComputePipeline();

    if (m_execBarriers.isBufferDirty(srcBuffer->getSliceHandle(), DxvkAccess::Read)
     || m_execBarriers.isImageDirty(dstImage, vk::makeSubresourceRange(dstSubresource), DxvkAccess::Write))
      m_execBarriers.recordCommands(m_cmd);

    // Retrieve compute pipeline for the given format
    auto pipeInfo = m_common->metaPack().getUnpackPipeline(dstImage->info().format, format);

    if (!pipeInfo.pipeHandle) {
      Logger::err(str::format(
        "DxvkContext: copyPackedBufferToDepthStencilImage: Unhandled formats"
        "\n  dstFormat = ", dstImage->info().format,
        "\n  srcFormat = ", format));
      return;
    }

    // Pick depth and stencil data formats
    VkFormat dataFormatD = VK_FORMAT_UNDEFINED;
    VkFormat dataFormatS = VK_FORMAT_UNDEFINED;

    const std::array<std::tuple<VkFormat, VkFormat, VkFormat>, 2> formats = { {
      { VK_FORMAT_D24_UNORM_S8_UINT,  VK_FORMAT_R32_UINT,   VK_FORMAT_R8_UINT },
      { VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_R32_SFLOAT, VK_FORMAT_R8_UINT },
    } };

    for (const auto& e : formats) {
      if (std::get<0>(e) == dstImage->info().format) {
        dataFormatD = std::get<1>(e);
        dataFormatS = std::get<2>(e);
      }
    }

    // Create temporary buffer for depth/stencil data
    VkDeviceSize pixelCount = dstExtent.width * dstExtent.height * dstSubresource.layerCount;
    VkDeviceSize dataSizeD = align(pixelCount * imageFormatInfo(dataFormatD)->elementSize, 256);
    VkDeviceSize dataSizeS = align(pixelCount * imageFormatInfo(dataFormatS)->elementSize, 256);

    DxvkBufferCreateInfo tmpBufferInfo;
    tmpBufferInfo.size = dataSizeD + dataSizeS;
    tmpBufferInfo.usage = VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT
      | VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    tmpBufferInfo.stages = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT
      | VK_PIPELINE_STAGE_TRANSFER_BIT;
    tmpBufferInfo.access = VK_ACCESS_SHADER_WRITE_BIT
      | VK_ACCESS_TRANSFER_READ_BIT;

    auto tmpBuffer = m_device->createBuffer(tmpBufferInfo, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, DxvkMemoryStats::Category::AppBuffer, "copyPackedBufferToDepthStencilImage");

    // Create formatted buffer views
    DxvkBufferViewCreateInfo tmpViewInfoD;
    tmpViewInfoD.format = dataFormatD;
    tmpViewInfoD.rangeOffset = 0;
    tmpViewInfoD.rangeLength = dataSizeD;

    DxvkBufferViewCreateInfo tmpViewInfoS;
    tmpViewInfoS.format = dataFormatS;
    tmpViewInfoS.rangeOffset = dataSizeD;
    tmpViewInfoS.rangeLength = dataSizeS;

    auto tmpBufferViewD = m_device->createBufferView(tmpBuffer, tmpViewInfoD);
    auto tmpBufferViewS = m_device->createBufferView(tmpBuffer, tmpViewInfoS);

    // Create descriptor set for the unpack operation
    DxvkMetaUnpackDescriptors descriptors;
    descriptors.dstDepth = tmpBufferViewD->handle();
    descriptors.dstStencil = tmpBufferViewS->handle();
    descriptors.srcBuffer  = srcBuffer->getDescriptor(srcBufferOffset, VK_WHOLE_SIZE).buffer;

    // NV-DXVK start: use EXT_debug_utils
    VkDescriptorSet dset = allocateDescriptorSet(pipeInfo.dsetLayout, "DxvkContext::copyPackedBufferToDepthStencilImage");
    // NV-DXVK end
    m_cmd->updateDescriptorSetWithTemplate(dset, pipeInfo.dsetTemplate, &descriptors);

    // Unpack the source buffer to temporary buffers
    DxvkMetaPackArgs args;
    args.srcOffset = srcOffset;
    args.srcExtent = srcExtent;
    args.dstOffset = VkOffset2D { 0, 0 };
    args.dstExtent = dstExtent;

    m_cmd->cmdBindPipeline(
      VK_PIPELINE_BIND_POINT_COMPUTE,
      pipeInfo.pipeHandle);

    m_cmd->cmdBindDescriptorSet(
      VK_PIPELINE_BIND_POINT_COMPUTE,
      pipeInfo.pipeLayout, dset,
      0, nullptr);

    m_cmd->cmdPushConstants(
      pipeInfo.pipeLayout,
      VK_SHADER_STAGE_COMPUTE_BIT,
      0, sizeof(args), &args);

    m_cmd->cmdDispatch(
      (dstExtent.width + 63) / 64,
      dstExtent.height,
      dstSubresource.layerCount);

    m_execBarriers.accessBuffer(
      tmpBuffer->getSliceHandle(),
      VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
      VK_ACCESS_SHADER_WRITE_BIT,
      VK_PIPELINE_STAGE_TRANSFER_BIT,
      VK_ACCESS_TRANSFER_READ_BIT);

    m_execBarriers.accessBuffer(
      srcBuffer->getSliceHandle(),
      VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
      VK_ACCESS_SHADER_READ_BIT,
      srcBuffer->info().stages,
      srcBuffer->info().access);

    // Prepare image for the data transfer operation
    VkOffset3D dstOffset3D = { dstOffset.x,     dstOffset.y,      0 };
    VkExtent3D dstExtent3D = { dstExtent.width, dstExtent.height, 1 };

    VkImageLayout initialImageLayout = dstImage->info().layout;

    if (dstImage->isFullSubresource(dstSubresource, dstExtent3D))
      initialImageLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    m_execBarriers.accessImage(
      dstImage, vk::makeSubresourceRange(dstSubresource),
      initialImageLayout,
      dstImage->info().stages,
      dstImage->info().access,
      dstImage->pickLayout(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL),
      VK_PIPELINE_STAGE_TRANSFER_BIT,
      VK_ACCESS_TRANSFER_WRITE_BIT);

    m_execBarriers.recordCommands(m_cmd);

    // Copy temporary buffer data to depth-stencil image
    VkImageSubresourceLayers dstSubresourceD = dstSubresource;
    dstSubresourceD.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;

    VkImageSubresourceLayers dstSubresourceS = dstSubresource;
    dstSubresourceS.aspectMask = VK_IMAGE_ASPECT_STENCIL_BIT;

    std::array<VkBufferImageCopy, 2> copyRegions = { {
      { tmpBufferViewD->info().rangeOffset, 0, 0, dstSubresourceD, dstOffset3D, dstExtent3D },
      { tmpBufferViewS->info().rangeOffset, 0, 0, dstSubresourceS, dstOffset3D, dstExtent3D },
    } };

    m_cmd->cmdCopyBufferToImage(DxvkCmdBuffer::ExecBuffer,
      tmpBuffer->getSliceHandle().handle,
      dstImage->handle(),
      dstImage->pickLayout(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL),
      copyRegions.size(),
      copyRegions.data());

    m_execBarriers.accessImage(
      dstImage, vk::makeSubresourceRange(dstSubresource),
      dstImage->pickLayout(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL),
      VK_PIPELINE_STAGE_TRANSFER_BIT,
      VK_ACCESS_TRANSFER_WRITE_BIT,
      dstImage->info().layout,
      dstImage->info().stages,
      dstImage->info().access);

    // Track all involved resources
    m_cmd->trackResource<DxvkAccess::Write>(dstImage);
    m_cmd->trackResource<DxvkAccess::Read>(srcBuffer);

    m_cmd->trackResource<DxvkAccess::None>(tmpBufferViewD);
    m_cmd->trackResource<DxvkAccess::None>(tmpBufferViewS);
  }


  void DxvkContext::discardBuffer(
    const Rc<DxvkBuffer>&       buffer) {
    ScopedCpuProfileZone();
    if (buffer->memFlags() & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)
      return;

    if (m_execBarriers.isBufferDirty(buffer->getSliceHandle(), DxvkAccess::Write))
      this->invalidateBuffer(buffer, buffer->allocSlice());
  }


  void DxvkContext::discardImageView(
    const Rc<DxvkImageView>&      imageView,
          VkImageAspectFlags      discardAspects) {
    ScopedCpuProfileZone();
    VkImageUsageFlags viewUsage = imageView->info().usage;

    // Ignore non-render target views since there's likely no good use case for
    // discarding those. Also, force reinitialization even if the image is bound
    // as a render target, which may have niche use cases for depth buffers.
    if (viewUsage & (VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)) {
      this->spillRenderPass(true);
      this->deferDiscard(imageView, discardAspects);
    }
  }


  void DxvkContext::dispatch(
    uint32_t x,
    uint32_t y,
    uint32_t z) {
    ScopedCpuProfileZone();
    if (this->commitComputeState()) {
      this->commitComputeInitBarriers();

      m_queryManager.beginQueries(m_cmd,
        VK_QUERY_TYPE_PIPELINE_STATISTICS);

      m_cmd->cmdDispatch(x, y, z);

      m_queryManager.endQueries(m_cmd,
        VK_QUERY_TYPE_PIPELINE_STATISTICS);

      this->commitComputePostBarriers();
    }

    m_cmd->addStatCtr(DxvkStatCounter::CmdDispatchCalls, 1);
  }


  void DxvkContext::dispatchIndirect(
    VkDeviceSize      offset) {
    ScopedCpuProfileZone();
    auto bufferSlice = m_state.id.argBuffer.getSliceHandle(
      offset, sizeof(VkDispatchIndirectCommand));

    if (m_execBarriers.isBufferDirty(bufferSlice, DxvkAccess::Read))
      m_execBarriers.recordCommands(m_cmd);

    if (this->commitComputeState()) {
      this->commitComputeInitBarriers();

      m_queryManager.beginQueries(m_cmd,
        VK_QUERY_TYPE_PIPELINE_STATISTICS);

      m_cmd->cmdDispatchIndirect(
        bufferSlice.handle,
        bufferSlice.offset);

      m_queryManager.endQueries(m_cmd,
        VK_QUERY_TYPE_PIPELINE_STATISTICS);

      this->commitComputePostBarriers();

      m_execBarriers.accessBuffer(bufferSlice,
        VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT,
        VK_ACCESS_INDIRECT_COMMAND_READ_BIT,
        m_state.id.argBuffer.bufferInfo().stages,
        m_state.id.argBuffer.bufferInfo().access);

      this->trackDrawBuffer();
    }

    m_cmd->addStatCtr(DxvkStatCounter::CmdDispatchCalls, 1);
  }

  void DxvkContext::draw(
    uint32_t vertexCount,
    uint32_t instanceCount,
    uint32_t firstVertex,
    uint32_t firstInstance) {
    ScopedCpuProfileZone();
    if (this->commitGraphicsState<false, false>()) {

        m_cmd->cmdDraw(
          vertexCount, instanceCount,
          firstVertex, firstInstance);
    }

    m_cmd->addStatCtr(DxvkStatCounter::CmdDrawCalls, 1);
  }


  void DxvkContext::drawIndirect(
    VkDeviceSize      offset,
    uint32_t          count,
    uint32_t          stride) {
    ScopedCpuProfileZone();

    if (this->commitGraphicsState<false, true>()) {
      auto descriptor = m_state.id.argBuffer.getDescriptor();

      m_cmd->cmdDrawIndirect(
        descriptor.buffer.buffer,
        descriptor.buffer.offset + offset,
        count, stride);
    }

    m_cmd->addStatCtr(DxvkStatCounter::CmdDrawCalls, 1);
  }


  void DxvkContext::drawIndirectCount(
    VkDeviceSize      offset,
    VkDeviceSize      countOffset,
    uint32_t          maxCount,
    uint32_t          stride) {
    ScopedCpuProfileZone();
    if (this->commitGraphicsState<false, true>()) {
      auto argDescriptor = m_state.id.argBuffer.getDescriptor();
      auto cntDescriptor = m_state.id.cntBuffer.getDescriptor();

      m_cmd->cmdDrawIndirectCount(
        argDescriptor.buffer.buffer,
        argDescriptor.buffer.offset + offset,
        cntDescriptor.buffer.buffer,
        cntDescriptor.buffer.offset + countOffset,
        maxCount, stride);
    }

    m_cmd->addStatCtr(DxvkStatCounter::CmdDrawCalls, 1);
  }


  void DxvkContext::drawIndexed(
    uint32_t indexCount,
    uint32_t instanceCount,
    uint32_t firstIndex,
    uint32_t vertexOffset,
    uint32_t firstInstance) {
    ScopedCpuProfileZone();
    if (this->commitGraphicsState<true, false>()) {
        m_cmd->cmdDrawIndexed(
          indexCount, instanceCount,
          firstIndex, vertexOffset,
          firstInstance);
    }

    m_cmd->addStatCtr(DxvkStatCounter::CmdDrawCalls, 1);
  }


  void DxvkContext::drawIndexedIndirect(
    VkDeviceSize      offset,
    uint32_t          count,
    uint32_t          stride) {
    ScopedCpuProfileZone();
    if (this->commitGraphicsState<true, true>()) {
      auto descriptor = m_state.id.argBuffer.getDescriptor();

      m_cmd->cmdDrawIndexedIndirect(
        descriptor.buffer.buffer,
        descriptor.buffer.offset + offset,
        count, stride);
    }

    m_cmd->addStatCtr(DxvkStatCounter::CmdDrawCalls, 1);
  }


  void DxvkContext::drawIndexedIndirectCount(
    VkDeviceSize      offset,
    VkDeviceSize      countOffset,
    uint32_t          maxCount,
    uint32_t          stride) {
    ScopedCpuProfileZone();
    if (this->commitGraphicsState<true, true>()) {
      auto argDescriptor = m_state.id.argBuffer.getDescriptor();
      auto cntDescriptor = m_state.id.cntBuffer.getDescriptor();

      m_cmd->cmdDrawIndexedIndirectCount(
        argDescriptor.buffer.buffer,
        argDescriptor.buffer.offset + offset,
        cntDescriptor.buffer.buffer,
        cntDescriptor.buffer.offset + countOffset,
        maxCount, stride);
    }

    m_cmd->addStatCtr(DxvkStatCounter::CmdDrawCalls, 1);
  }


  void DxvkContext::drawIndirectXfb(
    const DxvkBufferSlice& counterBuffer,
    uint32_t          counterDivisor,
    uint32_t          counterBias) {
    ScopedCpuProfileZone();
    if (this->commitGraphicsState<false, false>()) {
      auto physSlice = counterBuffer.getSliceHandle();

      m_cmd->cmdDrawIndirectVertexCount(1, 0,
        physSlice.handle,
        physSlice.offset,
        counterBias,
        counterDivisor);
    }

    m_cmd->addStatCtr(DxvkStatCounter::CmdDrawCalls, 1);
  }


  void DxvkContext::emitRenderTargetReadbackBarrier() {
    ScopedCpuProfileZone();
    if (m_flags.test(DxvkContextFlag::GpRenderPassBound)) {
      emitMemoryBarrier(VK_DEPENDENCY_BY_REGION_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        VK_ACCESS_SHADER_READ_BIT);
    }
  }


  void DxvkContext::initImage(
    const Rc<DxvkImage>& image,
    const VkImageSubresourceRange& subresources,
    VkImageLayout            initialLayout) {
    ScopedCpuProfileZone();
    m_execBarriers.accessImage(image, subresources,
      initialLayout, 0, 0,
      image->info().layout,
      image->info().stages,
      image->info().access);

    (initialLayout == VK_IMAGE_LAYOUT_PREINITIALIZED)
      ? m_cmd->trackResource<DxvkAccess::None>(image)
      : m_cmd->trackResource<DxvkAccess::Write>(image);
  }


  void DxvkContext::generateMipmaps(
    const Rc<DxvkImageView>& imageView,
    VkFilter                  filter) {
    ScopedCpuProfileZone();
    if (imageView->info().numLevels <= 1)
      return;
    
    this->spillRenderPass(false);

    m_execBarriers.recordCommands(m_cmd);

    // Create the a set of framebuffers and image views
    const Rc<DxvkMetaMipGenRenderPass> mipGenerator
      = new DxvkMetaMipGenRenderPass(m_device->vkd(), imageView);

    // Common descriptor set properties that we use to
    // bind the source image view to the fragment shader
    VkDescriptorImageInfo descriptorImage;
    descriptorImage.sampler = m_common->metaBlit().getSampler(filter);
    descriptorImage.imageView = VK_NULL_HANDLE;
    descriptorImage.imageLayout = imageView->imageInfo().layout;

    VkWriteDescriptorSet descriptorWrite;
    descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrite.pNext = nullptr;
    descriptorWrite.dstSet = VK_NULL_HANDLE;
    descriptorWrite.dstBinding = 0;
    descriptorWrite.dstArrayElement = 0;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorWrite.pImageInfo = &descriptorImage;
    descriptorWrite.pBufferInfo = nullptr;
    descriptorWrite.pTexelBufferView = nullptr;

    // Common render pass info
    VkRenderPassBeginInfo passInfo;
    passInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    passInfo.pNext = nullptr;
    passInfo.renderPass = mipGenerator->renderPass();
    passInfo.framebuffer = VK_NULL_HANDLE;
    passInfo.renderArea = VkRect2D{ };
    passInfo.clearValueCount = 0;
    passInfo.pClearValues = nullptr;

    // Retrieve a compatible pipeline to use for rendering
    DxvkMetaBlitPipeline pipeInfo = m_common->metaBlit().getPipeline(
      mipGenerator->viewType(), imageView->info().format, VK_SAMPLE_COUNT_1_BIT);

    for (uint32_t i = 0; i < mipGenerator->passCount(); i++) {
      DxvkMetaBlitPass pass = mipGenerator->pass(i);

      // Width, height and layer count for the current pass
      VkExtent3D passExtent = mipGenerator->passExtent(i);

      // Create descriptor set with the current source view
      descriptorImage.imageView = pass.srcView;
      // NV-DXVK start: use EXT_debug_utils
      descriptorWrite.dstSet = allocateDescriptorSet(pipeInfo.dsetLayout, "DxvkContext::generateMipmaps");
      // NV-DXVK end
      m_cmd->updateDescriptorSets(1, &descriptorWrite);

      // Set up viewport and scissor rect
      VkViewport viewport;
      viewport.x = 0.0f;
      viewport.y = 0.0f;
      viewport.width = float(passExtent.width);
      viewport.height = float(passExtent.height);
      viewport.minDepth = 0.0f;
      viewport.maxDepth = 1.0f;

      VkRect2D scissor;
      scissor.offset = { 0, 0 };
      scissor.extent = { passExtent.width, passExtent.height };

      // Set up render pass info
      passInfo.framebuffer = pass.framebuffer;
      passInfo.renderArea = scissor;

      // Set up push constants
      DxvkMetaBlitPushConstants pushConstants = { };
      pushConstants.srcCoord0 = { 0.0f, 0.0f, 0.0f };
      pushConstants.srcCoord1 = { 1.0f, 1.0f, 1.0f };
      pushConstants.layerCount = passExtent.depth;

      m_cmd->cmdBeginRenderPass(&passInfo, VK_SUBPASS_CONTENTS_INLINE);
      m_cmd->cmdBindPipeline(VK_PIPELINE_BIND_POINT_GRAPHICS, pipeInfo.pipeHandle);
      m_cmd->cmdBindDescriptorSet(VK_PIPELINE_BIND_POINT_GRAPHICS,
        pipeInfo.pipeLayout, descriptorWrite.dstSet, 0, nullptr);

      m_cmd->cmdSetViewport(0, 1, &viewport);
      m_cmd->cmdSetScissor(0, 1, &scissor);

      m_cmd->cmdPushConstants(
        pipeInfo.pipeLayout,
        VK_SHADER_STAGE_FRAGMENT_BIT,
        0, sizeof(pushConstants),
        &pushConstants);

      m_cmd->cmdDraw(3, passExtent.depth, 0, 0);
      m_cmd->cmdEndRenderPass();
    }

    m_cmd->trackResource<DxvkAccess::None>(mipGenerator);
    m_cmd->trackResource<DxvkAccess::Write>(imageView->image());
  }


  void DxvkContext::invalidateBuffer(
    const Rc<DxvkBuffer>& buffer,
    const DxvkBufferSliceHandle& slice) {
    ScopedCpuProfileZone();
    // Allocate new backing resource
    DxvkBufferSliceHandle prevSlice = buffer->rename(slice);
    m_cmd->freeBufferSlice(buffer, prevSlice);

    // We also need to update all bindings that the buffer
    // may be bound to either directly or through views.
    VkBufferUsageFlags usage = buffer->info().usage &
      ~(VK_BUFFER_USAGE_TRANSFER_DST_BIT |
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT);

    if (usage & VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT) {
      m_flags.set(prevSlice.handle == slice.handle
        ? DxvkContextFlags(
            DxvkContextFlag::GpDirtyDescriptorBinding,
            DxvkContextFlag::CpDirtyDescriptorBinding, 
            DxvkContextFlag::RpDirtyDescriptorBinding)
        : DxvkContextFlags(
            DxvkContextFlag::GpDirtyResources,
            DxvkContextFlag::CpDirtyResources, 
            DxvkContextFlag::RpDirtyResources));
    }

    // Fast early-out for uniform buffers, very common
    if (likely(usage == VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT))
      return;

    if (usage & (VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT
      | VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT
      | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)) {
      m_flags.set(DxvkContextFlag::GpDirtyResources,
        DxvkContextFlag::CpDirtyResources,
        DxvkContextFlag::RpDirtyResources);
    }

    if (usage & VK_BUFFER_USAGE_INDEX_BUFFER_BIT)
      m_flags.set(DxvkContextFlag::GpDirtyIndexBuffer);

    if (usage & VK_BUFFER_USAGE_VERTEX_BUFFER_BIT)
      m_flags.set(DxvkContextFlag::GpDirtyVertexBuffers);

    if (usage & VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT)
      m_flags.set(DxvkContextFlag::DirtyDrawBuffer);

    if (usage & VK_BUFFER_USAGE_TRANSFORM_FEEDBACK_BUFFER_BIT_EXT)
      m_flags.set(DxvkContextFlag::GpDirtyXfbBuffers);
  }


  void DxvkContext::pushConstants(
    uint32_t                  offset,
    uint32_t                  size,
    const void*               data) {
    ScopedCpuProfileZone();
    assert(size + offset <= MaxPushConstantSize);
// NV-DXVK start: multiple push const contexts
    std::memcpy(&m_state.pc.data[(uint32_t)m_state.pc.constantBank][offset], data, size);
// NV-DXVK end

    m_flags.set(DxvkContextFlag::DirtyPushConstants);
  }
  
// NV-DXVK start: multiple push const contexts
  void DxvkContext::setPushConstantBank(
    DxvkPushConstantBank constantBank) {
    ScopedCpuProfileZone();
    if (constantBank >= DxvkPushConstantBank::Count) {
      Logger::err("DxvkContext: setPushConstantBank: invalid bank index");
      return;
    }

    if (constantBank == m_state.pc.constantBank) {
      return;
    }

    m_flags.set(DxvkContextFlag::DirtyPushConstants);

    m_state.pc.constantBank = constantBank;
  }
// NV-DXVK end

  void DxvkContext::resolveImage(
    const Rc<DxvkImage>&            dstImage,
    const Rc<DxvkImage>&            srcImage,
    const VkImageResolve&           region,
          VkFormat                  format) {
    ScopedCpuProfileZone();
    this->spillRenderPass(true);
    this->prepareImage(m_execBarriers, dstImage, vk::makeSubresourceRange(region.dstSubresource));
    this->prepareImage(m_execBarriers, srcImage, vk::makeSubresourceRange(region.srcSubresource));

    if (format == VK_FORMAT_UNDEFINED)
      format = srcImage->info().format;

    bool useFb = srcImage->info().format != format
      || dstImage->info().format != format;

    if (m_device->perfHints().preferFbResolve) {
      useFb |= (dstImage->info().usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)
        && (srcImage->info().usage & VK_IMAGE_USAGE_SAMPLED_BIT);
    }

    if (!useFb) {
      this->resolveImageHw(
        dstImage, srcImage, region);
    }
    else {
      this->resolveImageFb(
        dstImage, srcImage, region, format,
        VK_RESOLVE_MODE_NONE_KHR,
        VK_RESOLVE_MODE_NONE_KHR);
    }
  }


  void DxvkContext::resolveDepthStencilImage(
    const Rc<DxvkImage>&            dstImage,
    const Rc<DxvkImage>&            srcImage,
    const VkImageResolve&           region,
          VkResolveModeFlagBitsKHR  depthMode,
          VkResolveModeFlagBitsKHR  stencilMode) {
    ScopedCpuProfileZone();
    this->spillRenderPass(true);
    this->prepareImage(m_execBarriers, dstImage, vk::makeSubresourceRange(region.dstSubresource));
    this->prepareImage(m_execBarriers, srcImage, vk::makeSubresourceRange(region.srcSubresource));

    // Technically legal, but no-op
    if (!depthMode && !stencilMode)
      return;

    // Subsequent functions expect stencil mode to be None
    // if either of the images have no stencil aspect
    if (!(region.dstSubresource.aspectMask
      & region.srcSubresource.aspectMask
      & VK_IMAGE_ASPECT_STENCIL_BIT))
      stencilMode = VK_RESOLVE_MODE_NONE_KHR;

    // We can only use the depth-stencil resolve path if we
    // are resolving a full subresource, and both images have
    // the same format.
    bool useFb = !dstImage->isFullSubresource(region.dstSubresource, region.extent)
      || !srcImage->isFullSubresource(region.srcSubresource, region.extent)
      || dstImage->info().format != srcImage->info().format;

    if (!useFb) {
      // Additionally, the given mode combination must be supported.
      const auto& properties = m_device->properties().khrDepthStencilResolve;

      useFb |= (properties.supportedDepthResolveModes & depthMode) != depthMode
        || (properties.supportedStencilResolveModes & stencilMode) != stencilMode;

      if (depthMode != stencilMode) {
        useFb |= (!depthMode || !stencilMode)
          ? !properties.independentResolveNone
          : !properties.independentResolve;
      }
    }

    if (useFb) {
      this->resolveImageFb(
        dstImage, srcImage, region, VK_FORMAT_UNDEFINED,
        depthMode, stencilMode);
    }
    else {
      this->resolveImageDs(
        dstImage, srcImage, region,
        depthMode, stencilMode);
    }
  }


  void DxvkContext::transformImage(
    const Rc<DxvkImage>&            dstImage,
    const VkImageSubresourceRange&  dstSubresources,
          VkImageLayout             srcLayout,
          VkImageLayout             dstLayout) {
    ScopedCpuProfileZone();
    this->spillRenderPass(false);
    
    if (srcLayout != dstLayout) {
      m_execBarriers.recordCommands(m_cmd);

      m_execBarriers.accessImage(
        dstImage, dstSubresources,
        srcLayout,
        dstImage->info().stages,
        dstImage->info().access,
        dstLayout,
        dstImage->info().stages,
        dstImage->info().access);

      m_cmd->trackResource<DxvkAccess::Write>(dstImage);
    }
  }


  void DxvkContext::performClear(
    const Rc<DxvkImageView>&        imageView,
          int32_t                   attachmentIndex,
          VkImageAspectFlags        discardAspects,
          VkImageAspectFlags        clearAspects,
          VkClearValue              clearValue) {
    ScopedCpuProfileZone();
    DxvkColorAttachmentOps colorOp;
    colorOp.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    colorOp.loadLayout = imageView->imageInfo().layout;
    colorOp.storeLayout = imageView->imageInfo().layout;

    DxvkDepthAttachmentOps depthOp;
    depthOp.loadOpD = VK_ATTACHMENT_LOAD_OP_LOAD;
    depthOp.loadOpS = VK_ATTACHMENT_LOAD_OP_LOAD;
    depthOp.loadLayout = imageView->imageInfo().layout;
    depthOp.storeLayout = imageView->imageInfo().layout;

    if (clearAspects & VK_IMAGE_ASPECT_COLOR_BIT)
      colorOp.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    else if (discardAspects & VK_IMAGE_ASPECT_COLOR_BIT)
      colorOp.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    
    if (clearAspects & VK_IMAGE_ASPECT_DEPTH_BIT)
      depthOp.loadOpD = VK_ATTACHMENT_LOAD_OP_CLEAR;
    else if (discardAspects & VK_IMAGE_ASPECT_DEPTH_BIT)
      depthOp.loadOpD = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    
    if (clearAspects & VK_IMAGE_ASPECT_STENCIL_BIT)
      depthOp.loadOpS = VK_ATTACHMENT_LOAD_OP_CLEAR;
    else if (discardAspects & VK_IMAGE_ASPECT_DEPTH_BIT)
      depthOp.loadOpS = VK_ATTACHMENT_LOAD_OP_DONT_CARE;

    if (attachmentIndex >= 0 && !m_state.om.framebufferInfo.isWritable(attachmentIndex, clearAspects | discardAspects)) {
      // Do not fold the clear/discard into the render pass if any of the affected aspects
      // isn't writable. We can only hit this particular path when starting a render pass,
      // so we can safely manipulate load layouts here.
      int32_t colorIndex = m_state.om.framebufferInfo.getColorAttachmentIndex(attachmentIndex);
      VkImageLayout renderLayout = m_state.om.framebufferInfo.getAttachment(attachmentIndex).layout;

      if (colorIndex < 0) {
        depthOp.loadLayout = m_state.om.renderPassOps.depthOps.loadLayout;
        depthOp.storeLayout = renderLayout;
        m_state.om.renderPassOps.depthOps.loadLayout = renderLayout;
      } else {
        colorOp.loadLayout = m_state.om.renderPassOps.colorOps[colorIndex].loadLayout;
        colorOp.storeLayout = renderLayout;
        m_state.om.renderPassOps.colorOps[colorIndex].loadLayout = renderLayout;
      }

      attachmentIndex = -1;
    }

    bool is3D = imageView->imageInfo().type == VK_IMAGE_TYPE_3D;

    if ((clearAspects | discardAspects) == imageView->info().aspect && !is3D) {
      colorOp.loadLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
      depthOp.loadLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    }

    if (attachmentIndex < 0) {
      if (m_execBarriers.isImageDirty(
        imageView->image(),
        imageView->imageSubresources(),
        DxvkAccess::Write))
        m_execBarriers.recordCommands(m_cmd);

      // Set up and bind a temporary framebuffer
      DxvkRenderTargets attachments;
      DxvkRenderPassOps ops;

      VkPipelineStageFlags clearStages = 0;
      VkAccessFlags        clearAccess = 0;
      
      if ((clearAspects | discardAspects) & VK_IMAGE_ASPECT_COLOR_BIT) {
        clearStages |= VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        clearAccess |= VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        attachments.color[0].view = imageView;
        attachments.color[0].layout = imageView->pickLayout(VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

        ops.colorOps[0] = colorOp;
      }
      else {
        clearStages |= VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT
          | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        clearAccess |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

        attachments.depth.view = imageView;
        attachments.depth.layout = imageView->pickLayout(VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);

        ops.depthOps = depthOp;
      }

      ops.barrier.srcStages = clearStages;
      ops.barrier.srcAccess = clearAccess;
      ops.barrier.dstStages = imageView->imageInfo().stages;
      ops.barrier.dstAccess = imageView->imageInfo().access;

      this->renderPassBindFramebuffer(makeFramebufferInfo(attachments), ops, 1, &clearValue);
      this->renderPassUnbindFramebuffer();
    } else {
      // Perform the operation when starting the next render pass
      if ((clearAspects | discardAspects) & VK_IMAGE_ASPECT_COLOR_BIT) {
        uint32_t colorIndex = m_state.om.framebufferInfo.getColorAttachmentIndex(attachmentIndex);

        m_state.om.renderPassOps.colorOps[colorIndex].loadOp = colorOp.loadOp;
        if (m_state.om.renderPassOps.colorOps[colorIndex].loadOp != VK_ATTACHMENT_LOAD_OP_LOAD && !is3D)
          m_state.om.renderPassOps.colorOps[colorIndex].loadLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        m_state.om.clearValues[attachmentIndex].color = clearValue.color;
      }
      
      if ((clearAspects | discardAspects) & VK_IMAGE_ASPECT_DEPTH_BIT) {
        m_state.om.renderPassOps.depthOps.loadOpD = depthOp.loadOpD;
        m_state.om.clearValues[attachmentIndex].depthStencil.depth = clearValue.depthStencil.depth;
      }
      
      if ((clearAspects | discardAspects) & VK_IMAGE_ASPECT_STENCIL_BIT) {
        m_state.om.renderPassOps.depthOps.loadOpS = depthOp.loadOpS;
        m_state.om.clearValues[attachmentIndex].depthStencil.stencil = clearValue.depthStencil.stencil;
      }

      if ((clearAspects | discardAspects) & (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)) {
        if (m_state.om.renderPassOps.depthOps.loadOpD != VK_ATTACHMENT_LOAD_OP_LOAD
         && m_state.om.renderPassOps.depthOps.loadOpS != VK_ATTACHMENT_LOAD_OP_LOAD)
          m_state.om.renderPassOps.depthOps.loadLayout = VK_IMAGE_LAYOUT_UNDEFINED;
      }
    }
  }


  void DxvkContext::deferClear(
    const Rc<DxvkImageView>& imageView,
    VkImageAspectFlags        clearAspects,
    VkClearValue              clearValue) {
    ScopedCpuProfileZone();
    for (auto& entry : m_deferredClears) {
      if (entry.imageView->matchesView(imageView)) {
        entry.imageView = imageView;
        entry.discardAspects &= ~clearAspects;
        entry.clearAspects |= clearAspects;

        if (clearAspects & VK_IMAGE_ASPECT_COLOR_BIT)
          entry.clearValue.color = clearValue.color;
        if (clearAspects & VK_IMAGE_ASPECT_DEPTH_BIT)
          entry.clearValue.depthStencil.depth = clearValue.depthStencil.depth;
        if (clearAspects & VK_IMAGE_ASPECT_STENCIL_BIT)
          entry.clearValue.depthStencil.stencil = clearValue.depthStencil.stencil;

        return;
      } else if (entry.imageView->checkSubresourceOverlap(imageView)) {
        this->spillRenderPass(false);
        break;
      }
    }

    m_deferredClears.push_back({ imageView, 0, clearAspects, clearValue });
  }

  void DxvkContext::deferDiscard(
    const Rc<DxvkImageView>&        imageView,
          VkImageAspectFlags        discardAspects) {
    ScopedCpuProfileZone();
    for (auto& entry : m_deferredClears) {
      if (entry.imageView->matchesView(imageView)) {
        entry.imageView = imageView;
        entry.discardAspects |= discardAspects;
        entry.clearAspects &= ~discardAspects;
        return;
      } else if (entry.imageView->checkSubresourceOverlap(imageView)) {
        this->spillRenderPass(false);
        break;
      }
    }

    m_deferredClears.push_back({ imageView, discardAspects });
  }


  void DxvkContext::flushClears(
    bool                      useRenderPass) {
    ScopedCpuProfileZone();
    for (const auto& clear : m_deferredClears) {
      int32_t attachmentIndex = -1;

      if (useRenderPass && m_state.om.framebufferInfo.isFullSize(clear.imageView))
        attachmentIndex = m_state.om.framebufferInfo.findAttachment(clear.imageView);

      this->performClear(clear.imageView, attachmentIndex,
        clear.discardAspects, clear.clearAspects, clear.clearValue);
    }

    m_deferredClears.clear();
  }


  void DxvkContext::flushSharedImages() {
    ScopedCpuProfileZone();
    for (auto i = m_deferredClears.begin(); i != m_deferredClears.end(); ) {
      if (i->imageView->imageInfo().shared) {
        this->performClear(i->imageView, -1, i->discardAspects, i->clearAspects, i->clearValue);
        i = m_deferredClears.erase(i);
      } else {
        i++;
      }
    }

    this->transitionRenderTargetLayouts(m_execBarriers, true);
  }


// NV-DXVK start: replaced buffers do not play well with rtx
  void DxvkContext::updateBuffer(
    const Rc<DxvkBuffer>&           buffer,
          VkDeviceSize              offset,
          VkDeviceSize              size,
    const void*                     data) {
    ScopedCpuProfileZone();
    bool replaceBuffer = this->tryInvalidateDeviceLocalBuffer(buffer, size);
    auto bufferSlice = buffer->getSliceHandle(offset, size);

    if (!replaceBuffer) {
      this->spillRenderPass(true);
    
      if (m_execBarriers.isBufferDirty(bufferSlice, DxvkAccess::Write))
        m_execBarriers.recordCommands(m_cmd);
    }

    DxvkCmdBuffer cmdBuffer = replaceBuffer
      ? DxvkCmdBuffer::InitBuffer
      : DxvkCmdBuffer::ExecBuffer;

    m_cmd->cmdUpdateBuffer(cmdBuffer,
      bufferSlice.handle,
      bufferSlice.offset,
      bufferSlice.length,
      data);

    auto& barriers = replaceBuffer
      ? m_initBarriers
      : m_execBarriers;

    barriers.accessBuffer(bufferSlice,
      VK_PIPELINE_STAGE_TRANSFER_BIT,
      VK_ACCESS_TRANSFER_WRITE_BIT,
      buffer->info().stages,
      buffer->info().access);

    m_cmd->trackResource<DxvkAccess::Write>(buffer);
  }

// NV-DXVK begin: utility function for partial buffer uploads
  void DxvkContext::writeToBuffer(
    const Rc<DxvkBuffer>& buffer,
          VkDeviceSize    offset,
          VkDeviceSize    size,
    const void*           data) {
    ScopedCpuProfileZone();

    if (size < 65536 && size % 4 == 0) {
      updateBuffer(buffer, offset, size, data);
    } else {
      this->spillRenderPass(true);
      
      DxvkBufferSliceHandle bufferSlice = buffer->getSliceHandle(offset, size);
      DxvkCmdBuffer cmdBuffer = DxvkCmdBuffer::ExecBuffer;

      auto stagingSlice = m_staging.alloc(CACHE_LINE_SIZE, size);
      auto stagingHandle = stagingSlice.getSliceHandle();

      std::memcpy(stagingHandle.mapPtr, data, size);

      VkBufferCopy region;
      region.srcOffset = stagingHandle.offset;
      region.dstOffset = bufferSlice.offset;
      region.size = size;

      m_cmd->cmdCopyBuffer(cmdBuffer,
                           stagingHandle.handle,
                           bufferSlice.handle,
                           1,
                           &region);

      m_cmd->trackResource<DxvkAccess::Read>(stagingSlice.buffer());

      auto& barriers = m_execBarriers;
      barriers.accessBuffer(
        bufferSlice,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_ACCESS_TRANSFER_WRITE_BIT,
        buffer->info().stages,
        buffer->info().access);

      m_cmd->trackResource<DxvkAccess::Write>(buffer);
    }
  }
// NV-DXVK end

// NV-DXVK start: preserve updateImage function
  void DxvkContext::updateImage(
    const Rc<DxvkImage>& image,
    const VkImageSubresourceLayers& subresources,
          VkOffset3D                imageOffset,
          VkExtent3D                imageExtent,
    const void*                     data,
          VkDeviceSize              pitchPerRow,
          VkDeviceSize              pitchPerLayer) {
    ScopedCpuProfileZone();
    this->spillRenderPass(true);

    // Upload data through a staging buffer. Special care needs to
    // be taken when dealing with compressed image formats: Rather
    // than copying pixels, we'll be copying blocks of pixels.
    const DxvkFormatInfo* formatInfo = image->formatInfo();

    // Align image extent to a full block. This is necessary in
    // case the image size is not a multiple of the block size.
    VkExtent3D elementCount = util::computeBlockCount(
      imageExtent, formatInfo->blockSize);
    elementCount.depth *= subresources.layerCount;

    // Allocate staging buffer memory for the image data. The
    // pixels or blocks will be tightly packed within the buffer.
    auto stagingSlice = m_staging.alloc(CACHE_LINE_SIZE,
      formatInfo->elementSize * util::flattenImageExtent(elementCount));
    auto stagingHandle = stagingSlice.getSliceHandle();

    util::packImageData(stagingHandle.mapPtr, data,
      elementCount, formatInfo->elementSize,
      pitchPerRow, pitchPerLayer);

    // Prepare the image layout. If the given extent covers
    // the entire image, we may discard its previous contents.
    auto subresourceRange = vk::makeSubresourceRange(subresources);
    subresourceRange.aspectMask = image->formatInfo()->aspectMask;

    this->prepareImage(m_execBarriers, image, subresourceRange);

    if (m_execBarriers.isImageDirty(image, subresourceRange, DxvkAccess::Write))
      m_execBarriers.recordCommands(m_cmd);

    // Initialize the image if the entire subresource is covered
    VkImageLayout imageLayoutInitial = image->info().layout;
    VkImageLayout imageLayoutTransfer = image->pickLayout(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    if (image->isFullSubresource(subresources, imageExtent))
      imageLayoutInitial = VK_IMAGE_LAYOUT_UNDEFINED;

    if (imageLayoutTransfer != imageLayoutInitial) {
      m_execAcquires.accessImage(
        image, subresourceRange,
        imageLayoutInitial,
        VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
        imageLayoutTransfer,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_ACCESS_TRANSFER_WRITE_BIT);
    }

    m_execAcquires.recordCommands(m_cmd);

    // Copy contents of the staging buffer into the image.
    // Since our source data is tightly packed, we do not
    // need to specify any strides.
    VkBufferImageCopy region;
    region.bufferOffset = stagingHandle.offset;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource = subresources;
    region.imageOffset = imageOffset;
    region.imageExtent = imageExtent;

    m_cmd->cmdCopyBufferToImage(DxvkCmdBuffer::ExecBuffer,
      stagingHandle.handle, image->handle(),
      imageLayoutTransfer, 1, &region);

    // Transition image back into its optimal layout
    m_execBarriers.accessImage(
      image, subresourceRange,
      imageLayoutTransfer,
      VK_PIPELINE_STAGE_TRANSFER_BIT,
      VK_ACCESS_TRANSFER_WRITE_BIT,
      image->info().layout,
      image->info().stages,
      image->info().access);

    m_cmd->trackResource<DxvkAccess::Write>(image);
    m_cmd->trackResource<DxvkAccess::Read>(stagingSlice.buffer());
  }
// NV-DXVK end

  void DxvkContext::updateDepthStencilImage(
    const Rc<DxvkImage>& image,
    const VkImageSubresourceLayers& subresources,
    VkOffset2D                imageOffset,
    VkExtent2D                imageExtent,
    const void* data,
    VkDeviceSize              pitchPerRow,
    VkDeviceSize              pitchPerLayer,
    VkFormat                  format) {
    ScopedCpuProfileZone();
    auto formatInfo = imageFormatInfo(format);

    VkExtent3D extent3D;
    extent3D.width = imageExtent.width;
    extent3D.height = imageExtent.height;
    extent3D.depth = subresources.layerCount;

    VkDeviceSize pixelCount = extent3D.width * extent3D.height * extent3D.depth;

    DxvkBufferCreateInfo tmpBufferInfo;
    tmpBufferInfo.size = pixelCount * formatInfo->elementSize;
    tmpBufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    tmpBufferInfo.stages = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    tmpBufferInfo.access = VK_ACCESS_SHADER_READ_BIT;

    auto tmpBuffer = m_device->createBuffer(tmpBufferInfo,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
      VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, DxvkMemoryStats::Category::AppTexture, "updateDepthStencilImage");

    util::packImageData(tmpBuffer->mapPtr(0), data,
      extent3D, formatInfo->elementSize,
      pitchPerRow, pitchPerLayer);

    copyPackedBufferToDepthStencilImage(
      image, subresources, imageOffset, imageExtent,
      tmpBuffer, 0, VkOffset2D { 0, 0 }, imageExtent,
      format);
  }


  void DxvkContext::uploadBuffer(
    const Rc<DxvkBuffer>& buffer,
    const void* data,
    uint32_t length) {
    ScopedCpuProfileZone();
    auto bufferSlice = buffer->getSliceHandle();

    if (length == 0)
    {
      length = bufferSlice.length;
    }

    auto stagingSlice = m_staging.alloc(CACHE_LINE_SIZE, length);
    auto stagingHandle = stagingSlice.getSliceHandle();

    std::memcpy(stagingHandle.mapPtr, data, length);

    VkBufferCopy region;
    region.srcOffset = stagingHandle.offset;
    region.dstOffset = bufferSlice.offset;
    region.size = length;

    m_cmd->cmdCopyBuffer(DxvkCmdBuffer::SdmaBuffer,
      stagingHandle.handle, bufferSlice.handle, 1, &region);

    m_sdmaBarriers.releaseBuffer(
      m_initBarriers, bufferSlice,
      m_device->queues().transfer.queueFamily,
      VK_PIPELINE_STAGE_TRANSFER_BIT,
      VK_ACCESS_TRANSFER_WRITE_BIT,
      m_device->queues().graphics.queueFamily,
      buffer->info().stages,
      buffer->info().access);

    m_cmd->trackResource<DxvkAccess::Read>(stagingSlice.buffer());
    m_cmd->trackResource<DxvkAccess::Write>(buffer);
  }


  void DxvkContext::uploadImage(
    const Rc<DxvkImage>& image,
    const VkImageSubresourceLayers& subresources,
    const void*               data,
    VkDeviceSize              pitchPerRow,
    VkDeviceSize              pitchPerLayer) {
    ScopedCpuProfileZone();
    const DxvkFormatInfo* formatInfo = image->formatInfo();

    VkOffset3D imageOffset = { 0, 0, 0 };
    VkExtent3D imageExtent = image->mipLevelExtent(subresources.mipLevel);

    // Allocate staging buffer slice and copy data to it
    VkExtent3D elementCount = util::computeBlockCount(
      imageExtent, formatInfo->blockSize);
    elementCount.depth *= subresources.layerCount;

    // NV-DXVK start: early submit heuristics for memcpy work
    auto bytesToCopy = formatInfo->elementSize * util::flattenImageExtent(elementCount);
    auto stagingSlice = m_staging.alloc(CACHE_LINE_SIZE, bytesToCopy);
    // NV-DXVK end

    auto stagingHandle = stagingSlice.getSliceHandle();

    util::packImageData(stagingHandle.mapPtr, data,
      elementCount, formatInfo->elementSize,
      pitchPerRow, pitchPerLayer);

    DxvkCmdBuffer cmdBuffer = DxvkCmdBuffer::SdmaBuffer;
    DxvkBarrierSet* barriers = &m_sdmaAcquires;

    if (subresources.aspectMask & (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)) {
      cmdBuffer = DxvkCmdBuffer::InitBuffer;
      barriers = &m_initBarriers;
    }

    // Discard previous subresource contents
    barriers->accessImage(image,
      vk::makeSubresourceRange(subresources),
      VK_IMAGE_LAYOUT_UNDEFINED, 0, 0,
      image->pickLayout(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL),
      VK_PIPELINE_STAGE_TRANSFER_BIT,
      VK_ACCESS_TRANSFER_WRITE_BIT);

    barriers->recordCommands(m_cmd);

    this->copyImageHostData(cmdBuffer,
      image, subresources, imageOffset, imageExtent,
      data, pitchPerRow, pitchPerLayer);

    // Transfer ownership to graphics queue
    if (cmdBuffer == DxvkCmdBuffer::SdmaBuffer) {
      m_sdmaBarriers.releaseImage(m_initBarriers,
        image, vk::makeSubresourceRange(subresources),
        m_device->queues().transfer.queueFamily,
        image->pickLayout(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL),
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_ACCESS_TRANSFER_WRITE_BIT,
        m_device->queues().graphics.queueFamily,
        image->info().layout,
        image->info().stages,
        image->info().access);
    }
    else {
      barriers->accessImage(image,
        vk::makeSubresourceRange(subresources),
        image->pickLayout(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL),
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_ACCESS_TRANSFER_WRITE_BIT,
        image->info().layout,
        image->info().stages,
        image->info().access);
    }

    m_cmd->trackResource<DxvkAccess::Write>(image);
    m_cmd->trackResource<DxvkAccess::Read>(stagingSlice.buffer());

    // NV-DXVK start: early submit heuristics for memcpy work
    recordGPUMemCopy(bytesToCopy);
    // NV-DXVK end
  }

  void DxvkContext::setViewports(
    uint32_t            viewportCount,
    const VkViewport* viewports,
    const VkRect2D* scissorRects) {
    ScopedCpuProfileZone();
    if (m_state.gp.state.rs.viewportCount() != viewportCount) {
      m_state.gp.state.rs.setViewportCount(viewportCount);
      m_flags.set(DxvkContextFlag::GpDirtyPipelineState);
    }

    for (uint32_t i = 0; i < viewportCount; i++) {
      m_state.vp.viewports[i] = viewports[i];
      m_state.vp.scissorRects[i] = scissorRects[i];

      // Vulkan viewports are not allowed to have a width or
      // height of zero, so we fall back to a dummy viewport
      // and instead set an empty scissor rect, which is legal.
      if (viewports[i].width == 0.0f || viewports[i].height == 0.0f) {
        m_state.vp.viewports[i] = VkViewport{
          0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f };
        m_state.vp.scissorRects[i] = VkRect2D{
          VkOffset2D { 0, 0 },
          VkExtent2D { 0, 0 } };
      }
    }

    m_flags.set(DxvkContextFlag::GpDirtyViewport);
  }


  void DxvkContext::setBlendConstants(
    DxvkBlendConstants  blendConstants) {
    ScopedCpuProfileZone();
    if (m_state.dyn.blendConstants != blendConstants) {
      m_state.dyn.blendConstants = blendConstants;
      m_flags.set(DxvkContextFlag::GpDirtyBlendConstants);
    }
  }


  void DxvkContext::setDepthBias(
    DxvkDepthBias       depthBias) {
    ScopedCpuProfileZone();
    if (m_state.dyn.depthBias != depthBias) {
      m_state.dyn.depthBias = depthBias;
      m_flags.set(DxvkContextFlag::GpDirtyDepthBias);
    }
  }


  void DxvkContext::setDepthBounds(
    DxvkDepthBounds     depthBounds) {
    ScopedCpuProfileZone();
    if (m_state.dyn.depthBounds != depthBounds) {
      m_state.dyn.depthBounds = depthBounds;
      m_flags.set(DxvkContextFlag::GpDirtyDepthBounds);
    }

    if (m_state.gp.state.ds.enableDepthBoundsTest() != depthBounds.enableDepthBounds) {
      m_state.gp.state.ds.setEnableDepthBoundsTest(depthBounds.enableDepthBounds);
      m_flags.set(DxvkContextFlag::GpDirtyPipelineState);
    }
  }


  void DxvkContext::setStencilReference(
    uint32_t            reference) {
    ScopedCpuProfileZone();
    if (m_state.dyn.stencilReference != reference) {
      m_state.dyn.stencilReference = reference;
      m_flags.set(DxvkContextFlag::GpDirtyStencilRef);
    }
  }


  void DxvkContext::setInputAssemblyState(const DxvkInputAssemblyState& ia) {
    ScopedCpuProfileZone();
    m_state.gp.state.ia = DxvkIaInfo(
      ia.primitiveTopology,
      ia.primitiveRestart,
      ia.patchVertexCount);

    m_flags.set(DxvkContextFlag::GpDirtyPipelineState);
  }


  void DxvkContext::setInputLayout(
    uint32_t             attributeCount,
    const DxvkVertexAttribute* attributes,
    uint32_t             bindingCount,
    const DxvkVertexBinding* bindings) {
    ScopedCpuProfileZone();
    m_flags.set(
      DxvkContextFlag::GpDirtyPipelineState,
      DxvkContextFlag::GpDirtyVertexBuffers);

    for (uint32_t i = 0; i < attributeCount; i++) {
      m_state.gp.state.ilAttributes[i] = DxvkIlAttribute(
        attributes[i].location, attributes[i].binding,
        attributes[i].format, attributes[i].offset);
    }

    for (uint32_t i = attributeCount; i < m_state.gp.state.il.attributeCount(); i++)
      m_state.gp.state.ilAttributes[i] = DxvkIlAttribute();

    for (uint32_t i = 0; i < bindingCount; i++) {
      m_state.gp.state.ilBindings[i] = DxvkIlBinding(
        bindings[i].binding, 0, bindings[i].inputRate,
        bindings[i].fetchRate);
    }

    for (uint32_t i = bindingCount; i < m_state.gp.state.il.bindingCount(); i++)
      m_state.gp.state.ilBindings[i] = DxvkIlBinding();

    m_state.gp.state.il = DxvkIlInfo(attributeCount, bindingCount);
  }


  void DxvkContext::setRasterizerState(const DxvkRasterizerState& rs) {
    ScopedCpuProfileZone();
    m_state.gp.state.rs = DxvkRsInfo(
      rs.depthClipEnable,
      rs.depthBiasEnable,
      rs.polygonMode,
      rs.cullMode,
      rs.frontFace,
      m_state.gp.state.rs.viewportCount(),
      rs.sampleCount,
      rs.conservativeMode);

    m_flags.set(DxvkContextFlag::GpDirtyPipelineState);
  }


  void DxvkContext::setMultisampleState(const DxvkMultisampleState& ms) {
    ScopedCpuProfileZone();
    m_state.gp.state.ms = DxvkMsInfo(
      m_state.gp.state.ms.sampleCount(),
      ms.sampleMask,
      ms.enableAlphaToCoverage);

    m_flags.set(DxvkContextFlag::GpDirtyPipelineState);
  }


  void DxvkContext::setDepthStencilState(const DxvkDepthStencilState& ds) {
    ScopedCpuProfileZone();
    m_state.gp.state.ds = DxvkDsInfo(
      ds.enableDepthTest,
      ds.enableDepthWrite,
      m_state.gp.state.ds.enableDepthBoundsTest(),
      ds.enableStencilTest,
      ds.depthCompareOp);

    m_state.gp.state.dsFront = DxvkDsStencilOp(ds.stencilOpFront);
    m_state.gp.state.dsBack = DxvkDsStencilOp(ds.stencilOpBack);

    m_flags.set(DxvkContextFlag::GpDirtyPipelineState);
  }


  void DxvkContext::setLogicOpState(const DxvkLogicOpState& lo) {
    ScopedCpuProfileZone();
    m_state.gp.state.om = DxvkOmInfo(
      lo.enableLogicOp,
      lo.logicOp);

    m_flags.set(DxvkContextFlag::GpDirtyPipelineState);
  }


  void DxvkContext::setBlendMode(
    uint32_t            attachment,
    const DxvkBlendMode& blendMode) {
    ScopedCpuProfileZone();
    m_state.gp.state.omBlend[attachment] = DxvkOmAttachmentBlend(
      blendMode.enableBlending,
      blendMode.colorSrcFactor,
      blendMode.colorDstFactor,
      blendMode.colorBlendOp,
      blendMode.alphaSrcFactor,
      blendMode.alphaDstFactor,
      blendMode.alphaBlendOp,
      blendMode.writeMask);

    m_flags.set(DxvkContextFlag::GpDirtyPipelineState);
  }


  void DxvkContext::setSpecConstant(
    VkPipelineBindPoint pipeline,
    uint32_t            index,
    uint32_t            value) {
    ScopedCpuProfileZone();
    // NV-DXVK start: terrain baking
    static_assert(D3D9SpecConstantId::Count <= DxvkLimits::MaxNumSpecConstants);
    // NV-DXVK end
    auto& specConst =
      pipeline == VK_PIPELINE_BIND_POINT_GRAPHICS
      ? m_state.gp.state.sc.specConstants[index]
      : pipeline == VK_PIPELINE_BIND_POINT_COMPUTE
        ? m_state.cp.state.sc.specConstants[index]
        : m_state.rp.state.sc.specConstants[index];

    if (specConst != value) {
      specConst = value;

      m_flags.set(
        pipeline == VK_PIPELINE_BIND_POINT_GRAPHICS
        ? DxvkContextFlag::GpDirtyPipelineState
        : pipeline == VK_PIPELINE_BIND_POINT_COMPUTE
          ? DxvkContextFlag::CpDirtyPipelineState
          : DxvkContextFlag::RpDirtyPipelineState);
    }
  }


  void DxvkContext::setBarrierControl(DxvkBarrierControlFlags control) {
    m_barrierControl = control;
  }


  void DxvkContext::signalGpuEvent(const Rc<DxvkGpuEvent>& event) {
    ScopedCpuProfileZone();
    this->spillRenderPass(true);
    
    DxvkGpuEventHandle handle = m_common->eventPool().allocEvent();

    m_cmd->cmdSetEvent(handle.event,
      VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);

    m_cmd->trackGpuEvent(event->reset(handle));
    m_cmd->trackResource<DxvkAccess::None>(event);
  }


  void DxvkContext::launchCuKernelNVX(
    const VkCuLaunchInfoNVX& nvxLaunchInfo,
    const std::vector<std::pair<Rc<DxvkBuffer>, DxvkAccessFlags>>& buffers,
    const std::vector<std::pair<Rc<DxvkImage>,  DxvkAccessFlags>>& images) {
    ScopedCpuProfileZone();
    // The resources in the std::vectors above are called-out
    // explicitly in the API for barrier and tracking purposes
    // since they're being used bindlessly.
    this->spillRenderPass(true);

    VkPipelineStageFlags srcStages = 0;
    VkAccessFlags srcAccess = 0;

    for (auto& r : buffers) {
      srcStages |= r.first->info().stages;
      srcAccess |= r.first->info().access;
    }

    for (auto& r : images) {
      srcStages |= r.first->info().stages;
      srcAccess |= r.first->info().access;

      this->prepareImage(m_execBarriers, r.first, r.first->getAvailableSubresources());
    }

    m_execBarriers.accessMemory(srcStages, srcAccess,
      VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
      VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
    m_execBarriers.recordCommands(m_cmd);

    m_cmd->cmdLaunchCuKernel(nvxLaunchInfo);

    for (auto& r : buffers) {
      VkAccessFlags accessFlags = (r.second.test(DxvkAccess::Read) * VK_ACCESS_SHADER_READ_BIT)
                                | (r.second.test(DxvkAccess::Write) * VK_ACCESS_SHADER_WRITE_BIT);
      DxvkBufferSliceHandle bufferSlice = r.first->getSliceHandle();
      m_execBarriers.accessBuffer(bufferSlice,
        VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
        accessFlags,
        r.first->info().stages,
        r.first->info().access);
    }

    for (auto& r : images) {
      VkAccessFlags accessFlags = (r.second.test(DxvkAccess::Read) * VK_ACCESS_SHADER_READ_BIT)
                                | (r.second.test(DxvkAccess::Write) * VK_ACCESS_SHADER_WRITE_BIT);
      m_execBarriers.accessImage(r.first,
        r.first->getAvailableSubresources(),
        r.first->info().layout,
        VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
        accessFlags,
        r.first->info().layout,
        r.first->info().stages,
        r.first->info().access);
    }

    for (auto& r : images) {
      if (r.second.test(DxvkAccess::Read)) m_cmd->trackResource<DxvkAccess::Read>(r.first);
      if (r.second.test(DxvkAccess::Write)) m_cmd->trackResource<DxvkAccess::Write>(r.first);
    }

    for (auto& r : buffers) {
      if (r.second.test(DxvkAccess::Read)) m_cmd->trackResource<DxvkAccess::Read>(r.first);
      if (r.second.test(DxvkAccess::Write)) m_cmd->trackResource<DxvkAccess::Write>(r.first);
    }
  }


  void DxvkContext::writeTimestamp(const Rc<DxvkGpuQuery>& query) {
    ScopedCpuProfileZone();
    m_queryManager.writeTimestamp(m_cmd, query);
  }


  void DxvkContext::signal(const Rc<sync::Signal>& signal, uint64_t value) {
    ScopedCpuProfileZone();
    m_cmd->queueSignal(signal, value);
  }


  void DxvkContext::beginDebugLabel(VkDebugUtilsLabelEXT *label) {
    ScopedCpuProfileZone();
    if (!m_device->instance()->extensions().extDebugUtils)
      return;

    m_cmd->cmdBeginDebugUtilsLabel(label);
  }

  // NV-DXVK start: Integrate Aftermath
  void DxvkContext::deviceDiagnosticCheckpoint(const void* data) {
    ScopedCpuProfileZone();
    if (m_device->extensions().nvDeviceDiagnosticCheckpoints)
      m_cmd->vkCmdSetCheckpointNV(data);
  }
  // NV-DXVK end

  void DxvkContext::endDebugLabel() {
    ScopedCpuProfileZone();
    if (!m_device->instance()->extensions().extDebugUtils)
      return;

    m_cmd->cmdEndDebugUtilsLabel();
  }

  void DxvkContext::insertDebugLabel(VkDebugUtilsLabelEXT *label) {
    ScopedCpuProfileZone();
    if (!m_device->instance()->extensions().extDebugUtils)
      return;

    m_cmd->cmdInsertDebugUtilsLabel(label);
  }


  // NV-DXVK start: early submit heuristics for memcpy work
  void DxvkContext::recordGPUMemCopy(uint32_t bytes)
  {
    // XXX TODO - this early submit logic is disabled because it results in missing geometry.
    return;

    m_bytesCopiedInCurrentCmdlist += bytes;

    const uint32_t threshold = m_device->config().memcpyKickoffThreshold;
    if (threshold > 0 && bytes >= threshold) {
      flushCommandList();
    }
  }
  // NV-DXVK end

  void DxvkContext::blitImageFb(
    const Rc<DxvkImage>& dstImage,
    const Rc<DxvkImage>& srcImage,
    const VkImageBlit& region,
    const VkComponentMapping& mapping,
    VkFilter              filter) {
    ScopedCpuProfileZone();
    auto dstSubresourceRange = vk::makeSubresourceRange(region.dstSubresource);
    auto srcSubresourceRange = vk::makeSubresourceRange(region.srcSubresource);

    if (m_execBarriers.isImageDirty(dstImage, dstSubresourceRange, DxvkAccess::Write)
      || m_execBarriers.isImageDirty(srcImage, srcSubresourceRange, DxvkAccess::Write))
      m_execBarriers.recordCommands(m_cmd);

    bool isDepthStencil = region.srcSubresource.aspectMask & (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT);

    VkImageLayout srcLayout = srcImage->pickLayout(isDepthStencil
      ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL
      : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    
    if (srcImage->info().layout != srcLayout) {
      m_execAcquires.accessImage(
        srcImage, srcSubresourceRange,
        srcImage->info().layout,
        srcImage->info().stages, 0,
        srcLayout,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        VK_ACCESS_SHADER_READ_BIT);
      
      m_execAcquires.recordCommands(m_cmd);
    }

    // Sort out image offsets so that dstOffset[0] points
    // to the top-left corner of the target area
    VkOffset3D srcOffsets[2] = { region.srcOffsets[0], region.srcOffsets[1] };
    VkOffset3D dstOffsets[2] = { region.dstOffsets[0], region.dstOffsets[1] };

    if (dstOffsets[0].x > dstOffsets[1].x) {
      std::swap(dstOffsets[0].x, dstOffsets[1].x);
      std::swap(srcOffsets[0].x, srcOffsets[1].x);
    }

    if (dstOffsets[0].y > dstOffsets[1].y) {
      std::swap(dstOffsets[0].y, dstOffsets[1].y);
      std::swap(srcOffsets[0].y, srcOffsets[1].y);
    }

    if (dstOffsets[0].z > dstOffsets[1].z) {
      std::swap(dstOffsets[0].z, dstOffsets[1].z);
      std::swap(srcOffsets[0].z, srcOffsets[1].z);
    }

    VkExtent3D dstExtent = {
      uint32_t(dstOffsets[1].x - dstOffsets[0].x),
      uint32_t(dstOffsets[1].y - dstOffsets[0].y),
      uint32_t(dstOffsets[1].z - dstOffsets[0].z) };

    // Begin render pass
    Rc<DxvkMetaBlitRenderPass> pass = new DxvkMetaBlitRenderPass(
      m_device, dstImage, srcImage, region, mapping);
    DxvkMetaBlitPass passObjects = pass->pass();

    VkExtent3D imageExtent = dstImage->mipLevelExtent(region.dstSubresource.mipLevel);

    VkRect2D renderArea;
    renderArea.offset = VkOffset2D{ 0, 0 };
    renderArea.extent = VkExtent2D{ imageExtent.width, imageExtent.height };

    VkRenderPassBeginInfo passInfo;
    passInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    passInfo.pNext = nullptr;
    passInfo.renderPass = passObjects.renderPass;
    passInfo.framebuffer = passObjects.framebuffer;
    passInfo.renderArea = renderArea;
    passInfo.clearValueCount = 0;
    passInfo.pClearValues = nullptr;

    m_cmd->cmdBeginRenderPass(&passInfo, VK_SUBPASS_CONTENTS_INLINE);

    // Bind pipeline
    DxvkMetaBlitPipeline pipeInfo = m_common->metaBlit().getPipeline(
      pass->viewType(), dstImage->info().format, dstImage->info().sampleCount);

    m_cmd->cmdBindPipeline(VK_PIPELINE_BIND_POINT_GRAPHICS, pipeInfo.pipeHandle);

    // Set up viewport
    VkViewport viewport;
    viewport.x = float(dstOffsets[0].x);
    viewport.y = float(dstOffsets[0].y);
    viewport.width = float(dstExtent.width);
    viewport.height = float(dstExtent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor;
    scissor.offset = { dstOffsets[0].x, dstOffsets[0].y };
    scissor.extent = { dstExtent.width, dstExtent.height };

    m_cmd->cmdSetViewport(0, 1, &viewport);
    m_cmd->cmdSetScissor(0, 1, &scissor);

    // Bind source image view
    VkDescriptorImageInfo descriptorImage;
    descriptorImage.sampler     = m_common->metaBlit().getSampler(filter);
    descriptorImage.imageView   = passObjects.srcView;
    descriptorImage.imageLayout = srcLayout;
    
    VkWriteDescriptorSet descriptorWrite;
    descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrite.pNext = nullptr;
    // NV-DXVK start: use EXT_debug_utils
    descriptorWrite.dstSet = allocateDescriptorSet(pipeInfo.dsetLayout, "DxvkContext::blitImageFb");
    // NV-DXVK end
    descriptorWrite.dstBinding = 0;
    descriptorWrite.dstArrayElement = 0;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorWrite.pImageInfo = &descriptorImage;
    descriptorWrite.pBufferInfo = nullptr;
    descriptorWrite.pTexelBufferView = nullptr;

    m_cmd->updateDescriptorSets(1, &descriptorWrite);
    m_cmd->cmdBindDescriptorSet(VK_PIPELINE_BIND_POINT_GRAPHICS,
      pipeInfo.pipeLayout, descriptorWrite.dstSet, 0, nullptr);

    // Compute shader parameters for the operation
    VkExtent3D srcExtent = srcImage->mipLevelExtent(region.srcSubresource.mipLevel);

    DxvkMetaBlitPushConstants pushConstants = { };
    pushConstants.srcCoord0 = {
      float(srcOffsets[0].x) / float(srcExtent.width),
      float(srcOffsets[0].y) / float(srcExtent.height),
      float(srcOffsets[0].z) / float(srcExtent.depth) };
    pushConstants.srcCoord1 = {
      float(srcOffsets[1].x) / float(srcExtent.width),
      float(srcOffsets[1].y) / float(srcExtent.height),
      float(srcOffsets[1].z) / float(srcExtent.depth) };
    pushConstants.layerCount = pass->framebufferLayerCount();

    m_cmd->cmdPushConstants(
      pipeInfo.pipeLayout,
      VK_SHADER_STAGE_FRAGMENT_BIT,
      0, sizeof(pushConstants),
      &pushConstants);

    m_cmd->cmdDraw(3, pushConstants.layerCount, 0, 0);
    m_cmd->cmdEndRenderPass();

    // Add barriers and track image objects
    m_execBarriers.accessImage(dstImage,
      vk::makeSubresourceRange(region.dstSubresource),
      dstImage->info().layout,
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
      VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
      dstImage->info().layout,
      dstImage->info().stages,
      dstImage->info().access);

    m_execBarriers.accessImage(srcImage,
      vk::makeSubresourceRange(region.srcSubresource),
      srcLayout,
      VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
      VK_ACCESS_SHADER_READ_BIT,
      srcImage->info().layout,
      srcImage->info().stages,
      srcImage->info().access);

    m_cmd->trackResource<DxvkAccess::Write>(dstImage);
    m_cmd->trackResource<DxvkAccess::Read>(srcImage);
    m_cmd->trackResource<DxvkAccess::None>(pass);
  }


  void DxvkContext::blitImageHw(
    const Rc<DxvkImage>& dstImage,
    const Rc<DxvkImage>& srcImage,
    const VkImageBlit& region,
    VkFilter              filter) {
    ScopedCpuProfileZone();
    auto dstSubresourceRange = vk::makeSubresourceRange(region.dstSubresource);
    auto srcSubresourceRange = vk::makeSubresourceRange(region.srcSubresource);

    if (m_execBarriers.isImageDirty(dstImage, dstSubresourceRange, DxvkAccess::Write)
      || m_execBarriers.isImageDirty(srcImage, srcSubresourceRange, DxvkAccess::Write))
      m_execBarriers.recordCommands(m_cmd);

    // Prepare the two images for transfer ops if necessary
    auto dstLayout = dstImage->pickLayout(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    auto srcLayout = srcImage->pickLayout(VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

    if (dstImage->info().layout != dstLayout) {
      m_execAcquires.accessImage(
        dstImage, dstSubresourceRange,
        dstImage->info().layout,
        VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
        dstLayout,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_ACCESS_TRANSFER_WRITE_BIT);
    }

    if (srcImage->info().layout != srcLayout) {
      m_execAcquires.accessImage(
        srcImage, srcSubresourceRange,
        srcImage->info().layout,
        VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
        srcLayout,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_ACCESS_TRANSFER_READ_BIT);
    }

    m_execAcquires.recordCommands(m_cmd);

    // Perform the blit operation
    m_cmd->cmdBlitImage(
      srcImage->handle(), srcLayout,
      dstImage->handle(), dstLayout,
      1, &region, filter);

    m_execBarriers.accessImage(
      dstImage, dstSubresourceRange, dstLayout,
      VK_PIPELINE_STAGE_TRANSFER_BIT,
      VK_ACCESS_TRANSFER_WRITE_BIT,
      dstImage->info().layout,
      dstImage->info().stages,
      dstImage->info().access);

    m_execBarriers.accessImage(
      srcImage, srcSubresourceRange, srcLayout,
      VK_PIPELINE_STAGE_TRANSFER_BIT,
      VK_ACCESS_TRANSFER_READ_BIT,
      srcImage->info().layout,
      srcImage->info().stages,
      srcImage->info().access);

    m_cmd->trackResource<DxvkAccess::Write>(dstImage);
    m_cmd->trackResource<DxvkAccess::Read>(srcImage);
  }


  template<bool ToImage>
  void DxvkContext::copyImageBufferData(
          DxvkCmdBuffer         cmd,
    const Rc<DxvkImage>&        image,
    const VkImageSubresourceLayers& imageSubresource,
          VkOffset3D            imageOffset,
          VkExtent3D            imageExtent,
          VkImageLayout         imageLayout,
    const DxvkBufferSliceHandle& bufferSlice,
          VkDeviceSize          bufferRowAlignment,
          VkDeviceSize          bufferSliceAlignment) {
    ScopedCpuProfileZone();
    auto formatInfo = image->formatInfo();
    auto layers = imageSubresource.layerCount;

    VkDeviceSize bufferOffset = bufferSlice.offset;

    // Do one copy region per layer in case the buffer memory layout is weird
    if (bufferSliceAlignment || formatInfo->flags.test(DxvkFormatFlag::MultiPlane))
      layers = 1;

    for (uint32_t i = 0; i < imageSubresource.layerCount; i += layers) {
      auto aspectOffset = bufferOffset;

      for (auto aspects = imageSubresource.aspectMask; aspects; ) {
        auto aspect = vk::getNextAspect(aspects);
        auto elementSize = formatInfo->elementSize;

        VkBufferImageCopy copyRegion = { };
        copyRegion.imageSubresource.aspectMask = aspect;
        copyRegion.imageSubresource.baseArrayLayer = imageSubresource.baseArrayLayer + i;
        copyRegion.imageSubresource.layerCount = layers;
        copyRegion.imageSubresource.mipLevel = imageSubresource.mipLevel;
        copyRegion.imageOffset = imageOffset;
        copyRegion.imageExtent = imageExtent;

        if (formatInfo->flags.test(DxvkFormatFlag::MultiPlane)) {
          auto plane = &formatInfo->planes[vk::getPlaneIndex(aspect)];
          copyRegion.imageOffset.x /= plane->blockSize.width;
          copyRegion.imageOffset.y /= plane->blockSize.height;
          copyRegion.imageExtent.width  /= plane->blockSize.width;
          copyRegion.imageExtent.height /= plane->blockSize.height;
          elementSize = plane->elementSize;
        }

        // Vulkan can't really express row pitch in the same way that client APIs
        // may expect, so we'll need to do some heroics here and hope that it works
        VkExtent3D blockCount = util::computeBlockCount(copyRegion.imageExtent, formatInfo->blockSize);
        VkDeviceSize rowPitch = blockCount.width * elementSize;

        if (bufferRowAlignment > elementSize)
          rowPitch = bufferRowAlignment >= rowPitch ? bufferRowAlignment : align(rowPitch, bufferRowAlignment);

        VkDeviceSize slicePitch = blockCount.height * rowPitch;

        if (image->info().type == VK_IMAGE_TYPE_3D && bufferSliceAlignment > elementSize)
          slicePitch = bufferSliceAlignment >= slicePitch ? bufferSliceAlignment : align(slicePitch, bufferSliceAlignment);

        copyRegion.bufferOffset      = aspectOffset;
        copyRegion.bufferRowLength   = formatInfo->blockSize.width * rowPitch / elementSize;
        copyRegion.bufferImageHeight = formatInfo->blockSize.height * slicePitch / rowPitch;

        // Perform the actual copy
        if constexpr (ToImage) {
          m_cmd->cmdCopyBufferToImage(cmd, bufferSlice.handle,
            image->handle(), imageLayout, 1, &copyRegion);
        } else {
          m_cmd->cmdCopyImageToBuffer(cmd, image->handle(), imageLayout,
            bufferSlice.handle, 1, &copyRegion);
        }

        aspectOffset += blockCount.depth * slicePitch;
      }

      // Advance to next layer. This is non-trivial for multi-plane formats
      // since plane data for each layer is expected to be packed.
      VkDeviceSize layerPitch = aspectOffset - bufferOffset;

      if (bufferSliceAlignment)
        layerPitch = bufferSliceAlignment >= layerPitch ? bufferSliceAlignment : align(layerPitch, bufferSliceAlignment);

      bufferOffset += layerPitch;
    }
  }


  void DxvkContext::copyImageHostData(
          DxvkCmdBuffer         cmd,
    const Rc<DxvkImage>&        image,
    const VkImageSubresourceLayers& imageSubresource,
          VkOffset3D            imageOffset,
          VkExtent3D            imageExtent,
    const void*                 hostData,
          VkDeviceSize          rowPitch,
          VkDeviceSize          slicePitch) {
    ScopedCpuProfileZone();
    auto formatInfo = image->formatInfo();
    auto srcData = reinterpret_cast<const char*>(hostData);

    for (uint32_t i = 0; i < imageSubresource.layerCount; i++) {
      auto layerData = srcData + i * slicePitch;

      for (auto aspects = imageSubresource.aspectMask; aspects; ) {
        auto aspect = vk::getNextAspect(aspects);
        auto extent = imageExtent;

        VkDeviceSize elementSize = formatInfo->elementSize;

        if (formatInfo->flags.test(DxvkFormatFlag::MultiPlane)) {
          auto plane = &formatInfo->planes[vk::getPlaneIndex(aspect)];
          extent.width  /= plane->blockSize.width;
          extent.height /= plane->blockSize.height;
          elementSize = plane->elementSize;
        }

        auto blockCount = util::computeBlockCount(extent, formatInfo->blockSize);
        auto stagingSlice  = m_staging.alloc(CACHE_LINE_SIZE, elementSize * util::flattenImageExtent(blockCount));
        auto stagingHandle = stagingSlice.getSliceHandle();

        util::packImageData(stagingHandle.mapPtr, layerData,
          blockCount, elementSize, rowPitch, slicePitch);

        auto subresource = imageSubresource;
        subresource.aspectMask = aspect;

        this->copyImageBufferData<true>(cmd,
          image, subresource, imageOffset, imageExtent,
          image->pickLayout(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL),
          stagingHandle, 0, 0);

        layerData += blockCount.height * rowPitch;

        m_cmd->trackResource<DxvkAccess::Read>(stagingSlice.buffer());
      }
    }
  }


  void DxvkContext::clearImageViewFb(
    const Rc<DxvkImageView>& imageView,
    VkOffset3D            offset,
    VkExtent3D            extent,
    VkImageAspectFlags    aspect,
    VkClearValue          value) {
    ScopedCpuProfileZone();
    this->updateFramebuffer();

    // Find out if the render target view is currently bound,
    // so that we can avoid spilling the render pass if it is.
    int32_t attachmentIndex = -1;

    if (m_state.om.framebufferInfo.isFullSize(imageView))
      attachmentIndex = m_state.om.framebufferInfo.findAttachment(imageView);

    if (attachmentIndex >= 0 && !m_state.om.framebufferInfo.isWritable(attachmentIndex, aspect))
      attachmentIndex = -1;

    if (attachmentIndex < 0) {
      this->spillRenderPass(false);

      if (m_execBarriers.isImageDirty(
        imageView->image(),
        imageView->imageSubresources(),
        DxvkAccess::Write))
        m_execBarriers.recordCommands(m_cmd);

      // Set up a temporary framebuffer
      DxvkRenderTargets attachments;
      DxvkRenderPassOps ops;

      VkPipelineStageFlags clearStages = 0;
      VkAccessFlags        clearAccess = 0;

      if (imageView->info().aspect & VK_IMAGE_ASPECT_COLOR_BIT) {
        clearStages |= VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        clearAccess |= VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
          | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;

        attachments.color[0].view = imageView;
        attachments.color[0].layout = imageView->pickLayout(VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

        ops.colorOps[0].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        ops.colorOps[0].loadLayout = imageView->imageInfo().layout;
        ops.colorOps[0].storeLayout = imageView->imageInfo().layout;
      }
      else {
        clearStages |= VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT
          | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        clearAccess |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT
          | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;

        attachments.depth.view = imageView;
        attachments.depth.layout = imageView->pickLayout(VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);

        ops.depthOps.loadOpD = VK_ATTACHMENT_LOAD_OP_LOAD;
        ops.depthOps.loadOpS = VK_ATTACHMENT_LOAD_OP_LOAD;
        ops.depthOps.loadLayout = imageView->imageInfo().layout;
        ops.depthOps.storeLayout = imageView->imageInfo().layout;
      }

      ops.barrier.srcStages = clearStages;
      ops.barrier.srcAccess = clearAccess;
      ops.barrier.dstStages = imageView->imageInfo().stages;
      ops.barrier.dstAccess = imageView->imageInfo().access;

      // We cannot leverage render pass clears
      // because we clear only part of the view
      this->renderPassBindFramebuffer(makeFramebufferInfo(attachments), ops, 0, nullptr);
    } else {
      // Make sure the render pass is active so
      // that we can actually perform the clear
      this->startRenderPass();
    }

    // Perform the actual clear operation
    VkClearAttachment clearInfo;
    clearInfo.aspectMask          = aspect;
    clearInfo.colorAttachment     = 0;
    clearInfo.clearValue          = value;

    if ((aspect & VK_IMAGE_ASPECT_COLOR_BIT) && (attachmentIndex >= 0))
      clearInfo.colorAttachment   = m_state.om.framebufferInfo.getColorAttachmentIndex(attachmentIndex);

    VkClearRect clearRect;
    clearRect.rect.offset.x = offset.x;
    clearRect.rect.offset.y = offset.y;
    clearRect.rect.extent.width = extent.width;
    clearRect.rect.extent.height = extent.height;
    clearRect.baseArrayLayer = 0;
    clearRect.layerCount = imageView->info().numLayers;

    clearAttachments(clearInfo, clearRect);

    // Unbind temporary framebuffer
    if (attachmentIndex < 0)
      this->renderPassUnbindFramebuffer();
  }

  void DxvkContext::clearAttachments(VkClearAttachment clearInfo, VkClearRect clearRect)
  {
    ScopedCpuProfileZone();
    m_cmd->cmdClearAttachments(1, &clearInfo, 1, &clearRect);
  }

  void DxvkContext::clearImageViewCs(
    const Rc<DxvkImageView>&    imageView,
          VkOffset3D            offset,
          VkExtent3D            extent,
          VkClearValue          value) {
    ScopedCpuProfileZone();
    this->spillRenderPass(false);
    this->unbindComputePipeline();

    if (m_execBarriers.isImageDirty(
      imageView->image(),
      imageView->imageSubresources(),
      DxvkAccess::Write))
      m_execBarriers.recordCommands(m_cmd);

    // Query pipeline objects to use for this clear operation
    DxvkMetaClearPipeline pipeInfo = m_common->metaClear().getClearImagePipeline(
      imageView->type(), imageFormatInfo(imageView->info().format)->flags);

    // Create a descriptor set pointing to the view
    // NV-DXVK start: use EXT_debug_utils
    VkDescriptorSet descriptorSet = allocateDescriptorSet(pipeInfo.dsetLayout, "DxvkContext::clearImageViewCs");
    // NV-DXVK end

    VkDescriptorImageInfo viewInfo;
    viewInfo.sampler = VK_NULL_HANDLE;
    viewInfo.imageView = imageView->handle();
    viewInfo.imageLayout = imageView->imageInfo().layout;

    VkWriteDescriptorSet descriptorWrite;
    descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrite.pNext = nullptr;
    descriptorWrite.dstSet = descriptorSet;
    descriptorWrite.dstBinding = 0;
    descriptorWrite.dstArrayElement = 0;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    descriptorWrite.pImageInfo = &viewInfo;
    descriptorWrite.pBufferInfo = nullptr;
    descriptorWrite.pTexelBufferView = nullptr;
    m_cmd->updateDescriptorSets(1, &descriptorWrite);

    // Prepare shader arguments
    DxvkMetaClearArgs pushArgs = { };
    pushArgs.clearValue = value.color;
    pushArgs.offset = offset;
    pushArgs.extent = extent;

    VkExtent3D workgroups = util::computeBlockCount(
      pushArgs.extent, pipeInfo.workgroupSize);

    if (imageView->type() == VK_IMAGE_VIEW_TYPE_1D_ARRAY)
      workgroups.height = imageView->subresources().layerCount;
    else if (imageView->type() == VK_IMAGE_VIEW_TYPE_2D_ARRAY)
      workgroups.depth = imageView->subresources().layerCount;

    m_cmd->cmdBindPipeline(
      VK_PIPELINE_BIND_POINT_COMPUTE,
      pipeInfo.pipeline);
    m_cmd->cmdBindDescriptorSet(
      VK_PIPELINE_BIND_POINT_COMPUTE,
      pipeInfo.pipeLayout, descriptorSet,
      0, nullptr);
    m_cmd->cmdPushConstants(
      pipeInfo.pipeLayout,
      VK_SHADER_STAGE_COMPUTE_BIT,
      0, sizeof(pushArgs), &pushArgs);
    m_cmd->cmdDispatch(
      workgroups.width,
      workgroups.height,
      workgroups.depth);

    m_execBarriers.accessImage(
      imageView->image(),
      imageView->imageSubresources(),
      imageView->imageInfo().layout,
      VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
      VK_ACCESS_SHADER_WRITE_BIT,
      imageView->imageInfo().layout,
      imageView->imageInfo().stages,
      imageView->imageInfo().access);

    m_cmd->trackResource<DxvkAccess::None>(imageView);
    m_cmd->trackResource<DxvkAccess::Write>(imageView->image());
  }

  
  void DxvkContext::copyImageHw(
    const Rc<DxvkImage>&        dstImage,
          VkImageSubresourceLayers dstSubresource,
          VkOffset3D            dstOffset,
    const Rc<DxvkImage>&        srcImage,
          VkImageSubresourceLayers srcSubresource,
          VkOffset3D            srcOffset,
          VkExtent3D            extent) {
    ScopedCpuProfileZone();
    auto dstSubresourceRange = vk::makeSubresourceRange(dstSubresource);
    auto srcSubresourceRange = vk::makeSubresourceRange(srcSubresource);

    auto dstFormatInfo = dstImage->formatInfo();

    if (m_execBarriers.isImageDirty(dstImage, dstSubresourceRange, DxvkAccess::Write)
     || m_execBarriers.isImageDirty(srcImage, srcSubresourceRange, DxvkAccess::Write))
      m_execBarriers.recordCommands(m_cmd);

    VkImageLayout dstImageLayout = dstImage->pickLayout(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkImageLayout srcImageLayout = srcImage->pickLayout(VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

    VkImageLayout dstInitImageLayout = dstImage->info().layout;

    if (dstImage->isFullSubresource(dstSubresource, extent))
      dstInitImageLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (dstImageLayout != dstInitImageLayout) {
      m_execAcquires.accessImage(
        dstImage, dstSubresourceRange,
        dstInitImageLayout,
        VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
        dstImageLayout,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_ACCESS_TRANSFER_WRITE_BIT);
    }

    if (srcImageLayout != srcImage->info().layout) {
      m_execAcquires.accessImage(
        srcImage, srcSubresourceRange,
        srcImage->info().layout,
        VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
        srcImageLayout,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_ACCESS_TRANSFER_READ_BIT);
    }

    m_execAcquires.recordCommands(m_cmd);
    
    for (auto aspects = dstSubresource.aspectMask; aspects; ) {
      auto aspect = vk::getNextAspect(aspects);

      VkImageCopy imageRegion;
      imageRegion.srcSubresource = srcSubresource;
      imageRegion.srcSubresource.aspectMask = aspect;
      imageRegion.srcOffset      = srcOffset;
      imageRegion.dstSubresource = dstSubresource;
      imageRegion.dstSubresource.aspectMask = aspect;
      imageRegion.dstOffset      = dstOffset;
      imageRegion.extent         = extent;

      if (dstFormatInfo->flags.test(DxvkFormatFlag::MultiPlane)) {
        auto plane = &dstFormatInfo->planes[vk::getPlaneIndex(aspect)];
        imageRegion.srcOffset.x /= plane->blockSize.width;
        imageRegion.srcOffset.y /= plane->blockSize.height;
        imageRegion.dstOffset.x /= plane->blockSize.width;
        imageRegion.dstOffset.y /= plane->blockSize.height;
        imageRegion.extent.width  /= plane->blockSize.width;
        imageRegion.extent.height /= plane->blockSize.height;
      }

      m_cmd->cmdCopyImage(DxvkCmdBuffer::ExecBuffer,
        srcImage->handle(), srcImageLayout,
        dstImage->handle(), dstImageLayout,
        1, &imageRegion);
    }
    
    m_execBarriers.accessImage(
      dstImage, dstSubresourceRange,
      dstImageLayout,
      VK_PIPELINE_STAGE_TRANSFER_BIT,
      VK_ACCESS_TRANSFER_WRITE_BIT,
      dstImage->info().layout,
      dstImage->info().stages,
      dstImage->info().access);

    m_execBarriers.accessImage(
      srcImage, srcSubresourceRange,
      srcImageLayout,
      VK_PIPELINE_STAGE_TRANSFER_BIT,
      VK_ACCESS_TRANSFER_READ_BIT,
      srcImage->info().layout,
      srcImage->info().stages,
      srcImage->info().access);
    
    m_cmd->trackResource<DxvkAccess::Write>(dstImage);
    m_cmd->trackResource<DxvkAccess::Read>(srcImage);
  }

  
  void DxvkContext::copyImageFb(
    const Rc<DxvkImage>& dstImage,
    VkImageSubresourceLayers dstSubresource,
    VkOffset3D            dstOffset,
    const Rc<DxvkImage>& srcImage,
    VkImageSubresourceLayers srcSubresource,
    VkOffset3D            srcOffset,
    VkExtent3D            extent) {
    ScopedCpuProfileZone();
    auto dstSubresourceRange = vk::makeSubresourceRange(dstSubresource);
    auto srcSubresourceRange = vk::makeSubresourceRange(srcSubresource);

    if (m_execBarriers.isImageDirty(dstImage, dstSubresourceRange, DxvkAccess::Write)
      || m_execBarriers.isImageDirty(srcImage, srcSubresourceRange, DxvkAccess::Write))
      m_execBarriers.recordCommands(m_cmd);

    // Source image needs to be readable
    if (!(srcImage->info().usage & VK_IMAGE_USAGE_SAMPLED_BIT)) {
      Logger::err("DxvkContext: copyImageFb: Source image not readable");
      return;
    }

    // Render target format to use for this copy
    VkFormat viewFormat = m_common->metaCopy().getCopyDestinationFormat(
      dstSubresource.aspectMask,
      srcSubresource.aspectMask,
      srcImage->info().format);

    if (viewFormat == VK_FORMAT_UNDEFINED) {
      Logger::err("DxvkContext: copyImageFb: Unsupported format");
      return;
    }

    // We might have to transition the source image layout
    VkImageLayout srcLayout = (srcSubresource.aspectMask & VK_IMAGE_ASPECT_COLOR_BIT)
      ? srcImage->pickLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
      : srcImage->pickLayout(VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);

    if (srcImage->info().layout != srcLayout) {
      m_execAcquires.accessImage(
        srcImage, srcSubresourceRange,
        srcImage->info().layout,
        srcImage->info().stages, 0,
        srcLayout,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        VK_ACCESS_SHADER_READ_BIT);

      m_execAcquires.recordCommands(m_cmd);
    }

    // In some cases, we may be able to render to the destination
    // image directly, which is faster than using a temporary image
    VkImageUsageFlagBits tgtUsage = (dstSubresource.aspectMask & VK_IMAGE_ASPECT_COLOR_BIT)
      ? VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT
      : VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;

    bool useDirectRender = (dstImage->isViewCompatible(viewFormat))
      && (dstImage->info().usage & tgtUsage);

    // If needed, create a temporary render target for the copy
    Rc<DxvkImage>            tgtImage;
    VkImageSubresourceLayers tgtSubresource = dstSubresource;
    VkOffset3D               tgtOffset = dstOffset;

    if (!useDirectRender) {
      DxvkImageCreateInfo info;
      info.type = dstImage->info().type;
      info.format = viewFormat;
      info.flags = 0;
      info.sampleCount = dstImage->info().sampleCount;
      info.extent = extent;
      info.numLayers = dstSubresource.layerCount;
      info.mipLevels = 1;
      info.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | tgtUsage;
      info.stages = VK_PIPELINE_STAGE_TRANSFER_BIT;
      info.access = VK_ACCESS_TRANSFER_READ_BIT;
      info.tiling = VK_IMAGE_TILING_OPTIMAL;
      info.layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

      // NV-DXVK start: add debug names to VkImage objects
      tgtImage = m_device->createImage(info, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, DxvkMemoryStats::Category::AppTexture, "copyImageFb target");
      // NV-DXVK end

      tgtSubresource.mipLevel = 0;
      tgtSubresource.baseArrayLayer = 0;

      tgtOffset = { 0, 0, 0 };
    } else {
      tgtImage = dstImage;
    }

    // Create source and destination image views
    VkImageViewType viewType = dstImage->info().type == VK_IMAGE_TYPE_1D
      ? VK_IMAGE_VIEW_TYPE_1D_ARRAY
      : VK_IMAGE_VIEW_TYPE_2D_ARRAY;

    DxvkImageViewCreateInfo tgtViewInfo;
    tgtViewInfo.type = viewType;
    tgtViewInfo.format = viewFormat;
    tgtViewInfo.usage = tgtUsage;
    tgtViewInfo.aspect = tgtSubresource.aspectMask;
    tgtViewInfo.minLevel = tgtSubresource.mipLevel;
    tgtViewInfo.numLevels = 1;
    tgtViewInfo.minLayer = tgtSubresource.baseArrayLayer;
    tgtViewInfo.numLayers = tgtSubresource.layerCount;

    DxvkImageViewCreateInfo srcViewInfo;
    srcViewInfo.type = viewType;
    srcViewInfo.format = srcImage->info().format;
    srcViewInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
    srcViewInfo.aspect = srcSubresource.aspectMask & (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_COLOR_BIT);
    srcViewInfo.minLevel = srcSubresource.mipLevel;
    srcViewInfo.numLevels = 1;
    srcViewInfo.minLayer = srcSubresource.baseArrayLayer;
    srcViewInfo.numLayers = srcSubresource.layerCount;

    Rc<DxvkImageView> tgtImageView = m_device->createImageView(tgtImage, tgtViewInfo);
    Rc<DxvkImageView> srcImageView = m_device->createImageView(srcImage, srcViewInfo);
    Rc<DxvkImageView> srcStencilView;

    if (srcSubresource.aspectMask & VK_IMAGE_ASPECT_STENCIL_BIT) {
      srcViewInfo.aspect = VK_IMAGE_ASPECT_STENCIL_BIT;
      srcStencilView = m_device->createImageView(srcImage, srcViewInfo);
    }

    // Create framebuffer and pipeline for the copy
    Rc<DxvkMetaCopyRenderPass> fb = new DxvkMetaCopyRenderPass(
      m_device->vkd(), tgtImageView, srcImageView, srcStencilView,
      tgtImage->isFullSubresource(tgtSubresource, extent));

    auto pipeInfo = m_common->metaCopy().getPipeline(
      viewType, viewFormat, tgtImage->info().sampleCount);

    VkDescriptorImageInfo descriptorImage;
    descriptorImage.sampler = VK_NULL_HANDLE;
    descriptorImage.imageView = srcImageView->handle();
    descriptorImage.imageLayout = srcLayout;

    VkWriteDescriptorSet descriptorWrite;
    descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrite.pNext = nullptr;
    descriptorWrite.dstBinding = 0;
    descriptorWrite.dstArrayElement = 0;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorWrite.pImageInfo = &descriptorImage;
    descriptorWrite.pBufferInfo = nullptr;
    descriptorWrite.pTexelBufferView = nullptr;

    // NV-DXVK start: use EXT_debug_utils
    descriptorWrite.dstSet = allocateDescriptorSet(pipeInfo.dsetLayout, "DxvkContext::copyImageFb");
    // NV-DXVK end
    m_cmd->updateDescriptorSets(1, &descriptorWrite);

    if (srcSubresource.aspectMask & VK_IMAGE_ASPECT_STENCIL_BIT) {
      descriptorImage.imageView = srcStencilView->handle();
      descriptorWrite.dstBinding = 1;
      m_cmd->updateDescriptorSets(1, &descriptorWrite);
    }

    VkViewport viewport;
    viewport.x = float(tgtOffset.x);
    viewport.y = float(tgtOffset.y);
    viewport.width = float(extent.width);
    viewport.height = float(extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor;
    scissor.offset = { tgtOffset.x, tgtOffset.y };
    scissor.extent = { extent.width, extent.height };

    VkRenderPassBeginInfo info;
    info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    info.pNext = nullptr;
    info.renderPass = fb->renderPass();
    info.framebuffer = fb->framebuffer();
    info.renderArea.offset = { 0, 0 };
    info.renderArea.extent = {
      tgtImage->mipLevelExtent(tgtSubresource.mipLevel).width,
      tgtImage->mipLevelExtent(tgtSubresource.mipLevel).height };
    info.clearValueCount = 0;
    info.pClearValues = nullptr;

    // Perform the actual copy operation
    m_cmd->cmdBeginRenderPass(&info, VK_SUBPASS_CONTENTS_INLINE);
    m_cmd->cmdBindPipeline(VK_PIPELINE_BIND_POINT_GRAPHICS, pipeInfo.pipeHandle);
    m_cmd->cmdBindDescriptorSet(VK_PIPELINE_BIND_POINT_GRAPHICS,
      pipeInfo.pipeLayout, descriptorWrite.dstSet, 0, nullptr);

    m_cmd->cmdSetViewport(0, 1, &viewport);
    m_cmd->cmdSetScissor(0, 1, &scissor);

    VkOffset2D srcCoordOffset = {
      srcOffset.x - tgtOffset.x,
      srcOffset.y - tgtOffset.y };

    m_cmd->cmdPushConstants(pipeInfo.pipeLayout,
      VK_SHADER_STAGE_FRAGMENT_BIT,
      0, sizeof(srcCoordOffset),
      &srcCoordOffset);

    m_cmd->cmdDraw(3, tgtSubresource.layerCount, 0, 0);
    m_cmd->cmdEndRenderPass();

    if (srcLayout != srcImage->info().layout) {
      m_execBarriers.accessImage(
        srcImage, srcSubresourceRange, srcLayout,
        srcImage->info().stages,
        srcImage->info().access,
        srcImage->info().layout,
        srcImage->info().stages,
        srcImage->info().access);
    }

    m_cmd->trackResource<DxvkAccess::Write>(tgtImage);
    m_cmd->trackResource<DxvkAccess::Read>(srcImage);
    m_cmd->trackResource<DxvkAccess::None>(fb);

    // If necessary, copy the temporary image
    // to the original destination image
    if (!useDirectRender) {
      this->copyImageHw(
        dstImage, dstSubresource, dstOffset,
        tgtImage, tgtSubresource, tgtOffset,
        extent);
    }
  }

  bool DxvkContext::copyImageClear(
    const Rc<DxvkImage>&        dstImage,
          VkImageSubresourceLayers dstSubresource,
          VkOffset3D            dstOffset,
          VkExtent3D            dstExtent,
    const Rc<DxvkImage>&        srcImage,
          VkImageSubresourceLayers srcSubresource) {
    ScopedCpuProfileZone();
    // If the source image has a pending deferred clear, we can
    // implement the copy by clearing the destination image to
    // the same clear value.
    const VkImageUsageFlags attachmentUsage
      = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT
      | VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;

    if (!(dstImage->info().usage & attachmentUsage)
     || !(srcImage->info().usage & attachmentUsage))
      return false;

    // Ignore 3D images since those are complicated to handle
    if (dstImage->info().type == VK_IMAGE_TYPE_3D
     || srcImage->info().type == VK_IMAGE_TYPE_3D)
      return false;

    // Find a pending clear that overlaps with the source image
    const DxvkDeferredClear* clear = nullptr;

    for (const auto& entry : m_deferredClears) {
      // Entries in the deferred clear array cannot overlap, so
      // if we find an entry covering all source subresources,
      // it's the only one in the list that does.
      if ((entry.imageView->image() == srcImage) && ((srcSubresource.aspectMask & entry.clearAspects) == srcSubresource.aspectMask)
       && (vk::checkSubresourceRangeSuperset(entry.imageView->subresources(), vk::makeSubresourceRange(srcSubresource)))) {
        clear = &entry;
        break;
      }
    }

    if (!clear)
      return false;

    // Create a view for the destination image with the general
    // properties ofthe source image view used for the clear
    DxvkImageViewCreateInfo viewInfo = clear->imageView->info();
    viewInfo.type = dstImage->info().type == VK_IMAGE_TYPE_1D
      ? VK_IMAGE_VIEW_TYPE_1D_ARRAY
      : VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    viewInfo.minLevel = dstSubresource.mipLevel;
    viewInfo.numLevels = 1;
    viewInfo.minLayer = dstSubresource.baseArrayLayer;
    viewInfo.numLayers = dstSubresource.layerCount;

    // That is, if the formats are actually compatible
    // so that we can safely use the same clear value
    if (!dstImage->isViewCompatible(viewInfo.format))
      return false;

    // Ignore mismatched size for now, needs more testing since we'd
    // need to prepare the image first and then call clearImageViewFb
    if (dstImage->mipLevelExtent(dstSubresource.mipLevel) != dstExtent)
      return false;

    auto view = m_device->createImageView(dstImage, viewInfo);
    this->deferClear(view, srcSubresource.aspectMask, clear->clearValue);
    return true;
  }

  void DxvkContext::resolveImageHw(
    const Rc<DxvkImage>& dstImage,
    const Rc<DxvkImage>& srcImage,
    const VkImageResolve& region) {
    auto dstSubresourceRange = vk::makeSubresourceRange(region.dstSubresource);
    auto srcSubresourceRange = vk::makeSubresourceRange(region.srcSubresource);

    if (m_execBarriers.isImageDirty(dstImage, dstSubresourceRange, DxvkAccess::Write)
      || m_execBarriers.isImageDirty(srcImage, srcSubresourceRange, DxvkAccess::Write))
      m_execBarriers.recordCommands(m_cmd);

    // We only support resolving to the entire image
    // area, so we might as well discard its contents
    VkImageLayout dstLayout = dstImage->pickLayout(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkImageLayout srcLayout = srcImage->pickLayout(VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

    VkImageLayout initialLayout = dstImage->info().layout;

    if (dstImage->isFullSubresource(region.dstSubresource, region.extent))
      initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (dstLayout != initialLayout) {
      m_execAcquires.accessImage(
        dstImage, dstSubresourceRange, initialLayout,
        VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
        dstLayout,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_ACCESS_TRANSFER_WRITE_BIT);
    }

    if (srcLayout != srcImage->info().layout) {
      m_execAcquires.accessImage(
        srcImage, srcSubresourceRange,
        srcImage->info().layout,
        VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
        srcLayout,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_ACCESS_TRANSFER_READ_BIT);
    }

    m_execAcquires.recordCommands(m_cmd);

    m_cmd->cmdResolveImage(
      srcImage->handle(), srcLayout,
      dstImage->handle(), dstLayout,
      1, &region);

    m_execBarriers.accessImage(
      dstImage, dstSubresourceRange, dstLayout,
      VK_PIPELINE_STAGE_TRANSFER_BIT,
      VK_ACCESS_TRANSFER_WRITE_BIT,
      dstImage->info().layout,
      dstImage->info().stages,
      dstImage->info().access);

    m_execBarriers.accessImage(
      srcImage, srcSubresourceRange, srcLayout,
      VK_PIPELINE_STAGE_TRANSFER_BIT,
      VK_ACCESS_TRANSFER_READ_BIT,
      srcImage->info().layout,
      srcImage->info().stages,
      srcImage->info().access);

    m_cmd->trackResource<DxvkAccess::Write>(dstImage);
    m_cmd->trackResource<DxvkAccess::Read>(srcImage);
  }


  void DxvkContext::resolveImageDs(
    const Rc<DxvkImage>& dstImage,
    const Rc<DxvkImage>& srcImage,
    const VkImageResolve& region,
    VkResolveModeFlagBitsKHR  depthMode,
    VkResolveModeFlagBitsKHR  stencilMode) {
    ScopedCpuProfileZone();
    auto dstSubresourceRange = vk::makeSubresourceRange(region.dstSubresource);
    auto srcSubresourceRange = vk::makeSubresourceRange(region.srcSubresource);

    if (m_execBarriers.isImageDirty(dstImage, dstSubresourceRange, DxvkAccess::Write)
      || m_execBarriers.isImageDirty(srcImage, srcSubresourceRange, DxvkAccess::Write))
      m_execBarriers.recordCommands(m_cmd);

    // Create image views covering the requested subresourcs
    DxvkImageViewCreateInfo dstViewInfo;
    dstViewInfo.type = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    dstViewInfo.format = dstImage->info().format;
    dstViewInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    dstViewInfo.aspect = region.dstSubresource.aspectMask;
    dstViewInfo.minLevel = region.dstSubresource.mipLevel;
    dstViewInfo.numLevels = 1;
    dstViewInfo.minLayer = region.dstSubresource.baseArrayLayer;
    dstViewInfo.numLayers = region.dstSubresource.layerCount;

    DxvkImageViewCreateInfo srcViewInfo;
    srcViewInfo.type = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    srcViewInfo.format = srcImage->info().format;
    srcViewInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    srcViewInfo.aspect = region.srcSubresource.aspectMask;
    srcViewInfo.minLevel = region.srcSubresource.mipLevel;
    srcViewInfo.numLevels = 1;
    srcViewInfo.minLayer = region.srcSubresource.baseArrayLayer;
    srcViewInfo.numLayers = region.srcSubresource.layerCount;

    Rc<DxvkImageView> dstImageView = m_device->createImageView(dstImage, dstViewInfo);
    Rc<DxvkImageView> srcImageView = m_device->createImageView(srcImage, srcViewInfo);

    // Create a framebuffer for the resolve op
    VkExtent3D passExtent = dstImageView->mipLevelExtent(0);

    Rc<DxvkMetaResolveRenderPass> fb = new DxvkMetaResolveRenderPass(
      m_device->vkd(), dstImageView, srcImageView, depthMode, stencilMode);

    VkRenderPassBeginInfo info;
    info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    info.pNext = nullptr;
    info.renderPass = fb->renderPass();
    info.framebuffer = fb->framebuffer();
    info.renderArea.offset = { 0, 0 };
    info.renderArea.extent = { passExtent.width, passExtent.height };
    info.clearValueCount = 0;
    info.pClearValues = nullptr;

    m_cmd->cmdBeginRenderPass(&info, VK_SUBPASS_CONTENTS_INLINE);
    m_cmd->cmdEndRenderPass();

    m_cmd->trackResource<DxvkAccess::Write>(dstImage);
    m_cmd->trackResource<DxvkAccess::Read>(srcImage);
    m_cmd->trackResource<DxvkAccess::None>(fb);
  }


  void DxvkContext::resolveImageFb(
    const Rc<DxvkImage>& dstImage,
    const Rc<DxvkImage>& srcImage,
    const VkImageResolve& region,
    VkFormat                  format,
    VkResolveModeFlagBitsKHR  depthMode,
    VkResolveModeFlagBitsKHR  stencilMode) {
    ScopedCpuProfileZone();
    auto dstSubresourceRange = vk::makeSubresourceRange(region.dstSubresource);
    auto srcSubresourceRange = vk::makeSubresourceRange(region.srcSubresource);

    if (m_execBarriers.isImageDirty(dstImage, dstSubresourceRange, DxvkAccess::Write)
      || m_execBarriers.isImageDirty(srcImage, srcSubresourceRange, DxvkAccess::Write))
      m_execBarriers.recordCommands(m_cmd);

    // We might have to transition the source image layout
    VkImageLayout srcLayout = srcImage->pickLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    if (srcImage->info().layout != srcLayout) {
      m_execAcquires.accessImage(
        srcImage, srcSubresourceRange,
        srcImage->info().layout,
        srcImage->info().stages, 0,
        srcLayout,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        VK_ACCESS_SHADER_READ_BIT);

      m_execAcquires.recordCommands(m_cmd);
    }

    // Create image views covering the requested subresourcs
    DxvkImageViewCreateInfo dstViewInfo;
    dstViewInfo.type = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    dstViewInfo.format = format ? format : dstImage->info().format;
    dstViewInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    dstViewInfo.aspect = region.dstSubresource.aspectMask;
    dstViewInfo.minLevel = region.dstSubresource.mipLevel;
    dstViewInfo.numLevels = 1;
    dstViewInfo.minLayer = region.dstSubresource.baseArrayLayer;
    dstViewInfo.numLayers = region.dstSubresource.layerCount;

    if (region.dstSubresource.aspectMask & VK_IMAGE_ASPECT_DEPTH_BIT)
      dstViewInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;

    DxvkImageViewCreateInfo srcViewInfo;
    srcViewInfo.type = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    srcViewInfo.format = format ? format : srcImage->info().format;
    srcViewInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
    srcViewInfo.aspect = region.srcSubresource.aspectMask & (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_COLOR_BIT);
    srcViewInfo.minLevel = region.srcSubresource.mipLevel;
    srcViewInfo.numLevels = 1;
    srcViewInfo.minLayer = region.srcSubresource.baseArrayLayer;
    srcViewInfo.numLayers = region.srcSubresource.layerCount;

    Rc<DxvkImageView> dstImageView = m_device->createImageView(dstImage, dstViewInfo);
    Rc<DxvkImageView> srcImageView = m_device->createImageView(srcImage, srcViewInfo);
    Rc<DxvkImageView> srcStencilView = nullptr;

    if ((region.dstSubresource.aspectMask & VK_IMAGE_ASPECT_STENCIL_BIT) && stencilMode != VK_RESOLVE_MODE_NONE_KHR) {
      srcViewInfo.aspect = VK_IMAGE_ASPECT_STENCIL_BIT;
      srcStencilView = m_device->createImageView(srcImage, srcViewInfo);
    }

    // Create a framebuffer and pipeline for the resolve op
    VkExtent3D passExtent = dstImageView->mipLevelExtent(0);

    Rc<DxvkMetaResolveRenderPass> fb = new DxvkMetaResolveRenderPass(
      m_device->vkd(), dstImageView, srcImageView, srcStencilView,
      dstImage->isFullSubresource(region.dstSubresource, region.extent));

    auto pipeInfo = m_common->metaResolve().getPipeline(
      dstViewInfo.format, srcImage->info().sampleCount, depthMode, stencilMode);

    VkDescriptorImageInfo descriptorImage;
    descriptorImage.sampler = VK_NULL_HANDLE;
    descriptorImage.imageView = srcImageView->handle();
    descriptorImage.imageLayout = srcLayout;

    VkWriteDescriptorSet descriptorWrite;
    descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrite.pNext = nullptr;
    descriptorWrite.dstBinding = 0;
    descriptorWrite.dstArrayElement = 0;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorWrite.pImageInfo = &descriptorImage;
    descriptorWrite.pBufferInfo = nullptr;
    descriptorWrite.pTexelBufferView = nullptr;

    // NV-DXVK start: use EXT_debug_utils
    descriptorWrite.dstSet = allocateDescriptorSet(pipeInfo.dsetLayout, "DxvkContext::resolveImageFb");
    // NV-DXVK end
    m_cmd->updateDescriptorSets(1, &descriptorWrite);

    if (srcStencilView != nullptr) {
      descriptorWrite.dstBinding = 1;
      descriptorImage.imageView = srcStencilView->handle();
      m_cmd->updateDescriptorSets(1, &descriptorWrite);
    }

    VkViewport viewport;
    viewport.x = float(region.dstOffset.x);
    viewport.y = float(region.dstOffset.y);
    viewport.width = float(region.extent.width);
    viewport.height = float(region.extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor;
    scissor.offset = { region.dstOffset.x,  region.dstOffset.y };
    scissor.extent = { region.extent.width, region.extent.height };

    VkRenderPassBeginInfo info;
    info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    info.pNext = nullptr;
    info.renderPass = fb->renderPass();
    info.framebuffer = fb->framebuffer();
    info.renderArea.offset = { 0, 0 };
    info.renderArea.extent = { passExtent.width, passExtent.height };
    info.clearValueCount = 0;
    info.pClearValues = nullptr;

    // Perform the actual resolve operation
    VkOffset2D srcOffset = {
      region.srcOffset.x - region.dstOffset.x,
      region.srcOffset.y - region.dstOffset.y };

    m_cmd->cmdBeginRenderPass(&info, VK_SUBPASS_CONTENTS_INLINE);
    m_cmd->cmdBindPipeline(VK_PIPELINE_BIND_POINT_GRAPHICS, pipeInfo.pipeHandle);
    m_cmd->cmdBindDescriptorSet(VK_PIPELINE_BIND_POINT_GRAPHICS,
      pipeInfo.pipeLayout, descriptorWrite.dstSet, 0, nullptr);
    m_cmd->cmdSetViewport(0, 1, &viewport);
    m_cmd->cmdSetScissor(0, 1, &scissor);
    m_cmd->cmdPushConstants(pipeInfo.pipeLayout,
      VK_SHADER_STAGE_FRAGMENT_BIT,
      0, sizeof(srcOffset), &srcOffset);
    m_cmd->cmdDraw(3, region.dstSubresource.layerCount, 0, 0);
    m_cmd->cmdEndRenderPass();

    if (srcImage->info().layout != srcLayout) {
      m_execBarriers.accessImage(
        srcImage, srcSubresourceRange, srcLayout,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
        srcImage->info().layout,
        srcImage->info().stages,
        srcImage->info().access);
    }

    m_cmd->trackResource<DxvkAccess::Write>(dstImage);
    m_cmd->trackResource<DxvkAccess::Read>(srcImage);
    m_cmd->trackResource<DxvkAccess::None>(fb);
  }


  void DxvkContext::startRenderPass() {
    ScopedCpuProfileZone();
    if (!m_flags.test(DxvkContextFlag::GpRenderPassBound)) {
      this->applyRenderTargetLoadLayouts();
      this->flushClears(true);

      m_flags.set(DxvkContextFlag::GpRenderPassBound);
      m_flags.clr(DxvkContextFlag::GpRenderPassSuspended);

      m_execBarriers.recordCommands(m_cmd);

      this->renderPassBindFramebuffer(
        m_state.om.framebufferInfo,
        m_state.om.renderPassOps,
        m_state.om.framebufferInfo.numAttachments(),
        m_state.om.clearValues.data());

      // Track the final layout of each render target
      this->applyRenderTargetStoreLayouts();

      // Don't discard image contents if we have
      // to spill the current render pass
      this->resetRenderPassOps(
        m_state.om.renderTargets,
        m_state.om.renderPassOps);

      // Begin occlusion queries
      m_queryManager.beginQueries(m_cmd, VK_QUERY_TYPE_OCCLUSION);
      m_queryManager.beginQueries(m_cmd, VK_QUERY_TYPE_PIPELINE_STATISTICS);
    }
  }
  
  
  void DxvkContext::spillRenderPass(bool suspend) {
    ScopedCpuProfileZone();
    if (m_flags.test(DxvkContextFlag::GpRenderPassBound)) {
      m_flags.clr(DxvkContextFlag::GpRenderPassBound);

      this->pauseTransformFeedback();

      m_queryManager.endQueries(m_cmd, VK_QUERY_TYPE_OCCLUSION);
      m_queryManager.endQueries(m_cmd, VK_QUERY_TYPE_PIPELINE_STATISTICS);

      this->renderPassUnbindFramebuffer();

      if (suspend)
        m_flags.set(DxvkContextFlag::GpRenderPassSuspended);
      else
        this->transitionRenderTargetLayouts(m_gfxBarriers, false);

      m_gfxBarriers.recordCommands(m_cmd);

      this->unbindGraphicsPipeline();
      this->unbindRaytracingPipeline();
    } else if (!suspend) {
      // We may end a previously suspended render pass
      if (m_flags.test(DxvkContextFlag::GpRenderPassSuspended)) {
        m_flags.clr(DxvkContextFlag::GpRenderPassSuspended);
        this->transitionRenderTargetLayouts(m_gfxBarriers, false);
        m_gfxBarriers.recordCommands(m_cmd);
      }

      // Execute deferred clears if necessary
      this->flushClears(false);
    }
  }


  void DxvkContext::renderPassBindFramebuffer(
    const DxvkFramebufferInfo&  framebufferInfo,
    const DxvkRenderPassOps&    ops,
          uint32_t              clearValueCount,
    const VkClearValue*         clearValues) {
    ScopedCpuProfileZone();
    const DxvkFramebufferSize fbSize = framebufferInfo.size();

    Rc<DxvkFramebuffer> framebuffer = this->lookupFramebuffer(framebufferInfo);

    VkRect2D renderArea;
    renderArea.offset = VkOffset2D{ 0, 0 };
    renderArea.extent = VkExtent2D{ fbSize.width, fbSize.height };

    VkRenderPassBeginInfo info;
    info.sType                = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    info.pNext                = nullptr;
    info.renderPass           = framebufferInfo.renderPass()->getHandle(ops);
    info.framebuffer          = framebuffer->handle();
    info.renderArea           = renderArea;
    info.clearValueCount      = clearValueCount;
    info.pClearValues         = clearValues;

    m_cmd->cmdBeginRenderPass(&info,
      VK_SUBPASS_CONTENTS_INLINE);

    m_cmd->trackResource<DxvkAccess::None>(framebuffer);

    for (uint32_t i = 0; i < framebufferInfo.numAttachments(); i++) {
      m_cmd->trackResource<DxvkAccess::None> (framebufferInfo.getAttachment(i).view);
      m_cmd->trackResource<DxvkAccess::Write>(framebufferInfo.getAttachment(i).view->image());
    }

    m_cmd->addStatCtr(DxvkStatCounter::CmdRenderPassCount, 1);
  }


  void DxvkContext::renderPassUnbindFramebuffer() {
    ScopedCpuProfileZone();
    m_cmd->cmdEndRenderPass();
  }


  void DxvkContext::resetRenderPassOps(
    const DxvkRenderTargets&    renderTargets,
          DxvkRenderPassOps&    renderPassOps) {
    ScopedCpuProfileZone();
    VkAccessFlags access = 0;

    if (renderTargets.depth.view != nullptr) {
      renderPassOps.depthOps = DxvkDepthAttachmentOps {
        VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_LOAD_OP_LOAD,
        renderTargets.depth.layout, renderTargets.depth.layout };

      access |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;

      if (renderTargets.depth.layout != VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL)
        access |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    } else {
      renderPassOps.depthOps = DxvkDepthAttachmentOps { };
    }

    for (uint32_t i = 0; i < MaxNumRenderTargets; i++) {
      if (renderTargets.color[i].view != nullptr) {
        renderPassOps.colorOps[i] = DxvkColorAttachmentOps{
            VK_ATTACHMENT_LOAD_OP_LOAD,
            renderTargets.color[i].layout,
            renderTargets.color[i].layout };

        access |= VK_ACCESS_COLOR_ATTACHMENT_READ_BIT
               |  VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
      } else {
        renderPassOps.colorOps[i] = DxvkColorAttachmentOps { };
      }
    }

    renderPassOps.barrier.srcStages = VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT;
    renderPassOps.barrier.srcAccess = access;
    renderPassOps.barrier.dstStages = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    renderPassOps.barrier.dstAccess = access;
  }


  void DxvkContext::startTransformFeedback() {
    ScopedCpuProfileZone();
    if (!m_flags.test(DxvkContextFlag::GpXfbActive)) {
      m_flags.set(DxvkContextFlag::GpXfbActive);

      VkBuffer     ctrBuffers[MaxNumXfbBuffers];
      VkDeviceSize ctrOffsets[MaxNumXfbBuffers];

      for (uint32_t i = 0; i < MaxNumXfbBuffers; i++) {
        auto physSlice = m_state.xfb.counters[i].getSliceHandle();

        ctrBuffers[i] = physSlice.handle;
        ctrOffsets[i] = physSlice.offset;

        if (physSlice.handle != VK_NULL_HANDLE)
          m_cmd->trackResource<DxvkAccess::Read>(m_state.xfb.counters[i].buffer());
      }

      m_cmd->cmdBeginTransformFeedback(
        0, MaxNumXfbBuffers, ctrBuffers, ctrOffsets);

      m_queryManager.beginQueries(m_cmd,
        VK_QUERY_TYPE_TRANSFORM_FEEDBACK_STREAM_EXT);
    }
  }


  void DxvkContext::pauseTransformFeedback() {
    if (m_flags.test(DxvkContextFlag::GpXfbActive)) {
      ScopedCpuProfileZone();
      m_flags.clr(DxvkContextFlag::GpXfbActive);

      VkBuffer     ctrBuffers[MaxNumXfbBuffers];
      VkDeviceSize ctrOffsets[MaxNumXfbBuffers];

      for (uint32_t i = 0; i < MaxNumXfbBuffers; i++) {
        auto physSlice = m_state.xfb.counters[i].getSliceHandle();

        ctrBuffers[i] = physSlice.handle;
        ctrOffsets[i] = physSlice.offset;

        if (physSlice.handle != VK_NULL_HANDLE)
          m_cmd->trackResource<DxvkAccess::Write>(m_state.xfb.counters[i].buffer());
      }

      m_queryManager.endQueries(m_cmd,
        VK_QUERY_TYPE_TRANSFORM_FEEDBACK_STREAM_EXT);

      m_cmd->cmdEndTransformFeedback(
        0, MaxNumXfbBuffers, ctrBuffers, ctrOffsets);
    }
  }


  void DxvkContext::unbindComputePipeline() {
    ScopedCpuProfileZone();
    m_flags.set(
      DxvkContextFlag::CpDirtyPipeline,
      DxvkContextFlag::CpDirtyPipelineState,
      DxvkContextFlag::CpDirtyResources);

    m_cpActivePipeline = VK_NULL_HANDLE;
  }


  bool DxvkContext::updateComputePipeline() {
    ScopedCpuProfileZone();
    m_state.cp.pipeline = lookupComputePipeline(m_state.cp.shaders);

    if (unlikely(m_state.cp.pipeline == nullptr))
      return false;

    if (m_state.cp.pipeline->layout()->pushConstRange().size)
      m_flags.set(DxvkContextFlag::DirtyPushConstants);

    m_flags.clr(DxvkContextFlag::CpDirtyPipeline);
    return true;
  }


  bool DxvkContext::updateComputePipelineState() {
    ScopedCpuProfileZone();
    m_cpActivePipeline = m_state.cp.pipeline->getPipelineHandle(m_state.cp.state);

      if (unlikely(!m_cpActivePipeline))
          return false;

      m_cmd->cmdBindPipeline(
          VK_PIPELINE_BIND_POINT_COMPUTE,
          m_cpActivePipeline);

      m_flags.clr(DxvkContextFlag::CpDirtyPipelineState);
      return true;
  }

  void DxvkContext::unbindRaytracingPipeline() {
      m_flags.set(
          DxvkContextFlag::RpDirtyPipeline,
          DxvkContextFlag::RpDirtyPipelineState,
          DxvkContextFlag::RpDirtyResources);

      m_state.rp.pipeline = nullptr;
      m_rpActivePipeline = VK_NULL_HANDLE;
  }


  bool DxvkContext::updateRaytracingPipeline() {
    ScopedCpuProfileZone();
    m_state.rp.pipeline = lookupRaytracingPipeline(m_state.rp.shaders);

    if (unlikely(m_state.rp.pipeline == nullptr))
      return false;

    if (m_state.rp.pipeline->layout()->pushConstRange().size)
      m_flags.set(DxvkContextFlag::DirtyPushConstants);

    m_flags.clr(DxvkContextFlag::RpDirtyPipeline);
    return true;
  }


  bool DxvkContext::updateRaytracingPipelineState() {
    ScopedCpuProfileZone();

    m_rpActivePipeline = m_state.rp.pipeline->getPipelineHandle();

    if (unlikely(!m_rpActivePipeline))
      return false;

    m_cmd->cmdBindPipeline(
      VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR,
      m_rpActivePipeline);

    m_flags.clr(DxvkContextFlag::RpDirtyPipelineState);

    return true;
  }


  void DxvkContext::unbindGraphicsPipeline() {
    ScopedCpuProfileZone();
    m_flags.set(
      DxvkContextFlag::GpDirtyPipeline,
      DxvkContextFlag::GpDirtyPipelineState,
      DxvkContextFlag::GpDirtyResources,
      DxvkContextFlag::GpDirtyVertexBuffers,
      DxvkContextFlag::GpDirtyIndexBuffer,
      DxvkContextFlag::GpDirtyXfbBuffers,
      DxvkContextFlag::GpDirtyBlendConstants,
      DxvkContextFlag::GpDirtyStencilRef,
      DxvkContextFlag::GpDirtyViewport,
      DxvkContextFlag::GpDirtyDepthBias,
      DxvkContextFlag::GpDirtyDepthBounds);

    m_gpActivePipeline = VK_NULL_HANDLE;
  }


  bool DxvkContext::updateGraphicsPipeline() {
    ScopedCpuProfileZone();
    m_state.gp.pipeline = lookupGraphicsPipeline(m_state.gp.shaders);

    if (unlikely(m_state.gp.pipeline == nullptr)) {
      m_state.gp.flags = DxvkGraphicsPipelineFlags();
      return false;
    }

    if (m_state.gp.flags != m_state.gp.pipeline->flags()) {
      m_state.gp.flags = m_state.gp.pipeline->flags();

      // Force-update vertex/index buffers for hazard checks
      m_flags.set(DxvkContextFlag::GpDirtyIndexBuffer,
        DxvkContextFlag::GpDirtyVertexBuffers,
        DxvkContextFlag::GpDirtyXfbBuffers,
        DxvkContextFlag::DirtyDrawBuffer);

      // This is necessary because we'll only do hazard
      // tracking if the active pipeline has side effects
      if (!m_barrierControl.test(DxvkBarrierControl::IgnoreGraphicsBarriers))
        this->spillRenderPass(true);
    }

    if (m_state.gp.pipeline->layout()->pushConstRange().size)
      m_flags.set(DxvkContextFlag::DirtyPushConstants);

    m_flags.clr(DxvkContextFlag::GpDirtyPipeline);
    return true;
  }


  bool DxvkContext::updateGraphicsPipelineState() {
    ScopedCpuProfileZone();
    // Set up vertex buffer strides for active bindings
    for (uint32_t i = 0; i < m_state.gp.state.il.bindingCount(); i++) {
      const uint32_t binding = m_state.gp.state.ilBindings[i].binding();
      m_state.gp.state.ilBindings[i].setStride(m_state.vi.vertexStrides[binding]);
    }

    // Check which dynamic states need to be active. States that
    // are not dynamic will be invalidated in the command buffer.
    m_flags.clr(DxvkContextFlag::GpDynamicBlendConstants,
      DxvkContextFlag::GpDynamicDepthBias,
      DxvkContextFlag::GpDynamicDepthBounds,
      DxvkContextFlag::GpDynamicStencilRef);

    m_flags.set(m_state.gp.state.useDynamicBlendConstants()
      ? DxvkContextFlag::GpDynamicBlendConstants
      : DxvkContextFlag::GpDirtyBlendConstants);

    m_flags.set(m_state.gp.state.useDynamicDepthBias()
      ? DxvkContextFlag::GpDynamicDepthBias
      : DxvkContextFlag::GpDirtyDepthBias);

    m_flags.set(m_state.gp.state.useDynamicDepthBounds()
      ? DxvkContextFlag::GpDynamicDepthBounds
      : DxvkContextFlag::GpDirtyDepthBounds);

    m_flags.set(m_state.gp.state.useDynamicStencilRef()
      ? DxvkContextFlag::GpDynamicStencilRef
      : DxvkContextFlag::GpDirtyStencilRef);

    // Retrieve and bind actual Vulkan pipeline handle
    m_gpActivePipeline = m_state.gp.pipeline->getPipelineHandle(
      m_state.gp.state, m_state.om.framebufferInfo.renderPass());

    if (unlikely(!m_gpActivePipeline))
      return false;

    m_cmd->cmdBindPipeline(
      VK_PIPELINE_BIND_POINT_GRAPHICS,
      m_gpActivePipeline);

    m_flags.clr(DxvkContextFlag::GpDirtyPipelineState);
    return true;
  }


  void DxvkContext::updateComputeShaderResources() {
    ScopedCpuProfileZone();
    if ((m_flags.test(DxvkContextFlag::CpDirtyResources))
      || (m_state.cp.pipeline->layout()->hasStaticBufferBindings()))
      this->updateShaderResources<VK_PIPELINE_BIND_POINT_COMPUTE>(m_state.cp.pipeline->layout());

    this->updateShaderDescriptorSetBinding<VK_PIPELINE_BIND_POINT_COMPUTE>(
      m_cpSet, m_state.cp.pipeline->layout());

    m_flags.clr(DxvkContextFlag::CpDirtyResources,
      DxvkContextFlag::CpDirtyDescriptorBinding);
  }


  void DxvkContext::updateRaytracingShaderResources() {
    ScopedCpuProfileZone();
    if ((m_flags.test(DxvkContextFlag::RpDirtyResources))
      || (m_state.rp.pipeline->layout()->hasStaticBufferBindings()))
      this->updateShaderResources<VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR>(m_state.rp.pipeline->layout());

    this->updateShaderDescriptorSetBinding<VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR>(
      m_rpSet, m_state.rp.pipeline->layout());

    m_flags.clr(DxvkContextFlag::RpDirtyResources,
      DxvkContextFlag::RpDirtyDescriptorBinding);
  }


  void DxvkContext::updateGraphicsShaderResources() {
    ScopedCpuProfileZone();
    if ((m_flags.test(DxvkContextFlag::GpDirtyResources))
      || (m_state.gp.pipeline->layout()->hasStaticBufferBindings()))
      this->updateShaderResources<VK_PIPELINE_BIND_POINT_GRAPHICS>(m_state.gp.pipeline->layout());

    this->updateShaderDescriptorSetBinding<VK_PIPELINE_BIND_POINT_GRAPHICS>(
      m_gpSet, m_state.gp.pipeline->layout());

    m_flags.clr(DxvkContextFlag::GpDirtyResources,
      DxvkContextFlag::GpDirtyDescriptorBinding);
  }


  template<VkPipelineBindPoint BindPoint>
  void DxvkContext::updateShaderResources(const DxvkPipelineLayout* layout) {
    ScopedCpuProfileZone();
    std::array<DxvkDescriptorInfo, MaxNumActiveBindings> descriptors;

    // Assume that all bindings are active as a fast path
    DxvkBindingMask bindMask;
    bindMask.setFirst(layout->bindingCount());

    std::vector<VkWriteDescriptorSet> writeRecords;
    std::vector<VkDescriptorImageInfo*> imageInfoList;
    std::vector<VkDescriptorBufferInfo*> bufferInfoList;

    for (uint32_t i = 0; i < layout->bindingCount(); i++) {
      const auto& binding = layout->binding(i);
      const auto& res = m_rc[binding.slot];

      switch (binding.type) {
        case VK_DESCRIPTOR_TYPE_SAMPLER:
          if (res.sampler != nullptr) {
            descriptors[i].image.sampler     = res.sampler->handle();
            descriptors[i].image.imageView   = VK_NULL_HANDLE;
            descriptors[i].image.imageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            
            if (m_rcTracked.set(binding.slot))
              m_cmd->trackResource<DxvkAccess::None>(res.sampler);
          } else {
            descriptors[i].image = m_common->dummyResources().samplerDescriptor();
          } break;
        
        case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
          if (res.imageView != nullptr && res.imageView->handle(binding.view) != VK_NULL_HANDLE) {
            descriptors[i].image.sampler     = VK_NULL_HANDLE;
            descriptors[i].image.imageView   = res.imageView->handle(binding.view);
            descriptors[i].image.imageLayout = res.imageView->imageInfo().layout;
            
            if (m_rcTracked.set(binding.slot)) {
              m_cmd->trackResource<DxvkAccess::None>(res.imageView);
              m_cmd->trackResource<DxvkAccess::Read>(res.imageView->image());
            }
          } else {
            bindMask.clr(i);
            // NV-DXVK start: use null handles instead of dummy resources
            descriptors[i].image = { VK_NULL_HANDLE };
            // NV-DXVK end
          } break;
        
        case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
          if (res.imageView != nullptr && res.imageView->handle(binding.view) != VK_NULL_HANDLE) {
            descriptors[i].image.sampler     = VK_NULL_HANDLE;
            descriptors[i].image.imageView   = res.imageView->handle(binding.view);
            descriptors[i].image.imageLayout = res.imageView->imageInfo().layout;
            
            if (m_rcTracked.set(binding.slot)) {
              m_cmd->trackResource<DxvkAccess::None>(res.imageView);
              m_cmd->trackResource<DxvkAccess::Write>(res.imageView->image());
            }
          } else {
            bindMask.clr(i);
            // NV-DXVK start: use null handles instead of dummy resources
            descriptors[i].image = { VK_NULL_HANDLE };
            // NV-DXVK end
          } break;
        
        case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
          if (res.sampler != nullptr && res.imageView != nullptr
           && res.imageView->handle(binding.view) != VK_NULL_HANDLE) {
            descriptors[i].image.sampler     = res.sampler->handle();
            descriptors[i].image.imageView   = res.imageView->handle(binding.view);
            descriptors[i].image.imageLayout = res.imageView->imageInfo().layout;
            
            if (m_rcTracked.set(binding.slot)) {
              m_cmd->trackResource<DxvkAccess::None>(res.sampler);
              m_cmd->trackResource<DxvkAccess::None>(res.imageView);
              m_cmd->trackResource<DxvkAccess::Read>(res.imageView->image());
            }
          } else {
            bindMask.clr(i);
            // NV-DXVK start: use null handles instead of dummy resources
            descriptors[i].image = m_common->dummyResources().samplerDescriptor();
            // NV-DXVK end
          } 
          break;
        
        case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
          if (res.bufferView != nullptr) {
            res.bufferView->updateView();
            descriptors[i].texelBuffer = res.bufferView->handle();
            
            if (m_rcTracked.set(binding.slot)) {
              m_cmd->trackResource<DxvkAccess::None>(res.bufferView);
              m_cmd->trackResource<DxvkAccess::Read>(res.bufferView->buffer());
            }
          } else {
            bindMask.clr(i);
            descriptors[i].texelBuffer = m_common->dummyResources().bufferViewDescriptor();
          } break;
        
        case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
          if (res.bufferView != nullptr) {
            res.bufferView->updateView();
            descriptors[i].texelBuffer = res.bufferView->handle();
            
            if (m_rcTracked.set(binding.slot)) {
              m_cmd->trackResource<DxvkAccess::None>(res.bufferView);
              m_cmd->trackResource<DxvkAccess::Write>(res.bufferView->buffer());
            }
          } else {
            bindMask.clr(i);
            descriptors[i].texelBuffer = m_common->dummyResources().bufferViewDescriptor();
          } break;
        
        case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
          if (res.bufferSlice.defined()) {
            descriptors[i] = res.bufferSlice.getDescriptor();
            
            if (m_rcTracked.set(binding.slot))
              m_cmd->trackResource<DxvkAccess::Read>(res.bufferSlice.buffer());
          } else {
            bindMask.clr(i);
            descriptors[i].buffer = m_common->dummyResources().bufferDescriptor();
          } break;
        
        case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
          if (res.bufferSlice.defined()) {
            descriptors[i] = res.bufferSlice.getDescriptor();
            
            if (m_rcTracked.set(binding.slot))
              m_cmd->trackResource<DxvkAccess::Write>(res.bufferSlice.buffer());
          } else {
            bindMask.clr(i);
            descriptors[i].buffer = m_common->dummyResources().bufferDescriptor();
          }
          break;
        
        case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
          if (res.bufferSlice.defined()) {
            descriptors[i] = res.bufferSlice.getDescriptor();
            descriptors[i].buffer.offset = 0;
            
            if (m_rcTracked.set(binding.slot))
              m_cmd->trackResource<DxvkAccess::Read>(res.bufferSlice.buffer());
          } else {
            bindMask.clr(i);
            descriptors[i].buffer = m_common->dummyResources().bufferDescriptor();
          } break;
        
        case VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR:
          if (res.tlas != VK_NULL_HANDLE) {
            descriptors[i].accelerationStructure = res.tlas;
          } else {
            bindMask.clr(i);
            descriptors[i].accelerationStructure = VK_NULL_HANDLE;
          } break;

        default:
          Logger::err(str::format("DxvkContext: Unhandled descriptor type: ", binding.type));
      }
    }

    // Allocate and update descriptor set
    auto& set = 
      BindPoint == VK_PIPELINE_BIND_POINT_GRAPHICS 
      ? m_gpSet 
      : BindPoint == VK_PIPELINE_BIND_POINT_COMPUTE
        ? m_cpSet
        : m_rpSet;

    if (layout->bindingCount()) {
      // NV-DXVK start: use EXT_debug_utils
      set = allocateDescriptorSet(layout->descriptorSetLayout(), "DxvkContext::updateShaderResources");
      // NV-DXVK end

      for (auto& record: writeRecords) {
        record.dstSet = set;
      }

      m_cmd->updateDescriptorSetWithTemplate(set,
        layout->descriptorTemplate(), descriptors.data());

      if (writeRecords.size() > 0) {
        m_cmd->updateDescriptorSets(writeRecords.size(), &writeRecords[0]);
      }
    }
    else {
      set = VK_NULL_HANDLE;
    }

    for (auto ptr: imageInfoList) {
      delete[] ptr;
    }
    for (auto ptr : bufferInfoList) {
      delete[] ptr;
    }

    // Select the active binding mask to update
    auto& refMask = 
      BindPoint == VK_PIPELINE_BIND_POINT_GRAPHICS
      ? m_state.gp.state.bsBindingMask
      : BindPoint == VK_PIPELINE_BIND_POINT_COMPUTE
        ? m_state.cp.state.bsBindingMask
        : m_state.rp.state.bsBindingMask;

    // If some resources are not bound, we may need to
    // update spec constants and rebind the pipeline
    if (refMask != bindMask) {
      refMask = bindMask;

      m_flags.set(
        BindPoint == VK_PIPELINE_BIND_POINT_GRAPHICS
        ? DxvkContextFlag::GpDirtyPipelineState
        : BindPoint == VK_PIPELINE_BIND_POINT_COMPUTE
          ? DxvkContextFlag::CpDirtyPipelineState
          : DxvkContextFlag::RpDirtyPipelineState);
    }
  }


  template<VkPipelineBindPoint BindPoint>
  void DxvkContext::updateShaderDescriptorSetBinding(
    VkDescriptorSet         set,
    const DxvkPipelineLayout* layout) {
    ScopedCpuProfileZone();
    if (set) {
      std::array<uint32_t, MaxNumActiveBindings> offsets;

      for (uint32_t i = 0; i < layout->dynamicBindingCount(); i++) {
        const auto& binding = layout->dynamicBinding(i);
        const auto& res = m_rc[binding.slot];

        offsets[i] = res.bufferSlice.defined()
          ? res.bufferSlice.getDynamicOffset()
          : 0;
      }

      m_cmd->cmdBindDescriptorSet(BindPoint,
        layout->pipelineLayout(), set,
        layout->dynamicBindingCount(),
        offsets.data());
    }
  }


  DxvkFramebufferInfo DxvkContext::makeFramebufferInfo(
    const DxvkRenderTargets&      renderTargets) {
    ScopedCpuProfileZone();
    auto renderPassFormat = DxvkFramebufferInfo::getRenderPassFormat(renderTargets);
    auto renderPassObject = m_common->renderPassPool().getRenderPass(renderPassFormat);

    return DxvkFramebufferInfo(renderTargets, m_device->getDefaultFramebufferSize(), renderPassObject);
  }


  void DxvkContext::updateFramebuffer() {
    ScopedCpuProfileZone();
    if (m_flags.test(DxvkContextFlag::GpDirtyFramebuffer)) {
      m_flags.clr(DxvkContextFlag::GpDirtyFramebuffer);

      this->spillRenderPass(true);

      DxvkFramebufferInfo fbInfo = makeFramebufferInfo(m_state.om.renderTargets);
      this->updateRenderTargetLayouts(fbInfo, m_state.om.framebufferInfo);

      m_state.gp.state.ms.setSampleCount(fbInfo.getSampleCount());
      m_state.om.framebufferInfo = fbInfo;

      for (uint32_t i = 0; i < MaxNumRenderTargets; i++) {
        const Rc<DxvkImageView>& attachment = fbInfo.getColorTarget(i).view;

        VkComponentMapping mapping = attachment != nullptr
          ? util::invertComponentMapping(attachment->info().swizzle)
          : VkComponentMapping();

        m_state.gp.state.omSwizzle[i] = DxvkOmAttachmentSwizzle(mapping);
      }

      m_flags.set(DxvkContextFlag::GpDirtyPipelineState);
    }
  }


  void DxvkContext::applyRenderTargetLoadLayouts() {
    ScopedCpuProfileZone();
    for (uint32_t i = 0; i < MaxNumRenderTargets; i++)
      m_state.om.renderPassOps.colorOps[i].loadLayout = m_rtLayouts.color[i];

    m_state.om.renderPassOps.depthOps.loadLayout = m_rtLayouts.depth;
  }


  void DxvkContext::applyRenderTargetStoreLayouts() {
    ScopedCpuProfileZone();
    for (uint32_t i = 0; i < MaxNumRenderTargets; i++)
      m_rtLayouts.color[i] = m_state.om.renderPassOps.colorOps[i].storeLayout;

    m_rtLayouts.depth = m_state.om.renderPassOps.depthOps.storeLayout;
  }


  void DxvkContext::transitionRenderTargetLayouts(
          DxvkBarrierSet&         barriers,
          bool                    sharedOnly) {
    ScopedCpuProfileZone();
    for (uint32_t i = 0; i < MaxNumRenderTargets; i++) {
      const DxvkAttachment& color = m_state.om.framebufferInfo.getColorTarget(i);

      if (color.view != nullptr && (!sharedOnly || color.view->imageInfo().shared)) {
        this->transitionColorAttachment(barriers, color, m_rtLayouts.color[i]);
        m_rtLayouts.color[i] = color.view->imageInfo().layout;
      }
    }

    const DxvkAttachment& depth = m_state.om.framebufferInfo.getDepthTarget();

    if (depth.view != nullptr && (!sharedOnly || depth.view->imageInfo().shared)) {
      this->transitionDepthAttachment(barriers, depth, m_rtLayouts.depth);
      m_rtLayouts.depth = depth.view->imageInfo().layout;
    }
  }


  void DxvkContext::transitionColorAttachment(
          DxvkBarrierSet&         barriers,
    const DxvkAttachment&         attachment,
          VkImageLayout           oldLayout) {
    ScopedCpuProfileZone();
    if (oldLayout != attachment.view->imageInfo().layout) {
      barriers.accessImage(
        attachment.view->image(),
        attachment.view->imageSubresources(), oldLayout,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        attachment.view->imageInfo().layout,
        attachment.view->imageInfo().stages,
        attachment.view->imageInfo().access);

      m_cmd->trackResource<DxvkAccess::Write>(attachment.view->image());
    }
  }


  void DxvkContext::transitionDepthAttachment(
          DxvkBarrierSet&         barriers,
    const DxvkAttachment&         attachment,
          VkImageLayout           oldLayout) {
    ScopedCpuProfileZone();
    if (oldLayout != attachment.view->imageInfo().layout) {
      barriers.accessImage(
        attachment.view->image(),
        attachment.view->imageSubresources(), oldLayout,
        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
        VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
        oldLayout != VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL
          ? VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT : 0,
        attachment.view->imageInfo().layout,
        attachment.view->imageInfo().stages,
        attachment.view->imageInfo().access);

      m_cmd->trackResource<DxvkAccess::Write>(attachment.view->image());
    }
  }


  void DxvkContext::updateRenderTargetLayouts(
    const DxvkFramebufferInfo&    newFb,
    const DxvkFramebufferInfo&    oldFb) {
    ScopedCpuProfileZone();
    DxvkRenderTargetLayouts layouts = { };

    for (uint32_t i = 0; i < MaxNumRenderTargets; i++) {
      if (newFb.getColorTarget(i).view != nullptr)
        layouts.color[i] = newFb.getColorTarget(i).view->imageInfo().layout;
    }

    if (newFb.getDepthTarget().view != nullptr)
      layouts.depth = newFb.getDepthTarget().view->imageInfo().layout;

    // Check whether any of the previous attachments have been moved
    // around or been rebound with a different view. This may help
    // reduce the number of image layout transitions between passes.
    for (uint32_t i = 0; i < MaxNumRenderTargets; i++) {
      const DxvkAttachment& oldAttachment = oldFb.getColorTarget(i);

      if (oldAttachment.view != nullptr) {
        bool found = false;

        for (uint32_t j = 0; j < MaxNumRenderTargets && !found; j++) {
          const DxvkAttachment& newAttachment = newFb.getColorTarget(j);

          found = newAttachment.view == oldAttachment.view || (newAttachment.view != nullptr
            && newAttachment.view->image()        == oldAttachment.view->image()
            && newAttachment.view->subresources() == oldAttachment.view->subresources());

          if (found)
            layouts.color[j] = m_rtLayouts.color[i];
        }

        if (!found && m_flags.test(DxvkContextFlag::GpRenderPassSuspended))
          this->transitionColorAttachment(m_execBarriers, oldAttachment, m_rtLayouts.color[i]);
      }
    }

    const DxvkAttachment& oldAttachment = oldFb.getDepthTarget();

    if (oldAttachment.view != nullptr) {
      const DxvkAttachment& newAttachment = newFb.getDepthTarget();

      bool found = newAttachment.view == oldAttachment.view || (newAttachment.view != nullptr
        && newAttachment.view->image()        == oldAttachment.view->image()
        && newAttachment.view->subresources() == oldAttachment.view->subresources());

      if (found)
        layouts.depth = m_rtLayouts.depth;
      else if (m_flags.test(DxvkContextFlag::GpRenderPassSuspended))
        this->transitionDepthAttachment(m_execBarriers, oldAttachment, m_rtLayouts.depth);
    }

    m_rtLayouts = layouts;
  }
  
  
  void DxvkContext::prepareImage(
          DxvkBarrierSet&         barriers,
    const Rc<DxvkImage>&          image,
    const VkImageSubresourceRange& subresources,
          bool                    flushClears) {
    ScopedCpuProfileZone();
    // Images that can't be used as attachments are always in their
    // default layout, so we don't have to do anything in this case
    if (!(image->info().usage & (VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)))
      return;

    // Flush clears if there are any since they may affect the image
    if (!m_deferredClears.empty() && flushClears)
      this->spillRenderPass(false);

    // All images are in their default layout for suspended passes
    if (!m_flags.test(DxvkContextFlag::GpRenderPassSuspended))
      return;

    // 3D images require special care because they only have one
    // layer, but views may address individual 2D slices as layers
    bool is3D = image->info().type == VK_IMAGE_TYPE_3D;

    // Transition any attachment with overlapping subresources
    if (image->info().usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) {
      for (uint32_t i = 0; i < MaxNumRenderTargets; i++) {
        const DxvkAttachment& attachment = m_state.om.framebufferInfo.getColorTarget(i);

        if (attachment.view != nullptr && attachment.view->image() == image
         && (is3D || vk::checkSubresourceRangeOverlap(attachment.view->subresources(), subresources))) {
          this->transitionColorAttachment(barriers, attachment, m_rtLayouts.color[i]);
          m_rtLayouts.color[i] = image->info().layout;
        }
      }
    } else {
      const DxvkAttachment& attachment = m_state.om.framebufferInfo.getDepthTarget();

      if (attachment.view != nullptr && attachment.view->image() == image
       && (is3D || vk::checkSubresourceRangeOverlap(attachment.view->subresources(), subresources))) {
        this->transitionDepthAttachment(barriers, attachment, m_rtLayouts.depth);
        m_rtLayouts.depth = image->info().layout;
      }
    }
  }

  bool DxvkContext::updateIndexBufferBinding() {
    ScopedCpuProfileZone();
    if (unlikely(!m_state.vi.indexBuffer.defined()))
      return false;

    m_flags.clr(DxvkContextFlag::GpDirtyIndexBuffer);
    auto bufferInfo = m_state.vi.indexBuffer.getDescriptor();

    m_cmd->cmdBindIndexBuffer(
      bufferInfo.buffer.buffer,
      bufferInfo.buffer.offset,
      m_state.vi.indexType);

    if (m_vbTracked.set(MaxNumVertexBindings))
      m_cmd->trackResource<DxvkAccess::Read>(m_state.vi.indexBuffer.buffer());

    return true;
  }


  void DxvkContext::updateVertexBufferBindings() {
    ScopedCpuProfileZone();
    m_flags.clr(DxvkContextFlag::GpDirtyVertexBuffers);

    if (unlikely(!m_state.gp.state.il.bindingCount()))
      return;

    std::array<VkBuffer, MaxNumVertexBindings> buffers;
    std::array<VkDeviceSize, MaxNumVertexBindings> offsets;
    std::array<VkDeviceSize, MaxNumVertexBindings> lengths;

    // Set buffer handles and offsets for active bindings
    for (uint32_t i = 0; i < m_state.gp.state.il.bindingCount(); i++) {
      uint32_t binding = m_state.gp.state.ilBindings[i].binding();

      if (likely(m_state.vi.vertexBuffers[binding].defined())) {
        auto vbo = m_state.vi.vertexBuffers[binding].getDescriptor();

        buffers[i] = vbo.buffer.buffer;
        offsets[i] = vbo.buffer.offset;
        lengths[i] = vbo.buffer.range;

        if (m_vbTracked.set(binding))
          m_cmd->trackResource<DxvkAccess::Read>(m_state.vi.vertexBuffers[binding].buffer());
      }
      else if (m_features.test(DxvkContextFeature::NullDescriptors)) {
        buffers[i] = m_common->dummyResources().bufferHandle();
        offsets[i] = 0;
        lengths[i] = 0;
      }
      else {
        buffers[i] = m_common->dummyResources().bufferHandle();
        offsets[i] = 0;
        lengths[i] = 0;
      }
    }

    // Vertex bindigs get remapped when compiling the
    // pipeline, so this actually does the right thing
    if (m_features.test(DxvkContextFeature::ExtendedDynamicState)) {
      m_cmd->cmdBindVertexBuffers2(0, m_state.gp.state.il.bindingCount(),
        buffers.data(), offsets.data(), lengths.data(), nullptr);
    }
    else {
      m_cmd->cmdBindVertexBuffers(0, m_state.gp.state.il.bindingCount(),
        buffers.data(), offsets.data());
    }
  }


  void DxvkContext::updateTransformFeedbackBuffers() {
    ScopedCpuProfileZone();
    auto gsOptions = m_state.gp.shaders.gs->shaderOptions();

    VkBuffer     xfbBuffers[MaxNumXfbBuffers];
    VkDeviceSize xfbOffsets[MaxNumXfbBuffers];
    VkDeviceSize xfbLengths[MaxNumXfbBuffers];

    for (size_t i = 0; i < MaxNumXfbBuffers; i++) {
      auto physSlice = m_state.xfb.buffers[i].getSliceHandle();

      xfbBuffers[i] = physSlice.handle;
      xfbOffsets[i] = physSlice.offset;
      xfbLengths[i] = physSlice.length;

      if (physSlice.handle == VK_NULL_HANDLE)
        xfbBuffers[i] = m_common->dummyResources().bufferHandle();

      if (physSlice.handle != VK_NULL_HANDLE) {
        const Rc<DxvkBuffer>& buffer = m_state.xfb.buffers[i].buffer();
        buffer->setXfbVertexStride(gsOptions.xfbStrides[i]);

        m_cmd->trackResource<DxvkAccess::Write>(buffer);
      }
    }

    m_cmd->cmdBindTransformFeedbackBuffers(
      0, MaxNumXfbBuffers,
      xfbBuffers, xfbOffsets, xfbLengths);
  }


  void DxvkContext::updateTransformFeedbackState() {
    ScopedCpuProfileZone();
    if (m_flags.test(DxvkContextFlag::GpDirtyXfbBuffers)) {
      m_flags.clr(DxvkContextFlag::GpDirtyXfbBuffers);

      this->pauseTransformFeedback();
      this->updateTransformFeedbackBuffers();
    }

    this->startTransformFeedback();
  }


  void DxvkContext::updateDynamicState() {
    ScopedCpuProfileZone();
    if (!m_gpActivePipeline)
      return;

    if (m_flags.test(DxvkContextFlag::GpDirtyViewport)) {
      m_flags.clr(DxvkContextFlag::GpDirtyViewport);

      uint32_t viewportCount = m_state.gp.state.rs.viewportCount();
      m_cmd->cmdSetViewport(0, viewportCount, m_state.vp.viewports.data());
      m_cmd->cmdSetScissor(0, viewportCount, m_state.vp.scissorRects.data());
    }

    if (m_flags.all(DxvkContextFlag::GpDirtyBlendConstants,
      DxvkContextFlag::GpDynamicBlendConstants)) {
      m_flags.clr(DxvkContextFlag::GpDirtyBlendConstants);
      m_cmd->cmdSetBlendConstants(&m_state.dyn.blendConstants.r);
    }

    if (m_flags.all(DxvkContextFlag::GpDirtyStencilRef,
      DxvkContextFlag::GpDynamicStencilRef)) {
      m_flags.clr(DxvkContextFlag::GpDirtyStencilRef);

      m_cmd->cmdSetStencilReference(
        VK_STENCIL_FRONT_AND_BACK,
        m_state.dyn.stencilReference);
    }

    if (m_flags.all(DxvkContextFlag::GpDirtyDepthBias,
      DxvkContextFlag::GpDynamicDepthBias)) {
      m_flags.clr(DxvkContextFlag::GpDirtyDepthBias);

      m_cmd->cmdSetDepthBias(
        m_state.dyn.depthBias.depthBiasConstant,
        m_state.dyn.depthBias.depthBiasClamp,
        m_state.dyn.depthBias.depthBiasSlope);
    }

    if (m_flags.all(DxvkContextFlag::GpDirtyDepthBounds,
      DxvkContextFlag::GpDynamicDepthBounds)) {
      m_flags.clr(DxvkContextFlag::GpDirtyDepthBounds);

      m_cmd->cmdSetDepthBounds(
        m_state.dyn.depthBounds.minDepthBounds,
        m_state.dyn.depthBounds.maxDepthBounds);
    }
  }


  template<VkPipelineBindPoint BindPoint>
  void DxvkContext::updatePushConstants() {
    ScopedCpuProfileZone();
    m_flags.clr(DxvkContextFlag::DirtyPushConstants);

    auto layout = 
      BindPoint == VK_PIPELINE_BIND_POINT_GRAPHICS
      ? m_state.gp.pipeline->layout()
      : BindPoint == VK_PIPELINE_BIND_POINT_COMPUTE
        ? m_state.cp.pipeline->layout()
        : m_state.rp.pipeline->layout();

    if (!layout)
      return;

    VkPushConstantRange pushConstRange = layout->pushConstRange();
    if (!pushConstRange.size)
      return;

    assert(m_state.pc.constantBank < DxvkPushConstantBank::Count);

    m_cmd->cmdPushConstants(
      layout->pipelineLayout(),
      pushConstRange.stageFlags,
      pushConstRange.offset,
      pushConstRange.size,
      &m_state.pc.data[(uint32_t)m_state.pc.constantBank][pushConstRange.offset]);
  }


  bool DxvkContext::commitComputeState() {
    ScopedCpuProfileZone();
    this->spillRenderPass(false);

    if (m_flags.test(DxvkContextFlag::CpDirtyPipeline)) {
      if (unlikely(!this->updateComputePipeline()))
        return false;
    }

    if (m_flags.any(
      DxvkContextFlag::CpDirtyResources,
      DxvkContextFlag::CpDirtyDescriptorBinding))
      this->updateComputeShaderResources();

    if (m_flags.test(DxvkContextFlag::CpDirtyPipelineState)) {
      if (unlikely(!this->updateComputePipelineState()))
        return false;
    }

    if (m_flags.test(DxvkContextFlag::DirtyPushConstants))
      this->updatePushConstants<VK_PIPELINE_BIND_POINT_COMPUTE>();

    return true;
  }


  bool DxvkContext::commitRaytracingState() {
    ScopedCpuProfileZone();
    this->spillRenderPass(false);

    if (m_flags.test(DxvkContextFlag::RpDirtyPipeline)) {
      if (unlikely(!this->updateRaytracingPipeline()))
        return false;
    }

    if (m_flags.any(
      DxvkContextFlag::RpDirtyResources,
      DxvkContextFlag::RpDirtyDescriptorBinding))
      this->updateRaytracingShaderResources();

    if (m_flags.test(DxvkContextFlag::RpDirtyPipelineState)) {
      if (unlikely(!this->updateRaytracingPipelineState()))
        return false;
    }

    if (m_flags.test(DxvkContextFlag::DirtyPushConstants))
      this->updatePushConstants<VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR>();

    return true;
  }


  template<bool Indexed, bool Indirect>
  bool DxvkContext::commitGraphicsState() {
    ScopedCpuProfileZone();
    if (m_flags.test(DxvkContextFlag::GpDirtyPipeline)) {
      if (unlikely(!this->updateGraphicsPipeline()))
        return false;
    }

    if (m_state.gp.flags.any(DxvkGraphicsPipelineFlag::HasStorageDescriptors,
      DxvkGraphicsPipelineFlag::HasTransformFeedback)) {
      this->commitGraphicsBarriers<Indexed, Indirect, false>();
      this->commitGraphicsBarriers<Indexed, Indirect, true>();
    }

    if (m_flags.test(DxvkContextFlag::GpDirtyFramebuffer))
      this->updateFramebuffer();

    if (!m_flags.test(DxvkContextFlag::GpRenderPassBound))
      this->startRenderPass();

    if (m_flags.test(DxvkContextFlag::GpDirtyIndexBuffer) && Indexed) {
      if (unlikely(!this->updateIndexBufferBinding()))
        return false;
    }

    if (m_flags.test(DxvkContextFlag::GpDirtyVertexBuffers))
      this->updateVertexBufferBindings();

    if (m_flags.any(
      DxvkContextFlag::GpDirtyResources,
      DxvkContextFlag::GpDirtyDescriptorBinding))
      this->updateGraphicsShaderResources();

    if (m_flags.test(DxvkContextFlag::GpDirtyPipelineState)) {
      if (unlikely(!this->updateGraphicsPipelineState()))
        return false;
    }

    if (m_state.gp.flags.test(DxvkGraphicsPipelineFlag::HasTransformFeedback))
      this->updateTransformFeedbackState();

    if (m_flags.any(
      DxvkContextFlag::GpDirtyViewport,
      DxvkContextFlag::GpDirtyBlendConstants,
      DxvkContextFlag::GpDirtyStencilRef,
      DxvkContextFlag::GpDirtyDepthBias,
      DxvkContextFlag::GpDirtyDepthBounds))
      this->updateDynamicState();

    if (m_flags.test(DxvkContextFlag::DirtyPushConstants))
      this->updatePushConstants<VK_PIPELINE_BIND_POINT_GRAPHICS>();

    if (m_flags.test(DxvkContextFlag::DirtyDrawBuffer) && Indirect)
      this->trackDrawBuffer();

    return true;
  }

  // NV-DXVK start: Split out common post barriers logic
  void DxvkContext::commitPostBarriers(const DxvkDescriptorSlot binding, VkPipelineStageFlags stages) {
    ScopedCpuProfileZone();
    const DxvkShaderResourceSlot& slot = m_rc[binding.slot];

    VkAccessFlags        access = binding.access;
    
    switch (binding.type) {
      case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
      case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
      case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
        m_execBarriers.accessBuffer(
          slot.bufferSlice.getSliceHandle(),
          stages, access,
          slot.bufferSlice.bufferInfo().stages,
          slot.bufferSlice.bufferInfo().access);
        break;
    
      case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
      case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
        m_execBarriers.accessBuffer(
          slot.bufferView->getSliceHandle(),
          stages, access,
          slot.bufferView->bufferInfo().stages,
          slot.bufferView->bufferInfo().access);
        break;
      
      case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
      case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
      case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
        m_execBarriers.accessImage(
          slot.imageView->image(),
          slot.imageView->imageSubresources(),
          slot.imageView->imageInfo().layout,
          stages, access,
          slot.imageView->imageInfo().layout,
          slot.imageView->imageInfo().stages,
          slot.imageView->imageInfo().access);
        break;

      default:
        /* nothing to do */;
    }
  }
  // NV-DXVK end


  // NV-DXVK start: Split out common init barriers logic
  bool DxvkContext::commitInitBarriers(const DxvkDescriptorSlot binding, VkPipelineStageFlags stages) {
    ScopedCpuProfileZone();
    const DxvkShaderResourceSlot& slot = m_rc[binding.slot];

    DxvkAccessFlags dstAccess = DxvkBarrierSet::getAccessTypes(binding.access);
    DxvkAccessFlags srcAccess = 0;
    
    switch (binding.type) {
      case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
      case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
      case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
        srcAccess = m_execBarriers.getBufferAccess(
          slot.bufferSlice.getSliceHandle());
        break;
    
      case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
      case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
        srcAccess = m_execBarriers.getBufferAccess(
          slot.bufferView->getSliceHandle());
        break;
      
      case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
      case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
      case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
        srcAccess = m_execBarriers.getImageAccess(
          slot.imageView->image(),
          slot.imageView->imageSubresources());
        break;

      default:
        /* nothing to do */;
    }

    if (srcAccess == 0)
      return false;

    // Skip write-after-write barriers if explicitly requested
    if ((m_barrierControl.test(DxvkBarrierControl::IgnoreWriteAfterWrite))
     && (!(m_execBarriers.getSrcStages() & ~stages))
     && ((srcAccess | dstAccess) == DxvkAccess::Write))
        return false;

    return (srcAccess | dstAccess).test(DxvkAccess::Write);
  }
  // NV-DXVK end


  void DxvkContext::commitComputeInitBarriers() {
    ScopedCpuProfileZone();
    auto layout = m_state.cp.pipeline->layout();

    bool requiresBarrier = false;

    for (uint32_t i = 0; i < layout->bindingCount() && !requiresBarrier; i++) {
      if (m_state.cp.state.bsBindingMask.test(i)) {
        const DxvkDescriptorSlot binding = layout->binding(i);
        // NV-DXVK start: Split out common init barriers logic
        requiresBarrier = commitInitBarriers(binding, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT);
        // NV-DXVK end
      }
    }

    if (requiresBarrier)
      m_execBarriers.recordCommands(m_cmd);
  }


  void DxvkContext::commitComputePostBarriers() {
    ScopedCpuProfileZone();
    auto layout = m_state.cp.pipeline->layout();

    for (uint32_t i = 0; i < layout->bindingCount(); i++) {
      if (m_state.cp.state.bsBindingMask.test(i)) {
        const DxvkDescriptorSlot binding = layout->binding(i);
        // NV-DXVK start: Split out common post barriers logic
        commitPostBarriers(binding, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
        // NV-DXVK end
      }
    }
  }


  // NV-DXVK start: Ray tracing init/post barriers
  void DxvkContext::commitRaytracingInitBarriers() {
    ScopedCpuProfileZone();
    auto layout = m_state.rp.pipeline->layout();

    bool requiresBarrier = false;

    for (uint32_t i = 0; i < layout->bindingCount() && !requiresBarrier; i++) {
      if (m_state.rp.state.bsBindingMask.test(i)) {
        const DxvkDescriptorSlot binding = layout->binding(i);
        requiresBarrier = commitInitBarriers(binding, VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR);
      }
    }

    if (requiresBarrier)
      m_execBarriers.recordCommands(m_cmd);
  }


  void DxvkContext::commitRaytracingPostBarriers() {
    ScopedCpuProfileZone();
    auto layout = m_state.rp.pipeline->layout();

    for (uint32_t i = 0; i < layout->bindingCount(); i++) {
      if (m_state.rp.state.bsBindingMask.test(i)) {
        const DxvkDescriptorSlot binding = layout->binding(i);
        commitPostBarriers(binding, VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR);
      }
    }
  }
  // NV-DXVK end


  template<bool Indexed, bool Indirect, bool DoEmit>
  void DxvkContext::commitGraphicsBarriers() {
    ScopedCpuProfileZone();
    if (m_barrierControl.test(DxvkBarrierControl::IgnoreGraphicsBarriers))
      return;

    auto layout = m_state.gp.pipeline->layout();

    constexpr auto storageBufferAccess = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFORM_FEEDBACK_WRITE_BIT_EXT;
    constexpr auto storageImageAccess = VK_ACCESS_SHADER_WRITE_BIT;

    bool requiresBarrier = false;

    // Check the draw buffer for indirect draw calls
    if (m_flags.test(DxvkContextFlag::DirtyDrawBuffer) && Indirect) {
      std::array<DxvkBufferSlice*, 2> slices = { {
        &m_state.id.argBuffer,
        &m_state.id.cntBuffer,
      } };

      for (uint32_t i = 0; i < slices.size() && !requiresBarrier; i++) {
        if ((slices[i]->defined())
          && (slices[i]->bufferInfo().access & storageBufferAccess)) {
          requiresBarrier = this->checkGfxBufferBarrier<DoEmit>(*slices[i],
            VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT,
            VK_ACCESS_INDIRECT_COMMAND_READ_BIT).test(DxvkAccess::Write);
        }
      }
    }

    // Read-only stage, so we only have to check this if
    // the bindngs have actually changed between draws
    if (m_flags.test(DxvkContextFlag::GpDirtyIndexBuffer) && !requiresBarrier && Indexed) {
      const auto& indexBufferSlice = m_state.vi.indexBuffer;

      if ((indexBufferSlice.defined())
        && (indexBufferSlice.bufferInfo().access & storageBufferAccess)) {
        requiresBarrier = this->checkGfxBufferBarrier<DoEmit>(indexBufferSlice,
          VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
          VK_ACCESS_INDEX_READ_BIT).test(DxvkAccess::Write);
      }
    }

    // Same here, also ignore unused vertex bindings
    if (m_flags.test(DxvkContextFlag::GpDirtyVertexBuffers)) {
      uint32_t bindingCount = m_state.gp.state.il.bindingCount();

      for (uint32_t i = 0; i < bindingCount && !requiresBarrier; i++) {
        uint32_t binding = m_state.gp.state.ilBindings[i].binding();
        const auto& vertexBufferSlice = m_state.vi.vertexBuffers[binding];

        if ((vertexBufferSlice.defined())
          && (vertexBufferSlice.bufferInfo().access & storageBufferAccess)) {
          requiresBarrier = this->checkGfxBufferBarrier<DoEmit>(vertexBufferSlice,
            VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
            VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT).test(DxvkAccess::Write);
        }
      }
    }

    // Transform feedback buffer writes won't overlap, so we
    // also only need to check those when they are rebound
    if (m_flags.test(DxvkContextFlag::GpDirtyXfbBuffers)
      && m_state.gp.flags.test(DxvkGraphicsPipelineFlag::HasTransformFeedback)) {
      for (uint32_t i = 0; i < MaxNumXfbBuffers && !requiresBarrier; i++) {
        const auto& xfbBufferSlice = m_state.xfb.buffers[i];
        const auto& xfbCounterSlice = m_state.xfb.counters[i];

        if (xfbBufferSlice.defined()) {
          requiresBarrier = this->checkGfxBufferBarrier<DoEmit>(xfbBufferSlice,
            VK_PIPELINE_STAGE_TRANSFORM_FEEDBACK_BIT_EXT,
            VK_ACCESS_TRANSFORM_FEEDBACK_WRITE_BIT_EXT) != 0;

          if (xfbCounterSlice.defined()) {
            requiresBarrier |= this->checkGfxBufferBarrier<DoEmit>(xfbCounterSlice,
              VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT |
              VK_PIPELINE_STAGE_TRANSFORM_FEEDBACK_BIT_EXT,
              VK_ACCESS_TRANSFORM_FEEDBACK_COUNTER_READ_BIT_EXT |
              VK_ACCESS_TRANSFORM_FEEDBACK_COUNTER_WRITE_BIT_EXT) != 0;
          }
        }
      }
    }

    // Check shader resources on every draw to handle WAW hazards
    for (uint32_t i = 0; i < layout->bindingCount() && !requiresBarrier; i++) {
      const DxvkDescriptorSlot binding = layout->binding(i);
      const DxvkShaderResourceSlot& slot = m_rc[binding.slot];

      DxvkAccessFlags dstAccess = DxvkBarrierSet::getAccessTypes(binding.access);
      DxvkAccessFlags srcAccess = 0;

      switch (binding.type) {
        case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
        case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
        case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
          if ((slot.bufferSlice.defined())
           && (slot.bufferSlice.bufferInfo().access & storageBufferAccess)) {
            srcAccess = this->checkGfxBufferBarrier<DoEmit>(slot.bufferSlice,
              binding.stages, binding.access);
          }
          break;

        case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
        case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
          if ((slot.bufferView != nullptr)
           && (slot.bufferView->bufferInfo().access & storageBufferAccess)) {
            srcAccess = this->checkGfxBufferBarrier<DoEmit>(slot.bufferView->slice(),
              binding.stages, binding.access);
          }
          break;

        case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
        case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
        case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
          if ((slot.imageView != nullptr)
           && (slot.imageView->imageInfo().access & storageImageAccess)) {
            srcAccess = this->checkGfxImageBarrier<DoEmit>(slot.imageView,
              binding.stages, binding.access);
          }
          break;

      default:
        /* nothing to do */;
      }

      if (srcAccess == 0)
        continue;

      // Skip write-after-write barriers if explicitly requested
      if ((m_barrierControl.test(DxvkBarrierControl::IgnoreWriteAfterWrite))
        && ((srcAccess | dstAccess) == DxvkAccess::Write))
        continue;

      requiresBarrier = (srcAccess | dstAccess).test(DxvkAccess::Write);
    }

    // External subpass dependencies serve as full memory
    // and execution barriers, so we can use this to allow
    // inter-stage synchronization.
    if (requiresBarrier)
      this->spillRenderPass(true);
  }


  template<bool DoEmit>
  DxvkAccessFlags DxvkContext::checkGfxBufferBarrier(
    const DxvkBufferSlice& slice,
    VkPipelineStageFlags      stages,
    VkAccessFlags             access) {
    if constexpr (DoEmit) {
      m_gfxBarriers.accessBuffer(
        slice.getSliceHandle(),
        stages, access,
        slice.bufferInfo().stages,
        slice.bufferInfo().access);
      return DxvkAccessFlags();
    }
    else {
      return m_gfxBarriers.getBufferAccess(slice.getSliceHandle());
    }
  }


  template<bool DoEmit>
  DxvkAccessFlags DxvkContext::checkGfxImageBarrier(
    const Rc<DxvkImageView>& imageView,
    VkPipelineStageFlags      stages,
    VkAccessFlags             access) {
    if constexpr (DoEmit) {
      m_gfxBarriers.accessImage(
        imageView->image(),
        imageView->imageSubresources(),
        imageView->imageInfo().layout,
        stages, access,
        imageView->imageInfo().layout,
        imageView->imageInfo().stages,
        imageView->imageInfo().access);
      return DxvkAccessFlags();
    }
    else {
      return m_gfxBarriers.getImageAccess(
        imageView->image(),
        imageView->imageSubresources());
    }
  }


  void DxvkContext::emitMemoryBarrier(
    VkDependencyFlags         flags,
    VkPipelineStageFlags      srcStages,
    VkAccessFlags             srcAccess,
    VkPipelineStageFlags      dstStages,
    VkAccessFlags             dstAccess) {
    VkMemoryBarrier barrier;
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.pNext = nullptr;
    barrier.srcAccessMask = srcAccess;
    barrier.dstAccessMask = dstAccess;

    m_cmd->cmdPipelineBarrier(
      DxvkCmdBuffer::ExecBuffer, srcStages, dstStages,
      flags, 1, &barrier, 0, nullptr, 0, nullptr);

    m_cmd->addStatCtr(DxvkStatCounter::CmdBarrierCount, 1);
  }

  void DxvkContext::initializeImage(
    const Rc<DxvkImage>& image,
    const VkImageSubresourceRange& subresources,
    VkImageLayout             dstLayout,
    VkPipelineStageFlags      dstStages,
    VkAccessFlags             dstAccess) {
    ScopedCpuProfileZone();
    if (m_execBarriers.isImageDirty(image, subresources, DxvkAccess::Write))
      m_execBarriers.recordCommands(m_cmd);

    VkPipelineStageFlags srcStages = 0;

    if (image->isInUse())
      srcStages = dstStages;

    m_execAcquires.accessImage(image, subresources,
      VK_IMAGE_LAYOUT_UNDEFINED, srcStages, 0,
      dstLayout, dstStages, dstAccess);
  }

  // NV-DXVK start: use EXT_debug_utils
  VkDescriptorSet DxvkContext::allocateDescriptorSet(
          VkDescriptorSetLayout     layout,
          const char                *name) {
    ScopedCpuProfileZone();
    if (m_descPool == nullptr)
      m_descPool = m_device->createDescriptorPool();

    VkDescriptorSet set = m_descPool->alloc(layout, name);

    if (set == VK_NULL_HANDLE) {
      m_cmd->trackDescriptorPool(std::move(m_descPool));

      m_descPool = m_device->createDescriptorPool();
      set = m_descPool->alloc(layout, name);
    }

    return set;
  }
  // NV-DXVK end

  void DxvkContext::traceRays(uint32_t width, uint32_t height, uint32_t depth) {
    ScopedCpuProfileZone();
    if (this->commitRaytracingState()) {
      this->commitRaytracingInitBarriers();

      m_queryManager.beginQueries(m_cmd,
        VK_QUERY_TYPE_PIPELINE_STATISTICS);

      m_cmd->cmdTraceRaysKHR(
        &m_state.rp.pipeline->m_raygenShaderBindingTable,
        &m_state.rp.pipeline->m_missShaderBindingTable,
        &m_state.rp.pipeline->m_hitShaderBindingTable,
        &m_state.rp.pipeline->m_callableShaderBindingTable,
        width, height, depth);

      m_queryManager.endQueries(m_cmd,
        VK_QUERY_TYPE_PIPELINE_STATISTICS);

      this->commitRaytracingPostBarriers();
    }

    m_cmd->addStatCtr(DxvkStatCounter::CmdTraceRaysCalls, 1);
  }


  void DxvkContext::trackDrawBuffer() {
    ScopedCpuProfileZone();
    if (m_flags.test(DxvkContextFlag::DirtyDrawBuffer)) {
      m_flags.clr(DxvkContextFlag::DirtyDrawBuffer);

      if (m_state.id.argBuffer.defined())
        m_cmd->trackResource<DxvkAccess::Read>(m_state.id.argBuffer.buffer());

      if (m_state.id.cntBuffer.defined())
        m_cmd->trackResource<DxvkAccess::Read>(m_state.id.cntBuffer.buffer());
    }
  }


  bool DxvkContext::tryInvalidateDeviceLocalBuffer(
      const Rc<DxvkBuffer>&           buffer,
            VkDeviceSize              copySize) {
    ScopedCpuProfileZone();
    // We can only discard if the full buffer gets written, and we will only discard
    // small buffers in order to not waste significant amounts of memory.
    if (copySize != buffer->info().size || copySize > 0x40000)
      return false;

    // Don't discard host-visible buffers since that may interfere with the frontend
    if (buffer->memFlags() & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)
      return false;

    // NV-DXVK start: Don't swap out the backing resource for buffers being used for acceleration
    // structure builds
    if (buffer->info().usage & VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR)
      return false;
    // NV-DXVK end

    // Suspend the current render pass if transform feedback is active prior to
    // invalidating the buffer, since otherwise we may invalidate a bound buffer.
    if ((buffer->info().usage & VK_BUFFER_USAGE_TRANSFORM_FEEDBACK_COUNTER_BUFFER_BIT_EXT)
     && (m_flags.test(DxvkContextFlag::GpXfbActive)))
      this->spillRenderPass(true);

    this->invalidateBuffer(buffer, buffer->allocSlice());
    return true;
  }
  

  DxvkGraphicsPipeline* DxvkContext::lookupGraphicsPipeline(
    const DxvkGraphicsPipelineShaders& shaders) {
    ScopedCpuProfileZone();
    auto idx = shaders.hash() % m_gpLookupCache.size();

    if (unlikely(!m_gpLookupCache[idx] || !shaders.eq(m_gpLookupCache[idx]->shaders())))
      m_gpLookupCache[idx] = m_common->pipelineManager().createGraphicsPipeline(shaders);

    return m_gpLookupCache[idx];
  }


  DxvkComputePipeline* DxvkContext::lookupComputePipeline(
    const DxvkComputePipelineShaders& shaders) {
    ScopedCpuProfileZone();
    auto idx = shaders.hash() % m_cpLookupCache.size();

    if (unlikely(!m_cpLookupCache[idx] || !shaders.eq(m_cpLookupCache[idx]->shaders())))
      m_cpLookupCache[idx] = m_common->pipelineManager().createComputePipeline(shaders);

    return m_cpLookupCache[idx];
  }


  DxvkRaytracingPipeline* DxvkContext::lookupRaytracingPipeline(
    const DxvkRaytracingPipelineShaders& shaders) {
    ScopedCpuProfileZone();

    auto foundPipeline = m_rpLookupCache.find(shaders.hash());
    if (unlikely(foundPipeline == m_rpLookupCache.end() || !shaders.eq(foundPipeline->second->shaders()))) {
      DxvkRaytracingPipeline* pipeline = m_common->pipelineManager().createRaytracingPipeline(shaders);
      m_rpLookupCache[pipeline->shaders().hash()] = pipeline;
      return pipeline;
    }

    return foundPipeline->second;
  }


  Rc<DxvkFramebuffer> DxvkContext::lookupFramebuffer(
    const DxvkFramebufferInfo&      framebufferInfo) {
    ScopedCpuProfileZone();
    DxvkFramebufferKey key = framebufferInfo.key();
    size_t idx = key.hash() % m_framebufferCache.size();

    if (m_framebufferCache[idx] == nullptr || !m_framebufferCache[idx]->key().eq(key))
      m_framebufferCache[idx] = m_device->createFramebuffer(framebufferInfo);

    return m_framebufferCache[idx];
  }


  Rc<DxvkBuffer> DxvkContext::createZeroBuffer(
    VkDeviceSize              size) {
    ScopedCpuProfileZone();
    if (m_zeroBuffer != nullptr && m_zeroBuffer->info().size >= size)
      return m_zeroBuffer;

    DxvkBufferCreateInfo bufInfo;
    bufInfo.size = align<VkDeviceSize>(size, 1 << 20);
    bufInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT
      | VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bufInfo.stages = VK_PIPELINE_STAGE_TRANSFER_BIT;
    bufInfo.access = VK_ACCESS_TRANSFER_WRITE_BIT
      | VK_ACCESS_TRANSFER_READ_BIT;

    m_zeroBuffer = m_device->createBuffer(bufInfo,
      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, DxvkMemoryStats::Category::AppBuffer, "zeroBuffer");

    clearBuffer(m_zeroBuffer, 0, bufInfo.size, 0);
    m_execBarriers.recordCommands(m_cmd);
    return m_zeroBuffer;
  }
  
}
