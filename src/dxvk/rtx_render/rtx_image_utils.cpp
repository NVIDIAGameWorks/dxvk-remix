/*
* Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.
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
#include "rtx_image_utils.h"
#include "dxvk_device.h"
#include "rtx_render/rtx_shader_manager.h"

#include <rtx_shaders/cube_to_latlong.h>
#include "dxvk_scoped_annotation.h"

#include "rtx_context.h"
#include "rtx_options.h"

#include "rtx/pass/image_utils/cube_to_latlong.h"

namespace dxvk {

  // Defined within an unnamed namespace to ensure unique definition across binary
  namespace {
    class CubeToLatLongShader : public ManagedShader {
      SHADER_SOURCE(CubeToLatLongShader, VK_SHADER_STAGE_COMPUTE_BIT, cube_to_latlong)

      PUSH_CONSTANTS(CubeToLatLongArgs)

      BEGIN_PARAMETER()
        SAMPLERCUBE(CUBE_TO_LATLONG_INPUT)
        RW_TEXTURE2D(CUBE_TO_LATLONG_OUTPUT)
      END_PARAMETER()

      inline static VkExtent3D groupSize = VkExtent3D{ 32, 32, 1 };
    };

    PREWARM_SHADER_PIPELINE(CubeToLatLongShader);
  }

  RtxImageUtils::RtxImageUtils(DxvkDevice* pDevice) {
  }

  void RtxImageUtils::cubemapToLatLong(Rc<DxvkContext>& ctx, Rc<DxvkImageView>& cube,
                                       Rc<DxvkImageView>& latlong,
                                       LatLongTransform transform) const {
    auto latlongExt = latlong->image()->info().extent;

    CubeToLatLongArgs cb;
    cb.extent = uint2(latlongExt.width, latlongExt.height);
    cb.scale = float2(2.f * kPi / static_cast<float>(latlongExt.width),
                      kPi / static_cast<float>(latlongExt.height));
    cb.transform = static_cast<::LatLongTransform>(transform);

    ctx->setPushConstantBank(DxvkPushConstantBank::RTX);
    ctx->pushConstants(0, sizeof(CubeToLatLongArgs), &cb);

    auto linearSampler = ctx->getCommonObjects()->getResources().getSampler(VK_FILTER_LINEAR,
      VK_SAMPLER_MIPMAP_MODE_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);

    ctx->bindResourceView(CUBE_TO_LATLONG_INPUT, cube, nullptr);
    ctx->bindResourceSampler(CUBE_TO_LATLONG_INPUT, linearSampler);

    ctx->bindResourceView(CUBE_TO_LATLONG_OUTPUT, latlong, nullptr);

    ctx->bindShader(VK_SHADER_STAGE_COMPUTE_BIT, CubeToLatLongShader::getShader());

    auto workgroups = util::computeBlockCount(latlongExt,
                                              CubeToLatLongShader::groupSize);
    ctx->dispatch(workgroups.width, workgroups.height, workgroups.depth);
  }
}
