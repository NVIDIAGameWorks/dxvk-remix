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
#include "rtx_tone_mapping.h"
#include "dxvk_device.h"
#include "dxvk_scoped_annotation.h"
#include "rtx_render/rtx_shader_manager.h"
#include "rtx.h"
#include "rtx/pass/tonemap/tonemapping.h"

#include <rtx_shaders/auto_exposure.h>
#include <rtx_shaders/auto_exposure_histogram.h>
#include "rtx_imgui.h"
#include "rtx_render/rtx_debug_view.h"

#include "rtx/utility/debug_view_indices.h"

static_assert((TONEMAPPING_TONE_CURVE_SAMPLE_COUNT & 1) == 0, "The shader expects a sample count that is a multiply of 2.");

namespace dxvk {
  // Defined within an unnamed namespace to ensure unique definition across binary
  namespace {
    class AutoExposureHistogramShader : public ManagedShader {
      SHADER_SOURCE(AutoExposureHistogramShader, VK_SHADER_STAGE_COMPUTE_BIT, auto_exposure_histogram)

      PUSH_CONSTANTS(ToneMappingAutoExposureArgs)

      BEGIN_PARAMETER()
        RW_TEXTURE2D(AUTO_EXPOSURE_COLOR_INPUT)
        RW_TEXTURE1D(AUTO_EXPOSURE_HISTOGRAM_INPUT_OUTPUT)
      END_PARAMETER()
    };

    PREWARM_SHADER_PIPELINE(AutoExposureHistogramShader);

    class AutoExposureShader : public ManagedShader
    {
      SHADER_SOURCE(AutoExposureShader, VK_SHADER_STAGE_COMPUTE_BIT, auto_exposure)

      PUSH_CONSTANTS(ToneMappingAutoExposureArgs)

      BEGIN_PARAMETER()
        RW_TEXTURE1D(AUTO_EXPOSURE_HISTOGRAM_INPUT_OUTPUT)
        RW_TEXTURE1D(AUTO_EXPOSURE_EXPOSURE_INPUT_OUTPUT)
        RW_TEXTURE2D(AUTO_EXPOSURE_DEBUG_VIEW_OUTPUT)
        SAMPLER1D(AUTO_EXPOSURE_EC_INPUT)
      END_PARAMETER()
    };

    PREWARM_SHADER_PIPELINE(AutoExposureShader);
  }
  
  DxvkAutoExposure::DxvkAutoExposure(DxvkDevice* device)
  : CommonDeviceObject(device), m_vkd(device->vkd())  {
  }
  
  DxvkAutoExposure::~DxvkAutoExposure()  {  }

  // Calculate polyline
  static float lerp(void* data, int i) {
    float** points = (float**) data;
    float percent = clamp((float) i / (EXPOSURE_HISTOGRAM_SIZE - 1), 0.f, 0.999f);

    const int kNumPoints = 5;

    float offset = percent * (kNumPoints - 1);
    int lowerBin = int(offset);
    float w = offset - lowerBin;

    return *points[lowerBin] * (1.0f - w) + *points[lowerBin + 1] * w;
  }

  // This function is used to interpolate data for ImGUI plotting, which requires a void* function signature
  static float histogramWeight(void* data, int i) {
    return lerp(data, i);
  }

  void DxvkAutoExposure::showImguiSettings() {

    RemixGui::Checkbox("Eye Adaptation", &enabledObject());
    if (enabled()) {
      ImGui::Indent();
      RemixGui::Combo("Average Mode", &exposureAverageModeObject(), "Mean\0Median");

      RemixGui::DragFloat("Adaptation Speed", &autoExposureSpeedObject(), 0.001f, 0.f, 100.f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
      RemixGui::DragFloat("Min (EV100)", &evMinValueObject(), 0.01f, -24.f, 24.f);
      RemixGui::DragFloat("Max (EV100)", &evMaxValueObject(), 0.01f, -24.f, 24.f);

      RemixGui::Checkbox("Center Weighted Metering", &exposureCenterMeteringEnabledObject());
      ImGui::BeginDisabled(!exposureCenterMeteringEnabled());
      RemixGui::DragFloat("Center Metering Size", &centerMeteringSizeObject(), 0.01f, 0.01f, 1.0f);
      ImGui::EndDisabled();

      RemixGui::Checkbox("Exposure Compensation", &useExposureCompensationObject());

      if (useExposureCompensation()) {
        ImGui::Indent();
        const uint32_t kNumPoints = 5;
        dxvk::RtxOption<float>* splineOptions[] = {
          &exposureWeightCurve0,
          &exposureWeightCurve1,
          &exposureWeightCurve2,
          &exposureWeightCurve3,
          &exposureWeightCurve4
        };
        float splineValues[kNumPoints];
        float* splinePoints[kNumPoints];
        for (int i = 0; i < kNumPoints; i++) {
          splineValues[i] = splineOptions[i]->get();
          splinePoints[i] = &splineValues[i];
        }
        ImGui::PushID("AE-Spline-lines");
        ImGui::PlotLines("", &histogramWeight, splinePoints, EXPOSURE_HISTOGRAM_SIZE, 0, "", 0.0f, 1.0f, ImVec2(0, 150.0f));
        ImGui::PopID();
        
        ImGui::Columns(IM_ARRAYSIZE(splinePoints), "splineControls");
        for (int i = 0; i < IM_ARRAYSIZE(splinePoints); i++) {
          ImGui::SetColumnWidth(i, 45);

          ImGui::PushID(str::format("AE-Spline", i).c_str());
          float oldValue = *splinePoints[i];
          ImGui::VSliderFloat("", ImVec2(25, 72), splinePoints[i], 0.f, 1.f, "");
          m_isCurveChanged |= oldValue != *splinePoints[i];
          ImGui::PopID();

          float ev = Lerp(evMinValue(), evMaxValue(), (float) i / (IM_ARRAYSIZE(splinePoints) - 1));
          ImGui::Text(str::format(ev, "ev").c_str());

          ImGui::NextColumn();
        }
        ImGui::Columns(1);

        if (ImGui::Button("Reset")) {
          for (int i = 0; i < kNumPoints; i++) {
            splineValues[i] = 1.f;
          }
          m_isCurveChanged = true;
        }

        if (m_isCurveChanged) {
          for (int i = 0; i < kNumPoints; i++) {
            splineOptions[i]->setDeferred(splineValues[i]);
          }
          m_isCurveChanged = false;
        }
        ImGui::Unindent();
      }

      RemixGui::Separator();
      ImGui::Unindent();
    }
  }

  void DxvkAutoExposure::createResources(Rc<DxvkContext> ctx) {
    if (m_exposure.image != nullptr) {
      return;
    }

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

    desc.extent = VkExtent3D{ 1, 1, 1 };

    viewInfo.format = desc.format = VK_FORMAT_R32_SFLOAT;
    viewInfo.usage = desc.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    m_exposure.image = device()->createImage(desc, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, DxvkMemoryStats::Category::RTXRenderTarget, "autoexposure");
    m_exposure.view = device()->createImageView(m_exposure.image, viewInfo);
    ctx->changeImageLayout(m_exposure.image, VK_IMAGE_LAYOUT_GENERAL);

    desc.extent = VkExtent3D { EXPOSURE_HISTOGRAM_SIZE, 1, 1 };
    viewInfo.format = desc.format = VK_FORMAT_R32_UINT;
    viewInfo.usage = desc.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    m_exposureHistogram.image = device()->createImage(desc, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, DxvkMemoryStats::Category::RTXRenderTarget, "autoexposure histogram");
    m_exposureHistogram.view = device()->createImageView(m_exposureHistogram.image, viewInfo);
    ctx->changeImageLayout(m_exposureHistogram.image, VK_IMAGE_LAYOUT_GENERAL);

    desc.extent = VkExtent3D { EXPOSURE_HISTOGRAM_SIZE, 1, 1 };
    viewInfo.format = desc.format = VK_FORMAT_R32_SFLOAT;
    viewInfo.usage = desc.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    m_exposureWeightCurve.image = device()->createImage(desc, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, DxvkMemoryStats::Category::RTXRenderTarget, "autoexposure weight curve");
    m_exposureWeightCurve.view = device()->createImageView(m_exposureWeightCurve.image, viewInfo);
    ctx->changeImageLayout(m_exposureWeightCurve.image, VK_IMAGE_LAYOUT_GENERAL);
  }

  void DxvkAutoExposure::dispatchAutoExposure(
    Rc<DxvkContext> ctx,
    Rc<DxvkSampler> linearSampler,
    const Resources::RaytracingOutput& rtOutput,
    const float frameTimeMilliseconds) {

    if (m_resetState || !enabled()) {
      VkClearColorValue clearColor; 
      clearColor.float32[0] = clearColor.float32[1] = clearColor.float32[2] = clearColor.float32[3] = exp2f(0.0f);

      VkImageSubresourceRange subRange = {};
      subRange.layerCount = 1;
      subRange.levelCount = 1;
      subRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

      ctx->clearColorImage(m_exposure.image, clearColor, subRange);

      clearColor.uint32[0] = clearColor.uint32[1] = clearColor.uint32[2] = clearColor.uint32[3] = 0;
      ctx->clearColorImage(m_exposureHistogram.image, clearColor, subRange);
    }

    if (enabled()) {
      if (useExposureCompensation() && m_isCurveChanged) {
        float data[EXPOSURE_HISTOGRAM_SIZE];

        const uint32_t kNumPoints = 5;
        dxvk::RtxOption<float>* splineOptions[] = {
          &exposureWeightCurve0,
          &exposureWeightCurve1,
          &exposureWeightCurve2,
          &exposureWeightCurve3,
          &exposureWeightCurve4
        };
        float splineValues[kNumPoints];
        float* splinePoints[kNumPoints];
        for (int i = 0; i < kNumPoints; i++) {
          splineValues[i] = splineOptions[i]->get();
          splinePoints[i] = &splineValues[i];
        }

        for (int i = 0; i < EXPOSURE_HISTOGRAM_SIZE; i++) {
          data[i] = histogramWeight(splinePoints, i);
        }

        const uint32_t rowPitch = EXPOSURE_HISTOGRAM_SIZE * sizeof(float);
        ctx->updateImage(m_exposureWeightCurve.image,
                         VkImageSubresourceLayers { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
                         VkOffset3D { 0, 0, 0 },
                         VkExtent3D { EXPOSURE_HISTOGRAM_SIZE, 1, 1 },
                         (void*) &data[0], rowPitch, rowPitch);

        m_isCurveChanged = false;
      }


      {
        ScopedGpuProfileZone(ctx, "Histogram");
        static_cast<RtxContext*>(ctx.ptr())->setFramePassStage(RtxFramePassStage::AutoExposure_Histogram);
        // Prepare shader arguments
        ToneMappingAutoExposureArgs pushArgs = {};
        pushArgs.numPixels = rtOutput.m_finalOutputExtent.width * rtOutput.m_finalOutputExtent.height;
        // Note: Autoexposure speed is in units per second, so convert from milliseconds to seconds here.
        pushArgs.autoExposureSpeed = autoExposureSpeed() * (0.001f * frameTimeMilliseconds);
        pushArgs.evMinValue = evMinValue();
        pushArgs.evRange = evMaxValue() - evMinValue();
        pushArgs.debugMode = (ctx->getCommonObjects()->metaDebugView().debugViewIdx() == DEBUG_VIEW_EXPOSURE_HISTOGRAM);
        pushArgs.enableCenterMetering = exposureCenterMeteringEnabled();
        pushArgs.centerMeteringSize = centerMeteringSize();
        pushArgs.averageMode = (uint32_t)exposureAverageMode();
        pushArgs.useExposureCompensation = useExposureCompensation();
        ctx->pushConstants(0, sizeof(pushArgs), &pushArgs);

        // Calculate histogram
        ctx->bindResourceView(AUTO_EXPOSURE_HISTOGRAM_INPUT_OUTPUT, m_exposureHistogram.view, nullptr);
        ctx->bindResourceView(AUTO_EXPOSURE_COLOR_INPUT, rtOutput.m_finalOutput.view(Resources::AccessType::Read), nullptr);

        ctx->bindShader(VK_SHADER_STAGE_COMPUTE_BIT, AutoExposureHistogramShader::getShader());
        const VkExtent3D workgroups = util::computeBlockCount(rtOutput.m_finalOutputExtent, VkExtent3D { 16, 16, 1 });
        ctx->dispatch(workgroups.width, workgroups.height, workgroups.depth);
      }

      // Calculate avg luminance
      {
        ScopedGpuProfileZone(ctx, "Exposure");
        static_cast<RtxContext*>(ctx.ptr())->setFramePassStage(RtxFramePassStage::AutoExposure_Exposure);
        DebugView& debugView = ctx->getDevice()->getCommon()->metaDebugView();

        ctx->bindResourceView(AUTO_EXPOSURE_HISTOGRAM_INPUT_OUTPUT, m_exposureHistogram.view, nullptr);
        ctx->bindResourceView(AUTO_EXPOSURE_EXPOSURE_INPUT_OUTPUT, m_exposure.view, nullptr);
        ctx->bindResourceView(AUTO_EXPOSURE_DEBUG_VIEW_OUTPUT, debugView.getDebugOutput(), nullptr);
        ctx->bindResourceView(AUTO_EXPOSURE_EC_INPUT, m_exposureWeightCurve.view, nullptr);
        ctx->bindResourceSampler(AUTO_EXPOSURE_EC_INPUT, linearSampler);
        ctx->bindShader(VK_SHADER_STAGE_COMPUTE_BIT, AutoExposureShader::getShader());
        ctx->dispatch(1, 1, 1);
      }
    }
  }

  void DxvkAutoExposure::dispatch(
    Rc<DxvkContext> ctx,
    Rc<DxvkSampler> linearSampler,
    const Resources::RaytracingOutput& rtOutput,
    const float frameTimeMilliseconds,
    bool performSRGBConversion,
    bool resetHistory) {

    ScopedGpuProfileZone(ctx, "Auto Exposure");

    m_resetState |= resetHistory;

    ctx->setPushConstantBank(DxvkPushConstantBank::RTX);

    // TODO : set reset on significant camera changes as well
    if (m_exposureHistogram.image.ptr() == nullptr) {
      createResources(ctx);
      m_resetState = true;
    }

    dispatchAutoExposure(ctx, linearSampler, rtOutput, frameTimeMilliseconds);

    m_resetState = false;
  }
}
