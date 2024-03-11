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
#pragma once

#include "dxvk_context.h"
#include "rtx_resources.h"

#include "rtx/pass/image_utils/generate_mipmap.h"

namespace dxvk {

  class RtxMipmap {   
  public:
    struct Resource : public Resources::Resource {
      std::vector<Rc<DxvkImageView>> views;

      Resource() {}
      Resource(const Resources::Resource& in) : Resources::Resource(in) {}

      void reset() {
        Resources::Resource::reset();
        views.clear();
      }
    }; 
    
    static Resource createResource(Rc<DxvkContext>& ctx,
                                   const char* name,
                                   const VkExtent3D& extent,
                                   const VkFormat format,
                                   const VkImageUsageFlags extraUsageFlags,
                                   const VkClearColorValue clearValue,
                                   const uint32_t mipLevels);

    // Updates mip levels [1 : maxMipLevel] based on the contents of mip level 0.
    // calls to this should be wrapped with a `ScopedGpuProfileZone(ctx, "Foo Mipmap");` marker.
    static void updateMipmap(Rc<RtxContext> ctx, Resource mipmap, MipmapMethod method);
  };
  
}
