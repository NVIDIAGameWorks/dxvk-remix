/*
* Copyright (c) 2021-2025, NVIDIA CORPORATION. All rights reserved.
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
#include <optional>
#include <nvapi.h>
#include <NVIDIASansMd.ttf.h>
#include <RobotoMonoRg.ttf.h>

#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_impl_vulkan.h"
#include "imgui_impl_win32.h"
#include "implot.h"
#include "dxvk_imgui.h"
#include "rtx_render/rtx_imgui.h"
#include "dxvk_device.h"
#include "rtx_render/rtx_utils.h"
#include "rtx_render/rtx_shader_manager.h"
#include "rtx_render/rtx_camera.h"
#include "rtx_render/rtx_context.h"
#include "rtx_render/rtx_hash_collision_detection.h"
#include "rtx_render/rtx_options.h"
#include "rtx_render/rtx_terrain_baker.h"
#include "rtx_render/rtx_neural_radiance_cache.h"
#include "rtx_render/rtx_rtxdi_rayquery.h"
#include "rtx_render/rtx_restir_gi_rayquery.h"
#include "dxvk_image.h"
#include "../util/rc/util_rc_ptr.h"
#include "../util/util_math.h"
#include "rtx_render/rtx_opacity_micromap_manager.h"
#include "rtx_render/rtx_bridge_message_channel.h"
#include "dxvk_imgui_about.h"
#include "dxvk_imgui_splash.h"
#include "dxvk_imgui_capture.h"
#include "dxvk_scoped_annotation.h"
#include "../../d3d9/d3d9_rtx.h"
#include "dxvk_memory_tracker.h"


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
    VkDescriptorSet texID = VK_NULL_HANDLE;
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
    {"uitextures", "UI Texture", &RtxOptions::Get()->uiTexturesObject()},
    {"worldspaceuitextures", "World Space UI Texture", &RtxOptions::Get()->worldSpaceUiTexturesObject()},
    {"worldspaceuibackgroundtextures", "World Space UI Background Texture", &RtxOptions::Get()->worldSpaceUiBackgroundTexturesObject()},
    {"skytextures", "Sky Texture", &RtxOptions::Get()->skyBoxTexturesObject()},
    {"ignoretextures", "Ignore Texture (optional)", &RtxOptions::Get()->ignoreTexturesObject()},
    {"hidetextures", "Hide Texture Instance (optional)", &RtxOptions::Get()->hideInstanceTexturesObject()},
    {"lightmaptextures","Lightmap Textures (optional)", &RtxOptions::Get()->lightmapTexturesObject()},
    {"ignorelights", "Ignore Lights (optional)", &RtxOptions::Get()->ignoreLightsObject()},
    {"particletextures", "Particle Texture (optional)", &RtxOptions::Get()->particleTexturesObject()},
    {"beamtextures", "Beam Texture (optional)", &RtxOptions::Get()->beamTexturesObject()},
    {"ignoretransparencytextures", "Ignore Transparency Layer Texture (optional)", &RtxOptions::Get()->ignoreTransparencyLayerTexturesObject()},
    {"lightconvertertextures", "Add Light to Textures (optional)", &RtxOptions::Get()->lightConverterObject()},
    {"decaltextures", "Decal Texture (optional)", &RtxOptions::Get()->decalTexturesObject()},
    {"terraintextures", "Terrain Texture", &RtxOptions::Get()->terrainTexturesObject()},
    {"watertextures", "Water Texture (optional)", &RtxOptions::Get()->animatedWaterTexturesObject()},
    {"antiCullingTextures", "Anti-Culling Texture (optional)", &RtxOptions::Get()->antiCullingTexturesObject()},
    {"motionBlurMaskOutTextures", "Motion Blur Mask-Out Textures (optional)", &RtxOptions::Get()->motionBlurMaskOutTexturesObject()},
    {"playermodeltextures", "Player Model Texture (optional)", &RtxOptions::Get()->playerModelTexturesObject()},
    {"playermodelbodytextures", "Player Model Body Texture (optional)", &RtxOptions::Get()->playerModelBodyTexturesObject()},
    {"opacitymicromapignoretextures", "Opacity Micromap Ignore Texture (optional)", &RtxOptions::Get()->opacityMicromapIgnoreTexturesObject()},
    {"ignorebakedlightingtextures","Ignore Baked Lighting Textures (optional)", &RtxOptions::Get()->ignoreBakedLightingTexturesObject()},
    {"ignorealphaontextures","Ignore Alpha Channel of Textures (optional)", &RtxOptions::Get()->ignoreAlphaOnTexturesObject()},
    {"raytracedRenderTargetTextures","Raytraced Render Target Textures (optional)", &RtxOptions::Get()->raytracedRenderTargetTexturesObject(), ImGUI::kTextureFlagsRenderTarget}
  };

  ImGui::ComboWithKey<RenderPassGBufferRaytraceMode> renderPassGBufferRaytraceModeCombo {
    "GBuffer Raytracing Mode",
    ImGui::ComboWithKey<RenderPassGBufferRaytraceMode>::ComboEntries { {
        {RenderPassGBufferRaytraceMode::RayQuery, "RayQuery (CS)"},
        {RenderPassGBufferRaytraceMode::RayQueryRayGen, "RayQuery (RGS)"},
        {RenderPassGBufferRaytraceMode::TraceRay, "TraceRay (RGS)"}
    } }
  };

  ImGui::ComboWithKey<RenderPassIntegrateDirectRaytraceMode> renderPassIntegrateDirectRaytraceModeCombo {
    "Integrate Direct Raytracing Mode",
    ImGui::ComboWithKey<RenderPassIntegrateDirectRaytraceMode>::ComboEntries { {
        {RenderPassIntegrateDirectRaytraceMode::RayQuery, "RayQuery (CS)"},
        {RenderPassIntegrateDirectRaytraceMode::RayQueryRayGen, "RayQuery (RGS)"}
    } }
  };

  ImGui::ComboWithKey<RenderPassIntegrateIndirectRaytraceMode> renderPassIntegrateIndirectRaytraceModeCombo {
    "Integrate Indirect Raytracing Mode",
    ImGui::ComboWithKey<RenderPassIntegrateIndirectRaytraceMode>::ComboEntries { {
        {RenderPassIntegrateIndirectRaytraceMode::RayQuery, "RayQuery (CS)"},
        {RenderPassIntegrateIndirectRaytraceMode::RayQueryRayGen, "RayQuery (RGS)"},
        {RenderPassIntegrateIndirectRaytraceMode::TraceRay, "TraceRay (RGS)"}
    } }
  };

  ImGui::ComboWithKey<CameraAnimationMode> cameraAnimationModeCombo {
    "Camera Animation Mode",
    ImGui::ComboWithKey<CameraAnimationMode>::ComboEntries { {
        {CameraAnimationMode::CameraShake_LeftRight, "CameraShake Left-Right"},
        {CameraAnimationMode::CameraShake_FrontBack, "CameraShake Front-Back"},
        {CameraAnimationMode::CameraShake_Yaw, "CameraShake Yaw"},
        {CameraAnimationMode::CameraShake_Pitch, "CameraShake Pitch"},
        {CameraAnimationMode::YawRotation, "Camera Yaw Rotation"}
    } }
  };

  ImGui::ComboWithKey<int> minPathBouncesCombo {
    "Min Light Bounces",
    ImGui::ComboWithKey<int>::ComboEntries { {
        {0, "0"},
        {1, "1"},
    } }
  };

  ImGui::ComboWithKey<int> maxPathBouncesCombo {
    "Max Light Bounces",
    ImGui::ComboWithKey<int>::ComboEntries { {
        {1, "1"},
        {2, "2"},
        {3, "3"},
        {4, "4"},
        {5, "5"},
        {6, "6"},
        {7, "7"},
        {8, "8"},
    } }
  };

  ImGui::ComboWithKey<int> indirectLightingParticlesCombo {
    "Particle Light",
    ImGui::ComboWithKey<int>::ComboEntries { {
        {0, "None"},
        {1, "Low"},
        {2, "High"},
    } }
  };

  ImGui::ComboWithKey<NeuralRadianceCache::QualityPreset> neuralRadianceCacheQualityPresetCombo {
    "Neural Radiance Cache Quality",
    ImGui::ComboWithKey<NeuralRadianceCache::QualityPreset>::ComboEntries { {
        {NeuralRadianceCache::QualityPreset::Ultra, "Ultra"},
        {NeuralRadianceCache::QualityPreset::High, "High"},
        {NeuralRadianceCache::QualityPreset::Medium, "Medium"}
    } }
  };

  ImGui::ComboWithKey<bool> denoiserQualityCombo {
    "NRD Denoising Quality",
    ImGui::ComboWithKey<bool>::ComboEntries { {
        {true, "High"},
        {false,"Low"},
    } }
  };

  ImGui::ComboWithKey<int> textureQualityCombo {
    "Texture Quality",
    ImGui::ComboWithKey<int>::ComboEntries { {
        {0, "High"},
        {1, "Low"},
    } }
  };

  ImGui::ComboWithKey<ViewDistanceMode> viewDistanceModeCombo {
    "View Distance Mode",
    ImGui::ComboWithKey<ViewDistanceMode>::ComboEntries { {
        {ViewDistanceMode::None, "None"},
        {ViewDistanceMode::HardCutoff, "Hard Cutoff"},
        {ViewDistanceMode::CoherentNoise, "Coherent Noise"},
    } }
  };

  ImGui::ComboWithKey<ViewDistanceFunction> viewDistanceFunctionCombo {
    "View Distance Function",
    ImGui::ComboWithKey<ViewDistanceFunction>::ComboEntries { {
        {ViewDistanceFunction::Euclidean, "Euclidean"},
        {ViewDistanceFunction::PlanarEuclidean, "Planar Euclidean"},
    } }
  };

  static auto fusedWorldViewModeCombo = ImGui::ComboWithKey<FusedWorldViewMode>(
  "Fused World-View Mode",
  ImGui::ComboWithKey<FusedWorldViewMode>::ComboEntries { {
      {FusedWorldViewMode::None, "None"},
      {FusedWorldViewMode::View, "In View Transform"},
      {FusedWorldViewMode::World, "In World Transform"},
  } });

  static auto skyAutoDetectCombo = ImGui::ComboWithKey<SkyAutoDetectMode>(
    "Sky Auto-Detect",
    ImGui::ComboWithKey<SkyAutoDetectMode>::ComboEntries{ {
      {SkyAutoDetectMode::None, "Off"},
      {SkyAutoDetectMode::CameraPosition, "By Camera Position"},
      {SkyAutoDetectMode::CameraPositionAndDepthFlags, "By Camera Position and Depth Flags"}
  } });

  static auto upscalerNoDLSSCombo = ImGui::ComboWithKey<UpscalerType>(
    "Upscaler Type",
    { {
      {UpscalerType::None, "None"},
      {UpscalerType::NIS, "NIS"},
      {UpscalerType::TAAU, "TAA-U"},
  } });

  static auto upscalerDLSSCombo = ImGui::ComboWithKey<UpscalerType>(
    "Upscaler Type",
    { {
      {UpscalerType::None, "None"},
      {UpscalerType::DLSS, "DLSS"},
      {UpscalerType::NIS, "NIS"},
      {UpscalerType::TAAU, "TAA-U"},
  } });

  ImGui::ComboWithKey<RussianRouletteMode> secondPlusBounceRussianRouletteModeCombo {
    "2nd+ Bounce Russian Roulette Mode",
    ImGui::ComboWithKey<RussianRouletteMode>::ComboEntries { {
        {RussianRouletteMode::ThroughputBased, "Throughput Based"},
        {RussianRouletteMode::SpecularBased, "Specular Based"}
    } }
  };

  ImGui::ComboWithKey<IntegrateIndirectMode> integrateIndirectModeCombo {
    "Integrate Indirect Illumination Mode",
    ImGui::ComboWithKey<IntegrateIndirectMode>::ComboEntries { {
        {IntegrateIndirectMode::ImportanceSampled, "Importance Sampled",  
          "Importance Sampled. Importance sampled mode uses typical GI sampling and it is not recommended for general use as it provides the noisiest output.\n"
          "It serves as a reference integration mode for validation of other indirect integration modes." },
        {IntegrateIndirectMode::ReSTIRGI, "ReSTIR GI", 
          "ReSTIR GI provides improved indirect path sampling over \"Importance Sampled\" mode with better indirect diffuse and specular GI quality at increased performance cost."},
        {IntegrateIndirectMode::NeuralRadianceCache, "Neural Radiance Cache", 
          "Neural Radiance Cache (NRC). NRC is an AI based world space radiance cache. It is live trained by the path tracer\n"
          "and allows paths to terminate early by looking up the cached value and saving performance.\n"
          "NRC supports infinite bounces and often provides results closer to that of reference than ReSTIR GI\n"
          "while increasing performance in scenarios where ray paths have 2 or more bounces on average."}
    } }
  };

  static auto rayReconstructionModelCombo = ImGui::ComboWithKey<DxvkRayReconstruction::RayReconstructionModel>(
    "Ray Reconstruction Model",
    { {
      {DxvkRayReconstruction::RayReconstructionModel::Transformer, "Transformer", "Ensures highest image quality. Can be more expensive than CNN in terms of memory and performance."},
      {DxvkRayReconstruction::RayReconstructionModel::CNN, "CNN", "Ensures great image quality"},
  } });

  ImGui::ComboWithKey<int> dlfgMfgModeCombo {
  "DLSS Frame Generation Mode",
  ImGui::ComboWithKey<int>::ComboEntries { {
      {1, "2x"},
      {2, "3x"},
      {3, "4x"},
  } }
  };

  enum class TerrainMode {
    None,
    TerrainBaker,
    AsDecals,
  };
  static auto terrainModeCombo = ImGui::ComboWithKey<TerrainMode>(
    "Mode##terrain",
    {
      {        TerrainMode::None,              "None"},
      {TerrainMode::TerrainBaker,     "Terrain Baker"},
      {    TerrainMode::AsDecals, "Terrain-as-Decals"},
  });

  // Styles 
  constexpr ImGuiSliderFlags sliderFlags = ImGuiSliderFlags_AlwaysClamp;
  constexpr ImGuiTreeNodeFlags collapsingHeaderClosedFlags = ImGuiTreeNodeFlags_CollapsingHeader;
  constexpr ImGuiTreeNodeFlags collapsingHeaderFlags = collapsingHeaderClosedFlags | ImGuiTreeNodeFlags_DefaultOpen;
  constexpr ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus;
  constexpr ImGuiWindowFlags popupWindowFlags = ImGuiWindowFlags_NoSavedSettings;

  ImGui::ComboWithKey<UpscalerType>& getUpscalerCombo(DxvkDLSS& dlss, DxvkRayReconstruction& rayReconstruction) {
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
    bool rayReconstruction = RtxOptions::Get()->enableRayReconstruction();
    if (RtxOptions::Get()->showRayReconstructionOption()) {
      ImGui::BeginDisabled(!supportsRR);
      ImGui::Checkbox("Ray Reconstruction", &RtxOptions::Get()->enableRayReconstructionRef());

      if (RtxOptions::Get()->enableRayReconstruction()) {
        rayReconstructionModelCombo.getKey(&DxvkRayReconstruction::modelObject());
      }
      ImGui::EndDisabled();
    }

    // Disable DLSS-RR if it's unsupported.
    if (!supportsRR) {
      RtxOptions::Get()->enableRayReconstructionRef() = false;
    }
    changed = (rayReconstruction != RtxOptions::Get()->enableRayReconstruction());
    return changed;
  }

  ImGUI::ImGUI(DxvkDevice* device)
  : m_device (device)
  , m_hwnd   (nullptr)
  , m_about  (new ImGuiAbout)
  , m_splash  (new ImGuiSplash) {
    // Clamp Option ranges

    RTX_OPTION_CLAMP(reflexStatRangeInterpolationRate, 0.0f, 1.0f);
    RTX_OPTION_CLAMP_MIN(reflexStatRangePaddingRatio, 0.0f);

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
    // See: 'ImGui_ImplVulkan_AddTexture(...)' for more details about how this system works.
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
      ImGui_ImplVulkan_Data* bd = (ImGui_ImplVulkan_Data*) io.BackendRendererUserData;
      bd->FontView = VK_NULL_HANDLE;
      bd->FontImage = VK_NULL_HANDLE;

      ImGui_ImplVulkan_Shutdown();
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
    if (RtxOptions::Get()->keepTexturesForTagging()) {
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
    ImGui::SetCurrentContext(m_context);
    ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam);
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
    UIType oldType = RtxOptions::Get()->showUI();
    if (oldType == type && !force) {
      return;
    }
    if (oldType == UIType::Basic) {
      ImGui::CloseCurrentPopup();
    }
    
    if (type == UIType::Basic) {
      ImGui::OpenPopup(m_userGraphicsWindowTitle);
    }
    
    if (type == UIType::None) {
      onCloseMenus();
    } else {
      onOpenMenus();
    }

    RtxOptions::Get()->showUIRef() = type;

    if (RtxOptions::Get()->showUICursor()) {
      ImGui::GetIO().MouseDrawCursor = type != UIType::None;
    }

    if (RtxOptions::Get()->blockInputToGameInUI()) {
      BridgeMessageChannel::get().send("UWM_REMIX_UIACTIVE_MSG",
                                       type != UIType::None ? 1 : 0, 0);
    }
  }
  
  void ImGUI::showMaterialOptions() {
    if (ImGui::CollapsingHeader("Material Options (optional)", collapsingHeaderClosedFlags)) {
      ImGui::Indent();

      if (ImGui::CollapsingHeader("Legacy Material Defaults", collapsingHeaderFlags)) {
        ImGui::Indent();

        LegacyMaterialDefaults& legacyMaterial = RtxOptions::Get()->legacyMaterial;
        ImGui::Checkbox("Use Albedo/Opacity Texture (if present)", &legacyMaterial.useAlbedoTextureIfPresentObject());
        ImGui::Checkbox("Ignore Texture Alpha Channel", &legacyMaterial.ignoreAlphaChannelObject());
        ImGui::ColorEdit3("Albedo", &legacyMaterial.albedoConstantObject());
        ImGui::DragFloat("Opacity", &legacyMaterial.opacityConstantObject(), 0.01f, 0.f, 1.f);
        ImGui::ColorEdit3("Emissive Color", &legacyMaterial.emissiveColorConstantObject());
        ImGui::DragFloat("Emissive Intensity", &legacyMaterial.emissiveIntensityObject(), 0.01f, 0.01f, FLT_MAX, "%.3f", sliderFlags);
        ImGui::DragFloat("Roughness", &legacyMaterial.roughnessConstantObject(), 0.01f, 0.02f, 1.f, "%.3f", sliderFlags);
        ImGui::DragFloat("Metallic", &legacyMaterial.metallicConstantObject(), 0.01f, 0.0f, 1.f, "%.3f", sliderFlags);
        ImGui::DragFloat("Anisotropy", &legacyMaterial.anisotropyObject(), 0.01f, -1.0f, 1.f, "%.3f", sliderFlags);

        ImGui::Unindent();
      }

      if (ImGui::CollapsingHeader("PBR Material Modifiers", collapsingHeaderFlags)) {
        ImGui::Indent();

        if (ImGui::CollapsingHeader("Opaque", collapsingHeaderFlags)) {
          ImGui::Indent();

          OpaqueMaterialOptions& opaqueMaterialOptions = RtxOptions::Get()->opaqueMaterialOptions;
          ImGui::SliderFloat("Albedo Scale", &opaqueMaterialOptions.albedoScaleObject(), 0.0f, 1.f, "%.3f", sliderFlags);
          ImGui::SliderFloat("Albedo Bias", &opaqueMaterialOptions.albedoBiasObject(), -1.0f, 1.f, "%.3f", sliderFlags);
          ImGui::SliderFloat("Metallic Scale", &opaqueMaterialOptions.metallicScaleObject(), 0.0f, 1.f, "%.3f", sliderFlags);
          ImGui::SliderFloat("Metallic Bias", &opaqueMaterialOptions.metallicBiasObject(), -1.0f, 1.f, "%.3f", sliderFlags);
          ImGui::SliderFloat("Roughness Scale", &opaqueMaterialOptions.roughnessScaleObject(), 0.0f, 1.f, "%.3f", sliderFlags);
          ImGui::SliderFloat("Roughness Bias", &opaqueMaterialOptions.roughnessBiasObject(), -1.0f, 1.f, "%.3f", sliderFlags);
          ImGui::SliderFloat("Normal Strength##1", &opaqueMaterialOptions.normalIntensityObject(), -10.0f, 10.f, "%.3f", sliderFlags);

          ImGui::Checkbox("Enable dual-layer animated water normal", &opaqueMaterialOptions.layeredWaterNormalEnableObject());

          if (opaqueMaterialOptions.layeredWaterNormalEnable()) {
            ImGui::SliderFloat2("Layered Motion Direction", &opaqueMaterialOptions.layeredWaterNormalMotionObject(), -1.0f, 1.0f, "%.3f", sliderFlags);
            ImGui::SliderFloat("Layered Motion Scale", &opaqueMaterialOptions.layeredWaterNormalMotionScaleObject(), -10.0f, 10.0f, "%.3f", sliderFlags);
            ImGui::SliderFloat("LOD bias", &opaqueMaterialOptions.layeredWaterNormalLodBiasObject(), 0.0f, 16.0f, "%.3f", sliderFlags);
          }

          ImGui::Unindent();
        }

        if (ImGui::CollapsingHeader("Translucent", collapsingHeaderFlags)) {
          ImGui::Indent();

          TranslucentMaterialOptions& translucentMaterialOptions = RtxOptions::Get()->translucentMaterialOptions;
          ImGui::SliderFloat("Transmit. Color Scale", &translucentMaterialOptions.transmittanceColorScaleObject(), 0.0f, 1.f, "%.3f", sliderFlags);
          ImGui::SliderFloat("Transmit. Color Bias", &translucentMaterialOptions.transmittanceColorBiasObject(), -1.0f, 1.f, "%.3f", sliderFlags);
          ImGui::SliderFloat("Normal Strength##2", &translucentMaterialOptions.normalIntensityObject(), -10.0f, 10.f, "%.3f", sliderFlags);

          ImGui::Unindent();
        }

        ImGui::Unindent();
      }

      if (ImGui::CollapsingHeader("PBR Material Overrides", collapsingHeaderClosedFlags)) {
        ImGui::Indent();

        if (ImGui::CollapsingHeader("Opaque", collapsingHeaderFlags)) {
          ImGui::Indent();

          OpaqueMaterialOptions& opaqueMaterialOptions = RtxOptions::Get()->opaqueMaterialOptions;
          ImGui::Checkbox("Enable Thin-Film Layer", &opaqueMaterialOptions.enableThinFilmOverrideObject());

          if (opaqueMaterialOptions.enableThinFilmOverride()) {
            ImGui::SliderFloat("Thin Film Thickness", &opaqueMaterialOptions.thinFilmThicknessOverrideObject(), 0.0f, OPAQUE_SURFACE_MATERIAL_THIN_FILM_MAX_THICKNESS, "%.1f nm", sliderFlags);
          }

          ImGui::Unindent();
        }

        if (ImGui::CollapsingHeader("Translucent", collapsingHeaderFlags)) {
          ImGui::Indent();

          ImGui::Checkbox("Enable Diffuse Layer", &RtxOptions::Get()->translucentMaterialOptions.enableDiffuseLayerOverrideObject());

          ImGui::Unindent();
        }

        ImGui::Unindent();
      }

      ImGui::Unindent();
    }
  }

  void ImGUI::processHotkeys() {
    auto& io = ImGui::GetIO();
    auto& opts = *RtxOptions::Get();

    if (checkHotkeyState(RtxOptions::Get()->remixMenuKeyBinds())) {
      if(opts.defaultToAdvancedUI()) {
        switchMenu(opts.showUI() != UIType::None ? UIType::None : UIType::Advanced);
      } else {
        switchMenu(opts.showUI() != UIType::None ? UIType::None : UIType::Basic);
      }
    }

    // Toggle ImGUI mouse cursor. Alt-Del
    if (io.KeyAlt && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Delete))) {
      opts.showUICursorRef() = !opts.showUICursor();

      io.MouseDrawCursor = opts.showUICursor() && opts.showUI() != UIType::None;
    }

    // Toggle input blocking. Alt-Backspace
    if (io.KeyAlt && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Backspace))) {
      opts.blockInputToGameInUIRef() = !opts.blockInputToGameInUI();
      sendUIActivationMessage();
    }
  }

  void ImGUI::sendUIActivationMessage() {
    auto& opts = *RtxOptions::Get();
    const bool doBlock = opts.blockInputToGameInUI() &&
      opts.showUI() != UIType::None;

    BridgeMessageChannel::get().send("UWM_REMIX_UIACTIVE_MSG",
                                     doBlock ? 1 : 0, 0);
  }

  void ImGUI::update(const Rc<DxvkContext>& ctx) {
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplWin32_NewFrame();

    ImGui::NewFrame();

    processHotkeys();
    updateQuickActions(ctx);

    m_splash->update(m_largeFont);

    m_about->update(ctx);
    
    m_capture->update(ctx);

    showDebugVisualizations(ctx);

    const auto showUI = RtxOptions::Get()->showUI();

    if (showUI == UIType::Advanced) {
      showMainMenu(ctx);

      // Uncomment to see the ImGUI demo, good reference!  Also, need to undefine IMGUI_DISABLE_DEMO_WINDOWS (in "imgui_demo.cpp")
      //ImGui::ShowDemoWindow();
    }

    if (showUI == UIType::Basic) {
      showUserMenu(ctx);
    }

    // Note: Only display the latency stats window when the Advanced UI is active as the Basic UI acts as a modal which blocks other
    // windows from being interacted with.
    if (showUI == UIType::Advanced && m_reflexLatencyStatsOpen) {
      showReflexLatencyStats();
    }

    showHudMessages(ctx);

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
        RtxOptions::Get()->enableRaytracingRef() = false;
        RtxOptions::Get()->enableReplacementLightsRef() = false;
        RtxOptions::Get()->enableReplacementMaterialsRef() = false;
        RtxOptions::Get()->enableReplacementMeshesRef() = false;
        break;
      case RtxQuickAction::kRtxOnEnhanced:
        RtxOptions::Get()->enableRaytracingRef() = true;
        RtxOptions::Get()->enableReplacementLightsRef() = true;
        RtxOptions::Get()->enableReplacementMaterialsRef() = true;
        RtxOptions::Get()->enableReplacementMeshesRef() = true;
        break;
      case RtxQuickAction::kRtxOn:
        RtxOptions::Get()->enableRaytracingRef() = true;
        RtxOptions::Get()->enableReplacementLightsRef() = false;
        RtxOptions::Get()->enableReplacementMaterialsRef() = false;
        RtxOptions::Get()->enableReplacementMeshesRef() = false;
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
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(m_windowOnRight ? viewport->Size.x - m_windowWidth : 0.f, viewport->Pos.y));
    ImGui::SetNextWindowSize(ImVec2(m_windowWidth, viewport->Size.y));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.f, 0.f, 0.f, 0.6f));

    // Remember switch state first, the switch UI when the curent window is finished.
    int switchUI = -1;

    if (ImGui::Begin("RTX Remix Developer Menu", nullptr, windowFlags)) {
      ImGui::Separator();

      ImGui::Columns(2);

      // Center align 
      const float buttonWidth = 170;
      const float width = ImGui::GetColumnWidth();
      ImGui::SetCursorPosX((width - buttonWidth) / 2);

      if (ImGui::Button("Graphics Settings Menu", ImVec2(buttonWidth, 0))) {
        switchUI = (int) UIType::Basic;
      }

      ImGui::NextColumn();

      ImGui::Checkbox("Always Developer Menu", &RtxOptions::Get()->defaultToAdvancedUIObject());
      
      ImGui::EndColumns();

      ImGui::Separator();

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
              showAppConfig(ctx);
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

      m_windowWidth = ImGui::GetWindowWidth();
    }

    ImGui::Checkbox("Save Changed Settings Only", &RtxOptions::Get()->serializeChangedOptionOnlyObject());
    if (ImGui::Button("Save Settings")) {
      RtxOptions::Get()->serialize();
    }
    ImGui::SetTooltipToLastWidgetOnHover("This will save above settings in the rtx.conf file. Some may only take effect on next launch.");

    ImGui::SameLine();
    if (ImGui::Button("Reset Settings")) {
      RtxOptions::Get()->reset();
    }

    ImGui::SameLine();
    if (ImGui::Button("Hide UI")) {
      switchUI = (int) UIType::None;
    }
    ImGui::Text("Alt + Del: toggle cursor");
    ImGui::SameLine();
    ImGui::Text("Alt + Backspace: toggle game input");
    ImGui::PopStyleColor();
    ImGui::End();

    if (switchUI >= 0) {
      switchMenu((UIType) switchUI);
    }
  }

  void ImGUI::showUserMenu(const Rc<DxvkContext>& ctx) {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();

    // Record the texture setting at the first frame it shows up
    static int lastFrameID = -1;
    int currentFrameID = ctx->getDevice()->getCurrentFrameId();

    // Open popup if it's specified by user settings
    if (lastFrameID == -1) {
      ImGui::OpenPopup(m_userGraphicsWindowTitle);
    }

    ImGui::SetNextWindowPos(ImVec2(viewport->Size.x * 0.5 - m_userWindowWidth * 0.5, viewport->Size.y * 0.5 - m_userWindowHeight * 0.5));
    ImGui::SetNextWindowSize(ImVec2(m_userWindowWidth, 0));

    // Note: When changing this padding consider:
    // - Checking to ensure text including less visible instances from hover tooltips and etc do not take up more
    // lines such that empty text lines become ineffective (to prevent jittering when text changes).
    // - Updating Dummy elements as they currently are based on half the y padding for spacing consistency.
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(74, 10));

    if (ImGui::BeginPopupModal(m_userGraphicsWindowTitle, NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
      // Always display memory stats to user.
      showMemoryStats();

      const int itemWidth = 140;
      const int subItemWidth = 120;
      constexpr int subItemIndent = (itemWidth > subItemWidth) ? (itemWidth - subItemWidth) : 0;

      ImGui::PushItemWidth(itemWidth);

      const static ImGuiTabBarFlags tab_bar_flags = ImGuiTabBarFlags_NoCloseWithMiddleMouseButton;
      const static ImGuiTabItemFlags tab_item_flags = ImGuiTabItemFlags_NoCloseWithMiddleMouseButton;

      {
        ImGui::TextSeparator("Display Settings");
        ImGui::SliderInt("Brightness##user", &RtxOptions::userBrightnessObject(), 0, 100, "%d", ImGuiSliderFlags_AlwaysClamp);
        ImGui::Dummy({ 0.f, 4.f });
      }

      if (ImGui::BeginTabBar("Settings Tabs", tab_bar_flags)) {
        if (ImGui::BeginTabItem("General", nullptr, tab_item_flags)) {
          showUserGeneralSettings(ctx, subItemWidth, subItemIndent);

          ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Rendering", nullptr, tab_item_flags)) {
          showUserRenderingSettings(ctx, subItemWidth, subItemIndent);

          ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Content", nullptr, tab_item_flags)) {
          showUserContentSettings(ctx, subItemWidth, subItemIndent);

          ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
      }

      ImGui::Separator();
      ImGui::Dummy(ImVec2(0.0f, 5.0f));

      // Center align 
      const float buttonWidth = 170;
      const float width = ImGui::GetWindowSize().x;
      ImGui::SetCursorPosX((width - (buttonWidth * 3)) / 2);

      if (ImGui::Button("Developer Settings Menu", ImVec2(buttonWidth, 0))) {
        switchMenu(UIType::Advanced);
      }

      ImGui::SameLine();

      if (ImGui::Button("Save Settings", ImVec2(buttonWidth, 0))) {
        RtxOptions::Get()->serialize();
        m_userGraphicsSettingChanged = false;
      }

      ImGui::SetTooltipToLastWidgetOnHover("This will save above settings in the rtx.conf file. Some may only take effect on next launch.");

      ImGui::SameLine();

      if (ImGui::Button("Close", ImVec2(buttonWidth, 0))) {
        switchMenu(UIType::None);
      }

      if (m_userGraphicsSettingChanged) {
        ImGui::TextWrapped("Settings have been changed, click 'Save Settings' to save them and persist on next launch");
      }

      ImGui::PopItemWidth();
      ImGui::EndPopup();
    }

    ImGui::PopStyleVar();

    lastFrameID = currentFrameID;
  }

  void ImGUI::showUserGeneralSettings(
    const Rc<DxvkContext>& ctx,
    const int subItemWidth,
    const int subItemIndent) {
    auto common = ctx->getCommonObjects();
    DxvkDLSS& dlss = common->metaDLSS();
    DxvkRayReconstruction& rayReconstruction = common->metaRayReconstruction();
    DxvkDLFG& dlfg = common->metaDLFG();
    const RtxReflex& reflex = m_device->getCommon()->metaReflex();

    const bool dlssSupported = dlss.supportsDLSS();
    const bool dlfgSupported = dlfg.supportsDLFG();
    const bool dlssRRSupported = rayReconstruction.supportsRayReconstruction();
    const bool reflexInitialized = reflex.reflexInitialized();

    // Describe the tab

    const char* tabDescriptionText = "General performance settings. Enabling upscaling is recommended to significantly increase performance.";

    // Note: Specifically reference the DLSS preset when present.
    if (dlssSupported) {
      tabDescriptionText = "General performance settings. Enabling the DLSS preset is recommended to significantly increase performance.";
    }

    ImGui::TextWrapped(tabDescriptionText);

    ImGui::Dummy(ImVec2(0.0f, 5.0f));

    // Preset Settings

    if (dlssSupported) {
      const char* dlssPresetText = "DLSS Preset";
      const DlssPreset prevDlssPreset = RtxOptions::Get()->dlssPreset();

      ImGui::TextSeparator("Preset Settings");

      {
        m_userGraphicsSettingChanged |= ImGui::Combo(dlssPresetText, &RtxOptions::Get()->dlssPresetObject(), "Disabled\0Enabled\0Custom\0");
      }

      // Revert back to default DLSS settings when switch from Off to Custom
      if (prevDlssPreset == DlssPreset::Off && RtxOptions::Get()->dlssPreset() == DlssPreset::Custom) {
        RtxOptions::Get()->resetUpscaler();
      }

      RtxOptions::Get()->updateUpscalerFromDlssPreset();
    }

    // Note: Disable all settings in this section beyond the preset when a non-Custom DLSS preset is in use,
    // but only when DLSS is actually supported.
    // Note: This is stored as a bool and applied in a SetDisabled per-section so that the section labels do not get disabled
    // (as this changes the color of the line and text which is undesirable).
    const bool disableNonPresetSettings = RtxOptions::Get()->dlssPreset() != DlssPreset::Custom && dlssSupported;

    // Upscaling Settings

    ImGui::TextSeparator("Upscaling Settings");

    {
      ImGui::BeginDisabled(disableNonPresetSettings);

      // Upscaler Type

      // Note: Use a different combo box without DLSS's upscaler listed if DLSS overall is unsupported.
      auto oldUpscalerType = RtxOptions::Get()->upscalerType();
      bool oldDLSSRREnabled = RtxOptions::Get()->enableRayReconstruction();

      if (dlss.supportsDLSS()) {
        m_userGraphicsSettingChanged |= getUpscalerCombo(dlss, rayReconstruction).getKey(&RtxOptions::Get()->upscalerTypeObject());
      }
      
      ImGui::PushItemWidth(static_cast<float>(subItemWidth));
      ImGui::Indent(static_cast<float>(subItemIndent));

      if (dlss.supportsDLSS()) {
        m_userGraphicsSettingChanged |= showRayReconstructionEnable(dlssRRSupported);

        // If DLSS-RR is toggled, need to update some path tracer options accordingly to improve quality
        if (oldUpscalerType != RtxOptions::Get()->upscalerType() || oldDLSSRREnabled != RtxOptions::Get()->enableRayReconstruction()) {
          RtxOptions::Get()->updateLightingSetting();
        }
      } else {
        m_userGraphicsSettingChanged |= getUpscalerCombo(dlss, rayReconstruction).getKey(&RtxOptions::Get()->upscalerTypeObject());
      }

      // Upscaler Preset


      switch (RtxOptions::Get()->upscalerType()) {
        case UpscalerType::DLSS: 
        if (RtxOptions::Get()->enableRayReconstruction() == false) {
          m_userGraphicsSettingChanged |= ImGui::Combo("DLSS Mode", &RtxOptions::Get()->qualityDLSSObject(), "Ultra Performance\0Performance\0Balanced\0Quality\0Auto\0");

          // Display DLSS Upscaling Information

          const auto currentDLSSProfile = dlss.getCurrentProfile();
          uint32_t dlssInputWidth, dlssInputHeight;

          dlss.getInputSize(dlssInputWidth, dlssInputHeight);

          ImGui::TextWrapped(str::format("Computed DLSS Mode: ", dlssProfileToString(currentDLSSProfile), ", Render Resolution: ", dlssInputWidth, "x", dlssInputHeight).c_str());
        } else {
          m_userGraphicsSettingChanged |= ImGui::Combo("DLSS Mode", &RtxOptions::Get()->qualityDLSSObject(), "Ultra Performance\0Performance\0Balanced\0Quality\0Auto\0");

          // Display DLSS Upscaling Information

          const auto currentDLSSProfile = rayReconstruction.getCurrentProfile();
          uint32_t dlssInputWidth, dlssInputHeight;

          rayReconstruction.getInputSize(dlssInputWidth, dlssInputHeight);

          ImGui::TextWrapped(str::format("Computed DLSS Mode: ", dlssProfileToString(currentDLSSProfile), ", Render Resolution: ", dlssInputWidth, "x", dlssInputHeight).c_str());
        }
        break;
        case UpscalerType::NIS: {
          m_userGraphicsSettingChanged |= ImGui::Combo("NIS Preset", &RtxOptions::Get()->nisPresetObject(), "Performance\0Balanced\0Quality\0Fullscreen\0");
          RtxOptions::Get()->updateUpscalerFromNisPreset();

          // Display NIS Upscaling Information

          auto resolutionScale = RtxOptions::Get()->getResolutionScale();

          ImGui::TextWrapped(str::format("NIS Resolution Scale: ", resolutionScale).c_str());

          break;
        }
        case UpscalerType::TAAU: {
          m_userGraphicsSettingChanged |= ImGui::Combo("TAA-U Preset", &RtxOptions::Get()->taauPresetObject(), "Ultra Performance\0Performance\0Balanced\0Quality\0Fullscreen\0");
          RtxOptions::Get()->updateUpscalerFromTaauPreset();

          // Display TAA-U Upscaling Information

          auto resolutionScale = RtxOptions::Get()->getResolutionScale();

          ImGui::TextWrapped(str::format("TAA-U Resolution Scale: ", resolutionScale).c_str());

          break;
        }
      }

      ImGui::Unindent(static_cast<float>(subItemIndent));
      ImGui::PopItemWidth();

      ImGui::EndDisabled();
    }

    // Latency Reduction Settings
    if (dlfgSupported) {
      ImGui::TextSeparator("Frame Generation Settings");
      showDLFGOptions(ctx);
    }

    if (reflexInitialized) {
      ImGui::TextSeparator("Latency Reduction Settings");

      {
        ImGui::BeginDisabled(disableNonPresetSettings);

        // Note: Option to toggle the stats window is set to false here as this window is currently
        // set up to display only when the "advanced" developer settings UI is active.
        showReflexOptions(ctx, false);

        ImGui::EndDisabled();
      }
    }

    ImGui::Dummy(ImVec2(0.0f, 5.0f));
  }

  void ImGUI::showUserRenderingSettings(
    const Rc<DxvkContext>& ctx,
    const int subItemWidth,
    const int subItemIndent) {
    auto common = ctx->getCommonObjects();
    DxvkPostFx& postFx = common->metaPostFx();
    DxvkRtxdiRayQuery& rtxdiRayQuery = common->metaRtxdiRayQuery();
    DxvkReSTIRGIRayQuery& restirGiRayQuery = common->metaReSTIRGIRayQuery();

    // Describe the tab

    ImGui::TextWrapped("Rendering-specific settings. Complexity of rendering may be adjusted to balance between performance and quality.");

    ImGui::Dummy(ImVec2(0.0f, 5.0f));

    // Preset Settings

    ImGui::TextSeparator("Preset Settings");

    {
      m_userGraphicsSettingChanged |= ImGui::Combo("Rendering Preset", &RtxOptions::Get()->graphicsPresetObject(), "Ultra\0High\0Medium\0Low\0Custom\0");
    }

    // Map settings to indirect particle level
    int indirectLightParticlesLevel = 0;
    if (RtxOptions::Get()->enableUnorderedResolveInIndirectRays()) {
      indirectLightParticlesLevel = RtxOptions::Get()->enableUnorderedEmissiveParticlesInIndirectRays() ? 2 : 1;
    }

    // Map presets to options

    if (m_userGraphicsSettingChanged) {
      RtxOptions::Get()->updateGraphicsPresets(ctx->getDevice().ptr());
    }

    // Path Tracing Settings

    ImGui::TextSeparator("Path Tracing Settings");

    {
      // Note: Disabled flags should match preset mapping above to prevent changing settings when a preset overrides them.
      ImGui::BeginDisabled(RtxOptions::Get()->graphicsPreset() != GraphicsPreset::Custom);

      m_userGraphicsSettingChanged |= minPathBouncesCombo.getKey(&RtxOptions::Get()->pathMinBouncesObject());
      m_userGraphicsSettingChanged |= maxPathBouncesCombo.getKey(&RtxOptions::Get()->pathMaxBouncesObject());
      m_userGraphicsSettingChanged |= ImGui::Checkbox("Enable Volumetric Lighting", &RtxGlobalVolumetrics::enableObject());
      m_userGraphicsSettingChanged |= indirectLightingParticlesCombo.getKey(&indirectLightParticlesLevel);
      ImGui::SetTooltipToLastWidgetOnHover("Controls the quality of particles in indirect (reflection/GI) rays.");

      // NRC Quality Preset dropdown
      NeuralRadianceCache& nrc = common->metaNeuralRadianceCache();
      if (nrc.checkIsSupported(m_device)) {
        bool enableNeuralRadianceCache = RtxOptions::integrateIndirectMode() == IntegrateIndirectMode::NeuralRadianceCache;

        // Disable NRC quality preset combo when NRC is not enabled.
        ImGui::BeginDisabled(!enableNeuralRadianceCache);
        
        if (neuralRadianceCacheQualityPresetCombo.getKey(&NeuralRadianceCache::NrcOptions::qualityPresetObject())) {
          nrc.applyQualityPreset();
          m_userGraphicsSettingChanged = true;
        }

        ImGui::EndDisabled();
      }

      // Hide NRD denoiser quality list when DLSS-RR is enabled.
      bool useRayReconstruction = RtxOptions::Get()->isRayReconstructionEnabled();
      if (!useRayReconstruction) {
        m_userGraphicsSettingChanged |= denoiserQualityCombo.getKey(&RtxOptions::Get()->denoiseDirectAndIndirectLightingSeparatelyObject());
      }

      ImGui::EndDisabled();
    }

    // Post Effect Settings

    ImGui::TextSeparator("Post Effect Settings");

    {
      // Note: Disabled flags should match preset mapping above to prevent changing settings when a preset overrides them.
      ImGui::BeginDisabled(RtxOptions::Get()->graphicsPreset() == GraphicsPreset::Medium || RtxOptions::Get()->graphicsPreset() == GraphicsPreset::Low);

      m_userGraphicsSettingChanged |= ImGui::Checkbox("Enable Post Effects", &postFx.enableObject());

      {
        ImGui::PushItemWidth(static_cast<float>(subItemWidth));
        ImGui::Indent(static_cast<float>(subItemIndent));

        ImGui::BeginDisabled(!postFx.enable());

        m_userGraphicsSettingChanged |= ImGui::Checkbox("Enable Motion Blur", &postFx.enableMotionBlurObject());
        m_userGraphicsSettingChanged |= ImGui::Checkbox("Enable Chromatic Aberration", &postFx.enableChromaticAberrationObject());
        m_userGraphicsSettingChanged |= ImGui::Checkbox("Enable Vignette", &postFx.enableVignetteObject());

        ImGui::EndDisabled();

        ImGui::Unindent(static_cast<float>(subItemIndent));
        ImGui::PopItemWidth();
      }

      ImGui::EndDisabled();
    }

    // Other Settings

    ImGui::TextSeparator("Other Settings");

    {
      showVsyncOptions(true);
    }

    // Map indirect particle level back to settings
    if (RtxOptions::Get()->graphicsPreset() == GraphicsPreset::Custom) {
      switch (indirectLightParticlesLevel) {
      case 0:
        RtxOptions::Get()->enableUnorderedEmissiveParticlesInIndirectRaysRef() = false;
        RtxOptions::Get()->enableUnorderedResolveInIndirectRaysRef() = false;
        break;
      case 1:
        RtxOptions::Get()->enableUnorderedEmissiveParticlesInIndirectRaysRef() = false;
        RtxOptions::Get()->enableUnorderedResolveInIndirectRaysRef() = true;
        break;
      case 2:
        RtxOptions::Get()->enableUnorderedEmissiveParticlesInIndirectRaysRef() = true;
        RtxOptions::Get()->enableUnorderedResolveInIndirectRaysRef() = true;
        break;
      }
    }

    ImGui::Dummy(ImVec2(0.0f, 5.0f));
  }

  void ImGUI::showUserContentSettings(
    const Rc<DxvkContext>& ctx,
    const int subItemWidth,
    const int subItemIndent) {
    auto common = ctx->getCommonObjects();

    // Describe the tab

    ImGui::TextWrapped("Content-specific settings. Allows control of what types of assets Remix should replace (if any).");

    ImGui::Dummy(ImVec2(0.0f, 5.0f));

    ImGui::BeginDisabled(!common->getSceneManager().areAllReplacementsLoaded());

    m_userGraphicsSettingChanged |= ImGui::Checkbox("Enable All Enhanced Assets", &RtxOptions::Get()->enableReplacementAssetsObject());

    {
      ImGui::PushItemWidth(static_cast<float>(subItemWidth));
      ImGui::Indent(static_cast<float>(subItemIndent));

      ImGui::BeginDisabled(!RtxOptions::Get()->enableReplacementAssets());

      m_userGraphicsSettingChanged |= ImGui::Checkbox("Enable Enhanced Materials", &RtxOptions::Get()->enableReplacementMaterialsObject());
      m_userGraphicsSettingChanged |= ImGui::Checkbox("Enable Enhanced Meshes", &RtxOptions::Get()->enableReplacementMeshesObject());
      m_userGraphicsSettingChanged |= ImGui::Checkbox("Enable Enhanced Lights", &RtxOptions::Get()->enableReplacementLightsObject());

      ImGui::EndDisabled();

      ImGui::Unindent(static_cast<float>(subItemIndent));
      ImGui::PopItemWidth();
    }

    ImGui::EndDisabled();

    ImGui::Dummy(ImVec2(0.0f, 5.0f));
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
            ImGui::Separator();
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

  void ImGUI::showAppConfig(const Rc<DxvkContext>& ctx) {
    ImGui::PushItemWidth(250);
    if (ImGui::Button("Take Screenshot")) {
      RtxContext::triggerScreenshot();
    }

    ImGui::SetTooltipToLastWidgetOnHover("Screenshot will be dumped to, '<exe-dir>/Screenshots'");

    ImGui::SameLine(200.f);
    ImGui::Checkbox("Include G-Buffer", &RtxOptions::Get()->captureDebugImageObject());
        
#ifdef REMIX_DEVELOPMENT
    { // Recompile Shaders button and its status message
      if (ImGui::Button("Recompile Shaders")) {
        if (ShaderManager::getInstance()->reloadShaders())
          m_shaderMessage = ShaderMessageType::Ok;
        else
          m_shaderMessage = ShaderMessageType::Error;

        // Set a 5 seconds timeout to hide the message later
        m_shaderMessageTimeout = std::chrono::steady_clock::now() + std::chrono::seconds(5);
      }

      if (m_shaderMessage != ShaderMessageType::None) {
        // Display the message: green OK if successful, red ERROR if not
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, m_shaderMessage == ShaderMessageType::Ok ? 0xff40ff40 : 0xff4040ff);
        ImGui::TextUnformatted(m_shaderMessage == ShaderMessageType::Ok ? "OK" : "ERROR");
        ImGui::PopStyleColor();

        // Hide the message after a timeout
        if (std::chrono::steady_clock::now() > m_shaderMessageTimeout) {
          m_shaderMessage = ShaderMessageType::None;
        }
      }

      ImGui::SameLine(200.f);
      ImGui::Checkbox("Live shader edit mode", &RtxOptions::Shader::useLiveEditModeObject());

      ImGui::InputText("Shader Binary File Path", &RtxOptions::Shader::shaderBinaryPathObject(), ImGuiInputTextFlags_EnterReturnsTrue);
    }
#endif

    showVsyncOptions(false);

    // Render GUI for memory profiler here
    GpuMemoryTracker::renderGui();

    if (ImGui::CollapsingHeader("Camera", collapsingHeaderFlags)) {
      ImGui::Indent();

      RtCamera::showImguiSettings();

      {
        ImGui::PushID("CameraInfos");
        auto& cameraManager = ctx->getCommonObjects()->getSceneManager().getCameraManager();
        if (ImGui::CollapsingHeader("Types", collapsingHeaderClosedFlags)) {
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
            if (ImGui::CollapsingHeader(name, collapsingHeaderFlags)) {
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

      if (ImGui::CollapsingHeader("Camera Animation", collapsingHeaderClosedFlags)) {
        ImGui::Checkbox("Animate Camera", &RtxOptions::Get()->shakeCameraObject());
        cameraAnimationModeCombo.getKey(&RtxOptions::Get()->cameraAnimationModeObject());
        ImGui::DragFloat("Animation Amplitude", &RtxOptions::Get()->cameraAnimationAmplitudeObject(), 0.1f, 0.f, 1000.f, "%.2f", sliderFlags);
        ImGui::DragInt("Shake Period", &RtxOptions::Get()->cameraShakePeriodObject(), 0.1f, 1, 100, "%d", sliderFlags);
      }

      if (ImGui::CollapsingHeader("Advanced", collapsingHeaderClosedFlags)) {

        ImGui::Checkbox("Portals: Camera History Correction", &RtxOptions::Get()->rayPortalCameraHistoryCorrectionObject());
        ImGui::Checkbox("Portals: Camera In-Between Portals Correction", &RtxOptions::Get()->rayPortalCameraInBetweenPortalsCorrectionObject());

        if (RtxOptions::Get()->rayPortalCameraInBetweenPortalsCorrection()) {
          ImGui::Indent();

          ImGui::DragFloat("Portals: Camera In-Between Portals Correction Threshold", &RtxOptions::Get()->rayPortalCameraInBetweenPortalsCorrectionThresholdObject(), 0.01f, 0.0f, FLT_MAX, "%.3f", sliderFlags);

          ImGui::Unindent();
        }

        ImGui::Checkbox("Skip Objects Rendered with Unknown Camera", &RtxOptions::Get()->skipObjectsWithUnknownCameraObject());

        ImGui::Checkbox("Override Near Plane (if less than original)", &RtxOptions::Get()->enableNearPlaneOverrideObject());
        ImGui::BeginDisabled(!RtxOptions::Get()->enableNearPlaneOverride());
        ImGui::DragFloat("Desired Near Plane Distance", &RtxOptions::Get()->nearPlaneOverrideObject(), 0.01f, 0.0001f, FLT_MAX, "%.3f");
        ImGui::EndDisabled();
      }
      ImGui::Unindent();
    }

    if (ImGui::CollapsingHeader("Camera Sequence", collapsingHeaderClosedFlags)) {
      ImGui::Indent();
      RtCameraSequence::getInstance()->showImguiSettings();
      ImGui::Unindent();
    }

    if (ImGui::CollapsingHeader("Developer Options", collapsingHeaderFlags)) {
      ImGui::Indent();
      ImGui::Checkbox("Enable Instance Debugging", &RtxOptions::enableInstanceDebuggingToolsObject());
      ImGui::Checkbox("Disable Draw Calls Post RTX Injection", &RtxOptions::Get()->skipDrawCallsPostRTXInjectionObject());
      ImGui::Checkbox("Break into Debugger On Press of Key 'B'", &RtxOptions::enableBreakIntoDebuggerOnPressingBObject());
      if (ImGui::Checkbox("Block Input to Game in UI", &RtxOptions::Get()->blockInputToGameInUIObject())) {
        sendUIActivationMessage();
      }
      ImGui::Checkbox("Force Camera Jitter", &RtxOptions::Get()->forceCameraJitterObject());
      ImGui::DragInt("Camera Jitter Sequence Length", &RtxOptions::Get()->cameraJitterSequenceLengthObject());
      
      ImGui::DragIntRange2("Draw Call Range Filter", &RtxOptions::Get()->drawCallRangeObject(), 1.f, 0, INT32_MAX, nullptr, nullptr, ImGuiSliderFlags_AlwaysClamp);
      ImGui::InputInt("Instance Index Start", &RtxOptions::Get()->instanceOverrideInstanceIdxObject());
      ImGui::InputInt("Instance Index Range", &RtxOptions::Get()->instanceOverrideInstanceIdxRangeObject());
      ImGui::DragFloat3("Instance World Offset", &RtxOptions::Get()->instanceOverrideWorldOffsetObject(), 0.1f, -100.f, 100.f, "%.3f", sliderFlags);
      ImGui::Checkbox("Instance - Print Hash", &RtxOptions::Get()->instanceOverrideSelectedInstancePrintMaterialHashObject());

#ifdef REMIX_DEVELOPMENT
      ImGui::Checkbox("Show DLSS-RR Options", &RtxOptions::Get()->showRayReconstructionUIObject());
#endif

      ImGui::Unindent();
      ImGui::Checkbox("Throttle presents", &RtxOptions::Get()->enablePresentThrottleObject());
      if (RtxOptions::Get()->enablePresentThrottle()) {
        ImGui::Indent();
        ImGui::SliderInt("Present delay", &RtxOptions::Get()->presentThrottleDelayObject(), 1, 1000, "%d ms", sliderFlags);
        ImGui::Unindent();
      }
      ImGui::Checkbox("Hash Collision Detection", &HashCollisionDetectionOptions::enableObject());
      ImGui::Checkbox("Validate CPU index data", &RtxOptions::Get()->validateCPUIndexDataObject());
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

    std::string makeTextureInfo(XXH64_hash_t texHash, bool isMaterialReplacement) {
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
      
      return str.str();
    }

    void toggleTextureSelection(XXH64_hash_t textureHash, const char* uniqueId, fast_unordered_set& textureSet) {
      if (textureHash == kEmptyHash) {
        return;
      }

      const auto textureIterator = textureSet.find(textureHash);
      const char* action;
      if (textureIterator != textureSet.end()) {
        textureSet.erase(textureIterator);
        action = "removed";
      } else {
        textureSet.insert(textureHash);
        action = "added";
      }

      char buffer[256];
      sprintf_s(buffer, "%s - %s %016llX\n", uniqueId, action, textureHash);
      Logger::info(buffer);
    }

    fast_unordered_set* findTextureSetByUniqueId(const char* uniqueId) {
      if (uniqueId) {
        for (RtxTextureOption& category : rtxTextureOptions) {
          if (strcmp(category.uniqueId, uniqueId) == 0) {
            return &category.textureSetOption->getValue();
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

      void openImguiPopupOrToogle() {
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
                                   *textureSet);
          }
        } else {
          ImGui::OpenPopup(POPUP_NAME);
        }
      }

      void open(std::optional<XXH64_hash_t> texHash) {
        g_holdingTexture.exchange(texHash.value_or(kEmptyHash));
        g_openWhenAvailable = false;
        // no need to wait, open immediately
        openImguiPopupOrToogle();
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
            openImguiPopupOrToogle();
            g_openWhenAvailable = false;
          }
        }
        
        if (ImGui::BeginPopup(POPUP_NAME)) {
          const XXH64_hash_t texHash = g_holdingTexture.load();
          if (texHash != kEmptyHash) {
            ImGui::Text("Texture Info:\n%s", makeTextureInfo(texHash, isMaterialReplacement(sceneMgr, texHash)).c_str());
            if (ImGui::Button("Copy Texture hash##texture_popup")) {
              ImGui::SetClipboardText(hashToString(texHash).c_str());
            }
            uint32_t textureFeatureFlags = 0;
            const auto& pair = g_imguiTextureMap.find(texHash);
            if (pair != g_imguiTextureMap.end()) {
              textureFeatureFlags = pair->second.textureFeatureFlags;
            }
            for (auto& rtxOption : rtxTextureOptions) {
              rtxOption.bufferToggle = rtxOption.textureSetOption->getValue().count(texHash) > 0;
              if ((rtxOption.featureFlagMask & textureFeatureFlags) != rtxOption.featureFlagMask) {
                // option requires a feature, but the texture doesn't have that feature.
                continue;
              }
              if (IMGUI_ADD_TOOLTIP(ImGui::Checkbox(rtxOption.displayName, &rtxOption.bufferToggle), rtxOption.textureSetOption->getDescription())) {
                toggleTextureSelection(texHash, rtxOption.uniqueId, rtxOption.textureSetOption->getValue());
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
      const float ymax = 0.65f;
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
        auto& textureSet = listRtxOption.textureSetOption->getValue();
        textureHasSelection = textureSet.find(texHash) != textureSet.end();

        if ((listRtxOption.featureFlagMask & texImgui.textureFeatureFlags) != listRtxOption.featureFlagMask) {
          // If the list needs to be filtered by texture feature, skip it for this category.
          continue;
        }
      } else {
        for (const auto rtxOption : rtxTextureOptions) {
          auto& textureSet = rtxOption.textureSetOption->getValue();
          textureHasSelection = textureSet.find(texHash) != textureSet.end();
          if (textureHasSelection) {
            break;
          }
        }
      }

      if (legacyTextureGuiShowAssignedOnly()) {
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
        const float anim = animatedHighlightIntensity(common->getSceneManager().getGameTimeSinceStartMS());
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(anim * color.x, anim * color.y, anim * color.z, 1.f));
      } else if (textureHasSelection) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.996078f, 0.329412f, 0.f, 1.f));
      } else {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.f, 0.f, 0.f, 1.00f));
      }

      // Lazily create the tex ID ImGUI wants
      if (texImgui.texID == VK_NULL_HANDLE) {
        texImgui.texID = ImGui_ImplVulkan_AddTexture(VK_NULL_HANDLE, texImgui.imageView->handle(), VK_IMAGE_LAYOUT_GENERAL);

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
            if (rtxOption.textureSetOption->getValue().count(texHash) > 0) {
              if (rtxTextureSelection.empty()) {
                rtxTextureSelection = "\n";
              }
              rtxTextureSelection = str::format(rtxTextureSelection, " - ", rtxOption.displayName, "\n");
            }
          }
          ImGui::SetTooltip("%s(Left click to assign categories. Middle click to copy a texture hash.)\n\nCurrent categories:%s",
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
    ImGui::PushItemWidth(200);

    m_capture->show(ctx);
    
    if(ImGui::CollapsingHeader("Enhancements", collapsingHeaderFlags | ImGuiTreeNodeFlags_DefaultOpen)) {
      ImGui::Indent();
      showEnhancementsTab(ctx);
      ImGui::Unindent();
    }
  }
  
  void ImGUI::showEnhancementsTab(const Rc<DxvkContext>& ctx) {
    if (!ctx->getCommonObjects()->getSceneManager().areAllReplacementsLoaded()) {
      ImGui::Text("No USD enhancements detected, the following options have been disabled.  See documentation for how to use enhancements with Remix.");
    }

    ImGui::BeginDisabled(!ctx->getCommonObjects()->getSceneManager().areAllReplacementsLoaded());
    ImGui::Checkbox("Enable Enhanced Assets", &RtxOptions::Get()->enableReplacementAssetsObject());
    {
      ImGui::Indent();
      ImGui::BeginDisabled(!RtxOptions::Get()->enableReplacementAssets());

      ImGui::Checkbox("Enable Enhanced Materials", &RtxOptions::Get()->enableReplacementMaterialsObject());
      ImGui::Checkbox("Enable Enhanced Meshes", &RtxOptions::Get()->enableReplacementMeshesObject());
      ImGui::Checkbox("Enable Enhanced Lights", &RtxOptions::Get()->enableReplacementLightsObject());

      ImGui::EndDisabled();
      ImGui::Unindent();
    }
    ImGui::EndDisabled();
    ImGui::Separator();
    ImGui::Checkbox("Highlight Legacy Materials (flash red)", &RtxOptions::Get()->useHighlightLegacyModeObject());
    ImGui::Checkbox("Highlight Legacy Meshes with Shared Vertex Buffers (dull purple)", &RtxOptions::Get()->useHighlightUnsafeAnchorModeObject());
    ImGui::Checkbox("Highlight Replacements with Unstable Anchors (flash red)", &RtxOptions::Get()->useHighlightUnsafeReplacementModeObject());

  }

  namespace {
    std::optional<float> calculateTextureCategoryHeight(bool onlySelected, const char* uniqueId,
                                                        uint32_t numThumbnailsPerRow, float thumbnailSize) {
      constexpr float HeightLimit = 600;
      if (strcmp(uniqueId, Uncategorized) == 0) {
        return HeightLimit;
      }

      const fast_unordered_set* selected = nullptr;
      if (onlySelected) {
        const auto found = std::find_if(rtxTextureOptions.begin(), rtxTextureOptions.end(),
          [&](const RtxTextureOption& o) {
            return strcmp(o.uniqueId, uniqueId) == 0;
          });
        if (found == rtxTextureOptions.end() || !found->textureSetOption) {
          assert(0);
          return {};
        }
        selected = &found->textureSetOption->getValue();
      }

      float height = -1;
      uint32_t textureCount = 0;
      for (const auto& [texHash, texImgui] : g_imguiTextureMap) {
        if (selected) {
          if (selected->find(texHash) == selected->end()) {
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
      ImGui::Separator();
      spacing();
    };

    constexpr ImGuiTabBarFlags tab_bar_flags = ImGuiTabBarFlags_NoCloseWithMiddleMouseButton;
    constexpr ImGuiTabItemFlags tab_item_flags = ImGuiTabItemFlags_NoCloseWithMiddleMouseButton;
    if (!ImGui::BeginTabBar("##showSetupWindow", tab_bar_flags)) {
      return;
    }
    ImGui::PushItemWidth(200);

    texture_popup::lastOpenCategoryActive = false;

    const float thumbnailScale = RtxOptions::textureGridThumbnailScale();
    const float thumbnailSize = (120.f * thumbnailScale);
    const float thumbnailSpacing = ImGui::GetStyle().ItemSpacing.x;
    const float thumbnailPadding = ImGui::GetStyle().CellPadding.x;
    const uint32_t numThumbnailsPerRow = uint32_t(std::max(1.f, (m_windowWidth - 18.f) / (thumbnailSize + thumbnailSpacing + thumbnailPadding * 2.f)));

    if (IMGUI_ADD_TOOLTIP(ImGui::BeginTabItem("Step 1: Categorize Textures", nullptr, tab_item_flags), "Select texture definitions for Remix")) {
      spacing();
      ImGui::Checkbox("Preserve discarded textures", &RtxOptions::Get()->keepTexturesForTaggingObject());
      separator();

      // set thumbnail size
      {
        constexpr int step = 25;
        int percentage = static_cast<int>(round(100.f * RtxOptions::textureGridThumbnailScale()));

        float buttonsize = ImGui::GetFont() ? ImGui::GetFont()->FontSize * 1.3f : 4;
        if (ImGui::Button("-##thumbscale", { buttonsize, buttonsize })) {
          percentage = std::max(25, percentage - step);
        }
        ImGui::SameLine();
        if (ImGui::Button("+##thumbscale", { buttonsize, buttonsize })) {
          percentage = std::min(300, percentage + step);
        }
        ImGui::SameLine();
        ImGui::Text("Texture Thumbnail Scale: %d%%", percentage);
        if (ImGui::IsItemHovered()) {
          ImGui::SetTooltipUnformatted(RtxOptions::textureGridThumbnailScaleDescription());
        }

        RtxOptions::textureGridThumbnailScaleRef() = static_cast<float>(percentage) / 100.f;
      }

      ImGui::Checkbox("Split Texture Category List", &showLegacyTextureGuiObject());
      ImGui::BeginDisabled(!showLegacyTextureGui());
      ImGui::Checkbox("Only Show Assigned Textures in Category Lists", &legacyTextureGuiShowAssignedOnlyObject());
      ImGui::EndDisabled();

      separator();

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
            ImGui::CollapsingHeader(label.c_str(), collapsingHeaderClosedFlags);
            ImGui::EndDisabled();
            return;
          }
          const bool isForToggle = (texture_popup::lastOpenCategoryId == uniqueId);
          if (isForToggle) {
            ImGui::PushStyleColor(ImGuiCol_Header, ImVec4 { 0.996078f, 0.329412f, 0.f, 1.f });
            ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4 { 0.996078f, 0.329412f, 0.f, 1.f });
          }
          if (IMGUI_ADD_TOOLTIP(ImGui::CollapsingHeader(displayName, collapsingHeaderClosedFlags), description)) {
            if (height) {
              if (ImGui::IsItemToggledOpen() || texture_popup::lastOpenCategoryId.empty()) {
                // Update last opened category ID if texture category (ImGui::CollapsingHeader) was just toggled open or if ID is empty
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
          showLegacyGui(category.uniqueId, category.displayName, category.textureSetOption->getDescription());
        }

        // Check if last saved category was closed this frame
        if (!texture_popup::lastOpenCategoryActive) {
          texture_popup::lastOpenCategoryId.clear();
        }
      }

      separator();
      ImGui::EndTabItem();
    }

    if (ImGui::BeginTabItem("Step 2: Parameter Tuning", nullptr, tab_item_flags)) {
      spacing();
      ImGui::DragFloat("Scene Unit Scale", &RtxOptions::Get()->sceneScaleObject(), 0.00001f, 0.00001f, FLT_MAX, "%.5f", sliderFlags);
      ImGui::Checkbox("Scene Z-Up", &RtxOptions::Get()->zUpObject());
      ImGui::Checkbox("Scene Left-Handed Coordinate System", &RtxOptions::Get()->leftHandedCoordinateSystemObject());
      fusedWorldViewModeCombo.getKey(&RtxOptions::Get()->fusedWorldViewModeRef());
      ImGui::Separator();

      ImGui::DragFloat("Unique Object Search Distance", &RtxOptions::Get()->uniqueObjectDistanceObject(), 0.01f, FLT_MIN, FLT_MAX, "%.3f", sliderFlags);
      ImGui::Separator();

      ImGui::DragFloat("Vertex Color Strength", &RtxOptions::Get()->vertexColorStrengthObject(), 0.001f, 0.0f, 1.0f);
      ImGui::Separator();

      if (ImGui::CollapsingHeader("Heuristics", collapsingHeaderClosedFlags)) {
        ImGui::Indent();
        ImGui::Checkbox("Orthographic Is UI", &D3D9Rtx::orthographicIsUIObject());
        ImGui::Checkbox("Allow Cubemaps", &D3D9Rtx::allowCubemapsObject());
        ImGui::Checkbox("Always Calculate AABB (For Instance Matching)", &RtxOptions::Get()->enableAlwaysCalculateAABBObject());
        ImGui::Checkbox("Skip Sky Fog Values", &RtxOptions::fogIgnoreSkyObject());
        ImGui::Unindent();
      }

      if (ImGui::CollapsingHeader("Texture Parameters", collapsingHeaderClosedFlags)) {
        ImGui::Indent();
        ImGui::DragFloat("Force Cutout Alpha", &RtxOptions::Get()->forceCutoutAlphaObject(), 0.01f, 0.0f, 1.0f, "%.3f", sliderFlags);
        ImGui::DragFloat("World Space UI Background Offset", &RtxOptions::Get()->worldSpaceUiBackgroundOffsetObject(), 0.01f, -FLT_MAX, FLT_MAX, "%.3f", sliderFlags);
        ImGui::Checkbox("Ignore last texture stage", &RtxOptions::ignoreLastTextureStageObject());
        ImGui::Checkbox("Enable Multiple Stage Texture Factor Blending", &RtxOptions::enableMultiStageTextureFactorBlendingObject());
        ImGui::Unindent();
      }

      if (ImGui::CollapsingHeader("Shader Support (Experimental)", collapsingHeaderClosedFlags)) {
        ImGui::Indent();
        ImGui::Checkbox("Capture Vertices from Shader", &D3D9Rtx::useVertexCaptureObject());
        ImGui::Checkbox("Capture Normals from Shader", &D3D9Rtx::useVertexCapturedNormalsObject());
        ImGui::Separator();
        ImGui::Checkbox("Use World Transforms", &D3D9Rtx::useWorldMatricesForShadersObject());
        ImGui::Unindent();
      }

      if (ImGui::CollapsingHeader("View Model", collapsingHeaderClosedFlags)) {
        ImGui::Indent();
        ImGui::Checkbox("Enable View Model", &RtxOptions::ViewModel::enableObject());
        ImGui::SliderFloat("Max Z Threshold", &RtxOptions::ViewModel::maxZThresholdObject(), 0.0f, 1.0f);
        ImGui::Checkbox("Virtual Instances", &RtxOptions::ViewModel::enableVirtualInstancesObject());
        ImGui::Checkbox("Perspective Correction", &RtxOptions::ViewModel::perspectiveCorrectionObject());
        ImGui::DragFloat("Scale", &RtxOptions::ViewModel::scaleObject(), 0.01f, 0.01f, 2.0f);
        ImGui::Unindent();
      }

      if (ImGui::CollapsingHeader("Sky Tuning", collapsingHeaderClosedFlags)) {
        ImGui::Indent();
        ImGui::DragFloat("Sky Brightness", &RtxOptions::Get()->skyBrightnessObject(), 0.01f, 0.01f, FLT_MAX, "%.3f", sliderFlags);
        ImGui::InputInt("First N Untextured Draw Calls", &RtxOptions::Get()->skyDrawcallIdThresholdObject(), 1, 1, 0);
        ImGui::SliderFloat("Sky Min Z Threshold", &RtxOptions::Get()->skyMinZThresholdObject(), 0.0f, 1.0f);
        skyAutoDetectCombo.getKey(&RtxOptions::Get()->skyAutoDetectObject());

        if (ImGui::CollapsingHeader("Advanced", collapsingHeaderClosedFlags)) {
          ImGui::Indent();

          ImGui::Checkbox("Reproject Sky to Main Camera", &RtxOptions::skyReprojectToMainCameraSpaceObject());
          {
            ImGui::BeginDisabled(!RtxOptions::skyReprojectToMainCameraSpace());
            ImGui::DragFloat("Reprojected Sky Scale", &RtxOptions::skyReprojectScaleObject(), 1.0f, 0.1f, 1000.0f);
            ImGui::EndDisabled();
          }
          ImGui::DragFloat("Sky Auto-Detect Unique Camera Search Distance", &RtxOptions::skyAutoDetectUniqueCameraDistanceObject(), 1.0f, 0.1f, 1000.0f);

          ImGui::Checkbox("Force HDR sky", &RtxOptions::Get()->skyForceHDRObject());

          static const char* exts[] = { "256 (1.5MB vidmem)", "512 (6MB vidmem)", "1024 (24MB vidmem)",
            "2048 (96MB vidmem)", "4096 (384MB vidmem)", "8192 (1.5GB vidmem)" };

          static int extIdx;
          extIdx = std::clamp(bit::tzcnt(RtxOptions::Get()->skyProbeSide()), 8u, 13u) - 8;

          ImGui::Combo("Sky Probe Extent", &extIdx, exts, IM_ARRAYSIZE(exts));
          RtxOptions::Get()->skyProbeSideRef() = 1 << (extIdx + 8);

          ImGui::Unindent();
        }
        ImGui::Unindent();
      }

      auto common = ctx->getCommonObjects();
      common->getSceneManager().getLightManager().showImguiSettings();

      showMaterialOptions();

      if (ImGui::CollapsingHeader("Fog Tuning", collapsingHeaderClosedFlags)) {
        ImGui::Indent();
        ImGui::PushID("FogInfos");
        if (ImGui::CollapsingHeader("Explanation", collapsingHeaderClosedFlags)) {
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

      separator();
      ImGui::EndTabItem();
    }

    ImGui::PopItemWidth();
    ImGui::EndTabBar();
  }

  void ImGUI::setupStyle(ImGuiStyle* dst) {
    ImGuiStyle* style = dst ? dst : &ImGui::GetStyle();

    style->Colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.f, 0.f, 0.f, 0.4f);
    style->TabRounding = 1;
  }

  void ImGUI::showVsyncOptions(bool enableDLFGGuard) {
    // we should never get here without a swapchain, so we must have latched the vsync value already
    assert(RtxOptions::Get()->enableVsync() != EnableVsync::WaitingForImplicitSwapchain);
    
    if (enableDLFGGuard && DxvkDLFG::enable()) {
      ImGui::BeginDisabled();
    }

    bool vsyncEnabled = RtxOptions::Get()->enableVsync() == EnableVsync::On;
    ImGui::Checkbox("Enable V-Sync", &vsyncEnabled);
    RtxOptions::Get()->enableVsyncRef() = vsyncEnabled ? EnableVsync::On : EnableVsync::Off;

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

    m_userGraphicsSettingChanged |= ImGui::Checkbox("Enable DLSS Frame Generation", &DxvkDLFG::enableObject());
    if (supportsMultiFrame) {
      dlfgMfgModeCombo.getKey(&DxvkDLFG::maxInterpolatedFramesObject());
    }

    const auto& reason = ctx->getCommonObjects()->metaNGXContext().getDLFGNotSupportedReason();
    if (reason.size()) {
      ImGui::SetTooltipToLastWidgetOnHover(reason.c_str());
      ImGui::TextWrapped(reason.c_str());
    }

    if (!supportsDLFG) {
      ImGui::EndDisabled();
    }

    // Force Reflex on when using G
    if (supportsDLFG && ctx->isDLFGEnabled()) {
      RtxOptions::Get()->reflexModeRef() = ReflexMode::LowLatency;
    } else {
      DxvkDLFG::enableRef() = false;
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
      m_userGraphicsSettingChanged |= ImGui::Combo("Reflex", &RtxOptions::Get()->reflexModeObject(), "Disabled\0Enabled\0Enabled + Boost\0");
      ImGui::EndDisabled();
    }

    // Add a button to toggle the Reflex latency stats Window if requested

    if (displayStatsWindowToggle) {
      if (ImGui::Button("Toggle Latency Stats Window")) {
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
    ImGui::SetTooltipToLastWidgetOnHover("This measures the time from the start of the simulation to the end of the GPU rendering as a total game to render latency.");

    ImGui::Separator();

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

    ImGui::Separator();

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

  namespace {
    // Function is needed just to print megabytes as gigabytes,
    // as ImGui doesn't have a custom formatting to convert e.g. '1234' to '1.234'
    // Returns true if user modified the value. Overwrites value_megabytes.
    bool dragFloatMB_showGB(const char* label,
                            int* value_megabytes,
                            float* storage_gigabytes,
                            float vmin_gigabytes,
                            float vmax_gigabytes,
                            const char* description) {
      *storage_gigabytes = std::clamp(float(*value_megabytes) / 1024.f, 0.1F, vmax_gigabytes);
      // imgui for that float
      bool hasChanged = IMGUI_ADD_TOOLTIP(
        ImGui::DragFloat(label, storage_gigabytes, 0.5f, vmin_gigabytes, vmax_gigabytes, "%.1f GB", ImGuiSliderFlags_NoRoundToFormat),
        description);
      int megabytes = int(*storage_gigabytes * 1024);

      // convert back to int megabytes, quantizing by 256mb
      constexpr int Quantize = 256;
      int quantizedMegabytes = (std::clamp(megabytes, Quantize, int(vmax_gigabytes * 1024)) / Quantize) * Quantize;

      *value_megabytes = quantizedMegabytes;
      return hasChanged;
    }
  } // namespace

  void ImGUI::showRenderingSettings(const Rc<DxvkContext>& ctx) {
    ImGui::PushItemWidth(200);
    auto common = ctx->getCommonObjects();

    ImGui::Text("Disclaimer: The following settings are intended for developers,\nchanging them may introduce instability.");
    ImGui::Separator();

    // Always display memory stats to user.
    showMemoryStats();

    ImGui::Separator();

    if (ImGui::CollapsingHeader("General", collapsingHeaderFlags)) {
      auto& dlss = common->metaDLSS();
      auto& rayReconstruction = common->metaRayReconstruction();
      ImGui::Indent();

      if (RtxOptions::Get()->showRaytracingOption()) {
        ImGui::Checkbox("Raytracing Enabled", &RtxOptions::Get()->enableRaytracingObject());

        renderPassGBufferRaytraceModeCombo.getKey(&RtxOptions::Get()->renderPassGBufferRaytraceModeObject());
        renderPassIntegrateDirectRaytraceModeCombo.getKey(&RtxOptions::Get()->renderPassIntegrateDirectRaytraceModeObject());
        renderPassIntegrateIndirectRaytraceModeCombo.getKey(&RtxOptions::Get()->renderPassIntegrateIndirectRaytraceModeObject());

        ImGui::Separator();
      }

      showDLFGOptions(ctx);

      ImGui::Separator();

      showReflexOptions(ctx, true);

      ImGui::Separator();

      if (ctx->getCommonObjects()->metaDLSS().supportsDLSS()) {
        // Show upscaler and DLSS-RR option.
        auto oldUpscalerType = RtxOptions::Get()->upscalerType();
        bool oldDLSSRREnabled = RtxOptions::Get()->enableRayReconstruction();
        getUpscalerCombo(dlss, rayReconstruction).getKey(&RtxOptions::Get()->upscalerTypeObject());
        showRayReconstructionEnable(rayReconstruction.supportsRayReconstruction());

        // Update path tracer settings when upscaler is changed or DLSS-RR is toggled.
        if (oldUpscalerType != RtxOptions::Get()->upscalerType() || oldDLSSRREnabled != RtxOptions::Get()->enableRayReconstruction()) {
          RtxOptions::Get()->updateLightingSetting();
        }
      } else {
        getUpscalerCombo(dlss, rayReconstruction).getKey(&RtxOptions::Get()->upscalerTypeObject());
      }

      RtxOptions::Get()->updatePresetFromUpscaler();

      if (RtxOptions::Get()->upscalerType() == UpscalerType::DLSS && !ctx->getCommonObjects()->metaDLSS().supportsDLSS())
        RtxOptions::Get()->upscalerTypeRef() = UpscalerType::TAAU;

      if (RtxOptions::Get()->isRayReconstructionEnabled()) {
        ImGui::Combo("DLSS mode", &RtxOptions::Get()->qualityDLSSObject(), "Ultra Performance\0Performance\0Balanced\0Quality\0Auto\0Full Resolution\0");
        rayReconstruction.showRayReconstructionImguiSettings(false);
      } else if (RtxOptions::Get()->upscalerType() == UpscalerType::DLSS) {
        ImGui::Combo("DLSS mode", &RtxOptions::Get()->qualityDLSSObject(), "Ultra Performance\0Performance\0Balanced\0Quality\0Auto\0Full Resolution\0");
        dlss.showImguiSettings();
      } else if (RtxOptions::Get()->upscalerType() == UpscalerType::NIS) {
        ImGui::SliderFloat("Resolution scale", &RtxOptions::Get()->resolutionScaleObject(), 0.5f, 1.0f);
        ImGui::SliderFloat("Sharpness", &ctx->getCommonObjects()->metaNIS().m_sharpness, 0.1f, 1.0f);
        ImGui::Checkbox("Use FP16", &ctx->getCommonObjects()->metaNIS().m_useFp16);
      } else if (RtxOptions::Get()->upscalerType() == UpscalerType::TAAU) {
        ImGui::SliderFloat("Resolution scale", &RtxOptions::Get()->resolutionScaleObject(), 0.5f, 1.0f);
      }

      ImGui::Separator();

      ImGui::Checkbox("Allow Full Screen Exclusive?", &RtxOptions::Get()->allowFSEObject());

      ImGui::Unindent();
    }

    if (ImGui::CollapsingHeader("Pathtracing", collapsingHeaderClosedFlags)) {
      ImGui::Indent();

      ImGui::Checkbox("RNG: seed with frame index", &RtxOptions::Get()->rngSeedWithFrameIndexObject());

      if (ImGui::CollapsingHeader("Resolver", collapsingHeaderClosedFlags)) {
        ImGui::Indent();

        ImGui::DragInt("Max Primary Interactions", &RtxOptions::Get()->primaryRayMaxInteractionsObject(), 1.0f, 1, 255, "%d", sliderFlags);
        ImGui::DragInt("Max PSR Interactions", &RtxOptions::Get()->psrRayMaxInteractionsObject(), 1.0f, 1, 255, "%d", sliderFlags);
        ImGui::DragInt("Max Secondary Interactions", &RtxOptions::Get()->secondaryRayMaxInteractionsObject(), 1.0f, 1, 255, "%d", sliderFlags);
        ImGui::Checkbox("Separate Unordered Approximations", &RtxOptions::Get()->enableSeparateUnorderedApproximationsObject());
        ImGui::Checkbox("Direct Translucent Shadows", &RtxOptions::Get()->enableDirectTranslucentShadowsObject());
        ImGui::Checkbox("Direct Alpha Blended Shadows", &RtxOptions::Get()->enableDirectAlphaBlendShadowsObject());
        ImGui::Checkbox("Indirect Translucent Shadows", &RtxOptions::Get()->enableIndirectTranslucentShadowsObject());
        ImGui::Checkbox("Indirect Alpha Blended Shadows", &RtxOptions::Get()->enableIndirectAlphaBlendShadowsObject());
        ImGui::Checkbox("Decal Material Blending", &RtxOptions::Get()->enableDecalMaterialBlendingObject());
        ImGui::Checkbox("Billboard Orientation Correction", &RtxOptions::Get()->enableBillboardOrientationCorrectionObject());
        if (RtxOptions::Get()->enableBillboardOrientationCorrection()) {
          ImGui::Indent();
          ImGui::Checkbox("Dev: Use i-prims on primary rays", &RtxOptions::Get()->useIntersectionBillboardsOnPrimaryRaysObject());
          ImGui::Unindent();
        }
        ImGui::Checkbox("Track Particle Object", &RtxOptions::Get()->trackParticleObjectsObject());

        ImGui::SliderFloat("Resolve Transparency Threshold", &RtxOptions::Get()->resolveTransparencyThresholdObject(), 0.0f, 1.0f);
        RtxOptions::Get()->resolveOpaquenessThresholdRef() = std::max(RtxOptions::Get()->resolveTransparencyThreshold(), RtxOptions::Get()->resolveOpaquenessThreshold());
        ImGui::SliderFloat("Resolve Opaqueness Threshold", &RtxOptions::Get()->resolveOpaquenessThresholdObject(), 0.0f, 1.0f);

        ImGui::Unindent();
      }

      if (ImGui::CollapsingHeader("PSR", collapsingHeaderClosedFlags)) {
        ImGui::Indent();

        ImGui::Checkbox("Reflection PSR Enabled", &RtxOptions::Get()->enablePSRRObject());
        ImGui::Checkbox("Transmission PSR Enabled", &RtxOptions::Get()->enablePSTRObject());
        // # bounces limitted by 8b allocation in payload
        // Note: value of 255 effectively means unlimited bounces, and we don't want to allow that
        ImGui::DragInt("Max Reflection PSR Bounces", &RtxOptions::Get()->psrrMaxBouncesObject(), 1.0f, 1, 254, "%d", sliderFlags);
        ImGui::DragInt("Max Transmission PSR Bounces", &RtxOptions::Get()->pstrMaxBouncesObject(), 1.0f, 1, 254, "%d", sliderFlags);
        ImGui::Checkbox("Outgoing Transmission Approx Enabled", &RtxOptions::Get()->enablePSTROutgoingSplitApproximationObject());
        ImGui::Checkbox("Incident Transmission Approx Enabled", &RtxOptions::Get()->enablePSTRSecondaryIncidentSplitApproximationObject());
        ImGui::DragFloat("Reflection PSR Normal Detail Threshold", &RtxOptions::Get()->psrrNormalDetailThresholdObject(), 0.001f, 0.f, 1.f);
        ImGui::DragFloat("Transmission PSR Normal Detail Threshold", &RtxOptions::Get()->pstrNormalDetailThresholdObject(), 0.001f, 0.f, 1.f);

        ImGui::Unindent();
      }

      if (ImGui::CollapsingHeader("Integrator", collapsingHeaderClosedFlags)) {
        ImGui::Indent();

        ImGui::Checkbox("Enable Secondary Bounces", &RtxOptions::Get()->enableSecondaryBouncesObject());
        ImGui::Checkbox("Enable Russian Roulette", &RtxOptions::Get()->enableRussianRouletteObject());
        ImGui::Checkbox("Enable Probability Dithering Filtering for Primary Bounce", &RtxOptions::Get()->enableFirstBounceLobeProbabilityDitheringObject());
        ImGui::Checkbox("Unordered Resolve in Indirect Rays", &RtxOptions::enableUnorderedResolveInIndirectRaysObject());
        ImGui::BeginDisabled(!RtxOptions::enableUnorderedResolveInIndirectRays());
        ImGui::Checkbox("Probabilistic Unordered Resolve in Indirect Rays", &RtxOptions::enableProbabilisticUnorderedResolveInIndirectRaysObject());
        ImGui::EndDisabled();
        ImGui::Checkbox("Unordered Emissive Particles in Indirect Rays", &RtxOptions::Get()->enableUnorderedEmissiveParticlesInIndirectRaysObject());
        ImGui::Checkbox("Transmission Approximation in Indirect Rays", &RtxOptions::Get()->enableTransmissionApproximationInIndirectRaysObject());
        // # bounces limitted by 4b allocation in payload
        // Note: It's possible get up to 16 bounces => will require logic adjustment
        ImGui::DragInt("Minimum Path Bounces", &RtxOptions::Get()->pathMinBouncesObject(), 1.0f, 0, 15, "%d", sliderFlags);
        ImGui::DragInt("Maximum Path Bounces", &RtxOptions::Get()->pathMaxBouncesObject(), 1.0f, RtxOptions::Get()->pathMinBounces(), 15, "%d", sliderFlags);
        ImGui::DragFloat("Firefly Filtering Luminance Threshold", &RtxOptions::Get()->fireflyFilteringLuminanceThresholdObject(), 0.1f, 0.0f, FLT_MAX, "%.3f", sliderFlags);
        ImGui::DragFloat("Secondary Specular Firefly Filtering Threshold", &RtxOptions::Get()->secondarySpecularFireflyFilteringThresholdObject(), 0.1f, 0.0f, FLT_MAX, "%.3f", sliderFlags);
        ImGui::DragFloat("Opaque Diffuse Lobe Probability Zero Threshold", &RtxOptions::Get()->opaqueDiffuseLobeSamplingProbabilityZeroThresholdObject(), 0.001f, 0.0f, 1.0f, "%.3f", sliderFlags);
        ImGui::DragFloat("Min Opaque Diffuse Lobe Probability", &RtxOptions::Get()->minOpaqueDiffuseLobeSamplingProbabilityObject(), 0.001f, 0.0f, 1.0f, "%.3f", sliderFlags);
        ImGui::DragFloat("Opaque Specular Lobe Probability Zero Threshold", &RtxOptions::Get()->opaqueSpecularLobeSamplingProbabilityZeroThresholdObject(), 0.001f, 0.0f, 1.0f, "%.3f", sliderFlags);
        ImGui::DragFloat("Min Opaque Specular Lobe Probability", &RtxOptions::Get()->minOpaqueSpecularLobeSamplingProbabilityObject(), 0.001f, 0.0f, 1.0f, "%.3f", sliderFlags);
        ImGui::DragFloat("Opaque Opacity Transmission Lobe Probability Zero Threshold", &RtxOptions::Get()->opaqueOpacityTransmissionLobeSamplingProbabilityZeroThresholdObject(), 0.001f, 0.0f, 1.0f, "%.3f", sliderFlags);
        ImGui::DragFloat("Min Opaque Opacity Transmission Lobe Probability", &RtxOptions::Get()->minOpaqueOpacityTransmissionLobeSamplingProbabilityObject(), 0.001f, 0.0f, 1.0f, "%.3f", sliderFlags);
        ImGui::DragFloat("Diffuse Transmission Lobe Probability Zero Threshold", &RtxOptions::Get()->opaqueDiffuseTransmissionLobeSamplingProbabilityZeroThresholdObject(), 0.001f, 0.0f, 1.0f, "%.3f", sliderFlags);
        ImGui::DragFloat("Min Diffuse Transmission Lobe Probability", &RtxOptions::Get()->minOpaqueDiffuseTransmissionLobeSamplingProbabilityObject(), 0.001f, 0.0f, 1.0f, "%.3f", sliderFlags);
        ImGui::DragFloat("Translucent Specular Lobe Probability Zero Threshold", &RtxOptions::Get()->translucentSpecularLobeSamplingProbabilityZeroThresholdObject(), 0.001f, 0.0f, 1.0f, "%.3f", sliderFlags);
        ImGui::DragFloat("Min Translucent Specular Lobe Probability", &RtxOptions::Get()->minTranslucentSpecularLobeSamplingProbabilityObject(), 0.001f, 0.0f, 1.0f, "%.3f", sliderFlags);
        ImGui::DragFloat("Translucent Transmission Lobe Probability Zero Threshold", &RtxOptions::Get()->translucentTransmissionLobeSamplingProbabilityZeroThresholdObject(), 0.001f, 0.0f, 1.0f, "%.3f", sliderFlags);
        ImGui::DragFloat("Min Translucent Transmission Lobe Probability", &RtxOptions::Get()->minTranslucentTransmissionLobeSamplingProbabilityObject(), 0.001f, 0.0f, 1.0f, "%.3f", sliderFlags);
        ImGui::DragFloat("Indirect Ray Spread Angle Factor", &RtxOptions::Get()->indirectRaySpreadAngleFactorObject(), 0.001f, 0.0f, 1.0f, "%.3f", sliderFlags);

        if (RtxOptions::Get()->enableRussianRoulette() && ImGui::CollapsingHeader("Russian Roulette", collapsingHeaderClosedFlags)) {
          ImGui::Indent();

          ImGui::DragFloat("1st bounce: Min Continue Probability", &RtxOptions::Get()->russianRoulette1stBounceMinContinueProbabilityObject(), 0.01f, 0.0f, 1.0f, "%.3f", sliderFlags);
          ImGui::DragFloat("1st bounce: Max Continue Probability", &RtxOptions::Get()->russianRoulette1stBounceMaxContinueProbabilityObject(), 0.01f, 0.0f, 1.0f, "%.3f", sliderFlags);
          
          secondPlusBounceRussianRouletteModeCombo.getKey(&RtxOptions::Get()->russianRouletteModeObject());
          if (RtxOptions::Get()->russianRouletteMode() == RussianRouletteMode::ThroughputBased)
          {
            ImGui::DragFloat("2nd+ bounce: Max Continue Probability", &RtxOptions::Get()->russianRouletteMaxContinueProbabilityObject(), 0.01f, 0.0f, 1.0f, "%.3f", sliderFlags);
          }
          else
          {
            ImGui::DragFloat("2nd+ bounce: Diffuse Continue Probability", &RtxOptions::Get()->russianRouletteDiffuseContinueProbabilityObject(), 0.01f, 0.0f, 1.0f, "%.3f", sliderFlags);
            ImGui::DragFloat("2nd+ bounce: Specular Continue Probability", &RtxOptions::Get()->russianRouletteSpecularContinueProbabilityObject(), 0.01f, 0.0f, 1.0f, "%.3f", sliderFlags);
            ImGui::DragFloat("2nd+ bounce: Distance Factor", &RtxOptions::Get()->russianRouletteDistanceFactorObject(), 0.01f, 0.0f, 1.0f, "%.3f", sliderFlags);
          }
          
          ImGui::Unindent();
        }
        ImGui::Unindent();
      }

      if (RtxOptions::Get()->getIsOpacityMicromapSupported() && 
          ImGui::CollapsingHeader("Opacity Micromap", collapsingHeaderClosedFlags)) {
        ImGui::Indent();

        ImGui::Checkbox("Enable Opacity Micromap", &RtxOptions::Get()->opacityMicromap.enableObject());
        
        if (common->getOpacityMicromapManager())
          common->getOpacityMicromapManager()->showImguiSettings();

        ImGui::Unindent();
      }

      const VkPhysicalDeviceProperties& props = m_device->adapter()->deviceProperties();
      const NV_GPU_ARCHITECTURE_ID archId = RtxOptions::Get()->getNvidiaArch();

      // Shader Execution Reordering
      if (RtxOptions::Get()->isShaderExecutionReorderingSupported()) {
        if (ImGui::CollapsingHeader("Shader Execution Reordering", collapsingHeaderClosedFlags)) {
          ImGui::Indent();

          if (RtxOptions::Get()->renderPassIntegrateIndirectRaytraceMode() == DxvkPathtracerIntegrateIndirect::RaytraceMode::TraceRay)
            ImGui::Checkbox("Enable In Integrate Indirect Pass", &RtxOptions::Get()->enableShaderExecutionReorderingInPathtracerIntegrateIndirectObject());

          ImGui::Unindent();
        }
      }
      ImGui::Unindent();
    }

    if (ImGui::CollapsingHeader("Lighting", collapsingHeaderClosedFlags)) {
      ImGui::Indent();

      common->getSceneManager().getLightManager().showImguiLightOverview();

      if (ImGui::CollapsingHeader("Effect Light", collapsingHeaderClosedFlags)) {
        ImGui::Indent();

        ImGui::TextWrapped("These settings control the effect lights, which are created by Remix, and attached to objects tagged using the rtx.lightConverter option (found in the texture tagging menu as 'Add Light to Texture').");

        ImGui::DragFloat("Light Intensity", &RtxOptions::effectLightIntensityObject(), 0.01f, 0.0f, FLT_MAX, "%.3f", sliderFlags);
        ImGui::DragFloat("Light Radius", &RtxOptions::effectLightRadiusObject(), 0.01f, 0.01f, FLT_MAX, "%.3f", sliderFlags);
        // Plasma ball has first priority
        ImGui::Checkbox("Plasma Ball Effect", &RtxOptions::effectLightPlasmaBallObject());
        ImGui::BeginDisabled(RtxOptions::effectLightPlasmaBall());
        ImGui::ColorPicker3("Light Color", &(RtxOptions::effectLightColorRef().x));
        ImGui::EndDisabled();
        ImGui::Unindent();
      }

      ImGui::DragFloat("Emissive Intensity", &RtxOptions::Get()->emissiveIntensityObject(), 0.01f, 0.0f, FLT_MAX, "%.3f", sliderFlags);
      ImGui::Separator();
      ImGui::SliderInt("RIS Light Sample Count", &RtxOptions::Get()->risLightSampleCountObject(), 0, 64);
      ImGui::Separator();
      ImGui::Checkbox("Direct Lighting Enabled", &RtxOptions::Get()->enableDirectLightingObject());
      ImGui::Checkbox("Indirect Lighting Enabled", &RtxOptions::Get()->enableSecondaryBouncesObject());

      if (ImGui::CollapsingHeader("RTXDI", collapsingHeaderClosedFlags)) {
        ImGui::Indent();

        ImGui::Checkbox("Enable RTXDI", &RtxOptions::Get()->useRTXDIObject());

        auto& rtxdi = common->metaRtxdiRayQuery();
        rtxdi.showImguiSettings();
        ImGui::Unindent();
      }

      // Indirect Illumination Integration Mode
      if (ImGui::CollapsingHeader("Indirect Illumination", collapsingHeaderClosedFlags)) {
        ImGui::Indent();
        integrateIndirectModeCombo.getKey(&RtxOptions::integrateIndirectModeObject());

        if (RtxOptions::Get()->integrateIndirectMode() == IntegrateIndirectMode::ReSTIRGI) {
          if (ImGui::CollapsingHeader("ReSTIR GI", collapsingHeaderClosedFlags)) {
            ImGui::Indent();
            ImGui::PushID("ReSTIR GI");
            auto& restirGI = common->metaReSTIRGIRayQuery();
            restirGI.showImguiSettings();
            ImGui::PopID();
            ImGui::Unindent();
          }
        } else if (RtxOptions::Get()->integrateIndirectMode() == IntegrateIndirectMode::NeuralRadianceCache) {
          if (ImGui::CollapsingHeader("Neural Radiance Cache", collapsingHeaderClosedFlags)) {

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

      if (ImGui::CollapsingHeader("NEE Cache", collapsingHeaderClosedFlags)) {
        ImGui::Indent();
        ImGui::PushID("NEE Cache");
        auto& neeCache = common->metaNeeCache();
        neeCache.showImguiSettings();
        ImGui::PopID();
        ImGui::Unindent();
      }

      ImGui::Unindent();
    }

    if (ImGui::CollapsingHeader("Global Volumetrics", collapsingHeaderClosedFlags)) {
      ImGui::Indent();

      common->metaGlobalVolumetrics().showImguiSettings();

      common->metaDustParticles().showImguiSettings();

      ImGui::Unindent();
    }

    if (ImGui::CollapsingHeader("Subsurface Scattering", collapsingHeaderClosedFlags)) {
      ImGui::Indent();

      ImGui::Checkbox("Enable Thin Opaque", &RtxOptions::SubsurfaceScattering::enableThinOpaqueObject());
      ImGui::Checkbox("Enable Texture Maps", &RtxOptions::SubsurfaceScattering::enableTextureMapsObject());

      ImGui::Checkbox("Enable Diffusion Profile SSS", &RtxOptions::SubsurfaceScattering::enableDiffusionProfileObject());

      if (RtxOptions::SubsurfaceScattering::enableDiffusionProfile()) {
        ImGui::SliderFloat("SSS Scale", &RtxOptions::SubsurfaceScattering::diffusionProfileScaleObject(), 0.0f, 100.0f);

        ImGui::Checkbox("Enable SSS Transmission", &RtxOptions::SubsurfaceScattering::enableTransmissionObject());
        if (RtxOptions::SubsurfaceScattering::enableTransmission()) {
          ImGui::Checkbox("Enable SSS Transmission Single Scattering", &RtxOptions::SubsurfaceScattering::enableTransmissionSingleScatteringObject());
          ImGui::Checkbox("Enable Transmission Diffusion Profile Correction [Experimental]", &RtxOptions::SubsurfaceScattering::enableTransmissionDiffusionProfileCorrectionObject());
          ImGui::DragInt("SSS Transmission BSDF Sample Count", &RtxOptions::SubsurfaceScattering::transmissionBsdfSampleCountObject(), 0.1f, 1, 64, "%d", ImGuiSliderFlags_AlwaysClamp);
          ImGui::DragInt("SSS Transmission Single Scattering Sample Count", &RtxOptions::SubsurfaceScattering::transmissionSingleScatteringSampleCountObject(), 0.1f, 1, 64, "%d", ImGuiSliderFlags_AlwaysClamp);
        }
      }

      ImGui::DragInt2("Diffusion Profile Sampling Debugging Pixel Position", &RtxOptions::SubsurfaceScattering::diffusionProfileDebugPixelPositionObject(), 0.1f, 0, INT32_MAX, "%d", ImGuiSliderFlags_AlwaysClamp);

      ImGui::Unindent();
    }

    if (ImGui::CollapsingHeader("Alpha Test/Blending", collapsingHeaderClosedFlags)) {
      ImGui::Indent();

      ImGui::Checkbox("Render Alpha Blended", &RtxOptions::Get()->enableAlphaBlendObject());
      ImGui::Checkbox("Render Alpha Tested", &RtxOptions::Get()->enableAlphaTestObject());
      ImGui::Separator();

      ImGui::Checkbox("Emissive Blend Translation", &RtxOptions::enableEmissiveBlendModeTranslationObject());

      ImGui::Checkbox("Emissive Blend Override", &RtxOptions::enableEmissiveBlendEmissiveOverrideObject());
      ImGui::DragFloat("Emissive Blend Override Intensity", &RtxOptions::emissiveBlendOverrideEmissiveIntensityObject(), 0.001f, 0.0f, FLT_MAX, "%.3f", sliderFlags);

      ImGui::Separator();
      ImGui::SliderFloat("Particle Softness", &RtxOptions::Get()->particleSoftnessFactorObject(), 0.f, 0.5f);

      common->metaComposite().showStochasticAlphaBlendImguiSettings();
      ImGui::Unindent();
    }

    if (ImGui::CollapsingHeader("Denoising", collapsingHeaderClosedFlags)) {
      bool isRayReconstructionEnabled = RtxOptions::Get()->isRayReconstructionEnabled();
      bool useNRD = !isRayReconstructionEnabled || common->metaRayReconstruction().enableNRDForTraining();
      ImGui::Indent();
      ImGui::BeginDisabled(!useNRD);
      ImGui::Checkbox("Denoising Enabled", &RtxOptions::Get()->useDenoiserObject());
      ImGui::Checkbox("Reference Mode | Accumulation", &RtxOptions::Get()->useDenoiserReferenceModeObject());

      if (RtxOptions::Get()->useDenoiserReferenceMode()) {
        common->metaDebugView().showAccumulationImguiSettings("Accumulation (Aliased with Debug View's Settings)");
      }

      ImGui::EndDisabled();

      if(ImGui::CollapsingHeader("Settings", collapsingHeaderClosedFlags)) {
        ImGui::Indent();
        ImGui::Checkbox("Separate Primary Direct/Indirect Denoiser", &RtxOptions::Get()->denoiseDirectAndIndirectLightingSeparatelyObject());
        ImGui::Checkbox("Reset History On Settings Change", &RtxOptions::Get()->resetDenoiserHistoryOnSettingsChangeObject());
        ImGui::Checkbox("Replace Direct Specular HitT with Indirect Specular HitT", &RtxOptions::Get()->replaceDirectSpecularHitTWithIndirectSpecularHitTObject());
        ImGui::Checkbox("Use Virtual Shading Normals", &RtxOptions::Get()->useVirtualShadingNormalsForDenoisingObject());
        ImGui::Checkbox("Adaptive Resolution Denoising", &RtxOptions::Get()->adaptiveResolutionDenoisingObject());
        ImGui::Checkbox("Adaptive Accumulation", &RtxOptions::Get()->adaptiveAccumulationObject());
        common->metaDemodulate().showImguiSettings();
        common->metaComposite().showDenoiseImguiSettings();
        ImGui::Unindent();
      }
      bool useDoubleDenoisers = RtxOptions::Get()->isSeparatedDenoiserEnabled();
      if (isRayReconstructionEnabled && RtxOptions::showRayReconstructionUI()) {
        if (ImGui::CollapsingHeader("DLSS-RR", collapsingHeaderClosedFlags)) {
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
          if (ImGui::CollapsingHeader("Primary Direct Light Denoiser", collapsingHeaderClosedFlags)) {
            ImGui::Indent();
            ImGui::PushID("Primary Direct Light Denoiser");
            common->metaPrimaryDirectLightDenoiser().showImguiSettings();
            ImGui::PopID();
            ImGui::Unindent();
          }

          if (ImGui::CollapsingHeader("Primary Indirect Light Denoiser", collapsingHeaderClosedFlags)) {
            ImGui::Indent();
            ImGui::PushID("Primary Indirect Light Denoiser");
            common->metaPrimaryIndirectLightDenoiser().showImguiSettings();
            ImGui::PopID();
            ImGui::Unindent();
          }
        } else {
          if (ImGui::CollapsingHeader("Primary Direct/Indirect Light Denoiser", collapsingHeaderClosedFlags)) {
            ImGui::Indent();
            ImGui::PushID("Primary Direct/Indirect Light Denoiser");
            common->metaPrimaryCombinedLightDenoiser().showImguiSettings();
            ImGui::PopID();
            ImGui::Unindent();
          }
        }

        if (ImGui::CollapsingHeader("Secondary Direct/Indirect Light Denoiser", collapsingHeaderClosedFlags)) {
          ImGui::Indent();
          ImGui::PushID("Secondary Direct/Indirect Light Denoiser");
          common->metaSecondaryCombinedLightDenoiser().showImguiSettings();
          ImGui::PopID();
          ImGui::Unindent();
        }
      }

      ImGui::Unindent();
    }

    if (ImGui::CollapsingHeader("Post-Processing", collapsingHeaderClosedFlags)) {
      ImGui::Indent();

      if (ImGui::CollapsingHeader("Composition", collapsingHeaderClosedFlags))
        common->metaComposite().showImguiSettings();

      if (RtxOptions::Get()->upscalerType() == UpscalerType::TAAU) {
        if (ImGui::CollapsingHeader("TAA-U", collapsingHeaderClosedFlags))
          common->metaTAA().showImguiSettings();
      }

      if (ImGui::CollapsingHeader("Bloom", collapsingHeaderClosedFlags))
        common->metaBloom().showImguiSettings();

      if (ImGui::CollapsingHeader("Auto Exposure", collapsingHeaderClosedFlags))
        common->metaAutoExposure().showImguiSettings();

      if (ImGui::CollapsingHeader("Tonemapping", collapsingHeaderClosedFlags))
      {
        ImGui::SliderInt("User Brightness", &RtxOptions::userBrightnessObject(), 0, 100, "%d");
        ImGui::DragFloat("User Brightness EV Range", &RtxOptions::userBrightnessEVRangeObject(), 0.5f, 0.f, 10.f, "%.1f");
        ImGui::Separator();
        ImGui::Combo("Tonemapping Mode", &RtxOptions::Get()->tonemappingModeObject(), "Global\0Local\0");
        if (RtxOptions::Get()->tonemappingMode() == TonemappingMode::Global) {
          common->metaToneMapping().showImguiSettings();
        } else {
          common->metaLocalToneMapping().showImguiSettings();
        }
        if (RtxOptions::showLegacyACESOption()) {
          ImGui::Separator();
          ImGui::Checkbox("Use Legacy ACES", &RtxOptions::useLegacyACESObject());
          if (!RtxOptions::useLegacyACES()) {
            ImGui::Indent();
            ImGui::TextWrapped("WARNING: Non-legacy ACES is currently experimental and the implementation is a subject to change.");
            ImGui::Unindent();
          }
        }
      }

      if (ImGui::CollapsingHeader("Post FX", collapsingHeaderClosedFlags))
        common->metaPostFx().showImguiSettings();
      
      ImGui::Unindent();
    }

    if (ImGui::CollapsingHeader("Debug", collapsingHeaderClosedFlags)) {
      ImGui::Indent();
      common->metaDebugView().showImguiSettings();
      ImGui::Unindent();
    }

    if (ImGui::CollapsingHeader("Geometry", collapsingHeaderClosedFlags)) {
      ImGui::Indent();

      ImGui::Checkbox("Enable Triangle Culling (Globally)", &RtxOptions::Get()->enableCullingObject());
      ImGui::Checkbox("Enable Triangle Culling (Override Secondary Rays)", &RtxOptions::Get()->enableCullingInSecondaryRaysObject());
      ImGui::Separator();
      ImGui::DragInt("Min Prims in Dynamic BLAS", &RtxOptions::minPrimsInDynamicBLASObject(), 1.f, 100, 0);
      ImGui::DragInt("Max Prims in Merged BLAS", &RtxOptions::maxPrimsInMergedBLASObject(), 1.f, 100, 0);
      ImGui::Checkbox("Force Merge All Meshes", &RtxOptions::forceMergeAllMeshesObject());
      ImGui::Checkbox("Minimize BLAS Merging", &RtxOptions::minimizeBlasMergingObject());
      ImGui::Separator();
      ImGui::Checkbox("Portals: Virtual Instance Matching", &RtxOptions::Get()->useRayPortalVirtualInstanceMatchingObject());
      ImGui::Checkbox("Portals: Fade In Effect", &RtxOptions::Get()->enablePortalFadeInEffectObject());
      ImGui::Unindent();
    }

    if (ImGui::CollapsingHeader("Texture Streaming [Experimental]", collapsingHeaderClosedFlags)) {
      ImGui::Indent();
      ImGui::BeginDisabled(!RtxOptions::TextureManager::samplerFeedbackEnable());
      {
        if (RtxOptions::TextureManager::fixedBudgetEnable() && RtxOptions::TextureManager::samplerFeedbackEnable()) {
          static float s_gigbytes_helper = 0;
          if (dragFloatMB_showGB("Texture Budget##1",
                                 &RtxOptions::TextureManager::fixedBudgetMiBObject().getValue(),
                                 &s_gigbytes_helper,
                                 1.f,
                                 32.f,
                                 RtxOptions::TextureManager::fixedBudgetMiBObject().getDescription())) {
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
          ImGui::DragFloat("Texture Cache##2", &s_dummy, 0.5f, 1.f, 32.f, formatstr, ImGuiSliderFlags_NoRoundToFormat);
          ImGui::EndDisabled();
        }
      }
      {
        ImGui::BeginDisabled(RtxOptions::TextureManager::fixedBudgetEnable());
        if (ImGui::DragInt("of VRAM is dedicated to Textures",
                            &RtxOptions::TextureManager::budgetPercentageOfAvailableVramObject(),
                            10.F,
                            10,
                            100,
                            "%d%%")) {
          ctx->getCommonObjects()->getSceneManager().requestVramCompaction();
        }
        ImGui::EndDisabled();
      }
      if (ImGui::Checkbox("Force Fixed Texture Budget", &RtxOptions::TextureManager::fixedBudgetEnableObject())) {
        // budgeting technique changed => ask DXVK to return unused VRAM chunks to OS to better represent consumption
        ctx->getCommonObjects()->getSceneManager().requestVramCompaction();
      }
      ImGui::EndDisabled();

      ImGui::Dummy({ 0, 2 });
      if (ImGui::CollapsingHeader("Advanced##texstream", collapsingHeaderClosedFlags)) {
        ImGui::Indent();
        ImGui::Text("Streamed Texture VRAM usage: %.1f GB", float(g_streamedTextures_usedBytes) / 1024.F / 1024.F / 1024.F);
        ImGui::Dummy({ 0, 2 });
        ImGui::Separator();
        ImGui::Dummy({ 0, 2 });
        ImGui::TextUnformatted("Warning: toggling this option will enforce a full texture reload.");
        if (ImGui::Checkbox("Sampler Feedback", &RtxOptions::TextureManager::samplerFeedbackEnableObject())) {
          // sampler feedback ON/OFF changed => free all to refit textures in VRAM
          ctx->getCommonObjects()->getSceneManager().requestTextureVramFree();
        }
        ImGui::Dummy({ 0, 2 });
        ImGui::Separator();
        ImGui::Dummy({ 0, 2 });
        if (ImGui::Button("Demote All Textures")) {
          ctx->getCommonObjects()->getSceneManager().requestTextureVramFree();
        }
        ImGui::Checkbox("Reload Textures on Window Resize", &RtxOptions::Get()->reloadTextureWhenResolutionChangedObject());
        ImGui::Unindent();
      }
      ImGui::Unindent();
    }

    if (ImGui::CollapsingHeader("Terrain [Experimental]")) {
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
          TerrainBaker::enableBakingRef() = false;
          RtxOptions::terrainAsDecalsEnabledIfNoBakerRef() = false;
          break;
        }
        case TerrainMode::TerrainBaker: {
          TerrainBaker::enableBakingRef() = true;
          RtxOptions::terrainAsDecalsEnabledIfNoBakerRef() = false;
          break;
        }
        case TerrainMode::AsDecals: {
          TerrainBaker::enableBakingRef() = false;
          RtxOptions::terrainAsDecalsEnabledIfNoBakerRef() = true;
          break;
        }
        default: break;
        }
      }

      ImGui::Separator();

      if (TerrainBaker::enableBaking()) {
        common->getTerrainBaker().showImguiSettings();
      } else if (RtxOptions::terrainAsDecalsEnabledIfNoBaker()) {
        ImGui::Checkbox("Over-modulate Blending", &RtxOptions::terrainAsDecalsAllowOverModulateObject());
      }

      ImGui::Unindent();
    }

    if (ImGui::CollapsingHeader("Player Model", collapsingHeaderClosedFlags)) {
      ImGui::Indent();
      ImGui::Checkbox("Primary Shadows", &RtxOptions::Get()->playerModel.enablePrimaryShadowsObject());
      ImGui::Checkbox("Show in Primary Space", &RtxOptions::Get()->playerModel.enableInPrimarySpaceObject());
      ImGui::Checkbox("Create Virtual Instances", &RtxOptions::Get()->playerModel.enableVirtualInstancesObject());
      if (ImGui::CollapsingHeader("Calibration", collapsingHeaderClosedFlags)) {
        ImGui::Indent();
        ImGui::DragFloat("Backward Offset", &RtxOptions::Get()->playerModel.backwardOffsetObject(), 0.01f, 0.f, 100.f);
        ImGui::DragFloat("Horizontal Detection Distance", &RtxOptions::Get()->playerModel.horizontalDetectionDistanceObject(), 0.01f, 0.f, 100.f);
        ImGui::DragFloat("Vertical Detection Distance", &RtxOptions::Get()->playerModel.verticalDetectionDistanceObject(), 0.01f, 0.f, 100.f);
        ImGui::Unindent();
      }
      ImGui::Unindent();
    }

    if (ImGui::CollapsingHeader("Displacement [Experimental]", collapsingHeaderClosedFlags)) {
      ImGui::Indent();
      ImGui::TextWrapped("Warning: This is currently implemented using POM with a simple height map, displacing inwards.  The implementation may change in the future, which could include changes to the texture format or displacing outwards.\nRaymarched POM will use a simple raymarch algorithm, and will show artifacts on thin features and at oblique angles.\nQuadtree POM depends on custom mipmaps with maximums instead of averages, which can be generated using `generate_max_mip.py`.");
      ImGui::Combo("Mode", &RtxOptions::Displacement::modeObject(), "Off\0Raymarched POM\0Quadtree POM\0");
      ImGui::Checkbox("Enable Direct Lighting", &RtxOptions::Displacement::enableDirectLightingObject());
      ImGui::Checkbox("Enable Indirect Lighting", &RtxOptions::Displacement::enableIndirectLightingObject());
      ImGui::Checkbox("Enable Indirect Hit", &RtxOptions::Displacement::enableIndirectHitObject());
      ImGui::Checkbox("Enable NEE Cache", &RtxOptions::Displacement::enableNEECacheObject());
      ImGui::Checkbox("Enable ReSTIR_GI", &RtxOptions::Displacement::enableReSTIRGIObject());
      ImGui::Checkbox("Enable PSR", &RtxOptions::Displacement::enablePSRObject());
      ImGui::DragFloat("Global Displacement Factor", &RtxOptions::Displacement::displacementFactorObject(), 0.01f, 0.0f, 20.0f);
      ImGui::DragInt("Max Iterations", &RtxOptions::Displacement::maxIterationsObject(), 1.f, 1, 256, "%d", sliderFlags);
      ImGui::Unindent();
    }

    if (ImGui::CollapsingHeader("Raytraced Render Target [Experimental]", collapsingHeaderClosedFlags)) {
      ImGui::Indent();
      ImGui::TextWrapped("When a screen in-game is displaying the rasterized results of another camera, this can be used to raytrace that scene.\nNote that the render target texture containing the rasterized results needs to be set to `raytracedRenderTargetTextures` in the texture selection menu.");

      ImGui::Checkbox("Enable Raytraced Render Targets", &RtxOptions::Get()->raytracedRenderTarget.enableObject());
      ImGui::Unindent();
    }

    if (ImGui::CollapsingHeader("View Distance", collapsingHeaderClosedFlags)) {
      ImGui::Indent();

      viewDistanceModeCombo.getKey(&RtxOptions::Get()->viewDistanceOptions.distanceModeObject());

      if (RtxOptions::Get()->viewDistanceOptions.distanceMode() != ViewDistanceMode::None) {
        viewDistanceFunctionCombo.getKey(&RtxOptions::Get()->viewDistanceOptions.distanceFunctionObject());

        if (RtxOptions::Get()->viewDistanceOptions.distanceMode() == ViewDistanceMode::HardCutoff) {
          ImGui::DragFloat("Distance Threshold", &RtxOptions::Get()->viewDistanceOptions.distanceThresholdObject(), 0.1f, 0.0f, 0.0f, "%.2f", sliderFlags);
        } else if (RtxOptions::Get()->viewDistanceOptions.distanceMode() == ViewDistanceMode::CoherentNoise) {
          ImGui::DragFloat("Distance Fade Min", &RtxOptions::Get()->viewDistanceOptions.distanceFadeMinObject(), 0.1f, 0.0f, RtxOptions::Get()->viewDistanceOptions.distanceFadeMax(), "%.2f", sliderFlags);
          ImGui::DragFloat("Distance Fade Max", &RtxOptions::Get()->viewDistanceOptions.distanceFadeMaxObject(), 0.1f, RtxOptions::Get()->viewDistanceOptions.distanceFadeMin(), 0.0f, "%.2f", sliderFlags);
          ImGui::DragFloat("Noise Scale", &RtxOptions::Get()->viewDistanceOptions.noiseScaleObject(), 0.1f, 0.0f, 0.0f, "%.2f", sliderFlags);

          // Note: ImGui's limits do not apply for text entry for whatever reason so we need to clamp these options manually to ensure they do not trigger asserts.
          RtxOptions::Get()->viewDistanceOptions.distanceFadeMinRef() = std::min(RtxOptions::Get()->viewDistanceOptions.distanceFadeMin(), RtxOptions::Get()->viewDistanceOptions.distanceFadeMax());
          RtxOptions::Get()->viewDistanceOptions.distanceFadeMaxRef() = std::max(RtxOptions::Get()->viewDistanceOptions.distanceFadeMin(), RtxOptions::Get()->viewDistanceOptions.distanceFadeMax());
        }
      }

      ImGui::Unindent();
    }

    if (ImGui::CollapsingHeader("Material Filtering", collapsingHeaderClosedFlags)) {
      ImGui::Indent();

      ImGui::Checkbox("Use White Material Textures", &RtxOptions::Get()->useWhiteMaterialModeObject());
      ImGui::Separator();
      const float kMipBiasRange = 32;
      ImGui::DragFloat("Mip LOD Bias", &RtxOptions::Get()->nativeMipBiasObject(), 0.01f, -kMipBiasRange, kMipBiasRange, "%.2f", sliderFlags);
      ImGui::DragFloat("Upscaling LOD Bias", &RtxOptions::Get()->upscalingMipBiasObject(), 0.01f, -kMipBiasRange, kMipBiasRange, "%.2f", sliderFlags);
      ImGui::Separator();
      ImGui::Checkbox("Use Anisotropic Filtering", &RtxOptions::Get()->useAnisotropicFilteringObject());
      if (RtxOptions::Get()->useAnisotropicFiltering()) {
        ImGui::DragFloat("Max Anisotropy Samples", &RtxOptions::Get()->maxAnisotropySamplesObject(), 0.5f, 1.0f, 16.f, "%.3f", sliderFlags);
      }
      ImGui::DragFloat("Translucent Decal Albedo Factor", &RtxOptions::Get()->translucentDecalAlbedoFactorObject(), 0.01f);
      ImGui::Unindent();
    }

    if (ImGui::CollapsingHeader("Anti-Culling", collapsingHeaderClosedFlags)) {
      ImGui::Indent();

      ImGui::Checkbox("Anti-Culling Objects", &RtxOptions::AntiCulling::Object::enableObject());
      if (RtxOptions::AntiCulling::Object::enable()) {
        ImGui::Checkbox("High precision Anti-Culling", &RtxOptions::AntiCulling::Object::enableHighPrecisionAntiCullingObject());
        if (RtxOptions::AntiCulling::Object::enableHighPrecisionAntiCulling()) {
          ImGui::Checkbox("Infinity Far Frustum", &RtxOptions::AntiCulling::Object::enableInfinityFarFrustumObject());
        }
        ImGui::Checkbox("Enable Bounding Box Hash For Duplication Check", &RtxOptions::AntiCulling::Object::hashInstanceWithBoundingBoxHashObject());
        ImGui::InputInt("Instance Max Size", &RtxOptions::AntiCulling::Object::numObjectsToKeepObject(), 1, 1, 0);
        ImGui::DragFloat("Anti-Culling Fov Scale", &RtxOptions::AntiCulling::Object::fovScaleObject(), 0.01f, 0.1f, 2.0f);
        ImGui::DragFloat("Anti-Culling Far Plane Scale", &RtxOptions::AntiCulling::Object::farPlaneScaleObject(), 0.1f, 0.1f, 10000.0f);
      }
      ImGui::Separator();
      ImGui::Checkbox("Anti-Culling Lights", &RtxOptions::AntiCulling::Light::enableObject());
      if (RtxOptions::AntiCulling::Light::enable()) {
        ImGui::InputInt("Max Number Of Lights", &RtxOptions::AntiCulling::Light::numLightsToKeepObject(), 1, 1, 0);
        ImGui::InputInt("Max Number of Frames to keep lights", &RtxOptions::AntiCulling::Light::numFramesToExtendLightLifetimeObject(), 1, 1, 0);
        ImGui::DragFloat("Anti-Culling Lights Fov Scale", &RtxOptions::AntiCulling::Light::fovScaleObject(), 0.01f, 0.1f, 2.0f);
      }

      ImGui::Unindent();
    }

    ImGui::PopItemWidth();
  }

  void ImGUI::render(
    const HWND hwnd,
    const Rc<DxvkContext>& ctx,
    VkSurfaceFormatKHR surfaceFormat,
    VkExtent2D         surfaceSize,
    bool               vsync) {
    ScopedGpuProfileZone(ctx, "ImGUI Render");

    m_lastRenderVsyncStatus = vsync;

    ImGui::SetCurrentContext(m_context);
    ImPlot::SetCurrentContext(m_plotContext);

    // Sometimes games can change windows on us, so we need to check that here and tell ImGUI
    if (m_hwnd != hwnd) {
      if(m_init) {
        ImGui_ImplWin32_Shutdown();
      }
      m_hwnd = hwnd;
      ImGui_ImplWin32_Init(hwnd);
    }

    if (!m_init) {
      //this initializes imgui for Vulkan
      ImGui_ImplVulkan_InitInfo init_info = {};
      init_info.Instance = m_device->instance()->handle();
      init_info.PhysicalDevice = m_device->adapter()->handle();
      init_info.Device = m_device->handle();
      init_info.Queue = m_device->queues().graphics.queueHandle;
      init_info.DescriptorPool = m_imguiPool;
      init_info.MinImageCount = 2; // Note: Required to be at least 2 by ImGui.
      // Note: This image count is important for allocating multiple buffers for ImGui to support multiple frames
      // in flight without causing corruptions or crashes. This should match ideally what is set in DXVK (via something
      // like GetActualFrameLatency, but this can change at runtime. Instead we simply use the maximum number of frames
      // in flight supported by the Remix side of DXVK as this should be enough (as DXVK is also clamped to this amount
      // currently).
      init_info.ImageCount = kMaxFramesInFlight;
      init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

      ImGui_ImplVulkan_Init(&init_info, ctx->getFramebufferInfo().renderPass()->getDefaultHandle());

      //execute a gpu command to upload imgui font textures
      createFontsTexture(ctx);

      m_init = true;
    }

    update(ctx);

    this->setupRendererState(ctx, surfaceFormat, surfaceSize);

    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), ctx->getCmdBuffer(DxvkCmdBuffer::ExecBuffer));

    this->resetRendererState(ctx);
  }

  void ImGUI::setupRendererState(
    const Rc<DxvkContext>&  ctx,
          VkSurfaceFormatKHR surfaceFormat,
          VkExtent2D        surfaceSize) {
    bool isSrgb = imageFormatInfo(surfaceFormat.format)->flags.test(DxvkFormatFlag::ColorSpaceSrgb);

    VkViewport viewport;
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = float(surfaceSize.width);
    viewport.height = float(surfaceSize.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor;
    scissor.offset = { 0, 0 };
    scissor.extent = surfaceSize;

    ctx->setViewports(1, &viewport, &scissor);
    ctx->setRasterizerState(m_rsState);
    ctx->setBlendMode(0, m_blendMode);

    ctx->setSpecConstant(VK_PIPELINE_BIND_POINT_GRAPHICS, 0, isSrgb);
  }

  void ImGUI::resetRendererState(const Rc<DxvkContext>& ctx) {
    ctx->setSpecConstant(VK_PIPELINE_BIND_POINT_GRAPHICS, 0, 0);
  }

  void ImGUI::createFontsTexture(const Rc<DxvkContext>& ctx) {
    ImGuiIO& io = ImGui::GetIO();
    ImGui_ImplVulkan_Data* bd = (ImGui_ImplVulkan_Data*)io.BackendRendererUserData;
    ImGui_ImplVulkan_InitInfo* v = &bd->VulkanInitInfo;
    
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

    // Normal Size Font (Default)

    ImFontConfig normalFontCfg = ImFontConfig();
    normalFontCfg.SizePixels = 16.f;
    normalFontCfg.FontDataOwnedByAtlas = false;

    const size_t nvidiaSansLength = sizeof(___NVIDIASansMd) / sizeof(___NVIDIASansMd[0]);
    const size_t robotoMonoLength = sizeof(___RobotoMonoRg) / sizeof(___RobotoMonoRg[0]);

    {
      // Add letters/symbols (NVIDIA-Sans)
      io.FontDefault = io.Fonts->AddFontFromMemoryTTF(&___NVIDIASansMd[0], nvidiaSansLength, 0, &normalFontCfg, characterRange.Data);

      // Enable merging
      normalFontCfg.MergeMode = true;

      // Add numbers (Roboto-Mono)
      io.Fonts->AddFontFromMemoryTTF(&___RobotoMonoRg[0], robotoMonoLength, 0, &normalFontCfg, numericalRange.Data);
    }

    // Large Size Font

    ImFontConfig largeFontCfg = ImFontConfig();
    largeFontCfg.SizePixels = 24.f;
    largeFontCfg.FontDataOwnedByAtlas = false;

    {
      // Add letters/symbols (NVIDIA-Sans)
      m_largeFont = io.Fonts->AddFontFromMemoryTTF(&___NVIDIASansMd[0], nvidiaSansLength, 0, &largeFontCfg, characterRange.Data);

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

    // Update the Descriptor Set:
    bd->FontDescriptorSet = (VkDescriptorSet) ImGui_ImplVulkan_AddTexture(bd->FontSampler, bd->FontView, VK_IMAGE_LAYOUT_GENERAL);

    // Store our identifier
    io.Fonts->SetTexID((ImTextureID)bd->FontDescriptorSet);
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
  }

  void ImGUI::onOpenMenus() {
    // Before opening the menus, try free some memory, the idea being the 
    //  user may want to make some changes to various settings and so they
    //  should have all available memory to do so.
    freeUnusedMemory();
  }

  void ImGUI::freeUnusedMemory() {
    if (!m_device) {
      return;
    }

    m_device->getCommon()->getSceneManager().requestVramCompaction();
  }

}
