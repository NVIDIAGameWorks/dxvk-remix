/*
* Copyright (c) 2022-2023, NVIDIA CORPORATION. All rights reserved.
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
#include "rtx_neecache.h"
#include "dxvk_device.h"
#include "rtx.h"
#include "rtx/pass/common_binding_indices.h"
#include "rtx/pass/demodulate/demodulate_binding_indices.h"
#include "rtx/pass/demodulate/update_nee_cache_binding_indices.h"
#include "rtx_render/rtx_shader_manager.h"
#include "dxvk_scoped_annotation.h"
#include "dxvk_context.h"
#include "rtx_context.h"
#include "rtx_imgui.h"
#include "rtx/pass/raytrace_args.h"
#include "rtx/algorithm/neecache.h"

#include <rtx_shaders/demodulate.h>
#include <rtx_shaders/updateNeeCache.h>

namespace dxvk {

  // Defined within an unnamed namespace to ensure unique definition across binary
  namespace {
    // class DemodulateShader : public ManagedShader {
    //   SHADER_SOURCE(DemodulateShader, VK_SHADER_STAGE_COMPUTE_BIT, demodulate)

    //   PUSH_CONSTANTS(VkExtent2D)

    //   BEGIN_PARAMETER()
    //     CONSTANT_BUFFER(DEMODULATE_BINDING_CONSTANTS)
    //     TEXTURE2D(DEMODULATE_BINDING_SHARED_FLAGS_INPUT)
    //     TEXTURE2D(DEMODULATE_BINDING_PRIMARY_VIRTUAL_WORLD_SHADING_NORMAL_INPUT)
    //     TEXTURE2D(DEMODULATE_BINDING_PRIMARY_LINEAR_VIEW_Z_INPUT)
    //     TEXTURE2D(DEMODULATE_BINDING_PRIMARY_ALBEDO_INPUT)
    //     TEXTURE2D(DEMODULATE_BINDING_SECONDARY_VIRTUAL_WORLD_SHADING_NORMAL_INPUT)
    //     TEXTURE2D(DEMODULATE_BINDING_SECONDARY_LINEAR_VIEW_Z_INPUT)
    //     TEXTURE2D(DEMODULATE_BINDING_SECONDARY_ALBEDO_INPUT)
    //     TEXTURE2D(DEMODULATE_BINDING_INDIRECT_RADIANCE_HIT_DISTANCE_INPUT)
    //     TEXTURE2D(DEMODULATE_BINDING_PRIMARY_BASE_REFLECTIVITY_INPUT)
    //     TEXTURE2D(DEMODULATE_BINDING_SECONDARY_BASE_REFLECTIVITY_INPUT)
    //     RW_TEXTURE2D(DEMODULATE_BINDING_PRIMARY_DIRECT_DIFFUSE_RADIANCE_INPUT_OUTPUT)
    //     RW_TEXTURE2D(DEMODULATE_BINDING_PRIMARY_DIRECT_SPECULAR_RADIANCE_INPUT_OUTPUT)
    //     RW_TEXTURE2D(DEMODULATE_BINDING_PRIMARY_INDIRECT_DIFFUSE_RADIANCE_INPUT_OUTPUT)
    //     RW_TEXTURE2D(DEMODULATE_BINDING_PRIMARY_INDIRECT_SPECULAR_RADIANCE_INPUT_OUTPUT)
    //     RW_TEXTURE2D(DEMODULATE_BINDING_SECONDARY_COMBINED_DIFFUSE_RADIANCE_INPUT_OUTPUT)
    //     RW_TEXTURE2D(DEMODULATE_BINDING_SECONDARY_COMBINED_SPECULAR_RADIANCE_INPUT_OUTPUT)
    //     RW_TEXTURE2D(DEMODULATE_BINDING_PRIMARY_SPECULAR_ALBEDO_OUTPUT)
    //     RW_TEXTURE2D(DEMODULATE_BINDING_SECONDARY_SPECULAR_ALBEDO_OUTPUT)
    //     RW_TEXTURE2D(DEMODULATE_BINDING_DEBUG_VIEW_OUTPUT)
    //     RW_STRUCTURED_BUFFER(DEMODULATE_BINDING_RADIANCE_CACHE)
    //     RW_STRUCTURED_BUFFER(DEMODULATE_BINDING_RADIANCE_CACHE_TASK)
    //   END_PARAMETER()
    // };

    // PREWARM_SHADER_PIPELINE(DemodulateShader);

    class UpdateNEECacheShader : public ManagedShader {
      SHADER_SOURCE(UpdateNEECacheShader, VK_SHADER_STAGE_COMPUTE_BIT, updateNeeCache)

      //PUSH_CONSTANTS(VkExtent2D)
      BINDLESS_ENABLED()

      BEGIN_PARAMETER()
        COMMON_RAYTRACING_BINDINGS
        //CONSTANT_BUFFER(UPDATE_NEE_CACHE_BINDING_CONSTANTS)
        //TEXTURE2D(UPDATE_NEE_CACHE_BINDING_SHARED_FLAGS_INPUT)
        //TEXTURE2D(UPDATE_NEE_CACHE_BINDING_PRIMARY_VIRTUAL_WORLD_SHADING_NORMAL_INPUT)
        //TEXTURE2D(UPDATE_NEE_CACHE_BINDING_PRIMARY_LINEAR_VIEW_Z_INPUT)
        //TEXTURE2D(UPDATE_NEE_CACHE_BINDING_PRIMARY_ALBEDO_INPUT)
        //TEXTURE2D(UPDATE_NEE_CACHE_BINDING_SECONDARY_VIRTUAL_WORLD_SHADING_NORMAL_INPUT)
        //TEXTURE2D(UPDATE_NEE_CACHE_BINDING_SECONDARY_LINEAR_VIEW_Z_INPUT)
        //TEXTURE2D(UPDATE_NEE_CACHE_BINDING_SECONDARY_ALBEDO_INPUT)
        //TEXTURE2D(UPDATE_NEE_CACHE_BINDING_INDIRECT_RADIANCE_HIT_DISTANCE_INPUT)
        //TEXTURE2D(UPDATE_NEE_CACHE_BINDING_PRIMARY_BASE_REFLECTIVITY_INPUT)
        //TEXTURE2D(UPDATE_NEE_CACHE_BINDING_SECONDARY_BASE_REFLECTIVITY_INPUT)
        //RW_TEXTURE2D(UPDATE_NEE_CACHE_BINDING_PRIMARY_DIRECT_DIFFUSE_RADIANCE_INPUT_OUTPUT)
        //RW_TEXTURE2D(UPDATE_NEE_CACHE_BINDING_PRIMARY_DIRECT_SPECULAR_RADIANCE_INPUT_OUTPUT)
        //RW_TEXTURE2D(UPDATE_NEE_CACHE_BINDING_PRIMARY_INDIRECT_DIFFUSE_RADIANCE_INPUT_OUTPUT)
        //RW_TEXTURE2D(UPDATE_NEE_CACHE_BINDING_PRIMARY_INDIRECT_SPECULAR_RADIANCE_INPUT_OUTPUT)
        //RW_TEXTURE2D(UPDATE_NEE_CACHE_BINDING_SECONDARY_COMBINED_DIFFUSE_RADIANCE_INPUT_OUTPUT)
        //RW_TEXTURE2D(UPDATE_NEE_CACHE_BINDING_SECONDARY_COMBINED_SPECULAR_RADIANCE_INPUT_OUTPUT)
        //RW_TEXTURE2D(UPDATE_NEE_CACHE_BINDING_PRIMARY_SPECULAR_ALBEDO_OUTPUT)
        //RW_TEXTURE2D(UPDATE_NEE_CACHE_BINDING_SECONDARY_SPECULAR_ALBEDO_OUTPUT)
        //RW_TEXTURE2D(UPDATE_NEE_CACHE_BINDING_DEBUG_VIEW_OUTPUT)
        RW_STRUCTURED_BUFFER(UPDATE_NEE_CACHE_BINDING_RADIANCE_CACHE)
        RW_STRUCTURED_BUFFER(UPDATE_NEE_CACHE_BINDING_RADIANCE_CACHE_TASK)
        RW_TEXTURE2D(UPDATE_NEE_CACHE_BINDING_RADIANCE_CACHE_THREAD_TASK)
      END_PARAMETER()
    };

    PREWARM_SHADER_PIPELINE(UpdateNEECacheShader);
  }

  NeeCachePass::NeeCachePass(dxvk::DxvkDevice* device)
    : m_vkd(device->vkd()), m_device(device) {
  }

  NeeCachePass::~NeeCachePass() { }

  void NeeCachePass::showImguiSettings() {
    ImGui::Checkbox("Enabled", &enabledObject());
    ImGui::Checkbox("Enable Importance Sampling", &enableImportanceSamplingObject());
    ImGui::DragFloat("Cache Range", &rangeObject(), 1.f, 0.1f, 10000000.0f, "%.3f");
  }

  void NeeCachePass::dispatch(RtxContext* ctx, const Resources::RaytracingOutput& rtOutput) {
    const auto& numRaysExtent = rtOutput.m_compositeOutputExtent;
    VkExtent3D workgroups = util::computeBlockCount(numRaysExtent, VkExtent3D{ 16, 8, 1 });

    ScopedGpuProfileZone(ctx, "NEE Cache");

    Rc<DxvkBuffer> constantsBuffer = ctx->getResourceManager().getConstantsBuffer();
    DebugView& debugView = ctx->getDevice()->getCommon()->metaDebugView();

    // Bind resources

    {
      ctx->bindCommonRayTracingResources(rtOutput);
      ctx->bindResourceBuffer(UPDATE_NEE_CACHE_BINDING_RADIANCE_CACHE, DxvkBufferSlice(rtOutput.m_radianceCache, 0, rtOutput.m_radianceCache->info().size));
      ctx->bindResourceBuffer(UPDATE_NEE_CACHE_BINDING_RADIANCE_CACHE_TASK, DxvkBufferSlice(rtOutput.m_radianceCacheTask, 0, rtOutput.m_radianceCacheTask->info().size));
      ctx->bindResourceView(UPDATE_NEE_CACHE_BINDING_RADIANCE_CACHE_THREAD_TASK, rtOutput.m_radianceCacheThreadTask.view, nullptr);

      ctx->bindShader(VK_SHADER_STAGE_COMPUTE_BIT, UpdateNEECacheShader::getShader());
      ctx->dispatch(RADIANCE_CACHE_PROBE_RESOLUTION, RADIANCE_CACHE_PROBE_RESOLUTION, RADIANCE_CACHE_PROBE_RESOLUTION);
    }
  }
  
} // namespace dxvk
