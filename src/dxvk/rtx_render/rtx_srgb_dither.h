/*
* Copyright (c) 2026, NVIDIA CORPORATION. All rights reserved.
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

#include "dxvk_include.h"
#include "dxvk_context.h"
#include "rtx_resources.h"
#include "rtx_options.h"

namespace dxvk {

  class DxvkDevice;
  class RtxContext;

  // sRGB + dither pass: converts the linear post-tonemap LDR buffer to sRGB and
  // applies dithering. Runs as the very last step of the post-process pipeline.
  class DxvkSRGBDither : public CommonDeviceObject {
  public:
    explicit DxvkSRGBDither(DxvkDevice* device);

    void dispatch(
      Rc<RtxContext> ctx,
      const Resources::RaytracingOutput& rtOutput,
      bool performSRGBConversion);

    void showImguiSettings();

  private:
    enum class DitherMode : uint32_t {
      None = 0,
      Spatial,
      SpatialTemporal,
    };

    RTX_OPTION("rtx.srgbDither", DitherMode, ditherMode, DitherMode::SpatialTemporal,
               "Final output dither mode selection. Dithering allows for reduction of banding artifacts in the final rendered output from quantization using a small amount of monochromatic noise.\n"
               "Supported enum values are 0 = None (Disabled), 1 = Spatial (Enabled, Spatial dithering only), 2 = SpatialTemporal (Enabled, Spatial and temporal dithering).\n");
  };

}
