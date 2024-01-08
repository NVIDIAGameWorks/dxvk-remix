/*
* Copyright (c) 2021-2023, NVIDIA CORPORATION. All rights reserved.
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
#include "rtx_composite.h"
#include "dxvk_device.h"
#include "rtx/pass/composite/composite_binding_indices.h"
#include "rtx/pass/composite/composite_args.h"
#include "rtx/pass/raytrace_args.h"
#include "rtx_render/rtx_shader_manager.h"
#include <dxvk_scoped_annotation.h>
#include "rtx_imgui.h"
#include "rtx_context.h"
#include "rtx_options.h"

#include <rtx_shaders/composite.h>
#include <rtx_shaders/composite_alpha_blend.h>

constexpr ImGuiTreeNodeFlags collapsingHeaderClosedFlags = ImGuiTreeNodeFlags_CollapsingHeader;

namespace dxvk {

  // Defined within an unnamed namespace to ensure unique definition across binary
  namespace {

    class CompositeAlphaBlendShader : public ManagedShader {
      SHADER_SOURCE(CompositeAlphaBlendShader, VK_SHADER_STAGE_COMPUTE_BIT, composite_alpha_blend)

      BEGIN_PARAMETER()
        CONSTANT_BUFFER(COMPOSITE_CONSTANTS_INPUT)
        RW_TEXTURE2D(COMPOSITE_FINAL_OUTPUT)
        RW_TEXTURE2D(COMPOSITE_ALPHA_BLEND_RADIANCE_OUTPUT)
        RW_TEXTURE2D(COMPOSITE_LAST_FINAL_OUTPUT)
        RW_TEXTURE2D_READONLY(COMPOSITE_SHARED_FLAGS_INPUT)
        TEXTURE2D(COMPOSITE_SHARED_RADIANCE_RG_INPUT)
        TEXTURE2D(COMPOSITE_SHARED_RADIANCE_B_INPUT)
        RW_TEXTURE2D_READONLY(COMPOSITE_PRIMARY_ATTENUATION_INPUT)
        TEXTURE2D(COMPOSITE_PRIMARY_ALBEDO_INPUT)
        TEXTURE2D(COMPOSITE_PRIMARY_SPECULAR_ALBEDO_INPUT)
        TEXTURE2D(COMPOSITE_PRIMARY_VIRTUAL_WORLD_SHADING_NORMAL_INPUT)
        TEXTURE2D(COMPOSITE_PRIMARY_LINEAR_VIEW_Z_INPUT)
        RW_TEXTURE2D_READONLY(COMPOSITE_SECONDARY_ATTENUATION_INPUT)
        TEXTURE2D(COMPOSITE_SECONDARY_ALBEDO_INPUT)
        TEXTURE2D(COMPOSITE_SECONDARY_SPECULAR_ALBEDO_INPUT)
        TEXTURE2D(COMPOSITE_PRIMARY_DIRECT_DIFFUSE_RADIANCE_HIT_DISTANCE_INPUT)
        TEXTURE2D(COMPOSITE_PRIMARY_DIRECT_SPECULAR_RADIANCE_HIT_DISTANCE_INPUT)
        TEXTURE2D(COMPOSITE_PRIMARY_INDIRECT_DIFFUSE_RADIANCE_HIT_DISTANCE_INPUT)
        TEXTURE2D(COMPOSITE_PRIMARY_INDIRECT_SPECULAR_RADIANCE_HIT_DISTANCE_INPUT)
        TEXTURE2D(COMPOSITE_SECONDARY_COMBINED_DIFFUSE_RADIANCE_HIT_DISTANCE_INPUT)
        TEXTURE2D(COMPOSITE_SECONDARY_COMBINED_SPECULAR_RADIANCE_HIT_DISTANCE_INPUT)
        SAMPLER3D(COMPOSITE_VOLUME_PREINTEGRATED_RADIANCE_INPUT)
        SAMPLER3D(COMPOSITE_VOLUME_FILTERED_RADIANCE_INPUT)
        TEXTURE2D(COMPOSITE_BSDF_FACTOR_INPUT)
        TEXTURE2D(COMPOSITE_BSDF_FACTOR2_INPUT)
        TEXTURE2D(COMPOSITE_ALPHA_GBUFFER_INPUT)
        TEXTURE2DARRAY(COMPOSITE_BLUE_NOISE_TEXTURE)

        RW_TEXTURE2D(COMPOSITE_DEBUG_VIEW_OUTPUT)
      END_PARAMETER()
    };

    PREWARM_SHADER_PIPELINE(CompositeAlphaBlendShader);

    class CompositeShader : public ManagedShader {
      SHADER_SOURCE(CompositeShader, VK_SHADER_STAGE_COMPUTE_BIT, composite)

      BEGIN_PARAMETER()
        CONSTANT_BUFFER(COMPOSITE_CONSTANTS_INPUT)
        RW_TEXTURE2D(COMPOSITE_FINAL_OUTPUT)
        RW_TEXTURE2D(COMPOSITE_ALPHA_BLEND_RADIANCE_OUTPUT)
        RW_TEXTURE2D(COMPOSITE_LAST_FINAL_OUTPUT)
        RW_TEXTURE2D_READONLY(COMPOSITE_SHARED_FLAGS_INPUT)
        TEXTURE2D(COMPOSITE_SHARED_RADIANCE_RG_INPUT)
        TEXTURE2D(COMPOSITE_SHARED_RADIANCE_B_INPUT)
        RW_TEXTURE2D_READONLY(COMPOSITE_PRIMARY_ATTENUATION_INPUT)
        TEXTURE2D(COMPOSITE_PRIMARY_ALBEDO_INPUT)
        TEXTURE2D(COMPOSITE_PRIMARY_SPECULAR_ALBEDO_INPUT)
        TEXTURE2D(COMPOSITE_PRIMARY_VIRTUAL_WORLD_SHADING_NORMAL_INPUT)
        TEXTURE2D(COMPOSITE_PRIMARY_LINEAR_VIEW_Z_INPUT)
        RW_TEXTURE2D_READONLY(COMPOSITE_SECONDARY_ATTENUATION_INPUT)
        TEXTURE2D(COMPOSITE_SECONDARY_ALBEDO_INPUT)
        TEXTURE2D(COMPOSITE_SECONDARY_SPECULAR_ALBEDO_INPUT)
        TEXTURE2D(COMPOSITE_PRIMARY_DIRECT_DIFFUSE_RADIANCE_HIT_DISTANCE_INPUT)
        TEXTURE2D(COMPOSITE_PRIMARY_DIRECT_SPECULAR_RADIANCE_HIT_DISTANCE_INPUT)
        TEXTURE2D(COMPOSITE_PRIMARY_INDIRECT_DIFFUSE_RADIANCE_HIT_DISTANCE_INPUT)
        TEXTURE2D(COMPOSITE_PRIMARY_INDIRECT_SPECULAR_RADIANCE_HIT_DISTANCE_INPUT)
        TEXTURE2D(COMPOSITE_SECONDARY_COMBINED_DIFFUSE_RADIANCE_HIT_DISTANCE_INPUT)
        TEXTURE2D(COMPOSITE_SECONDARY_COMBINED_SPECULAR_RADIANCE_HIT_DISTANCE_INPUT)
        SAMPLER3D(COMPOSITE_VOLUME_PREINTEGRATED_RADIANCE_INPUT)
        SAMPLER3D(COMPOSITE_VOLUME_FILTERED_RADIANCE_INPUT)
        TEXTURE2D(COMPOSITE_BSDF_FACTOR_INPUT)
        TEXTURE2D(COMPOSITE_BSDF_FACTOR2_INPUT)
        TEXTURE2D(COMPOSITE_ALPHA_GBUFFER_INPUT)
        TEXTURE2DARRAY(COMPOSITE_BLUE_NOISE_TEXTURE)

        RW_TEXTURE2D(COMPOSITE_DEBUG_VIEW_OUTPUT)
      END_PARAMETER()
    };

    PREWARM_SHADER_PIPELINE(CompositeShader);
  }

  CompositePass::CompositePass(dxvk::DxvkDevice* device)
    : m_vkd(device->vkd()), m_device(device) {
  }

  CompositePass::~CompositePass() {
  }

  void CompositePass::showStochasticAlphaBlendImguiSettings() {
    if (ImGui::CollapsingHeader("Stochastic Alpha Blend", collapsingHeaderClosedFlags)) {
      ImGui::Indent();
      ImGui::Checkbox("Enable Stochastic Alpha Blend", &enableStochasticAlphaBlendObject());
      ImGui::DragFloat("Max Blend Opacity", &stochasticAlphaBlendOpacityThresholdObject(), 0.005f, 0.0f, 1.f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
      ImGui::Checkbox("Use Neighbor Search", &stochasticAlphaBlendUseNeighborSearchObject());
      ImGui::Checkbox("Search The Same Object", &stochasticAlphaBlendSearchTheSameObjectObject());
      ImGui::Checkbox("Share Search Result", &stochasticAlphaBlendShareNeighborsObject());
      ImGui::DragInt("Search Iterations", &stochasticAlphaBlendSearchIterationObject(), 0.1f, 1, 20, "%d", ImGuiSliderFlags_AlwaysClamp);
      ImGui::DragFloat("Initial Search Radius", &stochasticAlphaBlendInitialSearchRadiusObject(), 0.01f, 1.0f, 20.f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
      ImGui::DragFloat("Radius Expand Factor", &stochasticAlphaBlendRadiusExpandFactorObject(), 0.01f, 1.0f, 5.f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
      ImGui::DragFloat("Neighbor Normal Similarity", &stochasticAlphaBlendNormalSimilarityObject(), 0.001f, 0.0f, 1.f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
      ImGui::DragFloat("Neighbor Depth Difference", &stochasticAlphaBlendDepthDifferenceObject(), 0.001f, 0.0f, 1.f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
      ImGui::DragFloat("Neighbor Planar Difference", &stochasticAlphaBlendPlanarDifferenceObject(), 0.001f, 0.0f, 1.f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
      ImGui::Checkbox("Use Radiance Volume", &stochasticAlphaBlendUseRadianceVolumeObject());
      ImGui::DragFloat("Radiance Volume Multiplier", &stochasticAlphaBlendRadianceVolumeMultiplierObject(), 0.001f, 0.0f, 10.f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
      ImGui::Checkbox("Discard Black Pixels", &stochasticAlphaBlendDiscardBlackPixelObject());
      ImGui::Checkbox("Filter Stochastic Alpha Blend", &stochasticAlphaBlendEnableFilterObject());
      ImGui::Unindent();
    }
  }

  void CompositePass::showImguiSettings() {
    ImGui::Indent();

    ImGui::Checkbox("Enable Fog", &enableFogObject());
    ImGui::DragFloat("Fog Color Scale", &fogColorScaleObject(), 0.01f, 0.0f, 10.f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
    ImGui::DragFloat("Max Fog Distance", &maxFogDistanceObject(), 1.f, 0.0f, 0.f, "%.0f", ImGuiSliderFlags_AlwaysClamp);

    if (ImGui::CollapsingHeader("Signal Enablement", collapsingHeaderClosedFlags)) {
      ImGui::Indent();
      ImGui::Checkbox("Primary Direct Diffuse", &compositePrimaryDirectDiffuseObject());
      ImGui::Checkbox("Primary Direct Specular", &compositePrimaryDirectSpecularObject());
      ImGui::Checkbox("Primary Indirect Diffuse", &compositePrimaryIndirectDiffuseObject());
      ImGui::Checkbox("Primary Indirect Specular", &compositePrimaryIndirectSpecularObject());
      ImGui::Checkbox("Secondary Combined Diffuse", &compositeSecondaryCombinedDiffuseObject());
      ImGui::Checkbox("Secondary Combined Specular", &compositeSecondaryCombinedSpecularObject());
      ImGui::Unindent();
    }

    ImGui::Unindent();
  }

  void CompositePass::showDenoiseImguiSettings() {
    float bsdfPowers[2] = { dlssEnhancementDirectLightPower(), dlssEnhancementIndirectLightPower() };
    float bsdfMaxValues[2] = { dlssEnhancementDirectLightMaxValue(), dlssEnhancementIndirectLightMaxValue() };

    ImGui::Checkbox("Enhance BSDF Detail Under DLSS", &enableDLSSEnhancementObject());    
    ImGui::Combo("Indirect Light Enhancement Mode", &dlssEnhancementModeObject(), "Laplacian\0Normal Difference\0");
    ImGui::DragFloat2("Direct/Indirect Light Sharpness", bsdfPowers, 0.01f, 0.01f, 20.f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
    ImGui::DragFloat2("Direct/Indirect Light Max Strength", bsdfMaxValues, 0.01f, 0.1f, 200.f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
    ImGui::DragFloat("Pixel Highlight Reuse Strength", &pixelHighlightReuseStrengthObject(), 0.01f, 0.1f, 10.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
    ImGui::DragFloat("Indirect Light Min Sharpen Roughness", &dlssEnhancementIndirectLightMinRoughnessObject(), 0.01f, 0.f, 1.f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
    ImGui::Checkbox("Use Post Filter", &usePostFilterObject());
    ImGui::DragFloat("Post Filter Threshold", &postFilterThresholdObject(), 0.01f, 0.0f, 100.f, "%.2f", ImGuiSliderFlags_AlwaysClamp);

    dlssEnhancementDirectLightPowerRef() = bsdfPowers[0];
    dlssEnhancementIndirectLightPowerRef() = bsdfPowers[1];
    dlssEnhancementDirectLightMaxValueRef() = bsdfMaxValues[0];
    dlssEnhancementIndirectLightMaxValueRef() = bsdfMaxValues[1];
  }

  void CompositePass::createConstantsBuffer()
  {
    DxvkBufferCreateInfo info = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    info.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    info.stages = VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR;
    info.access = VK_ACCESS_TRANSFER_WRITE_BIT;
    info.size = sizeof(CompositeArgs);
    m_compositeConstants = m_device->createBuffer(info, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, DxvkMemoryStats::Category::RTXBuffer);
  }

  Rc<DxvkBuffer> CompositePass::getCompositeConstantsBuffer() {
    if (m_compositeConstants == nullptr) {
      createConstantsBuffer();
    }
    assert(m_compositeConstants != nullptr);
    return m_compositeConstants;
  }

  void CompositePass::dispatch(
    Rc<RtxContext> ctx,
    SceneManager& sceneManager,
    const Resources::RaytracingOutput& rtOutput,
    const Settings& settings)
  {
    const uint32_t frameIdx = ctx->getDevice()->getCurrentFrameId();

    CompositeArgs compositeArgs = {};
    compositeArgs.enableSeparatedDenoisers = rtOutput.m_raytraceArgs.enableSeparatedDenoisers;

    ctx->bindResourceView(COMPOSITE_SHARED_FLAGS_INPUT, rtOutput.m_sharedFlags.view, nullptr);
    ctx->bindResourceView(COMPOSITE_SHARED_RADIANCE_RG_INPUT, rtOutput.m_sharedRadianceRG.view, nullptr);
    ctx->bindResourceView(COMPOSITE_SHARED_RADIANCE_B_INPUT, rtOutput.m_sharedRadianceB.view, nullptr);
    
    ctx->bindResourceView(COMPOSITE_PRIMARY_ATTENUATION_INPUT, rtOutput.m_primaryAttenuation.view, nullptr);
    ctx->bindResourceView(COMPOSITE_PRIMARY_ALBEDO_INPUT, rtOutput.m_primaryAlbedo.view, nullptr);
    // Note: Texture contains Base Reflectivity here (due to being before the demodulate pass)

    ctx->bindResourceView(COMPOSITE_PRIMARY_SPECULAR_ALBEDO_INPUT, rtOutput.m_primarySpecularAlbedo.view(Resources::AccessType::Read), nullptr);
    ctx->bindResourceView(COMPOSITE_PRIMARY_LINEAR_VIEW_Z_INPUT, rtOutput.m_primaryLinearViewZ.view, nullptr);
    ctx->bindResourceView(COMPOSITE_PRIMARY_VIRTUAL_WORLD_SHADING_NORMAL_INPUT, rtOutput.m_primaryVirtualWorldShadingNormalPerceptualRoughness.view, nullptr);

    ctx->bindResourceView(COMPOSITE_SECONDARY_ATTENUATION_INPUT, rtOutput.m_secondaryAttenuation.view, nullptr);
    ctx->bindResourceView(COMPOSITE_SECONDARY_ALBEDO_INPUT, rtOutput.m_secondaryAlbedo.view, nullptr);
    ctx->bindResourceView(COMPOSITE_SECONDARY_SPECULAR_ALBEDO_INPUT, rtOutput.m_secondarySpecularAlbedo.view(Resources::AccessType::Read), nullptr);

    // Note: These inputs may either be noisy or denoised depending on if the reference denoiser is enabled.
    ctx->bindResourceView(COMPOSITE_PRIMARY_DIRECT_DIFFUSE_RADIANCE_HIT_DISTANCE_INPUT, rtOutput.m_primaryDirectDiffuseRadiance.view(Resources::AccessType::Read), nullptr);
    ctx->bindResourceView(COMPOSITE_PRIMARY_DIRECT_SPECULAR_RADIANCE_HIT_DISTANCE_INPUT, rtOutput.m_primaryDirectSpecularRadiance.view(Resources::AccessType::Read), nullptr);

    const bool isIndirectRadianceHitDistanceUsed = compositeArgs.enableSeparatedDenoisers;
    ctx->bindResourceView(COMPOSITE_PRIMARY_INDIRECT_DIFFUSE_RADIANCE_HIT_DISTANCE_INPUT, rtOutput.m_primaryIndirectDiffuseRadiance.view(Resources::AccessType::Read, isIndirectRadianceHitDistanceUsed), nullptr);
    ctx->bindResourceView(COMPOSITE_PRIMARY_INDIRECT_SPECULAR_RADIANCE_HIT_DISTANCE_INPUT, rtOutput.m_primaryIndirectSpecularRadiance.view(Resources::AccessType::Read, isIndirectRadianceHitDistanceUsed), nullptr);

    ctx->bindResourceView(COMPOSITE_SECONDARY_COMBINED_DIFFUSE_RADIANCE_HIT_DISTANCE_INPUT, rtOutput.m_secondaryCombinedDiffuseRadiance.view(Resources::AccessType::Read), nullptr);
    ctx->bindResourceView(COMPOSITE_SECONDARY_COMBINED_SPECULAR_RADIANCE_HIT_DISTANCE_INPUT, rtOutput.m_secondaryCombinedSpecularRadiance.view(Resources::AccessType::Read), nullptr);

    ctx->bindResourceView(COMPOSITE_BSDF_FACTOR_INPUT, rtOutput.m_bsdfFactor.view, nullptr);
    ctx->bindResourceView(COMPOSITE_BSDF_FACTOR2_INPUT, rtOutput.m_bsdfFactor2.view, nullptr);
    ctx->bindResourceView(COMPOSITE_ALPHA_GBUFFER_INPUT, rtOutput.m_alphaBlendGBuffer.view, nullptr);

    // Note: Clamp to edge used to avoid interpolation to black on the edges of the view.
    Rc<DxvkSampler> linearSampler = ctx->getResourceManager().getSampler(VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);

    ctx->bindResourceView(COMPOSITE_VOLUME_PREINTEGRATED_RADIANCE_INPUT, rtOutput.m_volumePreintegratedRadiance.view, nullptr);
    ctx->bindResourceSampler(COMPOSITE_VOLUME_PREINTEGRATED_RADIANCE_INPUT, linearSampler);

    ctx->bindResourceView(COMPOSITE_VOLUME_FILTERED_RADIANCE_INPUT, rtOutput.m_volumeFilteredRadiance.view, nullptr);
    ctx->bindResourceSampler(COMPOSITE_VOLUME_FILTERED_RADIANCE_INPUT, linearSampler);

    ctx->bindResourceView(COMPOSITE_FINAL_OUTPUT, rtOutput.m_compositeOutput.view(Resources::AccessType::Write), nullptr);
    ctx->bindResourceView(COMPOSITE_ALPHA_BLEND_RADIANCE_OUTPUT, rtOutput.m_alphaBlendRadiance.view(Resources::AccessType::Write), nullptr);
    ctx->bindResourceView(COMPOSITE_LAST_FINAL_OUTPUT, rtOutput.m_lastCompositeOutput.view(Resources::AccessType::Write), nullptr);
    ctx->bindResourceView(COMPOSITE_BLUE_NOISE_TEXTURE, ctx->getResourceManager().getBlueNoiseTexture(ctx), nullptr);

    DebugView& debugView = ctx->getDevice()->getCommon()->metaDebugView();
    ctx->bindResourceView(COMPOSITE_DEBUG_VIEW_OUTPUT, debugView.getDebugOutput(), nullptr);


    // Some camera paramters for primary ray reconstruction
    Camera cameraConstants = sceneManager.getCamera().getShaderConstants();
    compositeArgs.camera = cameraConstants;
    compositeArgs.projectionToViewJittered = cameraConstants.projectionToViewJittered;
    compositeArgs.viewToWorld = cameraConstants.viewToWorld;
    compositeArgs.resolution.x = float(cameraConstants.resolution.x);
    compositeArgs.resolution.y = float(cameraConstants.resolution.y);
    compositeArgs.nearPlane = cameraConstants.nearPlane;
    compositeArgs.frameIdx = m_device->getCurrentFrameId();

    if (enableFog()) {
      const float colorScale = fogColorScale();
      auto& fog = settings.fog;
      compositeArgs.fogMode = fog.mode;
      compositeArgs.fogColor = { fog.color.x * colorScale, fog.color.y * colorScale, fog.color.z * colorScale };
      // Todo: Scene scale stuff ignored for now because scene scale stuff is not actually functioning properly. Add back in if it's ever fixed.
      // compositeArgs.fogEnd = fog.end * RtxOptions::Get()->getSceneScale();
      // compositeArgs.fogScale = fog.scale * RtxOptions::Get()->getSceneScale();
      // Note: Density can simply be divided by the scene scale factor to account for the fact that the distance in the exponent
      // will be in render units (scaled by the scene scale), not the original game's units it was targetted for.
      // compositeArgs.fogDensity = fabsf(fog.density) / RtxOptions::Get()->getSceneScale();
      compositeArgs.fogEnd = fog.end;
      compositeArgs.fogScale = fog.scale;
      compositeArgs.fogDensity = fabsf(fog.density);
      compositeArgs.maxFogDistance = maxFogDistance();
    }

    // Combine the direct and indirect channels if the seperated denoiser is enabled, otherwise the channels will be combined
    // elsewhere before compositing.
    compositeArgs.combineLightingChannels = RtxOptions::Get()->isSeparatedDenoiserEnabled();
    compositeArgs.debugKnob = ctx->getCommonObjects()->metaDebugView().debugKnob();
    compositeArgs.demodulateRoughness = settings.demodulateRoughness;
    compositeArgs.roughnessDemodulationOffset = settings.roughnessDemodulationOffset;
    compositeArgs.usePostFilter = usePostFilter()
      && RtxOptions::Get()->isDenoiserEnabled()
      && !RtxOptions::Get()->useDenoiserReferenceMode();
    compositeArgs.postFilterThreshold = postFilterThreshold();
    compositeArgs.pixelHighlightReuseStrength = 1.0 / pixelHighlightReuseStrength();
    compositeArgs.enableRtxdi = RtxOptions::Get()->useRTXDI();
    compositeArgs.enableReSTIRGI = RtxOptions::Get()->useReSTIRGI();
    compositeArgs.volumeArgs = rtOutput.m_raytraceArgs.volumeArgs;

    NrdArgs primaryDirectNrdArgs;
    NrdArgs primaryIndirectNrdArgs;
    NrdArgs secondaryNrdArgs;

    ctx->getDenoiseArgs(primaryDirectNrdArgs, primaryIndirectNrdArgs, secondaryNrdArgs);

    compositeArgs.primaryDirectMissLinearViewZ = primaryDirectNrdArgs.missLinearViewZ;

    const bool useDenoisedInputs = settings.isNRDPreCompositionDenoiserEnabled;

    compositeArgs.primaryDirectDenoiser = !useDenoisedInputs ? DENOISER_MODE_OFF :
      rtOutput.m_raytraceArgs.primaryDirectNrd.isReblurEnabled ? DENOISER_MODE_REBLUR : DENOISER_MODE_RELAX;
    compositeArgs.primaryIndirectDenoiser = !useDenoisedInputs ? DENOISER_MODE_OFF :
      rtOutput.m_raytraceArgs.primaryIndirectNrd.isReblurEnabled ? DENOISER_MODE_REBLUR : DENOISER_MODE_RELAX;
    compositeArgs.secondaryCombinedDenoiser = !useDenoisedInputs ? DENOISER_MODE_OFF :
      rtOutput.m_raytraceArgs.secondaryCombinedNrd.isReblurEnabled ? DENOISER_MODE_REBLUR : DENOISER_MODE_RELAX;

    compositeArgs.debugViewIdx = rtOutput.m_raytraceArgs.debugView;

    compositeArgs.compositePrimaryDirectDiffuse = compositePrimaryDirectDiffuse();
    compositeArgs.compositePrimaryDirectSpecular = compositePrimaryDirectSpecular();
    compositeArgs.compositePrimaryIndirectDiffuse = compositePrimaryIndirectDiffuse();
    compositeArgs.compositePrimaryIndirectSpecular = compositePrimaryIndirectSpecular();
    compositeArgs.compositeSecondaryCombinedDiffuse = compositeSecondaryCombinedDiffuse();
    compositeArgs.compositeSecondaryCombinedSpecular = compositeSecondaryCombinedSpecular();

    compositeArgs.enableStochasticAlphaBlend = enableStochasticAlphaBlend();
    compositeArgs.stochasticAlphaBlendEnableFilter = stochasticAlphaBlendEnableFilter();
    compositeArgs.stochasticAlphaBlendUseNeighborSearch = stochasticAlphaBlendUseNeighborSearch();
    compositeArgs.stochasticAlphaBlendSearchTheSameObject = stochasticAlphaBlendSearchTheSameObject();
    compositeArgs.stochasticAlphaBlendUseRadianceVolume = stochasticAlphaBlendUseRadianceVolume();
    compositeArgs.stochasticAlphaBlendSearchIteration = stochasticAlphaBlendSearchIteration();
    compositeArgs.stochasticAlphaBlendInitialSearchRadius = stochasticAlphaBlendInitialSearchRadius();
    compositeArgs.stochasticAlphaBlendRadiusExpandFactor = stochasticAlphaBlendRadiusExpandFactor();
    compositeArgs.stochasticAlphaBlendShareNeighbors = stochasticAlphaBlendShareNeighbors();
    compositeArgs.stochasticAlphaBlendNormalSimilarity = stochasticAlphaBlendNormalSimilarity();
    compositeArgs.stochasticAlphaBlendDepthDifference = stochasticAlphaBlendDepthDifference();
    compositeArgs.stochasticAlphaBlendPlanarDifference = stochasticAlphaBlendPlanarDifference();
    compositeArgs.stochasticAlphaBlendDiscardBlackPixel = stochasticAlphaBlendDiscardBlackPixel();
    compositeArgs.stochasticAlphaBlendRadianceVolumeMultiplier = stochasticAlphaBlendRadianceVolumeMultiplier();
    
    compositeArgs.clearColorFinalColor = ctx->getCommonObjects()->getSceneManager().getGlobals().clearColorFinalColor;
    
    Rc<DxvkBuffer> cb = getCompositeConstantsBuffer();
    ctx->writeToBuffer(cb, 0, sizeof(CompositeArgs), &compositeArgs);
    ctx->getCommandList()->trackResource<DxvkAccess::Read>(cb);

    ctx->bindResourceBuffer(COMPOSITE_CONSTANTS_INPUT, DxvkBufferSlice(cb, 0, cb->info().size));
    VkExtent3D workgroups = util::computeBlockCount(rtOutput.m_compositeOutputExtent, VkExtent3D { 16, 8, 1 });

    if (enableStochasticAlphaBlend()) {
      ScopedGpuProfileZone(ctx, "Composite Alpha Blend");
      ctx->bindShader(VK_SHADER_STAGE_COMPUTE_BIT, CompositeAlphaBlendShader::getShader());
      ctx->dispatch(workgroups.width, workgroups.height, workgroups.depth);
    }

    {
      ScopedGpuProfileZone(ctx, "Composition");
      ctx->bindShader(VK_SHADER_STAGE_COMPUTE_BIT, CompositeShader::getShader());
      ctx->dispatch(workgroups.width, workgroups.height, workgroups.depth);
    }
  }
} // namespace dxvk
