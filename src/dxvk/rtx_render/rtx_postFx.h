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

    RTX_OPTION("rtx.postfx", bool,  enableMotionBlurNoiseSample, true, "Enable random distance sampling for every step along the motion vector. The random pattern is generated with interleaved gradient noise.");
    RTX_OPTION("rtx.postfx", bool,  enableMotionBlurEmissive, true, "Enable Motion Blur for Emissive surfaces. Disable this when the motion blur on emissive surfaces cause severe artifacts.");
    RTX_OPTION("rtx.postfx", uint,  motionBlurSampleCount, 4, "The number of samples along the motion vector. More samples could help to reduce motion blur noise.");
    RTX_OPTION("rtx.postfx", float, exposureFraction, 0.4f, "Simulate the camera exposure, the longer exposure will cause stronger motion blur.");
    RTX_OPTION("rtx.postfx", float, blurDiameterFraction, 0.02f, "The diameter of the circle that motion blur samplings occur. Motion vectors beyond this circle will be clamped.");
    RTX_OPTION("rtx.postfx", float, motionBlurMinimumVelocityThresholdInPixel, 1.0f, "The minimum motion vector distance that enable the motion blur. The unit is pixel size.");
    RTX_OPTION("rtx.postfx", float, motionBlurDynamicDeduction, 1.0f, "The deduction of motion blur for dynamic objects.");
    RTX_OPTION("rtx.postfx", float, motionBlurJitterStrength, 0.6f, "The jitter strength of every sample along the motion vector.");
    RTX_OPTION("rtx.postfx", float, chromaticAberrationAmount, 0.02f, "The strength of chromatic aberration.");
    RTX_OPTION("rtx.postfx", float, chromaticCenterAttenuationAmount, 0.975f, "Control the amount of chromatic aberration effect that attunuated when close to the center of screen.");
    RTX_OPTION("rtx.postfx", float, vignetteIntensity, 0.8f, "The darkness of vignette effect.");
    RTX_OPTION("rtx.postfx", float, vignetteRadius, 0.8f, "The radius that vignette effect starts. The unit is normalized screen space, 0 represents the center, 1 means the edge of the short edge of the rendering window. So, this setting can larger than 1 until reach to the long edge of the rendering window.");
    RTX_OPTION("rtx.postfx", float, vignetteSoftness, 0.2f, "The gradient that the color drop to black from the vignetteRadius to the edge of rendering window.");
  };

}
