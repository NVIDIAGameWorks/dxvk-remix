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
#include "rtx_context.h"
#include "rtx_postFx.h"
#include "dxvk_device.h"
#include "dxvk_scoped_annotation.h"
#include "rtx_render/rtx_shader_manager.h"
#include "rtx/pass/post_fx/post_fx.h"

#include <rtx_shaders/post_fx.h>
#include <rtx_shaders/post_fx_highlight.h>
#include <rtx_shaders/post_fx_motion_blur.h>
#include <rtx_shaders/post_fx_motion_blur_prefilter.h>
#include <pxr/base/arch/math.h>
#include "rtx_imgui.h"

#include "../util/util_globaltime.h"

namespace dxvk {
  std::array<uint8_t, 3> g_customHighlightColor = { 118, 185, 0 };

  // Defined within an unnamed namespace to ensure unique definition across binary
  namespace {
    class PostFxShader : public ManagedShader
    {
      SHADER_SOURCE(PostFxShader, VK_SHADER_STAGE_COMPUTE_BIT, post_fx)

      PUSH_CONSTANTS(PostFxArgs)

      BEGIN_PARAMETER()
        SAMPLER2D(POST_FX_INPUT)
        RW_TEXTURE2D(POST_FX_OUTPUT)
      END_PARAMETER()
    };

    PREWARM_SHADER_PIPELINE(PostFxShader);

    class PostFxMotionBlurShader : public ManagedShader
    {
      SHADER_SOURCE(PostFxMotionBlurShader, VK_SHADER_STAGE_COMPUTE_BIT, post_fx_motion_blur)

      PUSH_CONSTANTS(PostFxArgs)

      BEGIN_PARAMETER()
        TEXTURE2D(POST_FX_MOTION_BLUR_PRIMARY_SCREEN_SPACE_MOTION_INPUT)
        TEXTURE2D(POST_FX_MOTION_BLUR_PRIMARY_SURFACE_FLAGS_INPUT)
        TEXTURE2D(POST_FX_MOTION_BLUR_PRIMARY_LINEAR_VIEW_Z_INPUT)
        TEXTURE2DARRAY(POST_FX_MOTION_BLUR_BLUE_NOISE_TEXTURE_INPUT)
        TEXTURE2D(POST_FX_MOTION_BLUR_INPUT)
        SAMPLER(POST_FX_MOTION_BLUR_NEAREST_SAMPLER)
        SAMPLER(POST_FX_MOTION_BLUR_LINEAR_SAMPLER)
        RW_TEXTURE2D(POST_FX_MOTION_BLUR_OUTPUT)
      END_PARAMETER()
    };

    PREWARM_SHADER_PIPELINE(PostFxMotionBlurShader);

    class PostFxMotionBlurPrefilterShader : public ManagedShader
    {
      SHADER_SOURCE(PostFxMotionBlurPrefilterShader, VK_SHADER_STAGE_COMPUTE_BIT, post_fx_motion_blur_prefilter)

      PUSH_CONSTANTS(PostFxMotionBlurPrefilterArgs)

      BEGIN_PARAMETER()
        TEXTURE2D(POST_FX_MOTION_BLUR_PREFILTER_PRIMARY_SURFACE_FLAGS_INPUT)
        RW_TEXTURE2D(POST_FX_MOTION_BLUR_PREFILTER_PRIMARY_SURFACE_FLAGS_FILTERED_OUTPUT)
      END_PARAMETER()
    };

    PREWARM_SHADER_PIPELINE(PostFxMotionBlurPrefilterShader);

    class PostFxHighlightShader : public ManagedShader {
      SHADER_SOURCE(PostFxHighlightShader, VK_SHADER_STAGE_COMPUTE_BIT, post_fx_highlight)

        PUSH_CONSTANTS(PostFxHighlightingArgs)

        BEGIN_PARAMETER()
        TEXTURE2D(POST_FX_HIGHLIGHT_INPUT)
        RW_TEXTURE2D(POST_FX_HIGHLIGHT_OBJECT_PICKING_INPUT)
        TEXTURE2D(POST_FX_HIGHLIGHT_PRIMARY_CONE_RADIUS_INPUT)
        RW_TEXTURE2D(POST_FX_HIGHLIGHT_OUTPUT)
        STRUCTURED_BUFFER(POST_FX_HIGHLIGHT_VALUES)
        END_PARAMETER()
    };

    PREWARM_SHADER_PIPELINE(PostFxHighlightShader);
  }

  DxvkPostFx::DxvkPostFx(DxvkDevice* device)
  : m_vkd(device->vkd())
  {
  }

  DxvkPostFx::~DxvkPostFx()
  {
  }

  void DxvkPostFx::showImguiSettings()
  {
    RemixGui::Checkbox("Post Effect Enabled", &enableObject());
    if (enable())
    {
      RemixGui::Checkbox("Motion Blur Enabled", &enableMotionBlurObject());
      if (enableMotionBlur()) {
        RemixGui::Checkbox("Motion Blur Noise Sample Enabled", &enableMotionBlurNoiseSampleObject());
        RemixGui::Checkbox("Motion Blur Emissive Surface Enabled", &enableMotionBlurEmissiveObject());
        RemixGui::DragInt("Motion Blur Sample Count", &motionBlurSampleCountObject(), 0.1f, 1, 10, "%d", ImGuiSliderFlags_AlwaysClamp);
        RemixGui::DragFloat("Exposure Fraction", &exposureFractionObject(), 0.01f, 0.01f, 3.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
        RemixGui::DragFloat("Blur Diameter Fraction", &blurDiameterFractionObject(), 0.001f, 0.001f, 1.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
        RemixGui::DragFloat("Motion Blur Minimum Velocity Threshold (unit: pixel)", &motionBlurMinimumVelocityThresholdInPixelObject(), 0.01f, 0.01f, 3.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
        RemixGui::DragFloat("Motion Blur Dynamic Deduction", &motionBlurDynamicDeductionObject(), 0.001f, 0.0f, 1.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
        RemixGui::DragFloat("Motion Blur Jitter Strength", &motionBlurJitterStrengthObject(), 0.001f, 0.0f, 1.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
      }

      RemixGui::Checkbox("Chromatic Aberration Enabled", &enableChromaticAberrationObject());
      if (enableChromaticAberration()) {
        RemixGui::DragFloat("Fringe Intensity", &chromaticAberrationAmountObject(), 0.01f, 0.0f, 5.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
        RemixGui::DragFloat("Fringe Center Attenuation Amount", &chromaticCenterAttenuationAmountObject(), 0.001f, 0.0f, 1.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
      }

      RemixGui::Checkbox("Vignette Enabled", &enableVignetteObject());
      if (enableVignette()) {
        RemixGui::DragFloat("Vignette Intensity", &vignetteIntensityObject(), 0.01f, 0.0f, 5.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
        RemixGui::DragFloat("Vignette Radius", &vignetteRadiusObject(), 0.001f, 0.0f, 1.4f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
        RemixGui::DragFloat("Vignette Softness", &vignetteSoftnessObject(), 0.001f, 0.0f, 1.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
      }
    }
  }

  void dispatchMotionBlurPrefilterPass(
    Rc<RtxContext> ctx,
    const Resources::Resource& primarySurfaceFlags,
    const Resources::Resource& primarySurfaceFlagsFilteredOutput,
    const bool isVertical)
  {
    ScopedGpuProfileZone(ctx, "PostFx Motion Blur Prefilter");

    const VkExtent3D& inputSize = primarySurfaceFlags.image->info().extent;
    const VkExtent3D workgroups = util::computeBlockCount(inputSize, VkExtent3D { POST_FX_TILE_SIZE , POST_FX_TILE_SIZE, 1 });

    PostFxMotionBlurPrefilterArgs postFxMotionBlurPrefilterArgs = {};
    postFxMotionBlurPrefilterArgs.imageSize = { (uint) inputSize.width, (uint) inputSize.height };
    if (isVertical)
    {
      postFxMotionBlurPrefilterArgs.pixelStep = { 0, 1 };
    }
    else
    {
      postFxMotionBlurPrefilterArgs.pixelStep = { 1, 0 };
    }

    ctx->pushConstants(0, sizeof(PostFxMotionBlurPrefilterArgs), &postFxMotionBlurPrefilterArgs);

    ctx->bindResourceView(POST_FX_MOTION_BLUR_PREFILTER_PRIMARY_SURFACE_FLAGS_INPUT, primarySurfaceFlags.view, nullptr);
    ctx->bindResourceView(POST_FX_MOTION_BLUR_PREFILTER_PRIMARY_SURFACE_FLAGS_FILTERED_OUTPUT, primarySurfaceFlagsFilteredOutput.view, nullptr);

    ctx->bindShader(VK_SHADER_STAGE_COMPUTE_BIT, PostFxMotionBlurPrefilterShader::getShader());

    ctx->dispatch(workgroups.width, workgroups.height, workgroups.depth);
  }

  void dispatchMotionBlur(
    Rc<RtxContext> ctx,
    Rc<DxvkSampler> nearestSampler,
    Rc<DxvkSampler> linearSampler,
    const PostFxArgs& postFxArgs,
    const VkExtent3D& workgroups,
    const Resources::RaytracingOutput& rtOutput,
    const Resources::Resource& motionBlurInputTexture,
    const Resources::Resource& motionBlurOutputTexture)
  {
    ScopedGpuProfileZone(ctx, "PostFx Motion Blur");

    dispatchMotionBlurPrefilterPass(ctx,
                                    rtOutput.m_primarySurfaceFlags,
                                    rtOutput.m_primarySurfaceFlagsIntermediateTexture1.resource(Resources::AccessType::Write),
                                    false);

    dispatchMotionBlurPrefilterPass(ctx,
                                    rtOutput.m_primarySurfaceFlagsIntermediateTexture1.resource(Resources::AccessType::Read),
                                    rtOutput.m_primarySurfaceFlagsIntermediateTexture2.resource(Resources::AccessType::Write),
                                    true);

    ctx->pushConstants(0, sizeof(postFxArgs), &postFxArgs);

    ctx->bindResourceView(POST_FX_MOTION_BLUR_PRIMARY_SCREEN_SPACE_MOTION_INPUT, rtOutput.m_primaryScreenSpaceMotionVector.view, nullptr);
    ctx->bindResourceView(POST_FX_MOTION_BLUR_PRIMARY_SURFACE_FLAGS_INPUT, rtOutput.m_primarySurfaceFlagsIntermediateTexture2.view(Resources::AccessType::Read), nullptr);
    ctx->bindResourceView(POST_FX_MOTION_BLUR_PRIMARY_LINEAR_VIEW_Z_INPUT, rtOutput.m_primaryLinearViewZ.view, nullptr);
    ctx->bindResourceView(POST_FX_MOTION_BLUR_BLUE_NOISE_TEXTURE_INPUT, ctx->getResourceManager().getBlueNoiseTexture(ctx), nullptr);
    ctx->bindResourceView(POST_FX_MOTION_BLUR_INPUT, motionBlurInputTexture.view, nullptr);
    ctx->bindResourceView(POST_FX_MOTION_BLUR_OUTPUT, motionBlurOutputTexture.view, nullptr);
    ctx->bindResourceSampler(POST_FX_MOTION_BLUR_NEAREST_SAMPLER, nearestSampler);
    ctx->bindResourceSampler(POST_FX_MOTION_BLUR_LINEAR_SAMPLER, linearSampler);

    ctx->bindShader(VK_SHADER_STAGE_COMPUTE_BIT, PostFxMotionBlurShader::getShader());

    ctx->dispatch(workgroups.width, workgroups.height, workgroups.depth);
  }

  void dispatchPostLensEffects(
    Rc<DxvkContext> ctx,
    Rc<DxvkSampler> linearSampler,
    const PostFxArgs& postFxArgs,
    const VkExtent3D& workgroups,
    const Resources::Resource& postFxLensEffectInput,
    const Resources::Resource& postFxLensEffectOutput)
  {
    ScopedGpuProfileZone(ctx, "PostFx Lens Effect");

    ctx->pushConstants(0, sizeof(postFxArgs), &postFxArgs);

    ctx->bindResourceView(POST_FX_INPUT, postFxLensEffectInput.view, nullptr);
    ctx->bindResourceSampler(POST_FX_INPUT, linearSampler);
    ctx->bindResourceView(POST_FX_OUTPUT, postFxLensEffectOutput.view, nullptr);

    ctx->bindShader(VK_SHADER_STAGE_COMPUTE_BIT, PostFxShader::getShader());

    ctx->dispatch(workgroups.width, workgroups.height, workgroups.depth);
  }

  void DxvkPostFx::dispatch(
    Rc<RtxContext> ctx,
    Rc<DxvkSampler> nearestSampler,
    Rc<DxvkSampler> linearSampler,
    const uvec2& mainCameraResolution,
    const uint32_t frameIdx,
    const Resources::RaytracingOutput& rtOutput,
    const bool cameraCutDetected)
  {
    if (!enable()) {
      return;
    }

    ScopedGpuProfileZone(ctx, "PostFx");
    ctx->setFramePassStage(RtxFramePassStage::PostFX);

    // Simulate chromatic aberration offset scale by calculating the focal length differences of 3 Fraunhofer lines,
    // the wavelength of these lines are used for measuring chromatic aberrations
    // https://www.rp-photonics.com/chromatic_aberrations.html
    auto calculateChromaticAberrationScale = [](const float chromaticAberrationAmount) {
      constexpr float lambdaC = 656.3f; // [nm] blue Fraunhofer F line from hydrogen
      constexpr float lambdaD = 589.2f; // [nm] orange Fraunhofer D line from sodium, in the region of maximum sensitivity of the human eye
      constexpr float lambdaF = 486.1f; // [nm] red Fraunhofer C line from hydrogen

      // https://www.rp-photonics.com/abbe_number.html
      constexpr float abbeNumber = 40.0f; // Use typical glass abbe number
      constexpr float focalD = 0.05f; // Use typical camera lens focal to represent focal of D line
      constexpr float fcFocalDiff = focalD / abbeNumber * 0.5f;

      const float2 scale = float2(fcFocalDiff * (lambdaC - lambdaD), fcFocalDiff * (lambdaD - lambdaF));

      const float chromaticAberrationIntensity = chromaticAberrationAmount;

      return float2(scale.x * chromaticAberrationIntensity, scale.y * chromaticAberrationIntensity);
    };

    const Resources::Resource& inOutColorTexture = rtOutput.m_finalOutput.resource(Resources::AccessType::ReadWrite);
    const VkExtent3D& inputSize = inOutColorTexture.image->info().extent;
    const VkExtent3D workgroups = util::computeBlockCount(inputSize, VkExtent3D { POST_FX_TILE_SIZE , POST_FX_TILE_SIZE, 1 } );

    PostFxArgs postFxArgs = {};
    postFxArgs.imageSize = { (uint)inputSize.width, (uint)inputSize.height };
    postFxArgs.invImageSize = { 1.0f / (float) inputSize.width, 1.0f / (float) inputSize.height };
    postFxArgs.invMainCameraResolution = float2(1.0f / (float)mainCameraResolution.x, 1.0f / (float)mainCameraResolution.y);
    postFxArgs.inputOverOutputViewSize = float2((float)mainCameraResolution.x * postFxArgs.invImageSize.x, (float)mainCameraResolution.y * postFxArgs.invImageSize.y);
    postFxArgs.frameIdx = frameIdx;
    postFxArgs.enableMotionBlurNoiseSample = enableMotionBlurNoiseSample();
    postFxArgs.enableMotionBlurEmissive = enableMotionBlurEmissive();
    postFxArgs.motionBlurSampleCount = motionBlurSampleCount();
    postFxArgs.exposureFraction = exposureFraction();
    postFxArgs.blurDiameterFraction = blurDiameterFraction();
    postFxArgs.motionBlurMinimumVelocityThresholdInPixel = motionBlurMinimumVelocityThresholdInPixel();
    postFxArgs.motionBlurDynamicDeduction = motionBlurDynamicDeduction();
    postFxArgs.jitterStrength = motionBlurJitterStrength();
    postFxArgs.motionBlurDlfgDeduction = ctx->isDLFGEnabled() ?
      1.0f / static_cast<float>(ctx->dlfgInterpolatedFrameCount() + 1) : 1.0f;
    postFxArgs.chromaticCenterAttenuationAmount = chromaticCenterAttenuationAmount();
    postFxArgs.chromaticAberrationScale = calculateChromaticAberrationScale(isChromaticAberrationEnabled() ? chromaticAberrationAmount() : 0.0f);
    postFxArgs.vignetteIntensity = isVignetteEnabled() ? vignetteIntensity() : 0.0f;
    postFxArgs.vignetteRadius = vignetteRadius();
    postFxArgs.vignetteSoftness = vignetteSoftness();

    ctx->setPushConstantBank(DxvkPushConstantBank::RTX);

    const Resources::Resource* lastPointer = &inOutColorTexture;

    const bool motionBlurEnabled = !cameraCutDetected && isMotionBlurEnabled();
    if (motionBlurEnabled)
    {
      assert(motionBlurSampleCount() <= 10);
      lastPointer = &rtOutput.m_postFxIntermediateTexture;
      dispatchMotionBlur(
        ctx,
        nearestSampler, linearSampler,
        postFxArgs,
        workgroups,
        rtOutput,
        inOutColorTexture, *lastPointer);
    }

    if (isChromaticAberrationEnabled() || isVignetteEnabled())
    {
      dispatchPostLensEffects(ctx, linearSampler, postFxArgs, workgroups, *lastPointer, inOutColorTexture);

      lastPointer = &inOutColorTexture;
    }

    if (lastPointer->image != inOutColorTexture.image)
    {
      // Copy to the output texture if the final output is not the input texture
      ctx->copyImage(
        inOutColorTexture.image,
        { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
        { 0, 0, 0 },
        rtOutput.m_postFxIntermediateTexture.image,
        { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
        { 0, 0, 0 },
        inputSize);
    }
  }

  namespace {
    uint32_t bitCeilPow2(uint32_t v) {
      if (v == 0) {
        return 0;
      }

      // most significant bit
      unsigned long msb = 0;
      static_assert(sizeof(unsigned long) == sizeof(uint32_t));
      if (_BitScanReverse(&msb, v) == 0) {
        assert(0);
        return 0;
      }

      // if pow of 2, return itself
      if ((v & (v - 1)) == 0) {
        return msb;
      }
      return msb + 1;
    }

    uint32_t packColor(uint8_t r, uint8_t g, uint8_t  b) {
      return (r << 0) | (g << 8) | (b << 16);
    }
  }

  void DxvkPostFx::dispatchHighlighting(
    Rc<RtxContext> ctx,
    const Resources::RaytracingOutput& rtOutput,
    std::vector<uint32_t>&& objectPickingValuesToHighlight,
    const std::optional<Vector2i>& pixelToHighlight,
    HighlightColor color) {
    static_assert(sizeof(ObjectPickingValue) == sizeof(objectPickingValuesToHighlight[0]));
    if (!rtOutput.m_primaryObjectPicking.isValid()) {
      return;
    }
    if (objectPickingValuesToHighlight.empty() && !pixelToHighlight) {
      return;
    }
    ScopedGpuProfileZone(ctx, "PostFx Highlight");

    const Resources::Resource& inOutColorTexture = rtOutput.m_compositeOutput.resource(Resources::AccessType::ReadWrite);
    const VkExtent3D& inputSize = inOutColorTexture.image->info().extent;

    const auto workgroups = util::computeBlockCount(inputSize, VkExtent3D { POST_FX_TILE_SIZE , POST_FX_TILE_SIZE, 1 });

    uint32_t valuesToHighlightCountPow;
    {
      // deduplicate and sort to perform binary search in the shader
      std::vector<uint32_t>& sorted = objectPickingValuesToHighlight;
      {
        if (sorted.size() > POST_FX_HIGHLIGHTING_MAX_VALUES) {
          sorted.resize(POST_FX_HIGHLIGHTING_MAX_VALUES);
          ONCE(Logger::warn("Too many values to highlight, some objects will be omitted."));
        }
        auto newEnd = std::unique(sorted.begin(), sorted.end());
        sorted.erase(newEnd, sorted.end());
        std::sort(sorted.begin(), sorted.end());
      }

      valuesToHighlightCountPow = bitCeilPow2(static_cast<uint32_t>(sorted.size()));

      // fill invalid values as POST_FX_HIGHLIGHTING_INVALID_VALUE
      {
        const size_t validCount = sorted.size();
        sorted.resize(1 << valuesToHighlightCountPow);
        for (size_t i = validCount; i < sorted.size(); i++) {
          sorted[i] = POST_FX_HIGHLIGHTING_INVALID_VALUE;
        }
      }

      if (m_highlightingValues == nullptr) {
        auto info = DxvkBufferCreateInfo {};
        {
          info.usage = VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
          info.stages = VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
          info.access = VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;
          info.size = align(POST_FX_HIGHLIGHTING_MAX_VALUES * sizeof(ObjectPickingValue), kBufferAlignment);
        }
        m_highlightingValues = ctx->getDevice()->createBuffer(info, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, DxvkMemoryStats::Category::RTXBuffer, "Highlight Buffer");
      }

      if (!sorted.empty()) {
        ctx->writeToBuffer(m_highlightingValues, 0, sorted.size() * sizeof(ObjectPickingValue), sorted.data());
      }
    }

    auto args = PostFxHighlightingArgs {};
    {
      args.imageSize = { inputSize.width, inputSize.height };
      args.desaturateNonHighlighted = desaturateOthersOnHighlight() ? 1 : 0;
      args.timeSinceStartMS = (float)GlobalTime::get().absoluteTimeMs();
      args.pixel = pixelToHighlight ? int2 { pixelToHighlight->x, pixelToHighlight->y } : int2 { -1, -1 };
      args.highlightColorPacked =
        color == HighlightColor::World ? packColor(118, 185, 0) :
        color == HighlightColor::UI ? packColor(66, 150, 250) :
        color == HighlightColor::FromVariable ? packColor(g_customHighlightColor[0], g_customHighlightColor[1], g_customHighlightColor[2]) :
        packColor(255, 255, 255);
      args.valuesToHighlightCountPow = valuesToHighlightCountPow;
    }

    ctx->pushConstants(0, sizeof(args), &args);

    const Resources::Resource* lastOutput = &rtOutput.m_postFxIntermediateTexture;

    ctx->bindResourceView(POST_FX_HIGHLIGHT_INPUT, inOutColorTexture.view, nullptr);
    ctx->bindResourceView(POST_FX_HIGHLIGHT_OBJECT_PICKING_INPUT, rtOutput.m_primaryObjectPicking.view, nullptr);
    ctx->bindResourceView(POST_FX_HIGHLIGHT_PRIMARY_CONE_RADIUS_INPUT, rtOutput.m_primaryConeRadius.view, nullptr);
    ctx->bindResourceView(POST_FX_HIGHLIGHT_OUTPUT, lastOutput->view, nullptr);
    ctx->bindResourceBuffer(POST_FX_HIGHLIGHT_VALUES, DxvkBufferSlice(m_highlightingValues, 0, m_highlightingValues->info().size));

    ctx->bindShader(VK_SHADER_STAGE_COMPUTE_BIT, PostFxHighlightShader::getShader());
    ctx->dispatch(workgroups.width, workgroups.height, workgroups.depth);

    // Copy to the output texture if the final output is not the input texture
    if (lastOutput->image != inOutColorTexture.image) {
      ctx->copyImage(
        inOutColorTexture.image,
        { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
        { 0, 0, 0 },
        rtOutput.m_postFxIntermediateTexture.image,
        { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
        { 0, 0, 0 },
        inputSize);
    }
  }
}
