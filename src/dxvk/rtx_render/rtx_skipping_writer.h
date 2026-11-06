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

#include "../dxvk_context.h"

namespace dxvk {

  // Writes src bytes to staging dst, leaving out the byte ranges passed to writeThenSkipGap() in increasing order.
  // Ranges under 4KB are written anyway, which bounds the writes to (skipped bytes / 4KB) + 1.
  struct SkippingWriter {
    static constexpr size_t kMinGap = 4096;

    SkippingWriter(DxvkContext& ctx, const Rc<DxvkBuffer>& dst, const void* src)
      : m_ctx(ctx), m_dst(dst), m_src(static_cast<const uint8_t*>(src)) { }

    // Writes [m_begin, gapBegin) and continues at gapEnd; gaps already passed are ignored
    void writeThenSkipGap(size_t gapBegin, size_t gapEnd) {
      if (gapBegin >= m_begin && gapEnd - gapBegin >= kMinGap) {
        writeUntil(gapBegin);
        m_begin = gapEnd;
      }
    }

    // Writes [m_begin, end)
    void writeUntil(size_t end) {
      if (end > m_begin) {
        m_ctx.writeToBuffer(m_dst, m_begin, end - m_begin, m_src + m_begin);
      }
    }

  private:
    DxvkContext& m_ctx;
    Rc<DxvkBuffer> m_dst;
    const uint8_t* m_src;
    size_t m_begin = 0; // First byte not written yet
  };

}
