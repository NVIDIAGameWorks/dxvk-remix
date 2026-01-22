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
#include "rtx_context.h"
#include "rtx_taa.h"
#include "dxvk_device.h"
#include "dxvk_scoped_annotation.h"
#include "rtx_render/rtx_shader_manager.h"
#include "rtx/pass/temporal_aa/temporal_aa.h"

#include <rtx_shaders/temporal_aa.h>
#include <pxr/base/arch/math.h>
#include "rtx_imgui.h"

namespace dxvk {
  // Defined within an unnamed namespace to ensure unique definition across binary
  namespace {
    class TemporalAaShader : public ManagedShader
    {
      SHADER_SOURCE(TemporalAaShader, VK_SHADER_STAGE_COMPUTE_BIT, temporal_aa)
    
      PUSH_CONSTANTS(TemporalAaArgs)
    
      BEGIN_PARAMETER()
        TEXTURE2D(TAA_INPUT)
        TEXTURE2D(TAA_FEEDBACK_INPUT)
        TEXTURE2D(TAA_PRIMARY_SCREEN_SPACE_MOTION_INPUT)
        RW_TEXTURE2D(TAA_FEEDBACK_OUTPUT)
        RW_TEXTURE2D(TAA_OUTPUT)
        SAMPLER(TAA_LINEAR_SAMPLER)
      END_PARAMETER()
    };

    PREWARM_SHADER_PIPELINE(TemporalAaShader);
  }


  DxvkTemporalAA::DxvkTemporalAA(DxvkDevice* device)
  : RtxPass(device), m_vkd(device->vkd()) {
  }

  DxvkTemporalAA::~DxvkTemporalAA() {
  }

  void DxvkTemporalAA::showImguiSettings() {
    RemixGui::DragFloat("Maximum Radiance", &maximumRadianceObject(), 0.01f, 1e8f, 100.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
    RemixGui::DragFloat("New Frame Weight", &newFrameWeightObject(), 0.001f, 1.0f, 0.001f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
    RemixGui::DragFloat("Color Clamping Factor", &colorClampingFactorObject(), 0.001f, 0.005f, FLT_MAX, "%.2f", ImGuiSliderFlags_AlwaysClamp);
  }

  void DxvkTemporalAA::dispatch(
    Rc<DxvkContext> ctx,
    Rc<DxvkSampler> linearSampler,
    const uvec2& mainCameraResolution,
    const float jitterOffset[2],
    const Resources::Resource& colorTextureInput,
    const Resources::Resource& primaryScreenSpaceMotionVector,
    const Resources::Resource& colorTextureOutput,
    const bool isUpscale) {
    ScopedGpuProfileZone(ctx, "TAA");

    const VkExtent3D& inputSize = colorTextureInput.image->info().extent;
    const VkExtent3D& outputSize = colorTextureOutput.image->info().extent;

    TemporalAaArgs temporalAaArgs = {};
    temporalAaArgs.isTaaUpscale = isUpscale;
    temporalAaArgs.imageSizeOutput = { (uint) outputSize.width, (uint) outputSize.height };
    temporalAaArgs.invImageSizeOutput = { 1.0f / (float)outputSize.width, 1.0f / (float)outputSize.height };
    temporalAaArgs.invMainCameraResolution = float2(1.0f / (float)mainCameraResolution.x, 1.0f / (float)mainCameraResolution.y);
    temporalAaArgs.jitterOffset = float2(jitterOffset[0], jitterOffset[1]);
    temporalAaArgs.inputOverOutputViewSize = float2((float)inputSize.width / outputSize.width, (float)inputSize.height / outputSize.height);
    temporalAaArgs.upscalingFactor = (float)outputSize.width / (float)inputSize.width;
    temporalAaArgs.maximumRadiance = maximumRadiance();
    temporalAaArgs.invMaximumRadiance = 1.0f / maximumRadiance();
    temporalAaArgs.colorClampingFactor = colorClampingFactor();
    temporalAaArgs.newFrameWeight = newFrameWeight();

    ctx->pushConstants(0, sizeof(temporalAaArgs), &temporalAaArgs);

    const uint32_t frameIdx = ctx->getDevice()->getCurrentFrameId();
    const uint32_t feedbackInputIdx = (frameIdx & 1);
    const uint32_t feedbackOutputIdx = (feedbackInputIdx ^ 1);

    ctx->bindResourceView(TAA_INPUT, colorTextureInput.view, nullptr);
    ctx->bindResourceView(TAA_FEEDBACK_INPUT, m_taaFeedbackTexture[feedbackInputIdx].view, nullptr);
    ctx->bindResourceView(TAA_PRIMARY_SCREEN_SPACE_MOTION_INPUT, primaryScreenSpaceMotionVector.view, nullptr);
    ctx->bindResourceView(TAA_FEEDBACK_OUTPUT, m_taaFeedbackTexture[feedbackOutputIdx].view, nullptr);
    ctx->bindResourceView(TAA_OUTPUT, colorTextureOutput.view, nullptr);
    ctx->bindResourceSampler(TAA_LINEAR_SAMPLER, linearSampler);

    ctx->bindShader(VK_SHADER_STAGE_COMPUTE_BIT, TemporalAaShader::getShader());

    const VkExtent3D workgroups = util::computeBlockCount(outputSize, VkExtent3D { TAA_TILE_SIZE_X , TAA_TILE_SIZE_Y, 1 } );
    ctx->dispatch(workgroups.width, workgroups.height, workgroups.depth);
  }

  bool DxvkTemporalAA::isEnabled() const {
    return RtxOptions::isTAAEnabled();
  }

  void DxvkTemporalAA::createTargetResource(Rc<DxvkContext>& ctx, const VkExtent3D& targetExtent) {
    // TAA intermediate textures
    for (uint32_t i = 0; i < 2; i++) {
      m_taaFeedbackTexture[i] = Resources::createImageResource(ctx, "TAA feedback texture", targetExtent, VK_FORMAT_R32G32B32A32_SFLOAT);
    }
  }

  void DxvkTemporalAA::releaseTargetResource() {
    for (uint32_t i = 0; i < 2; i++) {
      m_taaFeedbackTexture[i].reset();
    }
  }
}
