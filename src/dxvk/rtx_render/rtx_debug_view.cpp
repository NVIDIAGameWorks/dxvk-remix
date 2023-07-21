/*
* Copyright (c) 2022-2023, NVIDIA CORPORATION. All rights reserved.
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
#include "rtx_debug_view.h"
#include "dxvk_device.h"
#include "dxvk_context.h"
#include "dxvk_objects.h"
#include "rtx/utility/shader_types.h"
#include "rtx/external/turbo_colormap.h"
#include "rtx/utility/debug_view_indices.h"
#include "rtx/pass/debug_view/debug_view_binding_indices.h"
#include "rtx/pass/debug_view/debug_view_waveform_render_binding_indices.h"
#include "rtx/pass/debug_view/debug_view_args.h"
#include "rtx_render/rtx_shader_manager.h"
#include "rtx_imgui.h"
#include "dxvk_scoped_annotation.h"
#include "rtx_context.h"

#include <rtx_shaders/debug_view.h>
#include <rtx_shaders/debug_view_waveform_render.h>

#include "rtx_options.h"

namespace dxvk {
  static const bool s_disableAnimation = (env::getEnvVar("DXVK_DEBUG_VIEW_DISABLE_ANIMATION") == "1");

  static const auto colormap0 = turboColormap(0.0f);
  static const auto colormap25= turboColormap(0.25f);
  static const auto colormap50 = turboColormap(0.5f);
  static const auto colormap75 = turboColormap(0.75f);
  static const auto colormap100 = turboColormap(1.0f);

  ImGui::ComboWithKey<uint32_t> debugViewCombo = ImGui::ComboWithKey<uint32_t>(
    "Debug View",
    ImGui::ComboWithKey<uint32_t>::ComboEntries{ {
        {DEBUG_VIEW_PRIMITIVE_INDEX, "Primitive Index"},
        {DEBUG_VIEW_GEOMETRY_HASH, "Geometry Hash"},
        {DEBUG_VIEW_CUSTOM_INDEX, "Custom Index"},
        {DEBUG_VIEW_BARYCENTRICS, "Barycentric Coordinates"},
        {DEBUG_VIEW_IS_FRONT_HIT, "Is Front Hit"},
        {DEBUG_VIEW_IS_STATIC, "Is Static"},
        {DEBUG_VIEW_IS_OPAQUE, "Is Opaque"},
        {DEBUG_VIEW_IS_DIRECTION_ALTERED, "Is Direction Altered"},
        {DEBUG_VIEW_IS_EMISSIVE_BLEND, "Is Emissive Blend"},
        {DEBUG_VIEW_VIEW_DIRECTION, "View Direction"},
        {DEBUG_VIEW_CONE_RADIUS, "Cone Radius"},

        {DEBUG_VIEW_POSITION, "Position"},
        {DEBUG_VIEW_TEXCOORDS, "Texture Coordinates"},
        {DEBUG_VIEW_TEXCOORD_GENERATION_MODE, "Texture Coordinates Generation Mode"},
        {DEBUG_VIEW_VIRTUAL_MOTION_VECTOR, "Virtual Motion Vector"},
        {DEBUG_VIEW_SCREEN_SPACE_MOTION_VECTOR, "Screen-Space Motion Vector"},
        {DEBUG_VIEW_TRIANGLE_NORMAL, "Triangle Normal"},
        {DEBUG_VIEW_INTERPOLATED_NORMAL, "Interpolated Normal"},
        {DEBUG_VIEW_INTERPOLATED_TANGENT, "Interpolated Tangent"},
        {DEBUG_VIEW_INTERPOLATED_BITANGENT, "Interpolated Bitangent"},
        {DEBUG_VIEW_SHADING_NORMAL, "Shading Normal"},
        {DEBUG_VIEW_VIRTUAL_SHADING_NORMAL, "Virtual Shading Normal"},
        {DEBUG_VIEW_VERTEX_COLOR, "Vertex Color"},
        {DEBUG_VIEW_PORTAL_SPACE, "Portal Space"},

        {DEBUG_VIEW_MATERIAL_TYPE, "Material Type"},
        {DEBUG_VIEW_ALBEDO, "Diffuse Albedo"},
        {DEBUG_VIEW_RAW_ALBEDO, "Diffuse Raw Albedo (RGS only)"},        
        {DEBUG_VIEW_BASE_REFLECTIVITY, "Base Reflectivity"},
        {DEBUG_VIEW_ROUGHNESS, "Isotropic Roughness"},
        {DEBUG_VIEW_PERCEPTUAL_ROUGHNESS, "Perceptual Roughness"},
        {DEBUG_VIEW_ANISOTROPY, "Anisotropy"},
        {DEBUG_VIEW_ANISOTROPIC_ROUGHNESS, "Anisotropic Roughness"},
        {DEBUG_VIEW_OPACITY, "Opacity"},
        {DEBUG_VIEW_EMISSIVE_RADIANCE, "Emissive Radiance"},
        {DEBUG_VIEW_EMISSIVE_TRIANGLE_INTENSITY, "Emissive Triangle Intensity"},
        {DEBUG_VIEW_SURFACE_AREA, "Surface Area"},
        {DEBUG_VIEW_THIN_FILM_THICKNESS, "Thin Film Thickness"},

        {DEBUG_VIEW_IS_BAKED_TERRAIN, "Terrain: Is Baked Terrain (RGS only)"},
        {DEBUG_VIEW_TERRAIN_MAP, "Terrain: Cascade Map"},
        {DEBUG_VIEW_TERRAIN_MAP_OPACITY, "Terrain: Cascade Map Opacity"},
        {DEBUG_VIEW_CASCADE_LEVEL, "Terrain: Cascade Level (RGS only)"},

        {DEBUG_VIEW_VIRTUAL_HIT_DISTANCE, "Virtual Hit Distance"},
        {DEBUG_VIEW_PRIMARY_DEPTH, "Primary Depth" },

        {DEBUG_VIEW_BLUE_NOISE, "Blue Noise"},
        {DEBUG_VIEW_PIXEL_CHECKERBOARD, "Pixel Checkerboard"},
        {DEBUG_VIEW_VOLUME_RADIANCE_DEPTH_LAYERS, "Volume Radiance Depth Layers"},
        {DEBUG_VIEW_SURFACE_VOLUME_RADIANCE, "Surface Volume Radiance"},

        {DEBUG_VIEW_COMPOSITE_OUTPUT, "Composite Output"},

        {DEBUG_VIEW_LOCAL_TONEMAPPER_LUMINANCE_OUTPUT, "Local Tonemapper Luminance Output"},
        {DEBUG_VIEW_LOCAL_TONEMAPPER_EXPOSURE_OUTPUT, "Local Tonemapper Blend Weight"},
        {DEBUG_VIEW_LOCAL_TONEMAPPER_BLEND_OUTPUT, "Local Tonemapper Assembled Exposure"},
        {DEBUG_VIEW_LOCAL_TONEMAPPER_FINAL_COMBINE_OUTPUT, "Local Tonemapper Final Multiplier"},

        {DEBUG_VIEW_POST_TONEMAP_OUTPUT, "Final Output"},
        {DEBUG_VIEW_PRE_TONEMAP_OUTPUT, "Final Output (Pre Tonemap)"},
        {DEBUG_VIEW_EXPOSURE_HISTOGRAM, "Exposure Histogram"},

        {DEBUG_VIEW_VIEW_MODEL, "View Model: Final Output"},
        {DEBUG_VIEW_RESTIR_GI_INITIAL_SAMPLE, "ReSTIR GI Initial Sample"},
        {DEBUG_VIEW_RESTIR_GI_TEMPORAL_REUSE, "ReSTIR GI Temporal Reprojection"},
        {DEBUG_VIEW_RESTIR_GI_SPATIAL_REUSE, "ReSTIR GI Spatial Reuse"},
        {DEBUG_VIEW_RESTIR_GI_FINAL_SHADING, "ReSTIR GI Final Shading MIS Weight"},
        {DEBUG_VIEW_RESTIR_GI_VIRTUAL_HIT_T, "ReSTIR GI Virtual Hit Distance"},

        {DEBUG_VIEW_NEE_CACHE_CANDIDATE_ID, "NEE Cache Candidate"},
        {DEBUG_VIEW_NEE_CACHE_HISTOGRAM, "NEE Cache Histogram"},
        {DEBUG_VIEW_NEE_CACHE_SAMPLE_RADIANCE, "NEE Cache Sample Radiance"},
        {DEBUG_VIEW_NEE_CACHE_TASK, "NEE Cache Task"},

        {DEBUG_VIEW_RTXDI_GRADIENTS, "RTXDI Gradients"},
        {DEBUG_VIEW_RTXDI_CONFIDENCE, "RTXDI Confidence"},

        {DEBUG_VIEW_STOCHASTIC_ALPHA_BLEND_COLOR, "Stochastic Alpha Blend Color"},
        {DEBUG_VIEW_STOCHASTIC_ALPHA_BLEND_NORMAL, "Stochastic Alpha Blend Normal"},
        {DEBUG_VIEW_STOCHASTIC_ALPHA_BLEND_GEOMETRY_HASH, "Stochastic Alpha Blend Geometry Hash"},
        {DEBUG_VIEW_STOCHASTIC_ALPHA_BLEND_BACKGROUND_TRANSPARENCY, "Stochastic Alpha Blend Background Transparency"},

        {DEBUG_VIEW_GEOMETRY_FLAGS_FIRST_SAMPLED_LOBE_IS_SPECULAR, "Geometry Flags: First Sampled Lobe Is Specular"},
        {DEBUG_VIEW_INTEGRATE_INDIRECT_FIRST_RAY_THROUGHPUT, "Indirect First Ray Throughput"},
        {DEBUG_VIEW_INTEGRATE_INDIRECT_FIRST_SAMPLED_LOBE_PDF, "Indirect First Sampled Lobe Pdf"},
        {DEBUG_VIEW_INTEGRATE_INDIRECT_FIRST_SAMPLED_SOLID_ANGLE_PDF, "Indirect First Sampled Solid Angle Pdf"},

        {DEBUG_VIEW_PRIMARY_RAY_INTERACTIONS, "Primary Ray Interactions (RGS TR only)"},
        {DEBUG_VIEW_SECONDARY_RAY_INTERACTIONS, "Secondary Ray Interactions (RGS TR only)"},
        {DEBUG_VIEW_PRIMARY_RAY_BOUNCES, "Primary Ray Bounces"},
        {DEBUG_VIEW_SECONDARY_RAY_BOUNCES, "Secondary Ray Bounces"},
        {DEBUG_VIEW_PRIMARY_UNORDERED_INTERACTIONS, "Primary Unordered Ray Interactions"},
        {DEBUG_VIEW_SECONDARY_UNORDERED_INTERACTIONS, "Secondary Unordered Ray Interactions"},

        {DEBUG_VIEW_PRIMARY_RAY_AND_UNORDERED_INTERACTIONS, "Primary Ray Interactions (+Unordered) (RGS TR only)"},
        {DEBUG_VIEW_SECONDARY_RAY_AND_UNORDERED_INTERACTIONS, "Secondary Ray Interactions (+Unordered) (RGS TR only)"},

        {DEBUG_VIEW_PSR_PRIMARY_SECONDARY_SURFACE_MASK, "PSR Primary Secondary Surface Mask"},
        {DEBUG_VIEW_PSR_SELECTED_INTEGRATION_SURFACE_PDF, "PSR Selected Integration Surface PDF"},
        
        {DEBUG_VIEW_PRIMARY_USE_ALTERNATE_DISOCCLUSION_THRESHOLD, "Primary Use Alternate Disocclusion Threshold"},

        {DEBUG_VIEW_PRIMARY_SPECULAR_ALBEDO,               "Primary Specular Albedo"},
        {DEBUG_VIEW_SECONDARY_SPECULAR_ALBEDO,               "Secondary Specular Albedo"},

        {DEBUG_VIEW_NOISY_PRIMARY_DIRECT_DIFFUSE_RADIANCE,               "Primary Direct Diffuse: Noisy Color"},
        {DEBUG_VIEW_NOISY_PRIMARY_DIRECT_DIFFUSE_HIT_T,                  "Primary Direct Diffuse: Noisy HitT"},
        {DEBUG_VIEW_DEMODULATED_NOISY_PRIMARY_DIRECT_DIFFUSE_RADIANCE,   "Primary Direct Diffuse: Demodulated Noisy Color"},
        {DEBUG_VIEW_DENOISED_PRIMARY_DIRECT_DIFFUSE_RADIANCE,            "Primary Direct Diffuse: Denoised Color"},
        {DEBUG_VIEW_DENOISED_PRIMARY_DIRECT_DIFFUSE_HIT_T,               "Primary Direct Diffuse: Denoised HitT (ReBLUR) | Variance (ReLAX)"},
        {DEBUG_VIEW_NRD_NORMALIZED_PRIMARY_DIRECT_DIFFUSE_HIT_T,         "Primary Direct Diffuse: NRD Normalized  HitT"},

        {DEBUG_VIEW_NOISY_PRIMARY_DIRECT_SPECULAR_RADIANCE,              "Primary Direct Specular: Noisy Color"},
        {DEBUG_VIEW_NOISY_PRIMARY_DIRECT_SPECULAR_HIT_T,                 "Primary Direct Specular: Noisy HitT"},
        {DEBUG_VIEW_DEMODULATED_NOISY_PRIMARY_DIRECT_SPECULAR_RADIANCE,  "Primary Direct Specular: Demodulated Noisy Color"},
        {DEBUG_VIEW_DENOISED_PRIMARY_DIRECT_SPECULAR_RADIANCE,           "Primary Direct Specular: Denoised Color"},
        {DEBUG_VIEW_DENOISED_PRIMARY_DIRECT_SPECULAR_HIT_T,              "Primary Direct Specular: Denoised HitT (ReBLUR) | Variance (ReLAX)"},
        {DEBUG_VIEW_NRD_NORMALIZED_PRIMARY_DIRECT_SPECULAR_HIT_T,        "Primary Direct Specular: NRD Normalized  HitT"},

        {DEBUG_VIEW_NOISY_PRIMARY_INDIRECT_DIFFUSE_RADIANCE,               "Primary Indirect Diffuse: Noisy Color"},
        {DEBUG_VIEW_NOISY_PRIMARY_INDIRECT_DIFFUSE_HIT_T,                  "Primary Indirect Diffuse: Noisy HitT"},
        {DEBUG_VIEW_DEMODULATED_NOISY_PRIMARY_INDIRECT_DIFFUSE_RADIANCE,   "Primary Indirect Diffuse: Demodulated Noisy Color"},
        {DEBUG_VIEW_DENOISED_PRIMARY_INDIRECT_DIFFUSE_RADIANCE,            "Primary Indirect Diffuse: Denoised Color"},
        {DEBUG_VIEW_DENOISED_PRIMARY_INDIRECT_DIFFUSE_HIT_T,               "Primary Indirect Diffuse: Denoised HitT (ReBLUR) | Variance (ReLAX)"},
        {DEBUG_VIEW_NRD_NORMALIZED_PRIMARY_INDIRECT_DIFFUSE_HIT_T,         "Primary Indirect Diffuse: NRD Normalized  HitT"},

        {DEBUG_VIEW_NOISY_PRIMARY_INDIRECT_SPECULAR_RADIANCE,              "Primary Indirect Specular: Noisy Color"},
        {DEBUG_VIEW_NOISY_PRIMARY_INDIRECT_SPECULAR_HIT_T,                 "Primary Indirect Specular: Noisy HitT"},
        {DEBUG_VIEW_DEMODULATED_NOISY_PRIMARY_INDIRECT_SPECULAR_RADIANCE,  "Primary Indirect Specular: Demodulated Noisy Color"},
        {DEBUG_VIEW_DENOISED_PRIMARY_INDIRECT_SPECULAR_RADIANCE,           "Primary Indirect Specular: Denoised Color"},
        {DEBUG_VIEW_DENOISED_PRIMARY_INDIRECT_SPECULAR_HIT_T,              "Primary Indirect Specular: Denoised HitT (ReBLUR) | Variance (ReLAX)"},
        {DEBUG_VIEW_NRD_NORMALIZED_PRIMARY_INDIRECT_SPECULAR_HIT_T,        "Primary Indirect Specular: NRD Normalized  HitT"},

        {DEBUG_VIEW_NOISY_SECONDARY_DIRECT_DIFFUSE_RADIANCE,               "Secondary Direct Diffuse: Noisy Color"},
        {DEBUG_VIEW_NOISY_SECONDARY_INDIRECT_DIFFUSE_RADIANCE,             "Secondary Indirect Diffuse: Noisy Color"},
        {DEBUG_VIEW_NOISY_SECONDARY_COMBINED_DIFFUSE_RADIANCE,             "Secondary Combined Diffuse: Noisy Color"},
        {DEBUG_VIEW_NOISY_DEMODULATED_SECONDARY_COMBINED_DIFFUSE_RADIANCE, "Secondary Combined Diffuse: Demodulated Noisy Color"},
        {DEBUG_VIEW_DENOISED_SECONDARY_COMBINED_DIFFUSE_RADIANCE,          "Secondary Combined Diffuse: Denoised Color"},
        {DEBUG_VIEW_NOISY_SECONDARY_DIRECT_SPECULAR_RADIANCE,              "Secondary Direct Specular: Noisy Color"},
        {DEBUG_VIEW_NOISY_SECONDARY_INDIRECT_SPECULAR_RADIANCE,            "Secondary Indirect Specular: Noisy Color"},
        {DEBUG_VIEW_NOISY_SECONDARY_COMBINED_SPECULAR_RADIANCE,            "Secondary Combined Specular: Noisy Color"},
        {DEBUG_VIEW_NOISY_DEMODULATED_SECONDARY_COMBINED_SPECULAR_RADIANCE,"Secondary Combined Specular: Demodulated Noisy Color"},
        {DEBUG_VIEW_DENOISED_SECONDARY_COMBINED_SPECULAR_RADIANCE,         "Secondary Combined Specular: Denoised Color"},

        {DEBUG_VIEW_INSTRUMENTATION_THREAD_DIVERGENCE,                     "Thread Divergence(Debug Knob.x)"},
        {DEBUG_VIEW_NAN,                                                   "Inf/NaN Check"},
        {DEBUG_SURFACE_LOBE_CONSISTENCY,                                   "Surface/Lobe Consistency Check"},
        {DEBUG_VIEW_SCROLLING_LINE,                                        "Scrolling Line"},
    } });

  ImGui::ComboWithKey<DebugViewDisplayType> displayTypeCombo = ImGui::ComboWithKey<DebugViewDisplayType>(
  "Display Type",
  ImGui::ComboWithKey<DebugViewDisplayType>::ComboEntries{ {
      {DebugViewDisplayType::Standard, "Standard"},
      {DebugViewDisplayType::BGRExclusiveColor, "BGR Exclusive Color"},
      {DebugViewDisplayType::EV100, "Exposure Value (EV100)"},
      {DebugViewDisplayType::HDRWaveform, "HDR Waveform"},
  } });

  ImGui::ComboWithKey<DebugViewSamplerType> samplerTypeCombo = ImGui::ComboWithKey<DebugViewSamplerType>(
  "Sampler Type",
  ImGui::ComboWithKey<DebugViewSamplerType>::ComboEntries { {
      {DebugViewSamplerType::Nearest, "Nearest"},
      {DebugViewSamplerType::NormalizedNearest, "Normalized Nearest"},
      {DebugViewSamplerType::NormalizedLinear, "Normalized Linear"},
  } });

  // Defined within an unnamed namespace to ensure unique definition across binary
  namespace {
    class DebugViewShader : public ManagedShader {
      SHADER_SOURCE(DebugViewShader, VK_SHADER_STAGE_COMPUTE_BIT, debug_view)

      BEGIN_PARAMETER()
        RW_TEXTURE2D(DEBUG_VIEW_BINDING_INPUT_OUTPUT)
        TEXTURE2D(DEBUG_VIEW_BINDING_DENOISED_PRIMARY_DIRECT_DIFFUSE_RADIANCE_HIT_T_INPUT)
        TEXTURE2D(DEBUG_VIEW_BINDING_DENOISED_PRIMARY_DIRECT_SPECULAR_RADIANCE_HIT_T_INPUT)
        TEXTURE2D(DEBUG_VIEW_BINDING_DENOISED_SECONDARY_COMBINED_DIFFUSE_RADIANCE_HIT_T_INPUT)
        TEXTURE2D(DEBUG_VIEW_BINDING_DENOISED_SECONDARY_COMBINED_SPECULAR_RADIANCE_HIT_T_INPUT)
        TEXTURE2D(DEBUG_VIEW_BINDING_SHARED_FLAGS_INPUT)
        TEXTURE2D(DEBUG_VIEW_BINDING_PRIMARY_LINEAR_VIEW_Z_INPUT)
        TEXTURE2D(DEBUG_VIEW_BINDING_PRIMARY_VIRTUAL_WORLD_SHADING_NORMAL_PERCEPTUAL_ROUGHNESS_INPUT)
        TEXTURE2D(DEBUG_VIEW_BINDING_PRIMARY_VIRTUAL_MOTION_VECTOR_INPUT)
        TEXTURE2D(DEBUG_VIEW_BINDING_PRIMARY_SCREEN_SPACE_MOTION_VECTOR_INPUT)
        TEXTURE2D(DEBUG_VIEW_BINDING_RTXDI_CONFIDENCE_INPUT)
        TEXTURE2D(DEBUG_VIEW_BINDING_FINAL_SHADING_INPUT)
        TEXTURE2D(DEBUG_VIEW_BINDING_INSTRUMENTATION_INPUT)
        TEXTURE2D(DEBUG_VIEW_BINDING_TERRAIN_INPUT)
        TEXTURE2D(DEBUG_VIEW_BINDING_COMPOSITE_OUTPUT_INPUT)
        
        RW_TEXTURE2D(DEBUG_VIEW_BINDING_HDR_WAVEFORM_RED_INPUT_OUTPUT)
        RW_TEXTURE2D(DEBUG_VIEW_BINDING_HDR_WAVEFORM_GREEN_INPUT_OUTPUT)
        RW_TEXTURE2D(DEBUG_VIEW_BINDING_HDR_WAVEFORM_BLUE_INPUT_OUTPUT)

        SAMPLER(DEBUG_VIEW_BINDING_NEAREST_SAMPLER)
        SAMPLER(DEBUG_VIEW_BINDING_LINEAR_SAMPLER)

        CONSTANT_BUFFER(DEBUG_VIEW_BINDING_CONSTANTS_INPUT)
      END_PARAMETER()
    };

    PREWARM_SHADER_PIPELINE(DebugViewShader);

    class DebugViewWaveformRenderShader : public ManagedShader {
      SHADER_SOURCE(DebugViewWaveformRenderShader, VK_SHADER_STAGE_COMPUTE_BIT, debug_view_waveform_render)

      BEGIN_PARAMETER()
        TEXTURE2D(DEBUG_VIEW_WAVEFORM_RENDER_BINDING_HDR_WAVEFORM_RED_INPUT)
        TEXTURE2D(DEBUG_VIEW_WAVEFORM_RENDER_BINDING_HDR_WAVEFORM_GREEN_INPUT)
        TEXTURE2D(DEBUG_VIEW_WAVEFORM_RENDER_BINDING_HDR_WAVEFORM_BLUE_INPUT)
        RW_TEXTURE2D(DEBUG_VIEW_WAVEFORM_RENDER_BINDING_INPUT_OUTPUT)
        CONSTANT_BUFFER(DEBUG_VIEW_WAVEFORM_RENDER_BINDING_CONSTANTS_INPUT)
      END_PARAMETER()
    };

    PREWARM_SHADER_PIPELINE(DebugViewWaveformRenderShader);
  }

  DebugView::DebugView(dxvk::DxvkDevice* device)
    : RtxPass(device)
    , m_vkd(device->vkd())
    , m_device(device)
    , m_lastDebugViewIdx(DEBUG_VIEW_PRIMITIVE_INDEX)
    , m_startTime(std::chrono::system_clock::now()){
    initSettings(device->instance()->config());
  }

  void DebugView::initSettings(const dxvk::Config& config) {
    // Note: Set the last debug view index only if the debug view index was specified to be enabled to something (not disabled).
    if (debugViewIdx() != DEBUG_VIEW_DISABLED) {
      m_lastDebugViewIdx = debugViewIdx();
    }

    displayTypeRef() = static_cast<DebugViewDisplayType>(std::min(static_cast<uint32_t>(displayType()), static_cast<uint32_t>(DebugViewDisplayType::Count) - 1));
  }

  void DebugView::showImguiSettings()
  {
    const ImGuiSliderFlags sliderFlags = ImGuiSliderFlags_AlwaysClamp;
    const ImGuiTreeNodeFlags collapsingHeaderFlags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_CollapsingHeader;

    // Note: Ensure the enable checkbox state matches what the debug index was set to externally (for example when loaded from settings).
    bool enableDebugView = debugViewIdx() != DEBUG_VIEW_DISABLED;

    // Note: Ensure the last debug view index wasn't incorrectly set to the disabled index somehow.
    assert(m_lastDebugViewIdx != DEBUG_VIEW_DISABLED);

    if (ImGui::Button("Cache Current Image"))
      m_cacheCurrentImage = true;

    ImGui::Checkbox("Show Cached Image", &m_showCachedImage);

    ImGui::Checkbox("Enable Debug View", &enableDebugView);

    if (enableDebugView) {
      // Note: Write to the last debug view index to prevent from being overridden when disabled and re-enabled.
      debugViewCombo.getKey(&m_lastDebugViewIdx);

      debugViewIdxRef() = m_lastDebugViewIdx;
    } else {
      debugViewIdxRef() = DEBUG_VIEW_DISABLED;
    }

    ImGui::DragFloat4("Debug Knob", (float*)&m_debugKnob, 0.1f, -1000.f, 1000.f, "%.3f", sliderFlags);

    displayTypeCombo.getKey(&displayTypeObject());
    samplerTypeCombo.getKey(&samplerTypeObject());

    if (ImGui::CollapsingHeader("Display Settings", collapsingHeaderFlags)) {
      ImGui::Indent();

      ImGui::Text("Common:");

      ImGui::Checkbox("Color NaN Red/Inf Blue", &m_enableInfNanView);
      ImGui::InputInt("Color Code Pixel Radius", &m_colorCodeRadius);

      if (displayType() == DebugViewDisplayType::Standard) {
        ImGui::Text("Standard:");

        ImGui::Checkbox("Alpha Channel", &m_enableAlphaChannel);
        ImGui::Checkbox("Pseudo Color Mode", &enablePseudoColorObject());

        ImGui::DragFloat("Scale", &m_scale, 0.01f, 0.0f, FLT_MAX, "%.3f", sliderFlags);
        ImGui::InputFloat("Min Value", &minValueObject(), std::max(0.01f, 0.02f * abs(minValue())), std::max(0.1f, 0.1f * abs(minValue())));
        ImGui::InputFloat("Max Value", &maxValueObject(), std::max(0.01f, 0.02f * abs(maxValue())), std::max(0.1f, 0.1f * abs(maxValue())));
        maxValueRef() = std::max(1.00001f * minValue(), maxValue());

        // Color legend
        if (enablePseudoColor()) {
          ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4 { 0.25f, 0.25f, 0.25f, 1.0f });
          ImGui::BeginChildFrame(ImGui::GetID("Pseudocolor legend"), ImVec2(500, 20), ImGuiWindowFlags_NoScrollbar);
          ImGui::TextColored(ImVec4 { colormap0.x, colormap0.y, colormap0.z, 1.0f }, "%g", minValue());
          ImGui::SameLine();
          ImGui::TextColored(ImVec4 { colormap25.x, colormap25.y, colormap25.z, 1.0f }, "%g", lerp(static_cast<float>(minValue()), static_cast<float>(maxValue()), 0.25));
          ImGui::SameLine();
          ImGui::TextColored(ImVec4 { colormap50.x, colormap50.y, colormap50.z, 1.0f }, "%g", lerp(static_cast<float>(minValue()), static_cast<float>(maxValue()), 0.5));
          ImGui::SameLine();
          ImGui::TextColored(ImVec4 { colormap75.x, colormap75.y, colormap75.z, 1.0f }, "%g", lerp(static_cast<float>(minValue()), static_cast<float>(maxValue()), 0.75));
          ImGui::SameLine();
          ImGui::TextColored(ImVec4 { colormap100.x, colormap100.y, colormap100.z, 1.0f }, "%g", static_cast<float>(maxValue()));
          ImGui::EndChildFrame();
          ImGui::PopStyleColor();
        }
      } else if (displayType() == DebugViewDisplayType::BGRExclusiveColor) {
        ImGui::Text("BGR Exclusive Color:");

        ImGui::InputFloat("Max Value", &maxValueObject(), std::max(0.01f, 0.02f * abs(maxValue())), std::max(0.1f, 0.1f * abs(maxValue())));
      } else if (displayType() == DebugViewDisplayType::EV100) {
        ImGui::Text("Exposure Value (EV100):");

        ImGui::InputInt("Min Value (EV100)", &evMinValueObject());
        ImGui::InputInt("Max Value (EV100)", &evMaxValueObject());

        evMaxValueRef() = std::max(evMaxValue(), evMinValue());

        // Color legend
        {
          ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4 { 0.25f, 0.25f, 0.25f, 1.0f });
          ImGui::BeginChildFrame(ImGui::GetID("Exposure value legend"), ImVec2(500, 45), ImGuiWindowFlags_NoScrollbar);

          // Note: Somewhat lazy visual indicator to show which colors represent which EV values. A proper labeled gradient would be better,
          // or an actual spot meter that reads back from the GPU, but for now this is fine (should match the colors the GPU produces).
          ImGui::TextColored(ImVec4 { colormap0.x, colormap0.y, colormap0.z, 1.0f }, "%+d.0 EV", evMinValue());
          ImGui::SameLine();
          ImGui::TextColored(ImVec4 { colormap25.x, colormap25.y, colormap25.z, 1.0f }, "%+.2f EV", lerp(static_cast<float>(evMinValue()), static_cast<float>(evMaxValue()), 0.25));
          ImGui::SameLine();
          ImGui::TextColored(ImVec4 { colormap50.x, colormap50.y, colormap50.z, 1.0f }, "%+.1f EV", lerp(static_cast<float>(evMinValue()), static_cast<float>(evMaxValue()), 0.5));
          ImGui::SameLine();
          ImGui::TextColored(ImVec4 { colormap75.x, colormap75.y, colormap75.z, 1.0f }, "%+.2f EV", lerp(static_cast<float>(evMinValue()), static_cast<float>(evMaxValue()), 0.75));
          ImGui::SameLine();
          ImGui::TextColored(ImVec4 { colormap100.x, colormap100.y, colormap100.z, 1.0f }, "%+d.0 EV", evMaxValue());

          auto evToLuminanceValue = [&](float evValue) {
            // Given evValue = log2(luminance * type(100.0f / 12.5f))
            return static_cast<float>(pow(2, evValue) / (100 / 12.5));
          };
          ImGui::TextColored(ImVec4 { colormap0.x, colormap0.y, colormap0.z, 1.0f }, "%g", evToLuminanceValue(static_cast<float>(evMinValue())));
          ImGui::SameLine();
          ImGui::TextColored(ImVec4 { colormap25.x, colormap25.y, colormap25.z, 1.0f }, "%g", evToLuminanceValue(lerp(static_cast<float>(evMinValue()), static_cast<float>(evMaxValue()), 0.25)));
          ImGui::SameLine();
          ImGui::TextColored(ImVec4 { colormap50.x, colormap50.y, colormap50.z, 1.0f }, "%g", evToLuminanceValue(lerp(static_cast<float>(evMinValue()), static_cast<float>(evMaxValue()), 0.5)));
          ImGui::SameLine();
          ImGui::TextColored(ImVec4 { colormap75.x, colormap75.y, colormap75.z, 1.0f }, "%g", evToLuminanceValue(lerp(static_cast<float>(evMinValue()), static_cast<float>(evMaxValue()), 0.75)));
          ImGui::SameLine();
          ImGui::TextColored(ImVec4 { colormap100.x, colormap100.y, colormap100.z, 1.0f }, "%g", evToLuminanceValue(static_cast<float>(evMaxValue())));

          ImGui::EndChildFrame();
          ImGui::PopStyleColor();
        }
      } else if (displayType() == DebugViewDisplayType::HDRWaveform) {
        ImGui::Text("HDR Waveform:");

        ImGui::Checkbox("Luminance Mode", &m_enableLuminanceMode);

        ImGui::InputInt("Min Value (Log10)", &m_log10MinValue);
        ImGui::InputInt("Max Value (Log10)", &m_log10MaxValue);

        m_log10MaxValue = std::max(m_log10MaxValue, m_log10MinValue);

        ImGui::InputFloat("Histogram Normalization Scale", &m_hdrWaveformHistogramNormalizationScale, 0.02f, 0.1f);

        int hdrWaveformScaleFactor = static_cast<int>(m_hdrWaveformResolutionScaleFactor);
        ImGui::InputInt("Display Resolution Scale", &hdrWaveformScaleFactor);

        // Note: Clamped to 2 due to maximum texture allocation supporting up to this much.
        m_hdrWaveformResolutionScaleFactor = static_cast<uint32_t>(std::max(hdrWaveformScaleFactor, 2));

        int hdrWaveformPosition[2] = { static_cast<int>(m_hdrWaveformPosition.x),  static_cast<int>(m_hdrWaveformPosition.y) };
        ImGui::InputInt2("Display Position", hdrWaveformPosition);

        m_hdrWaveformPosition.x = static_cast<uint32_t>(std::max(hdrWaveformPosition[0], 0));
        m_hdrWaveformPosition.y = static_cast<uint32_t>(std::max(hdrWaveformPosition[1], 0));
      }

      ImGui::Unindent();
    }

    ImGui::Checkbox("Enable GPU Printing On Press CTRL", &gpuPrint.enableObject());

    if (ImGui::CollapsingHeader("GPU Print", collapsingHeaderFlags)) {
      ImGui::Checkbox("Use Mouse Position", &gpuPrint.useMousePositionObject());
      if (!gpuPrint.useMousePosition()) {
        ImGui::DragInt2("Pixel Position", &gpuPrint.pixelIndexObject(), 0.1f, 0, INT32_MAX, "%d", sliderFlags);
      }
    }   
  }

  void DebugView::createConstantsBuffer()
  {
    DxvkBufferCreateInfo info = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    info.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    info.stages = VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR;
    info.access = VK_ACCESS_TRANSFER_WRITE_BIT;
    info.size = sizeof(DebugViewArgs);
    m_debugViewConstants = m_device->createBuffer(info, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, DxvkMemoryStats::Category::RTXBuffer);
  }

  Rc<DxvkBuffer> DebugView::getDebugViewConstantsBuffer() {
    if (m_debugViewConstants == nullptr) {
      createConstantsBuffer();
    }
    assert(m_debugViewConstants != nullptr);
    return m_debugViewConstants;
  }

  void DebugView::onFrameBegin(Rc<DxvkContext>& ctx, const VkExtent3D& downscaledExtent, const VkExtent3D& targetExtent) {
    ScopedCpuProfileZone();

    RtxPass::onFrameBegin(ctx, downscaledExtent, targetExtent);

    if (!isActive())
      return;

    VkClearColorValue clearColor;
    
    if (debugViewIdx() == DEBUG_VIEW_NAN)
      clearColor = { 1.f, 0.f, 0.f, 0.f };
    else
      clearColor = { 0.f, 0.f, 0.f, 0.f };

    VkImageSubresourceRange subRange = {};
    subRange.layerCount = 1;
    subRange.levelCount = 1;
    subRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

    ctx->clearColorImage(m_debugView.image, clearColor, subRange);

    if (debugViewIdx() == DEBUG_VIEW_INSTRUMENTATION_THREAD_DIVERGENCE)
      ctx->clearColorImage(m_instrumentation.image, clearColor, subRange);
  }

  DebugViewArgs DebugView::getCommonDebugViewArgs(DxvkContext* ctx, const Resources::RaytracingOutput& rtOutput, DxvkObjects& common) {
    auto debugViewResolution = m_debugView.view->imageInfo().extent;
    auto currTime = std::chrono::system_clock::now();
    std::chrono::duration<float> elapsedSec = currTime - m_startTime;
    const uint32_t frameIdx = ctx->getDevice()->getCurrentFrameId();

    DebugViewArgs debugViewArgs = {};

    debugViewArgs.debugViewIdx = debugViewIdx();
    debugViewArgs.colorCodeRadius = std::clamp(m_colorCodeRadius, 0, 8);

    if (s_disableAnimation)
      debugViewArgs.animationTimeSec = 0;
    else
      debugViewArgs.animationTimeSec = elapsedSec.count();

    debugViewArgs.frameIdx = ctx->getDevice()->getCurrentFrameId();

    debugViewArgs.displayType = displayType();
    debugViewArgs.enableInfNanViewFlag = m_enableInfNanView;
    debugViewArgs.debugViewResolution.x = debugViewResolution.width;
    debugViewArgs.debugViewResolution.y = debugViewResolution.height;

    debugViewArgs.debugKnob = m_debugKnob;

    if (displayType() == DebugViewDisplayType::Standard) {
      debugViewArgs.enablePseudoColorFlag = enablePseudoColor();
      debugViewArgs.enableAlphaChannelFlag = m_enableAlphaChannel;

      debugViewArgs.minValue = minValue();
      debugViewArgs.maxValue = maxValue();
      debugViewArgs.scale = m_scale;
    } else if (displayType() == DebugViewDisplayType::EV100) {
      assert(evMaxValue() >= evMinValue());

      debugViewArgs.evMinValue = evMinValue();
      debugViewArgs.evRange = evMaxValue() - evMinValue();
    } else if (displayType() == DebugViewDisplayType::HDRWaveform) {
      debugViewArgs.enableLuminanceModeFlag = m_enableLuminanceMode;

      assert(m_log10MaxValue >= m_log10MinValue);

      debugViewArgs.log10MinValue = m_log10MinValue;
      debugViewArgs.log10Range = m_log10MaxValue - m_log10MinValue;
      debugViewArgs.hdrWaveformResolution.x = debugViewResolution.width / m_hdrWaveformResolutionScaleFactor;
      debugViewArgs.hdrWaveformResolution.y = debugViewResolution.height / m_hdrWaveformResolutionScaleFactor;
      debugViewArgs.hdrWaveformPosition.x = m_hdrWaveformPosition.x;
      debugViewArgs.hdrWaveformPosition.y = m_hdrWaveformPosition.y;
      debugViewArgs.hdrWaveformResolutionScaleFactor = m_hdrWaveformResolutionScaleFactor;
      debugViewArgs.hdrWaveformHistogramNormalizationScale = m_hdrWaveformHistogramNormalizationScale;
    }

    debugViewArgs.samplerType = samplerType();

    debugViewArgs.isRTXDIConfidenceValid = rtOutput.getCurrentRtxdiConfidence().matchesWriteFrameIdx(frameIdx);

    // Todo: Add cases for secondary denoiser.
    if (RtxOptions::Get()->isSeparatedDenoiserEnabled()) {
      switch (debugViewIdx()) {
      case DEBUG_VIEW_DENOISED_PRIMARY_DIRECT_DIFFUSE_RADIANCE:
      case DEBUG_VIEW_DENOISED_PRIMARY_DIRECT_SPECULAR_RADIANCE:
      case DEBUG_VIEW_DENOISED_PRIMARY_DIRECT_DIFFUSE_HIT_T:
      case DEBUG_VIEW_DENOISED_PRIMARY_DIRECT_SPECULAR_HIT_T:
        debugViewArgs.nrd = common.metaPrimaryDirectLightDenoiser().getNrdArgs();
        break;
      case DEBUG_VIEW_DENOISED_PRIMARY_INDIRECT_DIFFUSE_RADIANCE:
      case DEBUG_VIEW_DENOISED_PRIMARY_INDIRECT_SPECULAR_RADIANCE:
      case DEBUG_VIEW_DENOISED_PRIMARY_INDIRECT_DIFFUSE_HIT_T:
      case DEBUG_VIEW_DENOISED_PRIMARY_INDIRECT_SPECULAR_HIT_T:
        debugViewArgs.nrd = common.metaPrimaryIndirectLightDenoiser().getNrdArgs();
        break;
      default: break;
      }
    } else {
      debugViewArgs.nrd = common.metaPrimaryCombinedLightDenoiser().getNrdArgs();
    }

    return debugViewArgs;
  }

  void DebugView::dispatch(Rc<DxvkCommandList> cmdList, 
                           Rc<DxvkContext> ctx,
                           Rc<DxvkSampler> nearestSampler,
                           Rc<DxvkSampler> linearSampler,
                           Rc<DxvkImage>& outputImage,
                           const Resources::RaytracingOutput& rtOutput, 
                           DxvkObjects& common) {

    if (m_showCachedImage) {
      if (m_cachedImage.image.ptr())
        outputImage = m_cachedImage.image;
    }
    else if (debugViewIdx() != DEBUG_VIEW_DISABLED) {

      auto&& debugViewArgs = getCommonDebugViewArgs(ctx.ptr(), rtOutput, common);

      Rc<DxvkBuffer> cb = getDebugViewConstantsBuffer();
      ctx->updateBuffer(cb, 0, sizeof(DebugViewArgs), &debugViewArgs);
      cmdList->trackResource<DxvkAccess::Read>(cb);

      if (displayType() == DebugViewDisplayType::HDRWaveform) {
        // Clear HDR Waveform textures when in use before accumulated into

        VkClearColorValue clearColor;
        clearColor.uint32[0] = clearColor.uint32[1] = clearColor.uint32[2] = clearColor.uint32[3] = 0;

        VkImageSubresourceRange subRange = {};
        subRange.layerCount = 1;
        subRange.levelCount = 1;
        subRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

        ctx->clearColorImage(m_hdrWaveformRed.image, clearColor, subRange);
        ctx->clearColorImage(m_hdrWaveformGreen.image, clearColor, subRange);
        ctx->clearColorImage(m_hdrWaveformBlue.image, clearColor, subRange);
      }

      // Process Debug View
      {
        ScopedGpuProfileZone(ctx, "Debug View");

        ctx->bindResourceView(DEBUG_VIEW_BINDING_INPUT_OUTPUT, m_debugView.view, nullptr);
        ctx->bindResourceView(DEBUG_VIEW_BINDING_DENOISED_PRIMARY_DIRECT_DIFFUSE_RADIANCE_HIT_T_INPUT, rtOutput.m_primaryDirectDiffuseRadiance.view(Resources::AccessType::Read), nullptr);
        ctx->bindResourceView(DEBUG_VIEW_BINDING_DENOISED_PRIMARY_DIRECT_SPECULAR_RADIANCE_HIT_T_INPUT, rtOutput.m_primaryDirectSpecularRadiance.view(Resources::AccessType::Read), nullptr);
        ctx->bindResourceView(DEBUG_VIEW_BINDING_DENOISED_SECONDARY_COMBINED_DIFFUSE_RADIANCE_HIT_T_INPUT, rtOutput.m_secondaryCombinedDiffuseRadiance.view(Resources::AccessType::Read), nullptr);
        ctx->bindResourceView(DEBUG_VIEW_BINDING_DENOISED_SECONDARY_COMBINED_SPECULAR_RADIANCE_HIT_T_INPUT, rtOutput.m_secondaryCombinedSpecularRadiance.view(Resources::AccessType::Read), nullptr);
        ctx->bindResourceView(DEBUG_VIEW_BINDING_SHARED_FLAGS_INPUT, rtOutput.m_sharedFlags.view, nullptr);
        ctx->bindResourceView(DEBUG_VIEW_BINDING_PRIMARY_LINEAR_VIEW_Z_INPUT, rtOutput.m_primaryLinearViewZ.view, nullptr);
        ctx->bindResourceView(DEBUG_VIEW_BINDING_PRIMARY_VIRTUAL_WORLD_SHADING_NORMAL_PERCEPTUAL_ROUGHNESS_INPUT, rtOutput.m_primaryVirtualWorldShadingNormalPerceptualRoughness.view, nullptr);
        ctx->bindResourceView(DEBUG_VIEW_BINDING_PRIMARY_VIRTUAL_MOTION_VECTOR_INPUT, rtOutput.m_primaryVirtualMotionVector.view, nullptr);
        ctx->bindResourceView(DEBUG_VIEW_BINDING_PRIMARY_SCREEN_SPACE_MOTION_VECTOR_INPUT, rtOutput.m_primaryScreenSpaceMotionVector.view, nullptr);
        ctx->bindResourceView(DEBUG_VIEW_BINDING_RTXDI_CONFIDENCE_INPUT, rtOutput.getCurrentRtxdiConfidence().view(Resources::AccessType::Read, debugViewArgs.isRTXDIConfidenceValid), nullptr);
        ctx->bindResourceView(DEBUG_VIEW_BINDING_FINAL_SHADING_INPUT, rtOutput.m_finalOutput.view, nullptr);
        ctx->bindResourceView(DEBUG_VIEW_BINDING_COMPOSITE_OUTPUT_INPUT, rtOutput.m_compositeOutput.view(Resources::AccessType::Read), nullptr);
        ctx->bindResourceView(DEBUG_VIEW_BINDING_INSTRUMENTATION_INPUT, m_instrumentation.view, nullptr);
        ctx->bindResourceView(DEBUG_VIEW_BINDING_HDR_WAVEFORM_RED_INPUT_OUTPUT, m_hdrWaveformRed.view, nullptr);
        ctx->bindResourceView(DEBUG_VIEW_BINDING_HDR_WAVEFORM_GREEN_INPUT_OUTPUT, m_hdrWaveformGreen.view, nullptr);
        ctx->bindResourceView(DEBUG_VIEW_BINDING_HDR_WAVEFORM_BLUE_INPUT_OUTPUT, m_hdrWaveformBlue.view, nullptr);
        ctx->bindResourceBuffer(DEBUG_VIEW_BINDING_CONSTANTS_INPUT, DxvkBufferSlice(cb, 0, cb->info().size));

        ctx->bindResourceSampler(DEBUG_VIEW_BINDING_NEAREST_SAMPLER, nearestSampler);
        ctx->bindResourceSampler(DEBUG_VIEW_BINDING_LINEAR_SAMPLER, linearSampler);

        const auto& terrain = m_device->getCommon()->getResources().getTerrainTexture(ctx);
        ctx->bindResourceView(DEBUG_VIEW_BINDING_TERRAIN_INPUT, terrain.view, nullptr);

        ctx->bindShader(VK_SHADER_STAGE_COMPUTE_BIT, DebugViewShader::getShader());

        VkExtent3D workgroups = util::computeBlockCount(m_debugView.view->imageInfo().extent, VkExtent3D { 16, 8, 1 });
        ctx->dispatch(workgroups.width, workgroups.height, workgroups.depth);
      }

      // Display HDR Waveform
      if (displayType() == DebugViewDisplayType::HDRWaveform) {
        ScopedGpuProfileZone(ctx, "HDR Waveform Render");

        ctx->bindResourceView(DEBUG_VIEW_WAVEFORM_RENDER_BINDING_HDR_WAVEFORM_RED_INPUT, m_hdrWaveformRed.view, nullptr);
        ctx->bindResourceView(DEBUG_VIEW_WAVEFORM_RENDER_BINDING_HDR_WAVEFORM_GREEN_INPUT, m_hdrWaveformGreen.view, nullptr);
        ctx->bindResourceView(DEBUG_VIEW_WAVEFORM_RENDER_BINDING_HDR_WAVEFORM_BLUE_INPUT, m_hdrWaveformBlue.view, nullptr);
        ctx->bindResourceView(DEBUG_VIEW_WAVEFORM_RENDER_BINDING_INPUT_OUTPUT, m_debugView.view, nullptr);
        ctx->bindResourceBuffer(DEBUG_VIEW_WAVEFORM_RENDER_BINDING_CONSTANTS_INPUT, DxvkBufferSlice(cb, 0, cb->info().size));

        ctx->bindShader(VK_SHADER_STAGE_COMPUTE_BIT, DebugViewWaveformRenderShader::getShader());

        VkExtent3D waveformResolution = m_debugView.view->imageInfo().extent;

        waveformResolution.width /= m_hdrWaveformResolutionScaleFactor;
        waveformResolution.height /= m_hdrWaveformResolutionScaleFactor;

        VkExtent3D workgroups = util::computeBlockCount(waveformResolution, VkExtent3D { 16, 8, 1 });
        ctx->dispatch(workgroups.width, workgroups.height, workgroups.depth);
      }

      outputImage = m_debugView.image;
    }

    if (m_cacheCurrentImage) {
      if (!m_cachedImage.image.ptr() ||
          m_cachedImage.image->info().extent.width != outputImage->info().extent.width ||
          m_cachedImage.image->info().extent.height != outputImage->info().extent.height ||
          m_cachedImage.image->info().format != outputImage->info().format) {
        m_cachedImage = Resources::createImageResource(ctx, "debug view cache", outputImage->info().extent, outputImage->info().format);
      }

      const VkImageSubresourceLayers srcSubresourceLayers = { outputImage->formatInfo()->aspectMask, 0, 0, 1 };
      const VkImageSubresourceLayers dstSubresourceLayers = { m_cachedImage.image->formatInfo()->aspectMask, 0, 0, 1 };

      ctx->copyImage(
        m_cachedImage.image, dstSubresourceLayers, VkOffset3D { 0, 0, 0 },
        outputImage, srcSubresourceLayers, VkOffset3D { 0, 0, 0 },
        outputImage->info().extent);

      m_cacheCurrentImage = false;
    }
  }

  void DebugView::createDownscaledResource(Rc<DxvkContext>& ctx, const VkExtent3D& downscaledExtent) {
    // Debug
    m_debugView = Resources::createImageResource(ctx, "debug view", downscaledExtent, VK_FORMAT_R32G32B32A32_SFLOAT);

    // Note: Only allocate half resolution for HDR waveform buffers, this is the default view size
    // and while it is wasteful if the resolution scale is higher, this is probably fine.
    m_hdrWaveformRed = Resources::createImageResource(ctx, "debug hdr waveform red", { (downscaledExtent.width + 2) / 2, (downscaledExtent.height + 2) / 2, 1 }, VK_FORMAT_R32_UINT);
    m_hdrWaveformBlue = Resources::createImageResource(ctx, "debug hdr waveform green", { (downscaledExtent.width + 2) / 2, (downscaledExtent.height + 2) / 2, 1 }, VK_FORMAT_R32_UINT);
    m_hdrWaveformGreen = Resources::createImageResource(ctx, "debug hdr waveform blue", { (downscaledExtent.width + 2) / 2, (downscaledExtent.height + 2) / 2, 1 }, VK_FORMAT_R32_UINT);

    // Instrumentation
    m_instrumentation = Resources::createImageResource(ctx, "debug instrumentation", downscaledExtent, VK_FORMAT_R32_UINT);
  }

  void DebugView::releaseDownscaledResource() {
    m_debugView.reset();
    m_hdrWaveformRed.reset();
    m_hdrWaveformBlue.reset();
    m_hdrWaveformGreen.reset();
    m_instrumentation.reset();
  }

  bool DebugView::isActive() {
    return debugViewIdx() != DEBUG_VIEW_DISABLED || m_showCachedImage || m_cacheCurrentImage;
  }
} // namespace dxvk
