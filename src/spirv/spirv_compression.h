/*
* Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.
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

#include <vector>

#include "spirv_code_buffer.h"

namespace dxvk {

  /**
   * \brief Compressed SPIR-V code buffer
   *
   * Implements a fast in-memory compression
   * to keep memory footprint low.
   */
  class SpirvCompressedBuffer {
    constexpr static uint32_t NumMaskWords = 32;
  public:

    SpirvCompressedBuffer();

    SpirvCompressedBuffer(
      const SpirvCodeBuffer&  code);
    
    ~SpirvCompressedBuffer();
    
    SpirvCodeBuffer decompress() const;

    const std::vector<uint64_t>& getCode() const {
      return m_code;
    }

  private:

    uint32_t              m_size;
    std::vector<uint64_t> m_mask;
    std::vector<uint64_t> m_code;

  };

}