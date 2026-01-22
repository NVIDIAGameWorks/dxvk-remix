/*
* Copyright (c) 2024-2025, NVIDIA CORPORATION. All rights reserved.
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
#include <locale>
#include <codecvt>
#include <cassert>

#include "rtx.h"
#include "rtx_context.h"
#include "rtx_options.h"
#include "rtx/pass/tonemap/tonemapping.h"
#include "rtx/pass/ray_reconstruction/ray_reconstruction.h"
#include "dxvk_device.h"
#include "rtx_dlss.h"
#include "dxvk_scoped_annotation.h"
#include "rtx_ngx_wrapper.h"
#include "rtx_render/rtx_shader_manager.h"
#include "rtx_imgui.h"
#include "rtx_debug_view.h"

#include "rtx_matrix_helpers.h"

#include "rtx_ngx_wrapper.h"

#include "rtx_shaders/prepare_ray_reconstruction.h"
#include "rtx_shader_manager.h"
#include "rtx_dlss.h"
#include "rtx_ray_reconstruction.h"

namespace dxvk {

  // Defined within an unnamed namespace to ensure unique definition across binary
  namespace {
    class PrepareRayReconstructionShader : public ManagedShader {
      SHADER_SOURCE(PrepareRayReconstructionShader, VK_SHADER_STAGE_COMPUTE_BIT, prepare_ray_reconstruction)

        BEGIN_PARAMETER()
        TEXTURE2D(RAY_RECONSTRUCTION_NORMALS_INPUT)
        TEXTURE2D(RAY_RECONSTRUCTION_VIRTUAL_NORMALS_INPUT)
        CONSTANT_BUFFER(RAY_RECONSTRUCTION_CONSTANTS_INPUT)
        // Primary surface data
        TEXTURE2D(RAY_RECONSTRUCTION_PRIMARY_INDIRECT_SPECULAR_INPUT)
        TEXTURE2D(RAY_RECONSTRUCTION_PRIMARY_ATTENUATION_INPUT)
        TEXTURE2D(RAY_RECONSTRUCTION_PRIMARY_VIRTUAL_WORLD_SHADING_NORMAL_INPUT)
        TEXTURE2D(RAY_RECONSTRUCTION_PRIMARY_CONE_RADIUS_INPUT)
        TEXTURE2D(RAY_RECONSTRUCTION_PRIMARY_DISOCCLUSION_MASK_INPUT)
        // Secondary surface data
        TEXTURE2D(RAY_RECONSTRUCTION_SECONDARY_ALBEDO_INPUT)
        TEXTURE2D(RAY_RECONSTRUCTION_SECONDARY_SPECULAR_ALBEDO_INPUT)
        TEXTURE2D(RAY_RECONSTRUCTION_SECONDARY_ATTENUATION_INPUT)
        TEXTURE2D(RAY_RECONSTRUCTION_SECONDARY_VIRTUAL_WORLD_SHADING_NORMAL_INPUT)

        TEXTURE2D(RAY_RECONSTRUCTION_SHARED_FLAGS_INPUT)
        TEXTURE2D(RAY_RECONSTRUCTION_COMBINED_INPUT)
        TEXTURE2D(RAY_RECONSTRUCTION_NORMALS_DLSSRR_INPUT)
        TEXTURE2D(RAY_RECONSTRUCTION_DEPTHS_INPUT)
        TEXTURE2D(RAY_RECONSTRUCTION_MOTION_VECTOR_INPUT)

        RW_TEXTURE2D(RAY_RECONSTRUCTION_PRIMARY_ALBEDO_INPUT_OUTPUT)
        RW_TEXTURE2D(RAY_RECONSTRUCTION_PRIMARY_SPECULAR_ALBEDO_INPUT_OUTPUT)

        RW_TEXTURE2D(RAY_RECONSTRUCTION_NORMALS_OUTPUT)
        RW_TEXTURE2D(RAY_RECONSTRUCTION_HIT_DISTANCE_OUTPUT)
        RW_TEXTURE2D(RAY_RECONSTRUCTION_DEBUG_VIEW_OUTPUT)
        RW_TEXTURE2D(RAY_RECONSTRUCTION_PRIMARY_DISOCCLUSION_MASK_OUTPUT)

        END_PARAMETER()
    };
    PREWARM_SHADER_PIPELINE(PrepareRayReconstructionShader);
  }

  DxvkRayReconstruction::DxvkRayReconstruction(DxvkDevice* device)
    : DxvkDLSS(device)
    , m_prevModel(model())
    , m_prevEnableTransformerModelD(enableTransformerModelD()) {

    DxvkBufferCreateInfo info = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    info.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    info.stages = VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    info.access = VK_ACCESS_TRANSFER_WRITE_BIT;
    info.size = sizeof(RayReconstructionArgs);
    m_constants = device->createBuffer(info, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, DxvkMemoryStats::Category::RTXBuffer, "DLSS-RR constant buffer");
  }

  bool DxvkRayReconstruction::supportsRayReconstruction() const {
    return m_device->getCommon()->metaNGXContext().supportsRayReconstruction();
  }

  DxvkRayReconstruction::RayReconstructionParticleBufferMode DxvkRayReconstruction::getParticleBufferMode() {
    return RtxOptions::isRayReconstructionEnabled() ? particleBufferMode() : RayReconstructionParticleBufferMode::None;
  }

  void DxvkRayReconstruction::release() {
    m_rayReconstructionContext = {};
    mRecreate = true;
    m_normals.image = nullptr;
    m_normals.view = nullptr;
  }
  
  void DxvkRayReconstruction::onDestroy() {
    release();
  }

  bool DxvkRayReconstruction::useRayReconstruction() {
    return supportsRayReconstruction() && RtxOptions::isRayReconstructionEnabled();
  }

  void DxvkRayReconstruction::dispatch(
      Rc<RtxContext> ctx,
      DxvkBarrierSet& barriers,
      const Resources::RaytracingOutput& rtOutput,
      bool resetHistory,
      float frameTimeMilliseconds) {
    ScopedGpuProfileZone(ctx, "Ray Reconstruction");
    ctx->setFramePassStage(RtxFramePassStage::DLSSRR);

    if (!useRayReconstruction()) {
      return;
    }

    bool dlssAutoExposure = true;
    mRecreate |= (mAutoExposure != dlssAutoExposure)
      || m_prevModel != model()
      || m_prevEnableTransformerModelD != enableTransformerModelD();
    mAutoExposure = dlssAutoExposure;
    m_prevModel = model();
    m_prevEnableTransformerModelD = enableTransformerModelD();

    if (mRecreate) {
      initializeRayReconstruction(ctx);
      mRecreate = false;
    }

    SceneManager& sceneManager = device()->getCommon()->getSceneManager();
    DebugView& debugView = ctx->getDevice()->getCommon()->metaDebugView();

    // prepare DLSS-RR inputs
    VkExtent3D workgroups = util::computeBlockCount(
      rtOutput.m_primaryLinearViewZ.view->imageInfo().extent, VkExtent3D { 16, 16, 1 });

    auto motionVectorInput = enableDLSSRRSurfaceReplacement() ? &rtOutput.m_primaryScreenSpaceMotionVectorDLSSRR : &rtOutput.m_primaryScreenSpaceMotionVector;
    auto depthInput = enableDLSSRRSurfaceReplacement() ? &rtOutput.m_primaryDepthDLSSRR.resource(Resources::AccessType::Read) : &rtOutput.m_primaryDepth;
    
    {
      ScopedGpuProfileZone(ctx, "Prepare DLSS");

      RayReconstructionArgs constants = { };
      constants.camera = sceneManager.getCamera().getShaderConstants();
      constants.useExternalExposure = !mAutoExposure ? 1 : 0;
      constants.rayReconstructionUseVirtualNormals = m_useVirtualNormals ? 1 : 0;
      constants.combineSpecularAlbedo = combineSpecularAlbedo() ? 1 : 0;
      constants.debugViewIdx = rtOutput.m_raytraceArgs.debugView;
      constants.debugKnob = rtOutput.m_raytraceArgs.debugKnob;
      constants.enableDemodulateRoughness = demodulateRoughness() ? 1 : 0;
      constants.enableDemodulateAttenuation = demodulateAttenuation() ? 1 : 0;
      constants.upscalerRoughnessDemodulationOffset = upscalerRoughnessDemodulationOffset();
      constants.upscalerRoughnessDemodulationMultiplier = upscalerRoughnessDemodulationMultiplier();
      constants.enableDLSSRRInputs = enableDLSSRRSurfaceReplacement();
      constants.filterHitT = filterHitT();
      constants.particleBufferMode = (uint32_t)getParticleBufferMode();
      constants.frameIdx = rtOutput.m_raytraceArgs.frameIdx;
      constants.enableDisocclusionMaskBlur = enableDisocclusionMaskBlur();
      constants.disocclusionMaskBlurRadius = disocclusionMaskBlurRadius();
      constants.rcpSquaredDisocclusionMaskBlurGaussianWeightSigma = 1.0f / (disocclusionMaskBlurNormalizedGaussianWeightSigma() * disocclusionMaskBlurNormalizedGaussianWeightSigma());
      ctx->updateBuffer(m_constants, 0, sizeof(constants), &constants);
      ctx->getCommandList()->trackResource<DxvkAccess::Read>(m_constants);

      // Inputs

      ctx->bindResourceBuffer(RAY_RECONSTRUCTION_CONSTANTS_INPUT, DxvkBufferSlice(m_constants, 0, m_constants->info().size));

      if (m_useVirtualNormals) {
        ctx->bindResourceView(RAY_RECONSTRUCTION_NORMALS_INPUT, nullptr, nullptr);
        ctx->bindResourceView(RAY_RECONSTRUCTION_VIRTUAL_NORMALS_INPUT, rtOutput.m_primaryVirtualWorldShadingNormalPerceptualRoughness.view, nullptr);
      } else {
        ctx->bindResourceView(RAY_RECONSTRUCTION_NORMALS_INPUT, rtOutput.m_primaryWorldShadingNormal.view, nullptr);
        ctx->bindResourceView(RAY_RECONSTRUCTION_VIRTUAL_NORMALS_INPUT, nullptr, nullptr);
      }
      ctx->bindResourceView(RAY_RECONSTRUCTION_PRIMARY_INDIRECT_SPECULAR_INPUT, rtOutput.m_primaryIndirectSpecularRadiance.view(Resources::AccessType::Read), nullptr);
      // Primary data
      ctx->bindResourceView(RAY_RECONSTRUCTION_PRIMARY_ATTENUATION_INPUT, rtOutput.m_primaryAttenuation.view, nullptr);
      ctx->bindResourceView(RAY_RECONSTRUCTION_PRIMARY_VIRTUAL_WORLD_SHADING_NORMAL_INPUT, rtOutput.m_primaryVirtualWorldShadingNormalPerceptualRoughness.view, nullptr);
      ctx->bindResourceView(RAY_RECONSTRUCTION_PRIMARY_DISOCCLUSION_MASK_INPUT, rtOutput.m_primaryDisocclusionThresholdMix.view, nullptr);

      // Secondary data
      ctx->bindResourceView(RAY_RECONSTRUCTION_SECONDARY_ALBEDO_INPUT, rtOutput.m_secondaryAlbedo.view, nullptr);
      ctx->bindResourceView(RAY_RECONSTRUCTION_SECONDARY_SPECULAR_ALBEDO_INPUT, rtOutput.m_secondarySpecularAlbedo.view(Resources::AccessType::Read), nullptr);
      ctx->bindResourceView(RAY_RECONSTRUCTION_SECONDARY_ATTENUATION_INPUT, rtOutput.m_secondaryAttenuation.view, nullptr);
      ctx->bindResourceView(RAY_RECONSTRUCTION_SECONDARY_VIRTUAL_WORLD_SHADING_NORMAL_INPUT, rtOutput.m_secondaryVirtualWorldShadingNormalPerceptualRoughness.view, nullptr);
      ctx->bindResourceView(RAY_RECONSTRUCTION_PRIMARY_CONE_RADIUS_INPUT, rtOutput.m_primaryConeRadius.view, nullptr);

      ctx->bindResourceView(RAY_RECONSTRUCTION_SHARED_FLAGS_INPUT, rtOutput.m_sharedFlags.view, nullptr);
      ctx->bindResourceView(RAY_RECONSTRUCTION_COMBINED_INPUT, rtOutput.m_compositeOutput.view(Resources::AccessType::Read), nullptr);
      // DLSSRR data
      ctx->bindResourceView(RAY_RECONSTRUCTION_NORMALS_DLSSRR_INPUT, rtOutput.m_primaryWorldShadingNormalDLSSRR.view(Resources::AccessType::Read), nullptr);
      ctx->bindResourceView(RAY_RECONSTRUCTION_DEPTHS_INPUT, depthInput->view, nullptr);
      ctx->bindResourceView(RAY_RECONSTRUCTION_MOTION_VECTOR_INPUT, motionVectorInput->view, nullptr);

      // Inputs/Outputs

      ctx->bindResourceView(RAY_RECONSTRUCTION_PRIMARY_ALBEDO_INPUT_OUTPUT, rtOutput.m_primaryAlbedo.view, nullptr);
      ctx->bindResourceView(RAY_RECONSTRUCTION_PRIMARY_SPECULAR_ALBEDO_INPUT_OUTPUT, rtOutput.m_primarySpecularAlbedo.view(Resources::AccessType::ReadWrite), nullptr);

      // Outputs

      ctx->bindResourceView(RAY_RECONSTRUCTION_NORMALS_OUTPUT, m_normals.view, nullptr);
      ctx->bindResourceView(RAY_RECONSTRUCTION_HIT_DISTANCE_OUTPUT, rtOutput.m_rayReconstructionHitDistance.view(Resources::AccessType::Write), nullptr);
      ctx->bindResourceView(RAY_RECONSTRUCTION_DEBUG_VIEW_OUTPUT, debugView.getDebugOutput(), nullptr);
      ctx->bindResourceView(RAY_RECONSTRUCTION_PRIMARY_DISOCCLUSION_MASK_OUTPUT, rtOutput.m_primaryDisocclusionMaskForRR.view(Resources::AccessType::Write), nullptr);

      ctx->bindShader(VK_SHADER_STAGE_COMPUTE_BIT, PrepareRayReconstructionShader::getShader());

      ctx->dispatch(workgroups.width, workgroups.height, workgroups.depth);
    }

    {
      // The DLSS y coordinate is pointing down
      float jitterOffset[2];
      RtCamera& camera = sceneManager.getCamera();
      camera.getJittering(jitterOffset);
      mMotionVectorScale = MotionVectorScale::Absolute;

      float motionVectorScale[2] = { 1.f,1.f };

      std::vector<Rc<DxvkImageView>> pInputs = {
        rtOutput.m_compositeOutput.view(Resources::AccessType::Read),
        rtOutput.m_primaryScreenSpaceMotionVector.view,
        rtOutput.m_primaryDepth.view,
        m_useVirtualNormals ? rtOutput.m_primaryVirtualWorldShadingNormalPerceptualRoughness.view : rtOutput.m_primaryWorldShadingNormal.view,
        rtOutput.getCurrentPrimaryWorldPositionWorldTriangleNormal().view(Resources::AccessType::Read),
        rtOutput.m_primaryAlbedo.view,
        rtOutput.m_sharedBiasCurrentColorMask.view(Resources::AccessType::Read),
        rtOutput.m_rayReconstructionParticleBuffer.view,
        m_normals.view,
        rtOutput.m_primarySpecularAlbedo.view(Resources::AccessType::Read),
        rtOutput.m_primaryPerceptualRoughness.view,
        rtOutput.m_rayReconstructionHitDistance.view(Resources::AccessType::Read),
        rtOutput.m_primaryScreenSpaceMotionVectorDLSSRR.view,
        rtOutput.m_primaryDepthDLSSRR.view(Resources::AccessType::Read)
      };

      const DxvkAutoExposure& autoExposure = device()->getCommon()->metaAutoExposure();
      if (!mAutoExposure) {
        pInputs.push_back(autoExposure.getExposureTexture().view);
      }

      std::vector<Rc<DxvkImageView>> pOutputs = {
        rtOutput.m_sharedBiasCurrentColorMask.view(Resources::AccessType::Write),
        rtOutput.m_finalOutput.view(Resources::AccessType::Write)
      };

      for (auto input : pInputs) {
        if (input == nullptr) {
          continue;
        }
        
        barriers.accessImage(
          input->image(),
          input->imageSubresources(),
          input->imageInfo().layout,
          input->imageInfo().stages,
          input->imageInfo().access,
          input->imageInfo().layout,
          VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
          VK_ACCESS_SHADER_READ_BIT);

#ifdef REMIX_DEVELOPMENT
        ctx->cacheResourceAliasingImageView(input);
#endif
      }

      for (auto output : pOutputs) {
        barriers.accessImage(
          output->image(),
          output->imageSubresources(),
          output->imageInfo().layout,
          output->imageInfo().stages,
          output->imageInfo().access,
          output->imageInfo().layout,
          VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
          VK_ACCESS_SHADER_WRITE_BIT);

#ifdef REMIX_DEVELOPMENT
        ctx->cacheResourceAliasingImageView(output);
#endif
      }

      barriers.recordCommands(ctx->getCommandList());


      // Note: DLSS-RR currently uses DLSS's depth input for "linear view depth", which is what our virtual linear view Z represents (not quite depth in the
      // technical sense but this is likely what they mean).
      auto normalsInput = &m_normals;
      // Note: Texture contains specular albedo in this case as DLSS happens after demodulation
      auto specularAlbedoInput = &rtOutput.m_primarySpecularAlbedo.resource(Resources::AccessType::Read);
      m_rayReconstructionContext->setWorldToViewMatrix(camera.getWorldToView());
      m_rayReconstructionContext->setViewToProjectionMatrix(camera.getViewToProjection());

      // Note: Add texture inputs added here to the pInputs array above to properly access the images.
      NGXRayReconstructionContext::NGXBuffers buffers;
      buffers.pUnresolvedColor = &rtOutput.m_compositeOutput.resource(Resources::AccessType::Read);
      buffers.pResolvedColor = &rtOutput.m_finalOutput.resource(Resources::AccessType::Write);
      buffers.pMotionVectors = motionVectorInput;
      buffers.pDepth = depthInput;
      buffers.pDiffuseAlbedo = &rtOutput.m_primaryAlbedo;
      buffers.pSpecularAlbedo = specularAlbedoInput;
      buffers.pExposure = &autoExposure.getExposureTexture();
      buffers.pPosition = &rtOutput.getCurrentPrimaryWorldPositionWorldTriangleNormal().resource(Resources::AccessType::Read);
      buffers.pNormals = normalsInput;
      buffers.pRoughness = &rtOutput.m_primaryPerceptualRoughness;
      buffers.pBiasCurrentColorMask = &rtOutput.m_sharedBiasCurrentColorMask.resource(Resources::AccessType::Read);
      buffers.pHitDistance = useSpecularHitDistance() ? &rtOutput.m_rayReconstructionHitDistance.resource(Resources::AccessType::Read) : nullptr;
      buffers.pInTransparencyLayer = getParticleBufferMode() == RayReconstructionParticleBufferMode::RayReconstructionUpscaling ? &rtOutput.m_rayReconstructionParticleBuffer : nullptr;
      buffers.pDisocclusionMask = enableDisocclusionMaskBlur()
        ? &rtOutput.m_primaryDisocclusionMaskForRR.resource(Resources::AccessType::Read)
        : &rtOutput.m_primaryDisocclusionThresholdMix;

      NGXRayReconstructionContext::NGXSettings settings;
      settings.resetAccumulation = resetHistory;
      settings.antiGhost = m_biasCurrentColorEnabled;
      settings.preExposure = mPreExposure;
      settings.jitterOffset[0] = jitterOffset[0];
      settings.jitterOffset[1] = jitterOffset[1];
      settings.motionVectorScale[0] = motionVectorScale[0];
      settings.motionVectorScale[1] = motionVectorScale[1];
      settings.autoExposure = mAutoExposure;
      settings.frameTimeMilliseconds = frameTimeMilliseconds;

      m_rayReconstructionContext->evaluateRayReconstruction(ctx, buffers, settings);

      for (auto output : pOutputs) {
        barriers.accessImage(
          output->image(),
          output->imageSubresources(),
          output->imageInfo().layout,
          VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
          VK_ACCESS_SHADER_WRITE_BIT,
          output->imageInfo().layout,
          output->imageInfo().stages,
          output->imageInfo().access);

        ctx->getCommandList()->trackResource<DxvkAccess::None>(output);
        ctx->getCommandList()->trackResource<DxvkAccess::Write>(output->image());
      }
      barriers.recordCommands(ctx->getCommandList());
    }
  }

  void DxvkRayReconstruction::showRayReconstructionImguiSettings(bool showAdvancedSettings) {
    RemixGui::Checkbox("Anti-Ghost", &m_biasCurrentColorEnabled);

    if (showAdvancedSettings) {
      bool presetChanged = RemixGui::Combo("DLSS-RR Preset", &pathTracerPresetObject(), "Default\0ReSTIR Finetuned\0");
      if (presetChanged) {
        RtxOptions::updatePathTracerPreset(pathTracerPreset());
      }

      constexpr ImGuiSliderFlags sliderFlags = ImGuiSliderFlags_AlwaysClamp;

      RemixGui::Checkbox("Use Virtual Normals", &m_useVirtualNormals);
      RemixGui::Combo("Particle Mode", &particleBufferModeObject(), "None\0DLSS-RR Upscaling\0");
      RemixGui::Checkbox("Use Specular Hit Distance", &useSpecularHitDistanceObject());
      RemixGui::Checkbox("Preserve Settings in Native Mode", &preserveSettingsInNativeModeObject());
      RemixGui::Checkbox("Combine Specular Albedo", &combineSpecularAlbedoObject());
      RemixGui::Checkbox("Filter Hit Distance", &filterHitTObject());
      RemixGui::Checkbox("Use DLSS-RR Specific Surface Replacement", &enableDLSSRRSurfaceReplacementObject());
      RemixGui::Checkbox("DLSS-RR Demodulate Attenuation", &demodulateAttenuationObject());
      RemixGui::Checkbox("DLSS-RR Detail Enhancement", &enableDetailEnhancementObject());
      RemixGui::Checkbox("Preprocess Secondary Signal", &preprocessSecondarySignalObject());
      RemixGui::Checkbox("DLSS-RR Demodulate Roughness", &demodulateRoughnessObject());
      RemixGui::DragFloat("DLSS-RR Roughness Sensitivity", &upscalerRoughnessDemodulationOffsetObject(), 0.01f, 0.0f, 2.0f, "%.3f");
      RemixGui::DragFloat("DLSS-RR Roughness Multiplier", &upscalerRoughnessDemodulationMultiplierObject(), 0.01f, 0.0f, 20.0f, "%.3f");
      RemixGui::Checkbox("Composite Volumetric Light", &compositeVolumetricLightObject());      
      RemixGui::Checkbox("Transformer Model D", &enableTransformerModelDObject());

      if (RemixGui::CollapsingHeader("Disocclusion Mask")) {
        ImGui::Indent();

        RemixGui::Checkbox("Blur", &enableDisocclusionMaskBlurObject());
        RemixGui::DragInt("Blur Radius", &disocclusionMaskBlurRadiusObject(), 1.f, 1, 64, "%d", sliderFlags);
        RemixGui::DragFloat("Blur Normalized Gaussian Weight Sigma", &disocclusionMaskBlurNormalizedGaussianWeightSigmaObject(), 0.01f, 0.0f, 3.0f, "%.3f", sliderFlags);

        ImGui::Unindent();
      }
    }
  }

  void DxvkRayReconstruction::setSettings(const uint32_t displaySize[2], const DLSSProfile profile, uint32_t outRenderSize[2]) {
    ScopedCpuProfileZone();
    // Handle the "auto" case, this is the param we'll pass to determine optimal resolution setting
    DLSSProfile actualProfile = profile;
    if (actualProfile == DLSSProfile::Auto) {
      actualProfile = getAutoProfile(displaySize[0], displaySize[1]);
    }

    if (mActualProfile == actualProfile && displaySize[0] == mDLSSOutputSize[0] && displaySize[1] == mDLSSOutputSize[1]) {
      // Nothing changed that would alter DLSS resolution(s), so return the last cached optimal render size
      outRenderSize[0] = mInputSize[0];
      outRenderSize[1] = mInputSize[1];
      return;
    }

    mActualProfile = actualProfile;

    // We need to force a recreation of resources before running DLSS.
    mRecreate = true;

    // Update our requested profile
    mProfile = profile;

    NVSDK_NGX_PerfQuality_Value perfQuality = profileToQuality(mActualProfile);

    if (!m_rayReconstructionContext) {
      m_rayReconstructionContext = m_device->getCommon()->metaNGXContext().createRayReconstructionContext();
    }
    if (m_rayReconstructionContext) {
      const auto optimalSettings = m_rayReconstructionContext->queryOptimalSettings(displaySize, perfQuality);

      mInputSize[0] = outRenderSize[0] = optimalSettings.optimalRenderSize[0];
      mInputSize[1] = outRenderSize[1] = optimalSettings.optimalRenderSize[1];
    }

    mDLSSOutputSize[0] = displaySize[0];
    mDLSSOutputSize[1] = displaySize[1];

    // Note: Input size used for DLSS must be less than or equal to the desired output size. This is a requirement of the DLSS API currently.
    assert(mInputSize[0] <= mDLSSOutputSize[0] && mInputSize[1] <= mDLSSOutputSize[1]);
  }

  void DxvkRayReconstruction::initializeRayReconstruction(Rc<DxvkContext> renderContext) {
    DxvkImageCreateInfo desc;
    desc.type = VK_IMAGE_TYPE_2D;
    desc.flags = 0;
    desc.sampleCount = VK_SAMPLE_COUNT_1_BIT;
    desc.extent = { mInputSize[0], mInputSize[1], 1 };
    desc.numLayers = 1;
    desc.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    desc.stages = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    desc.access = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    desc.tiling = VK_IMAGE_TILING_OPTIMAL;
    desc.layout = VK_IMAGE_LAYOUT_UNDEFINED;

    DxvkImageViewCreateInfo viewInfo;
    viewInfo.type = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
    viewInfo.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.minLevel = 0;
    viewInfo.minLayer = 0;
    viewInfo.numLayers = 1;
    viewInfo.format = desc.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    viewInfo.numLevels = desc.mipLevels = 1;

    viewInfo.format = desc.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    m_normals.image = m_device->createImage(desc, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, DxvkMemoryStats::Category::RTXRenderTarget, "RayReconstruction normal");
    m_normals.view = m_device->createImageView(m_normals.image, viewInfo);
    renderContext->changeImageLayout(m_normals.image, VK_IMAGE_LAYOUT_GENERAL);

    
    if (!m_rayReconstructionContext) {
      m_rayReconstructionContext = m_device->getCommon()->metaNGXContext().createRayReconstructionContext();
    }

    NVSDK_NGX_PerfQuality_Value perfQuality = profileToQuality(mActualProfile);

    if (m_rayReconstructionContext) {

      // Model to use for DLSS-RR
      NVSDK_NGX_RayReconstruction_Hint_Render_Preset dlssdModel = (model() == RayReconstructionModel::CNN)
        ? /* CNN */ NVSDK_NGX_RayReconstruction_Hint_Render_Preset_A
        : enableTransformerModelD()
          ? /* Transformer D */ NVSDK_NGX_RayReconstruction_Hint_Render_Preset_D
          : /* Transformer E - Truthful Shrimp */ NVSDK_NGX_RayReconstruction_Hint_Render_Preset_E;

      auto optimalSettings = m_rayReconstructionContext->queryOptimalSettings(mInputSize, perfQuality);

      m_rayReconstructionContext->initialize(renderContext, mInputSize, mDLSSOutputSize, mIsHDR, mInverseDepth, mAutoExposure, false, dlssdModel, perfQuality);
    }
  }
} // namespace dxvk