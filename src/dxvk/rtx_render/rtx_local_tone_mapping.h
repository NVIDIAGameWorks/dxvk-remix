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

#include "dxvk_format.h"
#include "dxvk_include.h"
#include "dxvk_context.h"
#include "rtx_resources.h"

#include "../spirv/spirv_code_buffer.h"
#include "../util/util_matrix.h"
#include "rtx_options.h"

namespace dxvk {

  class DxvkDevice;

  class DxvkLocalToneMapping: public RtxPass {
  public:
    DxvkLocalToneMapping(DxvkDevice* device);
    ~DxvkLocalToneMapping();

    void dispatch(
      Rc<DxvkCommandList> cmdList,
      Rc<DxvkDevice> device,
      Rc<DxvkContext> ctx,
      Rc<DxvkSampler> linearSampler,
      Rc<DxvkImageView> exposureView,
      const Resources::RaytracingOutput& rtOutput,
      const float deltaTime,
      bool performSRGBConversion = true,
      bool resetHistory = false,
      bool enableAutoExposure = true);

    void showImguiSettings();

  private:

    bool isActive() { return RtxOptions::Get()->tonemappingMode() == TonemappingMode::Local; }

    void createTargetResource(Rc<DxvkContext>& ctx, const VkExtent3D& targetExtent);

    void releaseTargetResource();

    Resources::MipMapResource m_mips;
    Resources::MipMapResource m_mipsWeights;
    Resources::MipMapResource m_mipsAssemble;

    // Tone curve settings
    RTX_OPTION("rtx.localtonemap", int, mip, 3, "Top mip level of tone map pyramid.");
    RTX_OPTION("rtx.localtonemap", int, displayMip, 0, "Bottom mip level of tone map pyramid.");
    RTX_OPTION("rtx.localtonemap", bool, boostLocalContrast, false, "Boosts contrast on local features.");
    RTX_OPTION("rtx.localtonemap", bool, useGaussian, true, "Uses gaussian kernel to generate tone map pyramid.");
    RTX_OPTION("rtx.localtonemap", bool, finalizeWithACES, true, "Applies ACES tone mapping on final result.");
    RTX_OPTION("rtx.localtonemap", float, exposure, 0.75, "Exposure factor applied on average exposure.");
    RTX_OPTION("rtx.localtonemap", float, shadows, 2.0, "Shadow area strength. Higher values cause brighter shadows.");
    RTX_OPTION("rtx.localtonemap", float, highlights, 4.0, "Highlight area strength. Higher values cause darker highlight.");
    RTX_OPTION("rtx.localtonemap", float, exposurePreferenceSigma, 4.0, "Transition sharpness between different areas of exposure. Smaller values result in sharper transitions.");
    RTX_OPTION("rtx.localtonemap", float, exposurePreferenceOffset, 0.0, "Offset to reference luminance when calculating the weights a pixel belongs to shadow/normal/highlight areas.");
  };
  
}
