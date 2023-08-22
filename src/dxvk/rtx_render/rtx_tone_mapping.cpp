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
#include "rtx_tone_mapping.h"
#include "dxvk_device.h"
#include "dxvk_scoped_annotation.h"
#include "rtx_render/rtx_shader_manager.h"
#include "rtx.h"
#include "rtx/pass/tonemap/tonemapping.h"

#include <rtx_shaders/auto_exposure.h>
#include <rtx_shaders/auto_exposure_histogram.h>
#include <rtx_shaders/tonemapping_histogram.h>
#include <rtx_shaders/tonemapping_tone_curve.h>
#include <rtx_shaders/tonemapping_apply_tonemapping.h>
#include "rtx_imgui.h"
#include "rtx/utility/debug_view_indices.h"

static_assert((TONEMAPPING_TONE_CURVE_SAMPLE_COUNT & 1) == 0, "The shader expects a sample count that is a multiply of 2.");

namespace dxvk {
  // Defined within an unnamed namespace to ensure unique definition across binary
  namespace {
    class HistogramShader : public ManagedShader
    {
      SHADER_SOURCE(HistogramShader, VK_SHADER_STAGE_COMPUTE_BIT, tonemapping_histogram)

      PUSH_CONSTANTS(ToneMappingHistogramArgs)

      BEGIN_PARAMETER()
        RW_TEXTURE1D(TONEMAPPING_HISTOGRAM_HISTOGRAM_INPUT_OUTPUT)
        RW_TEXTURE2D_READONLY(TONEMAPPING_HISTOGRAM_COLOR_INPUT)
        RW_TEXTURE1D_READONLY(TONEMAPPING_HISTOGRAM_EXPOSURE_INPUT)
      END_PARAMETER()
    };

    PREWARM_SHADER_PIPELINE(HistogramShader);

    class ToneCurveShader : public ManagedShader
    {
      SHADER_SOURCE(ToneCurveShader, VK_SHADER_STAGE_COMPUTE_BIT, tonemapping_tone_curve)

      PUSH_CONSTANTS(ToneMappingCurveArgs)

      BEGIN_PARAMETER()
        RW_TEXTURE1D(TONEMAPPING_TONE_CURVE_HISTOGRAM_INPUT_OUTPUT)
        RW_TEXTURE1D(TONEMAPPING_TONE_CURVE_TONE_CURVE_INPUT_OUTPUT)
      END_PARAMETER()
    };

    PREWARM_SHADER_PIPELINE(ToneCurveShader);

    class ApplyTonemappingShader : public ManagedShader
    {
      SHADER_SOURCE(ApplyTonemappingShader, VK_SHADER_STAGE_COMPUTE_BIT, tonemapping_apply_tonemapping)

      PUSH_CONSTANTS(ToneMappingApplyToneMappingArgs)

      BEGIN_PARAMETER()
        RW_TEXTURE2D(TONEMAPPING_APPLY_TONEMAPPING_COLOR_INPUT)
        SAMPLER1D(TONEMAPPING_APPLY_TONEMAPPING_TONE_CURVE_INPUT)
        RW_TEXTURE1D_READONLY(TONEMAPPING_APPLY_TONEMAPPING_EXPOSURE_INPUT)
        RW_TEXTURE2D(TONEMAPPING_APPLY_TONEMAPPING_COLOR_OUTPUT)
      END_PARAMETER()
    };

    PREWARM_SHADER_PIPELINE(ApplyTonemappingShader);
  }
  
  DxvkToneMapping::DxvkToneMapping(DxvkDevice* device)
  : CommonDeviceObject(device), m_vkd(device->vkd())  {
  }
  
  DxvkToneMapping::~DxvkToneMapping()  {  }

  void DxvkToneMapping::showImguiSettings() {

    ImGui::DragFloat("Global Exposure", &exposureBiasObject(), 0.01f, -4.f, 4.f);
    
    ImGui::Checkbox("Color Grading Enabled", &colorGradingEnabledObject());
    if (colorGradingEnabled()) {
      ImGui::Indent();
      ImGui::DragFloat("Contrast", &contrastObject(), 0.01f, 0.f, 2.f);
      ImGui::DragFloat("Saturation", &saturationObject(), 0.01f, 0.f, 2.f);
      ImGui::DragFloat3("Color Balance", &colorBalanceObject(), 0.01f, 0.f, 2.f);
      ImGui::Separator();
      ImGui::Unindent();
    }

    ImGui::Checkbox("Tonemapping Enabled", &tonemappingEnabledObject());
    if (tonemappingEnabled()) {
      ImGui::Indent();
      ImGui::Checkbox("Finalize With ACES", &finalizeWithACESObject());

      ImGui::Checkbox("Tuning Mode", &tuningModeObject());
      if (tuningMode()) {
        ImGui::Indent();

        ImGui::DragFloat("Curve Shift", &curveShiftObject(), 0.01f, 0.f, 0.f);
        ImGui::DragFloat("Shadow Min Slope", &shadowMinSlopeObject(), 0.01f, 0.f, 0.f);
        ImGui::DragFloat("Shadow Contrast", &shadowContrastObject(), 0.01f, 0.f, 0.f);
        ImGui::DragFloat("Shadow Contrast End", &shadowContrastEndObject(), 0.01f, 0.f, 0.f);
        ImGui::DragFloat("Min Stops", &toneCurveMinStopsObject(), 0.01f, 0.f, 0.f);
        ImGui::DragFloat("Max Stops", &toneCurveMaxStopsObject(), 0.01f, 0.f, 0.f);

        ImGui::DragFloat("Max Exposure Increase", &maxExposureIncreaseObject(), 0.01f, 0.f, 0.f);
        ImGui::DragFloat("Dynamic Range", &dynamicRangeObject(), 0.01f, 0.f, 0.f);

        ImGui::Unindent();
      }
      ImGui::Separator();
      ImGui::Unindent();
    }
  }

  void DxvkToneMapping::createResources(Rc<DxvkContext> ctx) {
    DxvkImageCreateInfo desc;
    desc.type = VK_IMAGE_TYPE_1D;
    desc.flags = 0;
    desc.sampleCount = VK_SAMPLE_COUNT_1_BIT;
    desc.numLayers = 1;
    desc.mipLevels = 1;
    desc.stages = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    desc.access = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    desc.tiling = VK_IMAGE_TILING_OPTIMAL;
    desc.layout = VK_IMAGE_LAYOUT_UNDEFINED;

    DxvkImageViewCreateInfo viewInfo;
    viewInfo.type = VK_IMAGE_VIEW_TYPE_1D;
    viewInfo.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.minLevel = 0;
    viewInfo.numLevels = 1;
    viewInfo.minLayer = 0;
    viewInfo.numLayers = 1;
    viewInfo.format = desc.format;

    desc.extent = VkExtent3D{ TONEMAPPING_TONE_CURVE_SAMPLE_COUNT, 1, 1 };

    viewInfo.format = desc.format = VK_FORMAT_R32_UINT;
    viewInfo.usage = desc.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    m_toneHistogram.image = device()->createImage(desc, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, DxvkMemoryStats::Category::RTXRenderTarget, "tone mapper histogram");
    m_toneHistogram.view = device()->createImageView(m_toneHistogram.image, viewInfo);
    ctx->changeImageLayout(m_toneHistogram.image, VK_IMAGE_LAYOUT_GENERAL);

    viewInfo.format = desc.format = VK_FORMAT_R32_SFLOAT;
    viewInfo.usage = desc.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
    m_toneCurve.image = device()->createImage(desc, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, DxvkMemoryStats::Category::RTXRenderTarget, "tone mapper curve");
    m_toneCurve.view = device()->createImageView(m_toneCurve.image, viewInfo);
    ctx->changeImageLayout(m_toneCurve.image, VK_IMAGE_LAYOUT_GENERAL);
  }

  void DxvkToneMapping::dispatchHistogram(
    Rc<DxvkContext> ctx,
    Rc<DxvkImageView> exposureView,
    const Resources::Resource& colorBuffer,
    bool autoExposureEnabled) {

    ScopedGpuProfileZone(ctx, "Tonemap: Generate Histogram");

    // Clear the histogram resource
    if(m_resetState) {
      VkClearColorValue clearColor;
      clearColor.float32[0] = clearColor.float32[1] = clearColor.float32[2] = clearColor.float32[3] = 0;

      VkImageSubresourceRange subRange = {};
      subRange.layerCount = 1;
      subRange.levelCount = 1;
      subRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

      ctx->clearColorImage(m_toneHistogram.image, clearColor, subRange);
    }

    // Prepare shader arguments
    ToneMappingHistogramArgs pushArgs = {};
    pushArgs.enableAutoExposure = autoExposureEnabled;
    pushArgs.toneCurveMinStops = toneCurveMinStops();
    pushArgs.toneCurveMaxStops = toneCurveMaxStops();
    pushArgs.exposureFactor = exp2f(exposureBias());

    ctx->pushConstants(0, sizeof(pushArgs), &pushArgs);

    VkExtent3D workgroups = util::computeBlockCount(colorBuffer.view->imageInfo().extent, VkExtent3D{16, 16, 1 });

    ctx->bindResourceView(TONEMAPPING_HISTOGRAM_COLOR_INPUT, colorBuffer.view, nullptr);
    ctx->bindResourceView(TONEMAPPING_HISTOGRAM_HISTOGRAM_INPUT_OUTPUT, m_toneHistogram.view, nullptr);
    ctx->bindResourceView(TONEMAPPING_HISTOGRAM_EXPOSURE_INPUT, exposureView, nullptr);
    ctx->bindShader(VK_SHADER_STAGE_COMPUTE_BIT, HistogramShader::getShader());
    ctx->dispatch(workgroups.width, workgroups.height, workgroups.depth);
  }

  void DxvkToneMapping::dispatchToneCurve(
    Rc<DxvkContext> ctx) {

    ScopedGpuProfileZone(ctx, "Tonemap: Calculate Tone Curve");

    // Prepare shader arguments
    ToneMappingCurveArgs pushArgs = {};
    pushArgs.dynamicRange = dynamicRange();
    pushArgs.shadowMinSlope = shadowMinSlope();
    pushArgs.shadowContrast = shadowContrast();
    pushArgs.shadowContrastEnd = shadowContrastEnd();
    pushArgs.maxExposureIncrease = maxExposureIncrease();
    pushArgs.curveShift = curveShift();
    pushArgs.toneCurveMinStops = toneCurveMinStops();
    pushArgs.toneCurveMaxStops = toneCurveMaxStops();
    pushArgs.needsReset = m_resetState;

    VkExtent3D workgroups = VkExtent3D{ TONEMAPPING_TONE_CURVE_SAMPLE_COUNT, 1, 1 };

    ctx->bindResourceView(TONEMAPPING_TONE_CURVE_HISTOGRAM_INPUT_OUTPUT, m_toneHistogram.view, nullptr);
    ctx->bindResourceView(TONEMAPPING_TONE_CURVE_TONE_CURVE_INPUT_OUTPUT, m_toneCurve.view, nullptr);
    ctx->bindShader(VK_SHADER_STAGE_COMPUTE_BIT, ToneCurveShader::getShader());
    ctx->pushConstants(0, sizeof(pushArgs), &pushArgs);
    ctx->dispatch(workgroups.width, workgroups.height, workgroups.depth);
  }

  void DxvkToneMapping::dispatchApplyToneMapping(
    Rc<DxvkContext> ctx,
    Rc<DxvkSampler> linearSampler,
    Rc<DxvkImageView> exposureView,
    const Resources::Resource& inputBuffer,
    const Resources::Resource& colorBuffer,
    bool performSRGBConversion,
    bool autoExposureEnabled) {

    ScopedGpuProfileZone(ctx, "Apply Tone Mapping");

    const VkExtent3D workgroups = util::computeBlockCount(colorBuffer.view->imageInfo().extent, VkExtent3D{ 16 , 16, 1 });

    // Prepare shader arguments
    ToneMappingApplyToneMappingArgs pushArgs = {};
    pushArgs.toneMappingEnabled = tonemappingEnabled();
    pushArgs.colorGradingEnabled = colorGradingEnabled();
    pushArgs.enableAutoExposure = autoExposureEnabled;
    pushArgs.finalizeWithACES = finalizeWithACES();

    // Tonemap args
    pushArgs.performSRGBConversion = performSRGBConversion;
    pushArgs.shadowContrast = shadowContrast();
    pushArgs.shadowContrastEnd = shadowContrastEnd();
    pushArgs.exposureFactor = exp2f(exposureBias()); // ev100
    pushArgs.toneCurveMinStops = toneCurveMinStops();
    pushArgs.toneCurveMaxStops = toneCurveMaxStops();
    pushArgs.debugMode = tuningMode();

    // Color grad args
    pushArgs.colorBalance = colorBalance();
    pushArgs.contrast = contrast();
    pushArgs.saturation = saturation();

    ctx->bindResourceView(TONEMAPPING_APPLY_TONEMAPPING_COLOR_INPUT, inputBuffer.view, nullptr);
    ctx->bindResourceView(TONEMAPPING_APPLY_TONEMAPPING_TONE_CURVE_INPUT, m_toneCurve.view, nullptr);
    ctx->bindResourceView(TONEMAPPING_APPLY_TONEMAPPING_EXPOSURE_INPUT, exposureView, nullptr);
    ctx->bindResourceSampler(TONEMAPPING_APPLY_TONEMAPPING_TONE_CURVE_INPUT, linearSampler);
    ctx->bindResourceView(TONEMAPPING_APPLY_TONEMAPPING_COLOR_OUTPUT, colorBuffer.view, nullptr);
    ctx->bindShader(VK_SHADER_STAGE_COMPUTE_BIT, ApplyTonemappingShader::getShader());
    ctx->pushConstants(0, sizeof(pushArgs), &pushArgs);
    ctx->dispatch(workgroups.width, workgroups.height, workgroups.depth);

  }

  void DxvkToneMapping::dispatch(
    Rc<DxvkContext> ctx,
    Rc<DxvkSampler> linearSampler,
    Rc<DxvkImageView> exposureView,
    const Resources::RaytracingOutput& rtOutput,
    const float deltaTime,
    bool performSRGBConversion,
    bool resetHistory,
    bool autoExposureEnabled) {

    ScopedGpuProfileZone(ctx, "Tone Mapping");

    m_resetState |= resetHistory;

    ctx->setPushConstantBank(DxvkPushConstantBank::RTX);

    // TODO : set reset on significant camera changes as well
    if (m_toneHistogram.image.ptr() == nullptr) {
      createResources(ctx);
      m_resetState = true;
    }

    const Resources::Resource& inputColorBuffer = rtOutput.m_finalOutput;
    if (tonemappingEnabled()) {
      dispatchHistogram(ctx, exposureView, inputColorBuffer, autoExposureEnabled);
      dispatchToneCurve(ctx);
    }

    dispatchApplyToneMapping(ctx, linearSampler, exposureView, inputColorBuffer, rtOutput.m_finalOutput, performSRGBConversion, autoExposureEnabled);

    m_resetState = false;
  }
}
