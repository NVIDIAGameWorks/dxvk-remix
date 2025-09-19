/*
* Copyright (c) 2022-2025, NVIDIA CORPORATION. All rights reserved.
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
#pragma once

#include "rtx/utility/shader_types.h"
#include "rtx/utility/debug_view_indices.h"
#include "rtx/pass/debug_view/debug_view_args.h"

#include "dxvk_context.h"
#include "../dxvk_include.h"

#include "rtx_resources.h"
#include "rtx_objectpicking.h"
#include "rtx_options.h"
#include "rtx_accumulation.h"

struct DebugViewArgs;

namespace dxvk {
  class Config;
  class DxvkDevice;
  class DxvkObjects;
  class RtxContext;

  class DebugView : public RtxPass {

  public:

    DebugView(dxvk::DxvkDevice* device);
    ~DebugView() = default;

    void dispatch(Rc<RtxContext> ctx, Rc<DxvkSampler> nearestSampler, Rc<DxvkSampler> linearSampler, Rc<DxvkImage>& outputImage, const Resources::RaytracingOutput& rtOutput, DxvkObjects& common);
    void dispatchAfterCompositionPass(Rc<RtxContext> ctx, Rc<DxvkSampler> nearestSampler, Rc<DxvkSampler> linearSampler, const Resources::RaytracingOutput& rtOutput, DxvkObjects& common);
    void initSettings(const dxvk::Config& config);
    void prewarmShaders(DxvkPipelineManager& pipelineManager) const;

    void showImguiSettings();
    const vec4& debugKnob() const { return m_debugKnob; }

    bool getOverlayOnTopOfRenderOutput() const;

    const Rc<DxvkImageView>& getDebugOutput() {
      return m_debugView.view;
    }

    const Rc<DxvkImageView>& getFinalDebugOutput() {
      return static_cast<CompositeDebugView>(m_composite.compositeViewIdx()) != CompositeDebugView::Disabled
        ? m_composite.compositeView.view
        : m_debugView.view;
    }

    const Rc<DxvkImageView>& getInstrumentation() {
      return m_instrumentation.view;
    }

    uint32_t getDebugViewIndex() const;
    void setDebugViewIndex(uint32_t debugViewIndex);

    // GPU Print
    static struct GpuPrint {
      friend class DebugView;
      RTX_OPTION("rtx.debugView.gpuPrint", bool, enable, false, "Enables writing into a GPU buffer that's read by CPU when CTRL is pressed. The value is printed to console.");
      RTX_OPTION("rtx.debugView.gpuPrint", bool, useMousePosition, true, "Uses mouse position to select a pixel to GPU print for.");
      RTX_OPTION("rtx.debugView.gpuPrint", Vector2i, pixelIndex, Vector2i(INT32_MAX, INT32_MAX), "Pixel position to GPU print for. Requires useMousePosition to be turned off.");
    } gpuPrint;

  private:
    void initCompositeDebugViews();
    void updateCompositeView(Rc<DxvkContext>& ctx);
    void processOutputStatistics(Rc<RtxContext>& ctx, const Resources::RaytracingOutput& rtOutput);
    void showOutputStatistics();
    bool shouldDebugViewDispatch() const;
    void createConstantsBuffer();
    Rc<DxvkBuffer> getDebugViewConstantsBuffer();
    bool areDebugViewStatisticsSupported() const;

    DebugViewArgs getCommonDebugViewArgs(RtxContext& ctx, const Resources::RaytracingOutput& rtOutput, DxvkObjects& common);

    void generateCompositeImage(Rc<DxvkContext> ctx, Rc<DxvkImage>& outputImage);
    virtual void onFrameBegin(Rc<DxvkContext>& ctx, const FrameBeginContext& frameBeginCtx) override;
    virtual void createDownscaledResource(Rc<DxvkContext>& ctx, const VkExtent3D& downscaledExtent) override;
    virtual void releaseDownscaledResource() override;

    virtual bool isEnabled() const override;

    void dispatchDebugViewInternal(Rc<RtxContext> ctx, Rc<DxvkSampler> nearestSampler, Rc<DxvkSampler> linearSampler, DebugViewArgs& debugViewArgs, Rc<DxvkBuffer>& debugViewConstantBuffer, const Resources::RaytracingOutput& rtOutput, DxvkObjects& common);
    void dispatchPostprocess(Rc<RtxContext> ctx, DebugViewArgs& debugViewArgs, Rc<DxvkBuffer>& debugViewConstantBuffer, const Resources::RaytracingOutput& rtOutput);
    void dispatchRenderToOutput(Rc<RtxContext> ctx, DebugViewArgs& debugViewArgs, Rc<DxvkBuffer>& debugViewConstantBuffer, const Resources::RaytracingOutput& rtOutput);
    bool shouldRunDispatchPostCompositePass() const;
    Rc<DxvkShader> getDebugViewShader() const;

    Rc<DxvkBuffer> m_debugViewConstants;
    Rc<vk::DeviceFn> m_vkd;
    dxvk::DxvkDevice* m_device;
    std::chrono::time_point<std::chrono::system_clock> m_startTime;

    RTX_OPTION_ENV("rtx.debugView", uint32_t, debugViewIdx, DEBUG_VIEW_DISABLED, "DXVK_RTX_DEBUG_VIEW_INDEX", "Index of a debug view to show when Debug View is enabled. The index must be a valid value from DEBUG_VIEW_* macro defined indices. Value of 0 disables Debug View.");
    // Note: Used for preserving the debug view state only for ImGui purposes. Not to be used for anything else
    // and should not ever be set to the disabled debug view index.
    uint32_t m_lastDebugViewIdx;
    RTX_OPTION_ENV("rtx.debugView", DebugViewDisplayType, displayType, DebugViewDisplayType::Standard, "DXVK_RTX_DEBUG_VIEW_DISPLAY_TYPE",
                   "The display type to use for visualizing debug view input values.\n"
                   "Supported display types are: 0 = Standard, 1 = BGR Exclusive Color, 2 = EV100, 3 = HDR Waveform\n"
                   "Each mode may be useful for a different kind of visualization, though Standard is typically the most common mode to use.\n"
                   "Standard mode works for a simple direct, scaled or color mapped visualization, BGR exclusive for another type of color mapped visualization, and EV100 or the HDR Waveform for understanding HDR value magnitudes in the input on a log scale.");

    RTX_OPTION("rtx.debugView", DebugViewSamplerType, samplerType, DebugViewSamplerType::NormalizedLinear, "Sampler type for debug views that sample from a texture (applies only to a subset of debug views).\n"
                                                                                                           "Supported types are: 0 = Nearest, 1 = Normalized Nearest, 2 = Normalized Linear");

    struct Composite {
      friend class ImGUI; // <-- we want to modify these values directly.
      friend class DebugView; // <-- we want to modify these values directly.

      RTX_OPTION("rtx.debugView.composite", uint32_t, numColumnsInRuntimeValuesSets, 4, "Number of columns in the runtime values sets. This is used to determine how many values are displayed in the composite view.");

      RTX_OPTION_ENV("rtx.debugView.composite", uint32_t, compositeViewIdx, CompositeDebugView::Disabled, "RTX_DEBUG_VIEW_COMPOSITE_VIEW_INDEX", "Index of a composite view to show when Composite Debug View is enabled. The index must be a a valid value from CompositeDebugView enumeration. Value of 0 disables Composite Debug View.");
    
      // Note: Used for preserving the debug view state only for ImGui purposes. Not to be used for anything else
      // and should not ever be set to the disabled debug view index.
      CompositeDebugView lastCompositeViewIdx = CompositeDebugView::FinalRenderWithMaterialProperties;
      Resources::Resource compositeView;
    } m_composite;

    // Common Display
    bool m_enableInfNanView = true;
    int m_colorCodeRadius = 4;
    RTX_OPTION("rtx.debugView", bool, showFirstGBufferHit, false, "Show information of the first hit surface.\n");
    RTX_OPTION("rtx.debugView", bool, enableInputQuantization, false,
               "Enables uniform-step input quantization on debug view input buffers.\n"
               "This is mostly useful for when debugging artifacts relating to quantization that may not be visible in a buffer due to higher precision formats in use.\n"
               "For example, the final output from tonemapping is a floating point texture in the debug view but will be quantized to 8 bit on some monitors. Using this option the quantization which will be applied to the output can be visualized in advance.");
    // Note: Default to standard 8 bit unorm encoding step size ([0, 1] range normalized on a step size of 1/255).
    RTX_OPTION("rtx.debugView", float, inverseQuantizationStepSize, 255.0f,
               "The inverse of the uniform step size to quantize the debug view input to when Input Quantization is enabled.\n"
               "A value of 255 indicates that the input will be quantized to steps of 1/255, the same as the step size used when quantizing the range 0-1 to an 8 bit representation.");

    // Standard Display
    // Note: Previously rtx.debugView.enablePseudoColor and RTX_DEBUG_VIEW_ENABLE_PSEUDO_COLOR, alias this in the future if needed,
    // but likely nothing relied on it due to being a debugging option.
    RTX_OPTION_ENV("rtx.debugView", PseudoColorMode, pseudoColorMode, PseudoColorMode::Disabled, "RTX_DEBUG_VIEW_PSEUDO_COLOR_MODE",
                   "Selects a mode for mapping debug value inputs to a scalar to be visualized using a colormap spectrum. Only takes effect when rtx.debugView.displayType is set to \"Standard\".\n"
                   "Supported modes are: 0 = Disabled, 1 = Luminance, 2 = Red, 3 = Green, 4 = Blue, 5 = Alpha.\n"
                   "Useful for visualizing a value's range with greater precision than a simple monochromatic color spectrum (due to interpolating across an entire spectrum of color with a roughly uniform progression).");
    RTX_OPTION_ENV("rtx.debugView", bool, enableGammaCorrection, false, "RTX_DEBUG_VIEW_ENABLE_GAMMA_CORRECTION", "Enables gamma correction of a debug view value.");
    bool m_enableAlphaChannel = false;
    float m_scale = 1.f;

    public: static void maxValueOnChange();
    RTX_OPTION_ARGS("rtx.debugView", float, minValue, 0.f,
      "The minimum debug view input value to map to 0 in the output when the standard debug display is in use. Values below this value in the input will be clamped to 0 in the output.",
      args.environment = "DXVK_RTX_DEBUG_VIEW_MIN_VALUE",
      args.onChangeCallback = &maxValueOnChange);
      
    public: static void minValueOnChange();
    RTX_OPTION_ARGS("rtx.debugView", float, maxValue, 1.f,
      "The maximum debug view input value to map to 1 in the output when the standard debug display is in use. Values above this value in the input will be clamped to 1 in the output.",
      args.environment = "DXVK_RTX_DEBUG_VIEW_MAX_VALUE",
      args.onChangeCallback = &minValueOnChange);


    // EV100 Display
    RTX_OPTION_ENV("rtx.debugView", int32_t, evMinValue, -4, "DXVK_RTX_DEBUG_VIEW_EV_MIN_VALUE", "The minimum EV100 debug view input value to map to the bottom of the visualization range when EV100 debug display is in use. Values below this value in the input will be clamped to the bottom of the range.");
    RTX_OPTION_ENV("rtx.debugView", int32_t, evMaxValue, 4, "DXVK_RTX_DEBUG_VIEW_EV_MAX_VALUE", "The maximum EV100 debug view input value to map to the top of the visualization range when EV100 debug display is in use. Values above this value in the input will be clamped to the top of the range.")

    struct Accumulation {
      RTX_OPTION_ENV("rtx.debugView.accumulation", bool, enable, false, "RTX_DEBUG_VIEW_ACCUMULATION_ENABLE",
                     "Enables accumulation of debug ouptput's result to emulate multiple samples per pixel or over time.");
      RTX_OPTION_ARGS("rtx.debugView.accumulation", uint32_t, numberOfFramesToAccumulate, 1024,
                 "Number of frames to accumulate debug view's result over.\n"
                 "This can be used for generating reference images smoothed over time.\n"
                 "By default the accumulation stops once the limit is reached.\n"
                 "When desired, continous accumulation can be enabled via enableContinuousAccumulation.",
                args.minValue = 1);
      RTX_OPTION_ENV("rtx.debugView.accumulation", AccumulationBlendMode, blendMode, AccumulationBlendMode::Average, "RTX_DEBUG_VIEW_ACCUMULATION_BLEND_MODE",
                      "The blend mode to use for accumulating debug view output.\n"
                      "Supported modes are: 0 = Average, 1 = Min, 2 = Max.\n"
                      "Average is the default mode and is the most common mode to use for accumulation.\n"
                      "Min and Max are useful for visualizing the minimum or maximum value of a debug view output over time.");
      RTX_OPTION_ENV("rtx.debugView.accumulation", bool, resetOnCameraTransformChange, true, "RTX_DEBUG_VIEW_ACCUMULATION_RESET_ON_CAMERA_TRANFORM_CHANGE",
                      "Resets the accumulated debug view output when the camera transform changes.");
    } accumulation;

    RTX_OPTION_ENV("rtx.debugView", bool, replaceCompositeOutput, false, "RTX_DEBUG_VIEW_REPLACE_COMPOSITE_OUTPUT",
               "Replaces composite output with debug view output that is generated right after composition pass.\n"
               "Allows for debug view output to get the post composition pipeline applied to it, such as upscaling and postprocessing actions).\n"
               "Note any Debug Views having data set post Composite pass require this setting to be disabled to work.\n"
               "When disabled Debug View output is generated close to the end of RTX pipeline (after postprocessing and upscaling).");
    RTX_OPTION_ENV("rtx.debugView", bool, overlayOnTopOfRenderOutput, false, "RTX_DEBUG_VIEW_OVERLAY_ON_TOP_OF_RENDER_OUTPUT",
               "Enables the debug view to be overlayed directly on top of the rendered output.\n"
               "Primarily used to overlay NaN/Inf debug view on top since it overlays pixels of 0 value on top of rendered output.");

    // HDR Waveform Display
    bool m_enableLuminanceMode = false;
    int32_t m_log10MinValue = -3;
    int32_t m_log10MaxValue = 2;
    // Note: Resolution scale will always be >=2.
    uint32_t m_hdrWaveformResolutionScaleFactor = 2;
    uvec2 m_hdrWaveformPosition = uvec2{ 25, 25 };
    float m_hdrWaveformHistogramNormalizationScale = 8.0f;

    vec4 m_debugKnob = vec4{ 0.f, 0.f, 0.f, 0.f };

    bool m_cacheCurrentImage = false;
    bool m_showCachedImage = false;
    bool m_showOutputStatistics = false;
    bool m_printOutputStatistics = false;

    RtxAccumulation m_accumulation;

    Resources::Resource m_cachedImage;
    Resources::Resource m_debugView;

    // Non-debug view passes directly write to m_debugView, hence we need a separate resource to retain the accumulated result.
    // In addition this lets postprocess pass do gather on this resource while modifying the original m_debugView.
    Resources::Resource m_accumulatedFrameDebugView;

    // Statistics
    DebugViewOutputStatisticsMode m_outputStatisticsMode = DebugViewOutputStatisticsMode::Mean;
    Rc<DxvkBuffer> m_statisticsBuffer;
    vec4 m_outputStatistics;

    Resources::Resource m_hdrWaveformRed;
    Resources::Resource m_hdrWaveformGreen;
    Resources::Resource m_hdrWaveformBlue;
    Resources::Resource m_instrumentation;

  public:
    ObjectPicking ObjectPicking{};
    Highlighting Highlighting{};
  };
} // namespace dxvk
