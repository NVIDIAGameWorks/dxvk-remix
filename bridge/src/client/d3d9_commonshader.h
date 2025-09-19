/*
 * Copyright (c) 2023, NVIDIA CORPORATION. All rights reserved.
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

#include "util_common.h"
#include "base.h"

#include <d3d9.h>

class CommonShader {
  DWORD getShaderInstructionSize(const DWORD* pTokens) {
    const DWORD* pStart = pTokens;
    const DWORD opcode = ((*pTokens) & D3DSI_OPCODE_MASK);
    
    if (opcode == D3DSIO_COMMENT) {
      return (((*pTokens) & D3DSI_COMMENTSIZE_MASK) >> D3DSI_COMMENTSIZE_SHIFT) + 1;
    }
    if (m_majorVersion >= 2) {
      return (((*pTokens) & D3DSI_INSTLENGTH_MASK) >> D3DSI_INSTLENGTH_SHIFT) + 1;
    }
    switch (opcode) {
      case 0x0: return 1;
      case 0x1: return 3;
      case 0x2: return 4;
      case 0x3: return 4;
      case 0x4: return 5;
      case 0x5: return 4;
      case 0x6: return 3;
      case 0x7: return 3;
      case 0x8: return 4;
      case 0x9: return 4;
      case 0xa: return 4;
      case 0xb: return 4;
      case 0xc: return 4;
      case 0xd: return 4;
      case 0xe: return 3;
      case 0xf: return 3;
      case 0x10: return 3;
      case 0x11: return 4;
      case 0x12: return 5;
      case 0x13: return 3;
      case 0x14: return 4;
      case 0x15: return 4;
      case 0x16: return 4;
      case 0x17: return 4;
      case 0x18: return 4;
      case 0x19: return 2;
      case 0x1a: return 3;
      case 0x1b: return 3;
      case 0x1c: return 1;
      case 0x1d: return 1;
      case 0x1e: return 2;
      case 0x1f: return 3;
      case 0x20: return 4;
      case 0x21: return 4;
      case 0x22: return 5;
      case 0x23: return 3;
      case 0x24: return 3;
      case 0x25: return 5;
      case 0x26: return 2;
      case 0x27: return 1;
      case 0x28: return 2;
      case 0x29: return 3;
      case 0x2a: return 1;
      case 0x2b: return 1;
      case 0x2c: return 1;
      case 0x2d: return 3;
      case 0x2e: return 3;
      case 0x2f: return 3;
      case 0x30: return 5;
      case 0x40: return 2;
      case 0x41: return 2;
      case 0x42: return 2;
      case 0x43: return 3;
      case 0x44: return 3;
      case 0x45: return 3;
      case 0x46: return 3;
      case 0x47: return 3;
      case 0x48: return 3;
      case 0x49: return 3;
      case 0x4a: return 3;
      case 0x4c: return 4;
      case 0x4d: return 3;
      case 0x4e: return 3;
      case 0x4f: return 3;
      case 0x50: return 5;
      case 0x51: return 5;
      case 0x52: return 3;
      case 0x53: return 3;
      case 0x54: return 3;
      case 0x55: return 3;
      case 0x56: return 3;
      case 0x57: return 2;
      case 0x58: return 5;
      case 0x59: return 4;
      case 0x5a: return 5;
      case 0x5b: return 3;
      case 0x5c: return 3;
      case 0x5d: return 5;
      case 0x5e: return 4;
      case 0x5f: return 4;
      case 0x60: return 3;
      default: return 1;
    }
    return 1;

  }

  size_t getShaderByteSize(const DWORD* pTokens) {
    // Set starting pointer and increment past the header.
    const DWORD* pStart = pTokens++;
    while (((*pTokens) & D3DSI_OPCODE_MASK) != D3DSIO_END) {
      pTokens += getShaderInstructionSize(pTokens);
    }
    return ((pTokens - pStart) + 1) * sizeof(DWORD);
  }

  size_t analyze(const DWORD* pFunction) {
    m_majorVersion = D3DSHADER_VERSION_MAJOR(*pFunction);
    m_minorVersion = D3DSHADER_VERSION_MINOR(*pFunction);

    return getShaderByteSize(pFunction);
  }

  std::vector<uint8_t> m_code;

  uint32_t m_majorVersion = -1;
  uint32_t m_minorVersion = -1;

public:
  CommonShader(const DWORD* pFunction) {
    const size_t size = analyze(pFunction);

    m_code.resize(size);
    memcpy(m_code.data(), pFunction, m_code.size());
  }

  const DWORD* getCode() const {
    return (DWORD*) m_code.data();
  }

  const size_t getSize() const {
    return m_code.size();
  }

  const uint32_t getMajorVersion() const {
    return m_majorVersion;
  }

  const uint32_t getMinorVersion() const {
    return m_minorVersion;
  }
};