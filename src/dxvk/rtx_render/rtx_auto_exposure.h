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

  class DxvkAutoExposure: public CommonDeviceObject {
  public:
    explicit DxvkAutoExposure(DxvkDevice* device);
    ~DxvkAutoExposure();

    void dispatch(
      Rc<DxvkCommandList> cmdList,
      Rc<DxvkContext> ctx,
      Rc<DxvkSampler> linearSampler,
      const Resources::RaytracingOutput& rtOutput,
      const float deltaTime,
      bool performSRGBConversion = true,
      bool resetHistory = false);

    void showImguiSettings();

    const Resources::Resource& getExposureTexture() const { return m_exposure; }

  private:
    void createResources(Rc<DxvkContext> ctx);

    void dispatchAutoExposure(
      Rc<DxvkCommandList> cmdList,
      Rc<DxvkContext> ctx,
      Rc<DxvkSampler> linearSampler,
      const Resources::RaytracingOutput& rtOutput,
      const float deltaTime);

    Rc<vk::DeviceFn> m_vkd;

    Resources::Resource m_exposure;
    Resources::Resource m_exposureHistogram;
    Resources::Resource m_exposureWeightCurve;

    bool m_resetState = true;
    bool m_isCurveChanged = true;

    enum ExposureAverageMode : uint32_t {
      Mean = 0,
      Median
    };

    RTX_OPTION("rtx.autoExposure", bool, enabled, true, "Automatically adjusts exposure so that the image won't be too bright or too dark.");

    // Exposure Settings
    RTX_OPTION("rtx.autoExposure", float, autoExposureSpeed, 5.f, "Average exposure changing speed when the image changes.");
    RTX_OPTION("rtx.autoExposure", float, evMinValue, -2.0f, "Min/Max values tuned by moving from bright/dark locations in game, and adjusting until they look correct.");
    RTX_OPTION("rtx.autoExposure", float, evMaxValue, 5.f, "Min/Max values tuned by moving from bright/dark locations in game, and adjusting until they look correct.");
    RTX_OPTION("rtx.autoExposure", bool,  exposureCenterMeteringEnabled, false, "Gives higher weight to pixels around the screen center.");
    RTX_OPTION("rtx.autoExposure", float, centerMeteringSize, 0.5f, "The importance of pixels around the screen center.");
    RTX_OPTION("rtx.autoExposure", ExposureAverageMode, exposureAverageMode, ExposureAverageMode::Median, "Average mode. Valid values: <Mean=0, Median=1>. The mean mode averages exposures across pixels. The median mode is more stable for extreme pixel values.");
    RTX_OPTION("rtx.autoExposure", bool, useExposureCompensation, false, "Uses a curve to determine the importance of different exposure levels when calculating average exposure.");
    RTX_OPTION("rtx.autoExposure", float, exposureWeightCurve0, 1.f, "Curve control point 0.");
    RTX_OPTION("rtx.autoExposure", float, exposureWeightCurve1, 1.f, "Curve control point 1.");
    RTX_OPTION("rtx.autoExposure", float, exposureWeightCurve2, 1.f, "Curve control point 2.");
    RTX_OPTION("rtx.autoExposure", float, exposureWeightCurve3, 1.f, "Curve control point 3.");
    RTX_OPTION("rtx.autoExposure", float, exposureWeightCurve4, 1.f, "Curve control point 4.");
  };
  
}
