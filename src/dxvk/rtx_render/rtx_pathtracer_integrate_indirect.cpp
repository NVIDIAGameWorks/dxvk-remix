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
#include "rtx_pathtracer_integrate_indirect.h"
#include "dxvk_device.h"
#include "rtx_render/rtx_shader_manager.h"
#include "rtx_context.h"
#include "rtx_options.h"

#include "rtx/pass/common_binding_indices.h"
#include "rtx/pass/integrate/integrate_indirect_binding_indices.h"
#include "rtx/concept/surface_material/surface_material_hitgroup.h"

#include <rtx_shaders/integrate_indirect_raygen.h>
#include <rtx_shaders/integrate_indirect_raygen_ser.h>
#include <rtx_shaders/integrate_indirect_rayquery.h>
#include <rtx_shaders/integrate_indirect_rayquery_raygen.h>
#include <rtx_shaders/integrate_indirect_material_opaque_translucent_closestHit.h>
#include <rtx_shaders/integrate_indirect_material_rayPortal_closestHit.h>
#include <rtx_shaders/integrate_indirect_miss.h>

#include "dxvk_scoped_annotation.h"
#include "rtx_opacity_micromap_manager.h"

namespace dxvk {

  // Defined within an unnamed namespace to ensure unique definition across binary
  namespace {
    class IntegrateIndirectRayGenShader : public ManagedShader {
    public:
      BINDLESS_ENABLED()

      BEGIN_PARAMETER()
        COMMON_RAYTRACING_BINDINGS

        SAMPLERCUBE(INTEGRATE_INDIRECT_BINDING_SKYPROBE)

        TEXTURE2D(INTEGRATE_INDIRECT_BINDING_SHARED_FLAGS_INPUT)
        TEXTURE2D(INTEGRATE_INDIRECT_BINDING_SHARED_MEDIUM_MATERIAL_INDEX_INPUT)

        TEXTURE2D(INTEGRATE_INDIRECT_BINDING_PRIMARY_CONE_RADIUS_INPUT)
        TEXTURE2D(INTEGRATE_INDIRECT_BINDING_SECONDARY_CONE_RADIUS_INPUT)
        TEXTURE2D(INTEGRATE_INDIRECT_BINDING_PRIMARY_WORLD_POSITION_INPUT)
        RW_STRUCTURED_BUFFER(INTEGRATE_INDIRECT_BINDING_PRIMARY_RTXDI_RESERVOIR)

        TEXTURE2D(INTEGRATE_INDIRECT_BINDING_RAY_ORIGIN_DIRECTION_INPUT)
        TEXTURE2D(INTEGRATE_INDIRECT_BINDING_THROUGHPUT_CONE_RADIUS_INPUT)
        TEXTURE2D(INTEGRATE_INDIRECT_BINDING_FIRST_HIT_PERCEPTUAL_ROUGHNESS_INPUT)
        TEXTURE2D(INTEGRATE_INDIRECT_BINDING_FIRST_SAMPLED_LOBE_DATA_INPUT)
        TEXTURE2D(INTEGRATE_INDIRECT_BINDING_LAST_GBUFFER_INPUT)
        TEXTURE2D(INTEGRATE_INDIRECT_BINDING_PREV_WORLD_POSITION_INPUT)
        SAMPLER3D(INTEGRATE_INDIRECT_BINDING_VOLUME_FILTERED_RADIANCE_INPUT)
        TEXTURE2D(INTEGRATE_INDIRECT_BINDING_PRIMARY_HIT_DISTANCE_INPUT)
        TEXTURE2D(INTEGRATE_INDIRECT_BINDING_SECONDARY_HIT_DISTANCE_INPUT)
        TEXTURE2D(INTEGRATE_INDIRECT_BINDING_LAST_COMPOSITE_INPUT)

        RW_TEXTURE2D(INTEGRATE_INDIRECT_BINDING_DECAL_MATERIAL_STORAGE)
        RW_TEXTURE2D(INTEGRATE_INDIRECT_BINDING_DECAL_EMISSIVE_RADIANCE_STORAGE)

        RW_TEXTURE2D(INTEGRATE_INDIRECT_BINDING_INDIRECT_RADIANCE_HIT_DISTANCE_OUTPUT)
        RW_STRUCTURED_BUFFER(INTEGRATE_INDIRECT_BINDING_RESTIR_GI_RESERVOIR_OUTPUT)
        RW_TEXTURE2D(INTEGRATE_INDIRECT_BINDING_RESTIR_GI_RADIANCE_OUTPUT)
        RW_TEXTURE2D(INTEGRATE_INDIRECT_BINDING_RESTIR_GI_HIT_GEOMETRY_OUTPUT)

        RW_TEXTURE2D(INTEGRATE_INSTRUMENTATION)

      END_PARAMETER()
    };

    class IntegrateIndirectClosestHitShader : public ManagedShader {

      BEGIN_PARAMETER()
      END_PARAMETER()
    };

    class IntegrateIndirectMissShader : public ManagedShader {

      SHADER_SOURCE(IntegrateIndirectMissShader, VK_SHADER_STAGE_MISS_BIT_KHR, integrate_indirect_miss)

      BEGIN_PARAMETER()
      END_PARAMETER()
    };
  }

  DxvkPathtracerIntegrateIndirect::DxvkPathtracerIntegrateIndirect(DxvkDevice* device) : m_device(device) {
  }

  void DxvkPathtracerIntegrateIndirect::prewarmShaders(DxvkPipelineManager& pipelineManager) const {

    const bool isOpacityMicromapSupported = OpacityMicromapManager::checkIsOpacityMicromapSupported(m_device);

    for (int32_t useRayQuery = 1; useRayQuery >= 0; useRayQuery--)
      for (int32_t serEnabled = 1; serEnabled >= 0; serEnabled--)
        for (int32_t ommEnabled = isOpacityMicromapSupported; ommEnabled >= 0; ommEnabled--) {
          pipelineManager.registerRaytracingShaders(getPipelineShaders(useRayQuery, serEnabled, ommEnabled));
        }

    DxvkComputePipelineShaders shaders;
    shaders.cs = getComputeShader();
    pipelineManager.createComputePipeline(shaders);
  }

  void DxvkPathtracerIntegrateIndirect::dispatch(RtxContext* ctx, const Resources::RaytracingOutput& rtOutput) {
    ScopedGpuProfileZone(ctx, "Integrate Indirect Raytracing");

    const uint32_t frameIdx = ctx->getDevice()->getCurrentFrameId();

    // Bind resources

    // Note: Clamp to edge used to avoid interpolation to black on the edges of the view.
    Rc<DxvkSampler> linearSampler = ctx->getResourceManager().getSampler(VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);

    ctx->bindCommonRayTracingResources(rtOutput);

    ctx->bindResourceView(INTEGRATE_INDIRECT_BINDING_SKYPROBE, ctx->getResourceManager().getSkyProbe(ctx).view, nullptr);
    ctx->bindResourceSampler(INTEGRATE_INDIRECT_BINDING_SKYPROBE, linearSampler);

    ctx->bindResourceView(INTEGRATE_INDIRECT_BINDING_SHARED_FLAGS_INPUT, rtOutput.m_sharedFlags.view, nullptr);
    ctx->bindResourceView(INTEGRATE_INDIRECT_BINDING_SHARED_MEDIUM_MATERIAL_INDEX_INPUT, rtOutput.m_sharedMediumMaterialIndex.view, nullptr);

    ctx->bindResourceView(INTEGRATE_INDIRECT_BINDING_PRIMARY_CONE_RADIUS_INPUT, rtOutput.m_primaryConeRadius.view, nullptr);
    ctx->bindResourceView(INTEGRATE_INDIRECT_BINDING_SECONDARY_CONE_RADIUS_INPUT, rtOutput.m_secondaryConeRadius.view(Resources::AccessType::Read), nullptr);
    ctx->bindResourceView(INTEGRATE_INDIRECT_BINDING_PRIMARY_WORLD_POSITION_INPUT, rtOutput.getCurrentPrimaryWorldPositionWorldTriangleNormal().view, nullptr);
    ctx->bindResourceBuffer(INTEGRATE_INDIRECT_BINDING_PRIMARY_RTXDI_RESERVOIR, DxvkBufferSlice(rtOutput.m_rtxdiReservoirBuffer, 0, rtOutput.m_rtxdiReservoirBuffer->info().size));
    ctx->bindResourceView(INTEGRATE_INDIRECT_BINDING_RAY_ORIGIN_DIRECTION_INPUT, rtOutput.m_indirectRayOriginDirection.view(Resources::AccessType::Read), nullptr);
    ctx->bindResourceView(INTEGRATE_INDIRECT_BINDING_FIRST_HIT_PERCEPTUAL_ROUGHNESS_INPUT, rtOutput.m_indirectFirstHitPerceptualRoughness.view(Resources::AccessType::Read), nullptr);
    ctx->bindResourceView(INTEGRATE_INDIRECT_BINDING_LAST_GBUFFER_INPUT, rtOutput.m_gbufferLast.view, nullptr);
    ctx->bindResourceView(INTEGRATE_INDIRECT_BINDING_PREV_WORLD_POSITION_INPUT, rtOutput.getPreviousPrimaryWorldPositionWorldTriangleNormal().view, nullptr);

    ctx->bindResourceView(INTEGRATE_INDIRECT_BINDING_VOLUME_FILTERED_RADIANCE_INPUT, rtOutput.m_volumeFilteredRadiance.view, nullptr);
    ctx->bindResourceSampler(INTEGRATE_INDIRECT_BINDING_VOLUME_FILTERED_RADIANCE_INPUT, linearSampler);

    ctx->bindResourceView(INTEGRATE_INDIRECT_BINDING_PRIMARY_HIT_DISTANCE_INPUT, rtOutput.m_primaryHitDistance.view, nullptr);
    ctx->bindResourceView(INTEGRATE_INDIRECT_BINDING_SECONDARY_HIT_DISTANCE_INPUT, rtOutput.m_secondaryHitDistance.view, nullptr);

    const uint32_t isLastCompositeOutputValid = rtOutput.m_lastCompositeOutput.matchesWriteFrameIdx(frameIdx - 1);
    assert(isLastCompositeOutputValid == rtOutput.m_raytraceArgs.isLastCompositeOutputValid && "Last composite state changed since CB was initialized");
    ctx->bindResourceView(INTEGRATE_INDIRECT_BINDING_LAST_COMPOSITE_INPUT, rtOutput.m_lastCompositeOutput.view(Resources::AccessType::Read, isLastCompositeOutputValid), nullptr);
    ctx->bindResourceView(INTEGRATE_INDIRECT_BINDING_FIRST_SAMPLED_LOBE_DATA_INPUT, rtOutput.m_indirectFirstSampledLobeData.view(Resources::AccessType::Read), nullptr);

    // Storage resources
    ctx->bindResourceView(INTEGRATE_INDIRECT_BINDING_DECAL_MATERIAL_STORAGE, rtOutput.m_decalMaterial.view(Resources::AccessType::Write), nullptr);

    // Output resources
    ctx->bindResourceBuffer(INTEGRATE_INDIRECT_BINDING_RESTIR_GI_RESERVOIR_OUTPUT, DxvkBufferSlice(rtOutput.m_restirGIReservoirBuffer, 0, rtOutput.m_restirGIReservoirBuffer->info().size));
    ctx->bindResourceView(INTEGRATE_INDIRECT_BINDING_RESTIR_GI_RADIANCE_OUTPUT, rtOutput.m_restirGIRadiance.view(Resources::AccessType::Write), nullptr);
    ctx->bindResourceView(INTEGRATE_INDIRECT_BINDING_RESTIR_GI_HIT_GEOMETRY_OUTPUT, rtOutput.m_restirGIHitGeometry.view, nullptr);

    // Aliased resources
    // m_indirectRadiance writes the actual output carried forward and therefore it must be bound with write access last
    ctx->bindResourceView(INTEGRATE_INDIRECT_BINDING_THROUGHPUT_CONE_RADIUS_INPUT, rtOutput.m_indirectThroughputConeRadius.view(Resources::AccessType::Read), nullptr);
    ctx->bindResourceView(INTEGRATE_INDIRECT_BINDING_DECAL_EMISSIVE_RADIANCE_STORAGE, rtOutput.m_decalEmissiveRadiance.view(Resources::AccessType::Write), nullptr);
    ctx->bindResourceView(INTEGRATE_INDIRECT_BINDING_INDIRECT_RADIANCE_HIT_DISTANCE_OUTPUT, rtOutput.m_indirectRadianceHitDistance.view(Resources::AccessType::Write), nullptr);

    DebugView& debugView = ctx->getDevice()->getCommon()->metaDebugView();
    ctx->bindResourceView(INTEGRATE_INSTRUMENTATION, debugView.getInstrumentation(), nullptr);

    const auto rayDims = rtOutput.m_compositeOutputExtent;

    const bool serEnabled = RtxOptions::Get()->isShaderExecutionReorderingInPathtracerIntegrateIndirectEnabled();
    const bool ommEnabled = RtxOptions::Get()->getEnableOpacityMicromap();

    switch (RtxOptions::Get()->getRenderPassIntegrateIndirectRaytraceMode()) {
    case RaytraceMode::RayQuery:
      VkExtent3D workgroups = util::computeBlockCount(rayDims, VkExtent3D { 16, 8, 1 });
      ctx->bindShader(VK_SHADER_STAGE_COMPUTE_BIT, getComputeShader());
      ctx->dispatch(workgroups.width, workgroups.height, workgroups.depth);
      break;
    case RaytraceMode::RayQueryRayGen:
      ctx->bindRaytracingPipelineShaders(getPipelineShaders(true, serEnabled, ommEnabled));
      ctx->traceRays(rayDims.width, rayDims.height, rayDims.depth);
      break;
    case RaytraceMode::TraceRay:
      ctx->bindRaytracingPipelineShaders(getPipelineShaders(false, serEnabled, ommEnabled));
      ctx->traceRays(rayDims.width, rayDims.height, rayDims.depth);
      break;
    }
  }

  DxvkRaytracingPipelineShaders DxvkPathtracerIntegrateIndirect::getPipelineShaders(const bool useRayQuery,
                                                                                    const bool serEnabled,
                                                                                    const bool ommEnabled) {

    DxvkRaytracingPipelineShaders shaders;
    if (useRayQuery) {
      shaders.addGeneralShader(GET_SHADER_VARIANT(VK_SHADER_STAGE_RAYGEN_BIT_KHR, IntegrateIndirectRayGenShader, integrate_indirect_rayquery_raygen));
      shaders.debugName = "Integrate Indirect RayQuery (RGS)";
    } else {
      if (serEnabled)
        shaders.addGeneralShader(GET_SHADER_VARIANT(VK_SHADER_STAGE_RAYGEN_BIT_KHR, IntegrateIndirectRayGenShader, integrate_indirect_raygen_ser));
      else
        shaders.addGeneralShader(GET_SHADER_VARIANT(VK_SHADER_STAGE_RAYGEN_BIT_KHR, IntegrateIndirectRayGenShader, integrate_indirect_raygen));
      shaders.addGeneralShader(IntegrateIndirectMissShader::getShader());

      ADD_HIT_GROUPS(IntegrateIndirectClosestHitShader, integrate_indirect);

      shaders.debugName = "Integrate Indirect TraceRay (RGS)";
    }

    if (ommEnabled)
      shaders.pipelineFlags |= VK_PIPELINE_CREATE_RAY_TRACING_OPACITY_MICROMAP_BIT_EXT;

    return shaders;
  }

  Rc<DxvkShader> DxvkPathtracerIntegrateIndirect::getComputeShader() const {
    return GET_SHADER_VARIANT(VK_SHADER_STAGE_COMPUTE_BIT, IntegrateIndirectRayGenShader, integrate_indirect_rayquery);
  }

  const char* DxvkPathtracerIntegrateIndirect::raytraceModeToString(RaytraceMode raytraceMode) {
    switch (raytraceMode) {
    case RaytraceMode::RayQuery:
      return "Ray Query [CS]";
    case RaytraceMode::RayQueryRayGen:
      return "Ray Query [RGS]";
    case RaytraceMode::TraceRay:
      return "Trace Ray [RGS]";
    default:
      return "Unknown";
    }
  }
}
