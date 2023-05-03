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

  class DxvkBloom: public RtxPass {
    
  public:
    explicit DxvkBloom(DxvkDevice* device);
    ~DxvkBloom();

    void dispatch(
      Rc<DxvkCommandList> cmdList,
      Rc<RtxContext> ctx,
      Rc<DxvkSampler> linearSampler,
      const Resources::Resource& inOutColorBuffer);

    inline bool isEnabled() const { return enable() && intensity() > 0.f; }

    void showImguiSettings();
    
  private:
    void dispatchDownscale(
      Rc<DxvkCommandList> cmdList,
      Rc<DxvkContext> ctx,
      const Resources::Resource& inputBuffer,
      const Resources::Resource& outputBuffer);

    template<bool isVertical>
    void dispatchBlur(
      Rc<DxvkCommandList> cmdList,
      Rc<DxvkContext> ctx,
      Rc<DxvkSampler> linearSampler,
      const Resources::Resource& inputBuffer,
      const Resources::Resource& outputBuffer);

    void dispatchComposite(
      Rc<DxvkCommandList> cmdList,
      Rc<DxvkContext> ctx,
      Rc<DxvkSampler> linearSampler,
      const Resources::Resource& inOutColorBuffer,
      const Resources::Resource& bloomBuffer);

    void createTargetResource(Rc<DxvkContext>& ctx, const VkExtent3D& targetExtent);

    void releaseTargetResource();

    bool isActive();

    Rc<vk::DeviceFn> m_vkd;

    Resources::Resource m_bloomBuffer0;
    Resources::Resource m_bloomBuffer1;

    RTX_OPTION("rtx.bloom", bool, enable, true, "");
    RTX_OPTION("rtx.bloom", float, sigma, 0.1f, "");
    RTX_OPTION("rtx.bloom", float, intensity, 0.06f, "");

    void initSettings(const dxvk::Config& config);
  };
  
}
