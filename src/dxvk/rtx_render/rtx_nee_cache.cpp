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

#include <rtx_shaders/demodulate.h>
#include <rtx_shaders/update_nee_cache.h>

namespace dxvk {

  ImGui::ComboWithKey<NeeEnableMode> enableModeAfterFirstBounceCombo {
    "Enable Mode After First Bounce",
    ImGui::ComboWithKey<NeeEnableMode>::ComboEntries { {
        {NeeEnableMode::None, "None"},
        {NeeEnableMode::SpecularOnly, "Specular Only"},
        {NeeEnableMode::All, "All"},
    } }
  };

  // Defined within an unnamed namespace to ensure unique definition across binary
  namespace {
    class UpdateNEECacheShader : public ManagedShader {
      SHADER_SOURCE(UpdateNEECacheShader, VK_SHADER_STAGE_COMPUTE_BIT, update_nee_cache)

      BINDLESS_ENABLED()

      BEGIN_PARAMETER()
        COMMON_RAYTRACING_BINDINGS
        RW_STRUCTURED_BUFFER(UPDATE_NEE_CACHE_BINDING_NEE_CACHE)
        RW_STRUCTURED_BUFFER(UPDATE_NEE_CACHE_BINDING_NEE_CACHE_TASK)
        RW_STRUCTURED_BUFFER(UPDATE_NEE_CACHE_BINDING_NEE_CACHE_SAMPLE)
        TEXTURE2D(UPDATE_NEE_CACHE_BINDING_NEE_CACHE_THREAD_TASK)
        STRUCTURED_BUFFER(UPDATE_NEE_CACHE_BINDING_PRIMITIVE_ID_PREFIX_SUM)
        STRUCTURED_BUFFER(UPDATE_NEE_CACHE_BINDING_LAST_PRIMITIVE_ID_PREFIX_SUM)
      END_PARAMETER()
    };

    PREWARM_SHADER_PIPELINE(UpdateNEECacheShader);
  }

  NeeCachePass::NeeCachePass(dxvk::DxvkDevice* device)
    : m_vkd(device->vkd()) {
  }

  NeeCachePass::~NeeCachePass() { }

  void NeeCachePass::showImguiSettings() {
    ImGui::Checkbox("Enable NEE Cache", &enableObject());
    ImGui::Checkbox("Enable Importance Sampling", &enableImportanceSamplingObject());
    ImGui::Checkbox("Enable MIS", &enableMISObject());
    ImGui::Checkbox("Enable Update", &enableUpdateObject());
    ImGui::Checkbox("Enable On First Bounce", &enableOnFirstBounceObject());
    enableModeAfterFirstBounceCombo.getKey(&enableModeAfterFirstBounceObject());
    ImGui::Checkbox("Enable Analytical Light", &enableAnalyticalLightObject());
    ImGui::DragFloat("Specular Factor", &specularFactorObject(), 0.01f, 0.f, 20.f, "%.3f");
    ImGui::DragFloat("Learning Rate", &learningRateObject(), 0.01f, 0.f, 1.f, "%.3f");
    ImGui::DragFloat("Uniform Sampling Probability", &uniformSamplingProbabilityObject(), 0.01f, 0.f, 1.f, "%.3f");
    ImGui::DragFloat("Culling Threshold", &cullingThresholdObject(), 0.001f, 0.f, 1.f, "%.3f");
    ImGui::DragFloat("Emissive Texture Sample Footprint Scale", &emissiveTextureSampleFootprintScaleObject(), 0.001f, 0.f, 20.f, "%.3f");
    ImGui::DragFloat("Age Culling Speed", &ageCullingSpeedObject(), 0.001f, 0.0f, 0.99f, "%.3f");
    ImGui::DragFloat("Cell Resolution", &resolutionObject(), 0.01f, 0.01f, 100.0f, "%.3f");
    ImGui::DragFloat("Min Range", &minRangeObject(), 1.f, 0.1f, 10000.0f, "%.3f");
    ImGui::Checkbox("Approximate Particle Lighting", &approximateParticleLightingObject());
  }

  void NeeCachePass::setRaytraceArgs(RaytraceArgs& constants, bool resetHistory) const {    
    constants.neeCacheArgs.enable = enable();
    constants.neeCacheArgs.enableImportanceSampling = enableImportanceSampling();
    constants.neeCacheArgs.enableMIS = enableMIS();
    constants.neeCacheArgs.enableOnFirstBounce = enableOnFirstBounce();
    constants.neeCacheArgs.enableAnalyticalLight = enableAnalyticalLight();
    constants.neeCacheArgs.specularFactor = specularFactor();
    constants.neeCacheArgs.uniformSamplingProbability = uniformSamplingProbability();
    constants.neeCacheArgs.enableModeAfterFirstBounce = enableModeAfterFirstBounce();
    constants.neeCacheArgs.emissiveTextureSampleFootprintScale = emissiveTextureSampleFootprintScale();
    constants.neeCacheArgs.ageCullingSpeed = ageCullingSpeed();
    constants.neeCacheArgs.cullingThreshold = cullingThreshold();
    constants.neeCacheArgs.learningRate = learningRate();
    constants.neeCacheArgs.resolution = resolution();
    constants.neeCacheArgs.minRange = minRange() * RtxOptions::Get()->sceneScale();
    constants.neeCacheArgs.approximateParticleLighting = approximateParticleLighting();

    static uvec2 oldResolution {0, 0};
    constants.neeCacheArgs.clearCache = resetHistory || oldResolution.x != constants.camera.resolution.x || oldResolution.y != constants.camera.resolution.y;
    oldResolution = constants.camera.resolution;
  }

  void NeeCachePass::dispatch(RtxContext* ctx, const Resources::RaytracingOutput& rtOutput) {
    if (!enable() || !enableUpdate()) {
      return;
    }

    const VkExtent3D& numRaysExtent = rtOutput.m_compositeOutputExtent;
    VkExtent3D workgroups = util::computeBlockCount(numRaysExtent, VkExtent3D{ 16, 8, 1 });
    Rc<DxvkBuffer> primitiveIDPrefixSumBuffer = ctx->getSceneManager().getCurrentFramePrimitiveIDPrefixSumBuffer();
    Rc<DxvkBuffer> lastPrimitiveIDPrefixSumBuffer = ctx->getSceneManager().getLastFramePrimitiveIDPrefixSumBuffer();

    ScopedGpuProfileZone(ctx, "NEE Cache");

    // Bind resources
    {
      ScopedGpuProfileZone(ctx, "UpdateNEECacheShader");
      ctx->bindCommonRayTracingResources(rtOutput);
      ctx->bindResourceBuffer(UPDATE_NEE_CACHE_BINDING_NEE_CACHE, DxvkBufferSlice(rtOutput.m_neeCache, 0, rtOutput.m_neeCache->info().size));
      ctx->bindResourceBuffer(UPDATE_NEE_CACHE_BINDING_NEE_CACHE_TASK, DxvkBufferSlice(rtOutput.m_neeCacheTask, 0, rtOutput.m_neeCacheTask->info().size));
      ctx->bindResourceBuffer(UPDATE_NEE_CACHE_BINDING_NEE_CACHE_SAMPLE, DxvkBufferSlice(rtOutput.m_neeCacheSample, 0, rtOutput.m_neeCacheSample->info().size));
      ctx->bindResourceBuffer(UPDATE_NEE_CACHE_BINDING_PRIMITIVE_ID_PREFIX_SUM, DxvkBufferSlice(primitiveIDPrefixSumBuffer, 0, primitiveIDPrefixSumBuffer->info().size));
      ctx->bindResourceBuffer(UPDATE_NEE_CACHE_BINDING_LAST_PRIMITIVE_ID_PREFIX_SUM, DxvkBufferSlice(lastPrimitiveIDPrefixSumBuffer, 0, lastPrimitiveIDPrefixSumBuffer->info().size));
      ctx->bindResourceView(UPDATE_NEE_CACHE_BINDING_NEE_CACHE_THREAD_TASK, rtOutput.m_neeCacheThreadTask.view, nullptr);

      // NEE Cache update updates the nee cache based on last frame's record.
      // The cache is a world space hash grid storing short light and emissive triangle lists.
      // Each frame the integrator generates some records to update the cache in the next frame
      ctx->bindShader(VK_SHADER_STAGE_COMPUTE_BIT, UpdateNEECacheShader::getShader());
      ctx->dispatch(NEE_CACHE_PROBE_RESOLUTION, NEE_CACHE_PROBE_RESOLUTION/8, NEE_CACHE_PROBE_RESOLUTION);
    }
  }
  
} // namespace dxvk
