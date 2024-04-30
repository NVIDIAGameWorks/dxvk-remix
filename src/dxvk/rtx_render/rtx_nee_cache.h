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

#include "../dxvk_format.h"
#include "../dxvk_include.h"

#include "../spirv/spirv_code_buffer.h"
#include "rtx_resources.h"
#include "rtx_option.h"
#include "rtx/algorithm/nee_cache_data.h"

namespace dxvk {

  class RtxContext;

  class NeeCachePass {

  public:
    NeeCachePass(dxvk::DxvkDevice* device);
    ~NeeCachePass();

    void dispatch(
      RtxContext* ctx, 
      const Resources::RaytracingOutput& rtOutput);

    void showImguiSettings();

    void setRaytraceArgs(RaytraceArgs& raytraceArgs, bool resetHistory) const;

    RW_RTX_OPTION("rtx.neeCache", bool, enable, true, "[Experimental] Enable NEE cache. The integrator will perform NEE on emissive triangles, which usually have significant light contributions, stored in the cache.");
    RTX_OPTION("rtx.neeCache", bool, enableImportanceSampling, true, "Enable importance sampling.");
    RTX_OPTION("rtx.neeCache", bool, enableMIS, true, "Enable MIS.");
    RTX_OPTION("rtx.neeCache", bool, enableUpdate, true, "Enable Update.");
    RTX_OPTION("rtx.neeCache", bool, enableOnFirstBounce, true, "Enable NEE Cache on a first bounce.");
    RW_RTX_OPTION("rtx.neeCache", NeeEnableMode, enableModeAfterFirstBounce, NeeEnableMode::SpecularOnly, "NEE Cache enable mode on a second and higher bounces. 0 means off, 1 means enabled for specular rays only, 2 means always enabled.");
    RTX_OPTION("rtx.neeCache", bool, enableAnalyticalLight, true, "Enable NEE Cache on analytical light.");
    RTX_OPTION("rtx.neeCache", float, specularFactor, 1.0, "Specular component factor.");
    RTX_OPTION("rtx.neeCache", float, learningRate, 0.02, "Learning rate. Higher values makes the cache adapt to lighting changes more quickly.");
    RTX_OPTION("rtx.neeCache", float, uniformSamplingProbability, 0.1, "Uniform sampling probability.");
    RTX_OPTION("rtx.neeCache", float, cullingThreshold, 0.01, "Culling threshold.");
    RTX_OPTION("rtx.neeCache", float, resolution, 8.0, "Cell resolution. Higher values mean smaller cells.");
    RTX_OPTION("rtx.neeCache", float, minRange, 400, "The range for lowest level cells.");
    RTX_OPTION("rtx.neeCache", float, emissiveTextureSampleFootprintScale, 1.0, "Emissive texture sample footprint scale.");
    RTX_OPTION("rtx.neeCache", bool,  approximateParticleLighting, true, "Use particle albedo as emissive color.");
    RTX_OPTION("rtx.neeCache", float, ageCullingSpeed, 0.02, "This threshold determines culling speed of an old triangle. A triangle that is not detected for several frames will be deemed less important and culled quicker.");
  private:
    Rc<vk::DeviceFn> m_vkd;
  };
} // namespace dxvk
