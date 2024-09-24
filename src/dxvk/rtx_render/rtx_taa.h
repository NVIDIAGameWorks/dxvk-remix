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

#include "../spirv/spirv_code_buffer.h"
#include "../util/util_matrix.h"
#include "rtx_options.h"

namespace dxvk {

  class DxvkDevice;

  class DxvkTemporalAA: public RtxPass {
  public:
    DxvkTemporalAA(DxvkDevice* device);
    ~DxvkTemporalAA();

    void prewarmShaders(DxvkPipelineManager& pipelineManager) const;

    void dispatch(
      Rc<DxvkContext> ctx,
      Rc<DxvkSampler> linearSampler,
      const uvec2& mainCameraResolution,
      const float jitterOffset[2],
      const Resources::Resource& colorTextureInput,
      const Resources::Resource& primaryScreenSpaceMotionVector,
      const Resources::Resource& colorTextureOutput,
      const bool isUpscale);

    void showImguiSettings();

  private:
    virtual bool isEnabled() const override;

    virtual void createTargetResource(Rc<DxvkContext>& ctx, const VkExtent3D&) override;
    virtual void releaseTargetResource() override;

    Rc<vk::DeviceFn> m_vkd;

    Resources::Resource m_taaFeedbackTexture[2];

    RTX_OPTION("rtx.temporalAA", float, maximumRadiance, 10000.0f,
               "The maximum value to use in TAA-U's perceptual quantizer color transformation, measured in cd/m^2.\n"
               "The typical value used for the PQ transformation is 10,000 and usually shouldn't be changed.");
    RTX_OPTION("rtx.temporalAA", float, newFrameWeight, 0.1f,
               "The maximum amount of the current frame to use as part of the temporal anti-aliasing process. Must be in the range 0-1.\n"
               "Values closer to 0 will result in better image stability (less jittering) and less aliasing, values closer to 1 will result in more responsive results (less ghosting).");
    RTX_OPTION("rtx.temporalAA", float, colorClampingFactor, 1.0f,
               "A scalar factor to apply to the standard deviation of the neighborhood of pixels in the color signal used for clamping. Should be in the range 0-infinity.\n"
               "This value essentially represents how many standard deviations of tolerance from the current frame's colors around each pixel pixel the temporally accumulated color signal may have.\n"
               "Higher values will cause more ghosting whereas lower values may reduce ghosting but will impact image quality (less ability to upscale effectively) and reduce stability (more jittering).");
  };

}
