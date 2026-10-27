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

#include <array>
#include <cstdint>
#include <cstring>

#include <d3d9.h>

#include "util_common.h"

// Tracks the state the server holds, so that redundant setters can be skipped (client.dropRedundantState).
// It must be invalidated whenever the server's state can change without a setter, such as Apply or Reset.
// Object ids are never reused, so an equal id is the same object.
class SentState {
  template<typename T>
  struct Entry {
    T value;
    uint32_t gen = 0;
  };

  template<typename T>
  bool update(Entry<T>& entry, const T& value) {
    if (entry.gen == m_gen && std::memcmp(&entry.value, &value, sizeof(T)) == 0) {
      return false;
    }
    entry.value = value;
    entry.gen = m_gen;
    return true;
  }

  template<typename T, uint32_t Elements, uint32_t Registers>
  struct ConstantBank {
    struct Reg {
      T v[Elements];
      uint32_t gen = 0;
    };
    std::array<Reg, Registers> regs;
  };

  // Narrows the range to the registers that changed and records the whole range as sent.
  template<typename T, uint32_t Elements, uint32_t Registers>
  bool updateConstants(ConstantBank<T, Elements, Registers>& bank, uint32_t& start, const T*& pData, uint32_t& count) {
    if (count == 0 || start >= Registers || count > Registers - start) {
      return true;
    }
    uint32_t first = count;
    uint32_t last = 0;
    for (uint32_t i = 0; i < count; ++i) {
      auto& reg = bank.regs[start + i];
      const T* pSrc = pData + size_t(i) * Elements;
      if (reg.gen != m_gen || std::memcmp(reg.v, pSrc, sizeof(T) * Elements) != 0) {
        std::memcpy(reg.v, pSrc, sizeof(T) * Elements);
        reg.gen = m_gen;
        if (first == count) {
          first = i;
        }
        last = i;
      }
    }
    if (first == count) {
      return false;
    }
    pData += size_t(first) * Elements;
    start += first;
    count = last - first + 1;
    return true;
  }

public:
  static constexpr uint32_t kRenderStates = 256;
  static constexpr uint32_t kTextureStages = 8;
  static constexpr uint32_t kTextureStageStateTypes = 33;
  static constexpr uint32_t kSamplers = 21;
  static constexpr uint32_t kSamplerStateTypes = 14;
  static constexpr uint32_t kTransforms = 512;
  static constexpr uint32_t kStreams = caps::MaxStreams;

  void invalidateAll() {
    if (++m_gen == 0) {
      m_gen = 1;
    }
  }

  void invalidateViewportAndScissor() {
    m_viewport.gen = 0;
    m_scissor.gen = 0;
  }

  void invalidateStreamSource(uint32_t stream) {
    m_streams[stream].gen = 0;
  }

  void invalidateIndices() {
    m_indices.gen = 0;
  }

  bool renderState(uint32_t state, DWORD value) {
    // These render states carry driver hacks such as RESZ, ATOC and NVDB, so they are always sent.
    if (state >= kRenderStates || state == D3DRS_POINTSIZE ||
        (state >= D3DRS_ADAPTIVETESS_X && state <= D3DRS_ENABLEADAPTIVETESSELLATION)) {
      return true;
    }
    return update(m_renderStates[state], value);
  }

  bool textureStageState(uint32_t stage, uint32_t type, DWORD value) {
    if (stage >= kTextureStages || type >= kTextureStageStateTypes) {
      return true;
    }
    return update(m_textureStageStates[stage][type], value);
  }

  bool samplerState(uint32_t sampler, uint32_t type, DWORD value) {
    const uint32_t idx = samplerIndex(sampler);
    if (idx >= kSamplers || type >= kSamplerStateTypes) {
      return true;
    }
    return update(m_samplerStates[idx][type], value);
  }

  bool texture(uint32_t sampler, uint32_t objectId) {
    const uint32_t idx = samplerIndex(sampler);
    if (idx >= kSamplers) {
      return true;
    }
    return update(m_textures[idx], objectId);
  }

  bool transform(uint32_t state, const D3DMATRIX& matrix) {
    if (state >= kTransforms) {
      return true;
    }
    return update(m_transforms[state], matrix);
  }

  bool viewport(const D3DVIEWPORT9& value) {
    return update(m_viewport, value);
  }

  bool scissor(const RECT& value) {
    return update(m_scissor, value);
  }

  bool material(const D3DMATERIAL9& value) {
    return update(m_material, value);
  }

  bool streamSource(uint32_t stream, uint32_t objectId, uint32_t offset, uint32_t stride) {
    if (stream >= kStreams) {
      return true;
    }
    const StreamBinding binding { objectId, offset, stride };
    return update(m_streams[stream], binding);
  }

  bool streamSourceFreq(uint32_t stream, uint32_t divider) {
    if (stream >= kStreams) {
      return true;
    }
    return update(m_streamFreqs[stream], divider);
  }

  bool indices(uint32_t objectId) {
    return update(m_indices, objectId);
  }

  // SetFVF and SetVertexDeclaration replace each other on the server.
  bool vertexDeclaration(uint32_t objectId) {
    m_fvf.gen = 0;
    return update(m_vertexDecl, objectId);
  }

  bool fvf(DWORD value) {
    m_vertexDecl.gen = 0;
    return update(m_fvf, value);
  }

  bool vertexShader(uint32_t objectId) {
    return update(m_vertexShader, objectId);
  }

  bool pixelShader(uint32_t objectId) {
    return update(m_pixelShader, objectId);
  }

  bool vsConstantsF(uint32_t& start, const float*& pData, uint32_t& count) {
    return updateConstants(m_vsF, start, pData, count);
  }
  bool vsConstantsI(uint32_t& start, const int*& pData, uint32_t& count) {
    return updateConstants(m_vsI, start, pData, count);
  }
  bool vsConstantsB(uint32_t& start, const BOOL*& pData, uint32_t& count) {
    return updateConstants(m_vsB, start, pData, count);
  }
  bool psConstantsF(uint32_t& start, const float*& pData, uint32_t& count) {
    return updateConstants(m_psF, start, pData, count);
  }
  bool psConstantsI(uint32_t& start, const int*& pData, uint32_t& count) {
    return updateConstants(m_psI, start, pData, count);
  }
  bool psConstantsB(uint32_t& start, const BOOL*& pData, uint32_t& count) {
    return updateConstants(m_psB, start, pData, count);
  }

private:
  struct StreamBinding {
    uint32_t objectId, offset, stride;
  };

  static uint32_t samplerIndex(uint32_t sampler) {
    if (sampler < 16) {
      return sampler;
    }
    if (sampler >= D3DDMAPSAMPLER && sampler <= D3DVERTEXTEXTURESAMPLER3) {
      return 16 + (sampler - D3DDMAPSAMPLER);
    }
    return kSamplers;
  }

  uint32_t m_gen = 1;

  std::array<Entry<DWORD>, kRenderStates> m_renderStates;
  std::array<std::array<Entry<DWORD>, kTextureStageStateTypes>, kTextureStages> m_textureStageStates;
  std::array<std::array<Entry<DWORD>, kSamplerStateTypes>, kSamplers> m_samplerStates;
  std::array<Entry<uint32_t>, kSamplers> m_textures;
  std::array<Entry<D3DMATRIX>, kTransforms> m_transforms;
  Entry<D3DVIEWPORT9> m_viewport;
  Entry<RECT> m_scissor;
  Entry<D3DMATERIAL9> m_material;
  std::array<Entry<StreamBinding>, kStreams> m_streams;
  std::array<Entry<uint32_t>, kStreams> m_streamFreqs;
  Entry<uint32_t> m_indices;
  Entry<uint32_t> m_vertexDecl;
  Entry<DWORD> m_fvf;
  Entry<uint32_t> m_vertexShader;
  Entry<uint32_t> m_pixelShader;
  ConstantBank<float, 4, caps::MaxFloatConstantsSoftware> m_vsF;
  ConstantBank<int, 4, caps::MaxOtherConstantsSoftware> m_vsI;
  ConstantBank<BOOL, 1, caps::MaxOtherConstantsSoftware> m_vsB;
  ConstantBank<float, 4, caps::MaxFloatConstantsPS> m_psF;
  ConstantBank<int, 4, caps::MaxOtherConstants> m_psI;
  ConstantBank<BOOL, 1, caps::MaxOtherConstants> m_psB;
};
