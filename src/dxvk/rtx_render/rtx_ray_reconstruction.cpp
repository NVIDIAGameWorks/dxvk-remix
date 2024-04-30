/*
* Copyright (c) 2024, NVIDIA CORPORATION. All rights reserved.
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
#include "rtx/pass/rayreconstruction/ray_reconstruction.h"
#include "dxvk_device.h"
#include "rtx_dlss.h"
#include "dxvk_scoped_annotation.h"
#include "rtx_ngx_wrapper.h"
#include "rtx_render/rtx_shader_manager.h"
#include "rtx_imgui.h"

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
        TEXTURE2D(RAY_RECONSTRUCTION_PRIMARY_INDIRECT_SPECULAR_INPUT)
        RW_TEXTURE2D(RAY_RECONSTRUCTION_NORMALS_OUTPUT)
        CONSTANT_BUFFER(RAY_RECONSTRUCTION_CONSTANTS_INPUT)
        RW_TEXTURE2D(RAY_RECONSTRUCTION_HIT_DISTANCE_OUTPUT)
        // Primary surface data
        RW_TEXTURE2D(RAY_RECONSTRUCTION_PRIMARY_ALBEDO_INPUT_OUTPUT)
        RW_TEXTURE2D(RAY_RECONSTRUCTION_PRIMARY_SPECULAR_ALBEDO_INPUT_OUTPUT)
        TEXTURE2D(RAY_RECONSTRUCTION_PRIMARY_ATTENUATION_INPUT)
        TEXTURE2D(RAY_RECONSTRUCTION_PRIMARY_VIRTUAL_WORLD_SHADING_NORMAL_INPUT)
        // Secondary surface data
        TEXTURE2D(RAY_RECONSTRUCTION_SECONDARY_ALBEDO_INPUT)
        TEXTURE2D(RAY_RECONSTRUCTION_SECONDARY_SPECULAR_ALBEDO_INPUT)
        TEXTURE2D(RAY_RECONSTRUCTION_SECONDARY_ATTENUATION_INPUT)
        TEXTURE2D(RAY_RECONSTRUCTION_SECONDARY_VIRTUAL_WORLD_SHADING_NORMAL_INPUT)

        TEXTURE2D(RAY_RECONSTRUCTION_COMBINED_INPUT)
        TEXTURE2D(RAY_RECONSTRUCTION_SHARED_FLAGS_INPUT)
        RW_TEXTURE2D(RAY_RECONSTRUCTION_DEBUG_VIEW_OUTPUT)

        TEXTURE2D(RAY_RECONSTRUCTION_NORMALS_DLSSRR_INPUT)
        TEXTURE2D(RAY_RECONSTRUCTION_DEPTHS_INPUT)
        TEXTURE2D(RAY_RECONSTRUCTION_MOTION_VECTOR_INPUT)

        END_PARAMETER()
    };
  }

  DxvkRayReconstruction::DxvkRayReconstruction(DxvkDevice* device)
    : DxvkDLSS(device) {

    DxvkBufferCreateInfo info = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    info.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    info.stages = VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    info.access = VK_ACCESS_TRANSFER_WRITE_BIT;
    info.size = sizeof(RayReconstructionArgs);
    m_constants = device->createBuffer(info, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, DxvkMemoryStats::Category::RTXBuffer);
  }

  bool DxvkRayReconstruction::supportsRayReconstruction() const {
    return m_device->getCommon()->metaNGXContext().supportsRayReconstruction();
  }

  DxvkRayReconstruction::RayReconstructionParticleBufferMode DxvkRayReconstruction::getParticleBufferMode() {
    return RtxOptions::Get()->isRayReconstructionEnabled() ? particleBufferMode() : RayReconstructionParticleBufferMode::None;
  }

  void DxvkRayReconstruction::release() {
    mRecreate = true;
    m_normals.image = nullptr;
    m_normals.view = nullptr;

    NGXRayReconstructionContext* dlssRRWrapper = NGXRayReconstructionContext::getInstance(m_device);
    if (dlssRRWrapper) {
      dlssRRWrapper->releaseNGXFeature();
    }
  }
  
  void DxvkRayReconstruction::onDestroy() {
    release();
    NGXRayReconstructionContext::releaseInstance();
  }

  bool DxvkRayReconstruction::useRayReconstruction() {
    return supportsRayReconstruction() && RtxOptions::Get()->isRayReconstructionEnabled();
  }

  void DxvkRayReconstruction::dispatch(
      Rc<RtxContext> ctx,
      DxvkBarrierSet& barriers,
      const Resources::RaytracingOutput& rtOutput,
      bool resetHistory,
      float frameTimeSecs) {
    ScopedGpuProfileZone(ctx, "Ray Reconstruction");

    if (!useRayReconstruction()) {
      return;
    }

    bool dlssAutoExposure = true;
    mRecreate |= (mAutoExposure != dlssAutoExposure);
    mAutoExposure = dlssAutoExposure;

    if (mRecreate) {
      initializeRayReconstruction(ctx);
      mRecreate = false;
    }

    SceneManager& sceneManager = device()->getCommon()->getSceneManager();

    // prepare DLSS-RR inputs
    VkExtent3D workgroups = util::computeBlockCount(
      rtOutput.m_primaryLinearViewZ.view->imageInfo().extent, VkExtent3D { 16, 16, 1 });

    auto motionVectorInput = enableDLSSRRSurfaceReplacement() ? &rtOutput.m_primaryScreenSpaceMotionVectorDLSSRR : &rtOutput.m_primaryScreenSpaceMotionVector;
    auto depthInput = enableDLSSRRSurfaceReplacement() ? &rtOutput.m_primaryDepthDLSSRR : &rtOutput.m_primaryDepth;
    
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
      ctx->updateBuffer(m_constants, 0, sizeof(constants), &constants);
      ctx->getCommandList()->trackResource<DxvkAccess::Read>(m_constants);

      ctx->bindResourceBuffer(RAY_RECONSTRUCTION_CONSTANTS_INPUT, DxvkBufferSlice(m_constants, 0, m_constants->info().size));

      if (m_useVirtualNormals) {
        ctx->bindResourceView(RAY_RECONSTRUCTION_NORMALS_INPUT, nullptr, nullptr);
        ctx->bindResourceView(RAY_RECONSTRUCTION_VIRTUAL_NORMALS_INPUT, rtOutput.m_primaryVirtualWorldShadingNormalPerceptualRoughness.view, nullptr);
      } else {
        ctx->bindResourceView(RAY_RECONSTRUCTION_NORMALS_INPUT, rtOutput.m_primaryWorldShadingNormal.view, nullptr);
        ctx->bindResourceView(RAY_RECONSTRUCTION_VIRTUAL_NORMALS_INPUT, nullptr, nullptr);
      }
      ctx->bindResourceView(RAY_RECONSTRUCTION_PRIMARY_INDIRECT_SPECULAR_INPUT, rtOutput.m_primaryIndirectSpecularRadiance.view(Resources::AccessType::Read), nullptr);
      ctx->bindResourceView(RAY_RECONSTRUCTION_HIT_DISTANCE_OUTPUT, rtOutput.m_rayReconstructionHitDistance.view(Resources::AccessType::Write), nullptr);
      ctx->bindResourceView(RAY_RECONSTRUCTION_NORMALS_OUTPUT, m_normals.view, nullptr);
      // Primary data
      ctx->bindResourceView(RAY_RECONSTRUCTION_PRIMARY_ALBEDO_INPUT_OUTPUT, rtOutput.m_primaryAlbedo.view, nullptr);
      ctx->bindResourceView(RAY_RECONSTRUCTION_PRIMARY_SPECULAR_ALBEDO_INPUT_OUTPUT, rtOutput.m_primarySpecularAlbedo.view(Resources::AccessType::ReadWrite), nullptr);
      ctx->bindResourceView(RAY_RECONSTRUCTION_PRIMARY_ATTENUATION_INPUT, rtOutput.m_primaryAttenuation.view, nullptr);
      ctx->bindResourceView(RAY_RECONSTRUCTION_PRIMARY_VIRTUAL_WORLD_SHADING_NORMAL_INPUT, rtOutput.m_primaryVirtualWorldShadingNormalPerceptualRoughness.view, nullptr);
      // Secondary data
      ctx->bindResourceView(RAY_RECONSTRUCTION_SECONDARY_ALBEDO_INPUT, rtOutput.m_secondaryAlbedo.view, nullptr);
      ctx->bindResourceView(RAY_RECONSTRUCTION_SECONDARY_SPECULAR_ALBEDO_INPUT, rtOutput.m_secondarySpecularAlbedo.view(Resources::AccessType::Read), nullptr);
      ctx->bindResourceView(RAY_RECONSTRUCTION_SECONDARY_ATTENUATION_INPUT, rtOutput.m_secondaryAttenuation.view, nullptr);
      ctx->bindResourceView(RAY_RECONSTRUCTION_SECONDARY_VIRTUAL_WORLD_SHADING_NORMAL_INPUT, rtOutput.m_secondaryVirtualWorldShadingNormalPerceptualRoughness.view, nullptr);

      // DLSSRR data
      ctx->bindResourceView(RAY_RECONSTRUCTION_NORMALS_DLSSRR_INPUT, rtOutput.m_primaryWorldShadingNormalDLSSRR.view, nullptr);
      ctx->bindResourceView(RAY_RECONSTRUCTION_DEPTHS_INPUT, depthInput->view, nullptr);
      ctx->bindResourceView(RAY_RECONSTRUCTION_MOTION_VECTOR_INPUT, motionVectorInput->view, nullptr);

      DebugView& debugView = ctx->getDevice()->getCommon()->metaDebugView();
      ctx->bindResourceView(RAY_RECONSTRUCTION_DEBUG_VIEW_OUTPUT, debugView.getDebugOutput(), nullptr);
      ctx->bindResourceView(RAY_RECONSTRUCTION_SHARED_FLAGS_INPUT, rtOutput.m_sharedFlags.view, nullptr);
      ctx->bindResourceView(RAY_RECONSTRUCTION_COMBINED_INPUT, rtOutput.m_compositeOutput.view(Resources::AccessType::Read), nullptr);

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
        rtOutput.m_primaryDepthDLSSRR.view
      };

      const DxvkAutoExposure& autoExposure = device()->getCommon()->metaAutoExposure();
      if (!mAutoExposure)
        pInputs.push_back(autoExposure.getExposureTexture().view);

      std::vector<Rc<DxvkImageView>> pOutputs = {
        rtOutput.m_sharedBiasCurrentColorMask.view(Resources::AccessType::Read),
        rtOutput.m_finalOutput.view
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
      }

      barriers.recordCommands(ctx->getCommandList());

      NGXRayReconstructionContext* ngxInstance = NGXRayReconstructionContext::getInstance(m_device);

      // Note: DLSS-RR currently uses DLSS's depth input for "linear view depth", which is what our virtual linear view Z represents (not quite depth in the
      // technical sense but this is likely what they mean).
      auto normalsInput = &m_normals;
      // Note: Texture contains specular albedo in this case as DLSS happens after demodulation
      auto specularAlbedoInput = &rtOutput.m_primarySpecularAlbedo.resource(Resources::AccessType::Read);
      ngxInstance->setWorldToViewMatrix(camera.getWorldToView());
      ngxInstance->setViewToProjectionMatrix(camera.getViewToProjection());

      // Note: Add texture inputs added here to the pInputs array above to properly access the images.
      NGXRayReconstructionContext::NGXBuffers buffers;
      buffers.pUnresolvedColor = &rtOutput.m_compositeOutput.resource(Resources::AccessType::Read);
      buffers.pResolvedColor = &rtOutput.m_finalOutput;
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

      NGXRayReconstructionContext::NGXSettings settings;
      settings.resetAccumulation = resetHistory;
      settings.antiGhost = m_biasCurrentColorEnabled;
      settings.sharpness = 0.f;
      settings.preExposure = mPreExposure;
      settings.jitterOffset[0] = jitterOffset[0];
      settings.jitterOffset[1] = jitterOffset[1];
      settings.motionVectorScale[0] = motionVectorScale[0];
      settings.motionVectorScale[1] = motionVectorScale[1];
      settings.autoExposure = mAutoExposure;
      settings.frameTimeSecs = frameTimeSecs;

      ngxInstance->evaluateRayReconstruction(ctx, buffers, settings);

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
    ImGui::Checkbox("Anti-Ghost", &m_biasCurrentColorEnabled);

    if (showAdvancedSettings) {
      bool presetChanged = ImGui::Combo("DLSS-RR Preset", &pathTracerPresetObject(), "Default\0ReSTIR Finetuned\0");
      if (presetChanged) {
        RtxOptions::Get()->updatePathTracerPreset(pathTracerPreset());
      }
      
      ImGui::Checkbox("Use Virtual Normals", &m_useVirtualNormals);
      ImGui::Combo("Particle Mode", &particleBufferModeObject(), "None\0DLSS-RR Upscaling\0");
      ImGui::Checkbox("Use Specular Hit Distance", &useSpecularHitDistanceObject());
      ImGui::Checkbox("Preserve Settings in Native Mode", &preserveSettingsInNativeModeObject());
      ImGui::Checkbox("Combine Specular Albedo", &combineSpecularAlbedoObject());
      ImGui::Checkbox("Filter Hit Distance", &filterHitTObject());
      ImGui::Checkbox("Use DLSS-RR Specific Surface Replacement", &enableDLSSRRSurfaceReplacementObject());
      ImGui::Checkbox("DLSS-RR Demodulate Attenuation", &demodulateAttenuationObject());
      ImGui::Checkbox("DLSS-RR Detail Enhancement", &enableDetailEnhancementObject());
      ImGui::Checkbox("Preprocess Secondary Signal", &preprocessSecondarySignalObject());
      ImGui::Checkbox("DLSS-RR Demodulate Roughness", &demodulateRoughnessObject());
      ImGui::DragFloat("DLSS-RR Roughness Sensitivity", &upscalerRoughnessDemodulationOffsetObject(), 0.01f, 0.0f, 2.0f, "%.3f");
      ImGui::DragFloat("DLSS-RR Roughness Multiplier", &upscalerRoughnessDemodulationMultiplierObject(), 0.01f, 0.0f, 20.0f, "%.3f");
      ImGui::Checkbox("Composite Volumetric Light", &compositeVolumetricLightObject());
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

    if (mProfile == DLSSProfile::FullResolution) {
      mInputSize[0] = outRenderSize[0] = displaySize[0];
      mInputSize[1] = outRenderSize[1] = displaySize[1];
    } else {
      NVSDK_NGX_PerfQuality_Value perfQuality = profileToQuality(mActualProfile);

      auto optimalSettings = NGXRayReconstructionContext::getInstance(m_device)->queryOptimalSettings(displaySize, perfQuality);

      const int step = 32;
      optimalSettings.optimalRenderSize[0] = (optimalSettings.optimalRenderSize[0] + step - 1) / step * step;
      optimalSettings.optimalRenderSize[1] = (optimalSettings.optimalRenderSize[1] + step - 1) / step * step;
      mInputSize[0] = outRenderSize[0] = optimalSettings.optimalRenderSize[0];
      mInputSize[1] = outRenderSize[1] = optimalSettings.optimalRenderSize[1];
    }

    mDLSSOutputSize[0] = displaySize[0];
    mDLSSOutputSize[1] = displaySize[1];
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

    NGXRayReconstructionContext* dlssWrapper = NGXRayReconstructionContext::getInstance(m_device);
    dlssWrapper->releaseNGXFeature();

    NVSDK_NGX_PerfQuality_Value perfQuality = profileToQuality(mProfile);

    auto optimalSettings = dlssWrapper->queryOptimalSettings(mInputSize, perfQuality);

    dlssWrapper->initialize(renderContext, mInputSize, mDLSSOutputSize, mIsHDR, mInverseDepth, mAutoExposure, false, perfQuality);
  }
} // namespace dxvk