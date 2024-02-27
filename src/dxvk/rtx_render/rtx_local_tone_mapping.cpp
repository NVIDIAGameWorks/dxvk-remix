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
#include "rtx_local_tone_mapping.h"
#include "dxvk_device.h"
#include "dxvk_scoped_annotation.h"
#include "rtx_render/rtx_shader_manager.h"
#include "rtx.h"
#include "rtx/pass/tonemap/tonemapping.h"
#include "rtx/pass/local_tonemap/local_tonemapping.h"

#include <rtx_shaders/luminance.h>
#include <rtx_shaders/exposure_weight.h>
#include <rtx_shaders/generate_mipmap.h>
#include <rtx_shaders/blend.h>
#include <rtx_shaders/blend_laplacian.h>
#include <rtx_shaders/final_combine.h>
#include "rtx_imgui.h"
#include "rtx/utility/debug_view_indices.h"

namespace dxvk {
  // Defined within an unnamed namespace to ensure unique definition across binary
  namespace {

    class LuminanceShader : public ManagedShader {
      SHADER_SOURCE(LuminanceShader, VK_SHADER_STAGE_COMPUTE_BIT, luminance)

      PUSH_CONSTANTS(LuminanceArgs)

      BEGIN_PARAMETER()
        TEXTURE2D(LUMINANCE_ORIGINAL)
        RW_TEXTURE2D(LUMINANCE_OUTPUT)
        RW_TEXTURE2D(LUMINANCE_DEBUG_VIEW_OUTPUT)
        RW_TEXTURE1D(LUMINANCE_EXPOSURE)
      END_PARAMETER()
    };
    PREWARM_SHADER_PIPELINE(LuminanceShader);

    class ExposureWeightShader : public ManagedShader {
      SHADER_SOURCE(ExposureWeightShader, VK_SHADER_STAGE_COMPUTE_BIT, exposure_weight)

      PUSH_CONSTANTS(ExposureWeightArgs)

      BEGIN_PARAMETER()
        TEXTURE2D(EXPOSURE_WEIGHT_INPUT)
        RW_TEXTURE2D(EXPOSURE_WEIGHT_OUTPUT)
        RW_TEXTURE2D(EXPOSURE_DEBUG_VIEW_OUTPUT)
      END_PARAMETER()
    };
    PREWARM_SHADER_PIPELINE(ExposureWeightShader);

    class GenerateMipmapShader : public ManagedShader {
      SHADER_SOURCE(GenerateMipmapShader, VK_SHADER_STAGE_COMPUTE_BIT, generate_mipmap)

      PUSH_CONSTANTS(GenerateMipmapArgs)

      BEGIN_PARAMETER()
        SAMPLER2D(GENERATE_MIPMAP_INPUT)
        RW_TEXTURE2D(GENERATE_MIPMAP_OUTPUT)
      END_PARAMETER()
    };
    PREWARM_SHADER_PIPELINE(GenerateMipmapShader);

    class BlendShader : public ManagedShader {
      SHADER_SOURCE(BlendShader, VK_SHADER_STAGE_COMPUTE_BIT, blend)

      PUSH_CONSTANTS(BlendArgs)

      BEGIN_PARAMETER()
        TEXTURE2D(BLEND_EXPOSURE_INPUT)
        TEXTURE2D(BLEND_WEIGHT_INPUT)
        RW_TEXTURE2D(BLEND_OUTPUT)
        RW_TEXTURE2D(BLEND_DEBUG_VIEW_OUTPUT)
      END_PARAMETER()
    };
    PREWARM_SHADER_PIPELINE(BlendShader);

    class BlendLaplacianShader : public ManagedShader {
      SHADER_SOURCE(BlendLaplacianShader, VK_SHADER_STAGE_COMPUTE_BIT, blend_laplacian)

      PUSH_CONSTANTS(BlendLaplacianArgs)

      BEGIN_PARAMETER()
        SAMPLER2D(BLEND_LAPLACIAN_EXPOSURE_INPUT)
        SAMPLER2D(BLEND_LAPLACIAN_EXPOSURE_COARSER_INPUT)
        SAMPLER2D(BLEND_LAPLACIAN_WEIGHT_INPUT)
        SAMPLER2D(BLEND_LAPLACIAN_ACCUMULATE_INPUT)
        RW_TEXTURE2D(BLEND_LAPLACIAN_OUTPUT)
        RW_TEXTURE2D(BLEND_LAPLACIAN_DEBUG_VIEW_OUTPUT)
      END_PARAMETER()
    };
    PREWARM_SHADER_PIPELINE(BlendLaplacianShader);

    class FinalCombineShader : public ManagedShader {
      SHADER_SOURCE(FinalCombineShader, VK_SHADER_STAGE_COMPUTE_BIT, final_combine)

      PUSH_CONSTANTS(FinalCombineArgs)

      BEGIN_PARAMETER()
        TEXTURE2DARRAY(FINAL_COMBINE_BLUE_NOISE_TEXTURE_INPUT)
        SAMPLER2D(FINAL_COMBINE_MIP_ASSEMBLE)
        SAMPLER2D(FINAL_COMBINE_ORIGINAL_MIP)
        TEXTURE2D(FINAL_COMBINE_ORIGINAL_MIP0)
        TEXTURE2D(FINAL_COMBINE_WEIGHT_MIP0)
        RW_TEXTURE2D(FINAL_COMBINE_OUTPUT)
        RW_TEXTURE2D(FINAL_COMBINE_DEBUG_VIEW_OUTPUT)
        RW_TEXTURE1D(FINAL_COMBINE_EXPOSURE)
      END_PARAMETER()
    };
    PREWARM_SHADER_PIPELINE(FinalCombineShader);
  }
  
  DxvkLocalToneMapping::DxvkLocalToneMapping(DxvkDevice* device)
  : RtxPass(device)  {
  }
  
  DxvkLocalToneMapping::~DxvkLocalToneMapping()  {  }

  void DxvkLocalToneMapping::showImguiSettings() {
    ImGui::DragInt("Mip", &mipObject(), 0.06f, 0, 16);
    ImGui::DragInt("Display Mip", &displayMipObject(), 0.06f, 0, 16);
    ImGui::Checkbox("Boost Local Contrast", &boostLocalContrastObject());
    ImGui::Checkbox("Use Gaussian Kernel", &useGaussianObject());
    ImGui::Checkbox("Finalize With ACES", &finalizeWithACESObject());
    ImGui::DragFloat("Exposure Level", &exposureObject(), 0.01f, 0.f, 1000.f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
    ImGui::DragFloat("Shadow Level", &shadowsObject(), 0.01f, -10.f, 10.f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
    ImGui::DragFloat("Highlight Level", &highlightsObject(), 0.01f, -10.f, 10.f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
    ImGui::DragFloat("Exposure Preference Sigma", &exposurePreferenceSigmaObject(), 0.01f, 0.f, 100.f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
    ImGui::DragFloat("Exposure Preference Offset", &exposurePreferenceOffsetObject(), 0.001f, -1.f, 1.f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
    ImGui::Combo("Dither Mode", &ditherModeObject(), "Disabled\0Spatial\0Spatial + Temporal\0");
  }

   void DxvkLocalToneMapping::dispatch(
     Rc<RtxContext> ctx,
     Rc<DxvkSampler> linearSampler,
     Rc<DxvkImageView> exposureView,
     const Resources::RaytracingOutput& rtOutput,
     const float deltaTime,
     bool performSRGBConversion,
     bool resetHistory,
     bool enableAutoExposure) {

    if (m_mips.view.size() == 0) {
      return;
    }

    VkExtent3D finalResolution = rtOutput.m_finalOutput.view->imageInfo().extent;
    VkExtent3D downscaledResolution = rtOutput.m_compositeOutputExtent;

    ScopedGpuProfileZone(ctx, "Local Tone Mapping");
    const VkExtent3D workgroups = util::computeBlockCount(finalResolution, VkExtent3D { 16, 16, 1 });
    int totalMipLevels = (int)m_mipsAssemble.view.size();
    int mipLevel = std::min(mip(), totalMipLevels - 1);
    int displayMipLevel = std::min(displayMip(), totalMipLevels - 1);

    std::vector<uvec2> resolutionList;
    for (uint32_t w = finalResolution.width, h = finalResolution.height; w >= 1 && h >= 1; w /= 2, h /= 2) {
      resolutionList.emplace_back(uvec2 { w, h });
    }

    DebugView& debugView = ctx->getDevice()->getCommon()->metaDebugView();

    {
      ScopedGpuProfileZone(ctx, "Luminance");
      LuminanceArgs pushArgs = {};
      pushArgs.exposure = exposure();
      pushArgs.shadows = pow(2.f, shadows());
      pushArgs.highlights = pow(2.f, -highlights());
      pushArgs.debugView = debugView.debugViewIdx();
      pushArgs.enableAutoExposure = enableAutoExposure;
      ctx->pushConstants(0, sizeof(pushArgs), &pushArgs);
      ctx->bindResourceView(LUMINANCE_ORIGINAL, rtOutput.m_finalOutput.view, nullptr);
      ctx->bindResourceView(LUMINANCE_OUTPUT, m_mips.view[0], nullptr);
      ctx->bindResourceView(LUMINANCE_DEBUG_VIEW_OUTPUT, debugView.getDebugOutput(), nullptr);
      ctx->bindResourceView(LUMINANCE_EXPOSURE, exposureView, nullptr);
      ctx->bindShader(VK_SHADER_STAGE_COMPUTE_BIT, LuminanceShader::getShader());
      ctx->dispatch(workgroups.width, workgroups.height, workgroups.depth);
    }

    {
      ScopedGpuProfileZone(ctx, "Weight");
      ExposureWeightArgs pushArgs = {};
      pushArgs.sigmaSq = exposurePreferenceSigma() * exposurePreferenceSigma();
      pushArgs.offset = exposurePreferenceOffset();
      pushArgs.debugView = debugView.debugViewIdx();
      ctx->pushConstants(0, sizeof(pushArgs), &pushArgs);
      ctx->bindResourceView(EXPOSURE_WEIGHT_INPUT, m_mips.view[0], nullptr);
      ctx->bindResourceView(EXPOSURE_WEIGHT_OUTPUT, m_mipsWeights.view[0], nullptr);
      ctx->bindResourceView(EXPOSURE_DEBUG_VIEW_OUTPUT, debugView.getDebugOutput(), nullptr);
      ctx->bindShader(VK_SHADER_STAGE_COMPUTE_BIT, ExposureWeightShader::getShader());
      ctx->dispatch(workgroups.width, workgroups.height, workgroups.depth);
    }

    {
      ScopedGpuProfileZone(ctx, "Luminance Mip Map");
      
      for (int i = 1; i <= mipLevel; i++) {
        uvec2 mipResolution = resolutionList[i];
        GenerateMipmapArgs pushArgs;
        pushArgs.resolution = mipResolution;
        pushArgs.useGaussian = useGaussian();
        ctx->pushConstants(0, sizeof(pushArgs), &pushArgs);
        ctx->bindResourceView(GENERATE_MIPMAP_INPUT, m_mips.view[i - 1], nullptr);
        ctx->bindResourceView(GENERATE_MIPMAP_OUTPUT, m_mips.view[i], nullptr);
        ctx->bindResourceSampler(GENERATE_MIPMAP_INPUT, linearSampler);
        ctx->bindShader(VK_SHADER_STAGE_COMPUTE_BIT, GenerateMipmapShader::getShader());
        const VkExtent3D mipWorkgroups = util::computeBlockCount(VkExtent3D { mipResolution.x, mipResolution.y, 1 }, VkExtent3D { 16, 16, 1 });
        ctx->dispatch(mipWorkgroups.width, mipWorkgroups.height, mipWorkgroups.depth);
      }
    }

    {
      ScopedGpuProfileZone(ctx, "Weight Mip Map");
      
      for (int i = 1; i <= mipLevel; i++) {
        uvec2 mipResolution = resolutionList[i];
        GenerateMipmapArgs pushArgs;
        pushArgs.resolution = mipResolution;
        pushArgs.useGaussian = useGaussian();
        ctx->pushConstants(0, sizeof(pushArgs), &pushArgs);
        ctx->bindResourceView(GENERATE_MIPMAP_INPUT, m_mipsWeights.view[i - 1], nullptr);
        ctx->bindResourceView(GENERATE_MIPMAP_OUTPUT, m_mipsWeights.view[i], nullptr);
        ctx->bindResourceSampler(GENERATE_MIPMAP_INPUT, linearSampler);
        ctx->bindShader(VK_SHADER_STAGE_COMPUTE_BIT, GenerateMipmapShader::getShader());
        const VkExtent3D mipWorkgroups = util::computeBlockCount(VkExtent3D { mipResolution.x, mipResolution.y, 1 }, VkExtent3D { 16, 16, 1 });
        ctx->dispatch(mipWorkgroups.width, mipWorkgroups.height, mipWorkgroups.depth);
      }
    }


    {
      ScopedGpuProfileZone(ctx, "Blend");
      BlendArgs pushArgs = {};
      pushArgs.debugView = debugView.debugViewIdx();
      ctx->pushConstants(0, sizeof(pushArgs), &pushArgs);
      ctx->bindResourceView(BLEND_EXPOSURE_INPUT, m_mips.view[mipLevel], nullptr);
      ctx->bindResourceView(BLEND_WEIGHT_INPUT, m_mipsWeights.view[mipLevel], nullptr);
      ctx->bindResourceView(BLEND_OUTPUT, m_mipsAssemble.view[mipLevel], nullptr);
      ctx->bindResourceView(BLEND_DEBUG_VIEW_OUTPUT, debugView.getDebugOutput(), nullptr);
      ctx->bindShader(VK_SHADER_STAGE_COMPUTE_BIT, BlendShader::getShader());
      uvec2 mipResolution = resolutionList[mipLevel];
      const VkExtent3D mipWorkgroups = util::computeBlockCount(VkExtent3D { mipResolution.x, mipResolution.y, 1}, VkExtent3D { 16, 16, 1 });
      ctx->dispatch(mipWorkgroups.width, mipWorkgroups.height, mipWorkgroups.depth);
    }

    for (int i = mipLevel; i > displayMipLevel; i--) {
      ScopedGpuProfileZone(ctx, "Blend Laplacian");

      // Blend the finer levels - Laplacians.
      uvec2 targetResolution = resolutionList[i-1];
      BlendLaplacianArgs pushArgs = {};
      pushArgs.resolution = targetResolution;
      pushArgs.boostLocalContrast = boostLocalContrast() ? 1 : 0;
      pushArgs.debugView = debugView.debugViewIdx();
      ctx->pushConstants(0, sizeof(pushArgs), &pushArgs);
      ctx->bindResourceView(BLEND_LAPLACIAN_EXPOSURE_INPUT, m_mips.view[i-1], nullptr);
      ctx->bindResourceView(BLEND_LAPLACIAN_EXPOSURE_COARSER_INPUT, m_mips.view[i], nullptr);
      ctx->bindResourceView(BLEND_LAPLACIAN_WEIGHT_INPUT, m_mipsWeights.view[i-1], nullptr);
      ctx->bindResourceView(BLEND_LAPLACIAN_ACCUMULATE_INPUT, m_mipsAssemble.view[i], nullptr);

      ctx->bindResourceSampler(BLEND_LAPLACIAN_EXPOSURE_INPUT, linearSampler);
      ctx->bindResourceSampler(BLEND_LAPLACIAN_EXPOSURE_COARSER_INPUT, linearSampler);
      ctx->bindResourceSampler(BLEND_LAPLACIAN_WEIGHT_INPUT, linearSampler);
      ctx->bindResourceSampler(BLEND_LAPLACIAN_ACCUMULATE_INPUT, linearSampler);

      ctx->bindResourceView(BLEND_LAPLACIAN_OUTPUT, m_mipsAssemble.view[i-1], nullptr);
      ctx->bindResourceView(BLEND_LAPLACIAN_DEBUG_VIEW_OUTPUT, debugView.getDebugOutput(), nullptr);
      ctx->bindShader(VK_SHADER_STAGE_COMPUTE_BIT, BlendLaplacianShader::getShader());
      const VkExtent3D mipWorkgroups = util::computeBlockCount(VkExtent3D { targetResolution.x, targetResolution.y, 1 }, VkExtent3D { 16, 16, 1 });
      ctx->dispatch(mipWorkgroups.width, mipWorkgroups.height, mipWorkgroups.depth);
    }

    {
      ScopedGpuProfileZone(ctx, "Final Combine");

      uvec2 mipResolution = resolutionList[displayMipLevel];
      FinalCombineArgs pushArgs = {};
      pushArgs.mipPixelSize = vec4 {
        (float) mipResolution.x,
        (float) mipResolution.y,
        1.0f / mipResolution.x,
        1.0f / mipResolution.y
      };
      pushArgs.exposure = exposure();
      pushArgs.resolution = uvec2 { finalResolution.width, finalResolution.height };
      pushArgs.debugView = debugView.debugViewIdx();
      pushArgs.enableAutoExposure = enableAutoExposure;
      pushArgs.performSRGBConversion = performSRGBConversion;
      pushArgs.finalizeWithACES = finalizeWithACES();
      switch (ditherMode()) {
      case DitherMode::None: pushArgs.ditherMode = ditherModeNone; break;
      case DitherMode::Spatial: pushArgs.ditherMode = ditherModeSpatialOnly; break;
      case DitherMode::SpatialTemporal: pushArgs.ditherMode = ditherModeSpatialTemporal; break;
      }
      pushArgs.frameIndex = ctx->getDevice()->getCurrentFrameId();

      ctx->pushConstants(0, sizeof(pushArgs), &pushArgs);

      ctx->bindResourceView(FINAL_COMBINE_BLUE_NOISE_TEXTURE_INPUT, ctx->getResourceManager().getBlueNoiseTexture(ctx), nullptr);
      ctx->bindResourceView(FINAL_COMBINE_ORIGINAL_MIP0, m_mips.view[0], nullptr);
      ctx->bindResourceView(FINAL_COMBINE_ORIGINAL_MIP, m_mips.view[displayMipLevel], nullptr);
      ctx->bindResourceView(FINAL_COMBINE_WEIGHT_MIP0, m_mipsWeights.view[0], nullptr);
      ctx->bindResourceView(FINAL_COMBINE_MIP_ASSEMBLE, m_mipsAssemble.view[displayMipLevel], nullptr);

      ctx->bindResourceSampler(FINAL_COMBINE_ORIGINAL_MIP, linearSampler);
      ctx->bindResourceSampler(FINAL_COMBINE_MIP_ASSEMBLE, linearSampler);

      ctx->bindResourceView(FINAL_COMBINE_OUTPUT, rtOutput.m_finalOutput.view, nullptr);
      ctx->bindResourceView(FINAL_COMBINE_DEBUG_VIEW_OUTPUT, debugView.getDebugOutput(), nullptr);
      ctx->bindResourceView(FINAL_COMBINE_EXPOSURE, exposureView, nullptr);
      ctx->bindShader(VK_SHADER_STAGE_COMPUTE_BIT, FinalCombineShader::getShader());
      ctx->dispatch(workgroups.width, workgroups.height, workgroups.depth);
    }
  }

  void DxvkLocalToneMapping::createTargetResource(Rc<DxvkContext>& ctx, const VkExtent3D& targetExtent) {
    vec2 extendF = { static_cast<float>(targetExtent.width), static_cast<float>(targetExtent.height) };
    int mipLevel = static_cast<int>(floor(log2f(std::max(extendF.x, extendF.y)))) + 1;

    m_mips = Resources::createMipmapResource(ctx, targetExtent, VK_FORMAT_R16G16B16A16_SFLOAT, mipLevel, "local tone mapper mips");
    m_mipsWeights = Resources::createMipmapResource(ctx, targetExtent, VK_FORMAT_A2B10G10R10_UNORM_PACK32, mipLevel, "local tone mapper mips weights");
    m_mipsAssemble = Resources::createMipmapResource(ctx, targetExtent, VK_FORMAT_R16_SFLOAT, mipLevel, "local tone mapper mips assemble");
  }

  void DxvkLocalToneMapping::releaseTargetResource() {
    m_mips.reset();
    m_mipsWeights.reset();
    m_mipsAssemble.reset();
  }
}
