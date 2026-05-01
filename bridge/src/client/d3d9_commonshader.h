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

    // PS 1.4 reuses opcodes 0x40 and 0x42 (D3DSIO_TEXCRD and D3DSIO_TEXLD)
    // with EXPLICIT dst+src token pairs (3 dwords) instead of the
    // PS 1.0–1.3 forms (D3DSIO_TEXCOORD / D3DSIO_TEX) that had implicit
    // src derived from the dst register index (2 dwords). The static
    // table below was authored for PS 1.0–1.3 / VS 1.x and gives the
    // wrong size for PS 1.4 — the parser advances by 2 instead of 3,
    // lands mid-instruction, treats a register-arg token as the next
    // opcode, misaligns indefinitely, and walks past the actual
    // D3DSIO_END token off the end of the bytecode buffer. Without
    // PageHeap the over-read silently corrupts the heap freelist and
    // surfaces as a delayed RtlpAllocateHeap AV; with PageHeap it AVs
    // immediately on the guard page.
    //
    // PS 1.4 token layout:
    //   0x40 (D3DSIO_TEXCRD):  op + dst + src = 3 dwords
    //   0x42 (D3DSIO_TEXLD):   op + dst + src = 3 dwords
    //   0x41 (D3DSIO_TEXKILL): unchanged at 2 dwords (op + dst)
    if (m_majorVersion == 1 && m_minorVersion == 4) {
      switch (opcode) {
        case 0x40: return 3;  // D3DSIO_TEXCRD (PS 1.4)
        case 0x42: return 3;  // D3DSIO_TEXLD  (PS 1.4)
        // 0x41 (TEXKILL) falls through to the PS 1.0–1.3 table below
        // (still 2 dwords in PS 1.4). 0x43–0x4F are deprecated/reserved
        // in PS 1.4 and shouldn't appear in valid 1.4 bytecode.
      }
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

    // Defense-in-depth bound. Real D3D9 shaders are at most a few KB.
    // 16K dwords (64 KB) is far more than any legitimate shader. If the
    // walk exceeds this without finding D3DSIO_END the bytecode is
    // malformed or our instruction-size table doesn't recognize a token
    // in this shader version. Bailing with a logged error is strictly
    // better than walking off the end of the heap (silent freelist
    // corruption in non-PageHeap runs, instant guard-page AV under
    // PageHeap).
    constexpr size_t kMaxShaderDwords = 16 * 1024;
    size_t walked = 0;
    while (walked < kMaxShaderDwords &&
           ((*pTokens) & D3DSI_OPCODE_MASK) != D3DSIO_END) {
      const DWORD step = getShaderInstructionSize(pTokens);
      pTokens += step;
      walked += step;
    }

    if (walked >= kMaxShaderDwords) {
      bridge_util::Logger::err(
        "CommonShader::getShaderByteSize: walked past 16K-dword cap "
        "without finding D3DSIO_END. Bytecode malformed or unsupported "
        "shader version. Returning cap-bounded size; server will likely "
        "reject the shader.");
      // We never found END. Return the walked size so the caller's
      // memcpy is bounded by the cap rather than running until the
      // next page-fault. Server-side compilation will fail; that's
      // strictly better than crashing the client.
      return walked * sizeof(DWORD);
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