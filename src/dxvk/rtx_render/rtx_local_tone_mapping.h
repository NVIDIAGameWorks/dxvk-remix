/*
* Copyright (c) 2023-2024, NVIDIA CORPORATION. All rights reserved.
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
#include "rtx_render/rtx_mipmap.h"

#include "../spirv/spirv_code_buffer.h"
#include "../util/util_matrix.h"
#include "rtx_options.h"

namespace dxvk {

  class DxvkDevice;

  class DxvkLocalToneMapping: public RtxPass {
  public:
    explicit DxvkLocalToneMapping(DxvkDevice* device);
    ~DxvkLocalToneMapping();

    void dispatch(
      Rc<RtxContext> ctx,
      Rc<DxvkSampler> linearSampler,
      Rc<DxvkImageView> exposureView,
      const Resources::RaytracingOutput& rtOutput,
      const float frameTimeMilliseconds,
      bool performSRGBConversion = true,
      bool resetHistory = false,
      bool enableAutoExposure = true);

    void showImguiSettings();

  private:

    virtual bool isEnabled() const override;

    virtual void createTargetResource(Rc<DxvkContext>& ctx, const VkExtent3D& targetExtent) override;
    virtual void releaseTargetResource() override;

    RtxMipmap::Resource m_mips;
    RtxMipmap::Resource m_mipsWeights;
    RtxMipmap::Resource m_mipsAssemble;

    enum class DitherMode : uint32_t {
      None = 0,
      Spatial,
      SpatialTemporal,
    };

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

    // Dithering settings
    // Todo: In the future it might be good to combine this option and the rtx.tonemap.ditherMode option to reduce code/documentation/UI duplication.
    RTX_OPTION("rtx.localtonemap", DitherMode, ditherMode, DitherMode::SpatialTemporal,
               "Local tonemap dither mode selection, local tonemapping dithering has the same functionality and values as the global tonemapping dithering option, see rtx.tonemap.ditherMode for a more in-depth description.\n"
               "Supported enum values are 0 = None (Disabled), 1 = Spatial (Enabled, Spatial dithering only), 2 = SpatialTemporal (Enabled, Spatial and temporal dithering).\n");
  };
  
}
