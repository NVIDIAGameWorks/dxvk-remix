/*
* Copyright (c) 2026, NVIDIA CORPORATION. All rights reserved.
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

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <limits>
#include <vector>

#include "rtx_context.h"
#include "rtx_nee_cache.h"
#include "../shaders/rtx/pass/instance_definitions.h"

namespace dxvk {

  // Per-surface primitive ID prefix sums of the GPU-resident scene, which the NEE cache uses to address triangles.
  // The previous buffer holds last frame's sums, and only the suffix behind the first changed surface is uploaded.
  struct GpuResidentSceneNee {
    GpuResidentSceneNee(DxvkDevice& device, uint32_t maxSurfaceCount)
      : m_primitiveCounts(maxSurfaceCount, 0)
      , m_prefixes(size_t(maxSurfaceCount) + 1, 0)
      , m_current(createBuffer(device, m_prefixes.size(), "GPU Resident Scene Current NEE Prefixes"))
      , m_previous(createBuffer(device, m_prefixes.size(), "GPU Resident Scene Previous NEE Prefixes")) {
    }

    void setPrimitiveCount(uint32_t surfaceIndex, uint32_t primitiveCount) {
      if (surfaceIndex >= m_primitiveCounts.size()) {
        assert(false);
        return;
      }
      if (m_primitiveCounts[surfaceIndex] == primitiveCount) {
        return;
      }

      m_primitiveCounts[surfaceIndex] = primitiveCount;
      m_dirtySurface = std::min(m_dirtySurface, surfaceIndex);
    }

    void upload(const Rc<RtxContext>& ctx) {
      // setPrimitiveCount keeps tracking the first dirty surface while the NEE cache is off,
      // so turning the cache back on uploads every change made in the meantime.
      if (!NeeCachePass::enable()) {
        return;
      }

      bool uploaded = false;
      if (m_previousDirtyPrefix != kClean) {
        uploadSuffix(ctx, m_previous, m_previousDirtyPrefix);
        m_previousDirtyPrefix = kClean;
        uploaded = true;
      }

      if (m_dirtySurface != kClean) {
        uint64_t prefix = m_prefixes[m_dirtySurface];
        for (uint32_t i = m_dirtySurface; i < m_primitiveCounts.size(); i++) {
          prefix += m_primitiveCounts[i];
          if (prefix > PRIMITIVE_INDEX_MAX_VALUE) {
            assert(false);
            return;
          }
          m_prefixes[i + 1] = uint32_t(prefix);
        }

        const uint32_t dirtyPrefix = m_initialized ? m_dirtySurface + 1 : 0;
        uploadSuffix(ctx, m_current, dirtyPrefix);
        m_previousDirtyPrefix = dirtyPrefix;
        m_dirtySurface = kClean;
        m_initialized = true;
        uploaded = true;
      }

      if (uploaded) {
        ctx->emitMemoryBarrier(
          0,
          VK_PIPELINE_STAGE_TRANSFER_BIT,
          VK_ACCESS_TRANSFER_WRITE_BIT,
          VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT
            | VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
          VK_ACCESS_SHADER_READ_BIT);
      }
    }

    const Rc<DxvkBuffer>& currentBuffer() const {
      return m_current;
    }

    const Rc<DxvkBuffer>& previousBuffer() const {
      return m_previous;
    }

  private:
    static constexpr uint32_t kClean = std::numeric_limits<uint32_t>::max();

    static Rc<DxvkBuffer> createBuffer(DxvkDevice& device, size_t prefixCount,
        const char* name) {
      DxvkBufferCreateInfo info;
      info.size = std::max<size_t>(prefixCount * sizeof(uint32_t), 16);
      info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
                 | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
      info.stages = VK_PIPELINE_STAGE_TRANSFER_BIT
                  | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT
                  | VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR;
      info.access = VK_ACCESS_TRANSFER_WRITE_BIT;
      return device.createBuffer(info, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        DxvkMemoryStats::Category::RTXBuffer, name);
    }

    void uploadSuffix(const Rc<RtxContext>& ctx, const Rc<DxvkBuffer>& buffer,
        uint32_t firstPrefix) const {
      const size_t offset = size_t(firstPrefix) * sizeof(uint32_t);
      const size_t size = m_prefixes.size() * sizeof(uint32_t) - offset;
      ctx->writeToBuffer(buffer, offset, size,
        m_prefixes.data() + firstPrefix);
    }

    std::vector<uint32_t> m_primitiveCounts;
    std::vector<uint32_t> m_prefixes;
    Rc<DxvkBuffer> m_current;
    Rc<DxvkBuffer> m_previous;
    uint32_t m_dirtySurface = 0;
    uint32_t m_previousDirtyPrefix = 0;
    bool m_initialized = false;
  };

}  // namespace dxvk
