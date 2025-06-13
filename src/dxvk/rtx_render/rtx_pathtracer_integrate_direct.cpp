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
#include "rtx_pathtracer_integrate_direct.h"
#include "dxvk_device.h"
#include "rtx_shader_manager.h"

#include "rtx/pass/common_binding_indices.h"
#include "rtx/pass/integrate/integrate_direct_binding_indices.h"

#include <rtx_shaders/integrate_direct_rayquery.h>
#include <rtx_shaders/integrate_direct_rayquery_raygen.h>

#include "dxvk_scoped_annotation.h"
#include "rtx_context.h"
#include "rtx_options.h"
#include "rtx_opacity_micromap_manager.h"

namespace dxvk {

  // Defined within an unnamed namespace to ensure unique definition across binary
  namespace {
    class IntegrateDirectRayGenShader : public ManagedShader {
    public:
      BINDLESS_ENABLED()

      BEGIN_PARAMETER()
        COMMON_RAYTRACING_BINDINGS

        SAMPLERCUBE(INTEGRATE_DIRECT_BINDING_SKYPROBE)

        TEXTURE2D(INTEGRATE_DIRECT_BINDING_SHARED_INTEGRATION_SURFACE_PDF_INPUT)
        TEXTURE2D(INTEGRATE_DIRECT_BINDING_SHARED_MATERIAL_DATA0_INPUT)
        TEXTURE2D(INTEGRATE_DIRECT_BINDING_SHARED_MATERIAL_DATA1_INPUT)
        TEXTURE2D(INTEGRATE_DIRECT_BINDING_SHARED_TEXTURE_COORD_INPUT)
        TEXTURE2D(INTEGRATE_DIRECT_BINDING_SHARED_SURFACE_INDEX_INPUT)
        TEXTURE2D(INTEGRATE_DIRECT_BINDING_SHARED_SUBSURFACE_DATA_INPUT)
        TEXTURE2D(INTEGRATE_DIRECT_BINDING_SHARED_SUBSURFACE_DIFFUSION_PROFILE_DATA_INPUT)

        TEXTURE2D(INTEGRATE_DIRECT_BINDING_PRIMARY_WORLD_SHADING_NORMAL_INPUT)
        TEXTURE2D(INTEGRATE_DIRECT_BINDING_PRIMARY_PERCEPTUAL_ROUGHNESS_INPUT)
        TEXTURE2D(INTEGRATE_DIRECT_BINDING_PRIMARY_ALBEDO_INPUT)
        TEXTURE2D(INTEGRATE_DIRECT_BINDING_PRIMARY_VIEW_DIRECTION_INPUT)
        TEXTURE2D(INTEGRATE_DIRECT_BINDING_PRIMARY_CONE_RADIUS_INPUT)
        TEXTURE2D(INTEGRATE_DIRECT_BINDING_PRIMARY_WORLD_POSITION_WORLD_TRIANGLE_NORMAL_INPUT)
        TEXTURE2D(INTEGRATE_DIRECT_BINDING_PRIMARY_POSITION_ERROR_INPUT)
        RW_STRUCTURED_BUFFER(INTEGRATE_DIRECT_BINDING_PRIMARY_RTXDI_RESERVOIR)

        TEXTURE2D(INTEGRATE_DIRECT_BINDING_SECONDARY_WORLD_SHADING_NORMAL_INPUT)
        TEXTURE2D(INTEGRATE_DIRECT_BINDING_SECONDARY_PERCEPTUAL_ROUGHNESS_INPUT)
        TEXTURE2D(INTEGRATE_DIRECT_BINDING_SECONDARY_ALBEDO_INPUT)
        TEXTURE2D(INTEGRATE_DIRECT_BINDING_SECONDARY_VIEW_DIRECTION_INPUT)
        TEXTURE2D(INTEGRATE_DIRECT_BINDING_SECONDARY_CONE_RADIUS_INPUT)
        TEXTURE2D(INTEGRATE_DIRECT_BINDING_SECONDARY_WORLD_POSITION_WORLD_TRIANGLE_NORMAL_INPUT)
        TEXTURE2D(INTEGRATE_DIRECT_BINDING_SECONDARY_POSITION_ERROR_INPUT)

        RW_TEXTURE2D(INTEGRATE_DIRECT_BINDING_SHARED_FLAGS_INPUT_OUTPUT)
        RW_TEXTURE2D(INTEGRATE_DIRECT_BINDING_SHARED_MEDIUM_MATERIAL_INDEX_INPUT_OUTPUT)
        RW_TEXTURE2D(INTEGRATE_DIRECT_BINDING_PRIMARY_BASE_REFLECTIVITY_INPUT_OUTPUT)
        RW_TEXTURE2D(INTEGRATE_DIRECT_BINDING_SECONDARY_BASE_REFLECTIVITY_INPUT_OUTPUT)

        RW_TEXTURE2D(INTEGRATE_DIRECT_BINDING_PRIMARY_DIRECT_DIFFUSE_LOBE_RADIANCE_OUTPUT)
        RW_TEXTURE2D(INTEGRATE_DIRECT_BINDING_PRIMARY_DIRECT_SPECULAR_LOBE_RADIANCE_OUTPUT)
        RW_TEXTURE2D(INTEGRATE_DIRECT_BINDING_SECONDARY_COMBINED_DIFFUSE_LOBE_RADIANCE_OUTPUT)
        RW_TEXTURE2D(INTEGRATE_DIRECT_BINDING_SECONDARY_COMBINED_SPECULAR_LOBE_RADIANCE_OUTPUT)
        RW_TEXTURE2D(INTEGRATE_DIRECT_BINDING_PRIMARY_RTXDI_ILLUMINANCE_OUTPUT)

        STRUCTURED_BUFFER(INTEGRATE_DIRECT_BINDING_NEE_CACHE)
        STRUCTURED_BUFFER(INTEGRATE_DIRECT_BINDING_NEE_CACHE_SAMPLE)
        RW_STRUCTURED_BUFFER(INTEGRATE_DIRECT_BINDING_NEE_CACHE_TASK)
        RW_TEXTURE2D(INTEGRATE_DIRECT_BINDING_NEE_CACHE_THREAD_TASK)

        RW_TEXTURE2D(INTEGRATE_DIRECT_BINDING_INDIRECT_RAY_ORIGIN_DIRECTION_OUTPUT)
        RW_TEXTURE2D(INTEGRATE_DIRECT_BINDING_INDIRECT_THROUGHPUT_CONE_RADIUS_OUTPUT)
        RW_TEXTURE2D(INTEGRATE_DIRECT_BINDING_INDIRECT_FIRST_HIT_PERCEPTUAL_ROUGHNESS_OUTPUT)
        RW_TEXTURE2D(INTEGRATE_DIRECT_BINDING_INDIRECT_FIRST_SAMPLED_LOBE_DATA_OUTPUT)
      END_PARAMETER()
    };
  }

  DxvkPathtracerIntegrateDirect::DxvkPathtracerIntegrateDirect(DxvkDevice* device) : CommonDeviceObject(device) {
  }

  void DxvkPathtracerIntegrateDirect::prewarmShaders(DxvkPipelineManager& pipelineManager) const {
    ScopedCpuProfileZoneN("Direct Integrate Shader Prewarming");

    const bool isOpacityMicromapSupported = OpacityMicromapManager::checkIsOpacityMicromapSupported(*m_device);

    if (RtxOptions::Shader::prewarmAllVariants()) {
      for (int32_t ommEnabled = isOpacityMicromapSupported; ommEnabled > 0; ommEnabled--) {
        pipelineManager.registerRaytracingShaders(getPipelineShaders(true, ommEnabled));
      }

      getComputeShader();
    } else {
      // Note: The getter for OMM enabled also checks if OMMs are supported, so we do not need to check for that manually.
      const bool ommEnabled = RtxOptions::getEnableOpacityMicromap();

      DxvkComputePipelineShaders shaders;
      switch (RtxOptions::renderPassIntegrateDirectRaytraceMode()) {
      case RaytraceMode::RayQuery:
        getComputeShader();
        break;
      case RaytraceMode::RayQueryRayGen:
        pipelineManager.registerRaytracingShaders(getPipelineShaders(true, ommEnabled));
        break;
      default:
        assert(false && "Invalid renderPassIntegrateDirectRaytraceMode in DxvkPathtracerIntegrateDirect::prewarmShaders");
        break;
      }
    }
  }

  void DxvkPathtracerIntegrateDirect::dispatch(
    RtxContext* ctx, 
    const Resources::RaytracingOutput& rtOutput) {
    ScopedGpuProfileZone(ctx, "Integrate Direct Raytracing");
    ctx->setFramePassStage(RtxFramePassStage::DirectIntegration);

    // Bind resources

    ctx->bindCommonRayTracingResources(rtOutput);

    Rc<DxvkSampler> linearSampler = ctx->getResourceManager().getSampler(VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);

    ctx->bindResourceView(INTEGRATE_DIRECT_BINDING_SKYPROBE, ctx->getResourceManager().getSkyProbe(ctx).view, nullptr);
    ctx->bindResourceSampler(INTEGRATE_DIRECT_BINDING_SKYPROBE, linearSampler);

    // Inputs 

    ctx->bindResourceView(INTEGRATE_DIRECT_BINDING_SHARED_INTEGRATION_SURFACE_PDF_INPUT, rtOutput.m_sharedIntegrationSurfacePdf.view(Resources::AccessType::Read), nullptr);
    ctx->bindResourceView(INTEGRATE_DIRECT_BINDING_SHARED_MATERIAL_DATA0_INPUT, rtOutput.m_sharedMaterialData0.view, nullptr);
    ctx->bindResourceView(INTEGRATE_DIRECT_BINDING_SHARED_MATERIAL_DATA1_INPUT, rtOutput.m_sharedMaterialData1.view, nullptr);
    ctx->bindResourceView(INTEGRATE_DIRECT_BINDING_SHARED_TEXTURE_COORD_INPUT, rtOutput.m_sharedTextureCoord.view, nullptr);
    ctx->bindResourceView(INTEGRATE_DIRECT_BINDING_SHARED_SURFACE_INDEX_INPUT, rtOutput.m_sharedSurfaceIndex.view(Resources::AccessType::Read), nullptr);
    ctx->bindResourceView(INTEGRATE_DIRECT_BINDING_SHARED_SUBSURFACE_DATA_INPUT, rtOutput.m_sharedSubsurfaceData.view, nullptr);
    ctx->bindResourceView(INTEGRATE_DIRECT_BINDING_SHARED_SUBSURFACE_DIFFUSION_PROFILE_DATA_INPUT, rtOutput.m_sharedSubsurfaceDiffusionProfileData.view, nullptr);

    ctx->bindResourceView(INTEGRATE_DIRECT_BINDING_PRIMARY_WORLD_SHADING_NORMAL_INPUT, rtOutput.m_primaryWorldShadingNormal.view, nullptr);
    ctx->bindResourceView(INTEGRATE_DIRECT_BINDING_PRIMARY_PERCEPTUAL_ROUGHNESS_INPUT, rtOutput.m_primaryPerceptualRoughness.view, nullptr);
    ctx->bindResourceView(INTEGRATE_DIRECT_BINDING_PRIMARY_ALBEDO_INPUT, rtOutput.m_primaryAlbedo.view, nullptr);
    ctx->bindResourceView(INTEGRATE_DIRECT_BINDING_PRIMARY_VIEW_DIRECTION_INPUT, rtOutput.m_primaryViewDirection.view, nullptr);
    ctx->bindResourceView(INTEGRATE_DIRECT_BINDING_PRIMARY_CONE_RADIUS_INPUT, rtOutput.m_primaryConeRadius.view, nullptr);
    ctx->bindResourceView(INTEGRATE_DIRECT_BINDING_PRIMARY_WORLD_POSITION_WORLD_TRIANGLE_NORMAL_INPUT, rtOutput.getCurrentPrimaryWorldPositionWorldTriangleNormal().view(Resources::AccessType::Read), nullptr);
    ctx->bindResourceView(INTEGRATE_DIRECT_BINDING_PRIMARY_POSITION_ERROR_INPUT, rtOutput.m_primaryPositionError.view, nullptr);
    ctx->bindResourceBuffer(INTEGRATE_DIRECT_BINDING_PRIMARY_RTXDI_RESERVOIR, DxvkBufferSlice(rtOutput.m_rtxdiReservoirBuffer, 0, rtOutput.m_rtxdiReservoirBuffer->info().size));

    ctx->bindResourceView(INTEGRATE_DIRECT_BINDING_SECONDARY_WORLD_SHADING_NORMAL_INPUT, rtOutput.m_secondaryWorldShadingNormal.view, nullptr);
    ctx->bindResourceView(INTEGRATE_DIRECT_BINDING_SECONDARY_PERCEPTUAL_ROUGHNESS_INPUT, rtOutput.m_secondaryPerceptualRoughness.view(Resources::AccessType::Read), nullptr);
    ctx->bindResourceView(INTEGRATE_DIRECT_BINDING_SECONDARY_ALBEDO_INPUT, rtOutput.m_secondaryAlbedo.view, nullptr);
    ctx->bindResourceView(INTEGRATE_DIRECT_BINDING_SECONDARY_VIEW_DIRECTION_INPUT, rtOutput.m_secondaryViewDirection.view(Resources::AccessType::Read), nullptr);
    ctx->bindResourceView(INTEGRATE_DIRECT_BINDING_SECONDARY_CONE_RADIUS_INPUT, rtOutput.m_secondaryConeRadius.view(Resources::AccessType::Read), nullptr);
    ctx->bindResourceView(INTEGRATE_DIRECT_BINDING_SECONDARY_WORLD_POSITION_WORLD_TRIANGLE_NORMAL_INPUT, rtOutput.m_secondaryWorldPositionWorldTriangleNormal.view(Resources::AccessType::Read), nullptr);

    // Inputs / Outputs

    ctx->bindResourceView(INTEGRATE_DIRECT_BINDING_SHARED_FLAGS_INPUT_OUTPUT, rtOutput.m_sharedFlags.view, nullptr);
    ctx->bindResourceView(INTEGRATE_DIRECT_BINDING_SHARED_MEDIUM_MATERIAL_INDEX_INPUT_OUTPUT, rtOutput.m_sharedMediumMaterialIndex.view, nullptr);
    ctx->bindResourceView(INTEGRATE_DIRECT_BINDING_PRIMARY_BASE_REFLECTIVITY_INPUT_OUTPUT, rtOutput.m_primaryBaseReflectivity.view(Resources::AccessType::ReadWrite), nullptr);
    ctx->bindResourceView(INTEGRATE_DIRECT_BINDING_SECONDARY_BASE_REFLECTIVITY_INPUT_OUTPUT, rtOutput.m_secondaryBaseReflectivity.view(Resources::AccessType::ReadWrite), nullptr);

    // Outputs

    ctx->bindResourceView(INTEGRATE_DIRECT_BINDING_PRIMARY_DIRECT_DIFFUSE_LOBE_RADIANCE_OUTPUT, rtOutput.m_primaryDirectDiffuseRadiance.view(Resources::AccessType::Write), nullptr);
    ctx->bindResourceView(INTEGRATE_DIRECT_BINDING_PRIMARY_DIRECT_SPECULAR_LOBE_RADIANCE_OUTPUT, rtOutput.m_primaryDirectSpecularRadiance.view(Resources::AccessType::Write), nullptr);
    ctx->bindResourceView(INTEGRATE_DIRECT_BINDING_SECONDARY_COMBINED_DIFFUSE_LOBE_RADIANCE_OUTPUT, rtOutput.m_secondaryCombinedDiffuseRadiance.view(Resources::AccessType::Write), nullptr);
    ctx->bindResourceView(INTEGRATE_DIRECT_BINDING_SECONDARY_COMBINED_SPECULAR_LOBE_RADIANCE_OUTPUT, rtOutput.m_secondaryCombinedSpecularRadiance.view(Resources::AccessType::Write), nullptr);
    ctx->bindResourceView(INTEGRATE_DIRECT_BINDING_PRIMARY_RTXDI_ILLUMINANCE_OUTPUT, rtOutput.getCurrentRtxdiIlluminance().view(Resources::AccessType::Write), nullptr);
    ctx->bindResourceBuffer(INTEGRATE_DIRECT_BINDING_NEE_CACHE, DxvkBufferSlice(rtOutput.m_neeCache, 0, rtOutput.m_neeCache->info().size));
    ctx->bindResourceBuffer(INTEGRATE_DIRECT_BINDING_NEE_CACHE_SAMPLE, DxvkBufferSlice(rtOutput.m_neeCacheSample, 0, rtOutput.m_neeCacheSample->info().size));
    ctx->bindResourceBuffer(INTEGRATE_DIRECT_BINDING_NEE_CACHE_TASK, DxvkBufferSlice(rtOutput.m_neeCacheTask, 0, rtOutput.m_neeCacheTask->info().size));
    ctx->bindResourceView(INTEGRATE_DIRECT_BINDING_NEE_CACHE_THREAD_TASK, rtOutput.m_neeCacheThreadTask.view, nullptr);

    ctx->bindResourceView(INTEGRATE_DIRECT_BINDING_INDIRECT_RAY_ORIGIN_DIRECTION_OUTPUT, rtOutput.m_indirectRayOriginDirection.view(Resources::AccessType::Write), nullptr);
    ctx->bindResourceView(INTEGRATE_DIRECT_BINDING_INDIRECT_THROUGHPUT_CONE_RADIUS_OUTPUT, rtOutput.m_indirectThroughputConeRadius.view(Resources::AccessType::Write), nullptr);
    ctx->bindResourceView(INTEGRATE_DIRECT_BINDING_INDIRECT_FIRST_HIT_PERCEPTUAL_ROUGHNESS_OUTPUT, rtOutput.m_indirectFirstHitPerceptualRoughness.view(Resources::AccessType::Write), nullptr);

    // Aliased resources
    ctx->bindResourceView(INTEGRATE_DIRECT_BINDING_SECONDARY_POSITION_ERROR_INPUT, rtOutput.m_secondaryPositionError.view(Resources::AccessType::Read), nullptr);
    ctx->bindResourceView(INTEGRATE_DIRECT_BINDING_INDIRECT_FIRST_SAMPLED_LOBE_DATA_OUTPUT, rtOutput.m_indirectFirstSampledLobeData.view(Resources::AccessType::Write), nullptr);

    const VkExtent3D& rayDims = rtOutput.m_compositeOutputExtent;

    const bool ommEnabled = RtxOptions::getEnableOpacityMicromap();

    switch (RtxOptions::renderPassIntegrateDirectRaytraceMode()) {
    case RaytraceMode::RayQuery:
      VkExtent3D workgroups = util::computeBlockCount(rayDims, VkExtent3D { 16, 8, 1 });
      ctx->bindShader(VK_SHADER_STAGE_COMPUTE_BIT, getComputeShader());
      ctx->dispatch(workgroups.width, workgroups.height, workgroups.depth);
      break;
    case RaytraceMode::RayQueryRayGen:
      ctx->bindRaytracingPipelineShaders(getPipelineShaders(true, ommEnabled));
      ctx->traceRays(rayDims.width, rayDims.height, rayDims.depth);
      break;
    default:
      assert(!"Unsupported RaytraceMode");
      break;
    }
  }

  DxvkRaytracingPipelineShaders DxvkPathtracerIntegrateDirect::getPipelineShaders(
    const bool useRayQuery,
    const bool ommEnabled) {

    DxvkRaytracingPipelineShaders shaders;
    if (useRayQuery) {
      shaders.debugName = "Integrate Direct RayQuery (RGS)";
      shaders.addGeneralShader(GET_SHADER_VARIANT(VK_SHADER_STAGE_RAYGEN_BIT_KHR, IntegrateDirectRayGenShader, integrate_direct_rayquery_raygen));
    } else {
      assert(!"TraceRay versions of the Integrate Direct pass are not implemented.");
    }

    if (ommEnabled) {
      shaders.pipelineFlags |= VK_PIPELINE_CREATE_RAY_TRACING_OPACITY_MICROMAP_BIT_EXT;
    }

    return shaders;
  }

  Rc<DxvkShader> DxvkPathtracerIntegrateDirect::getComputeShader() const {
    return GET_SHADER_VARIANT(VK_SHADER_STAGE_COMPUTE_BIT, IntegrateDirectRayGenShader, integrate_direct_rayquery);
  }

  const char* DxvkPathtracerIntegrateDirect::raytraceModeToString(RaytraceMode raytraceMode) {
    switch (raytraceMode) {
    case RaytraceMode::RayQuery:
      return "Ray Query [CS]";
    case RaytraceMode::RayQueryRayGen:
      return "Ray Query [RGS]";
    default:
      return "Unknown";
    }
  }
}
