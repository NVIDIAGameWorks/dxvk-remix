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
#include "rtx_nee_cache.h"
#include "dxvk_device.h"
#include "rtx.h"
#include "rtx/pass/common_binding_indices.h"
#include "rtx/pass/demodulate/demodulate_binding_indices.h"
#include "rtx/pass/nee_cache/update_nee_cache_binding_indices.h"
#include "rtx_render/rtx_shader_manager.h"
#include "dxvk_scoped_annotation.h"
#include "dxvk_context.h"
#include "rtx_context.h"
#include "rtx_imgui.h"
#include "rtx/pass/raytrace_args.h"
#include "rtx/algorithm/nee_cache.h"

#include <rtx_shaders/demodulate.h>
#include <rtx_shaders/update_nee_cache.h>
#include <rtx_shaders/update_nee_task.h>

namespace dxvk {

  // Defined within an unnamed namespace to ensure unique definition across binary
  namespace {
    class UpdateNEECacheShader : public ManagedShader {
      SHADER_SOURCE(UpdateNEECacheShader, VK_SHADER_STAGE_COMPUTE_BIT, update_nee_cache)

      BINDLESS_ENABLED()

      BEGIN_PARAMETER()
        COMMON_RAYTRACING_BINDINGS
        RW_STRUCTURED_BUFFER(UPDATE_NEE_CACHE_BINDING_NEE_CACHE)
        RW_STRUCTURED_BUFFER(UPDATE_NEE_CACHE_BINDING_NEE_CACHE_TASK)
        RW_TEXTURE2D(UPDATE_NEE_CACHE_BINDING_NEE_CACHE_THREAD_TASK)
      END_PARAMETER()
    };

    PREWARM_SHADER_PIPELINE(UpdateNEECacheShader);


    class UpdateNEETaskShader : public ManagedShader {
      SHADER_SOURCE(UpdateNEETaskShader, VK_SHADER_STAGE_COMPUTE_BIT, update_nee_task)

      BINDLESS_ENABLED()

      BEGIN_PARAMETER()
        COMMON_RAYTRACING_BINDINGS
        RW_STRUCTURED_BUFFER(UPDATE_NEE_CACHE_BINDING_NEE_CACHE)
        RW_STRUCTURED_BUFFER(UPDATE_NEE_CACHE_BINDING_NEE_CACHE_TASK)
        RW_TEXTURE2D(UPDATE_NEE_CACHE_BINDING_NEE_CACHE_THREAD_TASK)
      END_PARAMETER()
    };

    PREWARM_SHADER_PIPELINE(UpdateNEETaskShader);
  }

  NeeCachePass::NeeCachePass(dxvk::DxvkDevice* device)
    : m_vkd(device->vkd()), m_device(device) {
  }

  NeeCachePass::~NeeCachePass() { }

  void NeeCachePass::showImguiSettings() {
    ImGui::Checkbox("Enabled", &enabledObject());
    ImGui::Checkbox("Enable Importance Sampling", &enableImportanceSamplingObject());
    ImGui::Checkbox("Enable MIS", &enableMISObject());
    ImGui::Checkbox("Enable in First Bounce", &enableInFirstBounceObject());
    ImGui::Checkbox("Enable in Higher Bounces", &enableInHigherBouncesObject());
    ImGui::Checkbox("Enable Random Replacement", &enableRandomReplacementObject());
    ImGui::DragFloat("Texture Sample Footprint Size", &textureSampleFootprintSizeObject(), 0.001f, 0.f, 20.f, "%.3f");
    ImGui::DragFloat("Age Culling Speed", &ageCullingSpeedObject(), 0.001f, 0.0f, 0.99f, "%.3f");
    ImGui::DragFloat("Cache Range", &rangeObject(), 1.f, 0.1f, 10000000.0f, "%.3f");
  }

  void NeeCachePass::setRaytraceArgs(RaytraceArgs& constants) const {    
    constants.neeCacheArgs.enable = enabled();
    constants.neeCacheArgs.enableImportanceSampling = enableImportanceSampling();
    constants.neeCacheArgs.enableMIS = enableMIS();
    constants.neeCacheArgs.enableInFirstBounce = enableInFirstBounce();
    constants.neeCacheArgs.enableInHigherBounces = enableInHigherBounces();
    constants.neeCacheArgs.enableRandomReplacement = enableRandomReplacement();
    constants.neeCacheArgs.range = range();
    constants.neeCacheArgs.textureSampleFootprintSize = textureSampleFootprintSize();
    constants.neeCacheArgs.ageCullingSpeed = ageCullingSpeed();
  }

  void NeeCachePass::dispatch(RtxContext* ctx, const Resources::RaytracingOutput& rtOutput) {
    const auto& numRaysExtent = rtOutput.m_compositeOutputExtent;
    VkExtent3D workgroups = util::computeBlockCount(numRaysExtent, VkExtent3D{ 16, 8, 1 });

    ScopedGpuProfileZone(ctx, "NEE Cache");

    // Bind resources
    {
      ctx->bindCommonRayTracingResources(rtOutput);
      ctx->bindResourceBuffer(UPDATE_NEE_CACHE_BINDING_NEE_CACHE, DxvkBufferSlice(rtOutput.m_neeCache, 0, rtOutput.m_neeCache->info().size));
      ctx->bindResourceBuffer(UPDATE_NEE_CACHE_BINDING_NEE_CACHE_TASK, DxvkBufferSlice(rtOutput.m_neeCacheTask, 0, rtOutput.m_neeCacheTask->info().size));
      ctx->bindResourceView(UPDATE_NEE_CACHE_BINDING_NEE_CACHE_THREAD_TASK, rtOutput.m_neeCacheThreadTask.view, nullptr);

      ctx->bindShader(VK_SHADER_STAGE_COMPUTE_BIT, UpdateNEETaskShader::getShader());
      ctx->dispatch(workgroups.width, workgroups.height, workgroups.depth);
    }

    {
      ctx->bindCommonRayTracingResources(rtOutput);
      ctx->bindResourceBuffer(UPDATE_NEE_CACHE_BINDING_NEE_CACHE, DxvkBufferSlice(rtOutput.m_neeCache, 0, rtOutput.m_neeCache->info().size));
      ctx->bindResourceBuffer(UPDATE_NEE_CACHE_BINDING_NEE_CACHE_TASK, DxvkBufferSlice(rtOutput.m_neeCacheTask, 0, rtOutput.m_neeCacheTask->info().size));
      ctx->bindResourceView(UPDATE_NEE_CACHE_BINDING_NEE_CACHE_THREAD_TASK, rtOutput.m_neeCacheThreadTask.view, nullptr);

      ctx->bindShader(VK_SHADER_STAGE_COMPUTE_BIT, UpdateNEECacheShader::getShader());
      ctx->dispatch(NEE_CACHE_PROBE_RESOLUTION, NEE_CACHE_PROBE_RESOLUTION, NEE_CACHE_PROBE_RESOLUTION);
    }
  }
  
} // namespace dxvk
