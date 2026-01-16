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
#include "rtx_restir_gi_rayquery.h"
#include "rtx_rtxdi_rayquery.h"
#include "dxvk_device.h"
#include "rtx_shader_manager.h"

#include "rtx/pass/common_binding_indices.h"
#include "rtx/pass/rtxdi/restir_gi_reuse_binding_indices.h"
#include "rtx/pass/rtxdi/restir_gi_final_shading_binding_indices.h"
#include <rtxdi/RtxdiParameters.h>
#include "rtx/pass/integrate/integrate_indirect_binding_indices.h"
#include "rtx/pass/integrate/integrate_nee_binding_indices.h"
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
    RemixGui::ComboWithKey<ReSTIRGIBiasCorrection> restirGIBiasCorrectionCombo = RemixGui::ComboWithKey<ReSTIRGIBiasCorrection>(
    "ReSTIR GI Spatial Bias Correction",
    RemixGui::ComboWithKey<ReSTIRGIBiasCorrection>::ComboEntries { {
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

        // Inputs
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
        TEXTURE2D(RESTIR_GI_REUSE_BINDING_SUBSURFACE_DIFFUSION_PROFILE_DATA_INPUT)
        TEXTURE2DARRAY(RESTIR_GI_REUSE_BINDING_GRADIENTS_INPUT)

        // Inputs / Outputs
        RW_STRUCTURED_BUFFER(RESTIR_GI_REUSE_BINDING_RESERVOIR_INPUT_OUTPUT)
        RW_TEXTURE2D(RESTIR_GI_REUSE_BINDING_LAST_GBUFFER_INPUT_OUTPUT)

      END_PARAMETER()
    };

    PREWARM_SHADER_PIPELINE(ReSTIRGITemporalReuseShader);

    class ReSTIRGISpatialReuseShader : public ManagedShader
    {
      SHADER_SOURCE(ReSTIRGISpatialReuseShader, VK_SHADER_STAGE_COMPUTE_BIT, restir_gi_spatial_reuse)
      
      BINDLESS_ENABLED()

      BEGIN_PARAMETER()
        COMMON_RAYTRACING_BINDINGS
        
        // Inputs
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
        TEXTURE2D(RESTIR_GI_REUSE_BINDING_SUBSURFACE_DIFFUSION_PROFILE_DATA_INPUT)
        TEXTURE2DARRAY(RESTIR_GI_REUSE_BINDING_GRADIENTS_INPUT)

        // Inputs / Outputs
        RW_STRUCTURED_BUFFER(RESTIR_GI_REUSE_BINDING_RESERVOIR_INPUT_OUTPUT)
        RW_TEXTURE2D(RESTIR_GI_REUSE_BINDING_LAST_GBUFFER_INPUT_OUTPUT)

      END_PARAMETER()
    };

    PREWARM_SHADER_PIPELINE(ReSTIRGISpatialReuseShader);

    class ReSTIRGIFinalShadingShader : public ManagedShader {
      SHADER_SOURCE(ReSTIRGIFinalShadingShader, VK_SHADER_STAGE_COMPUTE_BIT, restir_gi_final_shading)
      
      BINDLESS_ENABLED()

      BEGIN_PARAMETER()
        COMMON_RAYTRACING_BINDINGS

        // Inputs
        TEXTURE2D(RESTIR_GI_FINAL_SHADING_BINDING_SHARED_FLAGS_INPUT)
        TEXTURE2D(RESTIR_GI_FINAL_SHADING_BINDING_SHARED_MATERIAL_DATA0_INPUT)
        TEXTURE2D(RESTIR_GI_FINAL_SHADING_BINDING_SHARED_MATERIAL_DATA1_INPUT)
        TEXTURE2D(RESTIR_GI_FINAL_SHADING_BINDING_SHARED_TEXTURE_COORD_INPUT)
        TEXTURE2D(RESTIR_GI_FINAL_SHADING_BINDING_SHARED_SURFACE_INDEX_INPUT)
        TEXTURE2D(RESTIR_GI_FINAL_SHADING_BINDING_SHARED_SUBSURFACE_DATA_INPUT)
        TEXTURE2D(RESTIR_GI_FINAL_SHADING_BINDING_SHARED_SUBSURFACE_DIFFUSION_PROFILE_DATA_INPUT)

        TEXTURE2D(RESTIR_GI_FINAL_SHADING_BINDING_PRIMARY_WORLD_SHADING_NORMAL_INPUT)
        TEXTURE2D(RESTIR_GI_FINAL_SHADING_BINDING_PRIMARY_WORLD_INTERPOLATED_NORMAL_INPUT)
        TEXTURE2D(RESTIR_GI_FINAL_SHADING_BINDING_PRIMARY_PERCEPTUAL_ROUGHNESS_INPUT)
        TEXTURE2D(RESTIR_GI_FINAL_SHADING_BINDING_PRIMARY_HIT_DISTANCE_INPUT)
        TEXTURE2D(RESTIR_GI_FINAL_SHADING_BINDING_PRIMARY_ALBEDO_INPUT)
        TEXTURE2D(RESTIR_GI_FINAL_SHADING_BINDING_PRIMARY_VIEW_DIRECTION_INPUT)
        TEXTURE2D(RESTIR_GI_FINAL_SHADING_BINDING_PRIMARY_CONE_RADIUS_INPUT)
        TEXTURE2D(RESTIR_GI_FINAL_SHADING_BINDING_PRIMARY_WORLD_POSITION_INPUT)
        TEXTURE2D(RESTIR_GI_FINAL_SHADING_BINDING_PRIMARY_POSITION_ERROR_INPUT)

        // Inputs / Outputs
        RW_TEXTURE2D(RESTIR_GI_FINAL_SHADING_BINDING_PRIMARY_BASE_REFLECTIVITY_INPUT_OUTPUT)
        RW_TEXTURE2D(RESTIR_GI_FINAL_SHADING_BINDING_PRIMARY_INDIRECT_DIFFUSE_RADIANCE_HIT_DISTANCE_INPUT_OUTPUT)
        RW_TEXTURE2D(RESTIR_GI_FINAL_SHADING_BINDING_PRIMARY_INDIRECT_SPECULAR_RADIANCE_HIT_DISTANCE_INPUT_OUTPUT)

        // Outputs
        RW_STRUCTURED_BUFFER(RESTIR_GI_FINAL_SHADING_BINDING_RESTIR_GI_RESERVOIR_OUTPUT)
        RW_TEXTURE2D(RESTIR_GI_FINAL_SHADING_BINDING_BSDF_FACTOR2_OUTPUT)
      END_PARAMETER()
    };

    PREWARM_SHADER_PIPELINE(ReSTIRGIFinalShadingShader);
  }

  DxvkReSTIRGIRayQuery::DxvkReSTIRGIRayQuery(DxvkDevice* device): RtxPass(device) {
  }

  void DxvkReSTIRGIRayQuery::showImguiSettings() {
    RemixGui::Checkbox("Temporal Reuse", &useTemporalReuseObject());
    RemixGui::Checkbox("Spatial Reuse", &useSpatialReuseObject());
    restirGIBiasCorrectionCombo.getKey(&biasCorrectionModeObject());
    RemixGui::DragFloat("Pairwise MIS Central Weight", &pairwiseMISCentralWeightObject(), 0.01f, 0.01f, 2.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
    RemixGui::Checkbox("Temporal Bias Correction", &useTemporalBiasCorrectionObject());
    RemixGui::Checkbox("Temporal Jacobian", &useTemporalJacobianObject());
    RemixGui::Combo("MIS", &misModeObject(), "None\0Roughness\0Parallax\0");
    RemixGui::DragFloat("MIS Roughness Threshold", &misRoughnessObject(), 0.001f, 0.f, 1.f, "%.3f");
    RemixGui::DragFloat("MIS Parallax Amount", &parallaxAmountObject(), 0.001f, 0.f, 1.f, "%.3f");
    RemixGui::Checkbox("Final Visibility", &useFinalVisibilityObject());
    RemixGui::Checkbox("Reflection Reprojection", &useReflectionReprojectionObject());
    RemixGui::DragFloat("Reflection Min Parallax", &reflectionMinParallaxObject(), 0.1f, 0.f, 50.f, "%.3f");
    RemixGui::Checkbox("Virtual Sample", &useVirtualSampleObject());
    RemixGui::DragFloat("Virtual Sample Luminance Threshold", &virtualSampleLuminanceThresholdObject(), 0.01f, 0.0f, 1000.f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
    RemixGui::DragFloat("Virtual Sample Roughness Threshold", &virtualSampleRoughnessThresholdObject(), 0.01f, 0.0f, 1.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
    RemixGui::DragFloat("Virtual Sample Specular Threshold", &virtualSampleSpecularThresholdObject(), 0.01f, 0.0f, 1.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
    RemixGui::DragFloat("Virtual Sample Max Distance Ratio", &virtualSampleMaxDistanceRatioObject(), 0.01f, 0.0f, 100.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
    RemixGui::Checkbox("Demodulate Target Function", &useDemodulatedTargetFunctionObject());
    RemixGui::Checkbox("Permutation Sampling", &usePermutationSamplingObject());
    RemixGui::Checkbox("DLSS-RR Compatibility Mode", &useDLSSRRCompatibilityModeObject());
    RemixGui::DragInt("DLSS-RR Compatible Temporal Randomization Radius", &DLSSRRTemporalRandomizationRadiusObject(), 1.f, 1, 160, "%d", ImGuiSliderFlags_AlwaysClamp);
    RemixGui::Combo("Light Stealing", &useSampleStealingObject(), "None\0Steal Sample\0Steal Pixel");
    RemixGui::DragFloat("Light Stealing Jitter", &sampleStealingJitterObject(), 0.01f, 0.0f, 20.f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
    RemixGui::Checkbox("Steal Boundary Pixels When Outside Of Screen", &stealBoundaryPixelSamplesWhenOutsideOfScreenObject());
    RemixGui::Checkbox("Boiling Filter", &useBoilingFilterObject());
    RemixGui::DragFloat("Boiling Filter Min Threshold", &boilingFilterMinThresholdObject(), 0.01f, 0.0f, FLT_MAX, "%.3f", ImGuiSliderFlags_AlwaysClamp);
    RemixGui::DragFloat("Boiling Filter Max Threshold", &boilingFilterMaxThresholdObject(), 0.01f, 0.0f, FLT_MAX, "%.3f", ImGuiSliderFlags_AlwaysClamp);
    RemixGui::DragFloat("Boiling Filter Remove Reservoir Threshold", &boilingFilterRemoveReservoirThresholdObject(), 0.01f, 0.0f, FLT_MAX, "%.3f", ImGuiSliderFlags_AlwaysClamp);
    RemixGui::Checkbox("Use Adaptive Temporal History", &useAdaptiveTemporalHistoryObject());
    if (useAdaptiveTemporalHistory()) {
      RemixGui::DragInt("Temporal History Length (ms)", &temporalAdaptiveHistoryLengthMsObject(), 1.f, 1, 3000, "%d", ImGuiSliderFlags_AlwaysClamp);
    } else {
      RemixGui::DragInt("Temporal History Length (frame)", &temporalFixedHistoryLengthObject(), 1.f, 1, 500, "%d", ImGuiSliderFlags_AlwaysClamp);
    }
    RemixGui::DragInt("Permutation Sampling Size", &permutationSamplingSizeObject(), 0.1f, 1, 8, "%d", ImGuiSliderFlags_AlwaysClamp);
    RemixGui::Checkbox("Discard Enlarged Pixels", &useDiscardEnlargedPixelsObject());
    RemixGui::DragFloat("History Discard Strength", &historyDiscardStrengthObject(), 0.01f, 0.f, 50.f, "%.1f");
    RemixGui::DragFloat("Firefly Threshold", &fireflyThresholdObject(), 0.01f, 1.f, 5000.f, "%.1f");
    RemixGui::DragFloat("Roughness Clamp", &roughnessClampObject(), 0.001f, 0.f, 1.f, "%.3f");
    RemixGui::Checkbox("Validate Lighting Change", &validateLightingChangeObject());
    RemixGui::DragFloat("Lighting Change Threshold", &lightingValidationThresholdObject(), 0.001f, 0.f, 1.f, "%.3f");
    RemixGui::Checkbox("Validate Visibility Change", &validateVisibilityChangeObject());
    RemixGui::DragFloat("Visibility Length Threshold", &visibilityValidationRangeObject(), 0.001f, 0.f, 10.f, "%.3f");
  }


  void DxvkReSTIRGIRayQuery::setToNRDPreset() {
    // Less aggressive boiling filter to keep more samples
    boilingFilterMinThreshold.setDeferred(10.0f);
    boilingFilterMaxThreshold.setDeferred(20.0f);
    historyDiscardStrength.setDeferred(0.0f);
    boilingFilterRemoveReservoirThreshold.setDeferred(62.f);

    // Weaken specular light at corners to reduce noise
    useVirtualSample.setDeferred(true);
    virtualSampleMaxDistanceRatio.setDeferred(0.0f);

    // Improve performance when stealing samples
    stealBoundaryPixelSamplesWhenOutsideOfScreen.setDeferred(true);
    useSampleStealing.setDeferred(ReSTIRGISampleStealing::StealPixel);
    sampleStealingJitter.setDeferred(0.0f);

    // No special handling to object movement
    validateVisibilityChange.setDeferred(false);

    // Legacy temporal reprojection
    useDLSSRRCompatibilityMode.setDeferred(false);
  }

  void DxvkReSTIRGIRayQuery::setToRayReconstructionPreset() {
    // More aggressive boiling filter to reduce sample coherency
    boilingFilterMinThreshold.setDeferred(15.0f);
    boilingFilterMaxThreshold.setDeferred(20.0f);
    historyDiscardStrength.setDeferred(10.0f);
    boilingFilterRemoveReservoirThreshold.setDeferred(30.f);

    // Preserve more specular light details at corners
    useVirtualSample.setDeferred(false);
    virtualSampleMaxDistanceRatio.setDeferred(0.5f);

    // Better specular light during camera movement
    useReflectionReprojection.setDeferred(true);

    // More stable signal
    useAdaptiveTemporalHistory.setDeferred(false);

    // Reduce sample coherency and improve sample quality when stealing samples 
    stealBoundaryPixelSamplesWhenOutsideOfScreen.setDeferred(true);
    useSampleStealing.setDeferred(ReSTIRGISampleStealing::StealSample);
    sampleStealingJitter.setDeferred(3.0);

    // More responsive to object movement
    validateVisibilityChange.setDeferred(true);

    // Randomize temporal reprojection to reduce coherency
    useDLSSRRCompatibilityMode.setDeferred(true);
  }

  void DxvkReSTIRGIRayQuery::bindIntegrateIndirectPathTracingResources(RtxContext& ctx) {

    ctx.bindResourceView(INTEGRATE_INDIRECT_BINDING_RESTIR_GI_HIT_GEOMETRY_OUTPUT, m_restirGIHitGeometry.view, nullptr);

    // Aliased resource methods must not be called when the resource is invalid
    if (isActive()) {
      ctx.bindResourceBuffer(INTEGRATE_INDIRECT_BINDING_RESTIR_GI_RESERVOIR_OUTPUT, DxvkBufferSlice(m_restirGIReservoirBuffer));
      ctx.bindResourceView(INTEGRATE_INDIRECT_BINDING_RESTIR_GI_RADIANCE_OUTPUT, m_restirGIRadiance.view(Resources::AccessType::Write), nullptr);
    } else {
      ctx.bindResourceBuffer(INTEGRATE_INDIRECT_BINDING_RESTIR_GI_RESERVOIR_OUTPUT, DxvkBufferSlice(nullptr, 0, 0));
      ctx.bindResourceView(INTEGRATE_INDIRECT_BINDING_RESTIR_GI_RADIANCE_OUTPUT, nullptr, nullptr);
    }
  }

  void DxvkReSTIRGIRayQuery::bindIntegrateIndirectNeeResources(RtxContext& ctx) {
    if (isActive()) {
      ctx.bindResourceBuffer(INTEGRATE_NEE_BINDING_RESTIR_GI_RESERVOIR_OUTPUT, DxvkBufferSlice(m_restirGIReservoirBuffer));
    } else {
      ctx.bindResourceBuffer(INTEGRATE_NEE_BINDING_RESTIR_GI_RESERVOIR_OUTPUT, DxvkBufferSlice(nullptr, 0, 0));
    }
  }

  const Resources::Resource& DxvkReSTIRGIRayQuery::getBsdfFactor2() const {
    return m_bsdfFactor2;
  }

  const Resources::AliasedResource& DxvkReSTIRGIRayQuery::getLastCompositeOutput() const {
    return m_lastCompositeOutput;
  }

  bool DxvkReSTIRGIRayQuery::isEnabled() const {
    return RtxOptions::useReSTIRGI();
  }

  void DxvkReSTIRGIRayQuery::createDownscaledResource(
    Rc<DxvkContext>& ctx,
    const VkExtent3D& downscaledExtent) {

    const Resources::RaytracingOutput& rtOutput = ctx->getCommonObjects()->getResources().getRaytracingOutput();

    int numReservoirBuffer = 3;
    int reservoirSize = sizeof(ReSTIRGI_PackedReservoir);
    int renderWidthBlocks = (downscaledExtent.width + RTXDI_RESERVOIR_BLOCK_SIZE - 1) / RTXDI_RESERVOIR_BLOCK_SIZE;
    int renderHeightBlocks = (downscaledExtent.height + RTXDI_RESERVOIR_BLOCK_SIZE - 1) / RTXDI_RESERVOIR_BLOCK_SIZE;
    int reservoirBufferPixels = renderWidthBlocks * renderHeightBlocks * RTXDI_RESERVOIR_BLOCK_SIZE * RTXDI_RESERVOIR_BLOCK_SIZE;

    DxvkBufferCreateInfo bufferInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    bufferInfo.usage = VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    bufferInfo.stages = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    bufferInfo.access = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    bufferInfo.size = reservoirBufferPixels * numReservoirBuffer * reservoirSize;
    m_restirGIReservoirBuffer = ctx->getDevice()->createBuffer(bufferInfo, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, DxvkMemoryStats::Category::RTXBuffer, "Restir GI Reservoir Buffer");

    m_restirGIRadiance = Resources::AliasedResource(rtOutput.m_compositeOutput, ctx, downscaledExtent, VK_FORMAT_R16G16B16A16_SFLOAT, "ReSTIR GI Radiance");
    m_restirGIHitGeometry = Resources::createImageResource(ctx, "ReSTIR GI Hit Geometry", downscaledExtent, VK_FORMAT_R32G32B32A32_SFLOAT);

    m_bsdfFactor2 = Resources::createImageResource(ctx, "bsdf factor 2", downscaledExtent, VK_FORMAT_R16G16_SFLOAT);
    m_lastCompositeOutput = Resources::AliasedResource(ctx, downscaledExtent, VK_FORMAT_R16G16B16A16_SFLOAT, "Last Composite Output");
  }

  void DxvkReSTIRGIRayQuery::releaseDownscaledResource() {
    m_restirGIReservoirBuffer = nullptr;
    m_restirGIRadiance.reset();
    m_restirGIHitGeometry.reset();
    m_bsdfFactor2.reset();
    m_lastCompositeOutput.reset();
  }

  void DxvkReSTIRGIRayQuery::dispatch(RtxContext* ctx, const Resources::RaytracingOutput& rtOutput) {

    if (!isActive()) {
      return;
    }

    ScopedGpuProfileZone(ctx, "ReSTIR GI");

    const uint32_t frameIdx = ctx->getDevice()->getCurrentFrameId();
    const auto& numRaysExtent = rtOutput.m_compositeOutputExtent;
    VkExtent3D workgroups = util::computeBlockCount(numRaysExtent, VkExtent3D { 16, 8, 1 });

    ctx->bindCommonRayTracingResources(rtOutput);

    {
      ScopedGpuProfileZone(ctx, "ReSTIR GI Temporal Reuse");
      ctx->setFramePassStage(RtxFramePassStage::ReSTIR_GI_TemporalReuse);

      // Inputs
      ctx->bindResourceView(RESTIR_GI_REUSE_BINDING_WORLD_SHADING_NORMAL_INPUT, rtOutput.m_primaryWorldShadingNormal.view, nullptr);
      ctx->bindResourceView(RESTIR_GI_REUSE_BINDING_PERCEPTUAL_ROUGHNESS_INPUT, rtOutput.m_primaryPerceptualRoughness.view, nullptr);
      ctx->bindResourceView(RESTIR_GI_REUSE_BINDING_HIT_DISTANCE_INPUT, rtOutput.m_primaryHitDistance.view, nullptr);
      ctx->bindResourceView(RESTIR_GI_REUSE_BINDING_ALBEDO_INPUT, rtOutput.m_primaryAlbedo.view, nullptr);
      ctx->bindResourceView(RESTIR_GI_REUSE_BINDING_BASE_REFLECTIVITY_INPUT, rtOutput.m_primaryBaseReflectivity.view(Resources::AccessType::Read), nullptr);
      ctx->bindResourceView(RESTIR_GI_REUSE_BINDING_WORLD_POSITION_INPUT, rtOutput.getCurrentPrimaryWorldPositionWorldTriangleNormal().view(Resources::AccessType::Read), nullptr);
      ctx->bindResourceView(RESTIR_GI_REUSE_BINDING_PREV_WORLD_POSITION_INPUT, rtOutput.getPreviousPrimaryWorldPositionWorldTriangleNormal().view(Resources::AccessType::Read, rtOutput.getPreviousPrimaryWorldPositionWorldTriangleNormal().matchesWriteFrameIdx(frameIdx - 1)), nullptr);
      ctx->bindResourceView(RESTIR_GI_REUSE_BINDING_VIEW_DIRECTION_INPUT, rtOutput.m_primaryViewDirection.view, nullptr);
      ctx->bindResourceView(RESTIR_GI_REUSE_BINDING_CONE_RADIUS_INPUT, rtOutput.m_primaryConeRadius.view, nullptr);
      ctx->bindResourceView(RESTIR_GI_REUSE_BINDING_MVEC_INPUT, rtOutput.m_primaryVirtualMotionVector.view(Resources::AccessType::Read), nullptr);
      ctx->bindResourceView(RESTIR_GI_REUSE_BINDING_RADIANCE_INPUT, m_restirGIRadiance.view(Resources::AccessType::Read), nullptr);
      ctx->bindResourceView(RESTIR_GI_REUSE_BINDING_HIT_GEOMETRY_INPUT, m_restirGIHitGeometry.view, nullptr);
      ctx->bindResourceView(RESTIR_GI_REUSE_BINDING_POSITION_ERROR_INPUT, rtOutput.m_primaryPositionError.view, nullptr);
      ctx->bindResourceView(RESTIR_GI_REUSE_BINDING_SHARED_SURFACE_INDEX_INPUT, rtOutput.m_sharedSurfaceIndex.view(Resources::AccessType::Read), nullptr);
      ctx->bindResourceView(RESTIR_GI_REUSE_BINDING_SUBSURFACE_DATA_INPUT, rtOutput.m_sharedSubsurfaceData.view, nullptr);
      ctx->bindResourceView(RESTIR_GI_REUSE_BINDING_SUBSURFACE_DIFFUSION_PROFILE_DATA_INPUT, rtOutput.m_sharedSubsurfaceDiffusionProfileData.view, nullptr);
      ctx->bindResourceView(RESTIR_GI_REUSE_BINDING_SHARED_FLAGS_INPUT, rtOutput.m_sharedFlags.view, nullptr);
      ctx->bindResourceView(RESTIR_GI_REUSE_BINDING_GRADIENTS_INPUT, rtOutput.m_rtxdiGradients.view, nullptr);

      // Inputs / Outputs
      ctx->bindResourceBuffer(RESTIR_GI_REUSE_BINDING_RESERVOIR_INPUT_OUTPUT, DxvkBufferSlice(m_restirGIReservoirBuffer, 0, m_restirGIReservoirBuffer->info().size));
      ctx->bindResourceView(RESTIR_GI_REUSE_BINDING_LAST_GBUFFER_INPUT_OUTPUT, rtOutput.m_gbufferLast.view, nullptr);

      ctx->bindShader(VK_SHADER_STAGE_COMPUTE_BIT, ReSTIRGITemporalReuseShader::getShader());
      ctx->dispatch(workgroups.width, workgroups.height, workgroups.depth);
    }

    {
      ScopedGpuProfileZone(ctx, "ReSTIR GI Spatial Reuse");
      ctx->setFramePassStage(RtxFramePassStage::ReSTIR_GI_SpatialReuse);

      // Inputs
      ctx->bindResourceView(RESTIR_GI_REUSE_BINDING_WORLD_SHADING_NORMAL_INPUT, rtOutput.m_primaryWorldShadingNormal.view, nullptr);
      ctx->bindResourceView(RESTIR_GI_REUSE_BINDING_PERCEPTUAL_ROUGHNESS_INPUT, rtOutput.m_primaryPerceptualRoughness.view, nullptr);
      ctx->bindResourceView(RESTIR_GI_REUSE_BINDING_HIT_DISTANCE_INPUT, rtOutput.m_primaryHitDistance.view, nullptr);
      ctx->bindResourceView(RESTIR_GI_REUSE_BINDING_ALBEDO_INPUT, rtOutput.m_primaryAlbedo.view, nullptr);
      ctx->bindResourceView(RESTIR_GI_REUSE_BINDING_BASE_REFLECTIVITY_INPUT, rtOutput.m_primaryBaseReflectivity.view(Resources::AccessType::Read), nullptr);
      ctx->bindResourceView(RESTIR_GI_REUSE_BINDING_WORLD_POSITION_INPUT, rtOutput.getCurrentPrimaryWorldPositionWorldTriangleNormal().view(Resources::AccessType::Read), nullptr);
      ctx->bindResourceView(RESTIR_GI_REUSE_BINDING_PREV_WORLD_POSITION_INPUT, rtOutput.getPreviousPrimaryWorldPositionWorldTriangleNormal().view(Resources::AccessType::Read, rtOutput.getPreviousPrimaryWorldPositionWorldTriangleNormal().matchesWriteFrameIdx(frameIdx - 1)), nullptr);
      ctx->bindResourceView(RESTIR_GI_REUSE_BINDING_VIEW_DIRECTION_INPUT, rtOutput.m_primaryViewDirection.view, nullptr);
      ctx->bindResourceView(RESTIR_GI_REUSE_BINDING_CONE_RADIUS_INPUT, rtOutput.m_primaryConeRadius.view, nullptr);
      ctx->bindResourceView(RESTIR_GI_REUSE_BINDING_MVEC_INPUT, rtOutput.m_primaryVirtualMotionVector.view(Resources::AccessType::Read), nullptr);
      ctx->bindResourceView(RESTIR_GI_REUSE_BINDING_RADIANCE_INPUT, m_restirGIRadiance.view(Resources::AccessType::Read), nullptr);
      ctx->bindResourceView(RESTIR_GI_REUSE_BINDING_HIT_GEOMETRY_INPUT, m_restirGIHitGeometry.view, nullptr);
      ctx->bindResourceView(RESTIR_GI_REUSE_BINDING_POSITION_ERROR_INPUT, rtOutput.m_primaryPositionError.view, nullptr);
      ctx->bindResourceView(RESTIR_GI_REUSE_BINDING_SHARED_SURFACE_INDEX_INPUT, rtOutput.m_sharedSurfaceIndex.view(Resources::AccessType::Read), nullptr);
      ctx->bindResourceView(RESTIR_GI_REUSE_BINDING_SUBSURFACE_DATA_INPUT, rtOutput.m_sharedSubsurfaceData.view, nullptr);
      ctx->bindResourceView(RESTIR_GI_REUSE_BINDING_SUBSURFACE_DIFFUSION_PROFILE_DATA_INPUT, rtOutput.m_sharedSubsurfaceDiffusionProfileData.view, nullptr);
      ctx->bindResourceView(RESTIR_GI_REUSE_BINDING_SHARED_FLAGS_INPUT, rtOutput.m_sharedFlags.view, nullptr);
      ctx->bindResourceView(RESTIR_GI_REUSE_BINDING_GRADIENTS_INPUT, rtOutput.m_rtxdiGradients.view, nullptr);

      // Inputs / Outputs
      ctx->bindResourceBuffer(RESTIR_GI_REUSE_BINDING_RESERVOIR_INPUT_OUTPUT, DxvkBufferSlice(m_restirGIReservoirBuffer, 0, m_restirGIReservoirBuffer->info().size));
      ctx->bindResourceView(RESTIR_GI_REUSE_BINDING_LAST_GBUFFER_INPUT_OUTPUT, rtOutput.m_gbufferLast.view, nullptr);

      ctx->bindShader(VK_SHADER_STAGE_COMPUTE_BIT, ReSTIRGISpatialReuseShader::getShader());
      ctx->dispatch(workgroups.width, workgroups.height, workgroups.depth);
    }

    workgroups = util::computeBlockCount(numRaysExtent, VkExtent3D { 8, 8, 1 });
    {
      ScopedGpuProfileZone(ctx, "ReSTIR GI Final Shading");
      ctx->setFramePassStage(RtxFramePassStage::ReSTIR_GI_FinalShading);
      ctx->bindCommonRayTracingResources(rtOutput);

      ctx->bindResourceView(RESTIR_GI_FINAL_SHADING_BINDING_SHARED_FLAGS_INPUT, rtOutput.m_sharedFlags.view, nullptr);
      ctx->bindResourceView(RESTIR_GI_FINAL_SHADING_BINDING_SHARED_MATERIAL_DATA0_INPUT, rtOutput.m_sharedMaterialData0.view, nullptr);
      ctx->bindResourceView(RESTIR_GI_FINAL_SHADING_BINDING_SHARED_MATERIAL_DATA1_INPUT, rtOutput.m_sharedMaterialData1.view, nullptr);
      ctx->bindResourceView(RESTIR_GI_FINAL_SHADING_BINDING_SHARED_TEXTURE_COORD_INPUT, rtOutput.m_sharedTextureCoord.view, nullptr);
      ctx->bindResourceView(RESTIR_GI_FINAL_SHADING_BINDING_SHARED_SURFACE_INDEX_INPUT, rtOutput.m_sharedSurfaceIndex.view(Resources::AccessType::Read), nullptr);
      ctx->bindResourceView(RESTIR_GI_FINAL_SHADING_BINDING_SHARED_SUBSURFACE_DATA_INPUT, rtOutput.m_sharedSubsurfaceData.view, nullptr);
      ctx->bindResourceView(RESTIR_GI_FINAL_SHADING_BINDING_SHARED_SUBSURFACE_DIFFUSION_PROFILE_DATA_INPUT, rtOutput.m_sharedSubsurfaceDiffusionProfileData.view, nullptr);

      ctx->bindResourceView(RESTIR_GI_FINAL_SHADING_BINDING_PRIMARY_WORLD_SHADING_NORMAL_INPUT, rtOutput.m_primaryWorldShadingNormal.view, nullptr);
      ctx->bindResourceView(RESTIR_GI_FINAL_SHADING_BINDING_PRIMARY_WORLD_INTERPOLATED_NORMAL_INPUT, rtOutput.m_primaryWorldInterpolatedNormal.view, nullptr);
      ctx->bindResourceView(RESTIR_GI_FINAL_SHADING_BINDING_PRIMARY_PERCEPTUAL_ROUGHNESS_INPUT, rtOutput.m_primaryPerceptualRoughness.view, nullptr);
      ctx->bindResourceView(RESTIR_GI_FINAL_SHADING_BINDING_PRIMARY_HIT_DISTANCE_INPUT, rtOutput.m_primaryHitDistance.view, nullptr);
      ctx->bindResourceView(RESTIR_GI_FINAL_SHADING_BINDING_PRIMARY_ALBEDO_INPUT, rtOutput.m_primaryAlbedo.view, nullptr);
      ctx->bindResourceView(RESTIR_GI_FINAL_SHADING_BINDING_PRIMARY_VIEW_DIRECTION_INPUT, rtOutput.m_primaryViewDirection.view, nullptr);
      ctx->bindResourceView(RESTIR_GI_FINAL_SHADING_BINDING_PRIMARY_CONE_RADIUS_INPUT, rtOutput.m_primaryConeRadius.view, nullptr);
      ctx->bindResourceView(RESTIR_GI_FINAL_SHADING_BINDING_PRIMARY_WORLD_POSITION_INPUT, rtOutput.getCurrentPrimaryWorldPositionWorldTriangleNormal().view(Resources::AccessType::Read), nullptr);
      ctx->bindResourceView(RESTIR_GI_FINAL_SHADING_BINDING_PRIMARY_POSITION_ERROR_INPUT, rtOutput.m_primaryPositionError.view, nullptr);

      ctx->bindResourceView(RESTIR_GI_FINAL_SHADING_BINDING_PRIMARY_BASE_REFLECTIVITY_INPUT_OUTPUT, rtOutput.m_primaryBaseReflectivity.view(Resources::AccessType::ReadWrite), nullptr);
      ctx->bindResourceView(RESTIR_GI_FINAL_SHADING_BINDING_PRIMARY_INDIRECT_DIFFUSE_RADIANCE_HIT_DISTANCE_INPUT_OUTPUT, rtOutput.m_primaryIndirectDiffuseRadiance.view(Resources::AccessType::ReadWrite), nullptr);
      ctx->bindResourceView(RESTIR_GI_FINAL_SHADING_BINDING_PRIMARY_INDIRECT_SPECULAR_RADIANCE_HIT_DISTANCE_INPUT_OUTPUT, rtOutput.m_primaryIndirectSpecularRadiance.view(Resources::AccessType::ReadWrite), nullptr);

      ctx->bindResourceBuffer(RESTIR_GI_FINAL_SHADING_BINDING_RESTIR_GI_RESERVOIR_OUTPUT, DxvkBufferSlice(m_restirGIReservoirBuffer, 0, m_restirGIReservoirBuffer->info().size));
      ctx->bindResourceView(RESTIR_GI_FINAL_SHADING_BINDING_BSDF_FACTOR2_OUTPUT, m_bsdfFactor2.view, nullptr);

      ctx->bindShader(VK_SHADER_STAGE_COMPUTE_BIT, ReSTIRGIFinalShadingShader::getShader());
      ctx->dispatch(workgroups.width, workgroups.height, workgroups.depth);
    }
  }
}
