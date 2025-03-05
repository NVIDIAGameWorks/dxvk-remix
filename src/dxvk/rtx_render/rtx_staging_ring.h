/*
* Copyright (c) 2025, NVIDIA CORPORATION. All rights reserved.
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

#pragma once

#include "dxvk_buffer.h"

namespace dxvk {

  // Allocates a fixed size buffer once, and returns slices from that buffer
  // by a simple atomic increment. That offset is reset when the command lists
  // used by returned slices become completed by GPU (which is detected on CPU by a fence).
  class RtxStagingRing {
  public:
    RtxStagingRing(const RtxStagingRing&) = delete;
    RtxStagingRing(RtxStagingRing&&) noexcept = delete;
    RtxStagingRing& operator=(const RtxStagingRing&) = delete;
    RtxStagingRing& operator=(RtxStagingRing&&) noexcept = delete;

    RtxStagingRing(const Rc<DxvkDevice>& device, VkDeviceSize budget)
      : m_buffer{ createBuffer(*device, budget) }
      , m_budget{ budget }
      , m_offset{ 0 } { }


    ~RtxStagingRing() { }


    const VkDeviceSize budget() const { return m_budget; }


    // Allocate a slice from a buffer. That slice needs to be submitted to a command list for a lifetime tracking.
    // WARNING: After a submission of the slice into a command list, 'onSliceSubmitToCmd' must be called.
    // If returns a null slice, then waiting for the GPU to complete the cmds
    // that m_buffer was used. Can be called only on a producer thread.
    DxvkBufferSlice alloc(VkDeviceSize align, VkDeviceSize size) {
      // When cmds associated with the DxvkResource (buffer) are completed,
      // GPU signals a fence, which is then picked up by the dxvk lifetime tracker
      // which releases a DxvkResource, so 'isInUse()' will be false,
      // which means that buffer is not used anywhere so we can safely reset.
      if (!m_buffer->isInUse()) {
        m_offset = 0;
      }

      size_t alignedSize = dxvk::align(size, align);
      size_t alignedOffset = dxvk::align(m_offset, align);

      if (alignedSize > m_budget) {
        assert(0 && "always check budget() before alloc()");
        return DxvkBufferSlice{}; // will never be able to alloc
      }

      if (alignedOffset + alignedSize > m_budget) {
        return DxvkBufferSlice{}; // waiting for gpu
      }

      {
        // add a temporary reference before the actual cmd will increase it by trackResource
        m_buffer->acquire(DxvkAccess::Write);
      }
      m_offset = alignedOffset + alignedSize;
      return DxvkBufferSlice{ m_buffer, alignedOffset, size };
    }


    // Call after a submission of the slice returned by alloc() to a cmd.
    // Can be called on a consumer thread that owns cmd.
    void onSliceSubmitToCmd() {
      // we can release the temporary reference, now that ctx->cmd actually called trackResource
      // (so GPU will signal to CPU [via a fence] that the resource is not in use anymore by GPU)
      m_buffer->release(DxvkAccess::Write);
    }


  private:
    static Rc<DxvkBuffer> createBuffer(DxvkDevice& device, VkDeviceSize size) {
      assert(size % kBufferAlignment == 0);
      DxvkBufferCreateInfo info{};
      {
        info.size = dxvk::align(size, kBufferAlignment);
        info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        info.stages =
          VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        info.access = VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_SHADER_READ_BIT;
      }
      return device.createBuffer(
        info,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
        DxvkMemoryStats::Category::AppBuffer, "RtxStagingRing");
    }


  private:
    const Rc<DxvkBuffer> m_buffer;
    const VkDeviceSize m_budget;
    VkDeviceSize m_offset;
  };

} // namespace dxvk
