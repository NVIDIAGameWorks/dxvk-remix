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
#include "rtx_pathtracer_gbuffer.h"
#include "dxvk_device.h"
#include "rtx_shader_manager.h"
#include "rtx_options.h"
#include "rtx_neural_radiance_cache.h"

#include "rtx/pass/common_binding_indices.h"
#include "rtx/pass/gbuffer/gbuffer_binding_indices.h"
#include "rtx/concept/surface_material/surface_material_hitgroup.h"

#include <rtx_shaders/gbuffer_raygen.h>
#include <rtx_shaders/gbuffer_raygen_nrc.h>
#include <rtx_shaders/gbuffer_raygen_ser.h>
#include <rtx_shaders/gbuffer_raygen_ser_nrc.h>
#include <rtx_shaders/gbuffer_rayquery.h>
#include <rtx_shaders/gbuffer_rayquery_nrc.h>
#include <rtx_shaders/gbuffer_rayquery_raygen.h>
#include <rtx_shaders/gbuffer_rayquery_raygen_nrc.h>
#include <rtx_shaders/gbuffer_psr_raygen.h>
#include <rtx_shaders/gbuffer_psr_raygen_ser.h>
#include <rtx_shaders/gbuffer_psr_raygen_nrc.h>
#include <rtx_shaders/gbuffer_psr_raygen_ser_nrc.h>
#include <rtx_shaders/gbuffer_psr_rayquery.h>
#include <rtx_shaders/gbuffer_psr_rayquery_raygen.h>
#include <rtx_shaders/gbuffer_psr_rayquery_nrc.h>
#include <rtx_shaders/gbuffer_psr_rayquery_raygen_nrc.h>

#include <rtx_shaders/gbuffer_miss.h>
#include <rtx_shaders/gbuffer_nrc_miss.h>
#include <rtx_shaders/gbuffer_psr_miss.h>
#include <rtx_shaders/gbuffer_psr_nrc_miss.h>

#include <rtx_shaders/gbuffer_material_opaque_translucent_closesthit.h>
#include <rtx_shaders/gbuffer_nrc_material_opaque_translucent_closesthit.h>
#include <rtx_shaders/gbuffer_material_rayPortal_closesthit.h>
#include <rtx_shaders/gbuffer_nrc_material_rayPortal_closesthit.h>
#include <rtx_shaders/gbuffer_psr_material_opaque_translucent_closesthit.h>
#include <rtx_shaders/gbuffer_psr_material_rayPortal_closesthit.h>
#include <rtx_shaders/gbuffer_psr_nrc_material_opaque_translucent_closesthit.h>
#include <rtx_shaders/gbuffer_psr_nrc_material_rayPortal_closesthit.h>


#include <rtx_shaders/gbuffer_raygen_wboit.h>
#include <rtx_shaders/gbuffer_raygen_nrc_wboit.h>
#include <rtx_shaders/gbuffer_raygen_ser_wboit.h>
#include <rtx_shaders/gbuffer_raygen_ser_nrc_wboit.h>
#include <rtx_shaders/gbuffer_rayquery_wboit.h>
#include <rtx_shaders/gbuffer_rayquery_nrc_wboit.h>
#include <rtx_shaders/gbuffer_rayquery_raygen_wboit.h>
#include <rtx_shaders/gbuffer_rayquery_raygen_nrc_wboit.h>
#include <rtx_shaders/gbuffer_psr_raygen_wboit.h>
#include <rtx_shaders/gbuffer_psr_raygen_ser_wboit.h>
#include <rtx_shaders/gbuffer_psr_raygen_nrc_wboit.h>
#include <rtx_shaders/gbuffer_psr_raygen_ser_nrc_wboit.h>
#include <rtx_shaders/gbuffer_psr_rayquery_wboit.h>
#include <rtx_shaders/gbuffer_psr_rayquery_raygen_wboit.h>
#include <rtx_shaders/gbuffer_psr_rayquery_nrc_wboit.h>
#include <rtx_shaders/gbuffer_psr_rayquery_raygen_nrc_wboit.h>


#include <rtx_shaders/gbuffer_miss_wboit.h>
#include <rtx_shaders/gbuffer_nrc_miss_wboit.h>
#include <rtx_shaders/gbuffer_psr_miss_wboit.h>
#include <rtx_shaders/gbuffer_psr_nrc_miss_wboit.h>

#include <rtx_shaders/gbuffer_material_opaque_translucent_closesthit_wboit.h>
#include <rtx_shaders/gbuffer_nrc_material_opaque_translucent_closesthit_wboit.h>
#include <rtx_shaders/gbuffer_material_rayPortal_closesthit_wboit.h>
#include <rtx_shaders/gbuffer_nrc_material_rayPortal_closesthit_wboit.h>
#include <rtx_shaders/gbuffer_psr_material_opaque_translucent_closesthit_wboit.h>
#include <rtx_shaders/gbuffer_psr_material_rayPortal_closesthit_wboit.h>
#include <rtx_shaders/gbuffer_psr_nrc_material_opaque_translucent_closesthit_wboit.h>
#include <rtx_shaders/gbuffer_psr_nrc_material_rayPortal_closesthit_wboit.h>

#include "dxvk_scoped_annotation.h"
#include "rtx_context.h"
#include "rtx_opacity_micromap_manager.h"

namespace dxvk {

  // Defined within an unnamed namespace to ensure unique definition across binary
  namespace {
    class GbufferRayGenShader : public ManagedShader {
    public:
      BINDLESS_ENABLED()

      PUSH_CONSTANTS(GbufferPushConstants)

      BEGIN_PARAMETER()
        COMMON_RAYTRACING_BINDINGS

        SAMPLER(GBUFFER_BINDING_LINEAR_WRAP_SAMPLER)

        SAMPLER3D(GBUFFER_BINDING_VOLUME_FILTERED_RADIANCE_Y_INPUT)
        SAMPLER3D(GBUFFER_BINDING_VOLUME_FILTERED_RADIANCE_CO_CG_INPUT)

        RW_TEXTURE2D(GBUFFER_BINDING_SHARED_FLAGS_OUTPUT)
        RW_TEXTURE2D(GBUFFER_BINDING_SHARED_RADIANCE_RG_OUTPUT)
        RW_TEXTURE2D(GBUFFER_BINDING_SHARED_RADIANCE_B_OUTPUT)
        RW_TEXTURE2D(GBUFFER_BINDING_SHARED_INTEGRATION_SURFACE_PDF_OUTPUT)
        RW_TEXTURE2D(GBUFFER_BINDING_SHARED_MATERIAL_DATA0_OUTPUT)
        RW_TEXTURE2D(GBUFFER_BINDING_SHARED_MATERIAL_DATA1_OUTPUT)
        RW_TEXTURE2D(GBUFFER_BINDING_SHARED_MEDIUM_MATERIAL_INDEX_OUTPUT)
        RW_TEXTURE2D(GBUFFER_BINDING_SHARED_TEXTURE_COORD_OUTPUT)
        RW_TEXTURE2D(GBUFFER_BINDING_SHARED_SURFACE_INDEX_OUTPUT)
        RW_TEXTURE2D(GBUFFER_BINDING_SHARED_SUBSURFACE_DATA_OUTPUT)
        RW_TEXTURE2D(GBUFFER_BINDING_SHARED_SUBSURFACE_DIFFUSION_PROFILE_DATA_OUTPUT)

        RW_TEXTURE2D(GBUFFER_BINDING_PRIMARY_ATTENUATION_OUTPUT)
        RW_TEXTURE2D(GBUFFER_BINDING_PRIMARY_WORLD_SHADING_NORMAL_OUTPUT)
        RW_TEXTURE2D(GBUFFER_BINDING_PRIMARY_WORLD_INTERPOLATED_NORMAL_OUTPUT)
        RW_TEXTURE2D(GBUFFER_BINDING_PRIMARY_PERCEPTUAL_ROUGHNESS_OUTPUT)
        RW_TEXTURE2D(GBUFFER_BINDING_PRIMARY_LINEAR_VIEW_Z_OUTPUT)
        RW_TEXTURE2D(GBUFFER_BINDING_PRIMARY_ALBEDO_OUTPUT)
        RW_TEXTURE2D(GBUFFER_BINDING_PRIMARY_BASE_REFLECTIVITY_OUTPUT)
        RW_TEXTURE2D(GBUFFER_BINDING_PRIMARY_VIRTUAL_MVEC_OUTPUT)
        RW_TEXTURE2D(GBUFFER_BINDING_PRIMARY_SCREEN_SPACE_MOTION_OUTPUT)
        RW_TEXTURE2D(GBUFFER_BINDING_PRIMARY_VIRTUAL_WORLD_SHADING_NORMAL_OUTPUT)
        RW_TEXTURE2D(GBUFFER_BINDING_PRIMARY_VIRTUAL_WORLD_SHADING_NORMAL_DENOISING_OUTPUT)
        RW_TEXTURE2D(GBUFFER_BINDING_PRIMARY_HIT_DISTANCE_OUTPUT)
        RW_TEXTURE2D(GBUFFER_BINDING_PRIMARY_VIEW_DIRECTION_OUTPUT)
        RW_TEXTURE2D(GBUFFER_BINDING_PRIMARY_CONE_RADIUS_OUTPUT)
        RW_TEXTURE2D(GBUFFER_BINDING_PRIMARY_POSITION_ERROR_OUTPUT)
        RW_TEXTURE2D(GBUFFER_BINDING_PRIMARY_OBJECT_PICKING_OUTPUT)

        RW_TEXTURE2D(GBUFFER_BINDING_SECONDARY_ATTENUATION_OUTPUT)
        RW_TEXTURE2D(GBUFFER_BINDING_SECONDARY_WORLD_SHADING_NORMAL_OUTPUT)
        RW_TEXTURE2D(GBUFFER_BINDING_SECONDARY_PERCEPTUAL_ROUGHNESS_OUTPUT)
        RW_TEXTURE2D(GBUFFER_BINDING_SECONDARY_LINEAR_VIEW_Z_OUTPUT)
        RW_TEXTURE2D(GBUFFER_BINDING_SECONDARY_ALBEDO_OUTPUT)
        RW_TEXTURE2D(GBUFFER_BINDING_SECONDARY_BASE_REFLECTIVITY_OUTPUT)
        RW_TEXTURE2D(GBUFFER_BINDING_SECONDARY_VIRTUAL_MVEC_OUTPUT)
        RW_TEXTURE2D(GBUFFER_BINDING_SECONDARY_VIRTUAL_WORLD_SHADING_NORMAL_OUTPUT)
        RW_TEXTURE2D(GBUFFER_BINDING_SECONDARY_VIRTUAL_WORLD_SHADING_NORMAL_DENOISING_OUTPUT)
        RW_TEXTURE2D(GBUFFER_BINDING_SECONDARY_HIT_DISTANCE_OUTPUT)
        RW_TEXTURE2D(GBUFFER_BINDING_SECONDARY_VIEW_DIRECTION_OUTPUT)
        RW_TEXTURE2D(GBUFFER_BINDING_SECONDARY_CONE_RADIUS_OUTPUT)
        RW_TEXTURE2D(GBUFFER_BINDING_SECONDARY_WORLD_POSITION_OUTPUT)
        RW_TEXTURE2D(GBUFFER_BINDING_SECONDARY_POSITION_ERROR_OUTPUT)
        RW_TEXTURE2D(GBUFFER_BINDING_PRIMARY_SURFACE_FLAGS_OUTPUT)
        RW_TEXTURE2D(GBUFFER_BINDING_PRIMARY_DISOCCLUSION_THRESHOLD_MIX_OUTPUT)
        RW_TEXTURE2D(GBUFFER_BINDING_PRIMARY_DEPTH_OUTPUT)
        RW_TEXTURE2D(GBUFFER_BINDING_SHARED_BIAS_CURRENT_COLOR_MASK_OUTPUT)
        RW_TEXTURE2D(GBUFFER_BINDING_ALPHA_BLEND_GBUFFER_OUTPUT)
        RW_TEXTURE2D(GBUFFER_BINDING_PARTICLE_BUFFER_OUTPUT)
        SAMPLER2D(GBUFFER_BINDING_SKYMATTE)
        SAMPLERCUBE(GBUFFER_BINDING_SKYPROBE)

        RW_TEXTURE2D(GBUFFER_BINDING_REFLECTION_PSR_DATA_STORAGE_0)
        RW_TEXTURE2D(GBUFFER_BINDING_PRIMARY_WORLD_POSITION_OUTPUT)

        RW_TEXTURE2D(GBUFFER_BINDING_REFLECTION_PSR_DATA_STORAGE_1)

        RW_TEXTURE2D(GBUFFER_BINDING_REFLECTION_PSR_DATA_STORAGE_2)
        RW_TEXTURE2D(GBUFFER_BINDING_TRANSMISSION_PSR_DATA_STORAGE_0)
        RW_TEXTURE2D(GBUFFER_BINDING_TRANSMISSION_PSR_DATA_STORAGE_1)
        RW_TEXTURE2D(GBUFFER_BINDING_TRANSMISSION_PSR_DATA_STORAGE_2)
        RW_TEXTURE2D(GBUFFER_BINDING_TRANSMISSION_PSR_DATA_STORAGE_3)

        RW_TEXTURE2D(GBUFFER_BINDING_PRIMARY_DEPTH_DLSSRR_OUTPUT)
        RW_TEXTURE2D(GBUFFER_BINDING_PRIMARY_NORMAL_DLSSRR_OUTPUT)
        RW_TEXTURE2D(GBUFFER_BINDING_PRIMARY_SCREEN_SPACE_MOTION_DLSSRR_OUTPUT)

        RW_STRUCTURED_BUFFER(GBUFFER_BINDING_NRC_QUERY_PATH_INFO_OUTPUT)
        RW_STRUCTURED_BUFFER(GBUFFER_BINDING_NRC_TRAINING_PATH_INFO_OUTPUT)
        RW_STRUCTURED_BUFFER(GBUFFER_BINDING_NRC_TRAINING_PATH_VERTICES_OUTPUT)
        RW_STRUCTURED_BUFFER(GBUFFER_BINDING_NRC_QUERY_RADIANCE_PARAMS_OUTPUT)
        RW_STRUCTURED_BUFFER(GBUFFER_BINDING_NRC_COUNTERS_OUTPUT)

        RW_TEXTURE2D(GBUFFER_BINDING_NRC_QUERY_PATH_DATA0_OUTPUT)
        RW_TEXTURE2D(GBUFFER_BINDING_NRC_QUERY_PATH_DATA1_OUTPUT)
        RW_TEXTURE2D(GBUFFER_BINDING_NRC_TRAINING_PATH_DATA1_OUTPUT)

        RW_TEXTURE2D(GBUFFER_BINDING_NRC_TRAINING_GBUFFER_SURFACE_RADIANCE_RG_OUTPUT)
        RW_TEXTURE2D(GBUFFER_BINDING_NRC_TRAINING_GBUFFER_SURFACE_RADIANCE_B_OUTPUT)

      END_PARAMETER()
    };

    class GbufferClosestHitShader : public ManagedShader {

      BEGIN_PARAMETER()
      END_PARAMETER()
    };

    class GbufferMissShader : public ManagedShader {
      
      BEGIN_PARAMETER()
      END_PARAMETER()
    };
  }

  DxvkPathtracerGbuffer::DxvkPathtracerGbuffer(DxvkDevice* device) : CommonDeviceObject(device) {
  }

  void DxvkPathtracerGbuffer::prewarmShaders(DxvkPipelineManager& pipelineManager) const {
    ScopedCpuProfileZoneN("Gbuffer Shader Prewarming");

    const bool isNrcSupported = NeuralRadianceCache::checkIsSupported(device());
    const bool isOpacityMicromapSupported = OpacityMicromapManager::checkIsOpacityMicromapSupported(*m_device);
    const bool isShaderExecutionReorderingSupported = 
      RtxContext::checkIsShaderExecutionReorderingSupported(*m_device) && 
      RtxOptions::isShaderExecutionReorderingInPathtracerGbufferEnabled();
    const bool portalsEnabled = RtxOptions::rayPortalModelTextureHashes().size() > 0;

    if (RtxOptions::Shader::prewarmAllVariants()) {
      for (int32_t nrcEnabled = isNrcSupported; nrcEnabled >= 0; nrcEnabled--) {
        for (int32_t isPSRPass = 1; isPSRPass >= 0; isPSRPass--) {
          for (int32_t wboitEnabled = 1; wboitEnabled >= 0; wboitEnabled--) {
            for (int32_t includePortals = portalsEnabled; includePortals >= 0; includePortals--) {
              for (int32_t useRayQuery = 1; useRayQuery >= 0; useRayQuery--) {
                for (int32_t serEnabled = isShaderExecutionReorderingSupported; serEnabled >= 0; serEnabled--) {
                  for (int32_t ommEnabled = isOpacityMicromapSupported; ommEnabled >= 0; ommEnabled--) {
                    pipelineManager.registerRaytracingShaders(getPipelineShaders(isPSRPass, useRayQuery, serEnabled, ommEnabled, includePortals, nrcEnabled, wboitEnabled));
                  }
                }
              }
            }

            getComputeShader(isPSRPass, nrcEnabled, wboitEnabled);
          }
        }
      }
    } else {
      // Note: The getters for these SER/OMM enabled flags also check if SER/OMMs are supported, so we do not need to check for that manually.
      const bool serEnabled = RtxOptions::isShaderExecutionReorderingInPathtracerGbufferEnabled();
      const bool ommEnabled = RtxOptions::getEnableOpacityMicromap();
      const bool nrcEnabled = RtxOptions::integrateIndirectMode() == IntegrateIndirectMode::NeuralRadianceCache;
      const bool wboitEnabled = RtxOptions::wboitEnabled();

      // Need both PSR and non-PSR passes.
      for (int32_t isPSRPass = 1; isPSRPass >= 0; isPSRPass--) {
        for (int32_t includePortals = portalsEnabled; includePortals >= 0; includePortals--) {
          DxvkComputePipelineShaders shaders;
          switch (RtxOptions::renderPassGBufferRaytraceMode()) {
          case RaytraceMode::RayQuery:
            getComputeShader(isPSRPass, nrcEnabled, wboitEnabled);
            break;
          case RaytraceMode::RayQueryRayGen:
            pipelineManager.registerRaytracingShaders(getPipelineShaders(isPSRPass, true, serEnabled, ommEnabled, includePortals, nrcEnabled, wboitEnabled));
            break;
          case RaytraceMode::TraceRay:
            pipelineManager.registerRaytracingShaders(getPipelineShaders(isPSRPass, false, serEnabled, ommEnabled, includePortals, nrcEnabled, wboitEnabled));
            break;
          case RaytraceMode::Count:
            assert(false && "Invalid RaytraceMode in DxvkPathtracerGbuffer::prewarmShaders");
            break;
          }
        }
      }
    }
  }

  void DxvkPathtracerGbuffer::dispatch(
    RtxContext* ctx, 
    const Resources::RaytracingOutput& rtOutput) {
    ScopedGpuProfileZone(ctx, "Gbuffer Raytracing");
    ctx->setFramePassStage(RtxFramePassStage::GBufferPrimaryRays);

    // Bind resources

    ctx->bindCommonRayTracingResources(rtOutput);

    // Note: Clamp to edge used to avoid interpolation to black on the edges of the view.
    Rc<DxvkSampler> linearClampSampler = ctx->getResourceManager().getSampler(VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
    Rc<DxvkSampler> linearWrapSampler = ctx->getResourceManager().getSampler(VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_NEAREST, VK_SAMPLER_ADDRESS_MODE_REPEAT);

    ctx->bindResourceSampler(GBUFFER_BINDING_LINEAR_WRAP_SAMPLER, linearWrapSampler);

    const RtxGlobalVolumetrics& globalVolumetrics = ctx->getCommonObjects()->metaGlobalVolumetrics();
    ctx->bindResourceView(GBUFFER_BINDING_VOLUME_FILTERED_RADIANCE_Y_INPUT, globalVolumetrics.getCurrentVolumeAccumulatedRadianceY().view, nullptr);
    ctx->bindResourceSampler(GBUFFER_BINDING_VOLUME_FILTERED_RADIANCE_Y_INPUT, linearClampSampler);
    ctx->bindResourceView(GBUFFER_BINDING_VOLUME_FILTERED_RADIANCE_CO_CG_INPUT, globalVolumetrics.getCurrentVolumeAccumulatedRadianceCoCg().view, nullptr);
    ctx->bindResourceSampler(GBUFFER_BINDING_VOLUME_FILTERED_RADIANCE_CO_CG_INPUT, linearClampSampler);

    ctx->bindResourceView(GBUFFER_BINDING_SKYMATTE, ctx->getResourceManager().getSkyMatte(ctx).view, nullptr);
    ctx->bindResourceSampler(GBUFFER_BINDING_SKYMATTE, linearClampSampler);

    // Requires the probe too for PSRR/T miss
    ctx->bindResourceView(GBUFFER_BINDING_SKYPROBE, ctx->getResourceManager().getSkyProbe(ctx).view, nullptr);
    ctx->bindResourceSampler(GBUFFER_BINDING_SKYPROBE, linearClampSampler);

    // Output resources

    ctx->bindResourceView(GBUFFER_BINDING_SHARED_FLAGS_OUTPUT, rtOutput.m_sharedFlags.view, nullptr);
    ctx->bindResourceView(GBUFFER_BINDING_SHARED_RADIANCE_RG_OUTPUT, rtOutput.m_sharedRadianceRG.view, nullptr);
    ctx->bindResourceView(GBUFFER_BINDING_SHARED_RADIANCE_B_OUTPUT, rtOutput.m_sharedRadianceB.view, nullptr);
    ctx->bindResourceView(GBUFFER_BINDING_SHARED_INTEGRATION_SURFACE_PDF_OUTPUT, rtOutput.m_sharedIntegrationSurfacePdf.view(Resources::AccessType::Write), nullptr);
    ctx->bindResourceView(GBUFFER_BINDING_SHARED_MATERIAL_DATA0_OUTPUT, rtOutput.m_sharedMaterialData0.view, nullptr);
    ctx->bindResourceView(GBUFFER_BINDING_SHARED_MATERIAL_DATA1_OUTPUT, rtOutput.m_sharedMaterialData1.view, nullptr);
    ctx->bindResourceView(GBUFFER_BINDING_SHARED_MEDIUM_MATERIAL_INDEX_OUTPUT, rtOutput.m_sharedMediumMaterialIndex.view, nullptr);
    ctx->bindResourceView(GBUFFER_BINDING_SHARED_BIAS_CURRENT_COLOR_MASK_OUTPUT, rtOutput.m_sharedBiasCurrentColorMask.view(Resources::AccessType::Write), nullptr);
    ctx->bindResourceView(GBUFFER_BINDING_SHARED_TEXTURE_COORD_OUTPUT, rtOutput.m_sharedTextureCoord.view, nullptr);
    ctx->bindResourceView(GBUFFER_BINDING_SHARED_SURFACE_INDEX_OUTPUT, rtOutput.m_sharedSurfaceIndex.view(Resources::AccessType::Write), nullptr);
    ctx->bindResourceView(GBUFFER_BINDING_SHARED_SUBSURFACE_DATA_OUTPUT, rtOutput.m_sharedSubsurfaceData.view, nullptr);
    ctx->bindResourceView(GBUFFER_BINDING_SHARED_SUBSURFACE_DIFFUSION_PROFILE_DATA_OUTPUT, rtOutput.m_sharedSubsurfaceDiffusionProfileData.view, nullptr);

    ctx->bindResourceView(GBUFFER_BINDING_PRIMARY_ATTENUATION_OUTPUT, rtOutput.m_primaryAttenuation.view, nullptr);
    ctx->bindResourceView(GBUFFER_BINDING_PRIMARY_WORLD_SHADING_NORMAL_OUTPUT, rtOutput.m_primaryWorldShadingNormal.view, nullptr);
    ctx->bindResourceView(GBUFFER_BINDING_PRIMARY_WORLD_INTERPOLATED_NORMAL_OUTPUT, rtOutput.m_primaryWorldInterpolatedNormal.view, nullptr);
    ctx->bindResourceView(GBUFFER_BINDING_PRIMARY_PERCEPTUAL_ROUGHNESS_OUTPUT, rtOutput.m_primaryPerceptualRoughness.view, nullptr);
    ctx->bindResourceView(GBUFFER_BINDING_PRIMARY_LINEAR_VIEW_Z_OUTPUT, rtOutput.m_primaryLinearViewZ.view, nullptr);
    ctx->bindResourceView(GBUFFER_BINDING_PRIMARY_ALBEDO_OUTPUT, rtOutput.m_primaryAlbedo.view, nullptr);
    ctx->bindResourceView(GBUFFER_BINDING_PRIMARY_BASE_REFLECTIVITY_OUTPUT, rtOutput.m_primaryBaseReflectivity.view(Resources::AccessType::Write), nullptr);
    ctx->bindResourceView(GBUFFER_BINDING_PRIMARY_VIRTUAL_MVEC_OUTPUT, rtOutput.m_primaryVirtualMotionVector.view(Resources::AccessType::Write), nullptr);
    ctx->bindResourceView(GBUFFER_BINDING_PRIMARY_SCREEN_SPACE_MOTION_OUTPUT, rtOutput.m_primaryScreenSpaceMotionVector.view, nullptr);
    ctx->bindResourceView(GBUFFER_BINDING_PRIMARY_VIRTUAL_WORLD_SHADING_NORMAL_OUTPUT, rtOutput.m_primaryVirtualWorldShadingNormalPerceptualRoughness.view, nullptr);
    ctx->bindResourceView(GBUFFER_BINDING_PRIMARY_VIRTUAL_WORLD_SHADING_NORMAL_DENOISING_OUTPUT, rtOutput.m_primaryVirtualWorldShadingNormalPerceptualRoughnessDenoising.view(Resources::AccessType::Write), nullptr);
    ctx->bindResourceView(GBUFFER_BINDING_PRIMARY_HIT_DISTANCE_OUTPUT, rtOutput.m_primaryHitDistance.view, nullptr);
    ctx->bindResourceView(GBUFFER_BINDING_PRIMARY_VIEW_DIRECTION_OUTPUT, rtOutput.m_primaryViewDirection.view, nullptr);
    ctx->bindResourceView(GBUFFER_BINDING_PRIMARY_CONE_RADIUS_OUTPUT, rtOutput.m_primaryConeRadius.view, nullptr);
    ctx->bindResourceView(GBUFFER_BINDING_PRIMARY_POSITION_ERROR_OUTPUT, rtOutput.m_primaryPositionError.view, nullptr);
    ctx->bindResourceView(GBUFFER_BINDING_PRIMARY_SURFACE_FLAGS_OUTPUT, rtOutput.m_primarySurfaceFlags.view, nullptr);
    ctx->bindResourceView(GBUFFER_BINDING_PRIMARY_DISOCCLUSION_THRESHOLD_MIX_OUTPUT, rtOutput.m_primaryDisocclusionThresholdMix.view, nullptr);
    ctx->bindResourceView(GBUFFER_BINDING_PRIMARY_DEPTH_OUTPUT, rtOutput.m_primaryDepth.view, nullptr);
    ctx->bindResourceView(GBUFFER_BINDING_PRIMARY_OBJECT_PICKING_OUTPUT, rtOutput.m_primaryObjectPicking.view, nullptr);

    ctx->bindResourceView(GBUFFER_BINDING_SECONDARY_ATTENUATION_OUTPUT, rtOutput.m_secondaryAttenuation.view, nullptr);
    ctx->bindResourceView(GBUFFER_BINDING_SECONDARY_WORLD_SHADING_NORMAL_OUTPUT, rtOutput.m_secondaryWorldShadingNormal.view, nullptr);
    ctx->bindResourceView(GBUFFER_BINDING_SECONDARY_PERCEPTUAL_ROUGHNESS_OUTPUT, rtOutput.m_secondaryPerceptualRoughness.view(Resources::AccessType::Write), nullptr);
    ctx->bindResourceView(GBUFFER_BINDING_SECONDARY_LINEAR_VIEW_Z_OUTPUT, rtOutput.m_secondaryLinearViewZ.view, nullptr);
    ctx->bindResourceView(GBUFFER_BINDING_SECONDARY_ALBEDO_OUTPUT, rtOutput.m_secondaryAlbedo.view, nullptr);
    ctx->bindResourceView(GBUFFER_BINDING_SECONDARY_BASE_REFLECTIVITY_OUTPUT, rtOutput.m_secondaryBaseReflectivity.view(Resources::AccessType::Write), nullptr);
    ctx->bindResourceView(GBUFFER_BINDING_SECONDARY_VIRTUAL_MVEC_OUTPUT, rtOutput.m_secondaryVirtualMotionVector.view(Resources::AccessType::Write), nullptr);
    ctx->bindResourceView(GBUFFER_BINDING_SECONDARY_VIRTUAL_WORLD_SHADING_NORMAL_OUTPUT, rtOutput.m_secondaryVirtualWorldShadingNormalPerceptualRoughness.view, nullptr);
    ctx->bindResourceView(GBUFFER_BINDING_SECONDARY_VIRTUAL_WORLD_SHADING_NORMAL_DENOISING_OUTPUT, rtOutput.m_secondaryVirtualWorldShadingNormalPerceptualRoughnessDenoising.view, nullptr);
    ctx->bindResourceView(GBUFFER_BINDING_SECONDARY_HIT_DISTANCE_OUTPUT, rtOutput.m_secondaryHitDistance.view, nullptr);
    ctx->bindResourceView(GBUFFER_BINDING_SECONDARY_VIEW_DIRECTION_OUTPUT, rtOutput.m_secondaryViewDirection.view(Resources::AccessType::Write), nullptr);
    ctx->bindResourceView(GBUFFER_BINDING_SECONDARY_CONE_RADIUS_OUTPUT, rtOutput.m_secondaryConeRadius.view(Resources::AccessType::Write), nullptr);
    ctx->bindResourceView(GBUFFER_BINDING_SECONDARY_POSITION_ERROR_OUTPUT, rtOutput.m_secondaryPositionError.view(Resources::AccessType::Write), nullptr);
    ctx->bindResourceView(GBUFFER_BINDING_SECONDARY_WORLD_POSITION_OUTPUT, rtOutput.m_secondaryWorldPositionWorldTriangleNormal.view(Resources::AccessType::Write), nullptr);
    ctx->bindResourceView(GBUFFER_BINDING_ALPHA_BLEND_GBUFFER_OUTPUT, rtOutput.m_alphaBlendGBuffer.view, nullptr);
    ctx->bindResourceView(GBUFFER_BINDING_PARTICLE_BUFFER_OUTPUT, rtOutput.m_rayReconstructionParticleBuffer.view, nullptr);

    ctx->bindResourceView(GBUFFER_BINDING_REFLECTION_PSR_DATA_STORAGE_0, rtOutput.m_gbufferPSRData[0].view(Resources::AccessType::Write), nullptr);
    ctx->bindResourceView(GBUFFER_BINDING_PRIMARY_WORLD_POSITION_OUTPUT, rtOutput.getCurrentPrimaryWorldPositionWorldTriangleNormal().view(Resources::AccessType::Write), nullptr);

    ctx->bindResourceView(GBUFFER_BINDING_REFLECTION_PSR_DATA_STORAGE_1, rtOutput.m_gbufferPSRData[1].view(Resources::AccessType::Write), nullptr);

    // Note: m_gbufferPSRData[2..6] are aliased with various radiance textures that are used later as integrator outputs.
    ctx->bindResourceView(GBUFFER_BINDING_REFLECTION_PSR_DATA_STORAGE_2, rtOutput.m_gbufferPSRData[2].view(Resources::AccessType::Write), nullptr);
    ctx->bindResourceView(GBUFFER_BINDING_TRANSMISSION_PSR_DATA_STORAGE_0, rtOutput.m_gbufferPSRData[3].view(Resources::AccessType::Write), nullptr);
    ctx->bindResourceView(GBUFFER_BINDING_TRANSMISSION_PSR_DATA_STORAGE_1, rtOutput.m_gbufferPSRData[4].view(Resources::AccessType::Write), nullptr);
    ctx->bindResourceView(GBUFFER_BINDING_TRANSMISSION_PSR_DATA_STORAGE_2, rtOutput.m_gbufferPSRData[5].view(Resources::AccessType::Write), nullptr);
    ctx->bindResourceView(GBUFFER_BINDING_TRANSMISSION_PSR_DATA_STORAGE_3, rtOutput.m_gbufferPSRData[6].view(Resources::AccessType::Write), nullptr);

    // Bind necessary buffers for DLSS-RR. 
    // Note: RR uses different PSR rules compared to other uses, and its resolves are resolved in an another shader.
    ctx->bindResourceView(GBUFFER_BINDING_PRIMARY_DEPTH_DLSSRR_OUTPUT, rtOutput.m_primaryDepthDLSSRR.view(Resources::AccessType::Write), nullptr);
    ctx->bindResourceView(GBUFFER_BINDING_PRIMARY_NORMAL_DLSSRR_OUTPUT, rtOutput.m_primaryWorldShadingNormalDLSSRR.view(Resources::AccessType::Write), nullptr);
    ctx->bindResourceView(GBUFFER_BINDING_PRIMARY_SCREEN_SPACE_MOTION_DLSSRR_OUTPUT, rtOutput.m_primaryScreenSpaceMotionVectorDLSSRR.view, nullptr);

    // Bind necessary resources for Neural Radiance Cache
    NeuralRadianceCache& nrc = ctx->getCommonObjects()->metaNeuralRadianceCache();
    nrc.bindGBufferPathTracingResources(*ctx);    
  
    const VkExtent3D& rayDims = rtOutput.m_compositeOutputExtent;

    const bool nrcEnabled = nrc.isActive();
    const bool serEnabled = RtxOptions::isShaderExecutionReorderingInPathtracerGbufferEnabled();
    const bool ommEnabled = RtxOptions::getEnableOpacityMicromap();
    const bool includePortals = RtxOptions::rayPortalModelTextureHashes().size() > 0 || rtOutput.m_raytraceArgs.numActiveRayPortals > 0;
    const bool wboitEnabled = RtxOptions::wboitEnabled();

    GbufferPushConstants pushArgs = {};
    pushArgs.isTransmissionPSR = 0;
    ctx->setPushConstantBank(DxvkPushConstantBank::RTX);
    ctx->pushConstants(0, sizeof(pushArgs), &pushArgs);


    switch (RtxOptions::renderPassGBufferRaytraceMode()) {
    case RaytraceMode::RayQuery:
      VkExtent3D workgroups = util::computeBlockCount(rayDims, VkExtent3D { 16, 8, 1 });
      {
        ScopedGpuProfileZone(ctx, "Primary Rays");
        ctx->bindShader(VK_SHADER_STAGE_COMPUTE_BIT, getComputeShader(false, nrcEnabled, wboitEnabled));
        ctx->dispatch(workgroups.width, workgroups.height, workgroups.depth);
      }

      {
        // Warning: do not change the order of Reflection and Transmission PSR, that will break
        // PSR data dependencies due to resource aliasing.
        ScopedGpuProfileZone(ctx, "Reflection PSR");
        ctx->setFramePassStage(RtxFramePassStage::ReflectionPSR);
        ctx->bindShader(VK_SHADER_STAGE_COMPUTE_BIT, getComputeShader(true, nrcEnabled, wboitEnabled));
        ctx->dispatch(workgroups.width, workgroups.height, workgroups.depth);
      }

      {
        ScopedGpuProfileZone(ctx, "Transmission PSR");
        ctx->setFramePassStage(RtxFramePassStage::TransmissionPSR);
        pushArgs.isTransmissionPSR = 1;
        ctx->pushConstants(0, sizeof(pushArgs), &pushArgs);
        ctx->dispatch(workgroups.width, workgroups.height, workgroups.depth);
      }
      break;

      case RaytraceMode::RayQueryRayGen:
      {
        ScopedGpuProfileZone(ctx, "Primary Rays");
        ctx->bindRaytracingPipelineShaders(getPipelineShaders(false, true, serEnabled, ommEnabled, includePortals, nrcEnabled, wboitEnabled));
        ctx->traceRays(rayDims.width, rayDims.height, rayDims.depth);
      }

      {
        // Warning: do not change the order of Reflection and Transmission PSR, that will break
        // PSR data dependencies due to resource aliasing.
        ScopedGpuProfileZone(ctx, "Reflection PSR");
        ctx->setFramePassStage(RtxFramePassStage::ReflectionPSR);
        ctx->bindRaytracingPipelineShaders(getPipelineShaders(true, true, serEnabled, ommEnabled, includePortals, nrcEnabled, wboitEnabled));
        ctx->traceRays(rayDims.width, rayDims.height, rayDims.depth);
      }

      {
        ScopedGpuProfileZone(ctx, "Transmission PSR");
        ctx->setFramePassStage(RtxFramePassStage::TransmissionPSR);
        pushArgs.isTransmissionPSR = 1;
        ctx->pushConstants(0, sizeof(pushArgs), &pushArgs);
        ctx->traceRays(rayDims.width, rayDims.height, rayDims.depth);
      }
      break;

      case RaytraceMode::TraceRay:
      {
        ScopedGpuProfileZone(ctx, "Primary Rays");
        ctx->bindRaytracingPipelineShaders(getPipelineShaders(false, false, serEnabled, ommEnabled, includePortals, nrcEnabled, wboitEnabled));
        ctx->traceRays(rayDims.width, rayDims.height, rayDims.depth);
      }

      {
        // Warning: do not change the order of Reflection and Transmission PSR, that will break
        // PSR data dependencies due to resource aliasing.
        ScopedGpuProfileZone(ctx, "Reflection PSR");
        ctx->setFramePassStage(RtxFramePassStage::ReflectionPSR);
        ctx->bindRaytracingPipelineShaders(getPipelineShaders(true, false, serEnabled, ommEnabled, includePortals, nrcEnabled, wboitEnabled));
        ctx->traceRays(rayDims.width, rayDims.height, rayDims.depth);
      }

      {
        ScopedGpuProfileZone(ctx, "Transmission PSR");
        ctx->setFramePassStage(RtxFramePassStage::TransmissionPSR);
        pushArgs.isTransmissionPSR = 1;
        ctx->pushConstants(0, sizeof(pushArgs), &pushArgs);
        ctx->traceRays(rayDims.width, rayDims.height, rayDims.depth);
      }
      break;
      case RaytraceMode::Count:
        assert(false && "Invalid RaytraceMode in DxvkPathtracerGbuffer::dispatch");
      break;
    }
  }

  DxvkRaytracingPipelineShaders DxvkPathtracerGbuffer::getPipelineShaders(
    const bool isPSRPass,
    const bool useRayQuery,
    const bool serEnabled,
    const bool ommEnabled,
    const bool includePortals,
    const bool nrcEnabled,
    const bool wboitEnabled) {
  ScopedCpuProfileZone();
    DxvkRaytracingPipelineShaders shaders;
    if (useRayQuery) {
      if (wboitEnabled) {
        if (isPSRPass) {
          if (nrcEnabled) {
            shaders.addGeneralShader(GET_SHADER_VARIANT(VK_SHADER_STAGE_RAYGEN_BIT_KHR, GbufferRayGenShader, gbuffer_psr_rayquery_raygen_nrc_wboit));
          } else {
            shaders.addGeneralShader(GET_SHADER_VARIANT(VK_SHADER_STAGE_RAYGEN_BIT_KHR, GbufferRayGenShader, gbuffer_psr_rayquery_raygen_wboit));
          }

          shaders.debugName = "GBuffer PSR RayQuery (RGS)";
        } else {
          if (nrcEnabled) {
            shaders.addGeneralShader(GET_SHADER_VARIANT(VK_SHADER_STAGE_RAYGEN_BIT_KHR, GbufferRayGenShader, gbuffer_rayquery_raygen_nrc_wboit));
          } else {
            shaders.addGeneralShader(GET_SHADER_VARIANT(VK_SHADER_STAGE_RAYGEN_BIT_KHR, GbufferRayGenShader, gbuffer_rayquery_raygen_wboit));
          }
          shaders.debugName = "GBuffer RayQuery (RGS)";
        }
      } else {
        if (isPSRPass) {
          if (nrcEnabled) {
            shaders.addGeneralShader(GET_SHADER_VARIANT(VK_SHADER_STAGE_RAYGEN_BIT_KHR, GbufferRayGenShader, gbuffer_psr_rayquery_raygen_nrc));
          } else {
            shaders.addGeneralShader(GET_SHADER_VARIANT(VK_SHADER_STAGE_RAYGEN_BIT_KHR, GbufferRayGenShader, gbuffer_psr_rayquery_raygen));
          }

          shaders.debugName = "GBuffer PSR RayQuery (RGS)";
        } else {
          if (nrcEnabled) {
            shaders.addGeneralShader(GET_SHADER_VARIANT(VK_SHADER_STAGE_RAYGEN_BIT_KHR, GbufferRayGenShader, gbuffer_rayquery_raygen_nrc));
          } else {
            shaders.addGeneralShader(GET_SHADER_VARIANT(VK_SHADER_STAGE_RAYGEN_BIT_KHR, GbufferRayGenShader, gbuffer_rayquery_raygen));
          }
          shaders.debugName = "GBuffer RayQuery (RGS)";
        }
      }
    } else {  // TraceRay

      // PSR RayGen
      if (isPSRPass) {
        if (wboitEnabled) {
          if (nrcEnabled) {
            if (serEnabled) {
              shaders.addGeneralShader(GET_SHADER_VARIANT(VK_SHADER_STAGE_RAYGEN_BIT_KHR, GbufferRayGenShader, gbuffer_psr_raygen_ser_nrc_wboit));
            } else {
              shaders.addGeneralShader(GET_SHADER_VARIANT(VK_SHADER_STAGE_RAYGEN_BIT_KHR, GbufferRayGenShader, gbuffer_psr_raygen_nrc_wboit));
            }
          } else {
            if (serEnabled) {
              shaders.addGeneralShader(GET_SHADER_VARIANT(VK_SHADER_STAGE_RAYGEN_BIT_KHR, GbufferRayGenShader, gbuffer_psr_raygen_ser_wboit));
            } else {
              shaders.addGeneralShader(GET_SHADER_VARIANT(VK_SHADER_STAGE_RAYGEN_BIT_KHR, GbufferRayGenShader, gbuffer_psr_raygen_wboit));
            }
          }

          // Miss
          if (nrcEnabled) {
            shaders.addGeneralShader(GET_SHADER_VARIANT(VK_SHADER_STAGE_MISS_BIT_KHR, GbufferMissShader, gbuffer_psr_nrc_miss_wboit));
          } else {
            shaders.addGeneralShader(GET_SHADER_VARIANT(VK_SHADER_STAGE_MISS_BIT_KHR, GbufferMissShader, gbuffer_psr_miss_wboit));
          }

          // HitGroup
          if (nrcEnabled) {
            if (includePortals) {
              shaders.addHitGroup(GET_SHADER_VARIANT(VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, GbufferClosestHitShader, gbuffer_psr_nrc_material_rayportal_closestHit_wboit), nullptr, nullptr);
            } else {
              shaders.addHitGroup(GET_SHADER_VARIANT(VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, GbufferClosestHitShader, gbuffer_psr_nrc_material_opaque_translucent_closestHit_wboit), nullptr, nullptr);
            }
          } else {
            if (includePortals) {
              shaders.addHitGroup(GET_SHADER_VARIANT(VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, GbufferClosestHitShader, gbuffer_psr_material_rayportal_closestHit_wboit), nullptr, nullptr);
            } else {
              shaders.addHitGroup(GET_SHADER_VARIANT(VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, GbufferClosestHitShader, gbuffer_psr_material_opaque_translucent_closestHit_wboit), nullptr, nullptr);
            }
          }
        } else {
          if (nrcEnabled) {
            if (serEnabled) {
              shaders.addGeneralShader(GET_SHADER_VARIANT(VK_SHADER_STAGE_RAYGEN_BIT_KHR, GbufferRayGenShader, gbuffer_psr_raygen_ser_nrc));
            } else {
              shaders.addGeneralShader(GET_SHADER_VARIANT(VK_SHADER_STAGE_RAYGEN_BIT_KHR, GbufferRayGenShader, gbuffer_psr_raygen_nrc));
            }
          } else {
            if (serEnabled) {
              shaders.addGeneralShader(GET_SHADER_VARIANT(VK_SHADER_STAGE_RAYGEN_BIT_KHR, GbufferRayGenShader, gbuffer_psr_raygen_ser));
            } else {
              shaders.addGeneralShader(GET_SHADER_VARIANT(VK_SHADER_STAGE_RAYGEN_BIT_KHR, GbufferRayGenShader, gbuffer_psr_raygen));
            }
          }

          // Miss
          if (nrcEnabled) {
            shaders.addGeneralShader(GET_SHADER_VARIANT(VK_SHADER_STAGE_MISS_BIT_KHR, GbufferMissShader, gbuffer_psr_nrc_miss));
          } else {
            shaders.addGeneralShader(GET_SHADER_VARIANT(VK_SHADER_STAGE_MISS_BIT_KHR, GbufferMissShader, gbuffer_psr_miss));
          }

          // HitGroup
          if (nrcEnabled) {
            if (includePortals) {
              shaders.addHitGroup(GET_SHADER_VARIANT(VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, GbufferClosestHitShader, gbuffer_psr_nrc_material_rayportal_closestHit), nullptr, nullptr);
            } else {
              shaders.addHitGroup(GET_SHADER_VARIANT(VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, GbufferClosestHitShader, gbuffer_psr_nrc_material_opaque_translucent_closestHit), nullptr, nullptr);
            }
          } else {
            if (includePortals) {
              shaders.addHitGroup(GET_SHADER_VARIANT(VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, GbufferClosestHitShader, gbuffer_psr_material_rayportal_closestHit), nullptr, nullptr);
            } else {
              shaders.addHitGroup(GET_SHADER_VARIANT(VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, GbufferClosestHitShader, gbuffer_psr_material_opaque_translucent_closestHit), nullptr, nullptr);
            }
          }
        }
        shaders.debugName = "GBuffer PSR TraceRay (RGS)";

      } else {  // RayGen


        if (wboitEnabled) {
          if (nrcEnabled) {
            if (serEnabled) {
              shaders.addGeneralShader(GET_SHADER_VARIANT(VK_SHADER_STAGE_RAYGEN_BIT_KHR, GbufferRayGenShader, gbuffer_raygen_ser_nrc_wboit));
            } else {
              shaders.addGeneralShader(GET_SHADER_VARIANT(VK_SHADER_STAGE_RAYGEN_BIT_KHR, GbufferRayGenShader, gbuffer_raygen_nrc_wboit));
            }
          } else {
            if (serEnabled) {
              shaders.addGeneralShader(GET_SHADER_VARIANT(VK_SHADER_STAGE_RAYGEN_BIT_KHR, GbufferRayGenShader, gbuffer_raygen_ser_wboit));
            } else {
              shaders.addGeneralShader(GET_SHADER_VARIANT(VK_SHADER_STAGE_RAYGEN_BIT_KHR, GbufferRayGenShader, gbuffer_raygen_wboit));
            }
          }

          // Miss
          if (nrcEnabled) {
            shaders.addGeneralShader(GET_SHADER_VARIANT(VK_SHADER_STAGE_MISS_BIT_KHR, GbufferMissShader, gbuffer_nrc_miss_wboit));
          } else {
            shaders.addGeneralShader(GET_SHADER_VARIANT(VK_SHADER_STAGE_MISS_BIT_KHR, GbufferMissShader, gbuffer_miss_wboit));
          }

          // HitGroup
          if (nrcEnabled) {
            if (includePortals) {
              shaders.addHitGroup(GET_SHADER_VARIANT(VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, GbufferClosestHitShader, gbuffer_nrc_material_rayportal_closestHit_wboit), nullptr, nullptr);
            } else {
              shaders.addHitGroup(GET_SHADER_VARIANT(VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, GbufferClosestHitShader, gbuffer_nrc_material_opaque_translucent_closestHit_wboit), nullptr, nullptr);
            }
          } else {
            if (includePortals) {
              shaders.addHitGroup(GET_SHADER_VARIANT(VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, GbufferClosestHitShader, gbuffer_material_rayportal_closestHit_wboit), nullptr, nullptr);
            } else {
              shaders.addHitGroup(GET_SHADER_VARIANT(VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, GbufferClosestHitShader, gbuffer_material_opaque_translucent_closestHit_wboit), nullptr, nullptr);
            }
        }
        } else {
          if (nrcEnabled) {
            if (serEnabled) {
              shaders.addGeneralShader(GET_SHADER_VARIANT(VK_SHADER_STAGE_RAYGEN_BIT_KHR, GbufferRayGenShader, gbuffer_raygen_ser_nrc));
            } else {
              shaders.addGeneralShader(GET_SHADER_VARIANT(VK_SHADER_STAGE_RAYGEN_BIT_KHR, GbufferRayGenShader, gbuffer_raygen_nrc));
            }
          } else {
            if (serEnabled) {
              shaders.addGeneralShader(GET_SHADER_VARIANT(VK_SHADER_STAGE_RAYGEN_BIT_KHR, GbufferRayGenShader, gbuffer_raygen_ser));
            } else {
              shaders.addGeneralShader(GET_SHADER_VARIANT(VK_SHADER_STAGE_RAYGEN_BIT_KHR, GbufferRayGenShader, gbuffer_raygen));
            }
          }

          // Miss
          if (nrcEnabled) {
            shaders.addGeneralShader(GET_SHADER_VARIANT(VK_SHADER_STAGE_MISS_BIT_KHR, GbufferMissShader, gbuffer_nrc_miss));
          } else {
            shaders.addGeneralShader(GET_SHADER_VARIANT(VK_SHADER_STAGE_MISS_BIT_KHR, GbufferMissShader, gbuffer_miss));
          }

          // HitGroup
          if (nrcEnabled) {
            if (includePortals) {
              shaders.addHitGroup(GET_SHADER_VARIANT(VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, GbufferClosestHitShader, gbuffer_nrc_material_rayportal_closestHit), nullptr, nullptr);
            } else {
              shaders.addHitGroup(GET_SHADER_VARIANT(VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, GbufferClosestHitShader, gbuffer_nrc_material_opaque_translucent_closestHit), nullptr, nullptr);
            }
          } else {
            if (includePortals) {
              shaders.addHitGroup(GET_SHADER_VARIANT(VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, GbufferClosestHitShader, gbuffer_material_rayportal_closestHit), nullptr, nullptr);
            } else {
              shaders.addHitGroup(GET_SHADER_VARIANT(VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, GbufferClosestHitShader, gbuffer_material_opaque_translucent_closestHit), nullptr, nullptr);
            }
          }
        }

        shaders.debugName = "GBuffer TraceRay (RGS)";
      }
    }

    if (ommEnabled) {
      shaders.pipelineFlags |= VK_PIPELINE_CREATE_RAY_TRACING_OPACITY_MICROMAP_BIT_EXT;
    }

    return shaders;
  }

  Rc<DxvkShader> DxvkPathtracerGbuffer::getComputeShader(
    const bool isPSRPass,
    const bool nrcEnabled, 
    const bool wboitEnabled) const {
    if (wboitEnabled) {
      if (nrcEnabled) {
        if (isPSRPass) {
          return GET_SHADER_VARIANT(VK_SHADER_STAGE_COMPUTE_BIT, GbufferRayGenShader, gbuffer_psr_rayquery_nrc_wboit);
        } else {
          return GET_SHADER_VARIANT(VK_SHADER_STAGE_COMPUTE_BIT, GbufferRayGenShader, gbuffer_rayquery_nrc_wboit);
        }
      } else {
        if (isPSRPass) {
          return GET_SHADER_VARIANT(VK_SHADER_STAGE_COMPUTE_BIT, GbufferRayGenShader, gbuffer_psr_rayquery_wboit);
        } else {
          return GET_SHADER_VARIANT(VK_SHADER_STAGE_COMPUTE_BIT, GbufferRayGenShader, gbuffer_rayquery_wboit);
        }
      }
    } else {
      if (nrcEnabled) {
        if (isPSRPass) {
          return GET_SHADER_VARIANT(VK_SHADER_STAGE_COMPUTE_BIT, GbufferRayGenShader, gbuffer_psr_rayquery_nrc);
        } else {
          return GET_SHADER_VARIANT(VK_SHADER_STAGE_COMPUTE_BIT, GbufferRayGenShader, gbuffer_rayquery_nrc);
        }
      } else {
        if (isPSRPass) {
          return GET_SHADER_VARIANT(VK_SHADER_STAGE_COMPUTE_BIT, GbufferRayGenShader, gbuffer_psr_rayquery);
        } else {
          return GET_SHADER_VARIANT(VK_SHADER_STAGE_COMPUTE_BIT, GbufferRayGenShader, gbuffer_rayquery);
        }
      }
    }
  }

  const char* DxvkPathtracerGbuffer::raytraceModeToString(RaytraceMode raytraceMode)
  {
    switch (raytraceMode)
    {
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
