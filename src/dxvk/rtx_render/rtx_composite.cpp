/*
* Copyright (c) 2021-2025, NVIDIA CORPORATION. All rights reserved.
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
#include "rtx_shader_manager.h"
#include <dxvk_scoped_annotation.h>
#include "rtx_imgui.h"
#include "rtx_context.h"
#include "rtx_options.h"
#include "rtx_ray_reconstruction.h"
#include "rtx_restir_gi_rayquery.h"
#include "rtx_debug_view.h"

#include "../util/util_globaltime.h"

#include <rtx_shaders/composite.h>
#include <rtx_shaders/composite_alpha_blend.h>
#include "rtx_texture_manager.h"

namespace dxvk {

  // Defined within an unnamed namespace to ensure unique definition across binary
  namespace {

    class CompositeAlphaBlendShader : public ManagedShader {
      SHADER_SOURCE(CompositeAlphaBlendShader, VK_SHADER_STAGE_COMPUTE_BIT, composite_alpha_blend)

      BEGIN_PARAMETER()
        RW_TEXTURE2D_READONLY(COMPOSITE_SHARED_FLAGS_INPUT)
        TEXTURE2D(COMPOSITE_SHARED_RADIANCE_RG_INPUT)
        TEXTURE2D(COMPOSITE_SHARED_RADIANCE_B_INPUT)
        RW_TEXTURE2D_READONLY(COMPOSITE_PRIMARY_ATTENUATION_INPUT)
        TEXTURE2D(COMPOSITE_PRIMARY_SPECULAR_ALBEDO_INPUT)
        TEXTURE2D(COMPOSITE_PRIMARY_LINEAR_VIEW_Z_INPUT)
        TEXTURE2D(COMPOSITE_PRIMARY_VIRTUAL_WORLD_SHADING_NORMAL_INPUT)
        RW_TEXTURE2D_READONLY(COMPOSITE_SECONDARY_ATTENUATION_INPUT)
        TEXTURE2D(COMPOSITE_SECONDARY_ALBEDO_INPUT)
        TEXTURE2D(COMPOSITE_SECONDARY_SPECULAR_ALBEDO_INPUT)
        TEXTURE2D(COMPOSITE_PRIMARY_DIRECT_DIFFUSE_RADIANCE_HIT_DISTANCE_INPUT)
        TEXTURE2D(COMPOSITE_PRIMARY_DIRECT_SPECULAR_RADIANCE_HIT_DISTANCE_INPUT)
        TEXTURE2D(COMPOSITE_PRIMARY_INDIRECT_DIFFUSE_RADIANCE_HIT_DISTANCE_INPUT)
        TEXTURE2D(COMPOSITE_PRIMARY_INDIRECT_SPECULAR_RADIANCE_HIT_DISTANCE_INPUT)
        TEXTURE2D(COMPOSITE_SECONDARY_COMBINED_DIFFUSE_RADIANCE_HIT_DISTANCE_INPUT)
        TEXTURE2D(COMPOSITE_SECONDARY_COMBINED_SPECULAR_RADIANCE_HIT_DISTANCE_INPUT)
        CONSTANT_BUFFER(COMPOSITE_CONSTANTS_INPUT)
        TEXTURE2D(COMPOSITE_BSDF_FACTOR_INPUT)
        TEXTURE2D(COMPOSITE_BSDF_FACTOR2_INPUT)
        SAMPLER3D(COMPOSITE_VOLUME_FILTERED_RADIANCE_AGE_INPUT)
        SAMPLER3D(COMPOSITE_VOLUME_FILTERED_RADIANCE_Y_INPUT)
        SAMPLER3D(COMPOSITE_VOLUME_FILTERED_RADIANCE_CO_CG_INPUT)
        TEXTURE2D(COMPOSITE_ALPHA_GBUFFER_INPUT)
        TEXTURE2DARRAY(COMPOSITE_BLUE_NOISE_TEXTURE)
        SAMPLER3D(COMPOSITE_VALUE_NOISE_SAMPLER)

        RW_TEXTURE2D(COMPOSITE_PRIMARY_ALBEDO_INPUT_OUTPUT)
        RW_TEXTURE2D(COMPOSITE_ACCUMULATED_FINAL_OUTPUT_INPUT_OUTPUT)

        RW_TEXTURE2D(COMPOSITE_FINAL_OUTPUT)
        RW_TEXTURE2D(COMPOSITE_LAST_FINAL_OUTPUT)
        RW_TEXTURE2D(COMPOSITE_ALPHA_BLEND_RADIANCE_OUTPUT)
        RW_TEXTURE2D(COMPOSITE_RAY_RECONSTRUCTION_PARTICLE_BUFFER_OUTPUT)
        RW_TEXTURE2D(COMPOSITE_DEBUG_VIEW_OUTPUT)
      END_PARAMETER()
    };

    PREWARM_SHADER_PIPELINE(CompositeAlphaBlendShader);

    class CompositeShader : public ManagedShader {
      SHADER_SOURCE(CompositeShader, VK_SHADER_STAGE_COMPUTE_BIT, composite)

      BEGIN_PARAMETER()
        RW_TEXTURE2D_READONLY(COMPOSITE_SHARED_FLAGS_INPUT)
        TEXTURE2D(COMPOSITE_SHARED_RADIANCE_RG_INPUT)
        TEXTURE2D(COMPOSITE_SHARED_RADIANCE_B_INPUT)
        RW_TEXTURE2D_READONLY(COMPOSITE_PRIMARY_ATTENUATION_INPUT)
        TEXTURE2D(COMPOSITE_PRIMARY_SPECULAR_ALBEDO_INPUT)
        TEXTURE2D(COMPOSITE_PRIMARY_LINEAR_VIEW_Z_INPUT)
        TEXTURE2D(COMPOSITE_PRIMARY_VIRTUAL_WORLD_SHADING_NORMAL_INPUT)
        RW_TEXTURE2D_READONLY(COMPOSITE_SECONDARY_ATTENUATION_INPUT)
        TEXTURE2D(COMPOSITE_SECONDARY_ALBEDO_INPUT)
        TEXTURE2D(COMPOSITE_SECONDARY_SPECULAR_ALBEDO_INPUT)
        TEXTURE2D(COMPOSITE_PRIMARY_DIRECT_DIFFUSE_RADIANCE_HIT_DISTANCE_INPUT)
        TEXTURE2D(COMPOSITE_PRIMARY_DIRECT_SPECULAR_RADIANCE_HIT_DISTANCE_INPUT)
        TEXTURE2D(COMPOSITE_PRIMARY_INDIRECT_DIFFUSE_RADIANCE_HIT_DISTANCE_INPUT)
        TEXTURE2D(COMPOSITE_PRIMARY_INDIRECT_SPECULAR_RADIANCE_HIT_DISTANCE_INPUT)
        TEXTURE2D(COMPOSITE_SECONDARY_COMBINED_DIFFUSE_RADIANCE_HIT_DISTANCE_INPUT)
        TEXTURE2D(COMPOSITE_SECONDARY_COMBINED_SPECULAR_RADIANCE_HIT_DISTANCE_INPUT)
        CONSTANT_BUFFER(COMPOSITE_CONSTANTS_INPUT)
        TEXTURE2D(COMPOSITE_BSDF_FACTOR_INPUT)
        TEXTURE2D(COMPOSITE_BSDF_FACTOR2_INPUT)
        SAMPLER3D(COMPOSITE_VOLUME_FILTERED_RADIANCE_AGE_INPUT)
        SAMPLER3D(COMPOSITE_VOLUME_FILTERED_RADIANCE_Y_INPUT)
        SAMPLER3D(COMPOSITE_VOLUME_FILTERED_RADIANCE_CO_CG_INPUT)
        TEXTURE2D(COMPOSITE_ALPHA_GBUFFER_INPUT)
        TEXTURE2DARRAY(COMPOSITE_BLUE_NOISE_TEXTURE)
        SAMPLER3D(COMPOSITE_VALUE_NOISE_SAMPLER)
        SAMPLER2D(COMPOSITE_SKY_LIGHT_TEXTURE)

        RW_TEXTURE2D(COMPOSITE_PRIMARY_ALBEDO_INPUT_OUTPUT)
        RW_TEXTURE2D(COMPOSITE_ACCUMULATED_FINAL_OUTPUT_INPUT_OUTPUT)

        RW_TEXTURE2D(COMPOSITE_FINAL_OUTPUT)
        RW_TEXTURE2D(COMPOSITE_LAST_FINAL_OUTPUT)
        RW_TEXTURE2D(COMPOSITE_ALPHA_BLEND_RADIANCE_OUTPUT)
        RW_TEXTURE2D(COMPOSITE_RAY_RECONSTRUCTION_PARTICLE_BUFFER_OUTPUT)
        RW_TEXTURE2D(COMPOSITE_DEBUG_VIEW_OUTPUT)
      END_PARAMETER()
    };

    PREWARM_SHADER_PIPELINE(CompositeShader);
  }

  CompositePass::CompositePass(dxvk::DxvkDevice* device)
    : RtxPass(device)
    , m_vkd(device->vkd()),
    m_device(device) {
  }

  CompositePass::~CompositePass() {
  }

  void CompositePass::showStochasticAlphaBlendImguiSettings() {
    if (RemixGui::CollapsingHeader("Stochastic Alpha Blend")) {
      ImGui::Indent();
      RemixGui::Checkbox("Enable Stochastic Alpha Blend", &enableStochasticAlphaBlendObject());
      RemixGui::DragFloat("Max Blend Opacity", &stochasticAlphaBlendOpacityThresholdObject(), 0.005f, 0.0f, 1.f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
      RemixGui::Checkbox("Use Neighbor Search", &stochasticAlphaBlendUseNeighborSearchObject());
      RemixGui::Checkbox("Search The Same Object", &stochasticAlphaBlendSearchTheSameObjectObject());
      RemixGui::Checkbox("Share Search Result", &stochasticAlphaBlendShareNeighborsObject());
      RemixGui::DragInt("Search Iterations", &stochasticAlphaBlendSearchIterationObject(), 0.1f, 1, 20, "%d", ImGuiSliderFlags_AlwaysClamp);
      RemixGui::DragFloat("Initial Search Radius", &stochasticAlphaBlendInitialSearchRadiusObject(), 0.01f, 1.0f, 20.f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
      RemixGui::DragFloat("Radius Expand Factor", &stochasticAlphaBlendRadiusExpandFactorObject(), 0.01f, 1.0f, 5.f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
      RemixGui::DragFloat("Neighbor Normal Similarity", &stochasticAlphaBlendNormalSimilarityObject(), 0.001f, 0.0f, 1.f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
      RemixGui::DragFloat("Neighbor Depth Difference", &stochasticAlphaBlendDepthDifferenceObject(), 0.001f, 0.0f, 1.f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
      RemixGui::DragFloat("Neighbor Planar Difference", &stochasticAlphaBlendPlanarDifferenceObject(), 0.001f, 0.0f, 1.f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
      RemixGui::Checkbox("Use Radiance Volume", &stochasticAlphaBlendUseRadianceVolumeObject());
      RemixGui::DragFloat("Radiance Volume Multiplier", &stochasticAlphaBlendRadianceVolumeMultiplierObject(), 0.001f, 0.0f, 10.f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
      RemixGui::Checkbox("Discard Black Pixels", &stochasticAlphaBlendDiscardBlackPixelObject());
      RemixGui::Checkbox("Filter Stochastic Alpha Blend", &stochasticAlphaBlendEnableFilterObject());
      ImGui::Unindent();
    }
  }

  void CompositePass::showDepthBasedFogImguiSettings() {
    RemixGui::Checkbox("Enable Depth-Based Fog", &enableFogObject());

    ImGui::BeginDisabled(!enableFog());
    ImGui::Indent();
    RemixGui::DragFloat("Fog Color Scale", &fogColorScaleObject(), 0.01f, 0.0f, 10.f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
    RemixGui::DragFloat("Max Fog Distance", &maxFogDistanceObject(), 1.f, 0.0f, 0.f, "%.0f", ImGuiSliderFlags_AlwaysClamp);
    ImGui::Unindent();
    ImGui::EndDisabled();
  }

  void CompositePass::showImguiSettings() {
    ImGui::TextUnformatted("Signal Enablement");
    {
      ImGui::Indent();
      RemixGui::Checkbox("Primary Direct Diffuse", &compositePrimaryDirectDiffuseObject());
      RemixGui::Checkbox("Primary Direct Specular", &compositePrimaryDirectSpecularObject());
      RemixGui::Checkbox("Primary Indirect Diffuse", &compositePrimaryIndirectDiffuseObject());
      RemixGui::Checkbox("Primary Indirect Specular", &compositePrimaryIndirectSpecularObject());
      RemixGui::Checkbox("Secondary Combined Diffuse", &compositeSecondaryCombinedDiffuseObject());
      RemixGui::Checkbox("Secondary Combined Specular", &compositeSecondaryCombinedSpecularObject());
      ImGui::Unindent();
    }
  }

  void CompositePass::showAccumulationImguiSettings() {
    m_accumulation.showImguiSettings(
      RtxOptions::Accumulation::numberOfFramesToAccumulateObject(), 
      RtxOptions::Accumulation::blendModeObject(), 
      RtxOptions::Accumulation::resetOnCameraTransformChangeObject());
  }

  void CompositePass::showDenoiseImguiSettings() {
    float bsdfPowers[2] = { dlssEnhancementDirectLightPower(), dlssEnhancementIndirectLightPower() };
    float bsdfMaxValues[2] = { dlssEnhancementDirectLightMaxValue(), dlssEnhancementIndirectLightMaxValue() };

    RemixGui::Checkbox("Enhance BSDF Detail Under DLSS", &enableDLSSEnhancementObject());    
    RemixGui::Combo("Indirect Light Enhancement Mode", &dlssEnhancementModeObject(), "Laplacian\0Normal Difference\0");
    RemixGui::DragFloat2("Direct/Indirect Light Sharpness", bsdfPowers, 0.01f, 0.01f, 20.f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
    RemixGui::DragFloat2("Direct/Indirect Light Max Strength", bsdfMaxValues, 0.01f, 0.1f, 200.f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
    RemixGui::DragFloat("Pixel Highlight Reuse Strength", &pixelHighlightReuseStrengthObject(), 0.01f, 0.1f, 10.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
    RemixGui::DragFloat("Indirect Light Min Sharpen Roughness", &dlssEnhancementIndirectLightMinRoughnessObject(), 0.01f, 0.f, 1.f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
    RemixGui::Checkbox("Use Post Filter", &usePostFilterObject());
    RemixGui::DragFloat("Post Filter Threshold", &postFilterThresholdObject(), 0.01f, 0.0f, 100.f, "%.2f", ImGuiSliderFlags_AlwaysClamp);

    dlssEnhancementDirectLightPower.setDeferred(bsdfPowers[0]);
    dlssEnhancementIndirectLightPower.setDeferred(bsdfPowers[1]);
    dlssEnhancementDirectLightMaxValue.setDeferred(bsdfMaxValues[0]);
    dlssEnhancementIndirectLightMaxValue.setDeferred(bsdfMaxValues[1]);
  }

  void CompositePass::createConstantsBuffer()
  {
    DxvkBufferCreateInfo info = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    info.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    info.stages = VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR;
    info.access = VK_ACCESS_TRANSFER_WRITE_BIT;
    info.size = sizeof(CompositeArgs);
    m_compositeConstants = m_device->createBuffer(info, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, DxvkMemoryStats::Category::RTXBuffer, "Composite Args Constant Buffer");
  }

  Rc<DxvkBuffer> CompositePass::getCompositeConstantsBuffer() {
    if (m_compositeConstants == nullptr) {
      createConstantsBuffer();
    }
    assert(m_compositeConstants != nullptr);
    return m_compositeConstants;
  }
  
  bool CompositePass::isEnabled() const {
    // This pass is always enabled
    return true;
  }

  bool CompositePass::enableAccumulation() const {
    return RtxOptions::useDenoiserReferenceMode();
  }

  void CompositePass::onFrameBegin(
  Rc<DxvkContext>& ctx,
  const FrameBeginContext& frameBeginCtx) {

    RtxPass::onFrameBegin(ctx, frameBeginCtx);

    // Accumulation per-frame setup
    {
      const bool enableAccumulationChanged = m_enableAccumulation != enableAccumulation();

      // Ensure consistent state during frame due to RTX_OPTIONs changing asynchronously
      m_enableAccumulation = enableAccumulation();

      RtxContext& rtxCtx = dynamic_cast<RtxContext&>(*ctx.ptr());
      m_accumulation.onFrameBegin(
        rtxCtx, m_enableAccumulation, RtxOptions::Accumulation::numberOfFramesToAccumulate(),
        RtxOptions::Accumulation::resetOnCameraTransformChange());

      // Create/release accumulation buffer when needed
      if (enableAccumulationChanged) {
        if (m_enableAccumulation) {
          m_accumulatedFinalOutput = Resources::createImageResource(ctx, "accumulated final output", frameBeginCtx.downscaledExtent, VK_FORMAT_R32G32B32A32_SFLOAT);
        } else {
          m_accumulatedFinalOutput.reset();
        }
      }
    }
  }

  void CompositePass::createDownscaledResource(Rc<DxvkContext>& ctx, const VkExtent3D& downscaledExtent) {
    m_accumulation.resetNumAccumulatedFrames();
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

    // Fill in accumulation args
    if (m_enableAccumulation) {
      m_accumulation.initAccumulationArgs(
        RtxOptions::Accumulation::blendMode(),
        compositeArgs.accumulationArgs);
    }

    // Inputs

    ctx->bindResourceView(COMPOSITE_SHARED_FLAGS_INPUT, rtOutput.m_sharedFlags.view, nullptr);
    ctx->bindResourceView(COMPOSITE_SHARED_RADIANCE_RG_INPUT, rtOutput.m_sharedRadianceRG.view, nullptr);
    ctx->bindResourceView(COMPOSITE_SHARED_RADIANCE_B_INPUT, rtOutput.m_sharedRadianceB.view, nullptr);
    
    ctx->bindResourceView(COMPOSITE_PRIMARY_ATTENUATION_INPUT, rtOutput.m_primaryAttenuation.view, nullptr);
    
    // Note: Texture contains Base Reflectivity here (due to being before the demodulate pass)

    ctx->bindResourceView(COMPOSITE_PRIMARY_SPECULAR_ALBEDO_INPUT, rtOutput.m_primarySpecularAlbedo.view(Resources::AccessType::Read), nullptr);
    ctx->bindResourceView(COMPOSITE_PRIMARY_LINEAR_VIEW_Z_INPUT, rtOutput.m_primaryLinearViewZ.view, nullptr);
    ctx->bindResourceView(COMPOSITE_PRIMARY_VIRTUAL_WORLD_SHADING_NORMAL_INPUT, rtOutput.m_primaryVirtualWorldShadingNormalPerceptualRoughness.view, nullptr);

    ctx->bindResourceView(COMPOSITE_SECONDARY_ATTENUATION_INPUT, rtOutput.m_secondaryAttenuation.view, nullptr);
    ctx->bindResourceView(COMPOSITE_SECONDARY_ALBEDO_INPUT, rtOutput.m_secondaryAlbedo.view, nullptr);
    ctx->bindResourceView(COMPOSITE_SECONDARY_SPECULAR_ALBEDO_INPUT, rtOutput.m_secondarySpecularAlbedo.view(Resources::AccessType::Read), nullptr);

    // Note: These inputs may either be noisy or denoised depending on if the reference denoiser if enabled or if RayReconstruction is in use.
    ctx->bindResourceView(COMPOSITE_PRIMARY_DIRECT_DIFFUSE_RADIANCE_HIT_DISTANCE_INPUT, rtOutput.m_primaryDirectDiffuseRadiance.view(Resources::AccessType::Read), nullptr);
    ctx->bindResourceView(COMPOSITE_PRIMARY_DIRECT_SPECULAR_RADIANCE_HIT_DISTANCE_INPUT, rtOutput.m_primaryDirectSpecularRadiance.view(Resources::AccessType::Read), nullptr);

    const bool isIndirectRadianceHitDistanceUsed = compositeArgs.enableSeparatedDenoisers;
    ctx->bindResourceView(COMPOSITE_PRIMARY_INDIRECT_DIFFUSE_RADIANCE_HIT_DISTANCE_INPUT, rtOutput.m_primaryIndirectDiffuseRadiance.view(Resources::AccessType::Read, isIndirectRadianceHitDistanceUsed), nullptr);
    ctx->bindResourceView(COMPOSITE_PRIMARY_INDIRECT_SPECULAR_RADIANCE_HIT_DISTANCE_INPUT, rtOutput.m_primaryIndirectSpecularRadiance.view(Resources::AccessType::Read, isIndirectRadianceHitDistanceUsed), nullptr);

    ctx->bindResourceView(COMPOSITE_SECONDARY_COMBINED_DIFFUSE_RADIANCE_HIT_DISTANCE_INPUT, rtOutput.m_secondaryCombinedDiffuseRadiance.view(Resources::AccessType::Read), nullptr);
    ctx->bindResourceView(COMPOSITE_SECONDARY_COMBINED_SPECULAR_RADIANCE_HIT_DISTANCE_INPUT, rtOutput.m_secondaryCombinedSpecularRadiance.view(Resources::AccessType::Read), nullptr);

    const DxvkReSTIRGIRayQuery& restirGI = ctx->getCommonObjects()->metaReSTIRGIRayQuery();
    ctx->bindResourceView(COMPOSITE_BSDF_FACTOR_INPUT, rtOutput.m_bsdfFactor.view, nullptr);
    ctx->bindResourceView(COMPOSITE_BSDF_FACTOR2_INPUT, restirGI.getBsdfFactor2().view, nullptr);
    ctx->bindResourceView(COMPOSITE_ALPHA_GBUFFER_INPUT, rtOutput.m_alphaBlendGBuffer.view, nullptr);

    // Note: Clamp to edge used to avoid interpolation to black on the edges of the view.
    Rc<DxvkSampler> linearSampler = ctx->getResourceManager().getSampler(VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);

    const RtxGlobalVolumetrics& globalVolumetrics = ctx->getCommonObjects()->metaGlobalVolumetrics();
    ctx->bindResourceView(COMPOSITE_VOLUME_FILTERED_RADIANCE_AGE_INPUT, globalVolumetrics.getCurrentVolumeAccumulatedRadianceAge().view, nullptr);
    ctx->bindResourceSampler(COMPOSITE_VOLUME_FILTERED_RADIANCE_AGE_INPUT, linearSampler);
    ctx->bindResourceView(COMPOSITE_VOLUME_FILTERED_RADIANCE_Y_INPUT, globalVolumetrics.getCurrentVolumeAccumulatedRadianceY().view, nullptr);
    ctx->bindResourceSampler(COMPOSITE_VOLUME_FILTERED_RADIANCE_Y_INPUT, linearSampler);
    ctx->bindResourceView(COMPOSITE_VOLUME_FILTERED_RADIANCE_CO_CG_INPUT, globalVolumetrics.getCurrentVolumeAccumulatedRadianceCoCg().view, nullptr);
    ctx->bindResourceSampler(COMPOSITE_VOLUME_FILTERED_RADIANCE_CO_CG_INPUT, linearSampler);

    // Inputs/Outputs

    ctx->bindResourceView(COMPOSITE_PRIMARY_ALBEDO_INPUT_OUTPUT, rtOutput.m_primaryAlbedo.view, nullptr);
    ctx->bindResourceView(COMPOSITE_ACCUMULATED_FINAL_OUTPUT_INPUT_OUTPUT, m_accumulatedFinalOutput.view, nullptr);

    // Outputs

    ctx->bindResourceView(COMPOSITE_FINAL_OUTPUT, rtOutput.m_compositeOutput.view(Resources::AccessType::Write), nullptr);
    ctx->bindResourceView(COMPOSITE_ALPHA_BLEND_RADIANCE_OUTPUT, rtOutput.m_alphaBlendRadiance.view(Resources::AccessType::Write), nullptr);
    ctx->bindResourceView(COMPOSITE_LAST_FINAL_OUTPUT, restirGI.isActive() ? restirGI.getLastCompositeOutput().view(Resources::AccessType::Write) : nullptr, nullptr);
    ctx->bindResourceView(COMPOSITE_BLUE_NOISE_TEXTURE, ctx->getResourceManager().getBlueNoiseTexture(ctx), nullptr);
    ctx->bindResourceView(COMPOSITE_VALUE_NOISE_SAMPLER, ctx->getResourceManager().getValueNoiseLut(ctx), nullptr);
    Rc<DxvkSampler> valueNoiseSampler = ctx->getResourceManager().getSampler(VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_NEAREST, VK_SAMPLER_ADDRESS_MODE_REPEAT);
    ctx->bindResourceSampler(COMPOSITE_VALUE_NOISE_SAMPLER, valueNoiseSampler);

    DebugView& debugView = ctx->getDevice()->getCommon()->metaDebugView();
    ctx->bindResourceView(COMPOSITE_DEBUG_VIEW_OUTPUT, debugView.getDebugOutput(), nullptr);
    ctx->bindResourceView(COMPOSITE_RAY_RECONSTRUCTION_PARTICLE_BUFFER_OUTPUT, rtOutput.m_rayReconstructionParticleBuffer.view, nullptr);

    ctx->bindResourceView(COMPOSITE_RAY_RECONSTRUCTION_PARTICLE_BUFFER_OUTPUT, rtOutput.m_rayReconstructionParticleBuffer.view, nullptr);

    const DomeLightArgs& domeLightArgs = sceneManager.getLightManager().getDomeLightArgs();
    ctx->bindResourceSampler(COMPOSITE_SKY_LIGHT_TEXTURE, linearSampler);
    if (domeLightArgs.active) {
      RtxTextureManager& texManager = ctx->getCommonObjects()->getTextureManager();
      const TextureRef& domeLightTex = texManager.getTextureTable()[domeLightArgs.textureIndex];
      
      ctx->bindResourceView(COMPOSITE_SKY_LIGHT_TEXTURE, domeLightTex.getImageView(), nullptr);
    } else {
      ctx->bindResourceView(COMPOSITE_SKY_LIGHT_TEXTURE, ctx->getResourceManager().getSkyMatte(ctx).view, nullptr);
    }

    // Some camera parameters for primary ray reconstruction
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
      // compositeArgs.fogEnd = fog.end * RtxOptions::sceneScale();
      // compositeArgs.fogScale = fog.scale * RtxOptions::sceneScale();
      // Note: Density can simply be divided by the scene scale factor to account for the fact that the distance in the exponent
      // will be in render units (scaled by the scene scale), not the original game's units it was targetted for.
      // compositeArgs.fogDensity = fabsf(fog.density) / RtxOptions::sceneScale();
      compositeArgs.fogEnd = fog.end;
      compositeArgs.fogScale = fog.scale;
      compositeArgs.fogDensity = fabsf(fog.density);
      compositeArgs.maxFogDistance = maxFogDistance();
    }

    // Combine the direct and indirect channels if the seperated denoiser is enabled, otherwise the channels will be combined
    // elsewhere before compositing.
    compositeArgs.combineLightingChannels = RtxOptions::denoiseDirectAndIndirectLightingSeparately();
    compositeArgs.debugKnob = ctx->getCommonObjects()->metaDebugView().debugKnob();
    compositeArgs.demodulateRoughness = settings.demodulateRoughness;
    compositeArgs.roughnessDemodulationOffset = settings.roughnessDemodulationOffset;
    compositeArgs.usePostFilter = usePostFilter()
      && (RtxOptions::useDenoiser() || RtxOptions::isRayReconstructionEnabled())
      && !RtxOptions::useDenoiserReferenceMode()
      && RtxOptions::useReSTIRGI();

    auto& rayReconstruction = ctx->getCommonObjects()->metaRayReconstruction();
    compositeArgs.postFilterThreshold = postFilterThreshold();
    compositeArgs.pixelHighlightReuseStrength = 1.0 / pixelHighlightReuseStrength();
    compositeArgs.enableRtxdi = RtxOptions::useRTXDI();
    compositeArgs.enableReSTIRGI = RtxOptions::useReSTIRGI();
    compositeArgs.volumeArgs = rtOutput.m_raytraceArgs.volumeArgs;
    compositeArgs.outputParticleLayer = ctx->useRayReconstruction() && rayReconstruction.useParticleBuffer();
    compositeArgs.outputSecondarySignalToParticleLayer = ctx->useRayReconstruction() && rayReconstruction.preprocessSecondarySignal();
    compositeArgs.enableDemodulateAttenuation = ctx->useRayReconstruction() && rayReconstruction.demodulateAttenuation();
    compositeArgs.enhanceAlbedo = ctx->useRayReconstruction() && rayReconstruction.enableDetailEnhancement();
    compositeArgs.compositeVolumetricLight = ctx->useRayReconstruction() && rayReconstruction.compositeVolumetricLight();

    NrdArgs primaryDirectNrdArgs;
    NrdArgs primaryIndirectNrdArgs;
    NrdArgs secondaryNrdArgs;

    ctx->getDenoiseArgs(primaryDirectNrdArgs, primaryIndirectNrdArgs, secondaryNrdArgs);

    compositeArgs.primaryDirectMissLinearViewZ = primaryDirectNrdArgs.missLinearViewZ;

    const bool useDenoisedInputs = settings.isNRDPreCompositionDenoiserEnabled && !ctx->useRayReconstruction();

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
    
    compositeArgs.clearColorFinalColor = sceneManager.getGlobals().clearColorFinalColor;

    // TODO: These are copied from raytrace_args.  Perhaps we should unify this...
    
    // We are going to use this value to perform some animations on GPU, to mitigate precision related issues loop time
    // at the 24 bit boundary (as we use a 8 bit scalar on top of this time which we want to fit into 32 bits without issues,
    // plus we also convert this value to a floating point value at some point as well which has 23 bits of precision).
    // Bitwise and used rather than modulus as well for slightly better performance.
    compositeArgs.timeSinceStartMS = static_cast<uint32_t>(GlobalTime::get().absoluteTimeMs()) & ((1U << 24U) - 1U);
    
    RayPortalManager::SceneData portalData = sceneManager.getRayPortalManager().getRayPortalInfoSceneData();
    compositeArgs.numActiveRayPortals = portalData.numActiveRayPortals;
    memcpy(&compositeArgs.rayPortalHitInfos[0], &portalData.rayPortalHitInfos, sizeof(portalData.rayPortalHitInfos));
    memcpy(&compositeArgs.rayPortalHitInfos[maxRayPortalCount], &portalData.previousRayPortalHitInfos, sizeof(portalData.previousRayPortalHitInfos));

    compositeArgs.domeLightArgs = domeLightArgs;
    compositeArgs.skyBrightness = RtxOptions::skyBrightness();

    Rc<DxvkBuffer> cb = getCompositeConstantsBuffer();
    ctx->writeToBuffer(cb, 0, sizeof(CompositeArgs), &compositeArgs);
    ctx->getCommandList()->trackResource<DxvkAccess::Read>(cb);

    ctx->bindResourceBuffer(COMPOSITE_CONSTANTS_INPUT, DxvkBufferSlice(cb, 0, cb->info().size));
    VkExtent3D workgroups = util::computeBlockCount(rtOutput.m_compositeOutputExtent, VkExtent3D { 16, 8, 1 });

    if (enableStochasticAlphaBlend()) {
      ScopedGpuProfileZone(ctx, "Composite Alpha Blend");
      ctx->setFramePassStage(RtxFramePassStage::CompositionAlphaBlend);
      ctx->bindShader(VK_SHADER_STAGE_COMPUTE_BIT, CompositeAlphaBlendShader::getShader());
      ctx->dispatch(workgroups.width, workgroups.height, workgroups.depth);
    }

    {
      ScopedGpuProfileZone(ctx, "Composition");
      ctx->setFramePassStage(RtxFramePassStage::Composition);
      ctx->bindShader(VK_SHADER_STAGE_COMPUTE_BIT, CompositeShader::getShader());
      ctx->dispatch(workgroups.width, workgroups.height, workgroups.depth);
    }

    // End frame from Composite Pass's perspective
    m_accumulation.onFrameEnd();
  }
} // namespace dxvk
