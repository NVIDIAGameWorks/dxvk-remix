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
#include "rtx/pass/rtxdi/restir_gi_reuse_binding_indices.h"
#include "rtx/pass/rtxdi/restir_gi_final_shading_binding_indices.h"
#include "dxvk_scoped_annotation.h"
#include "dxvk_context.h"
#include "rtx_context.h"
#include "rtx_imgui.h"

#include <rtx_shaders/restir_gi_temporal_reuse.h>
#include <rtx_shaders/restir_gi_spatial_reuse.h>
#include <rtx_shaders/restir_gi_final_shading.h>

namespace dxvk {

  // Defined within an unnamed namespace to ensure unique definition across binary
  namespace {
    ImGui::ComboWithKey<ReSTIRGIBiasCorrection> restirGIBiasCorrectionCombo = ImGui::ComboWithKey<ReSTIRGIBiasCorrection>(
    "ReSTIR GI Spatial Bias Correction",
    ImGui::ComboWithKey<ReSTIRGIBiasCorrection>::ComboEntries { {
        {ReSTIRGIBiasCorrection::None, "None"},
        {ReSTIRGIBiasCorrection::BRDF, "BRDF"},
        {ReSTIRGIBiasCorrection::Raytrace, "Raytrace"},
        {ReSTIRGIBiasCorrection::Pairwise, "Pairwise"},
        {ReSTIRGIBiasCorrection::PairwiseRaytrace, "Pairwise Raytrace"},
    } });

    class ReSTIRGITemporalReuseShader : public ManagedShader
    {
      SHADER_SOURCE(ReSTIRGITemporalReuseShader, VK_SHADER_STAGE_COMPUTE_BIT, restir_gi_temporal_reuse)
      
      BINDLESS_ENABLED()

      BEGIN_PARAMETER()
        COMMON_RAYTRACING_BINDINGS

        // ReSTIR GI Data
        RW_STRUCTURED_BUFFER(RESTIR_GI_REUSE_BINDING_RESERVOIR_INPUT_OUTPUT)
        // GBuffer
        TEXTURE2D(RESTIR_GI_REUSE_BINDING_WORLD_SHADING_NORMAL_INPUT)
        TEXTURE2D(RESTIR_GI_REUSE_BINDING_PERCEPTUAL_ROUGHNESS_INPUT)
        TEXTURE2D(RESTIR_GI_REUSE_BINDING_HIT_DISTANCE_INPUT)
        TEXTURE2D(RESTIR_GI_REUSE_BINDING_ALBEDO_INPUT)
        TEXTURE2D(RESTIR_GI_REUSE_BINDING_BASE_REFLECTIVITY_INPUT)
        TEXTURE2D(RESTIR_GI_REUSE_BINDING_WORLD_POSITION_INPUT)
        TEXTURE2D(RESTIR_GI_REUSE_BINDING_PREV_WORLD_POSITION_INPUT)
        TEXTURE2D(RESTIR_GI_REUSE_BINDING_VIEW_DIRECTION_INPUT)
        TEXTURE2D(RESTIR_GI_REUSE_BINDING_CONE_RADIUS_INPUT)
        TEXTURE2D(RESTIR_GI_REUSE_BINDING_MVEC_INPUT)
        TEXTURE2D(RESTIR_GI_REUSE_BINDING_RADIANCE_INPUT)
        TEXTURE2D(RESTIR_GI_REUSE_BINDING_HIT_GEOMETRY_INPUT)
        TEXTURE2D(RESTIR_GI_REUSE_BINDING_POSITION_ERROR_INPUT)
        TEXTURE2D(RESTIR_GI_REUSE_BINDING_SHARED_FLAGS_INPUT)
        TEXTURE2D(RESTIR_GI_REUSE_BINDING_SHARED_SURFACE_INDEX_INPUT)
        TEXTURE2D(RESTIR_GI_REUSE_BINDING_SUBSURFACE_DATA_INPUT)
        RW_TEXTURE2D(RESTIR_GI_REUSE_BINDING_LAST_GBUFFER)
        TEXTURE2DARRAY(RESTIR_GI_REUSE_BINDING_GRADIENTS_INPUT)
      END_PARAMETER()
    };

    PREWARM_SHADER_PIPELINE(ReSTIRGITemporalReuseShader);

    class ReSTIRGISpatialReuseShader : public ManagedShader
    {
      SHADER_SOURCE(ReSTIRGISpatialReuseShader, VK_SHADER_STAGE_COMPUTE_BIT, restir_gi_spatial_reuse)
      
      BINDLESS_ENABLED()

      BEGIN_PARAMETER()
        COMMON_RAYTRACING_BINDINGS
        
        // ReSTIR GI Data
        RW_STRUCTURED_BUFFER(RESTIR_GI_REUSE_BINDING_RESERVOIR_INPUT_OUTPUT)
        // GBuffer
        TEXTURE2D(RESTIR_GI_REUSE_BINDING_WORLD_SHADING_NORMAL_INPUT)
        TEXTURE2D(RESTIR_GI_REUSE_BINDING_PERCEPTUAL_ROUGHNESS_INPUT)
        TEXTURE2D(RESTIR_GI_REUSE_BINDING_HIT_DISTANCE_INPUT)
        TEXTURE2D(RESTIR_GI_REUSE_BINDING_ALBEDO_INPUT)
        TEXTURE2D(RESTIR_GI_REUSE_BINDING_BASE_REFLECTIVITY_INPUT)
        TEXTURE2D(RESTIR_GI_REUSE_BINDING_WORLD_POSITION_INPUT)
        TEXTURE2D(RESTIR_GI_REUSE_BINDING_PREV_WORLD_POSITION_INPUT)
        TEXTURE2D(RESTIR_GI_REUSE_BINDING_VIEW_DIRECTION_INPUT)
        TEXTURE2D(RESTIR_GI_REUSE_BINDING_CONE_RADIUS_INPUT)
        TEXTURE2D(RESTIR_GI_REUSE_BINDING_MVEC_INPUT)
        TEXTURE2D(RESTIR_GI_REUSE_BINDING_RADIANCE_INPUT)
        TEXTURE2D(RESTIR_GI_REUSE_BINDING_HIT_GEOMETRY_INPUT)
        TEXTURE2D(RESTIR_GI_REUSE_BINDING_POSITION_ERROR_INPUT)
        TEXTURE2D(RESTIR_GI_REUSE_BINDING_SHARED_FLAGS_INPUT)
        TEXTURE2D(RESTIR_GI_REUSE_BINDING_SHARED_SURFACE_INDEX_INPUT)
        TEXTURE2D(RESTIR_GI_REUSE_BINDING_SUBSURFACE_DATA_INPUT)
        RW_TEXTURE2D(RESTIR_GI_REUSE_BINDING_LAST_GBUFFER)
        TEXTURE2DARRAY(RESTIR_GI_REUSE_BINDING_GRADIENTS_INPUT)
      END_PARAMETER()
    };

    PREWARM_SHADER_PIPELINE(ReSTIRGISpatialReuseShader);

    class ReSTIRGIFinalShadingShader : public ManagedShader {
      SHADER_SOURCE(ReSTIRGIFinalShadingShader, VK_SHADER_STAGE_COMPUTE_BIT, restir_gi_final_shading)
      
      BINDLESS_ENABLED()

      BEGIN_PARAMETER()
        COMMON_RAYTRACING_BINDINGS

        TEXTURE2D(RESTIR_GI_FINAL_SHADING_BINDING_SHARED_FLAGS_INPUT)
        TEXTURE2D(RESTIR_GI_FINAL_SHADING_BINDING_SHARED_MATERIAL_DATA0_INPUT)
        TEXTURE2D(RESTIR_GI_FINAL_SHADING_BINDING_SHARED_MATERIAL_DATA1_INPUT)
        TEXTURE2D(RESTIR_GI_FINAL_SHADING_BINDING_SHARED_TEXTURE_COORD_INPUT)
        TEXTURE2D(RESTIR_GI_FINAL_SHADING_BINDING_SHARED_SURFACE_INDEX_INPUT)
        TEXTURE2D(RESTIR_GI_FINAL_SHADING_BINDING_SHARED_SUBSURFACE_DATA_INPUT)

        TEXTURE2D(RESTIR_GI_FINAL_SHADING_BINDING_PRIMARY_WORLD_SHADING_NORMAL_INPUT)
        TEXTURE2D(RESTIR_GI_FINAL_SHADING_BINDING_PRIMARY_WORLD_INTERPOLATED_NORMAL_INPUT)
        TEXTURE2D(RESTIR_GI_FINAL_SHADING_BINDING_PRIMARY_PERCEPTUAL_ROUGHNESS_INPUT)
        TEXTURE2D(RESTIR_GI_FINAL_SHADING_BINDING_PRIMARY_HIT_DISTANCE_INPUT)
        TEXTURE2D(RESTIR_GI_FINAL_SHADING_BINDING_PRIMARY_ALBEDO_INPUT)
        TEXTURE2D(RESTIR_GI_FINAL_SHADING_BINDING_PRIMARY_VIEW_DIRECTION_INPUT)
        TEXTURE2D(RESTIR_GI_FINAL_SHADING_BINDING_PRIMARY_CONE_RADIUS_INPUT)
        TEXTURE2D(RESTIR_GI_FINAL_SHADING_BINDING_PRIMARY_WORLD_POSITION_INPUT)
        TEXTURE2D(RESTIR_GI_FINAL_SHADING_BINDING_PRIMARY_POSITION_ERROR_INPUT)

        RW_TEXTURE2D(RESTIR_GI_FINAL_SHADING_BINDING_PRIMARY_BASE_REFLECTIVITY_INPUT_OUTPUT)
        RW_TEXTURE2D(RESTIR_GI_FINAL_SHADING_BINDING_PRIMARY_INDIRECT_DIFFUSE_RADIANCE_HIT_DISTANCE_INPUT_OUTPUT)
        RW_TEXTURE2D(RESTIR_GI_FINAL_SHADING_BINDING_PRIMARY_INDIRECT_SPECULAR_RADIANCE_HIT_DISTANCE_INPUT_OUTPUT)

        STRUCTURED_BUFFER(RESTIR_GI_FINAL_SHADING_BINDING_RESTIR_GI_RESERVOIR_OUTPUT)
        RW_TEXTURE2D(RESTIR_GI_FINAL_SHADING_BINDING_BSDF_FACTOR2_OUTPUT)
      END_PARAMETER()
    };

    PREWARM_SHADER_PIPELINE(ReSTIRGIFinalShadingShader);
  }

  DxvkReSTIRGIRayQuery::DxvkReSTIRGIRayQuery(DxvkDevice* device): RtxPass(device) {
  }

  void DxvkReSTIRGIRayQuery::showImguiSettings() {
    ImGui::Checkbox("Temporal Reuse", &useTemporalReuseObject());
    ImGui::Checkbox("Spatial Reuse", &useSpatialReuseObject());
    restirGIBiasCorrectionCombo.getKey(&biasCorrectionModeObject());
    ImGui::DragFloat("Pairwise MIS Central Weight", &pairwiseMISCentralWeightObject(), 0.01f, 0.01f, 2.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
    ImGui::Checkbox("Temporal Bias Correction", &useTemporalBiasCorrectionObject());
    ImGui::Checkbox("Temporal Jacobian", &useTemporalJacobianObject());
    ImGui::Combo("MIS", &misModeObject(), "None\0Roughness\0Parallax\0");
    ImGui::DragFloat("MIS Roughness Threshold", &misRoughnessObject(), 0.001f, 0.f, 1.f, "%.3f");
    ImGui::DragFloat("MIS Parallax Amount", &parallaxAmountObject(), 0.001f, 0.f, 1.f, "%.3f");
    ImGui::Checkbox("Final Visibility", &useFinalVisibilityObject());
    ImGui::Checkbox("Reflection Reprojection", &useReflectionReprojectionObject());
    ImGui::DragFloat("Reflection Min Parallax", &reflectionMinParallaxObject(), 0.1f, 0.f, 50.f, "%.3f");
    ImGui::Checkbox("Virtual Sample", &useVirtualSampleObject());
    ImGui::DragFloat("Virtual Sample Luminance Threshold", &virtualSampleLuminanceThresholdObject(), 0.01f, 0.0f, 1000.f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
    ImGui::DragFloat("Virtual Sample Roughness Threshold", &virtualSampleRoughnessThresholdObject(), 0.01f, 0.0f, 1.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
    ImGui::DragFloat("Virtual Sample Specular Threshold", &virtualSampleSpecularThresholdObject(), 0.01f, 0.0f, 1.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
    ImGui::Checkbox("Demodulate Target Function", &useDemodulatedTargetFunctionObject());
    ImGui::Checkbox("Permutation Sampling", &usePermutationSamplingObject());
    ImGui::Combo("Light Stealing", &useSampleStealingObject(), "None\0Steal Sample\0Steal Pixel");
    ImGui::Checkbox("Steal Boundary Pixels When Outside Of Screen", &stealBoundaryPixelSamplesWhenOutsideOfScreenObject());
    ImGui::Checkbox("Boiling Filter", &useBoilingFilterObject());
    ImGui::DragFloat("Boiling Filter Min Threshold", &boilingFilterMinThresholdObject(), 0.01f, 0.0f, FLT_MAX, "%.3f", ImGuiSliderFlags_AlwaysClamp);
    ImGui::DragFloat("Boiling Filter Max Threshold", &boilingFilterMaxThresholdObject(), 0.01f, 0.0f, FLT_MAX, "%.3f", ImGuiSliderFlags_AlwaysClamp);
    ImGui::DragFloat("Boiling Filter Remove Reservoir Threshold", &boilingFilterRemoveReservoirThresholdObject(), 0.01f, 0.0f, FLT_MAX, "%.3f", ImGuiSliderFlags_AlwaysClamp);
    ImGui::Checkbox("Use Adaptive Temporal History", &useAdaptiveTemporalHistoryObject());
    if(useAdaptiveTemporalHistory())
      ImGui::DragInt("Temporal History Length (ms)", &temporalAdaptiveHistoryLengthMsObject(), 1.f, 1, 3000, "%d", ImGuiSliderFlags_AlwaysClamp);
    else
      ImGui::DragInt("Temporal History Length (frame)", &temporalFixedHistoryLengthObject(), 1.f, 1, 500, "%d", ImGuiSliderFlags_AlwaysClamp);
    ImGui::DragInt("Permutation Sampling Size", &permutationSamplingSizeObject(), 0.1f, 1, 8, "%d", ImGuiSliderFlags_AlwaysClamp);
    ImGui::Checkbox("Discard Enlarged Pixels", &useDiscardEnlargedPixelsObject());
    ImGui::DragFloat("Firefly Threshold", &fireflyThresholdObject(), 0.01f, 1.f, 5000.f, "%.1f");
    ImGui::DragFloat("Roughness Clamp", &roughnessClampObject(), 0.001f, 0.f, 1.f, "%.3f");

    ImGui::Checkbox("Sample Validation", &useSampleValidationObject());
    ImGui::DragFloat("Sample Validation Threshold", &sampleValidationThresholdObject(), 0.001f, 0.f, 1.f, "%.3f");
  }

  void DxvkReSTIRGIRayQuery::dispatch(RtxContext* ctx, const Resources::RaytracingOutput& rtOutput) {

    if (!shouldDispatch())
      return;

    ScopedGpuProfileZone(ctx, "ReSTIR GI");

    const auto& numRaysExtent = rtOutput.m_compositeOutputExtent;
    VkExtent3D workgroups = util::computeBlockCount(numRaysExtent, VkExtent3D { 16, 8, 1 });

    ctx->bindCommonRayTracingResources(rtOutput);

    {
      ScopedGpuProfileZone(ctx, "ReSTIR GI Temporal Reuse");

      ctx->bindResourceBuffer(RESTIR_GI_REUSE_BINDING_RESERVOIR_INPUT_OUTPUT, DxvkBufferSlice(rtOutput.m_restirGIReservoirBuffer, 0, rtOutput.m_restirGIReservoirBuffer->info().size));
      ctx->bindResourceView(RESTIR_GI_REUSE_BINDING_WORLD_SHADING_NORMAL_INPUT, rtOutput.m_primaryWorldShadingNormal.view, nullptr);
      ctx->bindResourceView(RESTIR_GI_REUSE_BINDING_PERCEPTUAL_ROUGHNESS_INPUT, rtOutput.m_primaryPerceptualRoughness.view, nullptr);
      ctx->bindResourceView(RESTIR_GI_REUSE_BINDING_HIT_DISTANCE_INPUT, rtOutput.m_primaryHitDistance.view, nullptr);
      ctx->bindResourceView(RESTIR_GI_REUSE_BINDING_ALBEDO_INPUT, rtOutput.m_primaryAlbedo.view, nullptr);
      ctx->bindResourceView(RESTIR_GI_REUSE_BINDING_BASE_REFLECTIVITY_INPUT, rtOutput.m_primaryBaseReflectivity.view(Resources::AccessType::Read), nullptr);
      ctx->bindResourceView(RESTIR_GI_REUSE_BINDING_WORLD_POSITION_INPUT, rtOutput.getCurrentPrimaryWorldPositionWorldTriangleNormal().view, nullptr);
      ctx->bindResourceView(RESTIR_GI_REUSE_BINDING_PREV_WORLD_POSITION_INPUT, rtOutput.getPreviousPrimaryWorldPositionWorldTriangleNormal().view, nullptr);
      ctx->bindResourceView(RESTIR_GI_REUSE_BINDING_VIEW_DIRECTION_INPUT, rtOutput.m_primaryViewDirection.view, nullptr);
      ctx->bindResourceView(RESTIR_GI_REUSE_BINDING_CONE_RADIUS_INPUT, rtOutput.m_primaryConeRadius.view, nullptr);
      ctx->bindResourceView(RESTIR_GI_REUSE_BINDING_LAST_GBUFFER, rtOutput.m_gbufferLast.view, nullptr);
      ctx->bindResourceView(RESTIR_GI_REUSE_BINDING_MVEC_INPUT, rtOutput.m_primaryVirtualMotionVector.view, nullptr);
      ctx->bindResourceView(RESTIR_GI_REUSE_BINDING_RADIANCE_INPUT, rtOutput.m_restirGIRadiance.view(Resources::AccessType::Read), nullptr);
      ctx->bindResourceView(RESTIR_GI_REUSE_BINDING_HIT_GEOMETRY_INPUT, rtOutput.m_restirGIHitGeometry.view, nullptr);
      ctx->bindResourceView(RESTIR_GI_REUSE_BINDING_POSITION_ERROR_INPUT, rtOutput.m_primaryPositionError.view, nullptr);
      ctx->bindResourceView(RESTIR_GI_REUSE_BINDING_SHARED_SURFACE_INDEX_INPUT, rtOutput.m_sharedSurfaceIndex.view, nullptr);
      ctx->bindResourceView(RESTIR_GI_REUSE_BINDING_SUBSURFACE_DATA_INPUT, rtOutput.m_sharedSubsurfaceData.view, nullptr);
      ctx->bindResourceView(RESTIR_GI_REUSE_BINDING_SHARED_FLAGS_INPUT, rtOutput.m_sharedFlags.view, nullptr);
      ctx->bindResourceView(RESTIR_GI_REUSE_BINDING_GRADIENTS_INPUT, rtOutput.m_rtxdiGradients.view, nullptr);

      ctx->bindShader(VK_SHADER_STAGE_COMPUTE_BIT, ReSTIRGITemporalReuseShader::getShader());
      ctx->dispatch(workgroups.width, workgroups.height, workgroups.depth);
    }

    {
      ScopedGpuProfileZone(ctx, "ReSTIR GI Spatial Reuse");

      ctx->bindResourceBuffer(RESTIR_GI_REUSE_BINDING_RESERVOIR_INPUT_OUTPUT, DxvkBufferSlice(rtOutput.m_restirGIReservoirBuffer, 0, rtOutput.m_restirGIReservoirBuffer->info().size));
      ctx->bindResourceView(RESTIR_GI_REUSE_BINDING_WORLD_SHADING_NORMAL_INPUT, rtOutput.m_primaryWorldShadingNormal.view, nullptr);
      ctx->bindResourceView(RESTIR_GI_REUSE_BINDING_PERCEPTUAL_ROUGHNESS_INPUT, rtOutput.m_primaryPerceptualRoughness.view, nullptr);
      ctx->bindResourceView(RESTIR_GI_REUSE_BINDING_HIT_DISTANCE_INPUT, rtOutput.m_primaryHitDistance.view, nullptr);
      ctx->bindResourceView(RESTIR_GI_REUSE_BINDING_ALBEDO_INPUT, rtOutput.m_primaryAlbedo.view, nullptr);
      ctx->bindResourceView(RESTIR_GI_REUSE_BINDING_BASE_REFLECTIVITY_INPUT, rtOutput.m_primaryBaseReflectivity.view(Resources::AccessType::Read), nullptr);
      ctx->bindResourceView(RESTIR_GI_REUSE_BINDING_WORLD_POSITION_INPUT, rtOutput.getCurrentPrimaryWorldPositionWorldTriangleNormal().view, nullptr);
      ctx->bindResourceView(RESTIR_GI_REUSE_BINDING_PREV_WORLD_POSITION_INPUT, rtOutput.getPreviousPrimaryWorldPositionWorldTriangleNormal().view, nullptr);
      ctx->bindResourceView(RESTIR_GI_REUSE_BINDING_VIEW_DIRECTION_INPUT, rtOutput.m_primaryViewDirection.view, nullptr);
      ctx->bindResourceView(RESTIR_GI_REUSE_BINDING_CONE_RADIUS_INPUT, rtOutput.m_primaryConeRadius.view, nullptr);
      ctx->bindResourceView(RESTIR_GI_REUSE_BINDING_LAST_GBUFFER, rtOutput.m_gbufferLast.view, nullptr);
      ctx->bindResourceView(RESTIR_GI_REUSE_BINDING_MVEC_INPUT, rtOutput.m_primaryVirtualMotionVector.view, nullptr);
      ctx->bindResourceView(RESTIR_GI_REUSE_BINDING_RADIANCE_INPUT, rtOutput.m_restirGIRadiance.view(Resources::AccessType::Read), nullptr);
      ctx->bindResourceView(RESTIR_GI_REUSE_BINDING_HIT_GEOMETRY_INPUT, rtOutput.m_restirGIHitGeometry.view, nullptr);
      ctx->bindResourceView(RESTIR_GI_REUSE_BINDING_POSITION_ERROR_INPUT, rtOutput.m_primaryPositionError.view, nullptr);
      ctx->bindResourceView(RESTIR_GI_REUSE_BINDING_SHARED_SURFACE_INDEX_INPUT, rtOutput.m_sharedSurfaceIndex.view, nullptr);
      ctx->bindResourceView(RESTIR_GI_REUSE_BINDING_SUBSURFACE_DATA_INPUT, rtOutput.m_sharedSubsurfaceData.view, nullptr);
      ctx->bindResourceView(RESTIR_GI_REUSE_BINDING_SHARED_FLAGS_INPUT, rtOutput.m_sharedFlags.view, nullptr);
      ctx->bindResourceView(RESTIR_GI_REUSE_BINDING_GRADIENTS_INPUT, rtOutput.m_rtxdiGradients.view, nullptr);

      ctx->bindShader(VK_SHADER_STAGE_COMPUTE_BIT, ReSTIRGISpatialReuseShader::getShader());
      ctx->dispatch(workgroups.width, workgroups.height, workgroups.depth);
    }

    workgroups = util::computeBlockCount(numRaysExtent, VkExtent3D { 8, 8, 1 });
    {
      ScopedGpuProfileZone(ctx, "ReSTIR GI Final Shading");
      ctx->bindCommonRayTracingResources(rtOutput);

      ctx->bindResourceView(RESTIR_GI_FINAL_SHADING_BINDING_SHARED_FLAGS_INPUT, rtOutput.m_sharedFlags.view, nullptr);
      ctx->bindResourceView(RESTIR_GI_FINAL_SHADING_BINDING_SHARED_MATERIAL_DATA0_INPUT, rtOutput.m_sharedMaterialData0.view, nullptr);
      ctx->bindResourceView(RESTIR_GI_FINAL_SHADING_BINDING_SHARED_MATERIAL_DATA1_INPUT, rtOutput.m_sharedMaterialData1.view, nullptr);
      ctx->bindResourceView(RESTIR_GI_FINAL_SHADING_BINDING_SHARED_TEXTURE_COORD_INPUT, rtOutput.m_sharedTextureCoord.view, nullptr);
      ctx->bindResourceView(RESTIR_GI_FINAL_SHADING_BINDING_SHARED_SURFACE_INDEX_INPUT, rtOutput.m_sharedSurfaceIndex.view, nullptr);
      ctx->bindResourceView(RESTIR_GI_FINAL_SHADING_BINDING_SHARED_SUBSURFACE_DATA_INPUT, rtOutput.m_sharedSubsurfaceData.view, nullptr);

      ctx->bindResourceView(RESTIR_GI_FINAL_SHADING_BINDING_PRIMARY_WORLD_SHADING_NORMAL_INPUT, rtOutput.m_primaryWorldShadingNormal.view, nullptr);
      ctx->bindResourceView(RESTIR_GI_FINAL_SHADING_BINDING_PRIMARY_WORLD_INTERPOLATED_NORMAL_INPUT, rtOutput.m_primaryWorldInterpolatedNormal.view, nullptr);
      ctx->bindResourceView(RESTIR_GI_FINAL_SHADING_BINDING_PRIMARY_PERCEPTUAL_ROUGHNESS_INPUT, rtOutput.m_primaryPerceptualRoughness.view, nullptr);
      ctx->bindResourceView(RESTIR_GI_FINAL_SHADING_BINDING_PRIMARY_HIT_DISTANCE_INPUT, rtOutput.m_primaryHitDistance.view, nullptr);
      ctx->bindResourceView(RESTIR_GI_FINAL_SHADING_BINDING_PRIMARY_ALBEDO_INPUT, rtOutput.m_primaryAlbedo.view, nullptr);
      ctx->bindResourceView(RESTIR_GI_FINAL_SHADING_BINDING_PRIMARY_VIEW_DIRECTION_INPUT, rtOutput.m_primaryViewDirection.view, nullptr);
      ctx->bindResourceView(RESTIR_GI_FINAL_SHADING_BINDING_PRIMARY_CONE_RADIUS_INPUT, rtOutput.m_primaryConeRadius.view, nullptr);
      ctx->bindResourceView(RESTIR_GI_FINAL_SHADING_BINDING_PRIMARY_WORLD_POSITION_INPUT, rtOutput.getCurrentPrimaryWorldPositionWorldTriangleNormal().view, nullptr);
      ctx->bindResourceView(RESTIR_GI_FINAL_SHADING_BINDING_PRIMARY_POSITION_ERROR_INPUT, rtOutput.m_primaryPositionError.view, nullptr);

      ctx->bindResourceView(RESTIR_GI_FINAL_SHADING_BINDING_PRIMARY_BASE_REFLECTIVITY_INPUT_OUTPUT, rtOutput.m_primaryBaseReflectivity.view(Resources::AccessType::ReadWrite), nullptr);
      ctx->bindResourceView(RESTIR_GI_FINAL_SHADING_BINDING_PRIMARY_INDIRECT_DIFFUSE_RADIANCE_HIT_DISTANCE_INPUT_OUTPUT, rtOutput.m_primaryIndirectDiffuseRadiance.view(Resources::AccessType::ReadWrite), nullptr);
      ctx->bindResourceView(RESTIR_GI_FINAL_SHADING_BINDING_PRIMARY_INDIRECT_SPECULAR_RADIANCE_HIT_DISTANCE_INPUT_OUTPUT, rtOutput.m_primaryIndirectSpecularRadiance.view(Resources::AccessType::ReadWrite), nullptr);

      ctx->bindResourceBuffer(RESTIR_GI_FINAL_SHADING_BINDING_RESTIR_GI_RESERVOIR_OUTPUT, DxvkBufferSlice(rtOutput.m_restirGIReservoirBuffer, 0, rtOutput.m_restirGIReservoirBuffer->info().size));
      ctx->bindResourceView(RESTIR_GI_FINAL_SHADING_BINDING_BSDF_FACTOR2_OUTPUT, rtOutput.m_bsdfFactor2.view, nullptr);

      ctx->bindShader(VK_SHADER_STAGE_COMPUTE_BIT, ReSTIRGIFinalShadingShader::getShader());
      ctx->dispatch(workgroups.width, workgroups.height, workgroups.depth);
    }
  }
}
