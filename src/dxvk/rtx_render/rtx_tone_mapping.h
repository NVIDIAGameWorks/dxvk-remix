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

  class DxvkToneMapping: public CommonDeviceObject {
  public:
    explicit DxvkToneMapping(DxvkDevice* device);
    ~DxvkToneMapping();

    void dispatch(
      Rc<DxvkContext> ctx,
      Rc<DxvkSampler> linearSampler,
      Rc<DxvkImageView> exposureView,
      const Resources::RaytracingOutput& rtOutput,
      const float deltaTime,
      bool performSRGBConversion = true,
      bool resetHistory = false,
      bool autoExposureEnabled = true);
    
    bool isEnabled() const { return tonemappingEnabled(); }

    void showImguiSettings();

  private:
    void createResources(Rc<DxvkContext> ctx);

    void dispatchHistogram(
      Rc<DxvkContext> ctx,
      Rc<DxvkImageView> exposureView,
      const Resources::Resource& colorBuffer,
      bool autoExposureEnabled);

    void dispatchToneCurve(
      Rc<DxvkContext> ctx);

    void dispatchApplyToneMapping(
      Rc<DxvkContext> ctx,
      Rc<DxvkSampler> linearSampler,
      Rc<DxvkImageView> exposureView,
      const Resources::Resource& inputBuffer,
      const Resources::Resource& colorBuffer,
      bool performSRGBConversion,
      bool autoExposureEnabled);

    Rc<vk::DeviceFn> m_vkd;

    Resources::Resource m_toneHistogram;
    Resources::Resource m_toneCurve;

    bool m_resetState = true;
    bool m_isCurveChanged = true;

    enum ExposureAverageMode : uint32_t {
      Mean = 0,
      Median
    };

    RTX_OPTION("rtx.tonemap", float, exposureBias, 0.f, "");
    RTX_OPTION("rtx.tonemap", bool, tonemappingEnabled, true, "");
    RTX_OPTION("rtx.tonemap", bool, colorGradingEnabled, false, "");

    // Color grading settings
    RTX_OPTION("rtx.tonemap", Vector3, colorBalance, Vector3(1.f, 1.f, 1.0f), "");
    RTX_OPTION("rtx.tonemap", float, contrast, 1.0f, "");
    RTX_OPTION("rtx.tonemap", float, saturation, 1.0f, "");

    // Tone curve settings
    // Important that the min/max here do now under/overflow the dyamic range of input, or visual errors will be noticeable
    RTX_OPTION("rtx.tonemap", float, toneCurveMinStops, -24.0f, "Low endpoint of the tone curve (in log2(linear)).");
    RTX_OPTION("rtx.tonemap", float, toneCurveMaxStops, 8.0f, "High endpoint of the tone curve (in log2(linear))."); 
    RTX_OPTION("rtx.tonemap", bool,  tuningMode, false, "");
    RTX_OPTION("rtx.tonemap", bool,  finalizeWithACES, false, "");
    RTX_OPTION("rtx.tonemap", float, dynamicRange, 15.f, "Range [0, inf). Without further adjustments, the tone curve will try to fit the entire luminance of the scene into the range [-dynamicRange, 0] in linear photographic stops. Higher values adjust for ambient monitor lighting; perfect conditions -> 17.587 stops.");
    RTX_OPTION("rtx.tonemap", float, shadowMinSlope, 0.f, "Range [0, inf). Forces the tone curve below a linear value of 0.18 to have at least this slope, making the tone darker.");
    RTX_OPTION("rtx.tonemap", float, shadowContrast, 0.f, "Range [0, inf). Additional gamma power to apply to the tone of the tone curve below shadowContrastEnd.");
    RTX_OPTION("rtx.tonemap", float, shadowContrastEnd, 0.f, "Range (-inf, 0]. High endpoint for the shadow contrast effect in linear stops; values above this are unaffected.");
    RTX_OPTION("rtx.tonemap", float, curveShift, 0.0f, "Range [0, inf). Amount by which to shift the tone curve up or down. Nonzero values will cause additional clipping.");
    RTX_OPTION("rtx.tonemap", float, maxExposureIncrease, 5.f, "Range [0, inf). Forces the tone curve to not increase luminance values at any point more than this value.");

  };
  
}
