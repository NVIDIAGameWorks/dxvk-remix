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
#include "rtx/pass/integrate/integrate_nee_binding_indices.h"
#include "rtx/concept/surface_material/surface_material_hitgroup.h"

#include <rtx_shaders/integrate_indirect_raygen.h>
#include <rtx_shaders/integrate_indirect_raygen_ser.h>
#include <rtx_shaders/integrate_indirect_rayquery.h>
#include <rtx_shaders/integrate_indirect_rayquery_neeCache.h>
#include <rtx_shaders/integrate_indirect_rayquery_raygen.h>
#include <rtx_shaders/integrate_indirect_rayquery_raygen_neeCache.h>
#include <rtx_shaders/integrate_indirect_material_opaque_translucent_closestHit.h>
#include <rtx_shaders/integrate_indirect_material_rayPortal_closestHit.h>
#include <rtx_shaders/integrate_indirect_pom_material_opaque_translucent_closestHit.h>
#include <rtx_shaders/integrate_indirect_pom_material_rayPortal_closestHit.h>
#include <rtx_shaders/integrate_indirect_raygen_neeCache.h>
#include <rtx_shaders/integrate_indirect_raygen_ser_neeCache.h>
#include <rtx_shaders/integrate_indirect_neeCache_material_rayportal_closestHit.h>
#include <rtx_shaders/integrate_indirect_neeCache_material_opaque_translucent_closestHit.h>
#include <rtx_shaders/integrate_indirect_neeCache_pom_material_rayportal_closestHit.h>
#include <rtx_shaders/integrate_indirect_neeCache_pom_material_opaque_translucent_closestHit.h>
#include <rtx_shaders/integrate_indirect_miss.h>
#include <rtx_shaders/integrate_indirect_miss_neeCache.h>
#include <rtx_shaders/integrate_nee.h>
#include <rtx_shaders/visualize_nee.h>

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

        SAMPLER(INTEGRATE_BINDING_LINEAR_WRAP_SAMPLER)

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

        STRUCTURED_BUFFER(INTEGRATE_INDIRECT_BINDING_NEE_CACHE)
        STRUCTURED_BUFFER(INTEGRATE_INDIRECT_BINDING_NEE_CACHE_SAMPLE)
        STRUCTURED_BUFFER(INTEGRATE_INDIRECT_BINDING_PRIMITIVE_ID_PREFIX_SUM)
        RW_STRUCTURED_BUFFER(INTEGRATE_INDIRECT_BINDING_NEE_CACHE_TASK)
        RW_TEXTURE2D(INTEGRATE_INDIRECT_BINDING_NEE_CACHE_THREAD_TASK)

        TEXTURE2DARRAY(INTEGRATE_INDIRECT_BINDING_GRADIENTS_INPUT)

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
        TEXTURE2D(INTEGRATE_NEE_BINDING_HIT_GEOMETRY_INPUT)
        TEXTURE2D(INTEGRATE_NEE_BINDING_RADIANCE_INPUT)


        RW_TEXTURE2D(INTEGRATE_NEE_BINDING_PRIMARY_BASE_REFLECTIVITY_INPUT_OUTPUT)

        RW_TEXTURE2D(INTEGRATE_NEE_BINDING_PRIMARY_INDIRECT_DIFFUSE_RADIANCE_HIT_DISTANCE_OUTPUT)
        RW_TEXTURE2D(INTEGRATE_NEE_BINDING_PRIMARY_INDIRECT_SPECULAR_RADIANCE_HIT_DISTANCE_OUTPUT)
        STRUCTURED_BUFFER(INTEGRATE_NEE_BINDING_RESTIR_GI_RESERVOIR_OUTPUT)
        RW_TEXTURE2D(INTEGRATE_NEE_BINDING_BSDF_FACTOR2_OUTPUT)

        RW_STRUCTURED_BUFFER(INTEGRATE_NEE_BINDING_NEE_CACHE)
        RW_STRUCTURED_BUFFER(INTEGRATE_NEE_BINDING_NEE_CACHE_TASK)
        RW_STRUCTURED_BUFFER(INTEGRATE_NEE_BINDING_NEE_CACHE_SAMPLE)
        RW_TEXTURE2D(INTEGRATE_NEE_BINDING_NEE_CACHE_THREAD_TASK)
        STRUCTURED_BUFFER(INTEGRATE_NEE_BINDING_PRIMITIVE_ID_PREFIX_SUM)
      END_PARAMETER()
    };

    PREWARM_SHADER_PIPELINE(IntegrateNEEShader);

    class VisualizeNEEShader : public ManagedShader {
      SHADER_SOURCE(VisualizeNEEShader, VK_SHADER_STAGE_COMPUTE_BIT, visualize_nee)
      
      BINDLESS_ENABLED()

      BEGIN_PARAMETER()
        COMMON_RAYTRACING_BINDINGS

        TEXTURE2D(INTEGRATE_NEE_BINDING_SHARED_FLAGS_INPUT)
        TEXTURE2D(INTEGRATE_NEE_BINDING_SHARED_MATERIAL_DATA0_INPUT)
        TEXTURE2D(INTEGRATE_NEE_BINDING_SHARED_MATERIAL_DATA1_INPUT)
        RW_TEXTURE2D(INTEGRATE_NEE_BINDING_SHARED_TEXTURE_COORD_INPUT)
        RW_TEXTURE2D(INTEGRATE_NEE_BINDING_SHARED_SURFACE_INDEX_INPUT)
        TEXTURE2D(INTEGRATE_NEE_BINDING_SHARED_SUBSURFACE_DATA_INPUT)

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
        TEXTURE2D(INTEGRATE_NEE_BINDING_HIT_GEOMETRY_INPUT)
        TEXTURE2D(INTEGRATE_NEE_BINDING_RADIANCE_INPUT)


        RW_TEXTURE2D(INTEGRATE_NEE_BINDING_PRIMARY_BASE_REFLECTIVITY_INPUT_OUTPUT)

        RW_TEXTURE2D(INTEGRATE_NEE_BINDING_PRIMARY_INDIRECT_DIFFUSE_RADIANCE_HIT_DISTANCE_OUTPUT)
        RW_TEXTURE2D(INTEGRATE_NEE_BINDING_PRIMARY_INDIRECT_SPECULAR_RADIANCE_HIT_DISTANCE_OUTPUT)
        STRUCTURED_BUFFER(INTEGRATE_NEE_BINDING_RESTIR_GI_RESERVOIR_OUTPUT)
        RW_TEXTURE2D(INTEGRATE_NEE_BINDING_BSDF_FACTOR2_OUTPUT)

        RW_STRUCTURED_BUFFER(INTEGRATE_NEE_BINDING_NEE_CACHE)
        RW_STRUCTURED_BUFFER(INTEGRATE_NEE_BINDING_NEE_CACHE_TASK)
        RW_STRUCTURED_BUFFER(INTEGRATE_NEE_BINDING_NEE_CACHE_SAMPLE)
        RW_TEXTURE2D(INTEGRATE_NEE_BINDING_NEE_CACHE_THREAD_TASK)
        STRUCTURED_BUFFER(INTEGRATE_NEE_BINDING_PRIMITIVE_ID_PREFIX_SUM)
      END_PARAMETER()
    };

    PREWARM_SHADER_PIPELINE(VisualizeNEEShader);
  }

  DxvkPathtracerIntegrateIndirect::DxvkPathtracerIntegrateIndirect(DxvkDevice* device) : CommonDeviceObject(device) {
  }

  void DxvkPathtracerIntegrateIndirect::prewarmShaders(DxvkPipelineManager& pipelineManager) const {

    const bool isOpacityMicromapSupported = OpacityMicromapManager::checkIsOpacityMicromapSupported(*m_device);
    const bool isShaderExecutionReorderingSupported = 
      RtxContext::checkIsShaderExecutionReorderingSupported(*m_device) &&
      RtxOptions::Get()->isShaderExecutionReorderingInPathtracerIntegrateIndirectEnabled();

    for (int32_t useNeeCache = 1; useNeeCache >= 0; useNeeCache--) {
      for (int32_t includesPortals = 1; includesPortals >= 0; includesPortals--) {
        for (int32_t useRayQuery = 1; useRayQuery >= 0; useRayQuery--) {
          for (int32_t serEnabled = isShaderExecutionReorderingSupported; serEnabled >= 0; serEnabled--) {
            for (int32_t ommEnabled = isOpacityMicromapSupported; ommEnabled >= 0; ommEnabled--) {
              for (int32_t pomEnable = 1; pomEnable >= 0; pomEnable--) {
                pipelineManager.registerRaytracingShaders(getPipelineShaders(useRayQuery, serEnabled, ommEnabled, useNeeCache, includesPortals, pomEnable));
              }
            }
          }
        }
      }

      DxvkComputePipelineShaders shaders;
      shaders.cs = getComputeShader(useNeeCache);
      pipelineManager.createComputePipeline(shaders);
    }
  }

  void DxvkPathtracerIntegrateIndirect::dispatch(RtxContext* ctx, const Resources::RaytracingOutput& rtOutput) {

    const uint32_t frameIdx = ctx->getDevice()->getCurrentFrameId();

    // Bind resources

    // Note: Clamp to edge used to avoid interpolation to black on the edges of the view.
    Rc<DxvkSampler> linearClampSampler = ctx->getResourceManager().getSampler(VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
    Rc<DxvkSampler> linearWrapSampler = ctx->getResourceManager().getSampler(VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_NEAREST, VK_SAMPLER_ADDRESS_MODE_REPEAT);
    Rc<DxvkBuffer> primitiveIDPrefixSumBuffer = ctx->getSceneManager().getCurrentFramePrimitiveIDPrefixSumBuffer();

    ctx->bindCommonRayTracingResources(rtOutput);

    ctx->bindResourceSampler(INTEGRATE_BINDING_LINEAR_WRAP_SAMPLER, linearWrapSampler);

    ctx->bindResourceView(INTEGRATE_INDIRECT_BINDING_SKYPROBE, ctx->getResourceManager().getSkyProbe(ctx).view, nullptr);
    ctx->bindResourceSampler(INTEGRATE_INDIRECT_BINDING_SKYPROBE, linearClampSampler);

    ctx->bindResourceView(INTEGRATE_INDIRECT_BINDING_SHARED_FLAGS_INPUT, rtOutput.m_sharedFlags.view, nullptr);
    ctx->bindResourceView(INTEGRATE_INDIRECT_BINDING_SHARED_MEDIUM_MATERIAL_INDEX_INPUT, rtOutput.m_sharedMediumMaterialIndex.view, nullptr);
    ctx->bindResourceView(INTEGRATE_INDIRECT_BINDING_SHARED_TEXTURE_COORD_INPUT, rtOutput.m_sharedTextureCoord.view, nullptr);
    ctx->bindResourceView(INTEGRATE_INDIRECT_BINDING_SHARED_SURFACE_INDEX_INPUT, rtOutput.m_sharedSurfaceIndex.view, nullptr);
    ctx->bindResourceView(INTEGRATE_INDIRECT_BINDING_SHARED_SUBSURFACE_DATA_INPUT, rtOutput.m_sharedSubsurfaceData.view, nullptr);

    ctx->bindResourceView(INTEGRATE_INDIRECT_BINDING_PRIMARY_CONE_RADIUS_INPUT, rtOutput.m_primaryConeRadius.view, nullptr);
    ctx->bindResourceView(INTEGRATE_INDIRECT_BINDING_SECONDARY_CONE_RADIUS_INPUT, rtOutput.m_secondaryConeRadius.view(Resources::AccessType::Read), nullptr);
    ctx->bindResourceView(INTEGRATE_INDIRECT_BINDING_PRIMARY_WORLD_POSITION_INPUT, rtOutput.getCurrentPrimaryWorldPositionWorldTriangleNormal().view, nullptr);
    ctx->bindResourceBuffer(INTEGRATE_INDIRECT_BINDING_PRIMARY_RTXDI_RESERVOIR, DxvkBufferSlice(rtOutput.m_rtxdiReservoirBuffer, 0, rtOutput.m_rtxdiReservoirBuffer->info().size));
    ctx->bindResourceView(INTEGRATE_INDIRECT_BINDING_RAY_ORIGIN_DIRECTION_INPUT, rtOutput.m_indirectRayOriginDirection.view(Resources::AccessType::Read), nullptr);
    ctx->bindResourceView(INTEGRATE_INDIRECT_BINDING_FIRST_HIT_PERCEPTUAL_ROUGHNESS_INPUT, rtOutput.m_indirectFirstHitPerceptualRoughness.view(Resources::AccessType::Read), nullptr);
    ctx->bindResourceView(INTEGRATE_INDIRECT_BINDING_LAST_GBUFFER_INPUT, rtOutput.m_gbufferLast.view, nullptr);
    ctx->bindResourceView(INTEGRATE_INDIRECT_BINDING_PREV_WORLD_POSITION_INPUT, rtOutput.getPreviousPrimaryWorldPositionWorldTriangleNormal().view, nullptr);

    ctx->bindResourceView(INTEGRATE_INDIRECT_BINDING_VOLUME_FILTERED_RADIANCE_INPUT, rtOutput.m_volumeFilteredRadiance.view, nullptr);
    ctx->bindResourceSampler(INTEGRATE_INDIRECT_BINDING_VOLUME_FILTERED_RADIANCE_INPUT, linearClampSampler);

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

    ctx->bindResourceBuffer(INTEGRATE_INDIRECT_BINDING_NEE_CACHE, DxvkBufferSlice(rtOutput.m_neeCache, 0, rtOutput.m_neeCache->info().size));
    ctx->bindResourceBuffer(INTEGRATE_INDIRECT_BINDING_NEE_CACHE_SAMPLE, DxvkBufferSlice(rtOutput.m_neeCacheSample, 0, rtOutput.m_neeCacheSample->info().size));
    ctx->bindResourceBuffer(INTEGRATE_INDIRECT_BINDING_PRIMITIVE_ID_PREFIX_SUM, DxvkBufferSlice(primitiveIDPrefixSumBuffer, 0, primitiveIDPrefixSumBuffer->info().size));
    ctx->bindResourceBuffer(INTEGRATE_INDIRECT_BINDING_NEE_CACHE_TASK, DxvkBufferSlice(rtOutput.m_neeCacheTask, 0, rtOutput.m_neeCacheTask->info().size));
    ctx->bindResourceView(INTEGRATE_INDIRECT_BINDING_NEE_CACHE_THREAD_TASK, rtOutput.m_neeCacheThreadTask.view, nullptr);

    ctx->bindResourceView(INTEGRATE_INDIRECT_BINDING_GRADIENTS_INPUT, rtOutput.m_rtxdiGradients.view, nullptr);

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
    const bool includePortals = RtxOptions::Get()->rayPortalModelTextureHashes().size() > 0 || rtOutput.m_raytraceArgs.numActiveRayPortals > 0;
    const bool pomEnabled = rtOutput.m_raytraceArgs.pomMode != DisplacementMode::Off && RtxOptions::Displacement::enableIndirectHit();

    // Trace indirect ray
    {
      ScopedGpuProfileZone(ctx, "Integrate Indirect Raytracing");
      const NeeCachePass& neeCache = ctx->getCommonObjects()->metaNeeCache();
      switch (RtxOptions::Get()->getRenderPassIntegrateIndirectRaytraceMode()) {
      case RaytraceMode::RayQuery:
        VkExtent3D workgroups = util::computeBlockCount(rayDims, VkExtent3D { 16, 8, 1 });
        ctx->bindShader(VK_SHADER_STAGE_COMPUTE_BIT, getComputeShader(neeCache.enable()));
        ctx->dispatch(workgroups.width, workgroups.height, workgroups.depth);
        break;
      case RaytraceMode::RayQueryRayGen:
        ctx->bindRaytracingPipelineShaders(getPipelineShaders(true, serEnabled, ommEnabled, neeCache.enable(), includePortals, pomEnabled));
        ctx->traceRays(rayDims.width, rayDims.height, rayDims.depth);
        break;
      case RaytraceMode::TraceRay:
        ctx->bindRaytracingPipelineShaders(getPipelineShaders(false, serEnabled, ommEnabled, neeCache.enable(), includePortals, pomEnabled));
        ctx->traceRays(rayDims.width, rayDims.height, rayDims.depth);
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

    ScopedGpuProfileZone(ctx, "Integrate NEE");
    ctx->bindCommonRayTracingResources(rtOutput);

    ctx->bindResourceView(INTEGRATE_NEE_BINDING_SHARED_FLAGS_INPUT, rtOutput.m_sharedFlags.view, nullptr);
    ctx->bindResourceView(INTEGRATE_NEE_BINDING_SHARED_MATERIAL_DATA0_INPUT, rtOutput.m_sharedMaterialData0.view, nullptr);
    ctx->bindResourceView(INTEGRATE_NEE_BINDING_SHARED_MATERIAL_DATA1_INPUT, rtOutput.m_sharedMaterialData1.view, nullptr);
    ctx->bindResourceView(INTEGRATE_NEE_BINDING_SHARED_TEXTURE_COORD_INPUT, rtOutput.m_sharedTextureCoord.view, nullptr);
    ctx->bindResourceView(INTEGRATE_NEE_BINDING_SHARED_SURFACE_INDEX_INPUT, rtOutput.m_sharedSurfaceIndex.view, nullptr);
    ctx->bindResourceView(INTEGRATE_NEE_BINDING_SHARED_SUBSURFACE_DATA_INPUT, rtOutput.m_sharedSubsurfaceData.view, nullptr);

    ctx->bindResourceView(INTEGRATE_NEE_BINDING_PRIMARY_WORLD_SHADING_NORMAL_INPUT, rtOutput.m_primaryWorldShadingNormal.view, nullptr);
    ctx->bindResourceView(INTEGRATE_NEE_BINDING_PRIMARY_WORLD_INTERPOLATED_NORMAL_INPUT, rtOutput.m_primaryWorldInterpolatedNormal.view, nullptr);
    ctx->bindResourceView(INTEGRATE_NEE_BINDING_PRIMARY_PERCEPTUAL_ROUGHNESS_INPUT, rtOutput.m_primaryPerceptualRoughness.view, nullptr);
    ctx->bindResourceView(INTEGRATE_NEE_BINDING_PRIMARY_HIT_DISTANCE_INPUT, rtOutput.m_primaryHitDistance.view, nullptr);
    ctx->bindResourceView(INTEGRATE_NEE_BINDING_PRIMARY_ALBEDO_INPUT, rtOutput.m_primaryAlbedo.view, nullptr);
    ctx->bindResourceView(INTEGRATE_NEE_BINDING_PRIMARY_VIEW_DIRECTION_INPUT, rtOutput.m_primaryViewDirection.view, nullptr);
    ctx->bindResourceView(INTEGRATE_NEE_BINDING_PRIMARY_CONE_RADIUS_INPUT, rtOutput.m_primaryConeRadius.view, nullptr);
    ctx->bindResourceView(INTEGRATE_NEE_BINDING_PRIMARY_WORLD_POSITION_INPUT, rtOutput.getCurrentPrimaryWorldPositionWorldTriangleNormal().view, nullptr);
    ctx->bindResourceView(INTEGRATE_NEE_BINDING_PRIMARY_POSITION_ERROR_INPUT, rtOutput.m_primaryPositionError.view, nullptr);
    ctx->bindResourceView(INTEGRATE_NEE_BINDING_INDIRECT_RADIANCE_HIT_DISTANCE_INPUT, rtOutput.m_indirectRadianceHitDistance.view(Resources::AccessType::Read), nullptr);
    ctx->bindResourceView(INTEGRATE_NEE_BINDING_HIT_GEOMETRY_INPUT, rtOutput.m_restirGIHitGeometry.view, nullptr);
    ctx->bindResourceView(INTEGRATE_NEE_BINDING_RADIANCE_INPUT, rtOutput.m_restirGIRadiance.view(Resources::AccessType::Read), nullptr);

    ctx->bindResourceView(INTEGRATE_NEE_BINDING_PRIMARY_BASE_REFLECTIVITY_INPUT_OUTPUT, rtOutput.m_primaryBaseReflectivity.view(Resources::AccessType::ReadWrite), nullptr);

    ctx->bindResourceView(INTEGRATE_NEE_BINDING_PRIMARY_INDIRECT_DIFFUSE_RADIANCE_HIT_DISTANCE_OUTPUT, rtOutput.m_primaryIndirectDiffuseRadiance.view(Resources::AccessType::Write), nullptr);
    ctx->bindResourceView(INTEGRATE_NEE_BINDING_PRIMARY_INDIRECT_SPECULAR_RADIANCE_HIT_DISTANCE_OUTPUT, rtOutput.m_primaryIndirectSpecularRadiance.view(Resources::AccessType::Write), nullptr);

    ctx->bindResourceBuffer(INTEGRATE_NEE_BINDING_RESTIR_GI_RESERVOIR_OUTPUT, DxvkBufferSlice(rtOutput.m_restirGIReservoirBuffer, 0, rtOutput.m_restirGIReservoirBuffer->info().size));
    ctx->bindResourceView(INTEGRATE_NEE_BINDING_BSDF_FACTOR2_OUTPUT, rtOutput.m_bsdfFactor2.view, nullptr);

    ctx->bindResourceBuffer(INTEGRATE_NEE_BINDING_NEE_CACHE, DxvkBufferSlice(rtOutput.m_neeCache, 0, rtOutput.m_neeCache->info().size));
    ctx->bindResourceBuffer(INTEGRATE_NEE_BINDING_NEE_CACHE_TASK, DxvkBufferSlice(rtOutput.m_neeCacheTask, 0, rtOutput.m_neeCacheTask->info().size));
    ctx->bindResourceBuffer(INTEGRATE_NEE_BINDING_NEE_CACHE_SAMPLE, DxvkBufferSlice(rtOutput.m_neeCacheSample, 0, rtOutput.m_neeCacheSample->info().size));
    ctx->bindResourceView(INTEGRATE_NEE_BINDING_NEE_CACHE_THREAD_TASK, rtOutput.m_neeCacheThreadTask.view, nullptr);
    ctx->bindResourceBuffer(INTEGRATE_NEE_BINDING_PRIMITIVE_ID_PREFIX_SUM, DxvkBufferSlice(primitiveIDPrefixSumBuffer, 0, primitiveIDPrefixSumBuffer->info().size));

    ctx->bindShader(VK_SHADER_STAGE_COMPUTE_BIT, IntegrateNEEShader::getShader());
    ctx->dispatch(workgroups.width, workgroups.height, workgroups.depth);

    // Visualize the nee cache when debug view is chosen.
    uint32_t debugViewIndex = ctx->getCommonObjects()->metaDebugView().debugViewIdx();
    if (debugViewIndex == DEBUG_VIEW_NEE_CACHE_LIGHT_HISTOGRAM || debugViewIndex == DEBUG_VIEW_NEE_CACHE_HISTOGRAM ||
     debugViewIndex == DEBUG_VIEW_NEE_CACHE_ACCUMULATE_MAP || debugViewIndex == DEBUG_VIEW_NEE_CACHE_HASH_MAP)
    {
      ctx->bindShader(VK_SHADER_STAGE_COMPUTE_BIT, VisualizeNEEShader::getShader());
      ctx->dispatch(workgroups.width, workgroups.height, workgroups.depth);
    }
  }

  DxvkRaytracingPipelineShaders DxvkPathtracerIntegrateIndirect::getPipelineShaders(const bool useRayQuery,
                                                                                    const bool serEnabled,
                                                                                    const bool ommEnabled,
                                                                                    const bool useNeeCache,
                                                                                    const bool includePortals,
                                                                                    const bool pomEnabled) {

    DxvkRaytracingPipelineShaders shaders;
    if (useRayQuery) {
      if (useNeeCache)
        shaders.addGeneralShader(GET_SHADER_VARIANT(VK_SHADER_STAGE_RAYGEN_BIT_KHR, IntegrateIndirectRayGenShader, integrate_indirect_rayquery_raygen_neeCache));
      else
        shaders.addGeneralShader(GET_SHADER_VARIANT(VK_SHADER_STAGE_RAYGEN_BIT_KHR, IntegrateIndirectRayGenShader, integrate_indirect_rayquery_raygen));
      shaders.debugName = "Integrate Indirect RayQuery (RGS)";
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

      if (useNeeCache) {
        shaders.addGeneralShader(GET_SHADER_VARIANT(VK_SHADER_STAGE_MISS_BIT_KHR, IntegrateIndirectMissShader, integrate_indirect_miss_neeCache));
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
        shaders.addGeneralShader(GET_SHADER_VARIANT(VK_SHADER_STAGE_MISS_BIT_KHR, IntegrateIndirectMissShader, integrate_indirect_miss));
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

      shaders.debugName = "Integrate Indirect TraceRay (RGS)";
    }

    if (ommEnabled)
      shaders.pipelineFlags |= VK_PIPELINE_CREATE_RAY_TRACING_OPACITY_MICROMAP_BIT_EXT;

    return shaders;
  }

  Rc<DxvkShader> DxvkPathtracerIntegrateIndirect::getComputeShader(const bool useNeeCache) const {
    if (useNeeCache) {
      return GET_SHADER_VARIANT(VK_SHADER_STAGE_COMPUTE_BIT, IntegrateIndirectRayGenShader, integrate_indirect_rayquery_neeCache);
    } else {
      return GET_SHADER_VARIANT(VK_SHADER_STAGE_COMPUTE_BIT, IntegrateIndirectRayGenShader, integrate_indirect_rayquery);
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
