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

#include "vulkan/vulkan_core.h"

#include <stdint.h>

namespace lss {
namespace Mdl {
namespace Filter {
enum {
  Nearest = 0,
  Linear = 1
};
static inline uint8_t vkToMdl(const VkFilter vkFilter) {
  return (vkFilter > VK_FILTER_LINEAR) ? Nearest : (uint8_t)vkFilter;
}
static inline VkFilter mdlToVk(const uint8_t mdlFilter) {
  return (VkFilter)mdlFilter;
}
}

namespace WrapMode {
// https://raytracing-docs.nvidia.com/mdl/api/group__mi__neuray__mdl__compiler.html#ga852d194e585ada01cc272e85e367ca9b
enum {
  Clamp = 0,
  Repeat = 1,
  Mirrored_Repeat = 2,
  Clip = 3 // Clamp to border, where border always black
};
// pBorderColor is an optional convenience parameter to easily set border to black of "Clip" wrap mode is used
static inline uint8_t vkToMdl(const VkSamplerAddressMode vkAddrMode, VkClearColorValue* const pBorderColor = nullptr) {
  switch(vkAddrMode) {
    case VK_SAMPLER_ADDRESS_MODE_REPEAT: return Repeat;
    case VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT: return Mirrored_Repeat;
    case VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE: return Clamp;
    case VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER:
      if(pBorderColor) {
        memset(pBorderColor, 0x0, sizeof(VkClearColorValue));
      }
      return Clip; // Maybe don't support?
    default: return Repeat;
  };
}
// pBorderColor is an optional convenience parameter to easily set border to black of "Clip" wrap mode is used
static inline VkSamplerAddressMode mdlToVk(const uint8_t mdlWrapMode, VkClearColorValue* const pBorderColor = nullptr) {
  switch(mdlWrapMode) {
    case Clamp: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    case Repeat: return VK_SAMPLER_ADDRESS_MODE_REPEAT;
    case Mirrored_Repeat: return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
    case Clip:
      if(pBorderColor) {
        memset(pBorderColor, 0x0, sizeof(VkClearColorValue));
      }
      return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER; // Maybe don't support?
    default: return VK_SAMPLER_ADDRESS_MODE_REPEAT;
  };
}
}
  

}
}
