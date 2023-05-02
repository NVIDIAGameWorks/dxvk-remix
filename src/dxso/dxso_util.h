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
#pragma once

#include <cstdint>

#include "dxso_common.h"
#include "dxso_decoder.h"

#include "../d3d9/d3d9_caps.h"

namespace dxvk {

  enum class DxsoBindingType : uint32_t {
    ConstantBuffer,
    Image
  };

  enum class DxsoConstantBufferType : uint32_t {
    Float,
    Int,
    Bool
  };

  enum DxsoConstantBuffers : uint32_t {
    VSConstantBuffer = 0,
    VSFloatConstantBuffer = 0,
    VSIntConstantBuffer = 1,
    VSBoolConstantBuffer = 2,
    VSClipPlanes     = 3,
    VSFixedFunction  = 4,
    VSVertexBlendData = 5,
    // NV-DXVK start: vertex shader data capture implementation
    VSVertexCaptureData = 6,
    // NV-DXVK end
    VSCount,

    PSConstantBuffer = 0,
    PSFixedFunction  = 1,
    PSShared         = 2,
    PSCount
  };

  // NV-DXVK start: Use a large base offset on all resources to avoid conflicting with ray tracing resources.
  const uint32_t baseSlotOffset = 1000;
  // NV-DXVK end

  // TODO: Intergrate into compute resource slot ID/refactor all of this?
  constexpr uint32_t getSWVPBufferSlot() {
    return baseSlotOffset + DxsoConstantBuffers::VSCount + caps::MaxTexturesVS + DxsoConstantBuffers::PSCount + caps::MaxTexturesPS + 1; // From last pixel shader slot, above.
  }

  constexpr uint32_t getVertexCaptureBufferSlot() {
    // Note: One after the SWVP Buffer slot as it currently is the last slot in use in this range (following the general constant buffer/image resources).
    return getSWVPBufferSlot() + 1;
  }

  constexpr uint32_t computeResourceSlotId(
        DxsoProgramType shaderStage,
        DxsoBindingType bindingType,
        uint32_t        bindingIndex) {
    const uint32_t stageOffset = (DxsoConstantBuffers::VSCount + caps::MaxTexturesVS) * uint32_t(shaderStage);
    switch (bindingType) {
    case DxsoBindingType::ConstantBuffer:
      return baseSlotOffset + bindingIndex + stageOffset;
    case DxsoBindingType::Image:
      return baseSlotOffset + bindingIndex + stageOffset + (shaderStage == DxsoProgramType::PixelShader ? DxsoConstantBuffers::PSCount : DxsoConstantBuffers::VSCount);
    }
    return baseSlotOffset;
  }

  uint32_t RegisterLinkerSlot(DxsoSemantic semantic);

}