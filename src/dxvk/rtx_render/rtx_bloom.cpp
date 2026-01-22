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
#include "rtx_bloom.h"
#include "dxvk_device.h"
#include "dxvk_scoped_annotation.h"
#include "rtx_render/rtx_shader_manager.h"
#include "rtx/pass/bloom/bloom.h"

#include <rtx_shaders/bloom_downsample.h>
#include <rtx_shaders/bloom_upsample.h>
#include <rtx_shaders/bloom_composite.h>
#include "rtx_imgui.h"

namespace dxvk {
  // Defined within an unnamed namespace to ensure unique definition across binary
  namespace {
    class BloomDownsampleShader : public ManagedShader
    {
      SHADER_SOURCE(BloomDownsampleShader, VK_SHADER_STAGE_COMPUTE_BIT, bloom_downsample)

      PUSH_CONSTANTS(BloomDownsampleArgs)

      BEGIN_PARAMETER()
        SAMPLER2D(BLOOM_DOWNSAMPLE_INPUT)
      RW_TEXTURE2D(BLOOM_DOWNSAMPLE_OUTPUT)
      END_PARAMETER()
    };

    PREWARM_SHADER_PIPELINE(BloomDownsampleShader);

    class BloomUpsampleShader : public ManagedShader
    {
      SHADER_SOURCE(BloomUpsampleShader, VK_SHADER_STAGE_COMPUTE_BIT, bloom_upsample)

      PUSH_CONSTANTS(BloomUpsampleArgs)

      BEGIN_PARAMETER()
        SAMPLER2D(BLOOM_UPSAMPLE_INPUT)
        RW_TEXTURE2D(BLOOM_UPSAMPLE_OUTPUT)
      END_PARAMETER()
    };

    PREWARM_SHADER_PIPELINE(BloomUpsampleShader);

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

  void DxvkBloom::showImguiSettings() {
    ImGui::Indent();
    RemixGui::Checkbox("Bloom Enabled", &enableObject());
    ImGui::Indent();
    RemixGui::DragFloat("Intensity##bloom", &burnIntensityObject(), 0.05f, 0.f, 5.f, "%.2f");
    RemixGui::DragFloat("Threshold##bloom", &luminanceThresholdObject(), 0.05f, 0.f, 100.f, "%.2f");
    RemixGui::SliderInt("Radius##bloom", &stepsObject(), 4, MaxBloomSteps);
    ImGui::Unindent();
    ImGui::Unindent();
  }

  void DxvkBloom::dispatch(Rc<RtxContext> ctx, 
                           Rc<DxvkSampler> linearSampler, 
                           const Resources::Resource& inOutColorBuffer) {
    ScopedGpuProfileZone(ctx, "Bloom");
    ctx->setFramePassStage(RtxFramePassStage::Bloom);

    ctx->setPushConstantBank(DxvkPushConstantBank::RTX);

    const Resources::Resource* res[] = {
      &inOutColorBuffer,
      &m_bloomBuffer[0],
      &m_bloomBuffer[1],
      &m_bloomBuffer[2],
      &m_bloomBuffer[3],
      &m_bloomBuffer[4],
      &m_bloomBuffer[5],
      &m_bloomBuffer[6],
      &m_bloomBuffer[7],
    };
    static_assert(MaxBloomSteps == std::size(res) - 1);

    const int bloomDepth = std::clamp(steps(), 1, MaxBloomSteps);

    for (int i = 0; i < bloomDepth; i++) {
      dispatchDownsampleStep(ctx, linearSampler, *res[i], *res[i + 1], i == 0);
    }

    for (int i = bloomDepth; i > 1; i--) {
      dispatchUpsampleStep(ctx, linearSampler, *res[i], *res[i - 1]);
    }

    dispatchComposite(ctx, linearSampler, inOutColorBuffer, m_bloomBuffer[0]);
  }

  void DxvkBloom::dispatchDownsampleStep(
    Rc<DxvkContext> ctx,
    const Rc<DxvkSampler>& linearSampler,
    const Resources::Resource& inputBuffer,
    const Resources::Resource& outputBuffer,
    bool initial) {
    ScopedGpuProfileZone(ctx, "Bloom Downsample");

    const VkExtent3D inputSize = inputBuffer.image->info().extent;
    const VkExtent3D outputSize = outputBuffer.image->info().extent;

    // Prepare shader arguments
    BloomDownsampleArgs pushArgs = {};
    pushArgs.inputSizeInverse = { 1.0f / float(inputSize.width), 1.0f / float(inputSize.height) };
    pushArgs.downsampledOutputSize = { outputSize.width, outputSize.height };
    pushArgs.downsampledOutputSizeInverse = { 1.0f / float(outputSize.width), 1.0f / float(outputSize.height) };
    pushArgs.threshold = initial ? std::max(0.01f, luminanceThreshold()) : -1;
    ctx->pushConstants(0, sizeof(pushArgs), &pushArgs);

    const VkExtent3D workgroups = util::computeBlockCount(outputSize, VkExtent3D{ 16, 16, 1 });

    ctx->bindResourceView(BLOOM_DOWNSAMPLE_INPUT, inputBuffer.view, nullptr);
    ctx->bindResourceSampler(BLOOM_DOWNSAMPLE_INPUT, linearSampler);
    ctx->bindResourceView(BLOOM_DOWNSAMPLE_OUTPUT, outputBuffer.view, nullptr);
    ctx->bindShader(VK_SHADER_STAGE_COMPUTE_BIT, BloomDownsampleShader::getShader());
    ctx->dispatch(workgroups.width, workgroups.height, workgroups.depth);
  }

  void DxvkBloom::dispatchUpsampleStep(
    Rc<DxvkContext> ctx,
    const Rc<DxvkSampler>& linearSampler,
    const Resources::Resource& inputBuffer,
    const Resources::Resource& outputBuffer) {
    ScopedGpuProfileZone(ctx, "Bloom Upsample");

    VkExtent3D inputSize = inputBuffer.image->info().extent;
    VkExtent3D outputSize = outputBuffer.image->info().extent;

    // Prepare shader arguments
    BloomUpsampleArgs pushArgs = {};
    pushArgs.inputSizeInverse = { 1.f / float(inputSize.width), 1.f / float(inputSize.height) };
    pushArgs.upsampledOutputSize = { outputSize.width, outputSize.height };
    pushArgs.upsampledOutputSizeInverse = { 1.f / float(outputSize.width), 1.f / float(outputSize.height) };

    ctx->pushConstants(0, sizeof(pushArgs), &pushArgs);

    VkExtent3D workgroups = util::computeBlockCount(outputSize, VkExtent3D{ 16, 16, 1 });

    ctx->bindResourceView(BLOOM_UPSAMPLE_INPUT, inputBuffer.view, nullptr);
    ctx->bindResourceSampler(BLOOM_UPSAMPLE_INPUT, linearSampler);
    ctx->bindResourceView(BLOOM_UPSAMPLE_OUTPUT, outputBuffer.view, nullptr);
    ctx->bindShader(VK_SHADER_STAGE_COMPUTE_BIT, BloomUpsampleShader::getShader());
    ctx->dispatch(workgroups.width, workgroups.height, workgroups.depth);
  }

  void DxvkBloom::dispatchComposite(
    Rc<DxvkContext> ctx,
    const Rc<DxvkSampler> &linearSampler,
    const Resources::Resource& inOutColorBuffer,
    const Resources::Resource& bloomBuffer)
  {
    ScopedGpuProfileZone(ctx, "Composite");

    VkExtent3D outputSize = inOutColorBuffer.image->info().extent;

    // Prepare shader arguments
    BloomCompositeArgs pushArgs = {};
    pushArgs.imageSize = { outputSize.width, outputSize.height };
    pushArgs.imageSizeInverse = { 1.f / float(outputSize.width), 1.f / float(outputSize.height) };
    pushArgs.intensity = 0.01f * std::max(burnIntensity(), 0.0f);
    ctx->pushConstants(0, sizeof(pushArgs), &pushArgs);

    VkExtent3D workgroups = util::computeBlockCount(outputSize, VkExtent3D{ 16 , 16, 1 });

    ctx->bindResourceView(BLOOM_COMPOSITE_COLOR_INPUT_OUTPUT, inOutColorBuffer.view, nullptr);
    ctx->bindResourceView(BLOOM_COMPOSITE_BLOOM, bloomBuffer.view, nullptr);
    ctx->bindResourceSampler(BLOOM_COMPOSITE_BLOOM, linearSampler);
    ctx->bindShader(VK_SHADER_STAGE_COMPUTE_BIT, CompositeShader::getShader());
    ctx->dispatch(workgroups.width, workgroups.height, workgroups.depth);
  }

  void DxvkBloom::createTargetResource(Rc<DxvkContext>& ctx, const VkExtent3D& targetExtent) {
    for (uint32_t i = 0; i < std::size(m_bloomBuffer); i++) {
      const uint32_t divisor = (1U << (i + 1));

      m_bloomBuffer[i] = Resources::createImageResource(
        ctx,
        "bloom buffer",
        {
          util::ceilDivide(targetExtent.width, divisor),
          util::ceilDivide(targetExtent.height, divisor),
          1
        },
        VK_FORMAT_R16G16B16A16_SFLOAT);
    }
  }

  void DxvkBloom::releaseTargetResource() {
    for (auto& i : m_bloomBuffer) {
      i.reset();
    }
  }

  bool DxvkBloom::isEnabled() const {
    return enable() && burnIntensity() > 0.f;
  }
}
