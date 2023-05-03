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
#include "rtx_context.h"
#include "rtx_bloom.h"
#include "dxvk_device.h"
#include "dxvk_scoped_annotation.h"
#include "rtx_render/rtx_shader_manager.h"
#include "rtx/pass/bloom/bloom.h"

#include <rtx_shaders/bloom_downscale.h>
#include <rtx_shaders/bloom_blur.h>
#include <rtx_shaders/bloom_composite.h>
#include <pxr/base/arch/math.h>
#include "rtx_imgui.h"

namespace dxvk {
  // Defined within an unnamed namespace to ensure unique definition across binary
  namespace {
    class DownscaleShader : public ManagedShader
    {
      SHADER_SOURCE(DownscaleShader, VK_SHADER_STAGE_COMPUTE_BIT, bloom_downscale)

      PUSH_CONSTANTS(BloomDownscaleArgs)

      BEGIN_PARAMETER()
        TEXTURE2D(BLOOM_DOWNSCALE_INPUT)
        RW_TEXTURE2D(BLOOM_DOWNSCALE_OUTPUT)
      END_PARAMETER()
    };

    PREWARM_SHADER_PIPELINE(DownscaleShader);

    class BlurShader : public ManagedShader
    {
      SHADER_SOURCE(BlurShader, VK_SHADER_STAGE_COMPUTE_BIT, bloom_blur)

      PUSH_CONSTANTS(BloomBlurArgs)

      BEGIN_PARAMETER()
        SAMPLER2D(BLOOM_BLUR_INPUT)
        RW_TEXTURE2D(BLOOM_BLUR_OUTPUT)
      END_PARAMETER()
    };

    PREWARM_SHADER_PIPELINE(BlurShader);

    class CompositeShader : public ManagedShader
    {
      SHADER_SOURCE(CompositeShader, VK_SHADER_STAGE_COMPUTE_BIT, bloom_composite)

      PUSH_CONSTANTS(BloomCompositeArgs)

      BEGIN_PARAMETER()
        RW_TEXTURE2D(BLOOM_COMPOSITE_COLOR_INPUT_OUTPUT)
        SAMPLER2D(BLOOM_COMPOSITE_BLOOM)
      END_PARAMETER()
    };

    PREWARM_SHADER_PIPELINE(CompositeShader);
  }
  
  DxvkBloom::DxvkBloom(DxvkDevice* device): RtxPass(device), m_vkd(device->vkd()) {
  }
  
  DxvkBloom::~DxvkBloom()  {
  }

  void DxvkBloom::showImguiSettings()
  {
    ImGui::Checkbox("Bloom Enabled", &enableObject());
    ImGui::DragFloat("Bloom Sigma", &sigmaObject(), 0.001f, 0.f, 1.f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
    ImGui::DragFloat("Bloom Intensity", &intensityObject(), 0.001f, 0.f, 1.f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
  }

  void DxvkBloom::dispatch(
    Rc<DxvkCommandList> cmdList,
    Rc<RtxContext> ctx,
    Rc<DxvkSampler> linearSampler,
    const Resources::Resource& inOutColorBuffer)
  {
    ScopedGpuProfileZone(ctx, "Bloom");

    ctx->setPushConstantBank(DxvkPushConstantBank::RTX);

    dispatchDownscale(cmdList, ctx, inOutColorBuffer, m_bloomBuffer0);
    dispatchBlur<false>(cmdList, ctx, linearSampler, m_bloomBuffer0, m_bloomBuffer1);
    dispatchBlur<true>(cmdList, ctx, linearSampler, m_bloomBuffer1, m_bloomBuffer0);
    dispatchComposite(cmdList, ctx, linearSampler, inOutColorBuffer, m_bloomBuffer0);
  }

  void DxvkBloom::dispatchDownscale(
    Rc<DxvkCommandList> cmdList,
    Rc<DxvkContext> ctx,
    const Resources::Resource& inputBuffer,
    const Resources::Resource& outputBuffer)
  {
    ScopedGpuProfileZone(ctx, "Downscale");

    VkExtent3D inputSize = inputBuffer.image->info().extent;

    // Prepare shader arguments
    BloomDownscaleArgs pushArgs = {};
    pushArgs.inputSize = { (int)inputSize.width, (int)inputSize.height };
    ctx->pushConstants(0, sizeof(pushArgs), &pushArgs);

    VkExtent3D workgroups = util::computeBlockCount(inputSize, VkExtent3D{ 16 , 16, 1 });

    ctx->bindResourceView(BLOOM_DOWNSCALE_INPUT, inputBuffer.view, nullptr);
    ctx->bindResourceView(BLOOM_DOWNSCALE_OUTPUT, outputBuffer.view, nullptr);
    ctx->bindShader(VK_SHADER_STAGE_COMPUTE_BIT, DownscaleShader::getShader());
    ctx->dispatch(workgroups.width, workgroups.height, workgroups.depth);
  }

  template<bool isVertical>
  void DxvkBloom::dispatchBlur(
    Rc<DxvkCommandList> cmdList,
    Rc<DxvkContext> ctx,
    Rc<DxvkSampler> linearSampler,
    const Resources::Resource& inputBuffer,
    const Resources::Resource& outputBuffer)
  {
    ScopedGpuProfileZone(ctx, isVertical ? "Vertical Blur" : "Horizontal Blur");

    VkExtent3D inputSize = inputBuffer.image->info().extent;

    // Prepare shader arguments
    BloomBlurArgs pushArgs = {};
    pushArgs.imageSize = { (int)inputSize.width, (int)inputSize.height };
    pushArgs.invImageSize = { 1.f / (float)inputSize.width, 1.f / (float)inputSize.height };

    float bloomSigmaInPixels = sigma() * (float)inputSize.height;

    float effectiveSigma = bloomSigmaInPixels * 0.25f;
    effectiveSigma = std::min(effectiveSigma, 100.f);
    effectiveSigma = std::max(effectiveSigma, 1.f);

    if (isVertical)
      pushArgs.pixstep = { 0.f, 1.f };
    else
      pushArgs.pixstep = { 1.f, 0.f };

    pushArgs.argumentScale = -1.f / (2.0f * effectiveSigma * effectiveSigma);
    pushArgs.normalizationScale = 1.f / (sqrtf(2.f * (float)M_PI) * effectiveSigma);
    pushArgs.numSamples = (int)roundf(effectiveSigma * 4.f);
    ctx->pushConstants(0, sizeof(pushArgs), &pushArgs);

    VkExtent3D workgroups = util::computeBlockCount(inputSize, VkExtent3D{ 16 , 16, 1 });

    ctx->bindResourceView(BLOOM_BLUR_INPUT, inputBuffer.view, nullptr);
    ctx->bindResourceSampler(BLOOM_BLUR_INPUT, linearSampler);
    ctx->bindResourceView(BLOOM_BLUR_OUTPUT, outputBuffer.view, nullptr);
    ctx->bindShader(VK_SHADER_STAGE_COMPUTE_BIT, BlurShader::getShader());
    ctx->dispatch(workgroups.width, workgroups.height, workgroups.depth);
  }

  void DxvkBloom::dispatchComposite(
    Rc<DxvkCommandList> cmdList,
    Rc<DxvkContext> ctx,
    Rc<DxvkSampler> linearSampler,
    const Resources::Resource& inOutColorBuffer,
    const Resources::Resource& bloomBuffer)
  {
    ScopedGpuProfileZone(ctx, "Composite");

    VkExtent3D outputSize = inOutColorBuffer.image->info().extent;

    // Prepare shader arguments
    BloomCompositeArgs pushArgs = {};
    pushArgs.imageSize = { (int)outputSize.width, (int)outputSize.height };
    pushArgs.invImageSize = { 1.f / (float)outputSize.width, 1.f / (float)outputSize.height };
    pushArgs.blendFactor = std::max(0.f, std::min(1.f, intensity()));
    ctx->pushConstants(0, sizeof(pushArgs), &pushArgs);

    VkExtent3D workgroups = util::computeBlockCount(outputSize, VkExtent3D{ 16 , 16, 1 });

    ctx->bindResourceView(BLOOM_COMPOSITE_COLOR_INPUT_OUTPUT, inOutColorBuffer.view, nullptr);
    ctx->bindResourceView(BLOOM_COMPOSITE_BLOOM, bloomBuffer.view, nullptr);
    ctx->bindResourceSampler(BLOOM_COMPOSITE_BLOOM, linearSampler);
    ctx->bindShader(VK_SHADER_STAGE_COMPUTE_BIT, CompositeShader::getShader());
    ctx->dispatch(workgroups.width, workgroups.height, workgroups.depth);
  }

  void DxvkBloom::createTargetResource(Rc<DxvkContext>& ctx, const VkExtent3D& targetExtent) {
    m_bloomBuffer0 = Resources::createImageResource(ctx, "bloom buffer 0", { util::ceilDivide(targetExtent.width, 4), util::ceilDivide(targetExtent.height, 4), 1 }, VK_FORMAT_R16G16B16A16_SFLOAT);
    m_bloomBuffer1 = Resources::createImageResource(ctx, "bloom buffer 1", { util::ceilDivide(targetExtent.width, 4), util::ceilDivide(targetExtent.height, 4), 1 }, VK_FORMAT_R16G16B16A16_SFLOAT);
  }

  void DxvkBloom::releaseTargetResource() {
    m_bloomBuffer0.reset();
    m_bloomBuffer1.reset();
  }

  bool DxvkBloom::isActive() {
    return enable();
  }
}
