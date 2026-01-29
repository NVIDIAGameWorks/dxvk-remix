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

#include "../../rtx_option.h"

namespace dxvk {
namespace components {

// Shared constants for RtxOptionLayer components
// Max value is set to 10,000,000 to ensure no data loss when converting between float and uint32_t
// in RtxOptionLayerAction. Float has 24 bits of precision, so values up to 2^24 (16,777,216) can be
// represented exactly. This limit provides ample range for priority values while maintaining precision.
static constexpr uint32_t kMaxComponentRtxOptionLayerPriority = 10000000;
static constexpr uint32_t kMinComponentRtxOptionLayerPriority = RtxOptionLayer::s_userOptionLayerOffset + 1;
static constexpr uint32_t kDefaultComponentRtxOptionLayerPriority = 10000;

// Helper function to convert and clamp RtxOptionLayer priority value for components
inline uint32_t getRtxOptionLayerComponentClampedPriority(float priorityValue) {
  uint32_t priority = static_cast<uint32_t>(std::round(priorityValue));
  return std::clamp(priority, kMinComponentRtxOptionLayerPriority, kMaxComponentRtxOptionLayerPriority);
}

}  // namespace components
}  // namespace dxvk

