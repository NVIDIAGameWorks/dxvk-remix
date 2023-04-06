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

  class DxvkPostFx {
  public:
    DxvkPostFx(DxvkDevice* device);
    ~DxvkPostFx();

    void dispatch(
      Rc<DxvkCommandList> cmdList,
      Rc<RtxContext> ctx,
      Rc<DxvkSampler> nearestSampler,
      Rc<DxvkSampler> linearSampler,
      const uvec2& mainCameraResolution,
      const uint32_t frameIdx,
      const Resources::RaytracingOutput& rtOutput,
      const bool cameraCutDetected);

    void showImguiSettings();

    inline bool isPostFxEnabled() const { return enable(); }
    inline bool isMotionBlurEnabled() const { return enable() && enableMotionBlur() && motionBlurSampleCount() > 0 && exposureFraction() > 0.0f; }
    inline bool isChromaticAberrationEnabled() const { return enable() && enableChromaticAberration() && chromaticAberrationAmount() > 0.0f; }
    inline bool isVignetteEnabled() const { return enable() && enableVignette() && vignetteIntensity() > 0.0f; }

    RW_RTX_OPTION("rtx.postfx", bool, enable, true, "Enables post-processing effects.");
    RW_RTX_OPTION("rtx.postfx", bool, enableMotionBlur, true, "Enables motion blur post-processing effect.");
    RW_RTX_OPTION("rtx.postfx", bool, enableChromaticAberration, true, "Enables chromatic aberration post-processing effect.");
    RW_RTX_OPTION("rtx.postfx", bool, enableVignette, true, "Enables vignette post-processing effect.");
  private:
    Rc<vk::DeviceFn> m_vkd;

    RTX_OPTION("rtx.postfx", bool,  enableMotionBlurNoiseSample, true, "");
    RTX_OPTION("rtx.postfx", bool,  enableMotionBlurEmissive, true, "");
    RTX_OPTION("rtx.postfx", uint,  motionBlurSampleCount, 4, "");
    RTX_OPTION("rtx.postfx", float, exposureFraction, 0.4f, "");
    RTX_OPTION("rtx.postfx", float, blurDiameterFraction, 0.02f, "");
    RTX_OPTION("rtx.postfx", float, motionBlurMinimumVelocityThresholdInPixel, 1.0f, "");
    RTX_OPTION("rtx.postfx", float, motionBlurDynamicDeduction, 0.075f, "");
    RTX_OPTION("rtx.postfx", float, motionBlurJitterStrength, 0.6f, "");
    RTX_OPTION("rtx.postfx", float, chromaticAberrationAmount, 0.02f, "");
    RTX_OPTION("rtx.postfx", float, chromaticCenterAttenuationAmount, 0.975f, "");
    RTX_OPTION("rtx.postfx", float, vignetteIntensity, 0.8f, "");
    RTX_OPTION("rtx.postfx", float, vignetteRadius, 0.8f, "");
    RTX_OPTION("rtx.postfx", float, vignetteSoftness, 0.2f, "");
  };

}
