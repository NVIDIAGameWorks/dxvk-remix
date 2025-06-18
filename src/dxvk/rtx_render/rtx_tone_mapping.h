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
      Rc<RtxContext> ctx,
      Rc<DxvkSampler> linearSampler,
      Rc<DxvkImageView> exposureView,
      const Resources::RaytracingOutput& rtOutput,
      const float frameTimeMilliseconds,
      bool performSRGBConversion = true,
      bool resetHistory = false,
      bool autoExposureEnabled = true);
    
    bool isEnabled() const { return tonemappingEnabled(); }

    void showImguiSettings();

  private:
    void createResources(Rc<RtxContext> ctx);

    void dispatchHistogram(
      Rc<RtxContext> ctx,
      Rc<DxvkImageView> exposureView,
      const Resources::Resource& colorBuffer,
      bool autoExposureEnabled);

    void dispatchToneCurve(
      Rc<RtxContext> ctx);

    void dispatchApplyToneMapping(
      Rc<RtxContext> ctx,
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

    enum class ExposureAverageMode : uint32_t {
      Mean = 0,
      Median
    };

    enum class DitherMode : uint32_t {
      None = 0,
      Spatial,
      SpatialTemporal,
    };

    RTX_OPTION("rtx.tonemap", float, exposureBias, 0.f, "The exposure value to use for the global tonemapper when auto exposure is disabled, or a bias multiplier on top of the auto exposure's calculated exposure value.");
    RTX_OPTION("rtx.tonemap", bool, tonemappingEnabled, true, "A flag to enable or disable the local tonemapper. Note this flag will only take effect when the global tonemapper is set to be used (as opposed to another option such as the local tonemapper).");
    RTX_OPTION("rtx.tonemap", bool, colorGradingEnabled, false, "A flag to enable or disable color grading after the global tonemapper's tonemapping pass, but before gamma correction and dithering (if enabled).");

    // Color grading settings
    RTX_OPTION("rtx.tonemap", Vector3, colorBalance, Vector3(1.0f, 1.0f, 1.0f), "The color tint to apply after tonemapping when color grading is enabled for the tonemapper (rtx.tonemap.colorGradingEnabled). Values should be in the range [0, 1].");
    RTX_OPTION("rtx.tonemap", float, contrast, 1.0f, "The contrast adjustment to apply after tonemapping when color grading is enabled for the tonemapper (rtx.tonemap.colorGradingEnabled). Values should be in the range [0, 1].");
    RTX_OPTION("rtx.tonemap", float, saturation, 1.0f, "The saturation adjustment to apply after tonemapping when color grading is enabled for the tonemapper (rtx.tonemap.colorGradingEnabled). Values should be in the range [0, 1].");

    // Tone curve settings
    // Important that the min/max here do now under/overflow the dyamic range of input, or visual errors will be noticeable
    RTX_OPTION("rtx.tonemap", float, toneCurveMinStops, -24.0f, "Low endpoint of the tone curve (in log2(linear)).");
    RTX_OPTION("rtx.tonemap", float, toneCurveMaxStops, 8.0f, "High endpoint of the tone curve (in log2(linear))."); 
    RTX_OPTION("rtx.tonemap", bool,  tuningMode, false, "A flag to enable a debug visualization to tune the tonemapping exposure curve with, as well as exposing parameters for tuning the tonemapping in the UI.");
    RTX_OPTION("rtx.tonemap", bool,  finalizeWithACES, false, "A flag to enable applying a final pass of ACES tonemapping to the tonemapped result.");
    RTX_OPTION("rtx.tonemap", bool,  useAgX, false, "A flag to enable AgX tonemapping instead of ACES or standard tonemapping.");
    RTX_OPTION("rtx.tonemap", float, agxGamma, 2.0f, "AgX gamma adjustment for contrast control. Lower values increase contrast. Range [0.5, 3.0].");
    RTX_OPTION("rtx.tonemap", float, agxSaturation, 1.1f, "AgX saturation multiplier. Higher values increase color saturation. Range [0.5, 2.0].");
    RTX_OPTION("rtx.tonemap", float, agxExposureOffset, 0.0f, "AgX exposure offset in EV stops. Positive values brighten the image. Range [-2.0, 2.0].");
    RTX_OPTION("rtx.tonemap", int, agxLook, 0, "AgX look selection: 0=None, 1=Punchy, 2=Golden, 3=Greyscale. Different aesthetic looks for AgX.");
    RTX_OPTION("rtx.tonemap", float, agxContrast, 1.0f, "AgX contrast adjustment. Higher values increase contrast. Range [0.5, 2.0].");
    RTX_OPTION("rtx.tonemap", float, agxSlope, 1.0f, "AgX slope adjustment for highlight rolloff. Range [0.5, 2.0].");
    RTX_OPTION("rtx.tonemap", float, agxPower, 1.0f, "AgX power adjustment for midtone response. Range [0.5, 2.0].");
    RTX_OPTION("rtx.tonemap", float, dynamicRange, 15.f, "Range [0, inf). Without further adjustments, the tone curve will try to fit the entire luminance of the scene into the range [-dynamicRange, 0] in linear photographic stops. Higher values adjust for ambient monitor lighting; perfect conditions -> 17.587 stops.");
    RTX_OPTION("rtx.tonemap", float, shadowMinSlope, 0.f, "Range [0, inf). Forces the tone curve below a linear value of 0.18 to have at least this slope, making the tone darker.");
    RTX_OPTION("rtx.tonemap", float, shadowContrast, 0.f, "Range [0, inf). Additional gamma power to apply to the tone of the tone curve below shadowContrastEnd.");
    RTX_OPTION("rtx.tonemap", float, shadowContrastEnd, 0.f, "Range (-inf, 0]. High endpoint for the shadow contrast effect in linear stops; values above this are unaffected.");
    RTX_OPTION("rtx.tonemap", float, curveShift, 0.0f, "Range [0, inf). Amount by which to shift the tone curve up or down. Nonzero values will cause additional clipping.");
    RTX_OPTION("rtx.tonemap", float, maxExposureIncrease, 5.f, "Range [0, inf). Forces the tone curve to not increase luminance values at any point more than this value.");

    // Dithering settings
    RTX_OPTION("rtx.tonemap", DitherMode, ditherMode, DitherMode::SpatialTemporal,
               "Tonemap dither mode selection, dithering allows for reduction of banding artifacts in the final rendered output from quantization using a small amount of monochromatic noise. Impact typically most visible in darker regions with smooth lighting gradients.\n"
               "Enabling dithering will make the rendered image slightly noisier, though usually dither noise is fairly imperceptible in most cases without looking closely. Generally dithered results will also look better than the alternative of banding artifacts due to increasing perceptual precision of the signal.\n"
               "Note that temporal dithering may increase perceptual precision further but may also introduce more noticeable noise in the final output in some cases due to the noise pattern changing every frame unlike a purely spatial approach.\n"
               "Supported enum values are 0 = None (Disabled), 1 = Spatial (Enabled, Spatial dithering only), 2 = SpatialTemporal (Enabled, Spatial and temporal dithering).\n"
               "Generally enabling dithering is recommended, but disabling it may be useful in some niche cases for improving compression ratios in images or videos at the cost of quality (as noise while it may not be very visible may be more difficult to compress), or for capturing \"raw\" post-tonemapped data from the renderer.");
  };
  
}
