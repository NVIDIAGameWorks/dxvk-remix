/*
* Copyright (c) 2024, NVIDIA CORPORATION. All rights reserved.
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
#include "rtx_mipmap.h"
#include "dxvk_device.h"
#include "dxvk_scoped_annotation.h"
#include "rtx_render/rtx_shader_manager.h"
#include "rtx.h"
#include "rtx/pass/image_utils/generate_mipmap.h"

#include <rtx_shaders/generate_mipmap.h>

namespace dxvk {
  // Defined within an unnamed namespace to ensure unique definition across binary
  namespace {

    class GenerateMipmapShader : public ManagedShader {
      SHADER_SOURCE(GenerateMipmapShader, VK_SHADER_STAGE_COMPUTE_BIT, generate_mipmap)

      PUSH_CONSTANTS(GenerateMipmapArgs)

      BEGIN_PARAMETER()
        SAMPLER2D(GENERATE_MIPMAP_INPUT)
        RW_TEXTURE2D(GENERATE_MIPMAP_OUTPUT)
      END_PARAMETER()
    };
    PREWARM_SHADER_PIPELINE(GenerateMipmapShader);
  }

  RtxMipmap::Resource RtxMipmap::createResource(Rc<DxvkContext>& ctx,
                                                const char* name,
                                                const VkExtent3D& extent,
                                                const VkFormat format,
                                                const VkImageUsageFlags extraUsageFlags,
                                                const VkClearColorValue clearValue,
                                                const uint32_t mipLevels) {
    RtxMipmap::Resource resource = RtxMipmap::Resource(
      Resources::createImageResource(ctx, name, extent, format, 1, VK_IMAGE_TYPE_2D, VK_IMAGE_VIEW_TYPE_2D, 0, VK_IMAGE_USAGE_STORAGE_BIT | extraUsageFlags, clearValue, mipLevels)
    );
    

    if (mipLevels > 1) {
      DxvkImageViewCreateInfo viewInfo;
      viewInfo.type = VK_IMAGE_VIEW_TYPE_2D;
      viewInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | extraUsageFlags;
      viewInfo.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
      viewInfo.minLayer = 0;
      viewInfo.numLayers = 1;
      viewInfo.format = format;
      viewInfo.minLevel = 0;
      viewInfo.numLevels = 1;

      resource.views.clear();
      for (size_t i = 0; i < mipLevels; ++i) {
        resource.views.push_back(ctx->getDevice()->createImageView(resource.image, viewInfo));
        viewInfo.minLevel++;
      }
    }

    return resource;
  }

  void RtxMipmap::updateMipmap(Rc<RtxContext> ctx, Resource mipmap, MipmapMethod method) {
    VkFilter samplerType = method == MipmapMethod::Maximum ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
    Rc<DxvkSampler> sampler = ctx->getResourceManager().getSampler(samplerType, VK_SAMPLER_MIPMAP_MODE_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);

    for (size_t i = 1; i < mipmap.views.size(); i++) {
      VkExtent3D mipResolution = mipmap.image->mipLevelExtent(i);
      GenerateMipmapArgs pushArgs;
      pushArgs.resolution = {mipResolution.width, mipResolution.height};
      pushArgs.method = method;
      ctx->setPushConstantBank(DxvkPushConstantBank::RTX);
      ctx->pushConstants(0, sizeof(pushArgs), &pushArgs);
      ctx->bindResourceView(GENERATE_MIPMAP_INPUT, mipmap.views[i - 1], nullptr);
      ctx->bindResourceView(GENERATE_MIPMAP_OUTPUT, mipmap.views[i], nullptr);
      ctx->bindResourceSampler(GENERATE_MIPMAP_INPUT, sampler);
      ctx->bindShader(VK_SHADER_STAGE_COMPUTE_BIT, GenerateMipmapShader::getShader());
      const VkExtent3D mipWorkgroups = util::computeBlockCount(mipResolution, VkExtent3D { 16, 16, 1 });
      ctx->dispatch(mipWorkgroups.width, mipWorkgroups.height, mipWorkgroups.depth);
    }
  }
}
