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
#include "rtx_rtxdi_rayquery.h"
#include "dxvk_device.h"
#include "rtx_render/rtx_shader_manager.h"

#include "rtx/pass/common_binding_indices.h"
#include "rtx/pass/raytrace_args.h"
#include "rtx/pass/rtxdi/rtxdi_reuse_binding_indices.h"
#include "rtx/pass/rtxdi/rtxdi_compute_gradients_bindings.h"
#include "rtx/pass/rtxdi/rtxdi_filter_gradients_bindings.h"
#include "rtx/pass/rtxdi/rtxdi_compute_confidence_bindings.h"
#include "rtxdi/RtxdiParameters.h"
#include "dxvk_scoped_annotation.h"
#include "dxvk_context.h"
#include "rtx_context.h"
#include "rtx_imgui.h"

#include <rtx_shaders/rtxdi_temporal_reuse.h>
#include <rtx_shaders/rtxdi_spatial_reuse.h>
#include <rtx_shaders/rtxdi_compute_gradients.h>
#include <rtx_shaders/rtxdi_filter_gradients.h>
#include <rtx_shaders/rtxdi_compute_confidence.h>


namespace dxvk {

  // Defined within an unnamed namespace to ensure unique definition across binary
  namespace {
    class RTXDITemporalReuseShader : public ManagedShader {
      SHADER_SOURCE(RTXDITemporalReuseShader, VK_SHADER_STAGE_COMPUTE_BIT, rtxdi_temporal_reuse)

      BINDLESS_ENABLED()

      BEGIN_PARAMETER()
        COMMON_RAYTRACING_BINDINGS

        // RTXDI Data
        RW_STRUCTURED_BUFFER(RTXDI_REUSE_BINDING_RTXDI_RESERVOIR)
        // GBuffer
        TEXTURE2D(RTXDI_REUSE_BINDING_WORLD_SHADING_NORMAL_INPUT)
        TEXTURE2D(RTXDI_REUSE_BINDING_PERCEPTUAL_ROUGHNESS_INPUT)
        TEXTURE2D(RTXDI_REUSE_BINDING_HIT_DISTANCE_INPUT)
        TEXTURE2D(RTXDI_REUSE_BINDING_ALBEDO_INPUT)
        TEXTURE2D(RTXDI_REUSE_BINDING_BASE_REFLECTIVITY_INPUT)
        TEXTURE2D(RTXDI_REUSE_BINDING_WORLD_POSITION_INPUT)
        TEXTURE2D(RTXDI_REUSE_BINDING_PREV_WORLD_POSITION_INPUT)
        TEXTURE2D(RTXDI_REUSE_BINDING_VIEW_DIRECTION_INPUT)
        TEXTURE2D(RTXDI_REUSE_BINDING_CONE_RADIUS_INPUT)
        TEXTURE2D(RTXDI_REUSE_BINDING_WS_MVEC_INPUT_OUTPUT)
        TEXTURE2D(RTXDI_REUSE_BINDING_SS_MVEC_INPUT)
        TEXTURE2D(RTXDI_REUSE_BINDING_POSITION_ERROR_INPUT)
        TEXTURE2D(RTXDI_REUSE_BINDING_SHARED_SURFACE_INDEX_INPUT)
        TEXTURE2D(RTXDI_REUSE_BINDING_SUBSURFACE_DATA_INPUT)
        TEXTURE2D(RTXDI_REUSE_BINDING_SHARED_FLAGS_INPUT)
        RW_TEXTURE2D(RTXDI_REUSE_BINDING_LAST_GBUFFER)
        RW_TEXTURE2D(RTXDI_REUSE_BINDING_REPROJECTION_CONFIDENCE_OUTPUT)
        RW_TEXTURE2D(RTXDI_REUSE_BINDING_BSDF_FACTOR_OUTPUT)
        RW_TEXTURE2D(RTXDI_REUSE_BINDING_TEMPORAL_POSITION_OUTPUT)
        TEXTURE2D(RTXDI_REUSE_BINDING_BEST_LIGHTS_INPUT)
      END_PARAMETER()
    };

    PREWARM_SHADER_PIPELINE(RTXDITemporalReuseShader);

    class RTXDISpatialReuseShader : public ManagedShader {
      SHADER_SOURCE(RTXDISpatialReuseShader, VK_SHADER_STAGE_COMPUTE_BIT, rtxdi_spatial_reuse)

      BINDLESS_ENABLED()

      BEGIN_PARAMETER()
        COMMON_RAYTRACING_BINDINGS
        
        // RTXDI Data
        RW_STRUCTURED_BUFFER(RTXDI_REUSE_BINDING_RTXDI_RESERVOIR)
        // GBuffer
        TEXTURE2D(RTXDI_REUSE_BINDING_WORLD_SHADING_NORMAL_INPUT)
        TEXTURE2D(RTXDI_REUSE_BINDING_PERCEPTUAL_ROUGHNESS_INPUT)
        TEXTURE2D(RTXDI_REUSE_BINDING_HIT_DISTANCE_INPUT)
        TEXTURE2D(RTXDI_REUSE_BINDING_ALBEDO_INPUT)
        TEXTURE2D(RTXDI_REUSE_BINDING_BASE_REFLECTIVITY_INPUT)
        TEXTURE2D(RTXDI_REUSE_BINDING_WORLD_POSITION_INPUT)
        TEXTURE2D(RTXDI_REUSE_BINDING_PREV_WORLD_POSITION_INPUT)
        TEXTURE2D(RTXDI_REUSE_BINDING_VIEW_DIRECTION_INPUT)
        TEXTURE2D(RTXDI_REUSE_BINDING_CONE_RADIUS_INPUT)
        TEXTURE2D(RTXDI_REUSE_BINDING_WS_MVEC_INPUT_OUTPUT)
        TEXTURE2D(RTXDI_REUSE_BINDING_SS_MVEC_INPUT)
        TEXTURE2D(RTXDI_REUSE_BINDING_POSITION_ERROR_INPUT)
        TEXTURE2D(RTXDI_REUSE_BINDING_SHARED_SURFACE_INDEX_INPUT)
        TEXTURE2D(RTXDI_REUSE_BINDING_SUBSURFACE_DATA_INPUT)
        TEXTURE2D(RTXDI_REUSE_BINDING_SHARED_FLAGS_INPUT)
        RW_TEXTURE2D(RTXDI_REUSE_BINDING_LAST_GBUFFER)
        RW_TEXTURE2D(RTXDI_REUSE_BINDING_REPROJECTION_CONFIDENCE_OUTPUT)
        RW_TEXTURE2D(RTXDI_REUSE_BINDING_BSDF_FACTOR_OUTPUT)
        RW_TEXTURE2D(RTXDI_REUSE_BINDING_TEMPORAL_POSITION_OUTPUT)
        TEXTURE2D(RTXDI_REUSE_BINDING_BEST_LIGHTS_INPUT)
      END_PARAMETER()
    };

    PREWARM_SHADER_PIPELINE(RTXDISpatialReuseShader);

    class RTXDIComputeGradientsShader : public ManagedShader {
      SHADER_SOURCE(RTXDIComputeGradientsShader, VK_SHADER_STAGE_COMPUTE_BIT, rtxdi_compute_gradients)

      BINDLESS_ENABLED()

      PUSH_CONSTANTS(ComputeGradientsArgs)

      BEGIN_PARAMETER()
      RTXDI_COMPUTE_GRADIENTS_BINDINGS
      END_PARAMETER()
    };

    PREWARM_SHADER_PIPELINE(RTXDIComputeGradientsShader);

    class RTXDIFilterGradientsShader : public ManagedShader {
      SHADER_SOURCE(RTXDIFilterGradientsShader, VK_SHADER_STAGE_COMPUTE_BIT, rtxdi_filter_gradients)

      PUSH_CONSTANTS(FilterGradientsArgs)

      BEGIN_PARAMETER()
      RTXDI_FILTER_GRADIENTS_BINDINGS
      END_PARAMETER()
    };

    PREWARM_SHADER_PIPELINE(RTXDIFilterGradientsShader);

    class RTXDIComputeConfidenceShader : public ManagedShader {
      SHADER_SOURCE(RTXDIComputeConfidenceShader, VK_SHADER_STAGE_COMPUTE_BIT, rtxdi_compute_confidence)

      PUSH_CONSTANTS(ComputeConfidenceArgs)

      BEGIN_PARAMETER()
      RTXDI_COMPUTE_CONFIDENCE_BINDINGS
      END_PARAMETER()
    };

    PREWARM_SHADER_PIPELINE(RTXDIComputeConfidenceShader);
  }

  DxvkRtxdiRayQuery::DxvkRtxdiRayQuery(DxvkDevice* device) {
  }

  void DxvkRtxdiRayQuery::showImguiSettings() {
    ImGui::SliderInt("Initial Sample Count", &initialSampleCountObject(), 1, 64);
    ImGui::Checkbox("Sample Best Lights", &enableBestLightSamplingObject());
    ImGui::Checkbox("Initial Visibility", &enableInitialVisibilityObject());
    ImGui::Separator();
    ImGui::Checkbox("Temporal Reuse", &enableTemporalReuseObject());
    if (enableTemporalReuse()) {
      ImGui::SliderInt("Max History Length", &maxHistoryLengthObject(), 1, 32);
      ImGui::SliderInt("Permutation Sampling on Nth Frame", &permutationSamplingNthFrameObject(), 0, 8);
      ImGui::Checkbox("Temporal Bias Correction", &enableTemporalBiasCorrectionObject());
      ImGui::Checkbox("Discard Enlarged Pixels", &enableDiscardEnlargedPixelsObject());
    }
    ImGui::Separator();
    ImGui::Checkbox("Spatial Reuse", &enableSpatialReuseObject());
    if (enableSpatialReuse()) {
      ImGui::SliderInt("Spatial Sample Count", &spatialSamplesObject(), 1, 16);
      ImGui::SliderInt("Disocclusion Sample Count", &disocclusionSamplesObject(), 1, 16);
      ImGui::SliderInt("Disocclusion Frames", &disocclusionFramesObject(), 0, 16);
    }
    ImGui::Checkbox("Ray Traced Bias Correction", &enableRayTracedBiasCorrectionObject());
    ImGui::Separator();
    ImGui::Checkbox("Discard Invisible Samples", &enableDiscardInvisibleSamplesObject());
    ImGui::Checkbox("Indirect Sample Stealing", &enableSampleStealingObject());
    ImGui::Checkbox("Steal Boundary Samples When Outside Of Screen", &stealBoundaryPixelSamplesWhenOutsideOfScreenObject());
    ImGui::Checkbox("Cross Portal Light", &enableCrossPortalLightObject());
    ImGui::Checkbox("Compute Denoiser Confidence", &enableDenoiserConfidenceObject());

    if (enableDenoiserConfidence() && ImGui::CollapsingHeader("Confidence Settings", 0))
    {
      ImGui::Indent();
      ImGui::SliderFloat("History Length", &confidenceHistoryLengthObject(), 0.f, 16.f);
      ImGui::SliderFloat("Gradient Power", &confidenceGradientPowerObject(), 1.f, 16.f);
      ImGui::SliderFloat("Gradient Scale", &confidenceGradientScaleObject(), 0.f, 16.f);
      ImGui::SliderInt("Gradient Filter Passes", &gradientFilterPassesObject(), 0, 6);
      ImGui::SliderFloat("Filter HitDist Sensitivity", &gradientHitDistanceSensitivityObject(), 0.f, 50.f);
      ImGui::SliderFloat("Minimum Confidence", &minimumConfidenceObject(), 0.f, 1.f);
      ImGui::SliderFloat("Confidence HitDist Sensitivity", &confidenceHitDistanceSensitivityObject(), 0.f, 10000.f);
      ImGui::Unindent();
    }
  }

  void DxvkRtxdiRayQuery::setRaytraceArgs(Resources::RaytracingOutput& rtOutput) const {
    rtOutput.m_raytraceArgs.enableRtxdiCrossPortalLight = enableCrossPortalLight();
    rtOutput.m_raytraceArgs.enableRtxdiInitialVisibility = enableInitialVisibility();
    rtOutput.m_raytraceArgs.enableRtxdiPermutationSampling = permutationSamplingNthFrame() > 0 && (rtOutput.m_raytraceArgs.frameIdx % permutationSamplingNthFrame()) == 0;
    rtOutput.m_raytraceArgs.enableRtxdiRayTracedBiasCorrection = enableRayTracedBiasCorrection();
    rtOutput.m_raytraceArgs.enableRtxdiSampleStealing = enableSampleStealing();
    rtOutput.m_raytraceArgs.enableRtxdiStealBoundaryPixelSamplesWhenOutsideOfScreen = stealBoundaryPixelSamplesWhenOutsideOfScreen();
    rtOutput.m_raytraceArgs.enableRtxdiSpatialReuse = enableSpatialReuse();
    rtOutput.m_raytraceArgs.enableRtxdiTemporalBiasCorrection = enableTemporalBiasCorrection();
    rtOutput.m_raytraceArgs.enableRtxdiTemporalReuse = enableTemporalReuse();
    rtOutput.m_raytraceArgs.enableRtxdiDiscardInvisibleSamples = enableDiscardInvisibleSamples();
    rtOutput.m_raytraceArgs.enableRtxdiDiscardEnlargedPixels= enableDiscardEnlargedPixels();
    rtOutput.m_raytraceArgs.rtxdiDisocclusionSamples = disocclusionSamples();
    rtOutput.m_raytraceArgs.rtxdiDisocclusionFrames = float(disocclusionFrames());
    rtOutput.m_raytraceArgs.rtxdiSpatialSamples = spatialSamples();
    rtOutput.m_raytraceArgs.rtxdiMaxHistoryLength = maxHistoryLength();
    // Note: best light sampling uses data written into the RtxdiBestLights texture by the confidence pass on the previous frame.
    // We need to make sure that the data is there and valid: light indices from more than one frame ago are not mappable to the current frame.
    const bool isRtxdiBestLightsValid = rtOutput.m_rtxdiBestLights.matchesWriteFrameIdx(rtOutput.m_raytraceArgs.frameIdx - 1);

    rtOutput.m_raytraceArgs.enableRtxdiBestLightSampling = enableBestLightSampling() && isRtxdiBestLightsValid;
    // Note: initialSamples is not written here, it's used in LightManager::setRaytraceArgs
    // to derive the per-light-type sample counts
  }

  void DxvkRtxdiRayQuery::dispatch(RtxContext* ctx, const Resources::RaytracingOutput& rtOutput) {
    ScopedGpuProfileZone(ctx, "RTXDI");

    if (!RtxOptions::Get()->useRTXDI())
      return;

    const auto& numRaysExtent = rtOutput.m_compositeOutputExtent;
    VkExtent3D workgroups = util::computeBlockCount(numRaysExtent, VkExtent3D{ 16, 8, 1 });

    ctx->bindCommonRayTracingResources(rtOutput);

    {
      ScopedGpuProfileZone(ctx, "RTXDI Initial & Temporal Reuse");

      // Note: Primary buffers bound as these exhibit coherency for RTXDI and denoising.
      ctx->bindResourceBuffer(RTXDI_REUSE_BINDING_RTXDI_RESERVOIR, DxvkBufferSlice(rtOutput.m_rtxdiReservoirBuffer, 0, rtOutput.m_rtxdiReservoirBuffer->info().size));
      ctx->bindResourceView(RTXDI_REUSE_BINDING_WORLD_SHADING_NORMAL_INPUT, rtOutput.m_primaryWorldShadingNormal.view, nullptr);
      ctx->bindResourceView(RTXDI_REUSE_BINDING_PERCEPTUAL_ROUGHNESS_INPUT, rtOutput.m_primaryPerceptualRoughness.view, nullptr);
      ctx->bindResourceView(RTXDI_REUSE_BINDING_HIT_DISTANCE_INPUT, rtOutput.m_primaryHitDistance.view, nullptr);
      ctx->bindResourceView(RTXDI_REUSE_BINDING_ALBEDO_INPUT, rtOutput.m_primaryAlbedo.view, nullptr);
      // Note: Texture contains Base Reflectivity here (due to being before the demodulate pass)
      ctx->bindResourceView(RTXDI_REUSE_BINDING_BASE_REFLECTIVITY_INPUT, rtOutput.m_primaryBaseReflectivity.view(Resources::AccessType::Read), nullptr);
      ctx->bindResourceView(RTXDI_REUSE_BINDING_WORLD_POSITION_INPUT, rtOutput.getCurrentPrimaryWorldPositionWorldTriangleNormal().view, nullptr);
      ctx->bindResourceView(RTXDI_REUSE_BINDING_PREV_WORLD_POSITION_INPUT, rtOutput.getPreviousPrimaryWorldPositionWorldTriangleNormal().view, nullptr);
      ctx->bindResourceView(RTXDI_REUSE_BINDING_WS_MVEC_INPUT_OUTPUT, rtOutput.m_primaryVirtualMotionVector.view, nullptr);
      ctx->bindResourceView(RTXDI_REUSE_BINDING_SS_MVEC_INPUT, rtOutput.m_primaryScreenSpaceMotionVector.view, nullptr);
      ctx->bindResourceView(RTXDI_REUSE_BINDING_VIEW_DIRECTION_INPUT, rtOutput.m_primaryViewDirection.view, nullptr);
      ctx->bindResourceView(RTXDI_REUSE_BINDING_CONE_RADIUS_INPUT, rtOutput.m_primaryConeRadius.view, nullptr);
      ctx->bindResourceView(RTXDI_REUSE_BINDING_POSITION_ERROR_INPUT, rtOutput.m_primaryPositionError.view, nullptr);
      ctx->bindResourceView(RTXDI_REUSE_BINDING_LAST_GBUFFER, rtOutput.m_gbufferLast.view, nullptr);
      ctx->bindResourceView(RTXDI_REUSE_BINDING_REPROJECTION_CONFIDENCE_OUTPUT, rtOutput.m_reprojectionConfidence.view, nullptr);
      ctx->bindResourceView(RTXDI_REUSE_BINDING_SHARED_SURFACE_INDEX_INPUT, rtOutput.m_sharedSurfaceIndex.view, nullptr);
      ctx->bindResourceView(RTXDI_REUSE_BINDING_SUBSURFACE_DATA_INPUT, rtOutput.m_sharedSubsurfaceData.view, nullptr);
      ctx->bindResourceView(RTXDI_REUSE_BINDING_SHARED_FLAGS_INPUT, rtOutput.m_sharedFlags.view, nullptr);
      ctx->bindResourceView(RTXDI_REUSE_BINDING_BSDF_FACTOR_OUTPUT, rtOutput.m_bsdfFactor.view, nullptr);
      ctx->bindResourceView(RTXDI_REUSE_BINDING_TEMPORAL_POSITION_OUTPUT, rtOutput.m_primaryRtxdiTemporalPosition.view, nullptr);
      ctx->bindResourceView(RTXDI_REUSE_BINDING_BEST_LIGHTS_INPUT, rtOutput.m_rtxdiBestLights.view(Resources::AccessType::Read, rtOutput.m_raytraceArgs.enableRtxdiBestLightSampling) , nullptr);

      ctx->bindShader(VK_SHADER_STAGE_COMPUTE_BIT, RTXDITemporalReuseShader::getShader());
      ctx->dispatch(workgroups.width, workgroups.height, workgroups.depth);
    }

    {
      ScopedGpuProfileZone(ctx, "RTXDI Spatial Reuse");

      // Note: Primary buffers bound as these exhibit coherency for RTXDI and denoising.
      ctx->bindResourceBuffer(RTXDI_REUSE_BINDING_RTXDI_RESERVOIR, DxvkBufferSlice(rtOutput.m_rtxdiReservoirBuffer, 0, rtOutput.m_rtxdiReservoirBuffer->info().size));
      ctx->bindResourceView(RTXDI_REUSE_BINDING_WORLD_SHADING_NORMAL_INPUT, rtOutput.m_primaryWorldShadingNormal.view, nullptr);
      ctx->bindResourceView(RTXDI_REUSE_BINDING_PERCEPTUAL_ROUGHNESS_INPUT, rtOutput.m_primaryPerceptualRoughness.view, nullptr);
      ctx->bindResourceView(RTXDI_REUSE_BINDING_HIT_DISTANCE_INPUT, rtOutput.m_primaryHitDistance.view, nullptr);
      ctx->bindResourceView(RTXDI_REUSE_BINDING_ALBEDO_INPUT, rtOutput.m_primaryAlbedo.view, nullptr);
      ctx->bindResourceView(RTXDI_REUSE_BINDING_BASE_REFLECTIVITY_INPUT, rtOutput.m_primaryBaseReflectivity.view(Resources::AccessType::Read), nullptr);
      ctx->bindResourceView(RTXDI_REUSE_BINDING_WORLD_POSITION_INPUT, rtOutput.getCurrentPrimaryWorldPositionWorldTriangleNormal().view, nullptr);
      ctx->bindResourceView(RTXDI_REUSE_BINDING_PREV_WORLD_POSITION_INPUT, rtOutput.getPreviousPrimaryWorldPositionWorldTriangleNormal().view, nullptr);
      ctx->bindResourceView(RTXDI_REUSE_BINDING_WS_MVEC_INPUT_OUTPUT, rtOutput.m_primaryVirtualMotionVector.view, nullptr);
      ctx->bindResourceView(RTXDI_REUSE_BINDING_SS_MVEC_INPUT, rtOutput.m_primaryScreenSpaceMotionVector.view, nullptr);
      ctx->bindResourceView(RTXDI_REUSE_BINDING_VIEW_DIRECTION_INPUT, rtOutput.m_primaryViewDirection.view, nullptr);
      ctx->bindResourceView(RTXDI_REUSE_BINDING_CONE_RADIUS_INPUT, rtOutput.m_primaryConeRadius.view, nullptr);
      ctx->bindResourceView(RTXDI_REUSE_BINDING_POSITION_ERROR_INPUT, rtOutput.m_primaryPositionError.view, nullptr);
      ctx->bindResourceView(RTXDI_REUSE_BINDING_LAST_GBUFFER, rtOutput.m_gbufferLast.view, nullptr);
      ctx->bindResourceView(RTXDI_REUSE_BINDING_REPROJECTION_CONFIDENCE_OUTPUT, rtOutput.m_reprojectionConfidence.view, nullptr);
      ctx->bindResourceView(RTXDI_REUSE_BINDING_SHARED_SURFACE_INDEX_INPUT, rtOutput.m_sharedSurfaceIndex.view, nullptr);
      ctx->bindResourceView(RTXDI_REUSE_BINDING_SUBSURFACE_DATA_INPUT, rtOutput.m_sharedSubsurfaceData.view, nullptr);
      ctx->bindResourceView(RTXDI_REUSE_BINDING_SHARED_FLAGS_INPUT, rtOutput.m_sharedFlags.view, nullptr);
      ctx->bindResourceView(RTXDI_REUSE_BINDING_BSDF_FACTOR_OUTPUT, rtOutput.m_bsdfFactor.view, nullptr);
      ctx->bindResourceView(RTXDI_REUSE_BINDING_TEMPORAL_POSITION_OUTPUT, rtOutput.m_primaryRtxdiTemporalPosition.view, nullptr);
      ctx->bindResourceView(RTXDI_REUSE_BINDING_BEST_LIGHTS_INPUT, rtOutput.m_rtxdiBestLights.view(Resources::AccessType::Read, rtOutput.m_raytraceArgs.enableRtxdiBestLightSampling), nullptr);

      ctx->bindShader(VK_SHADER_STAGE_COMPUTE_BIT, RTXDISpatialReuseShader::getShader());
      ctx->dispatch(workgroups.width, workgroups.height, workgroups.depth);
    }
  }

  void DxvkRtxdiRayQuery::dispatchGradient(RtxContext* ctx, const Resources::RaytracingOutput& rtOutput) {
    
    if (!RtxOptions::Get()->useRTXDI() || 
        !getEnableDenoiserConfidence())
      return;

    const uint32_t frameIdx = ctx->getDevice()->getCurrentFrameId(); 
    VkExtent3D numThreads = rtOutput.m_compositeOutputExtent;
    VkExtent3D workgroups = util::computeBlockCount(numThreads, VkExtent3D { 16 * RTXDI_GRAD_FACTOR, 8 * RTXDI_GRAD_FACTOR, 1 });

    ctx->bindCommonRayTracingResources(rtOutput);

    ctx->setPushConstantBank(DxvkPushConstantBank::RTX);

    {
      ScopedGpuProfileZone(ctx, "Compute Gradients");
      
      ctx->bindResourceBuffer(RTXDI_COMPUTE_GRADIENTS_BINDING_RTXDI_RESERVOIR, DxvkBufferSlice(rtOutput.m_rtxdiReservoirBuffer, 0, rtOutput.m_rtxdiReservoirBuffer->info().size));
      ctx->bindResourceView(RTXDI_COMPUTE_GRADIENTS_BINDING_CURRENT_WORLD_POSITION_INPUT, rtOutput.getCurrentPrimaryWorldPositionWorldTriangleNormal().view, nullptr);
      ctx->bindResourceView(RTXDI_COMPUTE_GRADIENTS_BINDING_PREVIOUS_WORLD_POSITION_INPUT, rtOutput.getPreviousPrimaryWorldPositionWorldTriangleNormal().view, nullptr);
      ctx->bindResourceView(RTXDI_COMPUTE_GRADIENTS_BINDING_CONE_RADIUS_INPUT, rtOutput.m_primaryConeRadius.view, nullptr);
      ctx->bindResourceView(RTXDI_COMPUTE_GRADIENTS_BINDING_MVEC_INPUT, rtOutput.m_primaryVirtualMotionVector.view, nullptr);
      ctx->bindResourceView(RTXDI_COMPUTE_GRADIENTS_BINDING_POSITION_ERROR_INPUT, rtOutput.m_primaryPositionError.view, nullptr);
      ctx->bindResourceView(RTXDI_COMPUTE_GRADIENTS_BINDING_TEMPORAL_POSITION_INPUT, rtOutput.m_primaryRtxdiTemporalPosition.view, nullptr);
      ctx->bindResourceView(RTXDI_COMPUTE_GRADIENTS_BINDING_CURRENT_ILLUMINANCE_INPUT, rtOutput.getCurrentRtxdiIlluminance().view(Resources::AccessType::Read), nullptr);

      const bool isPreviousIlluminanceValid = rtOutput.getPreviousRtxdiIlluminance().matchesWriteFrameIdx(frameIdx - 1);
      ctx->bindResourceView(RTXDI_COMPUTE_GRADIENTS_BINDING_PREVIOUS_ILLUMINANCE_INPUT, rtOutput.getPreviousRtxdiIlluminance().view(Resources::AccessType::Read, isPreviousIlluminanceValid), nullptr);

      ctx->bindResourceView(RTXDI_COMPUTE_GRADIENTS_BINDING_HIT_DISTANCE_INPUT, rtOutput.m_primaryHitDistance.view, nullptr);
      ctx->bindResourceView(RTXDI_COMPUTE_GRADIENTS_BINDING_SHARED_FLAGS_INPUT, rtOutput.m_sharedFlags.view, nullptr);
      ctx->bindResourceView(RTXDI_COMPUTE_GRADIENTS_BINDING_GRADIENTS_OUTPUT, rtOutput.m_rtxdiGradients.view, nullptr);
      ctx->bindResourceView(RTXDI_COMPUTE_GRADIENTS_BINDING_BEST_LIGHTS_OUTPUT, rtOutput.m_rtxdiBestLights.view(Resources::AccessType::Write), nullptr);
      
      ctx->bindShader(VK_SHADER_STAGE_COMPUTE_BIT, RTXDIComputeGradientsShader::getShader());

      ComputeGradientsArgs args {};
      args.darknessBias = 1e-4f;
      args.usePreviousIlluminance = isPreviousIlluminanceValid;
      ctx->pushConstants(0, sizeof(args), &args);

      ctx->dispatch(workgroups.width, workgroups.height, workgroups.depth);
    }
  }

  void DxvkRtxdiRayQuery::dispatchConfidence(RtxContext* ctx, const Resources::RaytracingOutput& rtOutput) {
    
    if (!RtxOptions::Get()->useRTXDI() || 
        !getEnableDenoiserConfidence())
      return;

    ScopedGpuProfileZone(ctx, "RTXDI Confidence");

    const uint32_t frameIdx = ctx->getDevice()->getCurrentFrameId(); 
    VkExtent3D numThreads = rtOutput.m_compositeOutputExtent;
    VkExtent3D workgroups = util::computeBlockCount(numThreads, VkExtent3D { 16 * RTXDI_GRAD_FACTOR, 8 * RTXDI_GRAD_FACTOR, 1 });

    ctx->bindCommonRayTracingResources(rtOutput);

    ctx->setPushConstantBank(DxvkPushConstantBank::RTX);

    {
      ScopedGpuProfileZone(ctx, "Filter Gradients");

      ctx->bindResourceView(RTXDI_FILTER_GRADIENTS_BINDING_GRADIENTS_INPUT_OUTPUT, rtOutput.m_rtxdiGradients.view, nullptr);

      ctx->bindShader(VK_SHADER_STAGE_COMPUTE_BIT, RTXDIFilterGradientsShader::getShader());

      FilterGradientsArgs args {};
      args.gradientImageSize.x = rtOutput.m_rtxdiGradients.image->info().extent.width;
      args.gradientImageSize.y = rtOutput.m_rtxdiGradients.image->info().extent.height;
      args.hitDistanceSensitivity = gradientHitDistanceSensitivity();

      for (uint32_t passIndex = 0; passIndex < gradientFilterPasses(); ++passIndex) {
        args.passIndex = passIndex;
        ctx->pushConstants(0, sizeof(args), &args);

        ctx->dispatch(workgroups.width, workgroups.height, workgroups.depth);
      }
    }

    workgroups = util::computeBlockCount(numThreads, VkExtent3D { 16, 8, 1 });

    {
      ScopedGpuProfileZone(ctx, "Compute Confidence");
      
      ctx->bindResourceView(RTXDI_COMPUTE_CONFIDENCE_BINDING_GRADIENTS_INPUT, rtOutput.m_rtxdiGradients.view, nullptr);
      ctx->bindResourceView(RTXDI_COMPUTE_CONFIDENCE_BINDING_MVEC_INPUT, rtOutput.m_primaryScreenSpaceMotionVector.view, nullptr);
      ctx->bindResourceView(RTXDI_COMPUTE_CONFIDENCE_BINDING_HIT_DISTANCE_INPUT, rtOutput.m_primaryHitDistance.view, nullptr);

      const bool isPreviousConfidenceValid = rtOutput.getPreviousRtxdiConfidence().matchesWriteFrameIdx(frameIdx - 1);
      ctx->bindResourceView(RTXDI_COMPUTE_CONFIDENCE_BINDING_PREVIOUS_CONFIDENCE_INPUT, rtOutput.getPreviousRtxdiConfidence().view(Resources::AccessType::Read, isPreviousConfidenceValid), nullptr);
      ctx->bindResourceView(RTXDI_COMPUTE_CONFIDENCE_BINDING_CURRENT_CONFIDENCE_OUTPUT, rtOutput.getCurrentRtxdiConfidence().view(Resources::AccessType::Write), nullptr);

      ctx->bindShader(VK_SHADER_STAGE_COMPUTE_BIT, RTXDIComputeConfidenceShader::getShader());

      ComputeConfidenceArgs args {};
      args.resolution.x = numThreads.width;
      args.resolution.y = numThreads.height;
      args.blendFactor = isPreviousConfidenceValid ? 1.f / (1.f + confidenceHistoryLength()) : 1.f;
      args.gradientPower = confidenceGradientPower();
      args.gradientScale = confidenceGradientScale();
      args.minimumConfidence = minimumConfidence();
      args.inputBufferIndex = gradientFilterPasses() & 1;
      args.hitDistanceSensitivity = gradientHitDistanceSensitivity();
      args.confidenceHitDistanceSensitivity = confidenceHitDistanceSensitivity();
      ctx->pushConstants(0, sizeof(args), &args);
      
      ctx->dispatch(workgroups.width, workgroups.height, workgroups.depth);
    }
  }

}
