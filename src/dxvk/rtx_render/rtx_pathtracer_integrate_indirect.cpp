/*
* Copyright (c) 2023-2025, NVIDIA CORPORATION. All rights reserved.
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
#include "rtx_shader_manager.h"
#include "rtx_context.h"
#include "rtx_options.h"
#include "rtx_neural_radiance_cache.h"
#include "rtx_restir_gi_rayquery.h"
#include "rtx_debug_view.h"

#include "rtx/pass/common_binding_indices.h"
#include "rtx/pass/integrate/integrate_indirect_binding_indices.h"
#include "rtx/pass/integrate/integrate_nee_binding_indices.h"
#include "rtx/concept/surface_material/surface_material_hitgroup.h"

#include <rtx_shaders/integrate_indirect_raygen_neeCache.h>
#include <rtx_shaders/integrate_indirect_raygen_ser_neeCache.h>
#include <rtx_shaders/integrate_indirect_rayquery_raygen_neeCache.h>
#include <rtx_shaders/integrate_indirect_rayquery_raygen.h>
#include <rtx_shaders/integrate_indirect_raygen.h>
#include <rtx_shaders/integrate_indirect_raygen_ser.h>
#include <rtx_shaders/integrate_indirect_raygen_nrc_neeCache.h>
#include <rtx_shaders/integrate_indirect_raygen_ser_nrc_neeCache.h>
#include <rtx_shaders/integrate_indirect_rayquery_raygen_nrc_neeCache.h>
#include <rtx_shaders/integrate_indirect_rayquery_raygen_nrc.h>
#include <rtx_shaders/integrate_indirect_raygen_nrc.h>
#include <rtx_shaders/integrate_indirect_raygen_ser_nrc.h>

#include <rtx_shaders/integrate_indirect_rayquery_neeCache.h>
#include <rtx_shaders/integrate_indirect_rayquery.h>
#include <rtx_shaders/integrate_indirect_rayquery_nrc_neeCache.h>
#include <rtx_shaders/integrate_indirect_rayquery_nrc.h>

#include <rtx_shaders/integrate_indirect_material_opaque_translucent_closestHit.h>
#include <rtx_shaders/integrate_indirect_material_rayPortal_closestHit.h>
#include <rtx_shaders/integrate_indirect_pom_material_opaque_translucent_closestHit.h>
#include <rtx_shaders/integrate_indirect_pom_material_rayPortal_closestHit.h>
#include <rtx_shaders/integrate_indirect_nrc_material_opaque_translucent_closestHit.h>
#include <rtx_shaders/integrate_indirect_nrc_material_rayPortal_closestHit.h>
#include <rtx_shaders/integrate_indirect_nrc_pom_material_opaque_translucent_closestHit.h>
#include <rtx_shaders/integrate_indirect_nrc_pom_material_rayPortal_closestHit.h>
#include <rtx_shaders/integrate_indirect_neeCache_material_rayportal_closestHit.h>
#include <rtx_shaders/integrate_indirect_neeCache_material_opaque_translucent_closestHit.h>
#include <rtx_shaders/integrate_indirect_neeCache_pom_material_rayportal_closestHit.h>
#include <rtx_shaders/integrate_indirect_neeCache_pom_material_opaque_translucent_closestHit.h>
#include <rtx_shaders/integrate_indirect_nrc_neeCache_material_rayportal_closestHit.h>
#include <rtx_shaders/integrate_indirect_nrc_neeCache_material_opaque_translucent_closestHit.h>
#include <rtx_shaders/integrate_indirect_nrc_neeCache_pom_material_rayportal_closestHit.h>
#include <rtx_shaders/integrate_indirect_nrc_neeCache_pom_material_opaque_translucent_closestHit.h>

#include <rtx_shaders/integrate_indirect_miss.h>
#include <rtx_shaders/integrate_indirect_miss_neeCache.h>
#include <rtx_shaders/integrate_indirect_miss_nrc.h>
#include <rtx_shaders/integrate_indirect_miss_nrc_neeCache.h>



#include <rtx_shaders/integrate_indirect_raygen_neeCache_wboit.h>
#include <rtx_shaders/integrate_indirect_raygen_ser_neeCache_wboit.h>
#include <rtx_shaders/integrate_indirect_rayquery_raygen_neeCache_wboit.h>
#include <rtx_shaders/integrate_indirect_rayquery_raygen_wboit.h>
#include <rtx_shaders/integrate_indirect_raygen_wboit.h>
#include <rtx_shaders/integrate_indirect_raygen_ser_wboit.h>
#include <rtx_shaders/integrate_indirect_raygen_nrc_neeCache_wboit.h>
#include <rtx_shaders/integrate_indirect_raygen_ser_nrc_neeCache_wboit.h>
#include <rtx_shaders/integrate_indirect_rayquery_raygen_nrc_neeCache_wboit.h>
#include <rtx_shaders/integrate_indirect_rayquery_raygen_nrc_wboit.h>
#include <rtx_shaders/integrate_indirect_raygen_nrc_wboit.h>
#include <rtx_shaders/integrate_indirect_raygen_ser_nrc_wboit.h>

#include <rtx_shaders/integrate_indirect_rayquery_neeCache_wboit.h>
#include <rtx_shaders/integrate_indirect_rayquery_wboit.h>
#include <rtx_shaders/integrate_indirect_rayquery_nrc_neeCache_wboit.h>
#include <rtx_shaders/integrate_indirect_rayquery_nrc_wboit.h>

#include <rtx_shaders/integrate_indirect_material_opaque_translucent_closestHit_wboit.h>
#include <rtx_shaders/integrate_indirect_material_rayPortal_closestHit_wboit.h>
#include <rtx_shaders/integrate_indirect_pom_material_opaque_translucent_closestHit_wboit.h>
#include <rtx_shaders/integrate_indirect_pom_material_rayPortal_closestHit_wboit.h>
#include <rtx_shaders/integrate_indirect_nrc_material_opaque_translucent_closestHit_wboit.h>
#include <rtx_shaders/integrate_indirect_nrc_material_rayPortal_closestHit_wboit.h>
#include <rtx_shaders/integrate_indirect_nrc_pom_material_opaque_translucent_closestHit_wboit.h>
#include <rtx_shaders/integrate_indirect_nrc_pom_material_rayPortal_closestHit_wboit.h>
#include <rtx_shaders/integrate_indirect_neeCache_material_rayportal_closestHit_wboit.h>
#include <rtx_shaders/integrate_indirect_neeCache_material_opaque_translucent_closestHit_wboit.h>
#include <rtx_shaders/integrate_indirect_neeCache_pom_material_rayportal_closestHit_wboit.h>
#include <rtx_shaders/integrate_indirect_neeCache_pom_material_opaque_translucent_closestHit_wboit.h>
#include <rtx_shaders/integrate_indirect_nrc_neeCache_material_rayportal_closestHit_wboit.h>
#include <rtx_shaders/integrate_indirect_nrc_neeCache_material_opaque_translucent_closestHit_wboit.h>
#include <rtx_shaders/integrate_indirect_nrc_neeCache_pom_material_rayportal_closestHit_wboit.h>
#include <rtx_shaders/integrate_indirect_nrc_neeCache_pom_material_opaque_translucent_closestHit_wboit.h>

#include <rtx_shaders/integrate_indirect_miss_wboit.h>
#include <rtx_shaders/integrate_indirect_miss_neeCache_wboit.h>
#include <rtx_shaders/integrate_indirect_miss_nrc_wboit.h>
#include <rtx_shaders/integrate_indirect_miss_nrc_neeCache_wboit.h>

#include <rtx_shaders/integrate_nee.h>
#include <rtx_shaders/visualize_nee.h>

#include "dxvk_scoped_annotation.h"
#include "rtx_opacity_micromap_manager.h"
#include "rtx_neural_radiance_cache.h"

namespace dxvk {

  // Defined within an unnamed namespace to ensure unique definition across binary
  namespace {
    class IntegrateIndirectRayGenShader : public ManagedShader {
    public:
      BINDLESS_ENABLED()

      BEGIN_PARAMETER()
        COMMON_RAYTRACING_BINDINGS

        SAMPLER(INTEGRATE_INDIRECT_BINDING_LINEAR_WRAP_SAMPLER)

        SAMPLERCUBE(INTEGRATE_INDIRECT_BINDING_SKYPROBE)

        TEXTURE2D(INTEGRATE_INDIRECT_BINDING_SHARED_FLAGS_INPUT)
        TEXTURE2D(INTEGRATE_INDIRECT_BINDING_SHARED_MEDIUM_MATERIAL_INDEX_INPUT)
        TEXTURE2D(INTEGRATE_INDIRECT_BINDING_SHARED_TEXTURE_COORD_INPUT)
        TEXTURE2D(INTEGRATE_INDIRECT_BINDING_SHARED_SURFACE_INDEX_INPUT)
        TEXTURE2D(INTEGRATE_INDIRECT_BINDING_SHARED_SUBSURFACE_DATA_INPUT)

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
        SAMPLER3D(INTEGRATE_INDIRECT_BINDING_VOLUME_FILTERED_RADIANCE_Y_INPUT)
        SAMPLER3D(INTEGRATE_INDIRECT_BINDING_VOLUME_FILTERED_RADIANCE_CO_CG_INPUT)
        TEXTURE2D(INTEGRATE_INDIRECT_BINDING_PRIMARY_HIT_DISTANCE_INPUT)
        TEXTURE2D(INTEGRATE_INDIRECT_BINDING_SECONDARY_HIT_DISTANCE_INPUT)
        TEXTURE2D(INTEGRATE_INDIRECT_BINDING_LAST_COMPOSITE_INPUT)

        TEXTURE2DARRAY(INTEGRATE_INDIRECT_BINDING_GRADIENTS_INPUT)

        TEXTURE2D(INTEGRATE_INDIRECT_BINDING_NRC_PATH_DATA0_INPUT)
        TEXTURE2D(INTEGRATE_INDIRECT_BINDING_NRC_UPDATE_PATH_DATA0_INPUT)
        TEXTURE2D(INTEGRATE_INDIRECT_BINDING_NRC_PATH_DATA1_INPUT)
        TEXTURE2D(INTEGRATE_INDIRECT_BINDING_NRC_UPDATE_PATH_DATA1_INPUT)

        TEXTURE2D(INTEGRATE_INDIRECT_BINDING_NRC_TRAINING_GBUFFER_SURFACE_RADIANCE_RG_INPUT)
        TEXTURE2D(INTEGRATE_INDIRECT_BINDING_NRC_TRAINING_GBUFFER_SURFACE_RADIANCE_B_INPUT)

        RW_TEXTURE2D(INTEGRATE_INDIRECT_BINDING_PRIMARY_DIRECT_DIFFUSE_LOBE_RADIANCE_INPUT_OUTPUT)
        RW_TEXTURE2D(INTEGRATE_INDIRECT_BINDING_PRIMARY_DIRECT_SPECULAR_LOBE_RADIANCE_INPUT_OUTPUT)
        RW_TEXTURE2D(INTEGRATE_INDIRECT_BINDING_SECONDARY_COMBINED_DIFFUSE_LOBE_RADIANCE_INPUT_OUTPUT)
        RW_TEXTURE2D(INTEGRATE_INDIRECT_BINDING_SECONDARY_COMBINED_SPECULAR_LOBE_RADIANCE_INPUT_OUTPUT)

        RW_STRUCTURED_BUFFER(INTEGRATE_INDIRECT_BINDING_NRC_QUERY_PATH_INFO_INPUT_OUTPUT)
        RW_STRUCTURED_BUFFER(INTEGRATE_INDIRECT_BINDING_NRC_TRAINING_PATH_INFO_INPUT_OUTPUT)
        RW_STRUCTURED_BUFFER(INTEGRATE_INDIRECT_BINDING_NRC_TRAINING_PATH_VERTICES_INPUT_OUTPUT)
        RW_STRUCTURED_BUFFER(INTEGRATE_INDIRECT_BINDING_NRC_QUERY_RADIANCE_PARAMS_INPUT_OUTPUT)
        RW_STRUCTURED_BUFFER(INTEGRATE_INDIRECT_BINDING_NRC_COUNTERS_INPUT_OUTPUT)

        RW_TEXTURE2D(INTEGRATE_INDIRECT_BINDING_INDIRECT_RADIANCE_HIT_DISTANCE_OUTPUT)
        RW_STRUCTURED_BUFFER(INTEGRATE_INDIRECT_BINDING_RESTIR_GI_RESERVOIR_OUTPUT)
        RW_TEXTURE2D(INTEGRATE_INDIRECT_BINDING_RESTIR_GI_RADIANCE_OUTPUT)
        RW_TEXTURE2D(INTEGRATE_INDIRECT_BINDING_RESTIR_GI_HIT_GEOMETRY_OUTPUT)

        STRUCTURED_BUFFER(INTEGRATE_INDIRECT_BINDING_NEE_CACHE)
        STRUCTURED_BUFFER(INTEGRATE_INDIRECT_BINDING_NEE_CACHE_SAMPLE)
        STRUCTURED_BUFFER(INTEGRATE_INDIRECT_BINDING_PRIMITIVE_ID_PREFIX_SUM)
        RW_STRUCTURED_BUFFER(INTEGRATE_INDIRECT_BINDING_NEE_CACHE_TASK)
        RW_TEXTURE2D(INTEGRATE_INDIRECT_BINDING_NEE_CACHE_THREAD_TASK)


        RW_TEXTURE2D(INTEGRATE_INSTRUMENTATION)

      END_PARAMETER()
    };

    class IntegrateIndirectClosestHitShader : public ManagedShader {

      BEGIN_PARAMETER()
      END_PARAMETER()
    };

    class IntegrateIndirectMissShader : public ManagedShader {

      BEGIN_PARAMETER()
      END_PARAMETER()
    };

    class IntegrateNEEShader : public ManagedShader {
      SHADER_SOURCE(IntegrateNEEShader, VK_SHADER_STAGE_COMPUTE_BIT, integrate_nee)
      
      BINDLESS_ENABLED()

      BEGIN_PARAMETER()
        COMMON_RAYTRACING_BINDINGS

        TEXTURE2D(INTEGRATE_NEE_BINDING_SHARED_FLAGS_INPUT)
        TEXTURE2D(INTEGRATE_NEE_BINDING_SHARED_MATERIAL_DATA0_INPUT)
        TEXTURE2D(INTEGRATE_NEE_BINDING_SHARED_MATERIAL_DATA1_INPUT)
        TEXTURE2D(INTEGRATE_NEE_BINDING_SHARED_TEXTURE_COORD_INPUT)
        TEXTURE2D(INTEGRATE_NEE_BINDING_SHARED_SURFACE_INDEX_INPUT)
        TEXTURE2D(INTEGRATE_NEE_BINDING_SHARED_SUBSURFACE_DATA_INPUT)
        TEXTURE2D(INTEGRATE_NEE_BINDING_SHARED_SUBSURFACE_DIFFUSION_PROFILE_DATA_INPUT)

        TEXTURE2D(INTEGRATE_NEE_BINDING_PRIMARY_WORLD_SHADING_NORMAL_INPUT)
        TEXTURE2D(INTEGRATE_NEE_BINDING_PRIMARY_WORLD_INTERPOLATED_NORMAL_INPUT)
        TEXTURE2D(INTEGRATE_NEE_BINDING_PRIMARY_PERCEPTUAL_ROUGHNESS_INPUT)
        TEXTURE2D(INTEGRATE_NEE_BINDING_PRIMARY_HIT_DISTANCE_INPUT)
        TEXTURE2D(INTEGRATE_NEE_BINDING_PRIMARY_ALBEDO_INPUT)
        TEXTURE2D(INTEGRATE_NEE_BINDING_PRIMARY_VIEW_DIRECTION_INPUT)
        TEXTURE2D(INTEGRATE_NEE_BINDING_PRIMARY_CONE_RADIUS_INPUT)
        TEXTURE2D(INTEGRATE_NEE_BINDING_PRIMARY_WORLD_POSITION_INPUT)
        TEXTURE2D(INTEGRATE_NEE_BINDING_PRIMARY_POSITION_ERROR_INPUT)
        TEXTURE2D(INTEGRATE_NEE_BINDING_INDIRECT_RADIANCE_HIT_DISTANCE_INPUT)
        STRUCTURED_BUFFER(INTEGRATE_NEE_BINDING_PRIMITIVE_ID_PREFIX_SUM_INPUT)

        RW_TEXTURE2D(INTEGRATE_NEE_BINDING_PRIMARY_BASE_REFLECTIVITY_INPUT_OUTPUT)
        RW_STRUCTURED_BUFFER(INTEGRATE_NEE_BINDING_NRC_TRAINING_PATH_VERTICES_INPUT_OUTPUT)

        RW_TEXTURE2D(INTEGRATE_NEE_BINDING_PRIMARY_INDIRECT_DIFFUSE_RADIANCE_HIT_DISTANCE_OUTPUT)
        RW_TEXTURE2D(INTEGRATE_NEE_BINDING_PRIMARY_INDIRECT_SPECULAR_RADIANCE_HIT_DISTANCE_OUTPUT)
        RW_STRUCTURED_BUFFER(INTEGRATE_NEE_BINDING_RESTIR_GI_RESERVOIR_OUTPUT)
        RW_TEXTURE2D(INTEGRATE_NEE_BINDING_BSDF_FACTOR2_OUTPUT)

        RW_STRUCTURED_BUFFER(INTEGRATE_NEE_BINDING_NEE_CACHE)
        RW_STRUCTURED_BUFFER(INTEGRATE_NEE_BINDING_NEE_CACHE_TASK)
        RW_STRUCTURED_BUFFER(INTEGRATE_NEE_BINDING_NEE_CACHE_SAMPLE)
        RW_TEXTURE2D(INTEGRATE_NEE_BINDING_NEE_CACHE_THREAD_TASK)
      END_PARAMETER()
    };

    PREWARM_SHADER_PIPELINE(IntegrateNEEShader);

    class VisualizeNEEShader : public ManagedShader {
      SHADER_SOURCE(VisualizeNEEShader, VK_SHADER_STAGE_COMPUTE_BIT, visualize_nee)

      PUSH_CONSTANTS(VisualizeNeeArgs)
      
      BINDLESS_ENABLED()

      BEGIN_PARAMETER()
        COMMON_RAYTRACING_BINDINGS

        TEXTURE2D(INTEGRATE_NEE_BINDING_SHARED_FLAGS_INPUT)
        TEXTURE2D(INTEGRATE_NEE_BINDING_SHARED_MATERIAL_DATA0_INPUT)
        TEXTURE2D(INTEGRATE_NEE_BINDING_SHARED_MATERIAL_DATA1_INPUT)
        RW_TEXTURE2D(INTEGRATE_NEE_BINDING_SHARED_TEXTURE_COORD_INPUT)
        RW_TEXTURE2D(INTEGRATE_NEE_BINDING_SHARED_SURFACE_INDEX_INPUT)
        TEXTURE2D(INTEGRATE_NEE_BINDING_SHARED_SUBSURFACE_DATA_INPUT)
        TEXTURE2D(INTEGRATE_NEE_BINDING_SHARED_SUBSURFACE_DIFFUSION_PROFILE_DATA_INPUT)

        TEXTURE2D(INTEGRATE_NEE_BINDING_PRIMARY_WORLD_SHADING_NORMAL_INPUT)
        TEXTURE2D(INTEGRATE_NEE_BINDING_PRIMARY_WORLD_INTERPOLATED_NORMAL_INPUT)
        TEXTURE2D(INTEGRATE_NEE_BINDING_PRIMARY_PERCEPTUAL_ROUGHNESS_INPUT)
        TEXTURE2D(INTEGRATE_NEE_BINDING_PRIMARY_HIT_DISTANCE_INPUT)
        TEXTURE2D(INTEGRATE_NEE_BINDING_PRIMARY_ALBEDO_INPUT)
        TEXTURE2D(INTEGRATE_NEE_BINDING_PRIMARY_VIEW_DIRECTION_INPUT)
        TEXTURE2D(INTEGRATE_NEE_BINDING_PRIMARY_CONE_RADIUS_INPUT)
        TEXTURE2D(INTEGRATE_NEE_BINDING_PRIMARY_WORLD_POSITION_INPUT)
        TEXTURE2D(INTEGRATE_NEE_BINDING_PRIMARY_POSITION_ERROR_INPUT)
        TEXTURE2D(INTEGRATE_NEE_BINDING_INDIRECT_RADIANCE_HIT_DISTANCE_INPUT)
        STRUCTURED_BUFFER(INTEGRATE_NEE_BINDING_PRIMITIVE_ID_PREFIX_SUM_INPUT)

        RW_TEXTURE2D(INTEGRATE_NEE_BINDING_PRIMARY_BASE_REFLECTIVITY_INPUT_OUTPUT)
        RW_STRUCTURED_BUFFER(INTEGRATE_NEE_BINDING_NRC_TRAINING_PATH_VERTICES_INPUT_OUTPUT)

        RW_TEXTURE2D(INTEGRATE_NEE_BINDING_PRIMARY_INDIRECT_DIFFUSE_RADIANCE_HIT_DISTANCE_OUTPUT)
        RW_TEXTURE2D(INTEGRATE_NEE_BINDING_PRIMARY_INDIRECT_SPECULAR_RADIANCE_HIT_DISTANCE_OUTPUT)
        RW_STRUCTURED_BUFFER(INTEGRATE_NEE_BINDING_RESTIR_GI_RESERVOIR_OUTPUT)
        RW_TEXTURE2D(INTEGRATE_NEE_BINDING_BSDF_FACTOR2_OUTPUT)

        RW_STRUCTURED_BUFFER(INTEGRATE_NEE_BINDING_NEE_CACHE)
        RW_STRUCTURED_BUFFER(INTEGRATE_NEE_BINDING_NEE_CACHE_TASK)
        RW_STRUCTURED_BUFFER(INTEGRATE_NEE_BINDING_NEE_CACHE_SAMPLE)
        RW_TEXTURE2D(INTEGRATE_NEE_BINDING_NEE_CACHE_THREAD_TASK)
      END_PARAMETER()
    };

    PREWARM_SHADER_PIPELINE(VisualizeNEEShader);
  }

  DxvkPathtracerIntegrateIndirect::DxvkPathtracerIntegrateIndirect(DxvkDevice* device) 
    : CommonDeviceObject(device)
    , m_integrateIndirectMode(IntegrateIndirectMode::Count) {
  }

  void DxvkPathtracerIntegrateIndirect::prewarmShaders(DxvkPipelineManager& pipelineManager) const {
    ScopedCpuProfileZoneN("Indirect Integrate Shader Prewarming");

    const bool isNrcSupported = NeuralRadianceCache::checkIsSupported(device());
    const bool isOpacityMicromapSupported = OpacityMicromapManager::checkIsOpacityMicromapSupported(*m_device);
    const bool isShaderExecutionReorderingSupported = 
      RtxContext::checkIsShaderExecutionReorderingSupported(*m_device) &&
      RtxOptions::isShaderExecutionReorderingInPathtracerIntegrateIndirectEnabled();
    // Note: Portal enablement is controlled only via the configuration so unlike other things which can be enabled/disabled via
    // ImGui at runtime this is fine to use as a guide for which permutations need to be generated (much like if OMM or SER are
    // supported on a given platform, as this fact will not change during runtime either).
    const bool portalsEnabled = RtxOptions::rayPortalModelTextureHashes().size() > 0;

    if (RtxOptions::Shader::prewarmAllVariants()) {
      for (int32_t nrcEnabled = isNrcSupported ? 1 : 0; nrcEnabled >= 0; nrcEnabled--) {
        for (int32_t useNeeCache = 1; useNeeCache >= 0; useNeeCache--) {
          for (int32_t wboitEnabled = 1; wboitEnabled >= 0; wboitEnabled--) {
            for (int32_t includesPortals = portalsEnabled; includesPortals >= 0; includesPortals--) {
              for (int32_t useRayQuery = 1; useRayQuery >= 0; useRayQuery--) {
                for (int32_t serEnabled = isShaderExecutionReorderingSupported; serEnabled >= 0; serEnabled--) {
                  for (int32_t ommEnabled = isOpacityMicromapSupported; ommEnabled >= 0; ommEnabled--) {
                    for (int32_t pomEnabled = 1; pomEnabled >= 0; pomEnabled--) {
                      pipelineManager.registerRaytracingShaders(getPipelineShaders(useRayQuery, serEnabled, ommEnabled, useNeeCache, includesPortals, pomEnabled, nrcEnabled, wboitEnabled));
                    }
                  }
                }
              }
            }

            getComputeShader(useNeeCache, nrcEnabled, wboitEnabled);
          }
        }
      }
    } else {
      // Note: The getters for these SER/OMM enabled flags also check if SER/OMMs are supported, so we do not need to check for that manually.
      const bool serEnabled = RtxOptions::isShaderExecutionReorderingInPathtracerIntegrateIndirectEnabled();
      const bool ommEnabled = OpacityMicromapManager::checkIsOpacityMicromapSupported(*m_device) && RtxOptions::OpacityMicromap::enable();
      const bool useNeeCache = NeeCachePass::enable();
      const bool nrcEnabled = RtxOptions::integrateIndirectMode() == IntegrateIndirectMode::NeuralRadianceCache;
      const bool wboitEnabled = RtxOptions::wboitEnabled();

      for (int32_t includesPortals = portalsEnabled; includesPortals >= 0; includesPortals--) {
        // Prewarm POM on and off, as that can change based on game content (if nothing in the frame has a height texture, then POM turns off)
        for (int32_t pomEnabled = 1; pomEnabled >= 0; pomEnabled--) {
          DxvkComputePipelineShaders shaders;
          switch (RtxOptions::renderPassIntegrateIndirectRaytraceMode()) {
          case RaytraceMode::RayQuery:
            getComputeShader(useNeeCache, nrcEnabled, wboitEnabled);
            break;
          case RaytraceMode::RayQueryRayGen:
            pipelineManager.registerRaytracingShaders(getPipelineShaders(true, serEnabled, ommEnabled, useNeeCache, includesPortals, pomEnabled, nrcEnabled, wboitEnabled));
            break;
          case RaytraceMode::TraceRay:
            pipelineManager.registerRaytracingShaders(getPipelineShaders(false, serEnabled, ommEnabled, useNeeCache, includesPortals, pomEnabled, nrcEnabled, wboitEnabled));
            break;
          case RaytraceMode::Count:
            assert(false && "Invalid RaytraceMode in DxvkPathtracerIntegrateIndirect::prewarmShaders");
            break;
          }
        }
      }
    }
  }

  void DxvkPathtracerIntegrateIndirect::logIntegrateIndirectMode() {
    if (m_integrateIndirectMode != RtxOptions::integrateIndirectMode()) {
      m_integrateIndirectMode = RtxOptions::integrateIndirectMode();

      switch (m_integrateIndirectMode) {
      default:
        assert(0);
        break;
      case IntegrateIndirectMode::ImportanceSampled:
        Logger::info("[RTX] Integrate Indirect Mode: Importance Sampled - activated");
        break;
      case IntegrateIndirectMode::ReSTIRGI:
        Logger::info("[RTX] Integrate Indirect Mode: ReSTIR GI - activated");
        break;
      case IntegrateIndirectMode::NeuralRadianceCache:
        Logger::info("[RTX] Integrate Indirect Mode: Neural Radiance Cache - activated");
        break;
      }
    }
  }

  void DxvkPathtracerIntegrateIndirect::dispatch(
    RtxContext* ctx, 
    const Resources::RaytracingOutput& rtOutput) {

    const uint32_t frameIdx = ctx->getDevice()->getCurrentFrameId();

    logIntegrateIndirectMode();

    // Bind resources

    // Note: Clamp to edge used to avoid interpolation to black on the edges of the view.
    Rc<DxvkSampler> linearClampSampler = ctx->getResourceManager().getSampler(VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
    Rc<DxvkSampler> linearWrapSampler = ctx->getResourceManager().getSampler(VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_NEAREST, VK_SAMPLER_ADDRESS_MODE_REPEAT);
    Rc<DxvkBuffer> primitiveIDPrefixSumBuffer = ctx->getSceneManager().getCurrentFramePrimitiveIDPrefixSumBuffer();

    ctx->bindCommonRayTracingResources(rtOutput);

    ctx->bindResourceSampler(INTEGRATE_INDIRECT_BINDING_LINEAR_WRAP_SAMPLER, linearWrapSampler);

    ctx->bindResourceView(INTEGRATE_INDIRECT_BINDING_SKYPROBE, ctx->getResourceManager().getSkyProbe(ctx).view, nullptr);
    ctx->bindResourceSampler(INTEGRATE_INDIRECT_BINDING_SKYPROBE, linearClampSampler);

    ctx->bindResourceView(INTEGRATE_INDIRECT_BINDING_SHARED_FLAGS_INPUT, rtOutput.m_sharedFlags.view, nullptr);
    ctx->bindResourceView(INTEGRATE_INDIRECT_BINDING_SHARED_MEDIUM_MATERIAL_INDEX_INPUT, rtOutput.m_sharedMediumMaterialIndex.view, nullptr);
    ctx->bindResourceView(INTEGRATE_INDIRECT_BINDING_SHARED_TEXTURE_COORD_INPUT, rtOutput.m_sharedTextureCoord.view, nullptr);
    ctx->bindResourceView(INTEGRATE_INDIRECT_BINDING_SHARED_SURFACE_INDEX_INPUT, rtOutput.m_sharedSurfaceIndex.view(Resources::AccessType::Read), nullptr);
    ctx->bindResourceView(INTEGRATE_INDIRECT_BINDING_SHARED_SUBSURFACE_DATA_INPUT, rtOutput.m_sharedSubsurfaceData.view, nullptr);

    ctx->bindResourceView(INTEGRATE_INDIRECT_BINDING_PRIMARY_CONE_RADIUS_INPUT, rtOutput.m_primaryConeRadius.view, nullptr);
    ctx->bindResourceView(INTEGRATE_INDIRECT_BINDING_SECONDARY_CONE_RADIUS_INPUT, rtOutput.m_secondaryConeRadius.view(Resources::AccessType::Read), nullptr);
    ctx->bindResourceView(INTEGRATE_INDIRECT_BINDING_PRIMARY_WORLD_POSITION_INPUT, rtOutput.getCurrentPrimaryWorldPositionWorldTriangleNormal().view(Resources::AccessType::Read), nullptr);
    ctx->bindResourceBuffer(INTEGRATE_INDIRECT_BINDING_PRIMARY_RTXDI_RESERVOIR, DxvkBufferSlice(rtOutput.m_rtxdiReservoirBuffer, 0, rtOutput.m_rtxdiReservoirBuffer->info().size));
    ctx->bindResourceView(INTEGRATE_INDIRECT_BINDING_RAY_ORIGIN_DIRECTION_INPUT, rtOutput.m_indirectRayOriginDirection.view(Resources::AccessType::Read), nullptr);
    ctx->bindResourceView(INTEGRATE_INDIRECT_BINDING_FIRST_HIT_PERCEPTUAL_ROUGHNESS_INPUT, rtOutput.m_indirectFirstHitPerceptualRoughness.view(Resources::AccessType::Read), nullptr);
    ctx->bindResourceView(INTEGRATE_INDIRECT_BINDING_LAST_GBUFFER_INPUT, rtOutput.m_gbufferLast.view, nullptr);
    ctx->bindResourceView(INTEGRATE_INDIRECT_BINDING_PREV_WORLD_POSITION_INPUT, rtOutput.getPreviousPrimaryWorldPositionWorldTriangleNormal().view(Resources::AccessType::Read, rtOutput.getPreviousPrimaryWorldPositionWorldTriangleNormal().matchesWriteFrameIdx(frameIdx - 1)), nullptr);

    const RtxGlobalVolumetrics& globalVolumetrics = ctx->getCommonObjects()->metaGlobalVolumetrics();
    ctx->bindResourceView(INTEGRATE_INDIRECT_BINDING_VOLUME_FILTERED_RADIANCE_Y_INPUT, globalVolumetrics.getCurrentVolumeAccumulatedRadianceY().view, nullptr);
    ctx->bindResourceSampler(INTEGRATE_INDIRECT_BINDING_VOLUME_FILTERED_RADIANCE_Y_INPUT, linearClampSampler);
    ctx->bindResourceView(INTEGRATE_INDIRECT_BINDING_VOLUME_FILTERED_RADIANCE_CO_CG_INPUT, globalVolumetrics.getCurrentVolumeAccumulatedRadianceCoCg().view, nullptr);
    ctx->bindResourceSampler(INTEGRATE_INDIRECT_BINDING_VOLUME_FILTERED_RADIANCE_CO_CG_INPUT, linearClampSampler);

    ctx->bindResourceView(INTEGRATE_INDIRECT_BINDING_PRIMARY_HIT_DISTANCE_INPUT, rtOutput.m_primaryHitDistance.view, nullptr);
    ctx->bindResourceView(INTEGRATE_INDIRECT_BINDING_SECONDARY_HIT_DISTANCE_INPUT, rtOutput.m_secondaryHitDistance.view, nullptr);

    const DxvkReSTIRGIRayQuery& restirGI = ctx->getCommonObjects()->metaReSTIRGIRayQuery();
    if (restirGI.isActive()) {
      const uint32_t isLastCompositeOutputValid = restirGI.getLastCompositeOutput().matchesWriteFrameIdx(frameIdx - 1);
      assert(isLastCompositeOutputValid == rtOutput.m_raytraceArgs.isLastCompositeOutputValid && "Last composite state changed since CB was initialized");
      ctx->bindResourceView(INTEGRATE_INDIRECT_BINDING_LAST_COMPOSITE_INPUT, restirGI.getLastCompositeOutput().view(Resources::AccessType::Read, isLastCompositeOutputValid), nullptr);
    } else {
      ctx->bindResourceView(INTEGRATE_INDIRECT_BINDING_LAST_COMPOSITE_INPUT, nullptr, nullptr);
    }
    
    ctx->bindResourceView(INTEGRATE_INDIRECT_BINDING_FIRST_SAMPLED_LOBE_DATA_INPUT, rtOutput.m_indirectFirstSampledLobeData.view(Resources::AccessType::Read), nullptr);
    ctx->bindResourceView(INTEGRATE_INDIRECT_BINDING_GRADIENTS_INPUT, rtOutput.m_rtxdiGradients.view, nullptr);

    // Input / Output resources

    ctx->bindResourceView(INTEGRATE_INDIRECT_BINDING_PRIMARY_DIRECT_DIFFUSE_LOBE_RADIANCE_INPUT_OUTPUT, rtOutput.m_primaryDirectDiffuseRadiance.view(Resources::AccessType::ReadWrite), nullptr);
    ctx->bindResourceView(INTEGRATE_INDIRECT_BINDING_PRIMARY_DIRECT_SPECULAR_LOBE_RADIANCE_INPUT_OUTPUT, rtOutput.m_primaryDirectSpecularRadiance.view(Resources::AccessType::ReadWrite), nullptr);
    ctx->bindResourceView(INTEGRATE_INDIRECT_BINDING_SECONDARY_COMBINED_DIFFUSE_LOBE_RADIANCE_INPUT_OUTPUT, rtOutput.m_secondaryCombinedDiffuseRadiance.view(Resources::AccessType::ReadWrite), nullptr);
    ctx->bindResourceView(INTEGRATE_INDIRECT_BINDING_SECONDARY_COMBINED_SPECULAR_LOBE_RADIANCE_INPUT_OUTPUT, rtOutput.m_secondaryCombinedSpecularRadiance.view(Resources::AccessType::ReadWrite), nullptr);

    // Output resources

    ctx->bindResourceBuffer(INTEGRATE_INDIRECT_BINDING_NEE_CACHE, DxvkBufferSlice(rtOutput.m_neeCache, 0, rtOutput.m_neeCache->info().size));
    ctx->bindResourceBuffer(INTEGRATE_INDIRECT_BINDING_NEE_CACHE_SAMPLE, DxvkBufferSlice(rtOutput.m_neeCacheSample, 0, rtOutput.m_neeCacheSample->info().size));
    ctx->bindResourceBuffer(INTEGRATE_INDIRECT_BINDING_PRIMITIVE_ID_PREFIX_SUM, DxvkBufferSlice(primitiveIDPrefixSumBuffer, 0, primitiveIDPrefixSumBuffer->info().size));
    ctx->bindResourceBuffer(INTEGRATE_INDIRECT_BINDING_NEE_CACHE_TASK, DxvkBufferSlice(rtOutput.m_neeCacheTask, 0, rtOutput.m_neeCacheTask->info().size));
    ctx->bindResourceView(INTEGRATE_INDIRECT_BINDING_NEE_CACHE_THREAD_TASK, rtOutput.m_neeCacheThreadTask.view, nullptr);

    // Aliased resources
    // m_indirectRadiance writes the actual output carried forward and therefore it must be bound with write access last
    ctx->bindResourceView(INTEGRATE_INDIRECT_BINDING_THROUGHPUT_CONE_RADIUS_INPUT, rtOutput.m_indirectThroughputConeRadius.view(Resources::AccessType::Read), nullptr);

    // Bind necessary resources for Neural Radiance Cache
    NeuralRadianceCache& nrc = ctx->getCommonObjects()->metaNeuralRadianceCache();
    nrc.bindIntegrateIndirectPathTracingResources(*ctx);

    // Bind necessary resources for ReSTIR GI
    DxvkReSTIRGIRayQuery& reSTIRGI = ctx->getCommonObjects()->metaReSTIRGIRayQuery();
    reSTIRGI.bindIntegrateIndirectPathTracingResources(*ctx);

    ctx->bindResourceView(INTEGRATE_INDIRECT_BINDING_INDIRECT_RADIANCE_HIT_DISTANCE_OUTPUT, rtOutput.m_indirectRadianceHitDistance.view(Resources::AccessType::Write), nullptr);
    
    DebugView& debugView = ctx->getDevice()->getCommon()->metaDebugView();
    ctx->bindResourceView(INTEGRATE_INSTRUMENTATION, debugView.getInstrumentation(), nullptr);

    const bool nrcEnabled = nrc.isActive();

    const VkExtent3D& rayDims = nrcEnabled
      ? nrc.calcRaytracingResolution()
      : rtOutput.m_compositeOutputExtent;

    const bool serEnabled = RtxOptions::isShaderExecutionReorderingInPathtracerIntegrateIndirectEnabled();
    const bool ommEnabled = RtxOptions::getEnableOpacityMicromap();
    const bool includePortals = RtxOptions::rayPortalModelTextureHashes().size() > 0 || rtOutput.m_raytraceArgs.numActiveRayPortals > 0;
    const bool pomEnabled = rtOutput.m_raytraceArgs.pomMode != DisplacementMode::Off && RtxOptions::Displacement::enableIndirectHit();
    const bool neeCacheEnabled = NeeCachePass::enable();
    const bool wboitEnabled = RtxOptions::wboitEnabled();

    // Trace indirect ray
    {
      ScopedGpuProfileZone(ctx, "Integrate Indirect Raytracing");
      const NeeCachePass& neeCache = ctx->getCommonObjects()->metaNeeCache();
      switch (RtxOptions::renderPassIntegrateIndirectRaytraceMode()) {
      case RaytraceMode::RayQuery:
        VkExtent3D workgroups = util::computeBlockCount(rayDims, VkExtent3D { 16, 8, 1 });
        ctx->bindShader(VK_SHADER_STAGE_COMPUTE_BIT, getComputeShader(neeCacheEnabled, nrcEnabled, wboitEnabled));
        ctx->dispatch(workgroups.width, workgroups.height, workgroups.depth);
        break;
      case RaytraceMode::RayQueryRayGen:
        ctx->bindRaytracingPipelineShaders(getPipelineShaders(true, serEnabled, ommEnabled, neeCacheEnabled, includePortals, pomEnabled, nrcEnabled, wboitEnabled));
        ctx->traceRays(rayDims.width, rayDims.height, rayDims.depth);
        break;
      case RaytraceMode::TraceRay:
        ctx->bindRaytracingPipelineShaders(getPipelineShaders(false, serEnabled, ommEnabled, neeCacheEnabled, includePortals, pomEnabled, nrcEnabled, wboitEnabled));
        ctx->traceRays(rayDims.width, rayDims.height, rayDims.depth);
        break;
      case RaytraceMode::Count:
        assert(false && "Invalid RaytraceMode in DxvkPathtracerIntegrateIndirect::dispatch");
        break;
      }
    }
  }

  void DxvkPathtracerIntegrateIndirect::dispatchNEE(RtxContext* ctx, const Resources::RaytracingOutput& rtOutput) {
    // Sample triangles in the NEE cache and perform NEE
    // Construct restir input sample
    const auto rayDims = rtOutput.m_compositeOutputExtent;
    VkExtent3D workgroups = util::computeBlockCount(rayDims, VkExtent3D { 16, 8, 1 });
    Rc<DxvkBuffer> primitiveIDPrefixSumBuffer = ctx->getSceneManager().getCurrentFramePrimitiveIDPrefixSumBuffer();
    NeuralRadianceCache& nrc = ctx->getCommonObjects()->metaNeuralRadianceCache();

    ScopedGpuProfileZone(ctx, "Integrate NEE");
    ctx->setFramePassStage(RtxFramePassStage::NEE_Integration);
    ctx->bindCommonRayTracingResources(rtOutput);

    // Inputs

    ctx->bindResourceView(INTEGRATE_NEE_BINDING_SHARED_FLAGS_INPUT, rtOutput.m_sharedFlags.view, nullptr);
    ctx->bindResourceView(INTEGRATE_NEE_BINDING_SHARED_MATERIAL_DATA0_INPUT, rtOutput.m_sharedMaterialData0.view, nullptr);
    ctx->bindResourceView(INTEGRATE_NEE_BINDING_SHARED_MATERIAL_DATA1_INPUT, rtOutput.m_sharedMaterialData1.view, nullptr);
    ctx->bindResourceView(INTEGRATE_NEE_BINDING_SHARED_TEXTURE_COORD_INPUT, rtOutput.m_sharedTextureCoord.view, nullptr);
    ctx->bindResourceView(INTEGRATE_NEE_BINDING_SHARED_SURFACE_INDEX_INPUT, rtOutput.m_sharedSurfaceIndex.view(Resources::AccessType::Read), nullptr);
    ctx->bindResourceView(INTEGRATE_NEE_BINDING_SHARED_SUBSURFACE_DATA_INPUT, rtOutput.m_sharedSubsurfaceData.view, nullptr);
    ctx->bindResourceView(INTEGRATE_NEE_BINDING_SHARED_SUBSURFACE_DIFFUSION_PROFILE_DATA_INPUT, rtOutput.m_sharedSubsurfaceDiffusionProfileData.view, nullptr);

    ctx->bindResourceView(INTEGRATE_NEE_BINDING_PRIMARY_WORLD_SHADING_NORMAL_INPUT, rtOutput.m_primaryWorldShadingNormal.view, nullptr);
    ctx->bindResourceView(INTEGRATE_NEE_BINDING_PRIMARY_WORLD_INTERPOLATED_NORMAL_INPUT, rtOutput.m_primaryWorldInterpolatedNormal.view, nullptr);
    ctx->bindResourceView(INTEGRATE_NEE_BINDING_PRIMARY_PERCEPTUAL_ROUGHNESS_INPUT, rtOutput.m_primaryPerceptualRoughness.view, nullptr);
    ctx->bindResourceView(INTEGRATE_NEE_BINDING_PRIMARY_HIT_DISTANCE_INPUT, rtOutput.m_primaryHitDistance.view, nullptr);
    ctx->bindResourceView(INTEGRATE_NEE_BINDING_PRIMARY_ALBEDO_INPUT, rtOutput.m_primaryAlbedo.view, nullptr);
    ctx->bindResourceView(INTEGRATE_NEE_BINDING_PRIMARY_VIEW_DIRECTION_INPUT, rtOutput.m_primaryViewDirection.view, nullptr);
    ctx->bindResourceView(INTEGRATE_NEE_BINDING_PRIMARY_CONE_RADIUS_INPUT, rtOutput.m_primaryConeRadius.view, nullptr);
    ctx->bindResourceView(INTEGRATE_NEE_BINDING_PRIMARY_WORLD_POSITION_INPUT, rtOutput.getCurrentPrimaryWorldPositionWorldTriangleNormal().view(Resources::AccessType::Read), nullptr);
    ctx->bindResourceView(INTEGRATE_NEE_BINDING_PRIMARY_POSITION_ERROR_INPUT, rtOutput.m_primaryPositionError.view, nullptr);
    ctx->bindResourceView(INTEGRATE_NEE_BINDING_INDIRECT_RADIANCE_HIT_DISTANCE_INPUT, rtOutput.m_indirectRadianceHitDistance.view(Resources::AccessType::Read), nullptr);
    ctx->bindResourceBuffer(INTEGRATE_NEE_BINDING_PRIMITIVE_ID_PREFIX_SUM_INPUT, DxvkBufferSlice(primitiveIDPrefixSumBuffer, 0, primitiveIDPrefixSumBuffer->info().size));

    // Inputs / Outputs

    ctx->bindResourceView(INTEGRATE_NEE_BINDING_PRIMARY_BASE_REFLECTIVITY_INPUT_OUTPUT, rtOutput.m_primaryBaseReflectivity.view(Resources::AccessType::ReadWrite), nullptr);
    ctx->bindResourceBuffer(INTEGRATE_NEE_BINDING_NRC_TRAINING_PATH_VERTICES_INPUT_OUTPUT, nrc.getBufferSlice(*ctx, NeuralRadianceCache::ResourceType::TrainingPathVertices));

    // Outputs

    ctx->bindResourceView(INTEGRATE_NEE_BINDING_PRIMARY_INDIRECT_DIFFUSE_RADIANCE_HIT_DISTANCE_OUTPUT, rtOutput.m_primaryIndirectDiffuseRadiance.view(Resources::AccessType::Write), nullptr);
    ctx->bindResourceView(INTEGRATE_NEE_BINDING_PRIMARY_INDIRECT_SPECULAR_RADIANCE_HIT_DISTANCE_OUTPUT, rtOutput.m_primaryIndirectSpecularRadiance.view(Resources::AccessType::Write), nullptr);

    const DxvkReSTIRGIRayQuery& restirGI = ctx->getCommonObjects()->metaReSTIRGIRayQuery();
    ctx->bindResourceView(INTEGRATE_NEE_BINDING_BSDF_FACTOR2_OUTPUT, restirGI.getBsdfFactor2().view, nullptr);

    ctx->bindResourceBuffer(INTEGRATE_NEE_BINDING_NEE_CACHE, DxvkBufferSlice(rtOutput.m_neeCache, 0, rtOutput.m_neeCache->info().size));
    ctx->bindResourceBuffer(INTEGRATE_NEE_BINDING_NEE_CACHE_TASK, DxvkBufferSlice(rtOutput.m_neeCacheTask, 0, rtOutput.m_neeCacheTask->info().size));
    ctx->bindResourceBuffer(INTEGRATE_NEE_BINDING_NEE_CACHE_SAMPLE, DxvkBufferSlice(rtOutput.m_neeCacheSample, 0, rtOutput.m_neeCacheSample->info().size));
    ctx->bindResourceView(INTEGRATE_NEE_BINDING_NEE_CACHE_THREAD_TASK, rtOutput.m_neeCacheThreadTask.view, nullptr);

    // Bind necessary resources for ReSTIR GI
    DxvkReSTIRGIRayQuery& reSTIRGI = ctx->getCommonObjects()->metaReSTIRGIRayQuery();
    reSTIRGI.bindIntegrateIndirectNeeResources(*ctx);

    ctx->bindShader(VK_SHADER_STAGE_COMPUTE_BIT, IntegrateNEEShader::getShader());
    ctx->dispatch(workgroups.width, workgroups.height, workgroups.depth);

    // Visualize the nee cache when debug view is chosen.
    uint32_t debugViewIndex = ctx->getCommonObjects()->metaDebugView().debugViewIdx();
    if (debugViewIndex == DEBUG_VIEW_NEE_CACHE_LIGHT_HISTOGRAM || debugViewIndex == DEBUG_VIEW_NEE_CACHE_HISTOGRAM ||
     debugViewIndex == DEBUG_VIEW_NEE_CACHE_ACCUMULATE_MAP || debugViewIndex == DEBUG_VIEW_NEE_CACHE_HASH_MAP || debugViewIndex == DEBUG_VIEW_NEE_CACHE_TRIANGLE_CANDIDATE)
    {
      VisualizeNeeArgs args;
      auto mousePos = ImGui::GetMousePos();
      const VkExtent3D& finalResolution = rtOutput.m_finalOutputExtent;
      args.mouseUV = vec2(mousePos.x / finalResolution.width, mousePos.y / finalResolution.height);
      args.mouseUV.x = std::clamp(args.mouseUV.x, 0.0f, 1.0f);
      args.mouseUV.y = std::clamp(args.mouseUV.y, 0.0f, 1.0f);
      ctx->pushConstants(0, sizeof(args), &args);
      ctx->bindShader(VK_SHADER_STAGE_COMPUTE_BIT, VisualizeNEEShader::getShader());
      ctx->dispatch(workgroups.width, workgroups.height, workgroups.depth);
    }
  }

  DxvkRaytracingPipelineShaders DxvkPathtracerIntegrateIndirect::getPipelineShaders(
    const bool useRayQuery,
    const bool serEnabled,
    const bool ommEnabled,
    const bool useNeeCache,
    const bool includePortals,
    const bool pomEnabled,
    const bool nrcEnabled,
    const bool wboitEnabled) {

    DxvkRaytracingPipelineShaders shaders;
    if (wboitEnabled) {
      if (useRayQuery) {
        if (nrcEnabled) {
          if (useNeeCache) {
            shaders.addGeneralShader(GET_SHADER_VARIANT(VK_SHADER_STAGE_RAYGEN_BIT_KHR, IntegrateIndirectRayGenShader, integrate_indirect_rayquery_raygen_nrc_neeCache_wboit));
          } else {
            shaders.addGeneralShader(GET_SHADER_VARIANT(VK_SHADER_STAGE_RAYGEN_BIT_KHR, IntegrateIndirectRayGenShader, integrate_indirect_rayquery_raygen_nrc_wboit));
          }
        } else {
          if (useNeeCache) {
            shaders.addGeneralShader(GET_SHADER_VARIANT(VK_SHADER_STAGE_RAYGEN_BIT_KHR, IntegrateIndirectRayGenShader, integrate_indirect_rayquery_raygen_neeCache_wboit));
          } else {
            shaders.addGeneralShader(GET_SHADER_VARIANT(VK_SHADER_STAGE_RAYGEN_BIT_KHR, IntegrateIndirectRayGenShader, integrate_indirect_rayquery_raygen_wboit));
          }
        }
        shaders.debugName = "Integrate Indirect RayQuery (RGS)";
      } else {
        if (nrcEnabled) {
          if (serEnabled) {
            if (useNeeCache) {
              shaders.addGeneralShader(GET_SHADER_VARIANT(VK_SHADER_STAGE_RAYGEN_BIT_KHR, IntegrateIndirectRayGenShader, integrate_indirect_raygen_ser_nrc_neeCache_wboit));
            } else {
              shaders.addGeneralShader(GET_SHADER_VARIANT(VK_SHADER_STAGE_RAYGEN_BIT_KHR, IntegrateIndirectRayGenShader, integrate_indirect_raygen_ser_nrc_wboit));
            }
          } else {
            if (useNeeCache) {
              shaders.addGeneralShader(GET_SHADER_VARIANT(VK_SHADER_STAGE_RAYGEN_BIT_KHR, IntegrateIndirectRayGenShader, integrate_indirect_raygen_nrc_neeCache_wboit));
            } else {
              shaders.addGeneralShader(GET_SHADER_VARIANT(VK_SHADER_STAGE_RAYGEN_BIT_KHR, IntegrateIndirectRayGenShader, integrate_indirect_raygen_nrc_wboit));
            }
          }
        } else {
          if (serEnabled) {
            if (useNeeCache) {
              shaders.addGeneralShader(GET_SHADER_VARIANT(VK_SHADER_STAGE_RAYGEN_BIT_KHR, IntegrateIndirectRayGenShader, integrate_indirect_raygen_ser_neeCache_wboit));
            } else {
              shaders.addGeneralShader(GET_SHADER_VARIANT(VK_SHADER_STAGE_RAYGEN_BIT_KHR, IntegrateIndirectRayGenShader, integrate_indirect_raygen_ser_wboit));
            }
          } else {
            if (useNeeCache) {
              shaders.addGeneralShader(GET_SHADER_VARIANT(VK_SHADER_STAGE_RAYGEN_BIT_KHR, IntegrateIndirectRayGenShader, integrate_indirect_raygen_neeCache_wboit));
            } else {
              shaders.addGeneralShader(GET_SHADER_VARIANT(VK_SHADER_STAGE_RAYGEN_BIT_KHR, IntegrateIndirectRayGenShader, integrate_indirect_raygen_wboit));
            }
          }
        }

        if (nrcEnabled) {
          if (useNeeCache) {
            shaders.addGeneralShader(GET_SHADER_VARIANT(VK_SHADER_STAGE_MISS_BIT_KHR, IntegrateIndirectMissShader, integrate_indirect_miss_nrc_neeCache_wboit));
          } else {
            shaders.addGeneralShader(GET_SHADER_VARIANT(VK_SHADER_STAGE_MISS_BIT_KHR, IntegrateIndirectMissShader, integrate_indirect_miss_nrc_wboit));
          }
        } else {
          if (useNeeCache) {
            shaders.addGeneralShader(GET_SHADER_VARIANT(VK_SHADER_STAGE_MISS_BIT_KHR, IntegrateIndirectMissShader, integrate_indirect_miss_neeCache_wboit));
          } else {
            shaders.addGeneralShader(GET_SHADER_VARIANT(VK_SHADER_STAGE_MISS_BIT_KHR, IntegrateIndirectMissShader, integrate_indirect_miss_wboit));
          }
        }

        if (nrcEnabled) {
          if (useNeeCache) {
            if (pomEnabled) {
              if (includePortals) {
                shaders.addHitGroup(GET_SHADER_VARIANT(VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, IntegrateIndirectClosestHitShader, integrate_indirect_nrc_neeCache_pom_material_rayportal_closestHit_wboit), nullptr, nullptr);
              } else {
                shaders.addHitGroup(GET_SHADER_VARIANT(VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, IntegrateIndirectClosestHitShader, integrate_indirect_nrc_neeCache_pom_material_opaque_translucent_closestHit_wboit), nullptr, nullptr);
              }
            } else {
              if (includePortals) {
                shaders.addHitGroup(GET_SHADER_VARIANT(VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, IntegrateIndirectClosestHitShader, integrate_indirect_nrc_neeCache_material_rayportal_closestHit_wboit), nullptr, nullptr);
              } else {
                shaders.addHitGroup(GET_SHADER_VARIANT(VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, IntegrateIndirectClosestHitShader, integrate_indirect_nrc_neeCache_material_opaque_translucent_closestHit_wboit), nullptr, nullptr);
              }
            }
          } else {
            if (pomEnabled) {
              if (includePortals) {
                shaders.addHitGroup(GET_SHADER_VARIANT(VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, IntegrateIndirectClosestHitShader, integrate_indirect_nrc_pom_material_rayportal_closestHit_wboit), nullptr, nullptr);
              } else {
                shaders.addHitGroup(GET_SHADER_VARIANT(VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, IntegrateIndirectClosestHitShader, integrate_indirect_nrc_pom_material_opaque_translucent_closestHit_wboit), nullptr, nullptr);
              }
            } else {
              if (includePortals) {
                shaders.addHitGroup(GET_SHADER_VARIANT(VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, IntegrateIndirectClosestHitShader, integrate_indirect_nrc_material_rayportal_closestHit_wboit), nullptr, nullptr);
              } else {
                shaders.addHitGroup(GET_SHADER_VARIANT(VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, IntegrateIndirectClosestHitShader, integrate_indirect_nrc_material_opaque_translucent_closestHit_wboit), nullptr, nullptr);
              }
            }
          }
        } else {
          if (useNeeCache) {
            if (pomEnabled) {
              if (includePortals) {
                shaders.addHitGroup(GET_SHADER_VARIANT(VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, IntegrateIndirectClosestHitShader, integrate_indirect_neeCache_pom_material_rayportal_closestHit_wboit), nullptr, nullptr);
              } else {
                shaders.addHitGroup(GET_SHADER_VARIANT(VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, IntegrateIndirectClosestHitShader, integrate_indirect_neeCache_pom_material_opaque_translucent_closestHit_wboit), nullptr, nullptr);
              }
            } else {
              if (includePortals) {
                shaders.addHitGroup(GET_SHADER_VARIANT(VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, IntegrateIndirectClosestHitShader, integrate_indirect_neeCache_material_rayportal_closestHit_wboit), nullptr, nullptr);
              } else {
                shaders.addHitGroup(GET_SHADER_VARIANT(VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, IntegrateIndirectClosestHitShader, integrate_indirect_neeCache_material_opaque_translucent_closestHit_wboit), nullptr, nullptr);
              }
            }
          } else {
            if (pomEnabled) {
              if (includePortals) {
                shaders.addHitGroup(GET_SHADER_VARIANT(VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, IntegrateIndirectClosestHitShader, integrate_indirect_pom_material_rayportal_closestHit_wboit), nullptr, nullptr);
              } else {
                shaders.addHitGroup(GET_SHADER_VARIANT(VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, IntegrateIndirectClosestHitShader, integrate_indirect_pom_material_opaque_translucent_closestHit_wboit), nullptr, nullptr);
              }
            } else {
              if (includePortals) {
                shaders.addHitGroup(GET_SHADER_VARIANT(VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, IntegrateIndirectClosestHitShader, integrate_indirect_material_rayportal_closestHit_wboit), nullptr, nullptr);
              } else {
                shaders.addHitGroup(GET_SHADER_VARIANT(VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, IntegrateIndirectClosestHitShader, integrate_indirect_material_opaque_translucent_closestHit_wboit), nullptr, nullptr);
              }
            }
          }
        }
      }
      shaders.debugName = "Integrate Indirect TraceRay (RGS)";
    } else {
      if (useRayQuery) {
        if (nrcEnabled) {
          if (useNeeCache) {
            shaders.addGeneralShader(GET_SHADER_VARIANT(VK_SHADER_STAGE_RAYGEN_BIT_KHR, IntegrateIndirectRayGenShader, integrate_indirect_rayquery_raygen_nrc_neeCache));
          } else {
            shaders.addGeneralShader(GET_SHADER_VARIANT(VK_SHADER_STAGE_RAYGEN_BIT_KHR, IntegrateIndirectRayGenShader, integrate_indirect_rayquery_raygen_nrc));
          }
        } else {
          if (useNeeCache) {
            shaders.addGeneralShader(GET_SHADER_VARIANT(VK_SHADER_STAGE_RAYGEN_BIT_KHR, IntegrateIndirectRayGenShader, integrate_indirect_rayquery_raygen_neeCache));
          } else {
            shaders.addGeneralShader(GET_SHADER_VARIANT(VK_SHADER_STAGE_RAYGEN_BIT_KHR, IntegrateIndirectRayGenShader, integrate_indirect_rayquery_raygen));
          }
        }
        shaders.debugName = "Integrate Indirect RayQuery (RGS)";
      } else {
        if (nrcEnabled) {
          if (serEnabled) {
            if (useNeeCache) {
              shaders.addGeneralShader(GET_SHADER_VARIANT(VK_SHADER_STAGE_RAYGEN_BIT_KHR, IntegrateIndirectRayGenShader, integrate_indirect_raygen_ser_nrc_neeCache));
            } else {
              shaders.addGeneralShader(GET_SHADER_VARIANT(VK_SHADER_STAGE_RAYGEN_BIT_KHR, IntegrateIndirectRayGenShader, integrate_indirect_raygen_ser_nrc));
            }
          } else {
            if (useNeeCache) {
              shaders.addGeneralShader(GET_SHADER_VARIANT(VK_SHADER_STAGE_RAYGEN_BIT_KHR, IntegrateIndirectRayGenShader, integrate_indirect_raygen_nrc_neeCache));
            } else {
              shaders.addGeneralShader(GET_SHADER_VARIANT(VK_SHADER_STAGE_RAYGEN_BIT_KHR, IntegrateIndirectRayGenShader, integrate_indirect_raygen_nrc));
            }
          }
        } else {
          if (serEnabled) {
            if (useNeeCache) {
              shaders.addGeneralShader(GET_SHADER_VARIANT(VK_SHADER_STAGE_RAYGEN_BIT_KHR, IntegrateIndirectRayGenShader, integrate_indirect_raygen_ser_neeCache));
            } else {
              shaders.addGeneralShader(GET_SHADER_VARIANT(VK_SHADER_STAGE_RAYGEN_BIT_KHR, IntegrateIndirectRayGenShader, integrate_indirect_raygen_ser));
            }
          } else {
            if (useNeeCache) {
              shaders.addGeneralShader(GET_SHADER_VARIANT(VK_SHADER_STAGE_RAYGEN_BIT_KHR, IntegrateIndirectRayGenShader, integrate_indirect_raygen_neeCache));
            } else {
              shaders.addGeneralShader(GET_SHADER_VARIANT(VK_SHADER_STAGE_RAYGEN_BIT_KHR, IntegrateIndirectRayGenShader, integrate_indirect_raygen));
            }
          }
        }

        if (nrcEnabled) {
          if (useNeeCache) {
            shaders.addGeneralShader(GET_SHADER_VARIANT(VK_SHADER_STAGE_MISS_BIT_KHR, IntegrateIndirectMissShader, integrate_indirect_miss_nrc_neeCache));
          } else {
            shaders.addGeneralShader(GET_SHADER_VARIANT(VK_SHADER_STAGE_MISS_BIT_KHR, IntegrateIndirectMissShader, integrate_indirect_miss_nrc));
          }
        } else {
          if (useNeeCache) {
            shaders.addGeneralShader(GET_SHADER_VARIANT(VK_SHADER_STAGE_MISS_BIT_KHR, IntegrateIndirectMissShader, integrate_indirect_miss_neeCache));
          } else {
            shaders.addGeneralShader(GET_SHADER_VARIANT(VK_SHADER_STAGE_MISS_BIT_KHR, IntegrateIndirectMissShader, integrate_indirect_miss));
          }
        }

        if (nrcEnabled) {
          if (useNeeCache) {
            if (pomEnabled) {
              if (includePortals) {
                shaders.addHitGroup(GET_SHADER_VARIANT(VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, IntegrateIndirectClosestHitShader, integrate_indirect_nrc_neeCache_pom_material_rayportal_closestHit), nullptr, nullptr);
              } else {
                shaders.addHitGroup(GET_SHADER_VARIANT(VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, IntegrateIndirectClosestHitShader, integrate_indirect_nrc_neeCache_pom_material_opaque_translucent_closestHit), nullptr, nullptr);
              }
            } else {
              if (includePortals) {
                shaders.addHitGroup(GET_SHADER_VARIANT(VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, IntegrateIndirectClosestHitShader, integrate_indirect_nrc_neeCache_material_rayportal_closestHit), nullptr, nullptr);
              } else {
                shaders.addHitGroup(GET_SHADER_VARIANT(VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, IntegrateIndirectClosestHitShader, integrate_indirect_nrc_neeCache_material_opaque_translucent_closestHit), nullptr, nullptr);
              }
            }
          } else {
            if (pomEnabled) {
              if (includePortals) {
                shaders.addHitGroup(GET_SHADER_VARIANT(VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, IntegrateIndirectClosestHitShader, integrate_indirect_nrc_pom_material_rayportal_closestHit), nullptr, nullptr);
              } else {
                shaders.addHitGroup(GET_SHADER_VARIANT(VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, IntegrateIndirectClosestHitShader, integrate_indirect_nrc_pom_material_opaque_translucent_closestHit), nullptr, nullptr);
              }
            } else {
              if (includePortals) {
                shaders.addHitGroup(GET_SHADER_VARIANT(VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, IntegrateIndirectClosestHitShader, integrate_indirect_nrc_material_rayportal_closestHit), nullptr, nullptr);
              } else {
                shaders.addHitGroup(GET_SHADER_VARIANT(VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, IntegrateIndirectClosestHitShader, integrate_indirect_nrc_material_opaque_translucent_closestHit), nullptr, nullptr);
              }
            }
          }
        } else {
          if (useNeeCache) {
            if (pomEnabled) {
              if (includePortals) {
                shaders.addHitGroup(GET_SHADER_VARIANT(VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, IntegrateIndirectClosestHitShader, integrate_indirect_neeCache_pom_material_rayportal_closestHit), nullptr, nullptr);
              } else {
                shaders.addHitGroup(GET_SHADER_VARIANT(VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, IntegrateIndirectClosestHitShader, integrate_indirect_neeCache_pom_material_opaque_translucent_closestHit), nullptr, nullptr);
              }
            } else {
              if (includePortals) {
                shaders.addHitGroup(GET_SHADER_VARIANT(VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, IntegrateIndirectClosestHitShader, integrate_indirect_neeCache_material_rayportal_closestHit), nullptr, nullptr);
              } else {
                shaders.addHitGroup(GET_SHADER_VARIANT(VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, IntegrateIndirectClosestHitShader, integrate_indirect_neeCache_material_opaque_translucent_closestHit), nullptr, nullptr);
              }
            }
          } else {
            if (pomEnabled) {
              if (includePortals) {
                shaders.addHitGroup(GET_SHADER_VARIANT(VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, IntegrateIndirectClosestHitShader, integrate_indirect_pom_material_rayportal_closestHit), nullptr, nullptr);
              } else {
                shaders.addHitGroup(GET_SHADER_VARIANT(VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, IntegrateIndirectClosestHitShader, integrate_indirect_pom_material_opaque_translucent_closestHit), nullptr, nullptr);
              }
            } else {
              if (includePortals) {
                shaders.addHitGroup(GET_SHADER_VARIANT(VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, IntegrateIndirectClosestHitShader, integrate_indirect_material_rayportal_closestHit), nullptr, nullptr);
              } else {
                shaders.addHitGroup(GET_SHADER_VARIANT(VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, IntegrateIndirectClosestHitShader, integrate_indirect_material_opaque_translucent_closestHit), nullptr, nullptr);
              }
            }
          }
        }

        shaders.debugName = "Integrate Indirect TraceRay (RGS)";
      }
    }

    if (ommEnabled)
      shaders.pipelineFlags |= VK_PIPELINE_CREATE_RAY_TRACING_OPACITY_MICROMAP_BIT_EXT;

    return shaders;
  }

  Rc<DxvkShader> DxvkPathtracerIntegrateIndirect::getComputeShader(const bool useNeeCache, const bool nrcEnabled, const bool wboitEnabled) const {
    if (wboitEnabled) {
      if (nrcEnabled) {
        if (useNeeCache) {
          return GET_SHADER_VARIANT(VK_SHADER_STAGE_COMPUTE_BIT, IntegrateIndirectRayGenShader, integrate_indirect_rayquery_nrc_neeCache_wboit);
        } else {
          return GET_SHADER_VARIANT(VK_SHADER_STAGE_COMPUTE_BIT, IntegrateIndirectRayGenShader, integrate_indirect_rayquery_nrc_wboit);
        }
      } else {
        if (useNeeCache) {
          return GET_SHADER_VARIANT(VK_SHADER_STAGE_COMPUTE_BIT, IntegrateIndirectRayGenShader, integrate_indirect_rayquery_neeCache_wboit);
        } else {
          return GET_SHADER_VARIANT(VK_SHADER_STAGE_COMPUTE_BIT, IntegrateIndirectRayGenShader, integrate_indirect_rayquery_wboit);
        }
      }
    } else {
      if (nrcEnabled) {
        if (useNeeCache) {
          return GET_SHADER_VARIANT(VK_SHADER_STAGE_COMPUTE_BIT, IntegrateIndirectRayGenShader, integrate_indirect_rayquery_nrc_neeCache);
        } else {
          return GET_SHADER_VARIANT(VK_SHADER_STAGE_COMPUTE_BIT, IntegrateIndirectRayGenShader, integrate_indirect_rayquery_nrc);
        }
      } else {
        if (useNeeCache) {
          return GET_SHADER_VARIANT(VK_SHADER_STAGE_COMPUTE_BIT, IntegrateIndirectRayGenShader, integrate_indirect_rayquery_neeCache);
        } else {
          return GET_SHADER_VARIANT(VK_SHADER_STAGE_COMPUTE_BIT, IntegrateIndirectRayGenShader, integrate_indirect_rayquery);
        }
      }
    }
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
