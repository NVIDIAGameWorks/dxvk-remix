/*
* Copyright (c) 2021-2026, NVIDIA CORPORATION. All rights reserved.
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

#include <cassert>
#include <tuple>
#include <string>
#include <sstream>
#include <iomanip>
#include <optional>
#include <nvapi.h>
#include <NVIDIASansRg.ttf.h>
#include <NVIDIASansBd.ttf.h>
#include <RobotoMonoRg.ttf.h>

#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_impl_dxvk.hpp"
#include "imgui_impl_win32.h"
#include "implot.h"
#include "dxvk_imgui.h"
#include "rtx_render/rtx_imgui.h"
#include "dxvk_device.h"
#include "rtx_render/graph/rtx_graph_gui.h"
#include "rtx_render/rtx_utils.h"
#include "rtx_render/rtx_shader_manager.h"
#include "rtx_render/rtx_camera.h"
#include "rtx_render/rtx_context.h"
#include "rtx_render/rtx_hash_collision_detection.h"
#include "rtx_render/rtx_options.h"
#include "rtx_render/rtx_terrain_baker.h"
#include "rtx_render/rtx_neural_radiance_cache.h"
#include "rtx_render/rtx_ray_reconstruction.h"
#include "rtx_render/rtx_xess.h"
#include "rtx_render/rtx_rtxdi_rayquery.h"
#include "rtx_render/rtx_restir_gi_rayquery.h"
#include "rtx_render/rtx_debug_view.h"
#include "rtx_render/rtx_composite.h"
#include "dxvk_image.h"
#include "../util/rc/util_rc_ptr.h"
#include "../util/util_math.h"
#include "../util/util_globaltime.h"
#include "rtx_render/rtx_opacity_micromap_manager.h"
#include "rtx_render/rtx_bridge_message_channel.h"
#include "dxvk_imgui_about.h"
#include "dxvk_imgui_splash.h"
#include "dxvk_imgui_capture.h"
#include "rtx_render/rtx_option_layer_gui.h"
#include "rtx_render/rtx_option_manager.h"
#include "dxvk_scoped_annotation.h"
#include "../../d3d9/d3d9_rtx.h"
#include "dxvk_memory_tracker.h"
#include "rtx_render/rtx_particle_system.h"
#include "rtx_render/rtx_overlay_window.h"


namespace dxvk {
  extern size_t g_streamedTextures_budgetBytes;
  extern size_t g_streamedTextures_usedBytes;
}


extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
extern ImGuiKey ImGui_ImplWin32_VirtualKeyToImGuiKey(WPARAM wParam);

namespace ImGui {
  // Note: Implementation of text separators adapted from:
  // https://github.com/ocornut/imgui/issues/1643

  void CenteredSeparator(float width = 0) {
    ImGuiWindow* window = GetCurrentWindow();
    if (window->SkipItems)
      return;
    ImGuiContext& g = *GImGui;

    // Horizontal Separator
    float x1, x2;
    if (window->DC.CurrentColumns == NULL && (width == 0)) {
      // Span whole window
      x1 = window->DC.CursorPos.x;
      // Note: Account for padding on the Window
      x2 = window->Pos.x + window->Size.x - window->WindowPadding.x;
    } else {
      // Start at the cursor
      x1 = window->DC.CursorPos.x;
      if (width != 0) {
        x2 = x1 + width;
      } else {
        x2 = window->ClipRect.Max.x;
        // Pad right side of columns (except the last one)
        if (window->DC.CurrentColumns && (window->DC.CurrentColumns->Current < window->DC.CurrentColumns->Count - 1))
          x2 -= g.Style.ItemSpacing.x;
      }
    }
    float y1 = window->DC.CursorPos.y + int(window->DC.CurrLineSize.y / 2.0f);
    float y2 = y1 + 1.0f;

    window->DC.CursorPos.x += width; //+ g.Style.ItemSpacing.x;

    const ImRect bb(ImVec2(x1, y1), ImVec2(x2, y2));
    ItemSize(ImVec2(0.0f, 0.0f)); // NB: we don't provide our width so that it doesn't get feed back into AutoFit, we don't provide height to not alter layout.
    if (!ItemAdd(bb, NULL)) {
      return;
    }

    window->DrawList->AddLine(bb.Min, ImVec2(bb.Max.x, bb.Min.y), GetColorU32(ImGuiCol_Border));
  }

  // Create a centered separator right after the current item.
  // Eg.: 
  // ImGui::PreSeparator(10);
  // ImGui::Text("Section VI");
  // ImGui::SameLineSeparator();
  void SameLineSeparator(float width = 0) {
    ImGui::SameLine();
    CenteredSeparator(width);
  }

  // Create a centered separator which can be immediately followed by a item
  void PreSeparator(float width) {
    ImGuiWindow* window = GetCurrentWindow();
    if (window->DC.CurrLineSize.y == 0)
      window->DC.CurrLineSize.y = ImGui::GetTextLineHeight();
    CenteredSeparator(width);
    ImGui::SameLine();
  }

  // The value for width is arbitrary. But it looks nice.
  void TextSeparator(const char* text, float pre_width = 10.0f) {
    ImGui::PreSeparator(pre_width);
    ImGui::Text(text);
    ImGui::SameLineSeparator();
  }
}

namespace dxvk {
  struct ImGuiTexture {
    Rc<DxvkImageView> imageView = VK_NULL_HANDLE;
    ImTextureID texID = VK_NULL_HANDLE;
    uint32_t textureFeatureFlags = 0;
  };
  std::unordered_map<XXH64_hash_t, ImGuiTexture> g_imguiTextureMap;
  fast_unordered_cache<FogState> g_imguiFogMap;
  XXH64_hash_t g_usedFogStateHash;
  std::mutex g_imguiFogMapMutex; // protects g_imguiFogMap

  struct RtxTextureOption {
    const char* uniqueId;
    const char* displayName;
    RtxOption<fast_unordered_set>* textureSetOption;
    uint32_t featureFlagMask = ImGUI::kTextureFlagsDefault;
    bool bufferToggle;
  };

  std::vector<RtxTextureOption> rtxTextureOptions = {
    {"uitextures", "UI Texture", &RtxOptions::uiTexturesObject()},
    {"worldspaceuitextures", "World Space UI Texture", &RtxOptions::worldSpaceUiTexturesObject()},
    {"worldspaceuibackgroundtextures", "World Space UI Background Texture", &RtxOptions::worldSpaceUiBackgroundTexturesObject()},
    {"skytextures", "Sky Texture", &RtxOptions::skyBoxTexturesObject()},
    {"ignoretextures", "Ignore Texture (optional)", &RtxOptions::ignoreTexturesObject()},
    {"hidetextures", "Hide Texture Instance (optional)", &RtxOptions::hideInstanceTexturesObject()},
    {"lightmaptextures","Lightmap Textures (optional)", &RtxOptions::lightmapTexturesObject()},
    {"ignorelights", "Ignore Lights (optional)", &RtxOptions::ignoreLightsObject()},
    {"particletextures", "Particle Texture (optional)", &RtxOptions::particleTexturesObject()},
    {"beamtextures", "Beam Texture (optional)", &RtxOptions::beamTexturesObject()},
    {"ignoretransparencytextures", "Ignore Transparency Layer Texture (optional)", &RtxOptions::ignoreTransparencyLayerTexturesObject()},
    {"lightconvertertextures", "Add Light to Textures (optional)", &RtxOptions::lightConverterObject()},
    {"decaltextures", "Decal Texture (optional)", &RtxOptions::decalTexturesObject()},
    {"terraintextures", "Terrain Texture", &RtxOptions::terrainTexturesObject()},
    {"watertextures", "Water Texture (optional)", &RtxOptions::animatedWaterTexturesObject()},
    {"antiCullingTextures", "Anti-Culling Texture (optional)", &RtxOptions::antiCullingTexturesObject()},
    {"motionBlurMaskOutTextures", "Motion Blur Mask-Out Textures (optional)", &RtxOptions::motionBlurMaskOutTexturesObject()},
    {"playermodeltextures", "Player Model Texture (optional)", &RtxOptions::playerModelTexturesObject()},
    {"playermodelbodytextures", "Player Model Body Texture (optional)", &RtxOptions::playerModelBodyTexturesObject()},
    {"opacitymicromapignoretextures", "Opacity Micromap Ignore Texture (optional)", &RtxOptions::opacityMicromapIgnoreTexturesObject()},
    {"ignorebakedlightingtextures","Ignore Baked Lighting Textures (optional)", &RtxOptions::ignoreBakedLightingTexturesObject()},
    {"ignorealphaontextures","Ignore Alpha Channel of Textures (optional)", &RtxOptions::ignoreAlphaOnTexturesObject()},
    {"raytracedRenderTargetTextures","Raytraced Render Target Textures (optional)", &RtxOptions::raytracedRenderTargetTexturesObject(), ImGUI::kTextureFlagsRenderTarget},
    {"particleemittertextures","Particle Emitters (optional)", &RtxOptions::particleEmitterTexturesObject()}
  };

  RemixGui::ComboWithKey<RenderPassGBufferRaytraceMode> renderPassGBufferRaytraceModeCombo {
    "GBuffer Raytracing Mode",
    RemixGui::ComboWithKey<RenderPassGBufferRaytraceMode>::ComboEntries { {
        {RenderPassGBufferRaytraceMode::RayQuery, "RayQuery (CS)"},
        {RenderPassGBufferRaytraceMode::RayQueryRayGen, "RayQuery (RGS)"},
        {RenderPassGBufferRaytraceMode::TraceRay, "TraceRay (RGS)"}
    } }
  };

  RemixGui::ComboWithKey<RenderPassIntegrateDirectRaytraceMode> renderPassIntegrateDirectRaytraceModeCombo {
    "Integrate Direct Raytracing Mode",
    RemixGui::ComboWithKey<RenderPassIntegrateDirectRaytraceMode>::ComboEntries { {
        {RenderPassIntegrateDirectRaytraceMode::RayQuery, "RayQuery (CS)"},
        {RenderPassIntegrateDirectRaytraceMode::RayQueryRayGen, "RayQuery (RGS)"}
    } }
  };

  RemixGui::ComboWithKey<RenderPassIntegrateIndirectRaytraceMode> renderPassIntegrateIndirectRaytraceModeCombo {
    "Integrate Indirect Raytracing Mode",
    RemixGui::ComboWithKey<RenderPassIntegrateIndirectRaytraceMode>::ComboEntries { {
        {RenderPassIntegrateIndirectRaytraceMode::RayQuery, "RayQuery (CS)"},
        {RenderPassIntegrateIndirectRaytraceMode::RayQueryRayGen, "RayQuery (RGS)"},
        {RenderPassIntegrateIndirectRaytraceMode::TraceRay, "TraceRay (RGS)"}
    } }
  };

  RemixGui::ComboWithKey<CameraAnimationMode> cameraAnimationModeCombo {
    "Camera Animation Mode",
    RemixGui::ComboWithKey<CameraAnimationMode>::ComboEntries { {
        {CameraAnimationMode::CameraShake_LeftRight, "CameraShake Left-Right"},
        {CameraAnimationMode::CameraShake_FrontBack, "CameraShake Front-Back"},
        {CameraAnimationMode::CameraShake_Yaw, "CameraShake Yaw"},
        {CameraAnimationMode::CameraShake_Pitch, "CameraShake Pitch"},
        {CameraAnimationMode::YawRotation, "Camera Yaw Rotation"}
    } }
  };

  RemixGui::ComboWithKey<int> textureQualityCombo {
    "Texture Quality",
    RemixGui::ComboWithKey<int>::ComboEntries { {
        {0, "High"},
        {1, "Low"},
    } }
  };

  RemixGui::ComboWithKey<ViewDistanceMode> viewDistanceModeCombo {
    "View Distance Mode",
    RemixGui::ComboWithKey<ViewDistanceMode>::ComboEntries { {
        {ViewDistanceMode::None, "None"},
        {ViewDistanceMode::HardCutoff, "Hard Cutoff"},
        {ViewDistanceMode::CoherentNoise, "Coherent Noise"},
    } }
  };

  RemixGui::ComboWithKey<ViewDistanceFunction> viewDistanceFunctionCombo {
    "View Distance Function",
    RemixGui::ComboWithKey<ViewDistanceFunction>::ComboEntries { {
        {ViewDistanceFunction::Euclidean, "Euclidean"},
        {ViewDistanceFunction::PlanarEuclidean, "Planar Euclidean"},
    } }
  };

  static auto fusedWorldViewModeCombo = RemixGui::ComboWithKey<FusedWorldViewMode>(
  "Fused World-View Mode",
  RemixGui::ComboWithKey<FusedWorldViewMode>::ComboEntries { {
      {FusedWorldViewMode::None, "None"},
      {FusedWorldViewMode::View, "In View Transform"},
      {FusedWorldViewMode::World, "In World Transform"},
  } });

  static auto skyAutoDetectCombo = RemixGui::ComboWithKey<SkyAutoDetectMode>(
    "Sky Auto-Detect",
    RemixGui::ComboWithKey<SkyAutoDetectMode>::ComboEntries{ {
      {SkyAutoDetectMode::None, "Off"},
      {SkyAutoDetectMode::CameraPosition, "By Camera Position"},
      {SkyAutoDetectMode::CameraPositionAndDepthFlags, "By Camera Position and Depth Flags"}
  } });

  static auto upscalerNoDLSSCombo = RemixGui::ComboWithKey<UpscalerType>(
    "Upscaler Type",
    { {
      {UpscalerType::None, "None"},
      {UpscalerType::NIS, "NIS"},
      {UpscalerType::TAAU, "TAA-U"},
      {UpscalerType::XeSS, "XeSS"},
  } });

  static auto upscalerDLSSCombo = RemixGui::ComboWithKey<UpscalerType>(
    "Upscaler Type",
    { {
      {UpscalerType::None, "None"},
      {UpscalerType::DLSS, "DLSS"},
      {UpscalerType::NIS, "NIS"},
      {UpscalerType::TAAU, "TAA-U"},
      {UpscalerType::XeSS, "XeSS"},
  } });

  RemixGui::ComboWithKey<DLSSProfile> dlssProfileCombo{
    "DLSS Mode",
    RemixGui::ComboWithKey<DLSSProfile>::ComboEntries{ {
        {DLSSProfile::UltraPerf, "Ultra Performance"},
        {DLSSProfile::MaxPerf, "Performance"},
        {DLSSProfile::Balanced, "Balanced"},
        {DLSSProfile::MaxQuality, "Quality"},
        {DLSSProfile::FullResolution, "Full Resolution"},
        {DLSSProfile::Auto, "Auto"},
    } }
  };

  RemixGui::ComboWithKey<XeSSPreset> xessPresetCombo{
    "XeSS Preset",
    RemixGui::ComboWithKey<XeSSPreset>::ComboEntries{ {
        {XeSSPreset::UltraPerf, "Ultra Performance"},
        {XeSSPreset::Performance, "Performance"},
        {XeSSPreset::Balanced, "Balanced"},
        {XeSSPreset::Quality, "Quality"},
        {XeSSPreset::UltraQuality, "Ultra Quality"},
        {XeSSPreset::UltraQualityPlus, "Ultra Quality Plus"},
        {XeSSPreset::NativeAA, "Native Anti-Aliasing"},
        {XeSSPreset::Custom, "Custom"},
    } }
  };

  RemixGui::ComboWithKey<RussianRouletteMode> secondPlusBounceRussianRouletteModeCombo {
    "2nd+ Bounce Russian Roulette Mode",
    RemixGui::ComboWithKey<RussianRouletteMode>::ComboEntries { {
        {RussianRouletteMode::ThroughputBased, "Throughput Based"},
        {RussianRouletteMode::SpecularBased, "Specular Based"}
    } }
  };

  RemixGui::ComboWithKey<IntegrateIndirectMode> integrateIndirectModeCombo {
    "Integrate Indirect Illumination Mode",
    RemixGui::ComboWithKey<IntegrateIndirectMode>::ComboEntries { {
        {IntegrateIndirectMode::ImportanceSampled, "Importance Sampled",  
          "Importance Sampled. Importance sampled mode uses typical GI sampling and it is not recommended for general use as it provides the noisiest output.\n"
          "It serves as a reference integration mode for validation of other indirect integration modes." },
        {IntegrateIndirectMode::ReSTIRGI, "ReSTIR GI", 
          "ReSTIR GI provides improved indirect path sampling over \"Importance Sampled\" mode with better indirect diffuse and specular GI quality at increased performance cost."},
        {IntegrateIndirectMode::NeuralRadianceCache, "RTX Neural Radiance Cache", 
          "RTX Neural Radiance Cache (NRC). NRC is an AI based world space radiance cache. It is live trained by the path tracer\n"
          "and allows paths to terminate early by looking up the cached value and saving performance.\n"
          "NRC supports infinite bounces and often provides results closer to that of reference than ReSTIR GI\n"
          "while increasing performance in scenarios where ray paths have 2 or more bounces on average."}
    } }
  };

  static auto rayReconstructionModelCombo = RemixGui::ComboWithKey<DxvkRayReconstruction::RayReconstructionModel>(
    "Ray Reconstruction Model",
    { {
      {DxvkRayReconstruction::RayReconstructionModel::Transformer, "Transformer", "Ensures highest image quality. Can be more expensive than CNN in terms of memory and performance."},
      {DxvkRayReconstruction::RayReconstructionModel::CNN, "CNN", "Ensures great image quality"},
  } });

  RemixGui::ComboWithKey<int> dlfgMfgModeCombo {
    "DLSS Frame Generation Mode",
    RemixGui::ComboWithKey<int>::ComboEntries { {
        {1, "2x"},
        {2, "3x"},
        {3, "4x"},
    } }
  };

  RemixGui::ComboWithKey<ReflexMode> reflexModeCombo{
    "Reflex",
    RemixGui::ComboWithKey<ReflexMode>::ComboEntries{ {
        {ReflexMode::None, "Disabled"},
        {ReflexMode::LowLatency, "Enabled"},
        {ReflexMode::LowLatencyBoost, "Enabled + Boost"},
    } }
  };

#ifdef REMIX_DEVELOPMENT
  RemixGui::ComboWithKey<dxvk::RtxFramePassStage>::ComboEntries aliasingPassComboEntries = { {
      { RtxFramePassStage::FrameBegin, "FrameBegin" },
      { RtxFramePassStage::Volumetrics, "Volumetrics" },
      { RtxFramePassStage::VolumeIntegrateRestirInitial, "VolumeIntegrateRestirInitial" },
      { RtxFramePassStage::VolumeIntegrateRestirVisible, "VolumeIntegrateRestirVisible" },
      { RtxFramePassStage::VolumeIntegrateRestirTemporal, "VolumeIntegrateRestirTemporal" },
      { RtxFramePassStage::VolumeIntegrateRestirSpatialResampling, "VolumeIntegrateRestirSpatialResampling" },
      { RtxFramePassStage::VolumeIntegrateRaytracing, "VolumeIntegrateRaytracing" },
      { RtxFramePassStage::GBufferPrimaryRays, "GBufferPrimaryRays" },
      { RtxFramePassStage::ReflectionPSR, "ReflectionPSR" },
      { RtxFramePassStage::TransmissionPSR, "TransmissionPSR" },
      { RtxFramePassStage::RTXDI_InitialTemporalReuse, "RTXDI_InitialTemporalReuse" },
      { RtxFramePassStage::RTXDI_SpatialReuse, "RTXDI_SpatialReuse" },
      { RtxFramePassStage::NEE_Cache, "NEE_Cache" },
      { RtxFramePassStage::DirectIntegration, "DirectIntegration" },
      { RtxFramePassStage::RTXDI_ComputeGradients, "RTXDI_ComputeGradients" },
      { RtxFramePassStage::IndirectIntegration, "IndirectIntegration" },
      { RtxFramePassStage::NEE_Integration, "NEE_Integration" },
      { RtxFramePassStage::NRC, "NRC" },
      { RtxFramePassStage::RTXDI_FilterGradients, "RTXDI_FilterGradients" },
      { RtxFramePassStage::RTXDI_ComputeConfidence, "RTXDI_ComputeConfidence" },
      { RtxFramePassStage::ReSTIR_GI_TemporalReuse, "ReSTIR_GI_TemporalReuse" },
      { RtxFramePassStage::ReSTIR_GI_SpatialReuse, "ReSTIR_GI_SpatialReuse" },
      { RtxFramePassStage::ReSTIR_GI_FinalShading, "ReSTIR_GI_FinalShading" },
      { RtxFramePassStage::Demodulate, "Demodulate" },
      { RtxFramePassStage::NRD, "NRD" },
      { RtxFramePassStage::CompositionAlphaBlend, "CompositionAlphaBlend" },
      { RtxFramePassStage::Composition, "Composition" },
      { RtxFramePassStage::DLSS, "DLSS" },
      { RtxFramePassStage::DLSSRR, "DLSSRR" },
      { RtxFramePassStage::NIS, "NIS" },
      { RtxFramePassStage::XeSS, "XeSS" },
      { RtxFramePassStage::TAA, "TAA" },
      { RtxFramePassStage::DustParticles, "DustParticles" },
      { RtxFramePassStage::Bloom, "Bloom" },
      { RtxFramePassStage::PostFX, "PostFX" },
      { RtxFramePassStage::AutoExposure_Histogram, "AutoExposure_Histogram" },
      { RtxFramePassStage::AutoExposure_Exposure, "AutoExposure_Exposure" },
      { RtxFramePassStage::ToneMapping, "ToneMapping" },
      { RtxFramePassStage::FrameEnd, "FrameEnd" },
  } };

  static auto aliasingBeginPassCombo = RemixGui::ComboWithKey<dxvk::RtxFramePassStage>(
    "Aliasing Begin Pass", RemixGui::ComboWithKey<dxvk::RtxFramePassStage>::ComboEntries{ aliasingPassComboEntries });

  static auto aliasingEndPassCombo = RemixGui::ComboWithKey<dxvk::RtxFramePassStage>(
    "Aliasing End Pass", RemixGui::ComboWithKey<dxvk::RtxFramePassStage>::ComboEntries { aliasingPassComboEntries });

  static auto aliasingExtentCombo = RemixGui::ComboWithKey<RtxTextureExtentType>(
    "Aliasing Extent Type",
    { {
      { RtxTextureExtentType::DownScaledExtent, "DownScaledExtent" },
      { RtxTextureExtentType::TargetExtent, "TargetExtent" },
      { RtxTextureExtentType::Custom, "Custom" },
  } } );

  static auto aliasingFormatCombo = RemixGui::ComboWithKey<RtxTextureFormatCompatibilityCategory>(
     "Aliasing Format",
     { {
      { RtxTextureFormatCompatibilityCategory::Color_Format_8_Bits, "8 Bits Color Texture" },
      { RtxTextureFormatCompatibilityCategory::Color_Format_16_Bits, "16 Bits Color Texture" },
      { RtxTextureFormatCompatibilityCategory::Color_Format_32_Bits, "32 Bits Color Texture" },
      { RtxTextureFormatCompatibilityCategory::Color_Format_64_Bits, "64 Bits Color Texture" },
      { RtxTextureFormatCompatibilityCategory::Color_Format_128_Bits, "128 Bits Color Texture" },
      { RtxTextureFormatCompatibilityCategory::Color_Format_256_Bits, "256 Bits Color Texture" },
      // All other formats
      { RtxTextureFormatCompatibilityCategory::InvalidFormatCompatibilityCategory, "Not Listed Format" },
     } }
  );

  static auto aliasingImageTypeCombo = RemixGui::ComboWithKey<VkImageType>(
     "Aliasing Image Type",
     { {
      { VK_IMAGE_TYPE_1D, "VK_IMAGE_TYPE_1D" },
      { VK_IMAGE_TYPE_2D, "VK_IMAGE_TYPE_2D" },
      { VK_IMAGE_TYPE_3D, "VK_IMAGE_TYPE_3D" },
     } }
  );

  static auto aliasingImageViewTypeCombo = RemixGui::ComboWithKey<VkImageViewType>(
     "Aliasing Image View Type",
     { {
      { VK_IMAGE_VIEW_TYPE_1D, "VK_IMAGE_VIEW_TYPE_1D" },
      { VK_IMAGE_VIEW_TYPE_1D_ARRAY, "VK_IMAGE_VIEW_TYPE_1D_ARRAY" },
      { VK_IMAGE_VIEW_TYPE_2D, "VK_IMAGE_VIEW_TYPE_2D" },
      { VK_IMAGE_VIEW_TYPE_2D_ARRAY, "VK_IMAGE_VIEW_TYPE_2D_ARRAY" },
      { VK_IMAGE_VIEW_TYPE_3D, "VK_IMAGE_VIEW_TYPE_3D" },
      { VK_IMAGE_VIEW_TYPE_CUBE, "VK_IMAGE_VIEW_TYPE_CUBE" },
      { VK_IMAGE_VIEW_TYPE_CUBE_ARRAY, "VK_IMAGE_VIEW_TYPE_CUBE_ARRAY" },
     } }
  );
#endif

  enum class TerrainMode {
    None,
    TerrainBaker,
    AsDecals,
  };
  static auto terrainModeCombo = RemixGui::ComboWithKey<TerrainMode>(
    "Mode##terrain",
    {
      {        TerrainMode::None,              "None"},
      {TerrainMode::TerrainBaker,     "Terrain Baker"},
      {    TerrainMode::AsDecals, "Terrain-as-Decals"},
  });

  static auto themeCombo = RemixGui::ComboWithKey<ImGUI::Theme>(
    "Mode##theme",
    {
      {ImGUI::Theme::Toolkit,  "Default Theme"},
      {ImGUI::Theme::Legacy,   "Legacy Theme"},
      {ImGUI::Theme::Nvidia,   "NVIDIA Theme"},
  });

  // Styles 
  constexpr ImGuiSliderFlags sliderFlags = ImGuiSliderFlags_AlwaysClamp;
  constexpr ImGuiTreeNodeFlags collapsingHeaderClosedFlags = ImGuiTreeNodeFlags_CollapsingHeader;
  constexpr ImGuiTreeNodeFlags collapsingHeaderFlags = collapsingHeaderClosedFlags | ImGuiTreeNodeFlags_DefaultOpen;
  constexpr ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_AlwaysVerticalScrollbar;
  constexpr ImGuiWindowFlags popupWindowFlags = ImGuiWindowFlags_NoSavedSettings;

  RemixGui::ComboWithKey<UpscalerType>& getUpscalerCombo(DxvkDLSS& dlss, DxvkRayReconstruction& rayReconstruction) {
    if (dlss.supportsDLSS()) {
      return upscalerDLSSCombo;
    } else {
      // Drop DLSS item if unsupported.
      return upscalerNoDLSSCombo;
    }
  }

  bool ImGUI::showRayReconstructionEnable(bool supportsRR) {
    // Only show DLSS-RR option if "showRayReconstructionOption" is set to true.
    bool changed = false;
    bool rayReconstruction = RtxOptions::enableRayReconstruction();
    if (RtxOptions::showRayReconstructionOption()) {
      ImGui::BeginDisabled(!supportsRR);
      changed = RemixGui::Checkbox("Ray Reconstruction", &RtxOptions::enableRayReconstructionObject());

      if (RtxOptions::enableRayReconstruction()) {
        rayReconstructionModelCombo.getKey(&DxvkRayReconstruction::modelObject());
      }
      ImGui::EndDisabled();
    }

    // Disable DLSS-RR if it's unsupported.
    if (!supportsRR && RtxOptions::enableRayReconstruction()) {
      RtxOptions::enableRayReconstruction.setDeferred(false);
      changed = true;
    }
    return changed;
  }

  ImGUI::ImGUI(DxvkDevice* device)
  : m_device (device)
  , m_gameHwnd   (nullptr)
  , m_about  (new ImGuiAbout)
  , m_splash  (new ImGuiSplash)
  , m_graphGUI  (new RtxGraphGUI) {
    // Set up constant state
    m_rsState.polygonMode       = VK_POLYGON_MODE_FILL;
    m_rsState.cullMode          = VK_CULL_MODE_BACK_BIT;
    m_rsState.frontFace         = VK_FRONT_FACE_CLOCKWISE;
    m_rsState.depthClipEnable   = VK_FALSE;
    m_rsState.depthBiasEnable   = VK_FALSE;
    m_rsState.conservativeMode  = VK_CONSERVATIVE_RASTERIZATION_MODE_DISABLED_EXT;
    m_rsState.sampleCount       = VK_SAMPLE_COUNT_1_BIT;

    m_blendMode.enableBlending  = VK_TRUE;
    m_blendMode.colorSrcFactor  = VK_BLEND_FACTOR_ONE;
    m_blendMode.colorDstFactor  = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    m_blendMode.colorBlendOp    = VK_BLEND_OP_ADD;
    m_blendMode.alphaSrcFactor  = VK_BLEND_FACTOR_ONE;
    m_blendMode.alphaDstFactor  = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    m_blendMode.alphaBlendOp    = VK_BLEND_OP_ADD;
    m_blendMode.writeMask       = VK_COLOR_COMPONENT_R_BIT
                                | VK_COLOR_COMPONENT_G_BIT
                                | VK_COLOR_COMPONENT_B_BIT
                                | VK_COLOR_COMPONENT_A_BIT;
    
    // the size of the pool is oversized, but it's copied from imgui demo itself.
    VkDescriptorPoolSize pool_sizes[] =
    {
      { VK_DESCRIPTOR_TYPE_SAMPLER, 1000 },
      { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
      { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 },
      { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
      { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 },
      { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 },
      { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
      { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
      { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
      { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
      { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 }
    };

    VkDescriptorPoolCreateInfo pool_info = {};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    // ImGUI is currently using a single set per texture, and so we want this to be a big number 
    //  to support displaying texture lists in games that use a lot of textures.
    // See: 'ImGui_ImplDxvk::AddTexture(...)' for more details about how this system works.
    pool_info.maxSets = 10000;
    pool_info.poolSizeCount = std::size(pool_sizes);
    pool_info.pPoolSizes = pool_sizes;

    if (!NeuralRadianceCache::checkIsSupported(device)) {
      // Remove unsupported option
      integrateIndirectModeCombo.removeComboEntry(IntegrateIndirectMode::NeuralRadianceCache);
    }

    m_device->vkd()->vkCreateDescriptorPool(m_device->handle(), &pool_info, nullptr, &m_imguiPool);

    // Initialize the core structures of ImGui and ImPlot
    m_context = ImGui::CreateContext();
    m_plotContext = ImPlot::CreateContext();

    ImGui::SetCurrentContext(m_context);
    ImPlot::SetCurrentContext(m_plotContext);

    // Setup custom style
    setupStyle();

    // Enable keyboard nav
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    m_capture = new ImGuiCapture(this);

    if (RtxOptions::useNewGuiInputMethod()) {
      m_overlayWin = new GameOverlay("RemixGuiInputSink", this);
    }
  }

  ImGUI::~ImGUI() {
    g_imguiTextureMap.clear();

    ImGui::SetCurrentContext(m_context);
    ImPlot::SetCurrentContext(m_plotContext);

    if(m_init) {
      ImGui_ImplWin32_Shutdown();
    }

    //add the destroy the imgui created structures
    if(m_imguiPool != VK_NULL_HANDLE)
      m_device->vkd()->vkDestroyDescriptorPool(m_device->handle(), m_imguiPool, nullptr);

    if (m_init) {
      // FontView and FontImage will be released by m_fontTextureView and m_fontTexture later
      ImGuiIO& io = ImGui::GetIO();
      ImGui_ImplDxvk::Data* bd = (ImGui_ImplDxvk::Data*) io.BackendRendererUserData;
      bd->FontView = VK_NULL_HANDLE;
      bd->FontImage = VK_NULL_HANDLE;

      ImGui_ImplDxvk::Shutdown();
      m_init = false;
    }

    // Destroy the ImGui and ImPlot context
    ImPlot::DestroyContext(m_plotContext);
    ImGui::DestroyContext(m_context);
  }
  
  void ImGUI::AddTexture(const XXH64_hash_t hash, const Rc<DxvkImageView>& imageView, uint32_t textureFeatureFlags) {
    if (g_imguiTextureMap.find(hash) == g_imguiTextureMap.end()) {
      ImGuiTexture texture;
      texture.imageView = imageView; // Hold a refcount
      texture.texID = VK_NULL_HANDLE;
      texture.textureFeatureFlags = textureFeatureFlags;
      g_imguiTextureMap[hash] = texture;
    }
  }

  void ImGUI::ReleaseTexture(const XXH64_hash_t hash) {
    if (RtxOptions::keepTexturesForTagging()) {
      return;
    }
    
    // Note: Erase will do nothing if the hash does not exist in the map, and erase it if it is.
    g_imguiTextureMap.erase(hash);
  }

  void ImGUI::SetFogStates(const fast_unordered_cache<FogState>& fogStates, XXH64_hash_t usedFogHash) {
    const std::lock_guard<std::mutex> lock(g_imguiFogMapMutex);
    g_imguiFogMap = fogStates;
    g_usedFogStateHash = usedFogHash;
  }

  void ImGUI::wndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (m_overlayWin.ptr() != nullptr) {
      m_overlayWin->gameWndProcHandler(hWnd, msg, wParam, lParam);
    } else {
      // Note this is the old method for grabbing keyboard/mouse inputs which relies on hooking
      //  the wndproc from the original game, and sending that data across the x86 -> x64 bridge.  
      //  We see compatibilities in older applications with this approach that are tricky to resolve.
      //  Favour the new approach `useNewGuiInputMethod` when possible.
      ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam);
    }
  }

  void ImGUI::showMemoryStats() const {
    if (RtxOptions::Automation::disableDisplayMemoryStatistics()) {
      return;
    }

    // Gather runtime vidmem stats
    VkDeviceSize vidmemSize = 0;
    VkDeviceSize vidmemUsedSize = 0;

    DxvkAdapterMemoryInfo memHeapInfo = m_device->adapter()->getMemoryHeapInfo();
    DxvkMemoryAllocator& memoryManager = m_device->getCommon()->memoryManager();
    const VkPhysicalDeviceMemoryProperties& memoryProperties = memoryManager.getMemoryProperties();

    for (uint32_t i = 0; i < memoryProperties.memoryHeapCount; i++) {
      if (memoryProperties.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) {
        vidmemSize += memHeapInfo.heaps[i].memoryBudget;
        vidmemUsedSize += memHeapInfo.heaps[i].memoryAllocated;
      }
    }

    // Calculate video memory information

    constexpr float bytesPerMebibyte = 1024.f * 1024.f;
    const VkDeviceSize vidmemFreeSize = vidmemSize - std::min(vidmemUsedSize, vidmemSize);
    const float vidmemTotalSizeMB = (float)((double) vidmemSize / bytesPerMebibyte);
    const float vidmemUsedSizeMB = (float)((double) vidmemUsedSize / bytesPerMebibyte);
    const float vidmemFreeSizeMB = (float)((double) vidmemFreeSize / bytesPerMebibyte);
    const float freeVidMemRatio = (float)std::min((double) vidmemFreeSize / (double) vidmemSize, 1.0);

    // Display video memory information

#ifdef REMIX_DEVELOPMENT
    ImGui::Text("Video Memory Usage: %.f MiB / %.f MiB (%.f MiB free)", vidmemUsedSizeMB, vidmemTotalSizeMB, vidmemFreeSizeMB);
#else
    // Note: Simplify for end users, free memory is usually not as important to list and can just be observed visually with the graph.
    ImGui::Text("Video Memory Usage: %.f MiB / %.f MiB", vidmemUsedSizeMB, vidmemTotalSizeMB);
#endif

    // Note: Map the range [0.1, 0.6] to [0, 1] and clamp outside it to bias and clamp the green->red color transition more.
    const float remappedFreeVidMemRatio = std::max(std::min(freeVidMemRatio + 0.4f, 1.0f) - 0.5f, 0.0f) * 2.0f;
    ImVec4 barColor = ImVec4{ 1.0f, 1.0f, 1.0f, 1.0f };

    ImGui::ColorConvertHSVtoRGB(
      remappedFreeVidMemRatio * 0.32f, 0.717f, 0.704f,
      barColor.x, barColor.y, barColor.z);

    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, barColor);
    ImGui::ProgressBar(vidmemUsedSizeMB / vidmemTotalSizeMB);
    ImGui::PopStyleColor();

    ImGui::TextWrapped("RTX Remix dynamically uses available VRAM to maximize texture quality.");
    
    ImGui::Dummy(ImVec2 { 4, 0 });
  }

  void ImGUI::switchMenu(UIType type, bool force) {
    UIType oldType = RtxOptions::showUI();
    if (oldType == type && !force) {
      return;
    }
    
    if (type == UIType::None) {
      onCloseMenus();
    } else {
      onOpenMenus();
    }

    {
      // Target user layer for UI state changes (this is a user preference)
      RtxOptionLayerTarget layerTarget(RtxOptionEditTarget::User);
      RtxOptions::showUI.setDeferred(type);
    }

    if (RtxOptions::blockInputToGameInUI()) {
      BridgeMessageChannel::get().send("UWM_REMIX_UIACTIVE_MSG",
                                       type != UIType::None ? 1 : 0, 0);
    }
  }
  
  void ImGUI::showMaterialOptions() {
    if (RemixGui::CollapsingHeader("Material Options (optional)", collapsingHeaderClosedFlags)) {
      ImGui::Indent();

      if (RemixGui::CollapsingHeader("Legacy Material Defaults", collapsingHeaderFlags)) {
        ImGui::Indent();

        RemixGui::Checkbox("Use Albedo/Opacity Texture (if present)", &LegacyMaterialDefaults::useAlbedoTextureIfPresentObject());
        RemixGui::Checkbox("Ignore Texture Alpha Channel", &LegacyMaterialDefaults::ignoreAlphaChannelObject());
        RemixGui::ColorEdit3("Albedo", &LegacyMaterialDefaults::albedoConstantObject());
        RemixGui::DragFloat("Opacity", &LegacyMaterialDefaults::opacityConstantObject(), 0.01f, 0.f, 1.f);
        RemixGui::ColorEdit3("Emissive Color", &LegacyMaterialDefaults::emissiveColorConstantObject());
        RemixGui::DragFloat("Emissive Intensity", &LegacyMaterialDefaults::emissiveIntensityObject(), 0.01f, 0.01f, FLT_MAX, "%.3f", sliderFlags);
        RemixGui::DragFloat("Roughness", &LegacyMaterialDefaults::roughnessConstantObject(), 0.01f, 0.02f, 1.f, "%.3f", sliderFlags);
        RemixGui::DragFloat("Metallic", &LegacyMaterialDefaults::metallicConstantObject(), 0.01f, 0.0f, 1.f, "%.3f", sliderFlags);
        RemixGui::DragFloat("Anisotropy", &LegacyMaterialDefaults::anisotropyObject(), 0.01f, -1.0f, 1.f, "%.3f", sliderFlags);

        ImGui::Unindent();
      }

      if (RemixGui::CollapsingHeader("PBR Material Modifiers", collapsingHeaderFlags)) {
        ImGui::Indent();

        if (RemixGui::CollapsingHeader("Opaque", collapsingHeaderFlags)) {
          ImGui::Indent();

          RemixGui::SliderFloat("Albedo Scale", &OpaqueMaterialOptions::albedoScaleObject(), 0.0f, 1.f, "%.3f", sliderFlags);
          RemixGui::SliderFloat("Albedo Bias", &OpaqueMaterialOptions::albedoBiasObject(), -1.0f, 1.f, "%.3f", sliderFlags);
          RemixGui::SliderFloat("Metallic Scale", &OpaqueMaterialOptions::metallicScaleObject(), 0.0f, 1.f, "%.3f", sliderFlags);
          RemixGui::SliderFloat("Metallic Bias", &OpaqueMaterialOptions::metallicBiasObject(), -1.0f, 1.f, "%.3f", sliderFlags);
          RemixGui::SliderFloat("Roughness Scale", &OpaqueMaterialOptions::roughnessScaleObject(), 0.0f, 1.f, "%.3f", sliderFlags);
          RemixGui::SliderFloat("Roughness Bias", &OpaqueMaterialOptions::roughnessBiasObject(), -1.0f, 1.f, "%.3f", sliderFlags);
          RemixGui::SliderFloat("Normal Strength##1", &OpaqueMaterialOptions::normalIntensityObject(), -10.0f, 10.f, "%.3f", sliderFlags);

          RemixGui::Checkbox("Enable dual-layer animated water normal for Opaque", &OpaqueMaterialOptions::layeredWaterNormalEnableObject());

          if (OpaqueMaterialOptions::layeredWaterNormalEnable()) {
            ImGui::TextWrapped("Animated water with Opaque material is dependent on the original draw call animating using a texture transform.");
            RemixGui::SliderFloat2("Layered Motion Direction", &OpaqueMaterialOptions::layeredWaterNormalMotionObject(), -1.0f, 1.0f, "%.3f", sliderFlags);
            RemixGui::SliderFloat("Layered Motion Scale", &OpaqueMaterialOptions::layeredWaterNormalMotionScaleObject(), -10.0f, 10.0f, "%.3f", sliderFlags);
            RemixGui::SliderFloat("LOD bias", &OpaqueMaterialOptions::layeredWaterNormalLodBiasObject(), 0.0f, 16.0f, "%.3f", sliderFlags);
          }

          ImGui::Unindent();
        }

        if (RemixGui::CollapsingHeader("Translucent", collapsingHeaderFlags)) {
          ImGui::Indent();

          RemixGui::SliderFloat("Transmit. Color Scale", &TranslucentMaterialOptions::transmittanceColorScaleObject(), 0.0f, 1.f, "%.3f", sliderFlags);
          RemixGui::SliderFloat("Transmit. Color Bias", &TranslucentMaterialOptions::transmittanceColorBiasObject(), -1.0f, 1.f, "%.3f", sliderFlags);
          RemixGui::SliderFloat("Normal Strength##2", &TranslucentMaterialOptions::normalIntensityObject(), -10.0f, 10.f, "%.3f", sliderFlags);

          RemixGui::Checkbox("Enable dual-layer animated water normal for Translucent", &TranslucentMaterialOptions::animatedWaterEnableObject());
          if (TranslucentMaterialOptions::animatedWaterEnable()) {
            ImGui::TextWrapped("Animated water with Translucent materials will animate using Remix animation time.");

            RemixGui::SliderFloat2("Primary Texcoord Velocity", &TranslucentMaterialOptions::animatedWaterPrimaryNormalMotionObject(), -0.5f, 0.5f, "%.3f", sliderFlags);
            RemixGui::SliderFloat2("Secondary Normal Texcoord Velocity", &TranslucentMaterialOptions::animatedWaterSecondaryNormalMotionObject(), -0.5f, 0.5f, "%.3f", sliderFlags);
            RemixGui::SliderFloat("Secondary Normal LOD bias", &TranslucentMaterialOptions::animatedWaterSecondaryNormalLodBiasObject(), 0.0f, 16.0f, "%.3f", sliderFlags);
          }
          ImGui::Unindent();
        }

        ImGui::Unindent();
      }

      if (RemixGui::CollapsingHeader("PBR Material Overrides", collapsingHeaderClosedFlags)) {
        ImGui::Indent();

        if (RemixGui::CollapsingHeader("Opaque", collapsingHeaderFlags)) {
          ImGui::Indent();

          RemixGui::Checkbox("Enable Thin-Film Layer", &OpaqueMaterialOptions::enableThinFilmOverrideObject());

          if (OpaqueMaterialOptions::enableThinFilmOverride()) {
            RemixGui::SliderFloat("Thin Film Thickness", &OpaqueMaterialOptions::thinFilmThicknessOverrideObject(), 0.0f, OPAQUE_SURFACE_MATERIAL_THIN_FILM_MAX_THICKNESS, "%.1f nm", sliderFlags);
          }

          ImGui::Unindent();
        }

        if (RemixGui::CollapsingHeader("Translucent", collapsingHeaderFlags)) {
          ImGui::Indent();

          RemixGui::Checkbox("Enable Diffuse Layer", &TranslucentMaterialOptions::enableDiffuseLayerOverrideObject());

          ImGui::Unindent();
        }

        ImGui::Unindent();
      }

      ImGui::Unindent();
    }
  }

  void ImGUI::processHotkeys() {
    auto& io = ImGui::GetIO();

    if (checkHotkeyState(RtxOptions::remixMenuKeyBinds())) {
      if(RtxOptions::defaultToAdvancedUI()) {
        switchMenu(RtxOptions::showUI() != UIType::None ? UIType::None : UIType::Advanced);
      } else {
        switchMenu(RtxOptions::showUI() != UIType::None ? UIType::None : UIType::Basic);
      }
    }


    // Toggle ImGUI mouse cursor. Alt-Del
    if (io.KeyAlt && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Delete))) {
      RtxOptions::showUICursor.setDeferred(!RtxOptions::showUICursor());
    }

    // Toggle input blocking. Alt-Backspace
    if (io.KeyAlt && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Backspace))) {
      RtxOptions::blockInputToGameInUI.setDeferred(!RtxOptions::blockInputToGameInUI());
    }
  }

  void ImGUI::update(const Rc<DxvkContext>& ctx) {
    ImGui_ImplDxvk::NewFrame();
    ImGui_ImplWin32_NewFrame();

    ImGui::NewFrame();

    processHotkeys();
    updateQuickActions(ctx);

    m_splash->update(m_largeFont);

    m_about->update(ctx);
    
    m_capture->update(ctx);

    showDebugVisualizations(ctx);

    const auto showUI = RtxOptions::showUI();
    if (showUI == UIType::Advanced) {
      showMainMenu(ctx);

      // Uncomment to see the ImGUI demo, good reference!  Also, need to undefine IMGUI_DISABLE_DEMO_WINDOWS (in "imconfig.h")
      //ImGui::ShowDemoWindow();
    } else if (showUI == UIType::Basic) {
      showUserMenu(ctx);
    }
    
    // Render any blocked edit popup warnings
    RemixGui::RenderRtxOptionBlockedEditPopup();

    // Note: Only display the latency stats window when the Advanced UI is active as the Basic UI acts as a modal which blocks other
    // windows from being interacted with.
    if (showUI == UIType::Advanced && m_reflexLatencyStatsOpen) {
      showReflexLatencyStats();
    }

    if (showUI == UIType::None) {
      ImGui::CloseCurrentPopup();
      ImGui::GetIO().MouseDrawCursor = false;
    } else {
      if (RtxOptions::showUICursor()) {
        ImGui::GetIO().MouseDrawCursor = true;
        // Force display counter into invisible state
        while (ShowCursor(FALSE) >= 0) { }
      } else {
        // Force display counter into visible state
        while (ShowCursor(TRUE) < 0) {  }
      }
    }

    showHudMessages(ctx);

#ifdef REMIX_DEVELOPMENT
    // Show visual indicator when crash hotkey is armed
    if (RtxOptions::enableCrashHotkey()) {
      const auto crashHotkeyStr = buildKeyBindDescriptorString(RtxOptions::crashHotkey());
      const auto warningText = str::format("!! CRASH HOTKEY ARMED (", crashHotkeyStr, ") !!");
      const ImVec2 textSize = ImGui::CalcTextSize(warningText.c_str());
      const ImGuiViewport* viewport = ImGui::GetMainViewport();
      const ImVec2 textPos(viewport->Size.x - textSize.x - 10.0f, 10.0f);
      ImGui::GetForegroundDrawList()->AddText(textPos, IM_COL32(255, 50, 50, 255), warningText.c_str());
    }
#endif

    ImGui::Render();
  }

  void ImGUI::updateQuickActions(const Rc<DxvkContext>& ctx) {
#ifdef REMIX_DEVELOPMENT
    enum RtxQuickAction : uint32_t {
      kOriginal = 0,
      kRtxOnEnhanced,
      kRtxOn,
      kCount
    };

    auto common = ctx->getCommonObjects();
    static RtxQuickAction sQuickAction = common->getSceneManager().areAllReplacementsLoaded() ? RtxQuickAction::kRtxOnEnhanced : RtxQuickAction::kRtxOn;

    if (ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_KeypadAdd))) {
      sQuickAction = (RtxQuickAction) ((sQuickAction + 1) % RtxQuickAction::kCount);

      // Skip over the enhancements quick option if no replacements are loaded
      if(!common->getSceneManager().areAllReplacementsLoaded() && sQuickAction == RtxQuickAction::kRtxOnEnhanced)
        sQuickAction = (RtxQuickAction) ((sQuickAction + 1) % RtxQuickAction::kCount);

      switch (sQuickAction) {
      case RtxQuickAction::kOriginal:
        RtxOptions::enableRaytracing.setDeferred(false);
        RtxOptions::enableReplacementLights.setDeferred(false);
        RtxOptions::enableReplacementMaterials.setDeferred(false);
        RtxOptions::enableReplacementMeshes.setDeferred(false);
        break;
      case RtxQuickAction::kRtxOnEnhanced:
        RtxOptions::enableRaytracing.setDeferred(true);
        RtxOptions::enableReplacementLights.setDeferred(true);
        RtxOptions::enableReplacementMaterials.setDeferred(true);
        RtxOptions::enableReplacementMeshes.setDeferred(true);
        break;
      case RtxQuickAction::kRtxOn:
        RtxOptions::enableRaytracing.setDeferred(true);
        RtxOptions::enableReplacementLights.setDeferred(false);
        RtxOptions::enableReplacementMaterials.setDeferred(false);
        RtxOptions::enableReplacementMeshes.setDeferred(false);
        break;
      case RtxQuickAction::kCount:
        assert(false && "invalid RtxQuickAction::kCount in ImGUI::updateQuickActions");
        break;
      }
    }
#endif
  }


  void ImGUI::showDebugVisualizations(const Rc<DxvkContext>& ctx) {
    auto common = ctx->getCommonObjects();
    common->getSceneManager().getLightManager().showImguiDebugVisualization();
  }

  void ImGUI::showMainMenu(const Rc<DxvkContext>& ctx) {
    // Target rtx.conf layer for developer menu changes
    RtxOptionLayerTarget layerTarget(RtxOptionEditTarget::User);

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(m_windowOnRight ? viewport->Size.x - m_windowWidth : 0.f, viewport->Pos.y));
    ImGui::SetNextWindowSize(ImVec2(m_windowWidth, viewport->Size.y));

    // Remember switch state first, the switch UI when the curent window is finished.
    int switchUI = -1;
    bool advancedMenuOpen = RtxOptions::showUI() == UIType::Advanced;

    if (ImGui::Begin("RTX Remix Developer Menu", &advancedMenuOpen, windowFlags)) {
      // Begin handles window resize so this is fine. Do not set m_windowWidth after tabs so that tabs can modify the width
      m_windowWidth = ImGui::GetWindowWidth();

      if (ImGui::Button("Graphics Settings Menu", ImVec2(ImGui::GetContentRegionAvail().x * 0.5f, 0))) {
        switchUI = (int) UIType::Basic;
      }

      ImGui::SameLine();
      RemixGui::Checkbox("Default Menu", &RtxOptions::defaultToAdvancedUIObject());
      
      RemixGui::Separator();

      const static ImGuiTabBarFlags tab_bar_flags = ImGuiTabBarFlags_Reorderable | ImGuiTabBarFlags_NoCloseWithMiddleMouseButton;
      const static ImGuiTabItemFlags tab_item_flags = ImGuiTabItemFlags_NoCloseWithMiddleMouseButton;

      // Tab Bar
      if (ImGui::BeginTabBar("Developer Tabs", tab_bar_flags)) {
        for (int n = 0; n < kTab_Count; n++) {
          auto tabItemFlags = tab_item_flags;
          if(n == m_triggerTab) {
            tabItemFlags |= ImGuiTabItemFlags_SetSelected;
            m_triggerTab = kTab_Count;
          }
          if (ImGui::BeginTabItem(tabNames[n], nullptr, tabItemFlags)) {
            const Tabs tab = (Tabs) n;
            switch (tab) {
            case kTab_Rendering:
              showRenderingSettings(ctx);
              break;
            case kTab_Setup:
              showSetupWindow(ctx);
              break;
            case kTab_Enhancements:
              showEnhancementsWindow(ctx);
              break;
            case kTab_About:
              m_about->show(ctx);
              break;
            case kTab_Development:
              showDevelopmentSettings(ctx);
              break;
            case kTab_Count:
              assert(false && "kTab_Count hit in ImGUI::showMainMenu");
              break;
            }
            m_curTab = tab;
            ImGui::EndTabItem();
          }
        }

        if (ImGui::TabItemButton(m_windowOnRight ? "<<" : ">>")) {
          m_windowOnRight = !m_windowOnRight;
        }

        ImGui::EndTabBar();
      }
    }

    ImGui::Dummy(ImVec2(0, 2));
    RemixGui::Separator();
    ImGui::Dummy(ImVec2(0, 2));

    // Get layer pointers and check for unsaved changes
    RtxOptionLayer* rtxConfLayer = RtxOptionLayer::getRtxConfLayer();
    RtxOptionLayer* userLayer = const_cast<RtxOptionLayer*>(RtxOptionLayer::getUserLayer());
    const bool rtxHasUnsaved = rtxConfLayer && rtxConfLayer->hasUnsavedChanges();
    const bool userHasUnsaved = userLayer && userLayer->hasUnsavedChanges();
    
    // ============================================================================
    // Settings Management Section
    // ============================================================================
    if (RemixGui::CollapsingHeader("Settings Management", ImGuiTreeNodeFlags_DefaultOpen)) {

      // --- User Config Layer (higher priority, shown first) ---
      ImGui::Text("User Settings (user.conf):");
      if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Your personal preferences. Saved to `user.conf`.\n"
          "This includes graphics preset choices, direct graphics settings,\n"
          "and other per-user preferences like UI configuration.");
      }
      if (userHasUnsaved) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.0f, 1.0f), "(unsaved changes)");
        if (ImGui::IsItemHovered()) {
          ImGui::SetTooltip("Changes have been made since the user.conf file was last saved.");
        }
      }

      // Show unsaved changes in a CollapsingHeader
      if (userHasUnsaved && userLayer) {
        if (RemixGui::CollapsingHeader("View Changes##User")) {
          ImGui::Indent();
          OptionLayerUI::RenderOptions renderOpts;
          renderOpts.uniqueId = "##UserLayerList";
          OptionLayerUI::renderToImGui(userLayer, renderOpts);
          ImGui::Unindent();
        }
      }
      
      OptionLayerUI::renderLayerButtons(userLayer, "User");

      ImGui::Separator();

      // --- Migration: Move miscategorized options from user.conf to rtx.conf ---
      // Get cached count of options in user.conf that don't have UserSetting flag
      const uint32_t userMiscategorizedCount = userLayer ? userLayer->countMiscategorizedOptions() : 0;

      if (userMiscategorizedCount > 0) {
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.85f, 0.65f, 0.0f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.95f, 0.75f, 0.1f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.75f, 0.55f, 0.0f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
        
        std::string buttonLabel = str::format("Migrate ", userMiscategorizedCount, " Developer Setting", (userMiscategorizedCount > 1 ? "s" : ""), " to rtx.conf");
        if (ImGui::Button(buttonLabel.c_str(), ImVec2(-1, 0))) {
          const uint32_t migratedCount = userLayer->migrateMiscategorizedOptions();
          Logger::info(str::format("[RTX Option]: Migrated ", migratedCount, " developer settings from user.conf to rtx.conf"));
        }
        
        ImGui::PopStyleColor(4);

        // Tooltip needs to come after the PopStyleColor button to avoid having black text on a dark background
        if (ImGui::IsItemHovered()) {
          ImGui::SetTooltip("The user.conf file should only contain end user options (preferences and graphics quality settings).\n\n"
                            "This button will move all other settings from user.conf to rtx.conf.\n"
                            "It does not save the changes.");
        }
      }

      ImGui::Spacing();
      ImGui::Spacing();
      ImGui::Spacing();

      // --- RTX Config Layer ---
      ImGui::Text("Remix Config (rtx.conf):");
      if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("The main place where Remix configuration is stored.\n"
          "Saves to rtx.conf. This should be where mod\n"
          "developers configure game-specific settings.");
      }
      if (rtxHasUnsaved) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.0f, 1.0f), "(unsaved changes)");
        if (ImGui::IsItemHovered()) {
          ImGui::SetTooltip("Changes have been made since the rtx.conf file was last saved.");
        }
      }

      // Show unsaved changes in a CollapsingHeader
      if (rtxHasUnsaved && rtxConfLayer) {
        if (RemixGui::CollapsingHeader("View Changes##RtxConf")) {
          ImGui::Indent();
          OptionLayerUI::RenderOptions renderOpts;
          renderOpts.uniqueId = "##RtxConfLayerList";
          OptionLayerUI::renderToImGui(rtxConfLayer, renderOpts);
          ImGui::Unindent();
        }
      }
      
      OptionLayerUI::renderLayerButtons(rtxConfLayer, "RtxConf");

      // --- Migration: Move miscategorized options from rtx.conf to user.conf ---
      const uint32_t rtxMiscategorizedCount = rtxConfLayer ? rtxConfLayer->countMiscategorizedOptions() : 0;
      if (rtxMiscategorizedCount > 0) {
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.85f, 0.65f, 0.0f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.95f, 0.75f, 0.1f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.75f, 0.55f, 0.0f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
        
        std::string buttonLabel = str::format("Migrate ", rtxMiscategorizedCount, " User Setting", (rtxMiscategorizedCount > 1 ? "s" : ""), " to user.conf");
        if (ImGui::Button(buttonLabel.c_str(), ImVec2(-1, 0))) {
          const uint32_t migratedCount = rtxConfLayer->migrateMiscategorizedOptions();
          Logger::info(str::format("[RTX Option]: Migrated ", migratedCount, " user settings from rtx.conf to user.conf"));
        }
        
        ImGui::PopStyleColor(4);

        // Tooltip needs to come after the PopStyleColor button to avoid having black text on a dark background
        if (ImGui::IsItemHovered()) {
          ImGui::SetTooltip("The rtx.conf file contains settings that should be in user.conf (end user options that should not be in mods).\n\n"
                            "This button will move these settings from rtx.conf to user.conf.\n"
                            "It does not save the changes.");
        }
      }

      ImGui::Spacing();
      ImGui::Separator();
      ImGui::Spacing();

      // --- Create .conf file for Logic ---
      ImGui::Text("Create .conf file for Logic:");
      ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "(Only unsaved Remix Config changes will be exported)");
      static char exportFileName[512] = "exported_rtx.conf";
      ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 100);
      ImGui::InputText("##ExportFileName", exportFileName, IM_ARRAYSIZE(exportFileName));
      ImGui::SameLine();
      ImGui::BeginDisabled(!rtxHasUnsaved);
      if (ImGui::Button("Create", ImVec2(-1, 0))) {
        if (rtxConfLayer) {
          std::string exportPath(exportFileName);
          rtxConfLayer->exportUnsavedChanges(exportPath);
        }
      }
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        if (rtxHasUnsaved) {
          ImGui::SetTooltip("Create a .conf file containing only the unsaved changes from rtx.conf.\nIf the file already exists, changes will be merged into it.");
        } else {
          ImGui::SetTooltip("No unsaved changes in rtx.conf to export.");
        }
      }
      ImGui::EndDisabled();

    }

    ImGui::Spacing();

    // --- Bottom Buttons ---
    const float buttonWidth = ImGui::GetContentRegionAvail().x / 2 - (ImGui::GetStyle().ItemSpacing.x / 2);
    const bool anyUnsavedChanges = rtxHasUnsaved || userHasUnsaved;
    ImGui::BeginDisabled(!anyUnsavedChanges);
    if (ImGui::Button("Revert All Unsaved Changes", ImVec2(buttonWidth, 0))) {
      // Reload all layers that have unsaved changes
      if (rtxConfLayer && rtxConfLayer->hasUnsavedChanges()) {
        rtxConfLayer->reload();
      }
      if (userLayer && userLayer->hasUnsavedChanges()) {
        userLayer->reload();
      }
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
      if (anyUnsavedChanges) {
        ImGui::SetTooltip("Reload rtx.conf and user.conf from disk,\ndiscarding all unsaved changes in both layers.");
      } else {
        ImGui::SetTooltip("No unsaved changes to revert.");
      }
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    if (ImGui::Button("Hide UI", ImVec2(buttonWidth, 0))) {
      switchUI = (int) UIType::None;
    }

    RemixGui::TextCentered("[Alt + Del] Toggle cursor        [Alt + Backspace] Toggle game input");
    ImGui::End();

    // Close via titlebar close button
    if (!advancedMenuOpen) {
      switchUI = (int) UIType::None;
    }

    if (switchUI >= 0) {
      switchMenu((UIType) switchUI);
    }
  }

  struct HudMessage {
    HudMessage(const std::string& text, const std::optional<std::string>& subText) : text{ text }, subText{ subText } {}

    std::string text;
    std::optional<std::string> subText;
  };

  void ImGUI::showHudMessages(const Rc<DxvkContext>& ctx) {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    auto common = ctx->getCommonObjects();
    const auto& pipelineManager = common->pipelineManager();

    std::vector<HudMessage> hudMessages;

    // Add Shader Compilation HUD messages

    uint32_t asyncShaderCompilationCount = 0;
    if (RtxOptions::Shader::enableAsyncCompilation()) {
      asyncShaderCompilationCount = pipelineManager.remixShaderCompilationCount();
    }

    if (RtxOptions::Shader::enableAsyncCompilationUI() && asyncShaderCompilationCount > 0) {
      const auto compilationText = str::format("Compiling shaders (", asyncShaderCompilationCount, " remaining)");

      hudMessages.emplace_back(std::move(compilationText), "This may take some time if shaders are not cached yet.\nRemix will not render properly until compilation is finished.");
    }

    // Add Enhancement Loading HUD messages

    const auto replacementStates = common->getSceneManager().getReplacementStates();
    std::string replacementLoadingSubtext;
    std::uint32_t loadingReplacementStateCount{ 0U };

    for (std::size_t i{ 0U }; i < replacementStates.size(); ++i) {
      auto&& replacementState = replacementStates[i];

      // Add a newline when reporting on more than one mod in a loading state

      if (loadingReplacementStateCount != 0) {
        replacementLoadingSubtext += '\n';
      }

      // Hide individual mod progress messages beyond a requested amount
      // Note: This ensures if for some reason there are a significant amount of mods in place that the screen will not be filled with progress hud messages.

      constexpr std::size_t maxModProgressCount{ 4 };

      if (loadingReplacementStateCount >= maxModProgressCount) {
        replacementLoadingSubtext += str::format(replacementStates.size() - maxModProgressCount, " more hidden...");

        break;
      }

      // Set the progress message if the mod is in a loading state and increment the number of currently loading mods

      switch (replacementState.progressState) {
      case Mod::ProgressState::OpeningUSD: replacementLoadingSubtext += str::format("Opening USD"); break;
      case Mod::ProgressState::ProcessingMaterials: replacementLoadingSubtext += str::format("Processing Materials (", replacementState.progressCount, " processed)"); break;
      case Mod::ProgressState::ProcessingMeshes: replacementLoadingSubtext += str::format("Processing Meshes (", replacementState.progressCount, " processed)"); break;
      case Mod::ProgressState::ProcessingLights: replacementLoadingSubtext += str::format("Processing Lights (", replacementState.progressCount, " processed)"); break;
      default: break;
      }

      if (
        replacementState.progressState == Mod::ProgressState::OpeningUSD ||
        replacementState.progressState == Mod::ProgressState::ProcessingMaterials ||
        replacementState.progressState == Mod::ProgressState::ProcessingMeshes ||
        replacementState.progressState == Mod::ProgressState::ProcessingLights
      ) {
        ++loadingReplacementStateCount;
      }
    }

    assert((loadingReplacementStateCount == 0U) == replacementLoadingSubtext.empty());

    if (loadingReplacementStateCount != 0U) {
      hudMessages.emplace_back("Loading enhancements", replacementLoadingSubtext);
    }

    // Draw Hud Messages

    if (!hudMessages.empty()) {
      // Reset Hud Message time if needed
      // Note: This is done to minimize any potential precision issues if the game is left running for a long time.
      // Not the best solution ever, ideally just accumulating time with delta time passed in would probably be better
      // rather than querying the OS for timestamps, but getting delta time in the ImGui system is rather annoying, so
      // this is fine for now as this code isn't performance-critical anyways.

      const auto currentTime = std::chrono::steady_clock::now();

      if (!m_hudMessageTimeReset) {
        m_hudMessageStartTime = currentTime;
        m_hudMessageTimeReset = true;
      }

      // Calculate the length of the animated dot sequence based on the current time

      const auto hudMessageDisplayDuration{ currentTime - m_hudMessageStartTime };
      const auto hudMessageDisplayMilliseconds{
        std::chrono::duration_cast<std::chrono::milliseconds>(hudMessageDisplayDuration).count()
      };
      // Note: Generates a looping set of values in the range [1, 3] based on the time and the duration of each dot.
      const auto dotSequenceLength{ (hudMessageDisplayMilliseconds / hudMessageAnimatedDotDurationMilliseconds()) % 3 + 1 };

      ImGui::SetNextWindowPos(ImVec2(0, viewport->Size.y), ImGuiCond_Always, ImVec2(0.0f, 1.0f));
      // Note: 368 pixels chosen as a minimum width for the message box width to ensure the current length of text has enough space
      // to render an animated dot sequence without causing the width of the window to change, as this is visually distracting.
      // If longer message box text fields are ever desired than the current ones, this number will have to be updated.
      // Hack: Currently ImGui does not properly respect the window size constraints when ImGuiWindowFlags_AlwaysAutoResize is set.
      // This call should be using the size constraints (368, -1), (-1, -1) as -1 indicates "don't care" (and we only care about setting
      // a minimum width), but for some reason that does not work as reported by this bug: https://github.com/ocornut/imgui/issues/2629
      ImGui::SetNextWindowSizeConstraints(ImVec2(368.0f, 0.0f), ImVec2(1000.0f, 1000.0f));
      ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.5f, 0.2f, 0.2f, 0.35f));

      const ImGuiWindowFlags hud_flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove;
      if (ImGui::Begin("HUD", nullptr, hud_flags)) {
        for (std::size_t i{ 0U }; i < hudMessages.size(); ++i) {
          auto&& message{ hudMessages[i] };

          // Append the animated dot sequence to the message main text

          constexpr auto dotSequence{ "..." };
          std::string animatedMessageText{ message.text };

          animatedMessageText.append(dotSequence, dotSequenceLength);

          // Add a large main text and smaller sub text for each message

          ImGui::PushFont(m_largeFont);
          ImGui::Text(animatedMessageText.c_str());
          ImGui::PopFont();

          if (message.subText) {
            ImGui::Text(message.subText->c_str());
          }

          // Add a seperator between messages

          if (i != hudMessages.size() - 1) {
            RemixGui::Separator();
          }
        }
      }

      ImGui::PopStyleColor();
      ImGui::End();
    } else {
      // Note: Indicate that the Hud Message time will need to be reset the next time it is used.
      m_hudMessageTimeReset = false;
    }
  }

  void ImGUI::showDevelopmentSettings(const Rc<DxvkContext>& ctx) {
    ImGui::PushItemWidth((largeUiMode() ? m_largeWindowWidgetWidth : m_regularWindowWidgetWidth) + 50.0f);
    if (ImGui::Button("Take Screenshot")) {
      RtxContext::triggerScreenshot();
    }

    RemixGui::SetTooltipToLastWidgetOnHover("Screenshot will be dumped to, '<exe-dir>/Screenshots'");

    ImGui::SameLine(200.f);
    RemixGui::Checkbox("Include G-Buffer", &RtxOptions::captureDebugImageObject());

    RemixGui::Separator();
        
#ifdef REMIX_DEVELOPMENT
    { // Recompile Shaders button and its status information (Only available for Development Remix builds)
      const auto& shaderManager{ ShaderManager::getInstance() };
      const auto shaderReloadPhase{ shaderManager->getShaderReloadPhase() };
      const auto lastShaderReloadStatus{ shaderManager->getLastShaderReloadStatus() };

      // Note: Only allow the Recompile Shaders button to function if a shader recompile is not currently in progress (be
      // it one manually initiated by the user, or something automatic from the live shader edit mode).
      ImGui::BeginDisabled(shaderReloadPhase != ShaderManager::ShaderReloadPhase::Idle);

      if (ImGui::Button("Recompile Shaders")) {
        shaderManager->requestReloadShaders();
      }

      ImGui::EndDisabled();

      ImGui::SameLine(200.f);
      RemixGui::Checkbox("Live shader edit mode", &RtxOptions::Shader::useLiveEditModeObject());

      const char* shaderReloadPhaseText;
      const char* lastShaderReloadStatusText;
      ImVec4 shaderReloadPhaseTextColor;
      ImVec4 lastShaderReloadStatusTextColor;

      switch (shaderReloadPhase) {
      default: assert(false); [[fallthrough]];
      case ShaderManager::ShaderReloadPhase::Idle:
        shaderReloadPhaseText = "Idle";
        shaderReloadPhaseTextColor = ImVec4(0.5f, 0.5f, 0.5f, 1.0f);
        break;
      case ShaderManager::ShaderReloadPhase::SPIRVRecompilation:
        shaderReloadPhaseText = "Working (SPIR-V Recompilation)";
        shaderReloadPhaseTextColor = ImVec4(0.73f, 0.87f, 0.54f, 1.0f);
        break;
      case ShaderManager::ShaderReloadPhase::ShaderRecreation:
        shaderReloadPhaseText = "Working (Shader Recreation)";
        shaderReloadPhaseTextColor = ImVec4(0.73f, 0.87f, 0.54f, 1.0f);
        break;
      }

      switch (lastShaderReloadStatus) {
      default: assert(false); [[fallthrough]];
      case ShaderManager::ShaderReloadStatus::Unknown:
        lastShaderReloadStatusText = "N/A";
        lastShaderReloadStatusTextColor = ImVec4(0.5f, 0.5f, 0.5f, 1.0f);
        break;
      case ShaderManager::ShaderReloadStatus::Failure:
        lastShaderReloadStatusText = "Failure";
        lastShaderReloadStatusTextColor = ImVec4(0.83f, 0.32f, 0.32f, 1.0f);
        break;
      case ShaderManager::ShaderReloadStatus::Success:
        lastShaderReloadStatusText = "Success";
        lastShaderReloadStatusTextColor = ImVec4(0.44f, 0.81f, 0.42f, 1.0f);
        break;
      }

      ImGui::TextUnformatted("Shader Reload Phase:");
      ImGui::SameLine();
      ImGui::PushStyleColor(ImGuiCol_Text, shaderReloadPhaseTextColor);
      ImGui::TextUnformatted(shaderReloadPhaseText);
      ImGui::PopStyleColor();

      ImGui::TextUnformatted("Last Shader Reload Status:");
      ImGui::SameLine();
      ImGui::PushStyleColor(ImGuiCol_Text, lastShaderReloadStatusTextColor);
      ImGui::TextUnformatted(lastShaderReloadStatusText);
      ImGui::PopStyleColor();
    }

    ImGui::Separator();

    { // Crash Hotkey Feature - allows triggering a deliberate crash for testing crash handling
      const bool isArmed = RtxOptions::enableCrashHotkey();
      
      // Use warning color when armed to make it visually distinct
      if (isArmed) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
      }
      
      // ImGui::Checkbox returns true when the checkbox state changes
      const bool changed = RemixGui::Checkbox("Arm Crash Hotkey", &RtxOptions::enableCrashHotkeyObject());
      
      if (isArmed) {
        ImGui::PopStyleColor();
      }
      
      const auto crashHotkeyStr = buildKeyBindDescriptorString(RtxOptions::crashHotkey());
      RemixGui::SetTooltipToLastWidgetOnHover(
        str::format("When armed, pressing ", crashHotkeyStr, " will trigger a deliberate crash.\n"
        "Useful for testing crash handling, crash dumps, and crash reporting.\n"
        "A red warning indicator will appear on screen while armed.").c_str());
      
      // Log state changes for crash dump analysis
      if (changed) {
        const bool nowArmed = RtxOptions::enableCrashHotkey();
        if (nowArmed) {
          Logger::warn(str::format("Crash hotkey ARMED - press ", crashHotkeyStr, " to trigger crash"));
        } else {
          Logger::warn("Crash hotkey disarmed");
        }
      }
    }
#endif

    RemixGui::Separator();

    showVsyncOptions(false);

    // Render GUI for memory profiler here
    GpuMemoryTracker::renderGui();

    if (RemixGui::CollapsingHeader("Camera", collapsingHeaderFlags)) {
      ImGui::Indent();

      RtCamera::showImguiSettings();

      {
        ImGui::PushID("CameraInfos");
        auto& cameraManager = ctx->getCommonObjects()->getSceneManager().getCameraManager();
        if (RemixGui::CollapsingHeader("Types", collapsingHeaderClosedFlags)) {
          ImGui::Indent();
          constexpr static std::pair<CameraType::Enum, const char*> cameras[] = {
            { CameraType::Main,             "Main" },
            { CameraType::ViewModel,        "ViewModel" },
            { CameraType::Portal0,          "Portal0" },
            { CameraType::Portal1,          "Portal1" },
            { CameraType::Sky,              "Sky" },
            { CameraType::RenderToTexture,  "RenderToTexture" },
          };
          // C++20: should be static_assert with std::ranges::find_if
          assert(
            std::find_if(
              std::begin(cameras),
              std::end(cameras),
              [](const auto& p) { return p.first == CameraType::Unknown; })
            == std::end(cameras));
          static_assert(std::size(cameras) == CameraType::Count - 1);

          static auto printCamera = [](const char* name, const RtCamera* c) {
            if (RemixGui::CollapsingHeader(name, collapsingHeaderFlags)) {
              ImGui::Indent();
              if (c) {
                ImGui::Text("Position: %.2f %.2f %.2f", c->getPosition().x, c->getPosition().y, c->getPosition().z);
                ImGui::Text("Direction: %.2f %.2f %.2f", c->getDirection().x, c->getDirection().y, c->getDirection().z);
                ImGui::Text("Vertical FOV: %.1f", c->getFov() * kRadiansToDegrees);
                ImGui::Text("Near / Far plane: %.1f / %.1f", c->getNearPlane(), c->getFarPlane());
                ImGui::Text("Projection Handedness: %s", c->isLHS() ? "Left-handed" : "Right-handed");
                ImGui::Text("Overall Handedness: %s", c->isLHS() ^ isMirrorTransform(c->getViewToWorld(false))   ? "Left-handed" : "Right-handed");
              } else {
                ImGui::Text("Position: -");
                ImGui::Text("Direction: -");
                ImGui::Text("Vertical FOV: -");
                ImGui::Text("Near / Far plane: -");
                ImGui::Text("-");
                ImGui::Text("-");
              }
              ImGui::Unindent();
            }
          };

          for (const auto& [type, name] : cameras) {
            printCamera(name, cameraManager.isCameraValid(type) ? &cameraManager.getCamera(type) : nullptr);
          }
          ImGui::Unindent();
        }
        ImGui::PopID();
      }

      if (RemixGui::CollapsingHeader("Camera Animation", collapsingHeaderClosedFlags)) {
        RemixGui::Checkbox("Animate Camera", &RtxOptions::shakeCameraObject());
        cameraAnimationModeCombo.getKey(&RtxOptions::cameraAnimationModeObject());
        RemixGui::DragFloat("Animation Amplitude", &RtxOptions::cameraAnimationAmplitudeObject(), 0.1f, 0.f, 1000.f, "%.2f", sliderFlags);
        RemixGui::DragInt("Shake Period", &RtxOptions::cameraShakePeriodObject(), 0.1f, 1, 100, "%d", sliderFlags);
      }

      if (RemixGui::CollapsingHeader("Advanced", collapsingHeaderClosedFlags)) {

        RemixGui::Checkbox("Portals: Camera History Correction", &RtxOptions::rayPortalCameraHistoryCorrectionObject());
        RemixGui::Checkbox("Portals: Camera In-Between Portals Correction", &RtxOptions::rayPortalCameraInBetweenPortalsCorrectionObject());

        if (RtxOptions::rayPortalCameraInBetweenPortalsCorrection()) {
          ImGui::Indent();

          RemixGui::DragFloat("Portals: Camera In-Between Portals Correction Threshold", &RtxOptions::rayPortalCameraInBetweenPortalsCorrectionThresholdObject(), 0.01f, 0.0f, FLT_MAX, "%.3f", sliderFlags);

          ImGui::Unindent();
        }

        RemixGui::Checkbox("Skip Objects Rendered with Unknown Camera", &RtxOptions::skipObjectsWithUnknownCameraObject());

        RemixGui::Checkbox("Override Near Plane (if less than original)", &RtxOptions::enableNearPlaneOverrideObject());
        ImGui::BeginDisabled(!RtxOptions::enableNearPlaneOverride());
        RemixGui::DragFloat("Desired Near Plane Distance", &RtxOptions::nearPlaneOverrideObject(), 0.01f, 0.0001f, FLT_MAX, "%.3f");
        ImGui::EndDisabled();
      }
      ImGui::Unindent();
    }

    if (RemixGui::CollapsingHeader("Camera Sequence", collapsingHeaderClosedFlags)) {
      ImGui::Indent();
      RtCameraSequence::getInstance()->showImguiSettings();
      ImGui::Unindent();
    }

    if (RemixGui::CollapsingHeader("Developer Options", collapsingHeaderFlags)) {
      ImGui::Indent();
      RemixGui::Checkbox("Enable Instance Debugging", &RtxOptions::enableInstanceDebuggingToolsObject());
      RemixGui::Checkbox("Disable Draw Calls Post RTX Injection", &RtxOptions::skipDrawCallsPostRTXInjectionObject());
      RemixGui::Checkbox("Break into Debugger On Press of Key 'B'", &RtxOptions::enableBreakIntoDebuggerOnPressingBObject());
      RemixGui::Checkbox("Block Input to Game in UI", &RtxOptions::blockInputToGameInUIObject());
      RemixGui::Checkbox("Force Camera Jitter", &RtxOptions::forceCameraJitterObject());
      RemixGui::DragInt("Camera Jitter Sequence Length", &RtxOptions::cameraJitterSequenceLengthObject());
      
      RemixGui::DragIntRange2("Draw Call Range Filter", &RtxOptions::drawCallRangeObject(), 1.f, 0, INT32_MAX, nullptr, nullptr, ImGuiSliderFlags_AlwaysClamp);
      RemixGui::InputInt("Instance Index Start", &RtxOptions::instanceOverrideInstanceIdxObject());
      RemixGui::InputInt("Instance Index Range", &RtxOptions::instanceOverrideInstanceIdxRangeObject());
      RemixGui::DragFloat3("Instance World Offset", &RtxOptions::instanceOverrideWorldOffsetObject(), 0.1f, -100.f, 100.f, "%.3f", sliderFlags);
      RemixGui::Checkbox("Instance - Print Hash", &RtxOptions::instanceOverrideSelectedInstancePrintMaterialHashObject());

      ImGui::Unindent();
      RemixGui::Checkbox("Throttle presents", &RtxOptions::enablePresentThrottleObject());
      if (RtxOptions::enablePresentThrottle()) {
        ImGui::Indent();
        RemixGui::SliderInt("Present delay", &RtxOptions::presentThrottleDelayObject(), 1, 1000, "%d ms", sliderFlags);
        ImGui::Unindent();
      }
      RemixGui::Checkbox("Hash Collision Detection", &HashCollisionDetectionOptions::enableObject());
      RemixGui::Checkbox("Validate CPU index data", &RtxOptions::validateCPUIndexDataObject());

#ifdef REMIX_DEVELOPMENT
      if (RemixGui::CollapsingHeader("Resource Aliasing Query", collapsingHeaderClosedFlags)) {
        ImGui::Indent();
        aliasingBeginPassCombo.getKey(&RtxOptions::Aliasing::beginPassObject());
        aliasingEndPassCombo.getKey(&RtxOptions::Aliasing::endPassObject());
        aliasingFormatCombo.getKey(&RtxOptions::Aliasing::formatCategoryObject());
        aliasingExtentCombo.getKey(&RtxOptions::Aliasing::extentTypeObject());
        const auto aliasingExtentType = RtxOptions::Aliasing::extentType();
        if (aliasingExtentType == RtxTextureExtentType::Custom) {
          RemixGui::DragInt("Aliasing Width", &RtxOptions::Aliasing::widthObject());
          RemixGui::DragInt("Aliasing Height", &RtxOptions::Aliasing::heightObject());
        }
        if (RtxOptions::Aliasing::imageType() == VkImageType::VK_IMAGE_TYPE_3D)
        {
          RemixGui::DragInt("Aliasing Depth", &RtxOptions::Aliasing::depthObject());
        }
        RemixGui::DragInt("Aliasing Layer", &RtxOptions::Aliasing::layerObject());
        aliasingImageTypeCombo.getKey(&RtxOptions::Aliasing::imageTypeObject());
        aliasingImageViewTypeCombo.getKey(&RtxOptions::Aliasing::imageViewTypeObject());

        if (IMGUI_ADD_TOOLTIP(ImGui::Button("Check aliasing for a new resource"),
          "Make sure to check the resources can be aliased under all major settings. For example, DLSS-RR or NRD, NRC or ReSTIR-GI.")) {
          Resources::s_queryAliasing = true;
        } else {
          Resources::s_queryAliasing = false;
        }
        std::string resourceAliasingQueryText = "Resource Aliasing Query Result: (";
        if (RtxOptions::enableRayReconstruction()) {
          resourceAliasingQueryText += "DLSS-RR, ";
        } else {
          resourceAliasingQueryText += "NRD, ";
        }
        if (RtxOptions::integrateIndirectMode() == IntegrateIndirectMode::NeuralRadianceCache) {
          resourceAliasingQueryText += "NRC)";
        } else if (RtxOptions::integrateIndirectMode() == IntegrateIndirectMode::ReSTIRGI) {
          resourceAliasingQueryText += "ReSTIR-GI)";
        } else {
          resourceAliasingQueryText += "ImportanceSampled)";
        }

        ImGui::Text(resourceAliasingQueryText.c_str());
        ImGui::Text("%s", Resources::s_resourceAliasingQueryText.c_str());

        if (IMGUI_ADD_TOOLTIP(ImGui::Button("Check aliasing for current resources"), "Make sure the resources are being active when checking for aliasing.")) {
          Resources::s_startAliasingAnalyzer = true;
        } else {
          Resources::s_startAliasingAnalyzer = false;
        }
        auto& str = Resources::s_aliasingAnalyzerResultText;
        ImGui::Text("Available Aliasing:\n%s", Resources::s_aliasingAnalyzerResultText.c_str());
        ImGui::Unindent();
      }
#endif
    }

    if (IMGUI_ADD_TOOLTIP(RemixGui::CollapsingHeader("Option Layers"), "View what options are present in each layer, and alter the blend strength and threshold for them.")) {
      ImGui::Indent();
      static char optionLayerFilter[256] = "";
      // Filter for option layer contents
      IMGUI_ADD_TOOLTIP(ImGui::InputText("RtxOption Display Filter", optionLayerFilter, IM_ARRAYSIZE(optionLayerFilter)), 
          "Filter options displayed in the Contents sections. Only options containing this text will be shown.");

          RemixGui::Checkbox("Pause Graph Execution", &GraphManager::pauseGraphUpdatesObject());
      if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
          "Many graphs set `enable`, `blendStrength`, and `blendThreshold` every frame.\n"
          "Pausing the graph execution will allow controlling these values without interference.");
      }

        // Pre-compute lowercased filter once for efficiency
      std::string filterLower = optionLayerFilter;
      std::transform(filterLower.begin(), filterLower.end(), filterLower.begin(), ::tolower);

      uint32_t optionLayerCounter = 1;
      for (auto& [layerKey, optionLayerPtr] : RtxOptionManager::getLayerRegistry()) {
        RtxOptionLayer& optionLayer = *optionLayerPtr;

        const bool isDefaultLayer = layerKey == kRtxOptionLayerDefaultKey;
        const bool isUserLayer = layerKey == kRtxOptionLayerUserKey;
        const bool isQualityLayer = layerKey == kRtxOptionLayerQualityKey;
        const bool isDxvkLayer = layerKey == kRtxOptionLayerDxvkConfKey;
        const bool isRtxConfLayer = layerKey == kRtxOptionLayerRtxConfKey;
        const bool hasSaveableConfig = optionLayer.hasSaveableConfigFile() && !isDxvkLayer;
        const bool hasUnsaved = hasSaveableConfig && optionLayer.hasUnsavedChanges();

        // Skip layers with no values (empty layers), unless they have a saveable config
        // Dynamic layers with saveable configs should still be shown even when empty/disabled
        if (!optionLayer.hasValues() && !hasSaveableConfig) {
          continue;
        }
        
        // Determine display name - system layers have proper names,
        // dynamically loaded layers have file paths as names which need shortening
        std::string displayName = optionLayer.getName();
        
        // Add config file indicator for layers with associated config files
        if (isRtxConfLayer) {
          displayName += " (rtx.conf)";
        } else if (isUserLayer) {
          displayName += " (user.conf)";
        } else if (isDxvkLayer) {
          displayName += " (dxvk.conf)";
        }
        
        // For non-system layers, shorten long file paths for display
        if (displayName.length() > 40) {
          // Try to extract just the file name from the path
          const std::string modsMarker = (std::filesystem::path("rtx-remix") / "mods" / "").string();
          size_t modsPos = displayName.find(modsMarker);
          if (modsPos != std::string::npos) {
            displayName = displayName.substr(modsPos + modsMarker.length());
          } else {
            // Just take the last portion of the path
            size_t lastSep = displayName.find_last_of("/\\");
            if (lastSep != std::string::npos) {
              displayName = displayName.substr(lastSep + 1);
            }
          }
        }
        
        // Build header text and styling
        std::string unsavedIndicator = hasUnsaved ? " *" : "";
        const std::string optionLayerText = std::to_string(optionLayerCounter++) + ". " + displayName + unsavedIndicator + "###" + displayName;
        
        // Determine if layer is active (only applies to layers with blend controls)
        bool pendingEnabled = optionLayer.getPendingEnabled();
        float pendingStrength = optionLayer.getPendingBlendStrength();
        float pendingThreshold = optionLayer.getPendingBlendThreshold();
        const bool isLayerActive = !hasSaveableConfig || (pendingEnabled && pendingStrength > pendingThreshold);
        
        // Apply header styling
        bool pushedStyle = false;
        if (!isLayerActive) {
          ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
          pushedStyle = true;
        } else if (hasUnsaved) {
          ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.8f, 0.2f, 1.0f));
          pushedStyle = true;
        }
        
        // Build tooltip
        std::string tooltipText;
        if (isQualityLayer) {
          tooltipText = "Quality preset settings (highest priority). Empty when Graphics Preset is Custom.";
        } else if (isUserLayer) {
          tooltipText = "A User's local graphics settings.  Overrides all other layers except Quality Presets.";
        } else if (isDefaultLayer) {
          tooltipText = "Default values for each option, as defined in RtxOptions.md.";
        } else {
          tooltipText = optionLayer.getName();
        }
        if (!optionLayer.getFilePath().empty() && optionLayer.getFilePath() != optionLayer.getName()) {
          tooltipText += "\nFile: " + optionLayer.getFilePath();
        }
        if (hasUnsaved) {
          tooltipText += "\n[Has unsaved changes]";
        }
        
        bool headerOpen = IMGUI_ADD_TOOLTIP(RemixGui::CollapsingHeader(optionLayerText.c_str(), collapsingHeaderClosedFlags), tooltipText.c_str());
        
        if (pushedStyle) {
          ImGui::PopStyleColor();
        }
        
        if (headerOpen) {
          ImGui::Indent();
          
          // Priority display
          if (isQualityLayer) {
            ImGui::Text("Priority: MAX");
            if (ImGui::IsItemHovered()) {
              ImGui::SetTooltip("Highest possible priority - quality preset settings control these options when preset is not Custom.\nThis layer is empty when Graphics Preset is set to Custom.");
            }
          } else if (isUserLayer) {
            ImGui::Text("Priority: MAX - 1");
            if (ImGui::IsItemHovered()) {
              ImGui::SetTooltip("Second highest priority - user settings that override all layers except Quality Presets.\nWhen Graphics Preset is Custom, this becomes the effective highest priority layer.");
            }
          } else if (isDefaultLayer) {
            ImGui::Text("Priority: 0");
            if (ImGui::IsItemHovered()) {
              ImGui::SetTooltip("Lowest possible priority - every other layer will be applied on top of this layer.");
            }
          } else {
            ImGui::Text("Priority: %u", optionLayer.getLayerKey().priority);
            if (ImGui::IsItemHovered()) {
              ImGui::SetTooltip(
                "Layers are applied starting with the lowest priority layer, ending with the highest.\n"
                "Each layer overrides the values written before it.\n"
                "If a layer's blendWeight is not 1 and the option is a float or Vector type,\n"
                "then the values will be calculated as LERP(previousValue, layerValue, blendWeight).");
            }
          }
          
          // Enable/blend controls only for saveable config layers (Remix Config, User, dynamically loaded mods)
          if (hasSaveableConfig && !isUserLayer) {
            const std::string optionLayerEnabledText = "Enabled###Enabled_" + displayName;
            const std::string optionLayerStrengthText = " Strength###Strength_" + displayName;
            const std::string optionLayerThresholdText = " Threshold###Threshold_" + displayName;
            
            if (IMGUI_ADD_TOOLTIP(ImGui::Checkbox(optionLayerEnabledText.c_str(), &pendingEnabled), "Check to enable the option layer. Uncheck to disable it.")) {
              optionLayer.requestEnabled(pendingEnabled);
            }

            if (IMGUI_ADD_TOOLTIP(ImGui::SliderFloat(optionLayerStrengthText.c_str(), &pendingStrength, 0.0f, 1.0f),
                                  "Adjusts the blending strength of this option layer (0 = off, 1 = full effect).")) {
              optionLayer.requestBlendStrength(pendingStrength);
            }

            if (IMGUI_ADD_TOOLTIP(ImGui::SliderFloat(optionLayerThresholdText.c_str(), &pendingThreshold, 0.0f, 1.0f),
                                  "Sets the blending strength threshold for this option layer.")) {
              optionLayer.requestBlendThreshold(pendingThreshold);
            }
          }
          
          // Action buttons only for saveable config layers
          if (hasSaveableConfig) {
            OptionLayerUI::renderLayerButtons(optionLayerPtr.get(), displayName.c_str());
          }
          
          // Contents section
          const std::string optionLayerContentsText = "Contents###Contents_" + displayName;
          if (RemixGui::CollapsingHeader(optionLayerContentsText.c_str(), collapsingHeaderClosedFlags)) {
            ImGui::Indent();
            OptionLayerUI::displayContents(optionLayer, filterLower);
            ImGui::Unindent();
          }
          
          ImGui::Unindent();
        }
      }

      ImGui::Unindent();
    }

    if (RemixGui::CollapsingHeader("UI Options")) {
      ImGui::Indent();

      if (m_pendingUIOptionsScroll) {
        ImGui::SetScrollHereY(0.0f);
        m_pendingUIOptionsScroll = false;
      }

      {
        if (RemixGui::Checkbox("Compact UI", &compactGuiObject())) {
          // Scroll to UI Options on the next frame
          m_pendingUIOptionsScroll = true;
        }
      }

      RemixGui::Checkbox("Always Developer Menu", &RtxOptions::defaultToAdvancedUIObject());

      if (RemixGui::SliderFloat("Background Alpha", &backgroundAlphaObject(), 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp)) {
        adjustStyleBackgroundAlpha(backgroundAlpha());
      }

      
      if (RemixGui::Checkbox("Use Large UI", &largeUiModeObject())) {
        m_pendingUIOptionsScroll = true;
      }
      

      {
        constexpr float indent = 60.0f;
        ImGui::PushID("gui theme");
        ImGui::Dummy(ImVec2(0, 2));
        ImGui::Text("GUI Theme:");
        ImGui::PushItemWidth(ImGui::GetContentRegionMax().x - indent);

        if (themeCombo.getKey(&themeGuiObject())) {
          m_pendingUIOptionsScroll = true;
        }

        ImGui::PopItemWidth();
        ImGui::PopID();
      }

      ImGui::Unindent();
    }

    ImGui::PopItemWidth();
  }

  namespace {
    Vector2i tovec2i(const ImVec2& v) {
      return Vector2i { static_cast<int>(v.x), static_cast<int>(v.y) };
    };

    bool isWorldTextureSelectionAllowed() {
      // mouse cursor is not obstructed by any imgui window
      return !ImGui::GetIO().WantCaptureMouse;
    }

    bool isMaterialReplacement(SceneManager& sceneManager, XXH64_hash_t texHash) {
      return sceneManager.getAssetReplacer()->getReplacementMaterial(texHash) != nullptr;
    }

    std::string makeTextureInfo(XXH64_hash_t texHash, bool isMaterialReplacement, bool includeLayerInfo = true) {
      auto iter = g_imguiTextureMap.find(texHash);
      if (iter == g_imguiTextureMap.end()) {
        return {};
      }
      const auto& imageInfo = iter->second.imageView->imageInfo();

      const auto isRT = (imageInfo.usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);

      const auto vkFormatName = (std::stringstream{} << imageInfo.format).str();
      const auto formatName = std::string_view { vkFormatName }.substr(std::string_view{"VK_FORMAT_"}.length());

      auto str = std::ostringstream {};
      str << (isMaterialReplacement ? "Replaced material" : "Legacy material") << '\n';
      str << (isRT ? "Render Target " : "Texture ") << imageInfo.extent.width << 'x' << imageInfo.extent.height << '\n';
      str << formatName << '\n';
      str << "Hash: " << hashToString(texHash) << '\n';
      
      if (!includeLayerInfo) {
        return str.str();
      }
      
      // For each category, show which layers add/remove this hash
      for (const auto& category : rtxTextureOptions) {
        if (!category.textureSetOption) {
          continue;
        }
        
        std::string layerValues = RemixGui::FormatOptionLayerValues(category.textureSetOption, texHash, true);
        if (!layerValues.empty()) {
          str << '\n' << category.displayName << ":\n" << layerValues;
        }
      }
      
      return str.str();
    }

    void toggleTextureSelection(XXH64_hash_t textureHash, const char* uniqueId, RtxOption<fast_unordered_set>* textureSet) {
      if (textureHash == kEmptyHash) {
        return;
      }
      
      // Determine if user wants to add (currently unchecked) or remove (currently checked)
      const bool userWantsRemove = textureSet->containsHash(textureHash);

      // Analyze the layer state in a single pass
      const RtxOptionLayer* targetLayer = textureSet->getTargetLayer();
      const auto& targetKey = targetLayer->getLayerKey();
      
      // Track target layer's opinion and strongest weaker layer's opinion
      bool targetHasPositive = false;
      bool targetHasNegative = false;
      bool weakerLayerAddsHash = false;  // True if strongest weaker layer adds this hash
      
      textureSet->forEachLayerValue([&](const RtxOptionLayer* layer, const GenericValue& value) {
        const HashSetLayer* hashSet = value.hashSet;
        const auto& layerKey = layer->getLayerKey();
        
        if (layerKey == targetKey) {
          targetHasPositive = hashSet->hasPositive(textureHash);
          targetHasNegative = hashSet->hasNegative(textureHash);
        } else if (targetKey < layerKey) {
          // First weaker layer with an opinion - determines what happens without target layer
          weakerLayerAddsHash = hashSet->hasPositive(textureHash);
          return false; // Stop iteration
        }
        return true; // Continue
      }, textureHash);

      // Lambda to apply the user's intended action to the target layer
      auto applyAction = [textureSet, textureHash, targetLayer, userWantsRemove, weakerLayerAddsHash, uniqueId,
                          targetHasPositive, targetHasNegative]() {
        const char* action;
        if (userWantsRemove) {
          // User wants to remove this hash from the resolved set
          if (!targetHasNegative) {
            // Either the target has a positive opinion, or no opinion at all
            // In both cases, create a negative opinion to express "I don't want this"
            textureSet->removeHash(textureHash, targetLayer);
            action = "removed (negative opinion)";
          } else {
            // Already has a negative opinion - nothing to do
            action = "already removed";
          }
        } else {
          // User wants to add this hash to the resolved set
          if (!targetHasPositive) {
            // Either the target has a negative opinion, or no opinion at all
            // In both cases, create a positive opinion to express "I want this"
            textureSet->addHash(textureHash, targetLayer);
            action = "added (positive opinion)";
          } else {
            // Already has a positive opinion - nothing to do
            action = "already added";
          }
        }

        char buffer[256];
        sprintf_s(buffer, "%s - %s %016llX\n", uniqueId, action, textureHash);
        Logger::info(buffer);
      };

      // Check for blocking layers using the standardized popup system.
      // If blocked, popup is shown and applyAction will be called after user clears blockers.
      // If not blocked, apply directly.
      if (!RemixGui::CheckRtxOptionPopups(textureSet, textureHash, applyAction)) {
        applyAction();
      }
    }

    RtxOption<fast_unordered_set>* findTextureSetByUniqueId(const char* uniqueId) {
      if (uniqueId) {
        for (RtxTextureOption& category : rtxTextureOptions) {
          if (strcmp(category.uniqueId, uniqueId) == 0) {
            return category.textureSetOption;
          }
        }
      }
      return nullptr;
    }

    namespace texture_popup {
      constexpr char POPUP_NAME[] = "rtx_texture_selection_popup";

      bool lastOpenCategoryActive { false };
      std::string lastOpenCategoryId {};

      bool g_wasLeftClick { false };

      // need to keep a reference to a texture that was passed to 'open()',
      // as 'open()' is called only once, but popup needs to reference that texture throughout open-close
      std::atomic<XXH64_hash_t> g_holdingTexture {};
      bool g_openWhenAvailable {};

      void openImguiPopupOrToggle() {
        // don't show popup window and toggle the list directly,
        // if was a left mouse click in the splitted lists
        bool toggleWithoutPopup = ImGUI::showLegacyTextureGui() &&
                                  g_wasLeftClick &&
                                  !lastOpenCategoryId.empty();
        g_wasLeftClick = false;

        if (toggleWithoutPopup) {
          if (auto textureSet = findTextureSetByUniqueId(lastOpenCategoryId.c_str())) {
            toggleTextureSelection(g_holdingTexture.load(),
                                   lastOpenCategoryId.c_str(),
                                   textureSet);
          }
        } else {
          ImGui::OpenPopup(POPUP_NAME);
        }
      }

      void open(std::optional<XXH64_hash_t> texHash) {
        g_holdingTexture.exchange(texHash.value_or(kEmptyHash));
        g_openWhenAvailable = false;
        // no need to wait, open immediately
        openImguiPopupOrToggle();
      }

      void openAsync() {
        g_holdingTexture.exchange(kEmptyHash);
        g_openWhenAvailable = true;
      }

      bool isOpened() {
        return ImGui::IsPopupOpen(POPUP_NAME);
      }

      // Returns a texture hash that it holds, if the popup is opened.
      // Must be called every frame.
      std::optional<XXH64_hash_t> produce(SceneManager& sceneMgr) {
        // delayed open, if waiting async to set g_holdingTexture
        if (g_openWhenAvailable) {
          if (g_holdingTexture.load() != kEmptyHash) {
            openImguiPopupOrToggle();
            g_openWhenAvailable = false;
          }
        }
        
        if (ImGui::BeginPopup(POPUP_NAME)) {
          const XXH64_hash_t texHash = g_holdingTexture.load();
          if (texHash != kEmptyHash) {
            ImGui::Text("Texture Info:\n%s", makeTextureInfo(texHash, isMaterialReplacement(sceneMgr, texHash), false).c_str());
            if (ImGui::Button("Copy Texture hash##texture_popup")) {
              ImGui::SetClipboardText(hashToString(texHash).c_str());
            }
            uint32_t textureFeatureFlags = 0;
            const auto& pair = g_imguiTextureMap.find(texHash);
            if (pair != g_imguiTextureMap.end()) {
              textureFeatureFlags = pair->second.textureFeatureFlags;
            }
            for (auto& rtxOption : rtxTextureOptions) {
              rtxOption.bufferToggle = rtxOption.textureSetOption->containsHash(texHash);
              if ((rtxOption.featureFlagMask & textureFeatureFlags) != rtxOption.featureFlagMask) {
                // option requires a feature, but the texture doesn't have that feature.
                continue;
              }
              
              // Quick check for blocking layer (need this for display name)
              bool hasBlockingLayer = false;
              const RtxOptionLayer* targetLayer = rtxOption.textureSetOption->getTargetLayer();
              if (targetLayer) {
                hasBlockingLayer = rtxOption.textureSetOption->getBlockingLayer(targetLayer, texHash) != nullptr;
              }
              
              // Build display name with warning indicator if hash is blocked by higher priority layer
              std::string displayName = rtxOption.displayName;
              if (hasBlockingLayer) {
                displayName = std::string(rtxOption.displayName) + " [!]";
              }

              if (RemixGui::Checkbox(displayName.c_str(), &rtxOption.bufferToggle)) {
                toggleTextureSelection(texHash, rtxOption.uniqueId, rtxOption.textureSetOption);
              }
              
              // Only build the expensive tooltip when this item is actually hovered
              if (ImGui::IsItemHovered()) {
                std::ostringstream tooltipStream;
                tooltipStream << rtxOption.textureSetOption->getDescription() << "\n";
                
                std::string layerValues = RemixGui::FormatOptionLayerValues(rtxOption.textureSetOption, texHash, false);
                if (!layerValues.empty()) {
                  tooltipStream << "\nPer-layer status for this hash:\n" << layerValues;
                }
                
                ImGui::SetTooltip("%s", tooltipStream.str().c_str());
              }
            }

            ImGui::EndPopup();
            return texHash;
          }
          ImGui::EndPopup();
          return {};
        } else {
          // popup is closed, forget texture
          g_holdingTexture.exchange(kEmptyHash);
          return {};
        }
      }
    }

    // NOTE: this is temporary, might need to show a full replacement material info
    namespace replacement_popup {
      double g_startTime { 0 };

      void open(uint32_t surfMaterialIndex) {
        g_startTime = ImGui::GetTime();
      }

      // Must be called every frame.
      std::optional< uint32_t > produce(SceneManager& sceneMgr) {
        // if mouse is now over imgui windows or there was a click, close this tooltip
        if (ImGui::GetIO().WantCaptureMouse ||
            ImGui::IsMouseClicked(ImGuiMouseButton_Left) ||
            ImGui::IsMouseClicked(ImGuiMouseButton_Middle) ||
            ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
          g_startTime = 0;
        }
        if (ImGui::GetTime() - g_startTime < 1.5f) {
          ImGui::SetTooltip("Replacement material");
        }
        return {};
      }
    }

    float fract(float v) {
      return v - std::floor(v);
    }

    // should be in sync with post_fx_highlight.comp.slang::highlightIntensity(),
    // so animation of post-effect highlight and UI are same
    float animatedHighlightIntensity(uint64_t timeSinceStartMS) {
      constexpr float ymax = 0.65f;
      float t10 = 1.0f - fract(static_cast<float>(timeSinceStartMS) / 1000.0f);
      return clamp(t10 > ymax ? t10 - (1.0f - ymax) : t10, 0.0f, 1.0f) / ymax;
    }

    constexpr const char* Uncategorized = "_nocategory";
  } // anonymous namespace

  void ImGUI::showTextureSelectionGrid(const Rc<DxvkContext>& ctx, const char* uniqueId, const uint32_t texturesPerRow, const float thumbnailSize, const float minChildHeight) {
    ImGui::PushID(uniqueId);
    auto common = ctx->getCommonObjects();
    uint32_t cnt = 0;
    float x = 0;
    const float startX = ImGui::GetCursorPosX();
    const float thumbnailSpacing = ImGui::GetStyle().ItemSpacing.x;
    const float thumbnailPadding = ImGui::GetStyle().CellPadding.x;

    bool isListFiltered = false;
    RtxTextureOption listRtxOption{};

    for (auto rtxOption : rtxTextureOptions) {
      if (strcmp(rtxOption.uniqueId, uniqueId) == 0) {
        listRtxOption = rtxOption;
        isListFiltered = true;
        break;
      }
    }

    const ImVec2 availableSize = ImGui::GetContentRegionAvail();
    const float childWindowHeight = minChildHeight <= 600.0f ? minChildHeight
                                                             : availableSize.y < 600 ? 600.0f : availableSize.y;
    ImGuiWindowFlags window_flags = ImGuiWindowFlags_None;
    ImGui::BeginChild(str::format("Child", uniqueId).c_str(), ImVec2(availableSize.x, childWindowHeight), false, window_flags);

    bool clickedOnTextureButton = false;
    static std::atomic<XXH64_hash_t> g_jumpto {};

    const XXH64_hash_t textureInPopup = texture_popup::g_holdingTexture.load();

    auto foundTextureHash = std::optional<XXH64_hash_t> {};
    auto highlightColor = HighlightColor::World;

    for (auto& [texHash, texImgui] : g_imguiTextureMap) {
      bool textureHasSelection = false;

      if (isListFiltered) {
        const auto& textureSet = listRtxOption.textureSetOption->get();
        textureHasSelection = listRtxOption.textureSetOption->containsHash(texHash);

        if ((listRtxOption.featureFlagMask & texImgui.textureFeatureFlags) != listRtxOption.featureFlagMask) {
          // If the list needs to be filtered by texture feature, skip it for this category.
          continue;
        }
      } else {
        for (const auto rtxOption : rtxTextureOptions) {
          textureHasSelection = rtxOption.textureSetOption->containsHash(texHash);
          if (textureHasSelection) {
            break;
          }
        }
      }

      // Only apply "show assigned only" filtering when using the legacy split texture GUI
      // When showLegacyTextureGui() is false, we want to show ALL textures in the single grid
      if (showLegacyTextureGui() && legacyTextureGuiShowAssignedOnly()) {
        if (std::string_view { uniqueId } == Uncategorized) {
          if (textureHasSelection) {
            continue; // Currently handling the uncategorized texture tab and current texture is assigned to a category -> skip
          }
        } else {
          if (!textureHasSelection) {
            continue; // Texture is not assigned to this category -> skip
          }
        }
      }

      if (texHash == textureInPopup || texHash == g_jumpto.load()) {
        const auto blueColor = ImGui::GetStyleColorVec4(ImGuiCol_Button);
        const auto nvidiaColor = ImVec4(0.462745f, 0.725490f, 0.f, 1.f);

        const auto color = (texHash == textureInPopup ? blueColor : nvidiaColor);
        const float anim = animatedHighlightIntensity(GlobalTime::get().absoluteTimeMs());
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(anim * color.x, anim * color.y, anim * color.z, 1.f));
      } else if (textureHasSelection) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.996078f, 0.329412f, 0.f, 1.f));
      } else {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.f, 0.f, 0.f, 1.00f));
      }

      // Lazily create the tex ID ImGUI wants
      if (texImgui.texID == VK_NULL_HANDLE) {
        texImgui.texID = ImGui_ImplDxvk::AddTexture(nullptr, texImgui.imageView);

        if (texImgui.texID == VK_NULL_HANDLE) {
          ONCE(Logger::err("Failed to allocate ImGUI handle for texture, likely because we're trying to render more textures than VkDescriptorPoolCreateInfo::maxSets.  As such, we will truncate the texture list to show only what we can."));
          return;
        }
      }

      const auto& imageInfo = texImgui.imageView->imageInfo();

      // Calculate thumbnail extent with respect to image aspect
      const float aspect = static_cast<float>(imageInfo.extent.width) / imageInfo.extent.height;
      const ImVec2 extent {
        aspect >= 1.f ? thumbnailSize : thumbnailSize * aspect,
        aspect <= 1.f ? thumbnailSize : thumbnailSize / aspect
      };

      // Align thumbnail image button
      const float y = ImGui::GetCursorPosY();
      ImGui::SetCursorPosX(x + startX + (thumbnailSize - extent.x) / 2.f);
      ImGui::SetCursorPosY(y + (thumbnailSize - extent.y) / 2.f);

      if (ImGui::ImageButton(texImgui.texID, extent)) {
        clickedOnTextureButton = true;
        texture_popup::g_wasLeftClick = true;
      }

      if (!showLegacyTextureGui() || uniqueId == texture_popup::lastOpenCategoryId) {
        if (g_jumpto.load() == texHash) {
          ImGui::SetScrollHereY(0);
          g_jumpto.exchange(kEmptyHash);
        }
      }

      if (!texture_popup::isOpened()) {
        // if ImageButton is hovered
        if (ImGui::IsItemHovered()) {
          // imgui doesn't have right-click on a button, emulate it
          if (showLegacyTextureGui()) {
            if (ImGui::IsMouseReleased(ImGuiMouseButton_Right)) {
              clickedOnTextureButton = true;
              texture_popup::g_wasLeftClick = false;
            }
          }

          foundTextureHash = texHash;
          highlightColor = HighlightColor::UI;

          // show additional info
          std::string rtxTextureSelection;
          for (auto& rtxOption : rtxTextureOptions) {
            if (rtxOption.textureSetOption->containsHash(texHash)) {
              if (rtxTextureSelection.empty()) {
                rtxTextureSelection = "\n";
              }
              rtxTextureSelection = str::format(rtxTextureSelection, " - ", rtxOption.displayName, "\n");
            }
          }
          ImGui::SetTooltip("%s\n(Left click to assign categories. Middle click to copy a texture hash.)\n\nCurrent categories:%s",
                            makeTextureInfo(texHash, isMaterialReplacement(common->getSceneManager(), texHash)).c_str(),
                            rtxTextureSelection.empty() ? "\n - None\n" : rtxTextureSelection.c_str());
          if (ImGui::IsMouseReleased(ImGuiMouseButton_Middle)) {
            ImGui::SetClipboardText(hashToString(texHash).c_str());
          }
          texture_popup::lastOpenCategoryId = uniqueId;
        }
      }

      ImGui::PopStyleColor(1);

      if (++cnt % texturesPerRow != 0) {
        x += thumbnailSize + thumbnailSpacing + thumbnailPadding;
        ImGui::SetCursorPosY(y);
      } else {
        x = 0;
        ImGui::SetCursorPosY(y + thumbnailSize + thumbnailSpacing + thumbnailPadding);
      }
    }

    // popup for texture selection from world / ui
    // Only the "active" category is allowed to control the texture popup and highlighting logic
    if (!showLegacyTextureGui() || uniqueId == texture_popup::lastOpenCategoryId) {
      const bool wasUIClick = 
        !texture_popup::isOpened() && 
        clickedOnTextureButton;

      const bool wasWorldClick =
        isWorldTextureSelectionAllowed() &&
        !texture_popup::isOpened() &&
        !clickedOnTextureButton && 
        (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right));

      if (wasUIClick) {
        texture_popup::open(foundTextureHash);
      } else if (wasWorldClick) {
        texture_popup::g_wasLeftClick = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
        // open as empty
        texture_popup::openAsync();
        // and make a request on a mouse click
        common->metaDebugView().ObjectPicking.request(
          tovec2i(ImGui::GetMousePos()),
          tovec2i(ImGui::GetMousePos()) + Vector2i { 1, 1 },

          // and callback on result:
          [](std::vector<ObjectPickingValue>&& objectPickingValues, std::optional<XXH64_hash_t> legacyTextureHash) {
            // assert(legacyTextureHash);
            // found asynchronously the legacy texture hash, place it into texture_popup; so we would highlight it
            texture_popup::g_holdingTexture.exchange(legacyTextureHash.value_or(kEmptyHash));
            // move UI menu focus
            g_jumpto.exchange(legacyTextureHash.value_or(kEmptyHash));
          });
      }

      if (wasUIClick) {
        texture_popup::lastOpenCategoryId = uniqueId;
      }

      auto texHashToHighlight = std::optional<XXH64_hash_t>{};

      // top priority for what's inside a currently open texture popup
      if (auto texInPopup = texture_popup::produce(common->getSceneManager())) {
        texHashToHighlight = *texInPopup;
        highlightColor = HighlightColor::UI;
      } else {
        if (foundTextureHash) {
          texHashToHighlight = *foundTextureHash;
        }
      }

      if (texHashToHighlight) {
        common->metaDebugView().Highlighting.requestHighlighting(*texHashToHighlight, highlightColor, ctx->getDevice()->getCurrentFrameId());
      } else {
        // if no hash to highlight: world -- highlight under a mouse cursor, ui - just desaturate
        if (isWorldTextureSelectionAllowed()) {
          common->metaDebugView().Highlighting.requestHighlighting(tovec2i(ImGui::GetMousePos()), highlightColor, ctx->getDevice()->getCurrentFrameId());
        } else {
          common->metaDebugView().Highlighting.requestHighlighting(XXH64_hash_t { kEmptyHash }, HighlightColor::UI, ctx->getDevice()->getCurrentFrameId());
        }
      }

      // checked after the last 'showTextureSelectionGrid' call to see if saved category is still active
      texture_popup::lastOpenCategoryActive = true;
    }

    ImGui::EndChild();

    ImGui::NewLine();
    ImGui::PopID();
  }

  void ImGUI::showEnhancementsWindow(const Rc<DxvkContext>& ctx) {
    ImGui::PushItemWidth(largeUiMode() ? m_largeWindowWidgetWidth : m_regularWindowWidgetWidth);

    m_capture->show(ctx);
    
    if(RemixGui::CollapsingHeader("Enhancements", collapsingHeaderFlags | ImGuiTreeNodeFlags_DefaultOpen)) {
      ImGui::Indent();
      showEnhancementsTab(ctx);
      ImGui::Unindent();
    }
    
    // Graph Visualization Section
    RemixGui::Separator();
    if (RemixGui::CollapsingHeader("Remix Logic", ImGuiTreeNodeFlags_DefaultOpen)) {
      ImGui::Indent();
      m_graphGUI->showGraphVisualization(ctx);
      ImGui::Unindent();
    }
  }
  
  void ImGUI::showEnhancementsTab(const Rc<DxvkContext>& ctx) {
    if (!ctx->getCommonObjects()->getSceneManager().areAllReplacementsLoaded()) {
      ImGui::Text("No USD enhancements detected, the following options have been disabled.  See documentation for how to use enhancements with Remix.");
    }

    ImGui::BeginDisabled(!ctx->getCommonObjects()->getSceneManager().areAllReplacementsLoaded());
    RemixGui::Checkbox("Enable Enhanced Assets", &RtxOptions::enableReplacementAssetsObject());
    {
      ImGui::Indent();
      ImGui::BeginDisabled(!RtxOptions::enableReplacementAssets());

      RemixGui::Checkbox("Enable Enhanced Materials", &RtxOptions::enableReplacementMaterialsObject());
      RemixGui::Checkbox("Enable Enhanced Meshes", &RtxOptions::enableReplacementMeshesObject());
      RemixGui::Checkbox("Enable Enhanced Lights", &RtxOptions::enableReplacementLightsObject());

      ImGui::EndDisabled();
      ImGui::Unindent();
    }
    ImGui::EndDisabled();
    RemixGui::Separator();
    RemixGui::Checkbox("Highlight Legacy Materials (flash red)", &RtxOptions::useHighlightLegacyModeObject());
    RemixGui::Checkbox("Highlight Legacy Meshes with Shared Vertex Buffers (dull purple)", &RtxOptions::useHighlightUnsafeAnchorModeObject());
    RemixGui::Checkbox("Highlight Replacements with Unstable Anchors (flash red)", &RtxOptions::useHighlightUnsafeReplacementModeObject());

  }

  namespace {
    std::optional<float> calculateTextureCategoryHeight(bool onlySelected, const char* uniqueId,
                                                        uint32_t numThumbnailsPerRow, float thumbnailSize) {
      constexpr float HeightLimit = 600;
      if (strcmp(uniqueId, Uncategorized) == 0) {
        return HeightLimit;
      }

      const RtxOption<fast_unordered_set>* selected = nullptr;
      if (onlySelected) {
        const auto found = std::find_if(rtxTextureOptions.begin(), rtxTextureOptions.end(),
          [&](const RtxTextureOption& o) {
            return strcmp(o.uniqueId, uniqueId) == 0;
          });
        if (found == rtxTextureOptions.end() || !found->textureSetOption) {
          assert(0);
          return {};
        }
        selected = found->textureSetOption;
      }

      float height = -1;
      uint32_t textureCount = 0;
      for (const auto& [texHash, texImgui] : g_imguiTextureMap) {
        if (selected) {
          if (!selected->containsHash(texHash)) {
            continue;
          }
        }
        textureCount++;

        uint32_t rows = (textureCount + numThumbnailsPerRow - 1) / numThumbnailsPerRow;
        height = rows * thumbnailSize + 16.0f;
        if (height >= HeightLimit) {
          return HeightLimit;
        }
      }
      if (height <= 0) {
        return {};
      }
      assert(height >= thumbnailSize);
      return height;
    }
  }

  void ImGUI::showSetupWindow(const Rc<DxvkContext>& ctx) {
    static auto spacing = []() {
      ImGui::Dummy({ 0,2 });
    };
    static auto separator = []() {
      spacing();
      RemixGui::Separator();
      spacing();
    };

    constexpr ImGuiTabBarFlags tab_bar_flags = ImGuiTabBarFlags_NoCloseWithMiddleMouseButton;
    constexpr ImGuiTabItemFlags tab_item_flags = ImGuiTabItemFlags_NoCloseWithMiddleMouseButton;
    if (!ImGui::BeginTabBar("##showSetupWindow", tab_bar_flags)) {
      return;
    }
    ImGui::PushItemWidth(largeUiMode() ? m_largeWindowWidgetWidth : m_regularWindowWidgetWidth);

    texture_popup::lastOpenCategoryActive = false;

    const float thumbnailScale = RtxOptions::textureGridThumbnailScale();
    const float thumbnailSize = (120.f * thumbnailScale);
    const float thumbnailSpacing = ImGui::GetStyle().ItemSpacing.x;
    const float thumbnailPadding = ImGui::GetStyle().CellPadding.x;
    const uint32_t numThumbnailsPerRow = uint32_t(std::max(1.f, (m_windowWidth - 18.f) / (thumbnailSize + thumbnailSpacing + thumbnailPadding * 2.f)));

    if (IMGUI_ADD_TOOLTIP(ImGui::BeginTabItem("Step 1: Categorize Textures", nullptr, tab_item_flags), "Select texture definitions for Remix")) {
      spacing();
      RemixGui::Checkbox("Preserve discarded textures", &RtxOptions::keepTexturesForTaggingObject());
      separator();

      // set thumbnail size
      {
        constexpr int step = 25;
        int percentage = static_cast<int>(round(100.f * RtxOptions::textureGridThumbnailScale()));
        bool changed = false;

        float buttonsize = ImGui::GetFont() ? ImGui::GetFont()->FontSize * 1.3f : 4;
        if (ImGui::Button("-##thumbscale", { buttonsize, buttonsize })) {
          percentage = std::max(25, percentage - step);
          changed = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("+##thumbscale", { buttonsize, buttonsize })) {
          percentage = std::min(300, percentage + step);
          changed = true;
        }
        ImGui::SameLine();
        ImGui::Text("Texture Thumbnail Scale: %d%%", percentage);
        if (ImGui::IsItemHovered()) {
          RemixGui::SetTooltipUnformatted(RemixGui::BuildRtxOptionTooltip(&RtxOptions::textureGridThumbnailScale).c_str());
        }

        if (changed) {
          RtxOptions::textureGridThumbnailScale.setDeferred(static_cast<float>(percentage) / 100.f);
        }
      }

      RemixGui::Checkbox("Split Texture Category List", &showLegacyTextureGuiObject());
      ImGui::BeginDisabled(!showLegacyTextureGui());
      RemixGui::Checkbox("Only Show Assigned Textures in Category Lists", &legacyTextureGuiShowAssignedOnlyObject());
      ImGui::EndDisabled();

      separator();

      // One-time migration button: only show if there are texture hashes incorrectly stored in user.conf
      {
        const RtxOptionLayer* userLayer = RtxOptionLayer::getUserLayer();
        bool hasTexturesInUserConf = false;
        if (userLayer) {
          for (const auto& rtxOption : rtxTextureOptions) {
            if (rtxOption.textureSetOption && rtxOption.textureSetOption->hasValueInLayer(userLayer)) {
              hasTexturesInUserConf = true;
              break;
            }
          }
        }
        
        if (hasTexturesInUserConf) {
          ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.4f, 0.1f, 1.0f));
          ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.5f, 0.2f, 1.0f));
          if (IMGUI_ADD_TOOLTIP(ImGui::Button("Migrate user.conf Textures"), 
              "One-time fix: Move texture hashes from user.conf to rtx.conf.\n\n"
              "In previous versions, texture categories were incorrectly saved to user.conf.\n"
              "This button migrates them to rtx.conf where they belong.\n\n"
              "After clicking, use 'Save rtx.conf Layer' to write changes to disk.")) {
            RtxOptionLayer* rtxConfLayer = RtxOptionLayer::getRtxConfLayer();
            if (rtxConfLayer) {
              for (auto& rtxOption : rtxTextureOptions) {
                if (rtxOption.textureSetOption) {
                  rtxOption.textureSetOption->moveLayerValue(userLayer, rtxConfLayer);
                }
              }
              Logger::info("[RTX Option]: Migrated texture hashes from user.conf to rtx.conf");
            }
          }
          ImGui::PopStyleColor(2);
          separator();
        }
      }

      if (showLegacyTextureGui()) {
        ImGui::TextUnformatted(
          "Hover over an object on screen, or an icon in the grid below.\n"
          "Left click to toggle the currently active category.\n"
          "Right click to open a category selection window.");
      } else {
        ImGui::TextUnformatted(
          "Hover over an object on screen, or an icon in the grid below.\n"
          "Left click to open a category selection window.");
      }

      spacing();

      if (!showLegacyTextureGui()) {
        showTextureSelectionGrid(ctx, Uncategorized, numThumbnailsPerRow, thumbnailSize);
      } else {

        auto showLegacyGui = [&](const char* uniqueId, const char* displayName, const char* description) {
          const bool countOnlySelected = legacyTextureGuiShowAssignedOnly() && !(strcmp(uniqueId, Uncategorized) == 0);
          const auto height = calculateTextureCategoryHeight(countOnlySelected, uniqueId, numThumbnailsPerRow, thumbnailSize);
          if (!height.has_value()) {
            ImGui::BeginDisabled(true);
            const auto label = displayName + std::string { " [Empty]" };
            RemixGui::CollapsingHeader(label.c_str(), collapsingHeaderClosedFlags);
            ImGui::EndDisabled();
            return;
          }
          const bool isForToggle = (texture_popup::lastOpenCategoryId == uniqueId);
          if (isForToggle) {
            ImGui::PushStyleColor(ImGuiCol_Header, ImVec4 { 0.996078f, 0.329412f, 0.f, 1.f });
            ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4 { 0.996078f, 0.329412f, 0.f, 1.f });
          }
          if (IMGUI_ADD_TOOLTIP(RemixGui::CollapsingHeader(displayName, collapsingHeaderClosedFlags), description)) {
            if (height) {
              if (ImGui::IsItemToggledOpen() || texture_popup::lastOpenCategoryId.empty()) {
                // Update last opened category ID if texture category (RemixGui::CollapsingHeader) was just toggled open or if ID is empty
                texture_popup::lastOpenCategoryId = uniqueId;
              }

              showTextureSelectionGrid(ctx, uniqueId, numThumbnailsPerRow, thumbnailSize, *height);
            }
          }
          if (isForToggle) {
            ImGui::PopStyleColor(2);
          }
        };

        if (legacyTextureGuiShowAssignedOnly()) {
          showLegacyGui(Uncategorized, "Uncategorized", "Textures that are not assigned to any category");
          spacing();
        }
        for (const RtxTextureOption& category : rtxTextureOptions) {
          showLegacyGui(category.uniqueId, category.displayName, RemixGui::BuildRtxOptionTooltip(category.textureSetOption).c_str());
        }

        // Check if last saved category was closed this frame
        if (!texture_popup::lastOpenCategoryActive) {
          texture_popup::lastOpenCategoryId.clear();
        }
      }

      //separator();
      ImGui::EndTabItem();
    }

    if (ImGui::BeginTabItem("Step 2: Parameter Tuning", nullptr, tab_item_flags)) {
      spacing();
      RemixGui::DragFloat("Scene Unit Scale", &RtxOptions::sceneScaleObject(), 0.00001f, 0.00001f, FLT_MAX, "%.5f", sliderFlags);
      RemixGui::Checkbox("Scene Z-Up", &RtxOptions::zUpObject());
      RemixGui::Checkbox("Scene Left-Handed Coordinate System", &RtxOptions::leftHandedCoordinateSystemObject());
      fusedWorldViewModeCombo.getKey(&RtxOptions::fusedWorldViewModeObject());
      RemixGui::Separator();

      RemixGui::DragFloat("Unique Object Search Distance", &RtxOptions::uniqueObjectDistanceObject(), 0.01f, FLT_MIN, FLT_MAX, "%.3f", sliderFlags);
      RemixGui::Separator();

      RemixGui::DragFloat("Vertex Color Strength", &RtxOptions::vertexColorStrengthObject(), 0.001f, 0.0f, 1.0f);
      RemixGui::Checkbox("Vertex Color Is Baked Lighting", &RtxOptions::vertexColorIsBakedLightingObject());
      RemixGui::Checkbox("Ignore All Baked Lighting", &RtxOptions::ignoreAllVertexColorBakedLightingObject());
      RemixGui::Separator();

      if (RemixGui::CollapsingHeader("Heuristics", collapsingHeaderClosedFlags)) {
        ImGui::Indent();
        RemixGui::Checkbox("Orthographic Is UI", &D3D9Rtx::orthographicIsUIObject());
        RemixGui::Checkbox("Allow Cubemaps", &D3D9Rtx::allowCubemapsObject());
        RemixGui::Checkbox("Always Calculate AABB (For Instance Matching)", &RtxOptions::enableAlwaysCalculateAABBObject());
        RemixGui::Checkbox("Skip Sky Fog Values", &RtxOptions::fogIgnoreSkyObject());
        ImGui::Unindent();
      }

      if (RemixGui::CollapsingHeader("Texture Parameters", collapsingHeaderClosedFlags)) {
        ImGui::Indent();
        RemixGui::DragFloat("Force Cutout Alpha", &RtxOptions::forceCutoutAlphaObject(), 0.01f, 0.0f, 1.0f, "%.3f", sliderFlags);
        RemixGui::DragFloat("World Space UI Background Offset", &RtxOptions::worldSpaceUiBackgroundOffsetObject(), 0.01f, -FLT_MAX, FLT_MAX, "%.3f", sliderFlags);
        RemixGui::Checkbox("Ignore last texture stage", &RtxOptions::ignoreLastTextureStageObject());
        RemixGui::Checkbox("Enable Multiple Stage Texture Factor Blending", &RtxOptions::enableMultiStageTextureFactorBlendingObject());
        ImGui::Unindent();
      }

      if (RemixGui::CollapsingHeader("Shader Support (Experimental)", collapsingHeaderClosedFlags)) {
        ImGui::Indent();
        RemixGui::Checkbox("Capture Vertices from Shader", &D3D9Rtx::useVertexCaptureObject());
        RemixGui::Checkbox("Capture Normals from Shader", &D3D9Rtx::useVertexCapturedNormalsObject());
        RemixGui::Separator();
        RemixGui::Checkbox("Use World Transforms", &D3D9Rtx::useWorldMatricesForShadersObject());
        ImGui::Unindent();
      }

      if (RemixGui::CollapsingHeader("View Model", collapsingHeaderClosedFlags)) {
        ImGui::Indent();
        RemixGui::Checkbox("Enable View Model", &RtxOptions::ViewModel::enableObject());
        RemixGui::SliderFloat("Max Z Threshold", &RtxOptions::ViewModel::maxZThresholdObject(), 0.0f, 1.0f);
        RemixGui::Checkbox("Virtual Instances", &RtxOptions::ViewModel::enableVirtualInstancesObject());
        RemixGui::Checkbox("Perspective Correction", &RtxOptions::ViewModel::perspectiveCorrectionObject());
        RemixGui::DragFloat("Scale", &RtxOptions::ViewModel::scaleObject(), 0.01f, 0.01f, 2.0f);
        ImGui::Unindent();
      }

      if (RemixGui::CollapsingHeader("Sky Tuning", collapsingHeaderClosedFlags)) {
        ImGui::Indent();
        RemixGui::DragFloat("Sky Brightness", &RtxOptions::skyBrightnessObject(), 0.01f, 0.01f, FLT_MAX, "%.3f", sliderFlags);
        RemixGui::InputInt("First N Untextured Draw Calls", &RtxOptions::skyDrawcallIdThresholdObject(), 1, 1, 0);
        RemixGui::SliderFloat("Sky Min Z Threshold", &RtxOptions::skyMinZThresholdObject(), 0.0f, 1.0f);
        skyAutoDetectCombo.getKey(&RtxOptions::skyAutoDetectObject());

        if (RemixGui::CollapsingHeader("Advanced", collapsingHeaderClosedFlags)) {
          ImGui::Indent();

          RemixGui::Checkbox("Reproject Sky to Main Camera", &RtxOptions::skyReprojectToMainCameraSpaceObject());
          {
            ImGui::BeginDisabled(!RtxOptions::skyReprojectToMainCameraSpace());
            RemixGui::DragFloat("Reprojected Sky Scale", &RtxOptions::skyReprojectScaleObject(), 1.0f, 0.1f, 1000.0f);
            ImGui::EndDisabled();
          }
          RemixGui::DragFloat("Sky Auto-Detect Unique Camera Search Distance", &RtxOptions::skyAutoDetectUniqueCameraDistanceObject(), 1.0f, 0.1f, 1000.0f);

          RemixGui::Checkbox("Force HDR sky", &RtxOptions::skyForceHDRObject());

          static const char* exts[] = { "256 (1.5MB vidmem)", "512 (6MB vidmem)", "1024 (24MB vidmem)",
            "2048 (96MB vidmem)", "4096 (384MB vidmem)", "8192 (1.5GB vidmem)" };

          static int extIdx;
          extIdx = std::clamp(bit::tzcnt(RtxOptions::skyProbeSide()), 8u, 13u) - 8;

          RemixGui::Combo("Sky Probe Extent", &extIdx, exts, IM_ARRAYSIZE(exts));
          RtxOptions::skyProbeSide.setDeferred(1 << (extIdx + 8));

          ImGui::Unindent();
        }
        ImGui::Unindent();
      }

      auto common = ctx->getCommonObjects();
      common->getSceneManager().getLightManager().showImguiSettings();

      showMaterialOptions();

      if (RemixGui::CollapsingHeader("Fog Tuning", collapsingHeaderClosedFlags)) {
        ImGui::Indent();
        ImGui::PushID("FogInfos");
        if (RemixGui::CollapsingHeader("Explanation", collapsingHeaderClosedFlags)) {
          ImGui::Indent();
          ImGui::TextWrapped("In D3D9, every draw call comes with its own fog settings."
            " In Remix pathtracing, all rays need to use the same fog setting."
            " So Remix will choose the earliest valid non-sky fog to use."

            "\n\nIn some games, fog can be used to indicate the player is inside some "
            "translucent medium, like being underwater.  In path tracing this is "
            "better represented as starting inside a translucent material.  To "
            "support this, you can copy one or more of the fog hashes listed below, "
            "and specify a translucent replacement material in your mod.usda."

            "\n\nThis replacement material should share transmittance and ior properties"
            " with your water material, but does not need any textures set."

            "\n\nReplacing a given fog state with a translucent material will disable that "
            "fog."
          );
          ImGui::Unindent();
        }

        constexpr static const char* fogModes[] = {
          "D3DFOG_NONE",
          "D3DFOG_EXP",
          "D3DFOG_EXP2",
          "D3DFOG_LINEAR",
        };

        {
          const std::lock_guard<std::mutex> lock(g_imguiFogMapMutex);
          for (const auto& pair : g_imguiFogMap) {
            const std::string hashString = hashToString(pair.first);
            const char* replaced = ctx->getCommonObjects()->getSceneManager().getAssetReplacer()->getReplacementMaterial(pair.first) ? 
              " (Replaced)" : "";
            const char* usedAsMain = (g_usedFogStateHash == pair.first) ? " (Used for Rendering)" : "";
            ImGui::Text("Hash: %s%s%s", hashString.c_str(), replaced, usedAsMain);
            const FogState& fog = pair.second;
            ImGui::Indent();

            if (ImGui::Button(str::format("Copy hash to clipboard##fog_list", hashString).c_str())) {
              ImGui::SetClipboardText(hashString.c_str());
            }
            if (uint32_t(fog.mode) < 4) {
              ImGui::Text("Mode: %s", fogModes[uint32_t(fog.mode)]);
            } else {
              ImGui::Text("Mode: unknown enum value: %u", uint32_t(fog.mode));
            }
            ImGui::Text("Color: %.2f %.2f %.2f", fog.color.r, fog.color.g, fog.color.b);
            ImGui::Text("Scale: %.2f", fog.scale);
            ImGui::Text("End: %.2f", fog.end);
            ImGui::Text("Density: %.2f", fog.density);
            
            ImGui::Unindent();
          }
        }
        ImGui::PopID();
        ImGui::Unindent();
      }

      //separator();
      ImGui::EndTabItem();
    }

    ImGui::PopItemWidth();
    ImGui::EndTabBar();
  }

  void ImGUI::adjustStyleBackgroundAlpha(const float& alpha, ImGuiStyle* dst) {
    ImGuiStyle* style = dst ? dst : &ImGui::GetStyle();
    ImVec4& currColor = style->Colors[ImGuiCol_WindowBg];
    currColor.w = alpha;
  }

  void ImGUI::updateWindowWidths() {
    // Developer menu
    m_windowWidth = largeUiMode() ? m_largeWindowWidth : m_regularWindowWidth + (compactGui() ? 0.0f : 42.0f);

    // User menu popup
    m_userWindowWidth = largeUiMode() ? m_largeUserWindowWidth : m_regularUserWindowWidth;
    m_userWindowHeight = largeUiMode() ? m_largeUserWindowHeight : m_regularUserWindowHeight;
  }

  void ImGUI::setToolkitStyle(ImGuiStyle* dst) {
    ImGuiStyle* style = dst ? dst : &ImGui::GetStyle();

    style->Alpha = 1.0f;
    style->DisabledAlpha = 0.5f;

    style->WindowPadding = ImVec2(8.0f, 10.0f);
    style->FramePadding = compactGui() ? ImVec2(4.0f, 3.0f) : ImVec2(5.0f, 4.0f);
    style->CellPadding = ImVec2(5.0f, 4.0f);
    style->ItemSpacing = compactGui() ? ImVec2(8.0f, 4.0f) : ImVec2(3.0f, 5.0f);
    style->ItemInnerSpacing = ImVec2(4.0f, 4.0f);
    style->IndentSpacing = 10.0f;
    style->ColumnsMinSpacing = 10.0f;
    style->ScrollbarSize = 15.0f;
    style->GrabMinSize = 10.0f;

    style->WindowBorderSize = 1.0f;
    style->ChildBorderSize = 1.0f;
    style->PopupBorderSize = 1.0f;
    style->FrameBorderSize = 1.0f;
    style->TabBorderSize = 0.0f;

    style->WindowRounding = 0.0f;
    style->ChildRounding = 2.0f;
    style->FrameRounding = 3.0f;
    style->PopupRounding = 2.0f;
    style->ScrollbarRounding = 2.0f;
    style->GrabRounding = 1.0f;
    style->TabRounding = 2.0f;
    style->WindowMenuButtonPosition = ImGuiDir_None;

    style->WindowMinSize = ImVec2(32, 32);
    style->TouchExtraPadding = ImVec2(0, 0);
    style->LogSliderDeadzone = 4.0f;
    style->TabMinWidthForCloseButton = 0.0f;
    style->DisplayWindowPadding = ImVec2(19, 19);
    style->DisplaySafeAreaPadding = ImVec2(3, 3);
    style->MouseCursorScale = 1.0f;

    style->Colors[ImGuiCol_WindowBg] = ImVec4(0.188f, 0.188f, 0.188f, backgroundAlpha());
    style->Colors[ImGuiCol_PopupBg] = ImVec4(0.188f, 0.188f, 0.188f, 1.00f);
    style->Colors[ImGuiCol_Text] = ImVec4(0.8f, 0.8f, 0.8f, 1.00f);
    style->Colors[ImGuiCol_TextDisabled] = ImVec4(0.44f, 0.44f, 0.44f, 1.00f);
    style->Colors[ImGuiCol_ChildBg] = ImVec4(0.16f, 0.16f, 0.16f, 0.86f);
    style->Colors[ImGuiCol_Border] = ImVec4(0.34f, 0.34f, 0.34f, 0.86f);
    style->Colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    style->Colors[ImGuiCol_FrameBg] = ImVec4(0.188f, 0.188f, 0.188f, 1.00f);
    style->Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.15f, 0.30f, 0.35f, 1.00f);
    style->Colors[ImGuiCol_FrameBgActive] = ImVec4(0.10f, 0.15f, 0.16f, 0.59f);
    style->Colors[ImGuiCol_TitleBg] = ImVec4(0.06f, 0.06f, 0.06f, 1.00f);
    style->Colors[ImGuiCol_TitleBgActive] = ImVec4(0.06f, 0.06f, 0.06f, 1.00f);
    style->Colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.15f, 0.15f, 0.15f, 0.98f);
    style->Colors[ImGuiCol_MenuBarBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    style->Colors[ImGuiCol_ScrollbarBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.24f);
    style->Colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.25f, 0.25f, 0.25f, 1.00f);
    style->Colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.31f, 0.31f, 0.31f, 0.78f);
    style->Colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.78f, 0.78f, 0.78f, 0.33f);
    style->Colors[ImGuiCol_CheckMark] = ImVec4(0.53f, 0.53f, 0.53f, 1.00f);
    style->Colors[ImGuiCol_SliderGrab] = ImVec4(1.00f, 1.00f, 1.00f, 0.39f);
    style->Colors[ImGuiCol_SliderGrabActive] = ImVec4(0.10f, 0.46f, 0.56f, 1.00f);
    style->Colors[ImGuiCol_Button] = ImVec4(0.121f, 0.129f, 0.141f, 1.00f);
    style->Colors[ImGuiCol_ButtonHovered] = ImVec4(0.27f, 0.27f, 0.27f, 1.00f);
    style->Colors[ImGuiCol_ButtonActive] = ImVec4(0.40f, 0.44f, 0.45f, 1.00f);
    style->Colors[ImGuiCol_Header] = ImVec4(0.125f, 0.125f, 0.125f, 1.00f);
    style->Colors[ImGuiCol_HeaderHovered] = ImVec4(0.17f, 0.25f, 0.27f, 0.78f);
    style->Colors[ImGuiCol_HeaderActive] = ImVec4(0.17f, 0.25f, 0.27f, 0.78f);
    style->Colors[ImGuiCol_Separator] = ImVec4(0.35f, 0.35f, 0.35f, 1.00f);
    style->Colors[ImGuiCol_SeparatorHovered] = ImVec4(0.15f, 0.52f, 0.66f, 0.30f);
    style->Colors[ImGuiCol_SeparatorActive] = ImVec4(0.30f, 0.69f, 0.84f, 0.39f);
    style->Colors[ImGuiCol_ResizeGrip] = ImVec4(0.43f, 0.43f, 0.43f, 0.51f);
    style->Colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.07f, 0.39f, 0.47f, 0.59f);
    style->Colors[ImGuiCol_ResizeGripActive] = ImVec4(0.30f, 0.69f, 0.84f, 0.39f);
    style->Colors[ImGuiCol_Tab] = ImVec4(0.00f, 0.00f, 0.00f, 0.37f);
    style->Colors[ImGuiCol_TabHovered] = ImVec4(0.22f, 0.33f, 0.36f, 1.00f);
    style->Colors[ImGuiCol_TabActive] = ImVec4(0.11f, 0.42f, 0.51f, 1.00f);
    style->Colors[ImGuiCol_TabUnfocused] = ImVec4(0.00f, 0.00f, 0.00f, 0.16f);
    style->Colors[ImGuiCol_TabUnfocusedActive] = ImVec4(1.00f, 1.00f, 1.00f, 0.24f);
    style->Colors[ImGuiCol_PlotLines] = ImVec4(1.00f, 1.00f, 1.00f, 0.35f);
    style->Colors[ImGuiCol_PlotLinesHovered] = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
    style->Colors[ImGuiCol_PlotHistogram] = ImVec4(1.00f, 1.00f, 1.00f, 0.35f);
    style->Colors[ImGuiCol_PlotHistogramHovered] = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
    style->Colors[ImGuiCol_TableHeaderBg] = ImVec4(0.16f, 0.16f, 0.16f, 1.00f);
    style->Colors[ImGuiCol_TableBorderStrong] = ImVec4(0.00f, 0.00f, 0.00f, 1.00f);
    style->Colors[ImGuiCol_TableBorderLight] = ImVec4(0.00f, 0.00f, 0.00f, 0.54f);
    style->Colors[ImGuiCol_TableRowBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.39f);
    style->Colors[ImGuiCol_TableRowBgAlt] = ImVec4(0.11f, 0.42f, 0.51f, 0.35f);
    style->Colors[ImGuiCol_TextSelectedBg] = ImVec4(0.00f, 0.00f, 0.00f, 1.00f);
    style->Colors[ImGuiCol_DragDropTarget] = ImVec4(0.00f, 0.51f, 0.39f, 0.31f);
    style->Colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.00f, 1.00f, 1.00f, 0.70f);
    style->Colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.80f, 0.80f, 0.80f, 0.20f);
    style->Colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.56f);
  }

  void ImGUI::setLegacyStyle(ImGuiStyle* dst) {
    ImGuiStyle* style = dst ? dst : &ImGui::GetStyle();

    // Original ImGui theme from ImGuiStyle::ImGuiStyle()
    style->Alpha = 1.0f;
    style->DisabledAlpha = 0.60f;
    style->WindowPadding = ImVec2(8, 8);
    style->WindowRounding = 0.0f;
    style->WindowBorderSize = 1.0f;
    style->WindowMinSize = ImVec2(32, 32);
    style->WindowTitleAlign = ImVec2(0.0f, 0.5f);
    style->WindowMenuButtonPosition = ImGuiDir_Left;
    style->ChildRounding = 0.0f;
    style->ChildBorderSize = 1.0f;
    style->PopupRounding = 0.0f;
    style->PopupBorderSize = 1.0f;
    style->FramePadding = compactGui() ? ImVec2(4, 3) : ImVec2(7, 5);
    style->FrameRounding = 0.0f;
    style->FrameBorderSize = 0.0f;
    style->ItemSpacing = compactGui() ? ImVec2(8, 4) : ImVec2(3, 5);
    style->ItemInnerSpacing = compactGui() ? ImVec2(4, 4) : ImVec2(3, 8);
    style->CellPadding = ImVec2(4, 2);
    style->TouchExtraPadding = ImVec2(0, 0);
    style->IndentSpacing = 21.0f;
    style->ColumnsMinSpacing = 6.0f;
    style->ScrollbarSize = 14.0f;
    style->ScrollbarRounding = 9.0f;
    style->GrabMinSize = 10.0f;
    style->GrabRounding = 0.0f;
    style->LogSliderDeadzone = 4.0f;
    style->TabRounding = 4.0f;
    style->TabBorderSize = 0.0f;
    style->TabMinWidthForCloseButton = 0.0f;
    style->ColorButtonPosition = ImGuiDir_Right;
    style->ButtonTextAlign = ImVec2(0.5f, 0.5f);
    style->SelectableTextAlign = ImVec2(0.0f, 0.0f);
    style->DisplayWindowPadding = ImVec2(19, 19);
    style->DisplaySafeAreaPadding = ImVec2(3, 3);
    style->MouseCursorScale = 1.0f;
    style->AntiAliasedLines = true;
    style->AntiAliasedLinesUseTex = true;
    style->AntiAliasedFill = true;
    style->CurveTessellationTol = 1.25f;
    style->CircleTessellationMaxError = 0.30f;
    ImGui::StyleColorsDark(style);

    // Remix changes
    style->Colors[ImGuiCol_WindowBg] = ImVec4(0.f, 0.f, 0.f, backgroundAlpha());
    style->Colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.f, 0.f, 0.f, 0.4f);
    style->TabRounding = 1;
  }

  void ImGUI::setNvidiaStyle(ImGuiStyle* dst) {
    ImGuiStyle* style = dst ? dst : &ImGui::GetStyle();

    // Based on legacy theme
    setLegacyStyle(style);

    style->Colors[ImGuiCol_Text] = ImVec4(0.91f, 0.91f, 0.91f, 1.00f);
    style->Colors[ImGuiCol_TextDisabled] = ImVec4(0.44f, 0.44f, 0.44f, 1.00f);
    style->Colors[ImGuiCol_WindowBg] = ImVec4(0.10f, 0.10f, 0.10f, 0.90f);
    style->Colors[ImGuiCol_ChildBg] = ImVec4(0.19f, 0.19f, 0.19f, 0.80f);
    style->Colors[ImGuiCol_PopupBg] = ImVec4(0.28f, 0.28f, 0.28f, 1.00f);
    style->Colors[ImGuiCol_Border] = ImVec4(0.31f, 0.31f, 0.31f, 0.20f);
    style->Colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.23f);
    style->Colors[ImGuiCol_FrameBg] = ImVec4(0.19f, 0.19f, 0.19f, 1.00f);
    style->Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.33f, 0.47f, 0.08f, 1.00f);
    style->Colors[ImGuiCol_FrameBgActive] = ImVec4(0.46f, 0.73f, 0.00f, 1.00f);
    style->Colors[ImGuiCol_TitleBg] = ImVec4(0.20f, 0.20f, 0.20f, 0.98f);
    style->Colors[ImGuiCol_TitleBgActive] = ImVec4(0.15f, 0.15f, 0.15f, 0.98f);
    style->Colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.15f, 0.15f, 0.15f, 0.98f);
    style->Colors[ImGuiCol_MenuBarBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    style->Colors[ImGuiCol_ScrollbarBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.24f);
    style->Colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.34f, 0.34f, 0.34f, 0.39f);
    style->Colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.54f, 0.54f, 0.54f, 0.47f);
    style->Colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.78f, 0.78f, 0.78f, 0.33f);
    style->Colors[ImGuiCol_CheckMark] = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
    style->Colors[ImGuiCol_SliderGrab] = ImVec4(1.00f, 1.00f, 1.00f, 0.39f);
    style->Colors[ImGuiCol_SliderGrabActive] = ImVec4(1.00f, 1.00f, 1.00f, 0.31f);
    style->Colors[ImGuiCol_Button] = ImVec4(0.15f, 0.15f, 0.15f, 1.00f);
    style->Colors[ImGuiCol_ButtonHovered] = ImVec4(0.33f, 0.47f, 0.08f, 1.00f);
    style->Colors[ImGuiCol_ButtonActive] = ImVec4(0.46f, 0.73f, 0.00f, 1.00f);
    style->Colors[ImGuiCol_Header] = ImVec4(0.13f, 0.13f, 0.13f, 1.00f);
    style->Colors[ImGuiCol_HeaderHovered] = ImVec4(0.33f, 0.47f, 0.08f, 1.00f);
    style->Colors[ImGuiCol_HeaderActive] = ImVec4(0.33f, 0.47f, 0.08f, 1.00f);
    style->Colors[ImGuiCol_Separator] = ImVec4(0.35f, 0.35f, 0.35f, 1.00f);
    style->Colors[ImGuiCol_SeparatorHovered] = ImVec4(0.32f, 0.46f, 0.06f, 1.00f);
    style->Colors[ImGuiCol_SeparatorActive] = ImVec4(0.46f, 0.73f, 0.00f, 1.00f);
    style->Colors[ImGuiCol_ResizeGrip] = ImVec4(0.43f, 0.43f, 0.43f, 0.51f);
    style->Colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.32f, 0.46f, 0.06f, 1.00f);
    style->Colors[ImGuiCol_ResizeGripActive] = ImVec4(0.46f, 0.73f, 0.00f, 1.00f);
    style->Colors[ImGuiCol_Tab] = ImVec4(0.00f, 0.00f, 0.00f, 0.37f);
    style->Colors[ImGuiCol_TabHovered] = ImVec4(0.32f, 0.46f, 0.06f, 1.00f);
    style->Colors[ImGuiCol_TabActive] = ImVec4(0.46f, 0.73f, 0.00f, 1.00f);
    style->Colors[ImGuiCol_TabUnfocused] = ImVec4(0.00f, 0.00f, 0.00f, 0.16f);
    style->Colors[ImGuiCol_TabUnfocusedActive] = ImVec4(1.00f, 1.00f, 1.00f, 0.24f);
    style->Colors[ImGuiCol_PlotLines] = ImVec4(1.00f, 1.00f, 1.00f, 0.35f);
    style->Colors[ImGuiCol_PlotLinesHovered] = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
    style->Colors[ImGuiCol_PlotHistogram] = ImVec4(1.00f, 1.00f, 1.00f, 0.35f);
    style->Colors[ImGuiCol_PlotHistogramHovered] = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
    style->Colors[ImGuiCol_TableHeaderBg] = ImVec4(0.16f, 0.16f, 0.16f, 1.00f);
    style->Colors[ImGuiCol_TableBorderStrong] = ImVec4(0.00f, 0.00f, 0.00f, 1.00f);
    style->Colors[ImGuiCol_TableBorderLight] = ImVec4(0.00f, 0.00f, 0.00f, 0.54f);
    style->Colors[ImGuiCol_TableRowBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.39f);
    style->Colors[ImGuiCol_TableRowBgAlt] = ImVec4(0.46f, 0.73f, 0.00f, 1.00f);
    style->Colors[ImGuiCol_TextSelectedBg] = ImVec4(0.00f, 0.00f, 0.00f, 1.00f);
    style->Colors[ImGuiCol_DragDropTarget] = ImVec4(0.00f, 0.51f, 0.39f, 0.31f);
    style->Colors[ImGuiCol_NavHighlight] = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
    style->Colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.00f, 1.00f, 1.00f, 0.70f);
    style->Colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.80f, 0.80f, 0.80f, 0.20f);
    style->Colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.56f);
  }

  void ImGUI::onThemeChange(DxvkDevice* device) {
    if (GImGui != nullptr) {
      ImGUI& gui = device->getCommon()->getImgui();
      gui.setupStyle();

    }
  }

  void ImGUI::onBackgroundAlphaChange(DxvkDevice* device) {
    if (GImGui != nullptr) {
      ImGUI& gui = device->getCommon()->getImgui();
      gui.adjustStyleBackgroundAlpha(backgroundAlpha());
    }
  }

  void ImGUI::setupStyle(ImGuiStyle* dst) {
    ImGui::GetIO().FontDefault = largeUiMode() ? m_largeFont : m_regularFont;
    updateWindowWidths();
   
    ImGuiStyle* style = dst ? dst : &ImGui::GetStyle();
    switch (themeGui())
    {
    default:
    case Theme::Toolkit:
      setToolkitStyle(style);
      break;

    case Theme::Legacy:
      setLegacyStyle(style);
      break;
    
    case Theme::Nvidia:
      setNvidiaStyle(style);
      break;
    }
  }

  void ImGUI::showVsyncOptions(bool enableDLFGGuard) {
    // we should never get here without a swapchain, so we must have latched the vsync value already
    assert(RtxOptions::enableVsyncState != EnableVsync::WaitingForImplicitSwapchain);
    
    if (enableDLFGGuard && DxvkDLFG::enable()) {
      ImGui::BeginDisabled();
    }

    bool vsyncEnabled = RtxOptions::enableVsyncState == EnableVsync::On;
    bool changed = RemixGui::Checkbox("Enable V-Sync", &vsyncEnabled);
    if (changed) {
      // option has been toggled manually, so we need to actually store the value in the option.
      // RtxOptions::enableVsyncState will be changed by the onChange handler at the end of the frame.
      RtxOptions::enableVsync.setDeferred(vsyncEnabled ? EnableVsync::On : EnableVsync::Off);
    }

    ImGui::BeginDisabled();
    ImGui::Indent();
    ImGui::TextWrapped("This setting overrides the native game's V-Sync setting.");
    ImGui::Unindent();
    ImGui::EndDisabled();
    
    if (enableDLFGGuard && DxvkDLFG::enable()) {
      ImGui::Indent();
      ImGui::TextWrapped("When Frame Generation is active, V-Sync is automatically disabled.");
      ImGui::Unindent();

      ImGui::EndDisabled();
    }
  }

  void ImGUI::showDLFGOptions(const Rc<DxvkContext>& ctx) {
    const bool supportsDLFG = ctx->getCommonObjects()->metaNGXContext().supportsDLFG() && !ctx->getCommonObjects()->metaDLFG().hasDLFGFailed();
    const uint32_t maxInterpolatedFrames = ctx->getCommonObjects()->metaNGXContext().dlfgMaxInterpolatedFrames();
    const bool supportsMultiFrame = maxInterpolatedFrames > 1;

    if (!supportsDLFG) {
      ImGui::BeginDisabled();
    }

    bool dlfgChanged = RemixGui::Checkbox("Enable DLSS Frame Generation", &DxvkDLFG::enableObject());
    if (supportsMultiFrame) {
      dlfgMfgModeCombo.getKey(&DxvkDLFG::maxInterpolatedFramesObject());
    }

    const auto& reason = ctx->getCommonObjects()->metaNGXContext().getDLFGNotSupportedReason();
    if (reason.size()) {
      RemixGui::SetTooltipToLastWidgetOnHover(reason.c_str());
      ImGui::TextWrapped(reason.c_str());
    }

    if (!supportsDLFG) {
      ImGui::EndDisabled();
    }

    // Need to change Reflex in sync with DLFG, not on the next frame.
    if (dlfgChanged) {
      if (!supportsDLFG) {
        DxvkDLFG::enable.setDeferred(false);
      } else if (!DxvkDLFG::enable()){
        // DLFG was just enabled.  force Reflex to Low Latency.
        RtxOptions::reflexMode.setDeferred(ReflexMode::LowLatency);
      }
    }

  }

  void ImGUI::showReflexOptions(const Rc<DxvkContext>& ctx, bool displayStatsWindowToggle) {
    RtxReflex& reflex = m_device->getCommon()->metaReflex();

    // Note: Skip Reflex ImGUI options if Reflex is not initialized (either fully disabled or failed to be initialized).
    if (!reflex.reflexInitialized()) {
      return;
    }

    // Display Reflex mode selector

    {
      bool disableReflexUI = ctx->isDLFGEnabled();
      ImGui::BeginDisabled(disableReflexUI);
      reflexModeCombo.getKey(&RtxOptions::reflexModeObject());
      ImGui::EndDisabled();
    }

    // Add a button to toggle the Reflex latency stats Window if requested

    if (displayStatsWindowToggle) {
      if (ImGui::Button("Toggle Reflex Stats Window", ImVec2(ImGui::GetContentRegionAvail().x - GImGui->Style.FramePadding.x * 2, 0))) {
        m_reflexLatencyStatsOpen = !m_reflexLatencyStatsOpen;
      }
    }

  }

  void ImGUI::showReflexLatencyStats() {
    // Set up the latency stats Window

    ImGui::SetNextWindowSize(ImVec2(m_reflexLatencyStatsWindowWidth, m_reflexLatencyStatsWindowHeight), ImGuiCond_Once);

    if (!ImGui::Begin("Reflex Latency Stats", &m_reflexLatencyStatsOpen, popupWindowFlags)) {
      ImGui::End();

      return;
    }

    RtxReflex& reflex = m_device->getCommon()->metaReflex();
    const auto latencyStats = reflex.getLatencyStats();

    constexpr ImPlotFlags druationGraphFlags = ImPlotFlags_NoMouseText | ImPlotFlags_NoInputs | ImPlotFlags_NoMenus | ImPlotFlags_NoBoxSelect;
    constexpr ImPlotAxisFlags graphFrameAxisFlags = ImPlotAxisFlags_NoGridLines | ImPlotAxisFlags_NoTickMarks | ImPlotAxisFlags_NoTickLabels | ImPlotAxisFlags_Lock;
    constexpr ImPlotAxisFlags graphDurationAxisFlags = ImPlotAxisFlags_Lock;

    constexpr ImPlotFlags timingGraphFlags = ImPlotFlags_NoMouseText | ImPlotFlags_NoInputs | ImPlotFlags_NoMenus | ImPlotFlags_NoBoxSelect | ImPlotFlags_NoLegend;
    constexpr ImPlotAxisFlags graphTimeAxisFlags = ImPlotAxisFlags_Lock;
    constexpr ImPlotAxisFlags graphRegionAxisFlags = ImPlotAxisFlags_NoGridLines | ImPlotAxisFlags_NoTickMarks;

    // Update Reflex stat ranges

    const auto& interpolationRate = reflexStatRangeInterpolationRate();
    const auto& paddingRatio = reflexStatRangePaddingRatio();

    const auto newCurrentGameToRenderDurationMin = std::max(latencyStats.gameToRenderDurationMin - latencyStats.gameToRenderDurationMin * paddingRatio, 0.0f);
    const auto newCurrentGameToRenderDurationMax = latencyStats.gameToRenderDurationMax + latencyStats.gameToRenderDurationMax * paddingRatio;
    const auto newCurrentCombinedDurationMin = std::max(latencyStats.combinedDurationMin - latencyStats.combinedDurationMin * paddingRatio, 0.0f);
    const auto newCurrentcombinedDurationMax = latencyStats.combinedDurationMax + latencyStats.combinedDurationMax * paddingRatio;

    // Note: Check if the various range members have been initialized yet to allow the first frame to set them rather than interpolate (since they are
    // left as undefined values right now, and even if they were set to 0 or some other value it might give slightly jarring initial behavior).
    if (m_reflexRangesInitialized) {
      // Note: Exponential-esque moving averages.
      m_currentGameToRenderDurationMin = lerp(m_currentGameToRenderDurationMin, newCurrentGameToRenderDurationMin, interpolationRate);
      m_currentGameToRenderDurationMax = lerp(m_currentGameToRenderDurationMax, newCurrentGameToRenderDurationMax, interpolationRate);
      m_currentCombinedDurationMin = lerp(m_currentCombinedDurationMin, newCurrentCombinedDurationMin, interpolationRate);
      m_currentCombinedDurationMax = lerp(m_currentCombinedDurationMax, newCurrentcombinedDurationMax, interpolationRate);
    } else {
      m_currentGameToRenderDurationMin = newCurrentGameToRenderDurationMin;
      m_currentGameToRenderDurationMax = newCurrentGameToRenderDurationMax;
      m_currentCombinedDurationMin = newCurrentCombinedDurationMin;
      m_currentCombinedDurationMax = newCurrentcombinedDurationMax;

      m_reflexRangesInitialized = true;
    }

    // Draw Total Duration Plot

    if (ImPlot::BeginPlot("Total Duration", ImVec2(-1, 200), druationGraphFlags)) {
      ImPlot::SetupAxes("Frame", "Duration (ms)", graphFrameAxisFlags, graphDurationAxisFlags);
      ImPlot::SetupAxisLimits(ImAxis_X1, static_cast<double>(latencyStats.frameIDMin), static_cast<double>(latencyStats.frameIDMax), ImGuiCond_Always);
      ImPlot::SetupAxisLimits(ImAxis_Y1, static_cast<double>(m_currentGameToRenderDurationMin), static_cast<double>(m_currentGameToRenderDurationMax), ImPlotCond_Always);

      ImPlot::PlotLine("Game to Render", latencyStats.frameID, latencyStats.gameToRenderDuration, LatencyStats::statFrames, 0, 0);

      ImPlot::EndPlot();
    }

    ImGui::Text("Game to Render Duration: %.2f ms", latencyStats.gameToRenderDuration[LatencyStats::statFrames - 1]);
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    RemixGui::SetTooltipToLastWidgetOnHover("This measures the time from the start of the simulation to the end of the GPU rendering as a total game to render latency.");

    RemixGui::Separator();

    // Draw Region Duration Plot

    if (ImPlot::BeginPlot("Region Durations", ImVec2(-1, 250), druationGraphFlags)) {
      ImPlot::SetupAxes("Frame", "Duration (ms)", graphFrameAxisFlags, graphDurationAxisFlags);
      ImPlot::SetupAxisLimits(ImAxis_X1, static_cast<double>(latencyStats.frameIDMin), static_cast<double>(latencyStats.frameIDMax), ImGuiCond_Always);
      ImPlot::SetupAxisLimits(ImAxis_Y1, static_cast<double>(m_currentCombinedDurationMin), static_cast<double>(m_currentCombinedDurationMax), ImPlotCond_Always);

      ImPlot::PlotLine("Simulation", latencyStats.frameID, latencyStats.simDuration, LatencyStats::statFrames, 0, 0);
      ImPlot::PlotLine("Render Submit", latencyStats.frameID, latencyStats.renderSubmitDuration, LatencyStats::statFrames, 0, 0);
      ImPlot::PlotLine("Present", latencyStats.frameID, latencyStats.presentDuration, LatencyStats::statFrames, 0, 0);
      ImPlot::PlotLine("Driver", latencyStats.frameID, latencyStats.driverDuration, LatencyStats::statFrames, 0, 0);
      ImPlot::PlotLine("OS Queue", latencyStats.frameID, latencyStats.osRenderQueueDuration, LatencyStats::statFrames, 0, 0);
      ImPlot::PlotLine("GPU Render", latencyStats.frameID, latencyStats.gpuRenderDuration, LatencyStats::statFrames, 0, 0);

      ImPlot::EndPlot();
    }

    ImGui::Text("Simulation Duration: %.2f ms", latencyStats.simDuration[LatencyStats::statFrames - 1]);
    ImGui::Text("Render Submit Duration: %.2f ms", latencyStats.renderSubmitDuration[LatencyStats::statFrames - 1]);
    ImGui::Text("Present Duration: %.2f ms", latencyStats.presentDuration[LatencyStats::statFrames - 1]);
    ImGui::Text("Driver Duration: %.2f ms", latencyStats.driverDuration[LatencyStats::statFrames - 1]);
    ImGui::Text("OS Queue Duration: %.2f ms", latencyStats.osRenderQueueDuration[LatencyStats::statFrames - 1]);
    ImGui::Text("GPU Render Duration: %.2f ms", latencyStats.gpuRenderDuration[LatencyStats::statFrames - 1]);

    RemixGui::Separator();

    // Draw Region Timing Plot

    constexpr float microsecondsPerMillisecond { 1000.0f };

    if (ImPlot::BeginPlot("Region Timings", ImVec2(-1, 150), timingGraphFlags)) {
      ImPlot::SetupAxes(nullptr, nullptr, graphTimeAxisFlags, graphRegionAxisFlags);
      ImPlot::SetupAxisLimits(ImAxis_X1, 0.0f, static_cast<double>(static_cast<float>(latencyStats.combinedCurrentTimeMax - latencyStats.combinedCurrentTimeMin) / microsecondsPerMillisecond), ImPlotCond_Always);

      constexpr const char* regions[]{ "Simulation", "Render Submit", "Present", "Driver", "OS Queue", "GPU Render" };
      ImPlot::SetupAxisTicks(ImAxis_Y1, 0, 5, 6, regions, false);

      // Note: Name needed to color label.
      constexpr const char* labels[]{ "", "Time", "" };
      const float timeData[]{
        // Pre-Span
        static_cast<float>(latencyStats.simCurrentStartTime - latencyStats.combinedCurrentTimeMin) / microsecondsPerMillisecond,
        static_cast<float>(latencyStats.renderSubmitCurrentStartTime - latencyStats.combinedCurrentTimeMin) / microsecondsPerMillisecond,
        static_cast<float>(latencyStats.presentCurrentStartTime - latencyStats.combinedCurrentTimeMin) / microsecondsPerMillisecond,
        static_cast<float>(latencyStats.driverCurrentStartTime - latencyStats.combinedCurrentTimeMin) / microsecondsPerMillisecond,
        static_cast<float>(latencyStats.osRenderQueueCurrentStartTime - latencyStats.combinedCurrentTimeMin) / microsecondsPerMillisecond,
        static_cast<float>(latencyStats.gpuRenderCurrentStartTime - latencyStats.combinedCurrentTimeMin) / microsecondsPerMillisecond,
        // Current Span
        latencyStats.simDuration[LatencyStats::statFrames - 1],
        latencyStats.renderSubmitDuration[LatencyStats::statFrames - 1],
        latencyStats.presentDuration[LatencyStats::statFrames - 1],
        latencyStats.driverDuration[LatencyStats::statFrames - 1],
        latencyStats.osRenderQueueDuration[LatencyStats::statFrames - 1],
        latencyStats.gpuRenderDuration[LatencyStats::statFrames - 1],
        // Post-Span
        static_cast<float>(latencyStats.combinedCurrentTimeMax - latencyStats.simCurrentEndTime) / microsecondsPerMillisecond,
        static_cast<float>(latencyStats.combinedCurrentTimeMax - latencyStats.renderSubmitCurrentEndTime) / microsecondsPerMillisecond,
        static_cast<float>(latencyStats.combinedCurrentTimeMax - latencyStats.presentCurrentEndTime) / microsecondsPerMillisecond,
        static_cast<float>(latencyStats.combinedCurrentTimeMax - latencyStats.driverCurrentEndTime) / microsecondsPerMillisecond,
        static_cast<float>(latencyStats.combinedCurrentTimeMax - latencyStats.osRenderQueueCurrentEndTime) / microsecondsPerMillisecond,
        static_cast<float>(latencyStats.combinedCurrentTimeMax - latencyStats.gpuRenderCurrentEndTime) / microsecondsPerMillisecond,
      };

      ImPlot::PlotBarGroups(labels, timeData, 3, 6, 0.75, 0, ImPlotBarGroupsFlags_Stacked | ImPlotBarGroupsFlags_Horizontal);

      ImPlot::EndPlot();
    }

    ImGui::End();
  }

  void ImGUI::showRenderingSettings(const Rc<DxvkContext>& ctx) {
    ImGui::PushItemWidth(largeUiMode() ? m_largeWindowWidgetWidth : m_regularWindowWidgetWidth);
    auto common = ctx->getCommonObjects();

    ImGui::Text("Disclaimer: The following settings are intended for developers,\nchanging them may introduce instability.");
    RemixGui::Separator();

    // Always display memory stats to user.
    showMemoryStats();

    RemixGui::Separator();

    if (RemixGui::CollapsingHeader("General", collapsingHeaderFlags)) {
      auto& dlss = common->metaDLSS();
      auto& rayReconstruction = common->metaRayReconstruction();
      ImGui::Indent();

      if (RtxOptions::showRaytracingOption()) {
        RemixGui::Checkbox("Raytracing Enabled", &RtxOptions::enableRaytracingObject());

        renderPassGBufferRaytraceModeCombo.getKey(&RtxOptions::renderPassGBufferRaytraceModeObject());
        renderPassIntegrateDirectRaytraceModeCombo.getKey(&RtxOptions::renderPassIntegrateDirectRaytraceModeObject());
        renderPassIntegrateIndirectRaytraceModeCombo.getKey(&RtxOptions::renderPassIntegrateIndirectRaytraceModeObject());

        RemixGui::Separator();
      }

      showDLFGOptions(ctx);

      RemixGui::Separator();

      showReflexOptions(ctx, true);

      RemixGui::Separator();

      if (ctx->getCommonObjects()->metaDLSS().supportsDLSS()) {
        // Show upscaler and DLSS-RR option.
        auto oldUpscalerType = RtxOptions::upscalerType();
        bool oldDLSSRREnabled = RtxOptions::enableRayReconstruction();
        getUpscalerCombo(dlss, rayReconstruction).getKey(&RtxOptions::upscalerTypeObject());
        showRayReconstructionEnable(rayReconstruction.supportsRayReconstruction());

        // Update path tracer settings when upscaler is changed or DLSS-RR is toggled.
        if (oldUpscalerType != RtxOptions::upscalerType() || oldDLSSRREnabled != RtxOptions::enableRayReconstruction()) {
          RtxOptions::updateLightingSetting();
        }
      } else {
        getUpscalerCombo(dlss, rayReconstruction).getKey(&RtxOptions::upscalerTypeObject());
      }

      RtxOptions::updatePresetFromUpscaler();

      if (RtxOptions::upscalerType() == UpscalerType::DLSS && !ctx->getCommonObjects()->metaDLSS().supportsDLSS()) {
        RtxOptions::upscalerType.setDeferred(UpscalerType::TAAU);
      }

      if (RtxOptions::isRayReconstructionEnabled()) {
        dlssProfileCombo.getKey(&RtxOptions::qualityDLSSObject());
        rayReconstruction.showRayReconstructionImguiSettings(false);
      } else if (RtxOptions::upscalerType() == UpscalerType::DLSS) {
        dlssProfileCombo.getKey(&RtxOptions::qualityDLSSObject());
        dlss.showImguiSettings();
      } else if (RtxOptions::upscalerType() == UpscalerType::NIS) {
        RemixGui::SliderFloat("Resolution scale", &RtxOptions::resolutionScaleObject(), 0.5f, 1.0f);
        RemixGui::SliderFloat("Sharpness", &ctx->getCommonObjects()->metaNIS().m_sharpness, 0.1f, 1.0f);
        RemixGui::Checkbox("Use FP16", &ctx->getCommonObjects()->metaNIS().m_useFp16);
      } else if (RtxOptions::upscalerType() == UpscalerType::XeSS) {
          xessPresetCombo.getKey(&DxvkXeSS::XessOptions::presetObject());

          // Show resolution slider only for Custom preset
          if (DxvkXeSS::XessOptions::preset() == XeSSPreset::Custom) {
            RemixGui::SliderFloat("Resolution Scale", &RtxOptions::resolutionScaleObject(), 0.1f, 1.0f, "%.2f");
          }

          // Display XeSS internal resolution
          auto& xess = ctx->getCommonObjects()->metaXeSS();

          uint32_t inputWidth;
          uint32_t inputHeight;
          xess.getInputSize(inputWidth, inputHeight);
          ImGui::TextWrapped(str::format("Render Resolution: ", inputWidth, "x", inputHeight).c_str());
        } else if (RtxOptions::upscalerType() == UpscalerType::TAAU) {
        RemixGui::SliderFloat("Resolution scale", &RtxOptions::resolutionScaleObject(), 0.5f, 1.0f);
      }

      RemixGui::Separator();

      RemixGui::Checkbox("Allow Full Screen Exclusive?", &RtxOptions::allowFSEObject());

      ImGui::Unindent();
    }

    if (RemixGui::CollapsingHeader("Pathtracing", collapsingHeaderClosedFlags)) {
      ImGui::Indent();

      RemixGui::Checkbox("RNG: seed with frame index", &RtxOptions::rngSeedWithFrameIndexObject());

      if (RemixGui::CollapsingHeader("Resolver", collapsingHeaderClosedFlags)) {
        ImGui::Indent();

        RemixGui::DragInt("Max Primary Interactions", &RtxOptions::primaryRayMaxInteractionsObject(), 1.0f, 1, 255, "%d", sliderFlags);
        RemixGui::DragInt("Max PSR Interactions", &RtxOptions::psrRayMaxInteractionsObject(), 1.0f, 1, 255, "%d", sliderFlags);
        RemixGui::DragInt("Max Secondary Interactions", &RtxOptions::secondaryRayMaxInteractionsObject(), 1.0f, 1, 255, "%d", sliderFlags);
        RemixGui::Checkbox("Separate Unordered Approximations", &RtxOptions::enableSeparateUnorderedApproximationsObject());
        RemixGui::Checkbox("Direct Translucent Shadows", &RtxOptions::enableDirectTranslucentShadowsObject());
        RemixGui::Checkbox("Direct Alpha Blended Shadows", &RtxOptions::enableDirectAlphaBlendShadowsObject());
        RemixGui::Checkbox("Indirect Translucent Shadows", &RtxOptions::enableIndirectTranslucentShadowsObject());
        RemixGui::Checkbox("Indirect Alpha Blended Shadows", &RtxOptions::enableIndirectAlphaBlendShadowsObject());
        RemixGui::Checkbox("Decal Material Blending", &RtxOptions::enableDecalMaterialBlendingObject());
        RemixGui::Checkbox("Billboard Orientation Correction", &RtxOptions::enableBillboardOrientationCorrectionObject());
        if (RtxOptions::enableBillboardOrientationCorrection()) {
          ImGui::Indent();
          RemixGui::Checkbox("Dev: Use i-prims on primary rays", &RtxOptions::useIntersectionBillboardsOnPrimaryRaysObject());
          ImGui::Unindent();
        }
        RemixGui::Checkbox("Track Particle Object", &RtxOptions::trackParticleObjectsObject());

        RemixGui::SliderFloat("Resolve Transparency Threshold", &RtxOptions::resolveTransparencyThresholdObject(), 0.0f, 1.0f);
        RemixGui::SliderFloat("Resolve Opaqueness Threshold", &RtxOptions::resolveOpaquenessThresholdObject(), 0.0f, 1.0f);

        ImGui::Unindent();
      }

      if (RemixGui::CollapsingHeader("PSR", collapsingHeaderClosedFlags)) {
        ImGui::Indent();

        RemixGui::Checkbox("Reflection PSR Enabled", &RtxOptions::enablePSRRObject());
        RemixGui::Checkbox("Transmission PSR Enabled", &RtxOptions::enablePSTRObject());
        // # bounces limitted by 8b allocation in payload
        // Note: value of 255 effectively means unlimited bounces, and we don't want to allow that
        RemixGui::DragInt("Max Reflection PSR Bounces", &RtxOptions::psrrMaxBouncesObject(), 1.0f, 1, 254, "%d", sliderFlags);
        RemixGui::DragInt("Max Transmission PSR Bounces", &RtxOptions::pstrMaxBouncesObject(), 1.0f, 1, 254, "%d", sliderFlags);
        RemixGui::Checkbox("Outgoing Transmission Approx Enabled", &RtxOptions::enablePSTROutgoingSplitApproximationObject());
        RemixGui::Checkbox("Incident Transmission Approx Enabled", &RtxOptions::enablePSTRSecondaryIncidentSplitApproximationObject());
        RemixGui::DragFloat("Reflection PSR Normal Detail Threshold", &RtxOptions::psrrNormalDetailThresholdObject(), 0.001f, 0.f, 1.f);
        RemixGui::DragFloat("Transmission PSR Normal Detail Threshold", &RtxOptions::pstrNormalDetailThresholdObject(), 0.001f, 0.f, 1.f);

        ImGui::Unindent();
      }

      if (RemixGui::CollapsingHeader("Integrator", collapsingHeaderClosedFlags)) {
        ImGui::Indent();

        RemixGui::Checkbox("Enable Secondary Bounces", &RtxOptions::enableSecondaryBouncesObject());
        RemixGui::Checkbox("Enable Russian Roulette", &RtxOptions::enableRussianRouletteObject());
        RemixGui::Checkbox("Enable Probability Dithering Filtering for Primary Bounce", &RtxOptions::enableFirstBounceLobeProbabilityDitheringObject());
        RemixGui::Checkbox("Unordered Resolve in Indirect Rays", &RtxOptions::enableUnorderedResolveInIndirectRaysObject());
        ImGui::BeginDisabled(!RtxOptions::enableUnorderedResolveInIndirectRays());
        RemixGui::Checkbox("Probabilistic Unordered Resolve in Indirect Rays", &RtxOptions::enableProbabilisticUnorderedResolveInIndirectRaysObject());
        ImGui::EndDisabled();
        RemixGui::Checkbox("Unordered Emissive Particles in Indirect Rays", &RtxOptions::enableUnorderedEmissiveParticlesInIndirectRaysObject());
        RemixGui::Checkbox("Transmission Approximation in Indirect Rays", &RtxOptions::enableTransmissionApproximationInIndirectRaysObject());
        // # bounces limitted by 4b allocation in payload
        // Note: It's possible get up to 16 bounces => will require logic adjustment
        RemixGui::DragInt("Minimum Path Bounces", &RtxOptions::pathMinBouncesObject(), 1.0f, 0, 15, "%d", sliderFlags);
        RemixGui::DragInt("Maximum Path Bounces", &RtxOptions::pathMaxBouncesObject(), 1.0f, RtxOptions::pathMinBounces(), 15, "%d", sliderFlags);
        RemixGui::DragFloat("Firefly Filtering Luminance Threshold", &RtxOptions::fireflyFilteringLuminanceThresholdObject(), 0.1f, 0.0f, FLT_MAX, "%.3f", sliderFlags);
        RemixGui::DragFloat("Secondary Specular Firefly Filtering Threshold", &RtxOptions::secondarySpecularFireflyFilteringThresholdObject(), 0.1f, 0.0f, FLT_MAX, "%.3f", sliderFlags);
        RemixGui::DragFloat("Opaque Diffuse Lobe Probability Zero Threshold", &RtxOptions::opaqueDiffuseLobeSamplingProbabilityZeroThresholdObject(), 0.001f, 0.0f, 1.0f, "%.3f", sliderFlags);
        RemixGui::DragFloat("Min Opaque Diffuse Lobe Probability", &RtxOptions::minOpaqueDiffuseLobeSamplingProbabilityObject(), 0.001f, 0.0f, 1.0f, "%.3f", sliderFlags);
        RemixGui::DragFloat("Opaque Specular Lobe Probability Zero Threshold", &RtxOptions::opaqueSpecularLobeSamplingProbabilityZeroThresholdObject(), 0.001f, 0.0f, 1.0f, "%.3f", sliderFlags);
        RemixGui::DragFloat("Min Opaque Specular Lobe Probability", &RtxOptions::minOpaqueSpecularLobeSamplingProbabilityObject(), 0.001f, 0.0f, 1.0f, "%.3f", sliderFlags);
        RemixGui::DragFloat("Opaque Opacity Transmission Lobe Probability Zero Threshold", &RtxOptions::opaqueOpacityTransmissionLobeSamplingProbabilityZeroThresholdObject(), 0.001f, 0.0f, 1.0f, "%.3f", sliderFlags);
        RemixGui::DragFloat("Min Opaque Opacity Transmission Lobe Probability", &RtxOptions::minOpaqueOpacityTransmissionLobeSamplingProbabilityObject(), 0.001f, 0.0f, 1.0f, "%.3f", sliderFlags);
        RemixGui::DragFloat("Diffuse Transmission Lobe Probability Zero Threshold", &RtxOptions::opaqueDiffuseTransmissionLobeSamplingProbabilityZeroThresholdObject(), 0.001f, 0.0f, 1.0f, "%.3f", sliderFlags);
        RemixGui::DragFloat("Min Diffuse Transmission Lobe Probability", &RtxOptions::minOpaqueDiffuseTransmissionLobeSamplingProbabilityObject(), 0.001f, 0.0f, 1.0f, "%.3f", sliderFlags);
        RemixGui::DragFloat("Translucent Specular Lobe Probability Zero Threshold", &RtxOptions::translucentSpecularLobeSamplingProbabilityZeroThresholdObject(), 0.001f, 0.0f, 1.0f, "%.3f", sliderFlags);
        RemixGui::DragFloat("Min Translucent Specular Lobe Probability", &RtxOptions::minTranslucentSpecularLobeSamplingProbabilityObject(), 0.001f, 0.0f, 1.0f, "%.3f", sliderFlags);
        RemixGui::DragFloat("Translucent Transmission Lobe Probability Zero Threshold", &RtxOptions::translucentTransmissionLobeSamplingProbabilityZeroThresholdObject(), 0.001f, 0.0f, 1.0f, "%.3f", sliderFlags);
        RemixGui::DragFloat("Min Translucent Transmission Lobe Probability", &RtxOptions::minTranslucentTransmissionLobeSamplingProbabilityObject(), 0.001f, 0.0f, 1.0f, "%.3f", sliderFlags);
        RemixGui::DragFloat("Indirect Ray Spread Angle Factor", &RtxOptions::indirectRaySpreadAngleFactorObject(), 0.001f, 0.0f, 1.0f, "%.3f", sliderFlags);

        if (RtxOptions::enableRussianRoulette() && RemixGui::CollapsingHeader("Russian Roulette", collapsingHeaderClosedFlags)) {
          ImGui::Indent();

          RemixGui::DragFloat("1st bounce: Min Continue Probability", &RtxOptions::russianRoulette1stBounceMinContinueProbabilityObject(), 0.01f, 0.0f, 1.0f, "%.3f", sliderFlags);
          RemixGui::DragFloat("1st bounce: Max Continue Probability", &RtxOptions::russianRoulette1stBounceMaxContinueProbabilityObject(), 0.01f, 0.0f, 1.0f, "%.3f", sliderFlags);
          
          secondPlusBounceRussianRouletteModeCombo.getKey(&RtxOptions::russianRouletteModeObject());
          if (RtxOptions::russianRouletteMode() == RussianRouletteMode::ThroughputBased)
          {
            RemixGui::DragFloat("2nd+ bounce: Max Continue Probability", &RtxOptions::russianRouletteMaxContinueProbabilityObject(), 0.01f, 0.0f, 1.0f, "%.3f", sliderFlags);
          }
          else
          {
            RemixGui::DragFloat("2nd+ bounce: Diffuse Continue Probability", &RtxOptions::russianRouletteDiffuseContinueProbabilityObject(), 0.01f, 0.0f, 1.0f, "%.3f", sliderFlags);
            RemixGui::DragFloat("2nd+ bounce: Specular Continue Probability", &RtxOptions::russianRouletteSpecularContinueProbabilityObject(), 0.01f, 0.0f, 1.0f, "%.3f", sliderFlags);
            RemixGui::DragFloat("2nd+ bounce: Distance Factor", &RtxOptions::russianRouletteDistanceFactorObject(), 0.01f, 0.0f, 1.0f, "%.3f", sliderFlags);
          }
          
          ImGui::Unindent();
        }
        ImGui::Unindent();
      }

      if (RtxOptions::getIsOpacityMicromapSupported() && 
          RemixGui::CollapsingHeader("Opacity Micromap", collapsingHeaderClosedFlags)) {
        ImGui::Indent();

        RemixGui::Checkbox("Enable Opacity Micromap", &RtxOptions::OpacityMicromap::enableObject());
        
        if (common->getOpacityMicromapManager())
          common->getOpacityMicromapManager()->showImguiSettings();

        ImGui::Unindent();
      }

      const VkPhysicalDeviceProperties& props = m_device->adapter()->deviceProperties();
      const NV_GPU_ARCHITECTURE_ID archId = RtxOptions::getNvidiaArch();

      // Shader Execution Reordering
      if (RtxOptions::isShaderExecutionReorderingSupported()) {
        if (RemixGui::CollapsingHeader("Shader Execution Reordering", collapsingHeaderClosedFlags)) {
          ImGui::Indent();

          if (RtxOptions::renderPassIntegrateIndirectRaytraceMode() == DxvkPathtracerIntegrateIndirect::RaytraceMode::TraceRay)
            RemixGui::Checkbox("Enable In Integrate Indirect Pass", &RtxOptions::enableShaderExecutionReorderingInPathtracerIntegrateIndirectObject());

          ImGui::Unindent();
        }
      }
      ImGui::Unindent();
    }

    if (RemixGui::CollapsingHeader("Lighting", collapsingHeaderClosedFlags)) {
      ImGui::Indent();

      common->getSceneManager().getLightManager().showImguiLightOverview();

      if (RemixGui::CollapsingHeader("Effect Light", collapsingHeaderClosedFlags)) {
        ImGui::Indent();

        ImGui::TextWrapped("These settings control the effect lights, which are created by Remix, and attached to objects tagged using the rtx.lightConverter option (found in the texture tagging menu as 'Add Light to Texture').");

        RemixGui::DragFloat("Light Intensity", &RtxOptions::effectLightIntensityObject(), 0.01f, 0.0f, FLT_MAX, "%.3f", sliderFlags);
        RemixGui::DragFloat("Light Radius", &RtxOptions::effectLightRadiusObject(), 0.01f, 0.01f, FLT_MAX, "%.3f", sliderFlags);
        // Plasma ball has first priority
        RemixGui::Checkbox("Plasma Ball Effect", &RtxOptions::effectLightPlasmaBallObject());
        ImGui::BeginDisabled(RtxOptions::effectLightPlasmaBall());
        RemixGui::ColorPicker3("Light Color", &RtxOptions::effectLightColorObject());
        ImGui::EndDisabled();
        ImGui::Unindent();
      }

      RemixGui::DragFloat("Emissive Intensity", &RtxOptions::emissiveIntensityObject(), 0.01f, 0.0f, FLT_MAX, "%.3f", sliderFlags);
      RemixGui::Separator();
      RemixGui::SliderInt("RIS Light Sample Count", &RtxOptions::risLightSampleCountObject(), 0, 64);
      RemixGui::Separator();
      RemixGui::Checkbox("Direct Lighting Enabled", &RtxOptions::enableDirectLightingObject());
      RemixGui::Checkbox("Indirect Lighting Enabled", &RtxOptions::enableSecondaryBouncesObject());

      if (RemixGui::CollapsingHeader("RTXDI", collapsingHeaderClosedFlags)) {
        ImGui::Indent();

        RemixGui::Checkbox("Enable RTXDI", &RtxOptions::useRTXDIObject());

        auto& rtxdi = common->metaRtxdiRayQuery();
        rtxdi.showImguiSettings();
        ImGui::Unindent();
      }

      // Indirect Illumination Integration Mode
      if (RemixGui::CollapsingHeader("Indirect Illumination", collapsingHeaderClosedFlags)) {
        ImGui::Indent();
        integrateIndirectModeCombo.getKey(&RtxOptions::integrateIndirectModeObject());

        if (RtxOptions::integrateIndirectMode() == IntegrateIndirectMode::ReSTIRGI) {
          if (RemixGui::CollapsingHeader("ReSTIR GI", collapsingHeaderClosedFlags)) {
            ImGui::Indent();
            ImGui::PushID("ReSTIR GI");
            auto& restirGI = common->metaReSTIRGIRayQuery();
            restirGI.showImguiSettings();
            ImGui::PopID();
            ImGui::Unindent();
          }
        } else if (RtxOptions::integrateIndirectMode() == IntegrateIndirectMode::NeuralRadianceCache) {
          if (RemixGui::CollapsingHeader("RTX Neural Radiance Cache", collapsingHeaderClosedFlags)) {

            ImGui::Indent();
            ImGui::PushID("Neural Radiance Cache");
            NeuralRadianceCache& nrc = common->metaNeuralRadianceCache();
            nrc.showImguiSettings(*ctx);
            ImGui::PopID();
            ImGui::Unindent();
          }
        }

        ImGui::Unindent();
      }

      if (RemixGui::CollapsingHeader("NEE Cache", collapsingHeaderClosedFlags)) {
        ImGui::Indent();
        ImGui::PushID("NEE Cache");
        auto& neeCache = common->metaNeeCache();
        neeCache.showImguiSettings();
        ImGui::PopID();
        ImGui::Unindent();
      }

      ImGui::Unindent();
    }

    RtxParticleSystemManager::showImguiSettings();

    if (RemixGui::CollapsingHeader("RTX Volumetrics (Global)", collapsingHeaderClosedFlags)) {
      ImGui::Indent();

      common->metaGlobalVolumetrics().showImguiSettings();

      common->metaDustParticles().showImguiSettings();

      ImGui::Unindent();
    }

    if (RemixGui::CollapsingHeader("Subsurface Scattering", collapsingHeaderClosedFlags)) {
      ImGui::Indent();

      RemixGui::Checkbox("Enable Thin Opaque", &RtxOptions::SubsurfaceScattering::enableThinOpaqueObject());
      RemixGui::Checkbox("Enable Texture Maps", &RtxOptions::SubsurfaceScattering::enableTextureMapsObject());

      RemixGui::Checkbox("Enable Diffusion Profile SSS", &RtxOptions::SubsurfaceScattering::enableDiffusionProfileObject());

      if (RtxOptions::SubsurfaceScattering::enableDiffusionProfile()) {
        RemixGui::SliderFloat("SSS Scale", &RtxOptions::SubsurfaceScattering::diffusionProfileScaleObject(), 0.0f, 100.0f);

        RemixGui::Checkbox("Enable SSS Transmission", &RtxOptions::SubsurfaceScattering::enableTransmissionObject());
        if (RtxOptions::SubsurfaceScattering::enableTransmission()) {
          RemixGui::Checkbox("Enable SSS Transmission Single Scattering", &RtxOptions::SubsurfaceScattering::enableTransmissionSingleScatteringObject());
          RemixGui::Checkbox("Enable Transmission Diffusion Profile Correction [Experimental]", &RtxOptions::SubsurfaceScattering::enableTransmissionDiffusionProfileCorrectionObject());
          RemixGui::DragInt("SSS Transmission BSDF Sample Count", &RtxOptions::SubsurfaceScattering::transmissionBsdfSampleCountObject(), 0.1f, 1, 64, "%d", ImGuiSliderFlags_AlwaysClamp);
          RemixGui::DragInt("SSS Transmission Single Scattering Sample Count", &RtxOptions::SubsurfaceScattering::transmissionSingleScatteringSampleCountObject(), 0.1f, 1, 64, "%d", ImGuiSliderFlags_AlwaysClamp);
        }
      }

      RemixGui::DragInt2("Diffusion Profile Sampling Debugging Pixel Position", &RtxOptions::SubsurfaceScattering::diffusionProfileDebugPixelPositionObject(), 0.1f, 0, INT32_MAX, "%d", ImGuiSliderFlags_AlwaysClamp);

      ImGui::Unindent();
    }

    if (RemixGui::CollapsingHeader("Alpha Test/Blending", collapsingHeaderClosedFlags)) {
      ImGui::Indent();

      RemixGui::Checkbox("Render Alpha Blended", &RtxOptions::enableAlphaBlendObject());
      RemixGui::Checkbox("Render Alpha Tested", &RtxOptions::enableAlphaTestObject());
      RemixGui::Separator();

      RemixGui::Checkbox("Emissive Blend Translation", &RtxOptions::enableEmissiveBlendModeTranslationObject());

      RemixGui::Checkbox("Emissive Blend Override", &RtxOptions::enableEmissiveBlendEmissiveOverrideObject());
      RemixGui::DragFloat("Emissive Blend Override Intensity", &RtxOptions::emissiveBlendOverrideEmissiveIntensityObject(), 0.001f, 0.0f, FLT_MAX, "%.3f", sliderFlags);

      RemixGui::Separator();
      RemixGui::SliderFloat("Particle Softness", &RtxOptions::particleSoftnessFactorObject(), 0.f, 0.5f);
      RemixGui::Separator();
      if (RemixGui::CollapsingHeader("Weighted Blended OIT", collapsingHeaderClosedFlags)) {
        RemixGui::Checkbox("Enable", &RtxOptions::wboitEnabledObject());
        ImGui::BeginDisabled(!RtxOptions::wboitEnabled());
        RemixGui::SliderFloat("Energy Compensation", &RtxOptions::wboitEnergyLossCompensationObject(), 1.f, 10.f);
        RemixGui::SliderFloat("Depth Weight Tuning", &RtxOptions::wboitDepthWeightTuningObject(), 0.01f, 10.f);
        ImGui::EndDisabled();
      }
      common->metaComposite().showStochasticAlphaBlendImguiSettings();
      ImGui::Unindent();
    }

    if (RemixGui::CollapsingHeader("Denoising", collapsingHeaderClosedFlags)) {
      bool isRayReconstructionEnabled = RtxOptions::isRayReconstructionEnabled();
      bool useNRD = !isRayReconstructionEnabled || common->metaRayReconstruction().enableNRDForTraining();
      ImGui::Indent();
      ImGui::BeginDisabled(!useNRD);
      RemixGui::Checkbox("Denoising Enabled", &RtxOptions::useDenoiserObject());
      RemixGui::Checkbox("Reference Mode | Accumulation", &RtxOptions::useDenoiserReferenceModeObject());

      if (RtxOptions::useDenoiserReferenceMode()) {
        common->metaComposite().showAccumulationImguiSettings();
      }

      ImGui::EndDisabled();

      if(RemixGui::CollapsingHeader("Settings", collapsingHeaderClosedFlags)) {
        ImGui::Indent();
        RemixGui::Checkbox("Separate Primary Direct/Indirect Denoiser", &RtxOptions::denoiseDirectAndIndirectLightingSeparatelyObject());
        RemixGui::Checkbox("Reset History On Settings Change", &RtxOptions::resetDenoiserHistoryOnSettingsChangeObject());
        RemixGui::Checkbox("Replace Direct Specular HitT with Indirect Specular HitT", &RtxOptions::replaceDirectSpecularHitTWithIndirectSpecularHitTObject());
        RemixGui::Checkbox("Use Virtual Shading Normals", &RtxOptions::useVirtualShadingNormalsForDenoisingObject());
        RemixGui::Checkbox("Adaptive Resolution Denoising", &RtxOptions::adaptiveResolutionDenoisingObject());
        RemixGui::Checkbox("Adaptive Accumulation", &RtxOptions::adaptiveAccumulationObject());
        common->metaDemodulate().showImguiSettings();
        common->metaComposite().showDenoiseImguiSettings();
        ImGui::Unindent();
      }
      bool useDoubleDenoisers = RtxOptions::denoiseDirectAndIndirectLightingSeparately();
      if (isRayReconstructionEnabled) {
        if (RemixGui::CollapsingHeader("DLSS-RR", collapsingHeaderClosedFlags)) {
          ImGui::Indent();
          ImGui::PushID("DLSS-RR");
          common->metaRayReconstruction().showRayReconstructionImguiSettings(true);
          ImGui::PopID();
          ImGui::Unindent();
        }
      }
      
      if (useNRD)
      {
        if (useDoubleDenoisers) {
          if (RemixGui::CollapsingHeader("Primary Direct Light Denoiser", collapsingHeaderClosedFlags)) {
            ImGui::Indent();
            ImGui::PushID("Primary Direct Light Denoiser");
            common->metaPrimaryDirectLightDenoiser().showImguiSettings();
            ImGui::PopID();
            ImGui::Unindent();
          }

          if (RemixGui::CollapsingHeader("Primary Indirect Light Denoiser", collapsingHeaderClosedFlags)) {
            ImGui::Indent();
            ImGui::PushID("Primary Indirect Light Denoiser");
            common->metaPrimaryIndirectLightDenoiser().showImguiSettings();
            ImGui::PopID();
            ImGui::Unindent();
          }
        } else {
          if (RemixGui::CollapsingHeader("Primary Direct/Indirect Light Denoiser", collapsingHeaderClosedFlags)) {
            ImGui::Indent();
            ImGui::PushID("Primary Direct/Indirect Light Denoiser");
            common->metaPrimaryCombinedLightDenoiser().showImguiSettings();
            ImGui::PopID();
            ImGui::Unindent();
          }
        }

        if (RemixGui::CollapsingHeader("Secondary Direct/Indirect Light Denoiser", collapsingHeaderClosedFlags)) {
          ImGui::Indent();
          ImGui::PushID("Secondary Direct/Indirect Light Denoiser");
          common->metaSecondaryCombinedLightDenoiser().showImguiSettings();
          ImGui::PopID();
          ImGui::Unindent();
        }
      }

      ImGui::Unindent();
    }

    if (RemixGui::CollapsingHeader("Post-Processing", collapsingHeaderClosedFlags)) {
      ImGui::Indent();

      if (RemixGui::CollapsingHeader("Composition", collapsingHeaderClosedFlags))
        common->metaComposite().showImguiSettings();

      if (RtxOptions::upscalerType() == UpscalerType::TAAU) {
        if (RemixGui::CollapsingHeader("TAA-U", collapsingHeaderClosedFlags))
          common->metaTAA().showImguiSettings();
      }

      if (RemixGui::CollapsingHeader("Bloom", collapsingHeaderClosedFlags))
        common->metaBloom().showImguiSettings();

      if (RemixGui::CollapsingHeader("Auto Exposure", collapsingHeaderClosedFlags))
        common->metaAutoExposure().showImguiSettings();

      if (RemixGui::CollapsingHeader("Tonemapping", collapsingHeaderClosedFlags))
      {
        RemixGui::SliderInt("User Brightness", &RtxOptions::userBrightnessObject(), 0, 100, "%d");
        RemixGui::DragFloat("User Brightness EV Range", &RtxOptions::userBrightnessEVRangeObject(), 0.5f, 0.f, 10.f, "%.1f");
        RemixGui::Separator();
        RemixGui::Combo("Tonemapping Mode", &RtxOptions::tonemappingModeObject(), "Global\0Local\0");
        if (RtxOptions::tonemappingMode() == TonemappingMode::Global) {
          common->metaToneMapping().showImguiSettings();
        } else {
          common->metaLocalToneMapping().showImguiSettings();
        }
        if (RtxOptions::showLegacyACESOption()) {
          RemixGui::Separator();
          RemixGui::Checkbox("Use Legacy ACES", &RtxOptions::useLegacyACESObject());
          if (!RtxOptions::useLegacyACES()) {
            ImGui::Indent();
            ImGui::TextWrapped("WARNING: Non-legacy ACES is currently experimental and the implementation is a subject to change.");
            ImGui::Unindent();
          }
        }
      }

      if (RemixGui::CollapsingHeader("Post FX", collapsingHeaderClosedFlags))
        common->metaPostFx().showImguiSettings();
      
      ImGui::Unindent();
    }

    if (RemixGui::CollapsingHeader("Debug", collapsingHeaderClosedFlags)) {
      ImGui::Indent();
      common->metaDebugView().showImguiSettings();
      ImGui::Unindent();
    }

    if (RemixGui::CollapsingHeader("Geometry", collapsingHeaderClosedFlags)) {
      ImGui::Indent();

      RemixGui::Checkbox("Enable Triangle Culling (Globally)", &RtxOptions::enableCullingObject());
      RemixGui::Checkbox("Enable Triangle Culling (Override Secondary Rays)", &RtxOptions::enableCullingInSecondaryRaysObject());
      RemixGui::Separator();
      RemixGui::DragInt("Min Prims in Dynamic BLAS", &RtxOptions::minPrimsInDynamicBLASObject(), 1.f, 100, 0);
      RemixGui::DragInt("Max Prims in Merged BLAS", &RtxOptions::maxPrimsInMergedBLASObject(), 1.f, 100, 0);
      RemixGui::Checkbox("Force Merge All Meshes", &RtxOptions::forceMergeAllMeshesObject());
      RemixGui::Checkbox("Minimize BLAS Merging", &RtxOptions::minimizeBlasMergingObject());
      RemixGui::Separator();
      RemixGui::Checkbox("Portals: Virtual Instance Matching", &RtxOptions::useRayPortalVirtualInstanceMatchingObject());
      RemixGui::Checkbox("Portals: Fade In Effect", &RtxOptions::enablePortalFadeInEffectObject());
      ImGui::Unindent();
    }

    if (RemixGui::CollapsingHeader("Texture Streaming [Experimental]", collapsingHeaderClosedFlags)) {
      ImGui::Indent();
      if (RtxOptions::TextureManager::hotReload()) {
        ImGui::TextColored(ImVec4{ 250 / 255.F, 176 / 255.F, 50 / 255.F, 1.F }, "Hot-reloading active.");
        ImGui::Dummy({ 0, 2 });
      }
      ImGui::BeginDisabled(!RtxOptions::TextureManager::samplerFeedbackEnable());
      {
        if (RtxOptions::TextureManager::fixedBudgetEnable() && RtxOptions::TextureManager::samplerFeedbackEnable()) {
          if (RemixGui::DragFloatMB_showGB("Texture Budget##1",
                                        &RtxOptions::TextureManager::fixedBudgetMiBObject(),
                                        0.5f, 1.f, 32.f, "%.1f GB", ImGuiSliderFlags_NoRoundToFormat)) {
            ctx->getCommonObjects()->getSceneManager().requestVramCompaction();
          }
        } else {
          // always disabled drag float just to show the available texture cache budget
          ImGui::BeginDisabled(true);
          const char* formatstr = RtxOptions::TextureManager::samplerFeedbackEnable()
            ? "%.1f GB"
            : "UNB%0.0fUND";
          static float s_dummy{};
          s_dummy = RtxOptions::TextureManager::samplerFeedbackEnable()
            ? float(g_streamedTextures_budgetBytes) / 1024.F / 1024.F / 1024.F
            : 0.F;
          RemixGui::DragFloat("Texture Cache##2", &s_dummy, 0.5f, 1.f, 32.f, formatstr, ImGuiSliderFlags_NoRoundToFormat);
          ImGui::EndDisabled();
        }
      }
      {
        ImGui::BeginDisabled(RtxOptions::TextureManager::fixedBudgetEnable());
        if (RemixGui::DragInt("of VRAM is dedicated to Textures",
                            &RtxOptions::TextureManager::budgetPercentageOfAvailableVramObject(),
                            10.F,
                            10,
                            100,
                            "%d%%")) {
          ctx->getCommonObjects()->getSceneManager().requestVramCompaction();
        }
        ImGui::EndDisabled();
      }
      if (RemixGui::Checkbox("Force Fixed Texture Budget", &RtxOptions::TextureManager::fixedBudgetEnableObject())) {
        // budgeting technique changed => ask DXVK to return unused VRAM chunks to OS to better represent consumption
        ctx->getCommonObjects()->getSceneManager().requestVramCompaction();
      }
      ImGui::EndDisabled();

      ImGui::Dummy({ 0, 2 });
      if (RemixGui::CollapsingHeader("Advanced##texstream", collapsingHeaderClosedFlags)) {
        ImGui::Indent();
        ImGui::Text("Streamed Texture VRAM usage: %.1f GB", float(g_streamedTextures_usedBytes) / 1024.F / 1024.F / 1024.F);
        ImGui::Dummy({ 0, 2 });
        RemixGui::Separator();
        ImGui::Dummy({ 0, 2 });
        ImGui::TextUnformatted("Warning: toggling this option will enforce a full texture reload.");
        if (RemixGui::Checkbox("Sampler Feedback", &RtxOptions::TextureManager::samplerFeedbackEnableObject())) {
          // sampler feedback ON/OFF changed => free all to refit textures in VRAM
          ctx->getCommonObjects()->getSceneManager().requestTextureVramFree();
        }
        ImGui::Dummy({ 0, 2 });
        RemixGui::Separator();
        ImGui::Dummy({ 0, 2 });
        if (ImGui::Button("Demote All Textures")) {
          ctx->getCommonObjects()->getSceneManager().requestTextureVramFree();
        }
        RemixGui::Checkbox("Reload Textures on Window Resize", &RtxOptions::reloadTextureWhenResolutionChangedObject());
        ImGui::Unindent();
      }
      ImGui::Unindent();
    }

    if (RemixGui::CollapsingHeader("Terrain [Experimental]")) {
      ImGui::Indent();

      {
        TerrainMode mode = TerrainMode::None;
        if (TerrainBaker::enableBaking()) {
          mode = TerrainMode::TerrainBaker;
        } else {
          if (RtxOptions::terrainAsDecalsEnabledIfNoBaker()) {
            mode = TerrainMode::AsDecals;
          }
        }

        IMGUI_ADD_TOOLTIP(
          terrainModeCombo.getKey(&mode),
          "\'Terrain Baker\': rasterize the draw calls marked as \'Terrain\' into a single mesh that would be used for ray tracing.\n"
          "\n"
          "\'Terrain-as-Decals\': draw calls marked as 'Terrain' are ray traced as decals.");

        switch (mode) {
        case TerrainMode::None: {
          TerrainBaker::enableBaking.setDeferred(false);
          RtxOptions::terrainAsDecalsEnabledIfNoBaker.setDeferred(false);
          break;
        }
        case TerrainMode::TerrainBaker: {
          TerrainBaker::enableBaking.setDeferred(true);
          RtxOptions::terrainAsDecalsEnabledIfNoBaker.setDeferred(false);
          break;
        }
        case TerrainMode::AsDecals: {
          TerrainBaker::enableBaking.setDeferred(false);
          RtxOptions::terrainAsDecalsEnabledIfNoBaker.setDeferred(true);
          break;
        }
        default: break;
        }
      }

      RemixGui::Separator();

      if (TerrainBaker::enableBaking()) {
        common->getTerrainBaker().showImguiSettings();
      } else if (RtxOptions::terrainAsDecalsEnabledIfNoBaker()) {
        RemixGui::Checkbox("Over-modulate Blending", &RtxOptions::terrainAsDecalsAllowOverModulateObject());
      }

      ImGui::Unindent();
    }

    if (RemixGui::CollapsingHeader("Player Model", collapsingHeaderClosedFlags)) {
      ImGui::Indent();
      RemixGui::Checkbox("Primary Shadows", &RtxOptions::PlayerModel::enablePrimaryShadowsObject());
      RemixGui::Checkbox("Show in Primary Space", &RtxOptions::PlayerModel::enableInPrimarySpaceObject());
      RemixGui::Checkbox("Create Virtual Instances", &RtxOptions::PlayerModel::enableVirtualInstancesObject());
      if (RemixGui::CollapsingHeader("Calibration", collapsingHeaderClosedFlags)) {
        ImGui::Indent();
        RemixGui::DragFloat("Backward Offset", &RtxOptions::PlayerModel::backwardOffsetObject(), 0.01f, 0.f, 100.f);
        RemixGui::DragFloat("Horizontal Detection Distance", &RtxOptions::PlayerModel::horizontalDetectionDistanceObject(), 0.01f, 0.f, 100.f);
        RemixGui::DragFloat("Vertical Detection Distance", &RtxOptions::PlayerModel::verticalDetectionDistanceObject(), 0.01f, 0.f, 100.f);
        ImGui::Unindent();
      }
      ImGui::Unindent();
    }

    if (RemixGui::CollapsingHeader("Displacement [Experimental]", collapsingHeaderClosedFlags)) {
      ImGui::Indent();
      ImGui::TextWrapped("Warning: This is currently implemented using POM with a simple height map, displacing inwards.  The implementation may change in the future, which could include changes to the texture format or displacing outwards.\nRaymarched POM will use a simple raymarch algorithm, and will show artifacts on thin features and at oblique angles.\nQuadtree POM depends on custom mipmaps with maximums instead of averages, which can be generated using `generate_max_mip.py`.");
      RemixGui::Combo("Mode", &RtxOptions::Displacement::modeObject(), "Off\0Raymarched POM\0Quadtree POM\0");
      RemixGui::Checkbox("Enable Direct Lighting", &RtxOptions::Displacement::enableDirectLightingObject());
      RemixGui::Checkbox("Enable Indirect Lighting", &RtxOptions::Displacement::enableIndirectLightingObject());
      RemixGui::Checkbox("Enable Indirect Hit", &RtxOptions::Displacement::enableIndirectHitObject());
      RemixGui::Checkbox("Enable NEE Cache", &RtxOptions::Displacement::enableNEECacheObject());
      RemixGui::Checkbox("Enable ReSTIR_GI", &RtxOptions::Displacement::enableReSTIRGIObject());
      RemixGui::Checkbox("Enable PSR", &RtxOptions::Displacement::enablePSRObject());
      RemixGui::DragFloat("Global Displacement Factor", &RtxOptions::Displacement::displacementFactorObject(), 0.01f, 0.0f, 20.0f);
      RemixGui::DragInt("Max Iterations", &RtxOptions::Displacement::maxIterationsObject(), 1.f, 1, 256, "%d", sliderFlags);
      ImGui::Unindent();
    }

    if (RemixGui::CollapsingHeader("Raytraced Render Target [Experimental]", collapsingHeaderClosedFlags)) {
      ImGui::Indent();
      ImGui::TextWrapped("When a screen in-game is displaying the rasterized results of another camera, this can be used to raytrace that scene.\nNote that the render target texture containing the rasterized results needs to be set to `raytracedRenderTargetTextures` in the texture selection menu.");

      RemixGui::Checkbox("Enable Raytraced Render Targets", &RtxOptions::RaytracedRenderTarget::enableObject());
      ImGui::Unindent();
    }

    if (RemixGui::CollapsingHeader("View Distance", collapsingHeaderClosedFlags)) {
      ImGui::Indent();

      viewDistanceModeCombo.getKey(&ViewDistanceOptions::distanceModeObject());

      if (ViewDistanceOptions::distanceMode() != ViewDistanceMode::None) {
        viewDistanceFunctionCombo.getKey(&ViewDistanceOptions::distanceFunctionObject());

        if (ViewDistanceOptions::distanceMode() == ViewDistanceMode::HardCutoff) {
          RemixGui::DragFloat("Distance Threshold", &ViewDistanceOptions::distanceThresholdObject(), 0.1f, 0.0f, 0.0f, "%.2f", sliderFlags);
        } else if (ViewDistanceOptions::distanceMode() == ViewDistanceMode::CoherentNoise) {
          RemixGui::DragFloat("Distance Fade Min", &ViewDistanceOptions::distanceFadeMinObject(), 0.1f, 0.0f, ViewDistanceOptions::distanceFadeMax(), "%.2f", sliderFlags);
          RemixGui::DragFloat("Distance Fade Max", &ViewDistanceOptions::distanceFadeMaxObject(), 0.1f, ViewDistanceOptions::distanceFadeMin(), 0.0f, "%.2f", sliderFlags);
          RemixGui::DragFloat("Noise Scale", &ViewDistanceOptions::noiseScaleObject(), 0.1f, 0.0f, 0.0f, "%.2f", sliderFlags);
        }
      }

      ImGui::Unindent();
    }

    if (RemixGui::CollapsingHeader("Material Filtering", collapsingHeaderClosedFlags)) {
      ImGui::Indent();

      RemixGui::Checkbox("Use White Material Textures", &RtxOptions::useWhiteMaterialModeObject());
      RemixGui::Separator();
      constexpr float kMipBiasRange = 32;
      RemixGui::DragFloat("Mip LOD Bias", &RtxOptions::nativeMipBiasObject(), 0.01f, -kMipBiasRange, kMipBiasRange, "%.2f", sliderFlags);
      RemixGui::DragFloat("Upscaling LOD Bias", &RtxOptions::upscalingMipBiasObject(), 0.01f, -kMipBiasRange, kMipBiasRange, "%.2f", sliderFlags);
      RemixGui::Separator();
      RemixGui::Checkbox("Use Anisotropic Filtering", &RtxOptions::useAnisotropicFilteringObject());
      if (RtxOptions::useAnisotropicFiltering()) {
        RemixGui::DragFloat("Max Anisotropy Samples", &RtxOptions::maxAnisotropySamplesObject(), 0.5f, 1.0f, 16.f, "%.3f", sliderFlags);
      }
      RemixGui::DragFloat("Translucent Decal Albedo Factor", &RtxOptions::translucentDecalAlbedoFactorObject(), 0.01f);
      ImGui::Unindent();
    }

    if (!RtCamera::enableFreeCamera() &&
        RemixGui::CollapsingHeader("Anti-Culling", collapsingHeaderClosedFlags)) {

      ImGui::Indent();

      if (ctx->getCommonObjects()->getSceneManager().isAntiCullingSupported()) {
        RemixGui::Checkbox("Anti-Culling Objects", &RtxOptions::AntiCulling::Object::enableObject());
        if (RtxOptions::AntiCulling::Object::enable()) {
          RemixGui::Checkbox("High precision Anti-Culling", &RtxOptions::AntiCulling::Object::enableHighPrecisionAntiCullingObject());
          if (RtxOptions::AntiCulling::Object::enableHighPrecisionAntiCulling()) {
            RemixGui::Checkbox("Infinity Far Frustum", &RtxOptions::AntiCulling::Object::enableInfinityFarFrustumObject());
          }
          RemixGui::Checkbox("Enable Bounding Box Hash For Duplication Check", &RtxOptions::AntiCulling::Object::hashInstanceWithBoundingBoxHashObject());
          RemixGui::InputInt("Instance Max Size", &RtxOptions::AntiCulling::Object::numObjectsToKeepObject(), 1, 1, 0);
          RemixGui::DragFloat("Anti-Culling Fov Scale", &RtxOptions::AntiCulling::Object::fovScaleObject(), 0.01f, 0.1f, 2.0f);
          RemixGui::DragFloat("Anti-Culling Far Plane Scale", &RtxOptions::AntiCulling::Object::farPlaneScaleObject(), 0.1f, 0.1f, 10000.0f);
        }
        RemixGui::Separator();
        RemixGui::Checkbox("Anti-Culling Lights", &RtxOptions::AntiCulling::Light::enableObject());
        if (RtxOptions::AntiCulling::Light::enable()) {
          RemixGui::InputInt("Max Number Of Lights", &RtxOptions::AntiCulling::Light::numLightsToKeepObject(), 1, 1, 0);
          RemixGui::InputInt("Max Number of Frames to keep lights", &RtxOptions::AntiCulling::Light::numFramesToExtendLightLifetimeObject(), 1, 1, 0);
          RemixGui::DragFloat("Anti-Culling Lights Fov Scale", &RtxOptions::AntiCulling::Light::fovScaleObject(), 0.01f, 0.1f, 2.0f);
        }
      } else {
        ImGui::Text("The game doesn't set up the View Matrix, \nAnti-Culling is disabled to prevent visual corruption.");
      }

      ImGui::Unindent();
    }

    ImGui::PopItemWidth();
  }

  void ImGUI::render(
    const HWND gameHwnd,
    const Rc<DxvkContext>& ctx,
    VkExtent2D         surfaceSize,
    bool               vsync) {
    ScopedGpuProfileZone(ctx, "ImGUI Render");

    if (m_overlayWin.ptr() != nullptr) {
      m_overlayWin->update(gameHwnd);
    }

    m_lastRenderVsyncStatus = vsync;

    ImGui::SetCurrentContext(m_context);
    ImPlot::SetCurrentContext(m_plotContext);

    // Sometimes games can change windows on us, so we need to check that here and tell ImGUI
    if (m_gameHwnd != gameHwnd) {
      m_gameHwnd = gameHwnd;

      if (m_init) {
        ImGui_ImplWin32_Shutdown();
      }

      ImGui_ImplWin32_Init(gameHwnd);
    }

    if (!m_init) {
      ImGui_ImplDxvk::Init(m_device);

      //execute a gpu command to upload imgui font textures
      createFontsTexture(ctx);

      m_init = true;
    }

    update(ctx);

    ImGui_ImplDxvk::RenderDrawData(ImGui::GetDrawData(), ctx.ptr(), surfaceSize.width, surfaceSize.height);

    ctx->setSpecConstant(VK_PIPELINE_BIND_POINT_GRAPHICS, 0, 0);
  }

  void ImGUI::createFontsTexture(const Rc<DxvkContext>& ctx) {
    ImGuiIO& io = ImGui::GetIO();
    ImGui_ImplDxvk::Data* bd = (ImGui_ImplDxvk::Data*)io.BackendRendererUserData;
    
    // Range of characters we want to use the primary font
    ImVector<ImWchar> characterRange;
    {
      ImFontGlyphRangesBuilder builder;
      builder.AddText("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ!\"#$ % &\'()*+,-./:;<=>?@[\\]^_`{|}~ \t\n\r\x0b\x0c");
      builder.BuildRanges(&characterRange);
    }

    // Range of characters we want to use the second (monospaced) font for
    ImVector<ImWchar> numericalRange;
    {
      ImFontGlyphRangesBuilder builder;
      builder.AddText("0123456789");
      builder.BuildRanges(&numericalRange);
    }

    // Build a second font, where all characters are consistent.  This will be used for non-field/title text
    ImVector<ImWchar> allRange;
    {
      ImFontGlyphRangesBuilder builder;
      builder.AddText("0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ!\"#$ % &\'()*+,-./:;<=>?@[\\]^_`{|}~ \t\n\r\x0b\x0c");
      builder.BuildRanges(&allRange);
    }

    // Normal Size Font (Default)

    ImFontConfig normalFontCfg = ImFontConfig();
    normalFontCfg.SizePixels = 16.f;
    normalFontCfg.FontDataOwnedByAtlas = false;

    const size_t nvidiaSansLength = sizeof(___NVIDIASansRg) / sizeof(___NVIDIASansRg[0]);
    const size_t nvidiaSansBdLength = sizeof(___NVIDIASansBd) / sizeof(___NVIDIASansBd[0]);
    const size_t robotoMonoLength = sizeof(___RobotoMonoRg) / sizeof(___RobotoMonoRg[0]);

    {
      // Add letters/symbols (NVIDIA-Sans)
      m_regularFont = io.Fonts->AddFontFromMemoryTTF(&___NVIDIASansRg[0], nvidiaSansLength, 0, &normalFontCfg, characterRange.Data);
      io.FontDefault = m_regularFont;

      // Enable merging
      normalFontCfg.MergeMode = true;

      // Add numbers (Roboto-Mono)
      io.Fonts->AddFontFromMemoryTTF(&___RobotoMonoRg[0], robotoMonoLength, 0, &normalFontCfg, numericalRange.Data);

      normalFontCfg.MergeMode = false;
      m_boldFont = io.Fonts->AddFontFromMemoryTTF(&___NVIDIASansBd[0], nvidiaSansBdLength, 0, &normalFontCfg, allRange.Data);
    }

    // Large Size Font

    ImFontConfig largeFontCfg = ImFontConfig();
    largeFontCfg.SizePixels = 24.f;
    largeFontCfg.FontDataOwnedByAtlas = false;

    {
      // Add letters/symbols (NVIDIA-Sans)
      m_largeFont = io.Fonts->AddFontFromMemoryTTF(&___NVIDIASansBd[0], nvidiaSansLength, 0, &largeFontCfg, characterRange.Data);

      // Enable merging
      largeFontCfg.MergeMode = true;

      // Add numbers (Roboto-Mono)
      io.Fonts->AddFontFromMemoryTTF(&___RobotoMonoRg[0], robotoMonoLength, 0, &largeFontCfg, numericalRange.Data);
    }

    // Build the fonts

    io.Fonts->Build();


    // Allocate/upload glyph cache...

    unsigned char* pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    size_t row_pitch = (size_t)width * 4 * sizeof(char);
    size_t upload_size = height * row_pitch;

    VkResult err;

    // Create the Image:
    {
      DxvkImageCreateInfo info = {};
      info.type = VK_IMAGE_TYPE_2D;
      info.format = VK_FORMAT_R8G8B8A8_UNORM;
      info.extent.width = width;
      info.extent.height = height;
      info.extent.depth = 1;
      info.mipLevels = 1;
      info.numLayers = 1;
      info.sampleCount = VK_SAMPLE_COUNT_1_BIT;
      info.tiling = VK_IMAGE_TILING_OPTIMAL;
      info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
      info.layout = VK_IMAGE_LAYOUT_GENERAL;
      info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
      m_fontTexture = m_device->createImage(info, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, DxvkMemoryStats::Category::AppTexture, "imgui font texture");
      bd->FontImage = m_fontTexture->handle();
    }

    // Create the Image View:
    {
      DxvkImageViewCreateInfo info = {};
      info.type = VK_IMAGE_VIEW_TYPE_2D;
      info.format = VK_FORMAT_R8G8B8A8_UNORM;
      info.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
      info.numLevels = 1;
      info.numLayers = 1;
      info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
      m_fontTextureView = m_device->createImageView(m_fontTexture, info);
      bd->FontView = m_fontTextureView->handle();
    }

    ctx->updateImage(m_fontTexture,
      VkImageSubresourceLayers{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
      VkOffset3D{ 0, 0, 0 },
      m_fontTexture->mipLevelExtent(0),
      pixels, row_pitch, upload_size);

    Rc<DxvkSampler> sampler = m_device->getCommon()->getResources().getSampler(VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);

    // Store our identifier
    io.Fonts->SetTexID(ImGui_ImplDxvk::AddTexture(sampler, m_fontTextureView));
  }

  bool ImGUI::checkHotkeyState(const VirtualKeys& virtKeys, const bool allowContinuousPress) {
    bool result = false;
    if(virtKeys.size() > 0) {
      auto& io = ImGui::GetIO();
      result = true;
      for(const auto& vk : virtKeys) {
        if(vk.val == VK_SHIFT) {
          result = result && io.KeyShift;
        } else if(vk.val == VK_CONTROL) {
          result = result && io.KeyCtrl;
        } else if(vk.val == VK_MENU) {
          result = result && io.KeyAlt;
        } else {
          ImGuiKey key = ImGui::GetKeyIndex(ImGui_ImplWin32_VirtualKeyToImGuiKey(vk.val));
          if (allowContinuousPress) {
            result = result && ImGui::IsKeyDown(key);
          } else {
            result = result && ImGui::IsKeyPressed(key, false);
          }
        }
      }
    }
    return result;
  }

  void ImGUI::onCloseMenus() {
    // When closing the menus, try and free up some extra memory, just in case
    //  the user has toggled a bunch of systems while in menus causing an artificial
    //  inflation.
    freeUnusedMemory();

    ::ShowCursor(m_prevCursorVisible);
  }

  void ImGUI::onOpenMenus() {
    // Before opening the menus, try free some memory, the idea being the 
    //  user may want to make some changes to various settings and so they
    //  should have all available memory to do so.
    freeUnusedMemory();

    CURSORINFO info;
    GetCursorInfo(&info);
    m_prevCursorVisible = info.flags == CURSOR_SHOWING;
  }

  void ImGUI::freeUnusedMemory() {
    if (!m_device) {
      return;
    }

    m_device->getCommon()->getSceneManager().requestVramCompaction();
  }

}
