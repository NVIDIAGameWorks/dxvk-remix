/*
* Copyright (c) 2022-2023, NVIDIA CORPORATION. All rights reserved.
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

#include "../dxvk_format.h"
#include "../dxvk_include.h"

#include "../spirv/spirv_code_buffer.h"
#include "rtx_resources.h"
#include "rtx_option.h"

namespace dxvk {

  class RtxContext;

  class DemodulatePass {

  public:

    DemodulatePass(dxvk::DxvkDevice* device);
    ~DemodulatePass();

    void dispatch(
      RtxContext* ctx, 
      const Resources::RaytracingOutput& rtOutput);

    void showImguiSettings();

    RTX_OPTION("rtx.demodulate", bool, demodulateRoughness, true, "Demodulate roughness to improve specular details.");
    RTX_OPTION("rtx.demodulate", float, demodulateRoughnessOffset, 0.1f, "Strength of roughness demodulation, lower values are stronger.");
    RTX_OPTION("rtx.demodulate", bool, enableDirectLightBoilingFilter, true, "Boiling filter removing direct light sample when its luminance is too high.");
    RTX_OPTION("rtx.demodulate", float, directLightBoilingThreshold, 5.f, "Remove direct light sample when its luminance is higher than the average one multiplied by this threshold .");

  private:
    Rc<vk::DeviceFn> m_vkd;

    dxvk::DxvkDevice* m_device;
  };
} // namespace dxvk
