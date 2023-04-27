/*
* Copyright (c) 2021-2023, NVIDIA CORPORATION. All rights reserved.
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
#include <nvapi.h>
#include <NVIDIASansMd.ttf.h>
#include <RobotoMonoRg.ttf.h>

#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_impl_vulkan.h"
#include "imgui_impl_win32.h"
#include "dxvk_imgui.h"
#include "rtx_render/rtx_imgui.h"
#include "dxvk_device.h"
#include "rtx_render/rtx_shader_manager.h"
#include "rtx_render/rtx_camera.h"
#include "rtx_render/rtx_context.h"
#include "rtx_render/rtx_options.h"
#include "dxvk_image.h"
#include "../util/rc/util_rc_ptr.h"
#include "../util/util_math.h"
#include "rtx_render/rtx_opacity_micromap_manager.h"
#include "rtx_render/rtx_bridgemessagechannel.h"
#include "dxvk_imgui_about.h"
#include "dxvk_imgui_splash.h"
#include "dxvk_scoped_annotation.h"

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
  void TextSeparator(char* text, float pre_width = 10.0f) {
    ImGui::PreSeparator(pre_width);
    ImGui::Text(text);
    ImGui::SameLineSeparator();
  }
}

namespace dxvk {
  struct ImGuiTexture {
    Rc<DxvkImageView> imageView = VK_NULL_HANDLE;
    VkDescriptorSet texID = VK_NULL_HANDLE;
  };
  std::unordered_map<XXH64_hash_t, ImGuiTexture> g_imguiTextureMap;

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

  ImGui::ComboWithKey<bool> denoiserQualityCombo {
    "Denoising Quality",
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

  ImGui::ComboWithKey<UpscalerType> upscalerCombo {
    "Upscaler Type",
    ImGui::ComboWithKey<UpscalerType>::ComboEntries { {
        {UpscalerType::None, "None"},
        {UpscalerType::DLSS, "DLSS"},
        {UpscalerType::NIS, "NIS"},
        {UpscalerType::TAAU, "TAA-U"}
    } }
  };

  ImGui::ComboWithKey<UpscalerType> upscalerDlssUnsupportCombo {
    "Upscaler Type",
    ImGui::ComboWithKey<UpscalerType>::ComboEntries { {
        {UpscalerType::None, "None"},
        {UpscalerType::NIS, "NIS"},
        {UpscalerType::TAAU, "TAA-U"},
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

  // Styles 
  constexpr ImGuiSliderFlags sliderFlags = ImGuiSliderFlags_AlwaysClamp;
  constexpr ImGuiTreeNodeFlags collapsingHeaderClosedFlags = ImGuiTreeNodeFlags_CollapsingHeader;
  constexpr ImGuiTreeNodeFlags collapsingHeaderFlags = collapsingHeaderClosedFlags | ImGuiTreeNodeFlags_DefaultOpen;
  constexpr ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus;

  ImGUI::ImGUI(const Rc<DxvkDevice>& device, const HWND& hwnd)
  : m_device (device)
  , m_hwnd   (hwnd)
  , m_about  (new ImGuiAbout)
  , m_splash  (new ImGuiSplash) {
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
    pool_info.maxSets = 1000;
    pool_info.poolSizeCount = std::size(pool_sizes);
    pool_info.pPoolSizes = pool_sizes;

    m_device->vkd()->vkCreateDescriptorPool(m_device->handle(), &pool_info, nullptr, &m_imguiPool);

    //this initializes the core structures of imgui
    ImGui::CreateContext();

    //this initializes imgui for SDL
    ImGui_ImplWin32_Init(hwnd);

    // Setup custom style
    setupStyle();

    // Enable keyboard nav
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  }

  ImGUI::~ImGUI() {
    ImGui_ImplWin32_Shutdown();

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
  }
  
  void ImGUI::AddTexture(const XXH64_hash_t hash, const Rc<DxvkImageView>& imageView) {
    if (g_imguiTextureMap.find(hash) == g_imguiTextureMap.end()) {
      ImGuiTexture texture;
      texture.imageView = imageView; // Hold a refcount
      texture.texID = VK_NULL_HANDLE;
      g_imguiTextureMap[hash] = texture;
    }
  }

  void ImGUI::ReleaseTexture(const XXH64_hash_t hash) {
    if (RtxOptions::Get()->keepTexturesForTagging()) {
      return;
    }
    
    if (g_imguiTextureMap.find(hash) != g_imguiTextureMap.end())
      g_imguiTextureMap.erase(hash);
  }

  void ImGUI::wndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam);
  }

  void ImGUI::showMemoryStats() const {
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

    // Display a warning if free video memory is below a threshold

    const bool lowVideoMemory = freeVidMemRatio < 0.125f;

    if (lowVideoMemory) {
      // Note: Use caution when editing this text, it must fit on one line to avoid flickering (due to reserving 1 line of space for it).
      ImGui::TextColored(ImVec4{ 0.87f, 0.75f, 0.20f, 1.0f }, "Free video memory low! Consider lowering resolution/quality settings.");
    } else {
      // Note: Pad with a blank line when no warning is present to avoid menu flicking (since memory can bounce up and down on the threshold
      // in a distracting manner).
      ImGui::Text("");
    }

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
    RtxOptions::Get()->showUIRef() = type;

    if (RtxOptions::Get()->showUICursor()) {
      ImGui::GetIO().MouseDrawCursor = type != UIType::None;
    }

    if (RtxOptions::Get()->blockInputToGameInUI()) {
      BridgeMessageChannel::get().send("UWM_REMIX_UIACTIVE_MSG",
                                       type != UIType::None ? 1 : 0, 0);
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

    if (RtxOptions::Get()->showUI() == UIType::Advanced) {
      showMainMenu(ctx);

      // Uncomment to see the ImGUI demo, good reference!  Also, need to undefine IMGUI_DISABLE_DEMO_WINDOWS (in "imgui_demo.cpp")
      // ImGui::ShowDemoWindow();
    }

    if (RtxOptions::Get()->showUI() == UIType::Basic) {
      showUserMenu(ctx);
    }

    showErrorStatus(ctx);

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
    static RtxQuickAction sQuickAction = common->getSceneManager().areReplacementsLoaded() ? RtxQuickAction::kRtxOnEnhanced : RtxQuickAction::kRtxOn;

    if (ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_KeypadAdd))) {
      sQuickAction = (RtxQuickAction) ((sQuickAction + 1) % RtxQuickAction::kCount);

      // Skip over the enhancements quick option if no replacements are loaded
      if(!common->getSceneManager().areReplacementsLoaded() && sQuickAction == RtxQuickAction::kRtxOnEnhanced)
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
      enum Tabs {
        kRendering = 0,
        kSetup,
        kEnhancements,
        kAbout,
#ifdef REMIX_DEVELOPMENT
        kDevelopment,
#endif
        kCount
      };
      const char* names[] = { "Rendering", "Game Setup", "Enhancements", "About" , "Dev Settings"};

      if (ImGui::BeginTabBar("Developer Tabs", tab_bar_flags)) {
        for (int n = 0; n < Tabs::kCount; n++)
          if (ImGui::BeginTabItem(names[n], nullptr, tab_item_flags)) {
            switch ((Tabs) n) {
            case Tabs::kRendering:
              showRenderingSettings(ctx);
              break;
            case Tabs::kSetup:
              showSetupWindow(ctx);
              break;
            case Tabs::kEnhancements:
              showEnhancementsWindow(ctx);
              break;
            case Tabs::kAbout:
              m_about->show(ctx);
              break;
#ifdef REMIX_DEVELOPMENT
            case Tabs::kDevelopment:
              showAppConfig();
              break;
#endif
            }
            ImGui::EndTabItem();
          }

        if (ImGui::TabItemButton(m_windowOnRight ? "<<" : ">>"))
          m_windowOnRight = !m_windowOnRight;

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
    static int textureMipMapSetting = RtxOptions::Get()->skipReplacementTextureMipMapLevel();
    int currentFrameID = ctx->getDevice()->getCurrentFrameId();
    if (currentFrameID != lastFrameID + 1) {
      textureMipMapSetting = RtxOptions::Get()->skipReplacementTextureMipMapLevel();
    }

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
        if (textureMipMapSetting != RtxOptions::Get()->skipReplacementTextureMipMapLevel()) {
          ImGui::OpenPopup("Message");
          textureMipMapSetting = RtxOptions::Get()->skipReplacementTextureMipMapLevel();
        }
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

      if (ImGui::BeginPopupModal("Message", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("The texture quality setting will take effect next time you start the app.");
        ImGui::Indent(150);
        if (ImGui::Button("OK", ImVec2(120, 0))) {
          ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
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

    const bool dlssSupported = dlss.supportsDLSS();

    // Describe the tab

    const char* tabDescriptionText = "General performance settings. Enabling upscaling is recommended to significantly increase performance.";

    // Note: Specifically reference the DLSS preset when present.
    if (dlssSupported) {
      tabDescriptionText = "General performance settings. Enabling the DLSS 2.0 preset is recommended to significantly increase performance.";
    }

    ImGui::TextWrapped(tabDescriptionText);

    ImGui::Dummy(ImVec2(0.0f, 5.0f));

    // Preset Settings

    if (dlssSupported) {
      const char* dlssPresetText = "DLSS 2.0 Preset";
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
      if (dlss.supportsDLSS()) {
        m_userGraphicsSettingChanged |= upscalerCombo.getKey(&RtxOptions::Get()->upscalerTypeObject());
      } else {
        m_userGraphicsSettingChanged |= upscalerDlssUnsupportCombo.getKey(&RtxOptions::Get()->upscalerTypeObject());
      }

      // Upscaler Preset

      ImGui::PushItemWidth(static_cast<float>(subItemWidth));
      ImGui::Indent(static_cast<float>(subItemIndent));

      switch (RtxOptions::Get()->upscalerType()) {
        case UpscalerType::DLSS: {
          m_userGraphicsSettingChanged |= ImGui::Combo("DLSS Mode", &RtxOptions::Get()->qualityDLSSObject(), "Ultra Perf\0Performance\0Balanced\0Quality\0Auto\0");

          // Display DLSS Upscaling Information

          const auto currentDLSSProfile = dlss.getCurrentProfile();
          uint32_t dlssInputWidth, dlssInputHeight;

          dlss.getInputSize(dlssInputWidth, dlssInputHeight);

          ImGui::TextWrapped(str::format("Computed DLSS Mode: ", dlssProfileToString(currentDLSSProfile), ", Render Resolution: ", dlssInputWidth, "x", dlssInputHeight).c_str());

          break;
        }
        case UpscalerType::NIS: {
          m_userGraphicsSettingChanged |= ImGui::Combo("NIS Preset", &RtxOptions::Get()->nisPresetObject(), "Performance\0Balanced\0Quality\0Fullscreen\0");
          RtxOptions::Get()->updateUpscalerFromNisPreset();

          // Display NIS Upscaling Information

          auto resolutionScale = RtxOptions::Get()->getResolutionScale();

          ImGui::TextWrapped(str::format("NIS Resolution Scale: ", resolutionScale).c_str());

          break;
        }
        case UpscalerType::TAAU: {
          m_userGraphicsSettingChanged |= ImGui::Combo("TAA-U Preset", &RtxOptions::Get()->taauPresetObject(), "Performance\0Balanced\0Quality\0Fullscreen\0");
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

    ImGui::TextSeparator("Latency Reduction Settings");

    {
      ImGui::BeginDisabled(disableNonPresetSettings);

      showReflexOptions();

      ImGui::EndDisabled();
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
      indirectLightParticlesLevel = RtxOptions::Get()->enableEmissiveParticlesInIndirectRays() ? 2 : 1;
    }

    // Map presets to options

    RtxOptions::Get()->updateGraphicsPresets();

    // Note: These settings aren't updated in updateGraphicsPresets since they are not in the RtxOptions class.
    if (RtxOptions::Get()->graphicsPreset() == GraphicsPreset::Ultra ||
        RtxOptions::Get()->graphicsPreset() == GraphicsPreset::High) {
      rtxdiRayQuery.enableRayTracedBiasCorrectionRef() = true;
      restirGiRayQuery.biasCorrectionModeRef() = ReSTIRGIBiasCorrection::PairwiseRaytrace;
      restirGiRayQuery.useReflectionReprojectionRef() = true;
      common->metaComposite().enableStochasticAlphaBlendRef() = true;
    } else if (RtxOptions::Get()->graphicsPreset() == GraphicsPreset::Medium ||
               RtxOptions::Get()->graphicsPreset() == GraphicsPreset::Low) {
      rtxdiRayQuery.enableRayTracedBiasCorrectionRef() = false;
      restirGiRayQuery.biasCorrectionModeRef() = ReSTIRGIBiasCorrection::BRDF;
      postFx.enableRef() = false;
      restirGiRayQuery.useReflectionReprojectionRef() = false;
      common->metaComposite().enableStochasticAlphaBlendRef() = false;
    }

    // Path Tracing Settings

    ImGui::TextSeparator("Path Tracing Settings");

    {
      // Note: Disabled flags should match preset mapping above to prevent changing settings when a preset overrides them.
      ImGui::BeginDisabled(RtxOptions::Get()->graphicsPreset() != GraphicsPreset::Custom);

      m_userGraphicsSettingChanged |= minPathBouncesCombo.getKey(&RtxOptions::Get()->pathMinBouncesObject());
      m_userGraphicsSettingChanged |= maxPathBouncesCombo.getKey(&RtxOptions::Get()->pathMaxBouncesObject());
      m_userGraphicsSettingChanged |= ImGui::Checkbox("Enable Volumetric Lighting", &RtxOptions::Get()->enableVolumetricLightingObject());
      m_userGraphicsSettingChanged |= denoiserQualityCombo.getKey(&RtxOptions::Get()->denoiseDirectAndIndirectLightingSeparatelyObject());
      m_userGraphicsSettingChanged |= textureQualityCombo.getKey(&RtxOptions::Get()->skipReplacementTextureMipMapLevelObject());
      m_userGraphicsSettingChanged |= indirectLightingParticlesCombo.getKey(&indirectLightParticlesLevel);
      ImGui::SetTooltipToLastWidgetOnHover("Controls the quality of particles in indirect (reflection/GI) rays.");

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

    // Map indirect particle level back to settings
    if (RtxOptions::Get()->graphicsPreset() == GraphicsPreset::Custom) {
      switch (indirectLightParticlesLevel) {
      case 0:
        RtxOptions::Get()->enableEmissiveParticlesInIndirectRaysRef() = false;
        RtxOptions::Get()->enableUnorderedResolveInIndirectRaysRef() = false;
        break;
      case 1:
        RtxOptions::Get()->enableEmissiveParticlesInIndirectRaysRef() = false;
        RtxOptions::Get()->enableUnorderedResolveInIndirectRaysRef() = true;
        break;
      case 2:
        RtxOptions::Get()->enableEmissiveParticlesInIndirectRaysRef() = true;
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

    ImGui::BeginDisabled(!common->getSceneManager().areReplacementsLoaded());

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

  void ImGUI::showErrorStatus(const Rc<DxvkContext>& ctx) {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    auto common = ctx->getCommonObjects();
    std::vector<std::string> hudMessages;

    if (common->getSceneManager().areReplacementsLoaded()) {
      const std::string& exportStatus = common->getSceneManager().getReplacementStatus();
      if (exportStatus != "Release Ready") {
        hudMessages.emplace_back(str::format("Warning: Replacements are not production ready. Status: ", exportStatus));
      }
    }

    if(common->getSceneManager().areReplacementsLoading())
      hudMessages.emplace_back("Loading enhancements...");

    if (!hudMessages.empty()) {
      ImGui::SetNextWindowPos(ImVec2(0, viewport->Size.y), ImGuiCond_Always, ImVec2(0.0f, 1.0f));
      ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.5f, 0.2f, 0.2f, 0.35f));

      ImGuiWindowFlags hud_flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove;
      if (ImGui::Begin("HUD", nullptr, hud_flags)) {
 
        for (auto&& message : hudMessages) {
          ImGui::Text(message.c_str());
        }
      }
      ImGui::PopStyleColor();
      ImGui::End();
    }
  }

  void ImGUI::showAppConfig() {
    ImGui::PushItemWidth(250);
    if (ImGui::Button("Take Screenshot")) {
      RtxContext::triggerScreenshot();
    }

    ImGui::SetTooltipToLastWidgetOnHover("Screenshot will be dumped to, '<exe-dir>/Screenshots'");

    ImGui::SameLine(200.f);
    ImGui::Checkbox("Include G-Buffer", &RtxOptions::Get()->captureDebugImageObject());
        
    { // Recompile Shaders button and its status message
      using namespace std::chrono;
      static enum { None, OK, Error } shaderMessage = None;
      static time_point<steady_clock> shaderMessageTimeout;

      if (ImGui::Button("Recompile Shaders")) {
        if (ShaderManager::getInstance()->reloadShaders())
          shaderMessage = OK;
        else
          shaderMessage = Error;

        // Set a 5 seconds timeout to hide the message later
        shaderMessageTimeout = steady_clock::now() + seconds(5);
      }

      if (shaderMessage != None) {
        // Display the message: green OK if successful, red ERROR if not
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, shaderMessage == OK ? 0xff40ff40 : 0xff4040ff);
        ImGui::TextUnformatted(shaderMessage == OK ? "OK" : "ERROR");
        ImGui::PopStyleColor();

        // Hide the message after a timeout
        if (steady_clock::now() > shaderMessageTimeout) {
          shaderMessage = None;
        }
      }
    }
    ImGui::SameLine(200.f);
    ImGui::Checkbox("Live shader edit mode", &RtxOptions::Get()->useLiveShaderEditModeObject());

    ImGui::Checkbox("Force V-Sync Off?", &RtxOptions::Get()->forceVsyncOffObject());

    if (ImGui::CollapsingHeader("Camera", collapsingHeaderFlags)) {
      ImGui::Indent();

      const Vector3& cameraPosition = RtxContext::getLastCameraPosition();
      ImGui::Text("Camera at: %.2f %.2f %.2f", cameraPosition.x, cameraPosition.y, cameraPosition.z);

      RtCamera::showImguiSettings();

      if (ImGui::CollapsingHeader("Camera Animation", collapsingHeaderClosedFlags)) {
        ImGui::Checkbox("Animate Camera", &RtxOptions::Get()->shakeCameraObject());
        cameraAnimationModeCombo.getKey(&RtxOptions::Get()->cameraAnimationModeObject());
        ImGui::DragFloat("Animation Amplitude", &RtxOptions::Get()->cameraAnimationAmplitudeObject(), 0.1f, 0.f, 1000.f, "%.2f", sliderFlags);
        ImGui::DragInt("Shake Period", &RtxOptions::Get()->cameraShakePeriodObject(), 0.1f, 1, 100, "%d", sliderFlags);
      }

      if (ImGui::CollapsingHeader("Advanced", collapsingHeaderClosedFlags)) {

        ImGui::Checkbox("Portals: Camera History Correction", &RtxOptions::Get()->rayPortalCameraHistoryCorrectionObject());
        ImGui::Checkbox("Portals: Camera In-Between Portals Correction", &RtxOptions::Get()->rayPortalCameraInBetweenPortalsCorrectionObject());
        ImGui::Checkbox("Skip Objects Rendered with Unknown Camera", &RtxOptions::Get()->skipObjectsWithUnknownCameraObject());

        ImGui::Checkbox("Override Near Plane (if less than original)", &RtxOptions::Get()->enableNearPlaneOverrideObject());
        ImGui::BeginDisabled(!RtxOptions::Get()->enableNearPlaneOverride());
        ImGui::DragFloat("Desired Near Plane Distance", &RtxOptions::Get()->nearPlaneOverrideObject(), 0.01f, 0.0001f, FLT_MAX, "%.3f");
        ImGui::EndDisabled();
      }
      ImGui::Unindent();
    }

    if (ImGui::CollapsingHeader("Developer Options", collapsingHeaderFlags)) {
      ImGui::Indent();
      ImGui::Checkbox("Enable", &RtxOptions::Get()->enableDeveloperOptionsObject());
      ImGui::Checkbox("Disable Draw Calls Post RTX Injection", &RtxOptions::Get()->skipDrawCallsPostRTXInjectionObject());
      if (ImGui::Checkbox("Block Input to Game in UI", &RtxOptions::Get()->blockInputToGameInUIObject())) {
        sendUIActivationMessage();
      }
      ImGui::Checkbox("Force Camera Jitter", &RtxOptions::Get()->forceCameraJitterObject());
      ImGui::DragIntRange2("Draw Call Range Filter", &RtxOptions::Get()->drawCallRangeObject(), 1.f, 0, INT32_MAX, nullptr, nullptr, ImGuiSliderFlags_AlwaysClamp);
      ImGui::InputInt("Instance Index Start", &RtxOptions::Get()->instanceOverrideInstanceIdxObject());
      ImGui::InputInt("Instance Index Range", &RtxOptions::Get()->instanceOverrideInstanceIdxRangeObject());
      ImGui::DragFloat3("Instance World Offset", &RtxOptions::Get()->instanceOverrideWorldOffsetObject(), 0.1f, -100.f, 100.f, "%.3f", sliderFlags);
      ImGui::Checkbox("Instance - Print Hash", &RtxOptions::Get()->instanceOverrideSelectedInstancePrintMaterialHashObject());
      ImGui::Unindent();
      ImGui::Checkbox("Throttle presents", &RtxOptions::Get()->enablePresentThrottleObject());
      if (RtxOptions::Get()->enablePresentThrottle()) {
        ImGui::Indent();
        ImGui::SliderInt("Present delay (ms)", &RtxOptions::Get()->presentThrottleDelayObject(), 1, 100, "%d", sliderFlags);
        ImGui::Unindent();
      }
      ImGui::Checkbox("Validate CPU index data", &RtxOptions::Get()->validateCPUIndexDataObject());
    }

    ImGui::PopItemWidth();
  }

  void ImGUI::showTextureSelectionGrid(const char* uniqueId, const uint32_t texturesPerRow, const float thumbnailSize, std::unordered_set<XXH64_hash_t>& data) {
    ImGui::PushID(uniqueId);
    uint32_t cnt = 0;
    float x = 0;
    const float startX = ImGui::GetCursorPosX();
    const float thumbnailSpacing = ImGui::GetStyle().ItemSpacing.x;
    const float thumbnailPadding = ImGui::GetStyle().CellPadding.x;
    for (auto& pair : g_imguiTextureMap) {
      if (data.find(pair.first) != data.end())
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.996078f, 0.329412f, 0.f, 1.f));
      else
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.f, 0.f, 0.f, 1.00f));

      // Lazily create the tex ID ImGUI wants
      if(pair.second.texID == VK_NULL_HANDLE)
        pair.second.texID = ImGui_ImplVulkan_AddTexture(VK_NULL_HANDLE, pair.second.imageView->handle(), VK_IMAGE_LAYOUT_GENERAL);

      const auto& imageInfo = pair.second.imageView->imageInfo();

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

      if (ImGui::ImageButton(pair.second.texID, extent)) {
        const char* action;
        if (data.find(pair.first) != data.end()) {
          data.erase(pair.first);
          action = "removed";
        }
        else {
          data.insert(pair.first);
          action = "added";
        }

        char buffer[256];
        sprintf_s(buffer, "%s - %s %016llX\n", uniqueId, action, pair.first);
        Logger::info(buffer);
      }

      if (ImGui::IsItemHovered()) {
        std::stringstream formatName;
        formatName << imageInfo.format;

        char tooltip[1024];
        sprintf(tooltip, "%s: %dx%d %s\nHash: 0x%" PRIx64,
                (imageInfo.usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) ? "Render Target" : "Texture",
                imageInfo.extent.width, imageInfo.extent.height,
                formatName.str().c_str() + strlen("VK_FORMAT_"), pair.first);

        ImGui::SetTooltip(tooltip);
        if (ImGui::IsMouseDown(ImGuiMouseButton_Middle)) {
          ImGui::LogToClipboard();
          ImGui::LogText("%" PRIx64, pair.first);
          ImGui::LogFinish();
        }

        RtxOptions::Get()->highlightedTextureRef() = pair.first;
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

    ImGui::NewLine();
    ImGui::PopID();
  }

  void ImGUI::showEnhancementsWindow(const Rc<DxvkContext>& ctx) {
    ImGui::PushItemWidth(200);

    auto common = ctx->getCommonObjects();

    if (common->getSceneManager().areReplacementsLoaded() && RtxOptions::Get()->getEnableAnyReplacements()) {
      ImGui::Text("Disable all asset enhancements to capture.");
    } else if (ImGui::Button("Capture Frame in USD")) {
      RtxContext::triggerUsdCapture();
    }

    if (!common->getSceneManager().areReplacementsLoaded())
      ImGui::Text("No USD enhancements detected, the following options have been disabled.  See documentation for how to use enhancements with Remix.");

    ImGui::BeginDisabled(!common->getSceneManager().areReplacementsLoaded());
    ImGui::Checkbox("Enable Enhanced Assets", &RtxOptions::Get()->enableReplacementAssetsObject());
    {
      ImGui::BeginDisabled(!RtxOptions::Get()->enableReplacementAssets());
      ImGui::Checkbox("Enable Enhanced Materials", &RtxOptions::Get()->enableReplacementMaterialsObject());
      ImGui::Checkbox("Enable Adaptive Texture Resolution", &RtxOptions::Get()->enableAdaptiveResolutionReplacementTexturesObject());
      ImGui::DragInt("Skip Texture Mip Map Levels", &RtxOptions::Get()->skipReplacementTextureMipMapLevelObject(), 0.1f, 0, 16, "%d", sliderFlags);
      ImGui::Checkbox("Force High Resolution Textures", &RtxOptions::Get()->forceHighResolutionReplacementTexturesObject());
      ImGui::Checkbox("Enable Enhanced Meshes", &RtxOptions::Get()->enableReplacementMeshesObject());
      ImGui::Checkbox("Enable Enhanced Lights", &RtxOptions::Get()->enableReplacementLightsObject());
      ImGui::EndDisabled();
    }
    ImGui::EndDisabled();
    ImGui::Separator();
    ImGui::Checkbox("Highlight Legacy Materials (flash red)", &RtxOptions::Get()->useHighlightLegacyModeObject());
    ImGui::Checkbox("Highlight Legacy Meshes with Shared Vertex Buffers (dull purple)", &RtxOptions::Get()->useHighlightUnsafeAnchorModeObject());
    ImGui::Checkbox("Highlight Replacements with Unstable Anchors (flash red)", &RtxOptions::Get()->useHighlightUnsafeReplacementModeObject());
  }

  void ImGUI::showSetupWindow(const Rc<DxvkContext>& ctx) {
    ImGui::PushItemWidth(200);

    const float thumbnailSize = 135.f;
    const float thumbnailSpacing = ImGui::GetStyle().ItemSpacing.x;
    const float thumbnailPadding = ImGui::GetStyle().CellPadding.x;
    const uint32_t numThumbnailsPerRow = uint32_t(std::max(1.f, (m_windowWidth - 18.f) / (thumbnailSize + thumbnailSpacing + thumbnailPadding * 2.f)));

    ImGui::Checkbox("Preserve discarded textures", &RtxOptions::Get()->keepTexturesForTaggingObject());

    if (IMGUI_ADD_TOOLTIP(ImGui::CollapsingHeader("Step 1: UI Textures", collapsingHeaderClosedFlags), RtxOptions::Get()->uiTexturesDescription())) {
      showTextureSelectionGrid("uitextures", numThumbnailsPerRow, thumbnailSize, RtxOptions::Get()->uiTexturesRef());
    }

    if (IMGUI_ADD_TOOLTIP(ImGui::CollapsingHeader("Step 1.2: Worldspace UI Textures (optional)", collapsingHeaderClosedFlags), RtxOptions::Get()->worldSpaceUiTexturesDescription())) {
      showTextureSelectionGrid("worldspaceuitextures", numThumbnailsPerRow, thumbnailSize, RtxOptions::Get()->worldSpaceUiTexturesRef());
    }

    if (ImGui::CollapsingHeader("Step 2: Parameter Tuning", collapsingHeaderClosedFlags)) {
      ImGui::Indent();
      ImGui::DragFloat("Scene Unit Scale", &RtxOptions::Get()->sceneScaleObject(), 0.01f, 0.01f, FLT_MAX, "%.3f", sliderFlags);
      ImGui::Checkbox("Scene Z-Up", &RtxOptions::Get()->zUpObject());
      ImGui::Checkbox("Scene Left-Handed", &RtxOptions::Get()->isLHSObject());
      fusedWorldViewModeCombo.getKey(&RtxOptions::Get()->fusedWorldViewModeRef());
      ImGui::Separator();

      ImGui::DragFloat("Unique Object Search Distance", &RtxOptions::Get()->uniqueObjectDistanceObject(), 0.01f, 0.01f, FLT_MAX, "%.3f", sliderFlags);
      ImGui::Separator();

      ImGui::Checkbox("Shader-based Vertex Capture", &RtxOptions::Get()->useVertexCaptureObject());
      ImGui::Separator();

      ImGui::Checkbox("Ignore Stencil Volumes", &RtxOptions::Get()->ignoreStencilVolumeHeuristicsObject());
      ImGui::Separator();

      ImGui::DragFloat("Vertex Color Strength", &RtxOptions::Get()->vertexColorStrengthObject(), 0.001f, 0.0f, 1.0f);
      ImGui::Separator();
      
      if (ImGui::CollapsingHeader("View Model", collapsingHeaderClosedFlags)) {
        ImGui::Indent();
        ImGui::Checkbox("Enable View Model", &RtxOptions::Get()->viewModel.enableObject());
        ImGui::Checkbox("Virtual Instances", &RtxOptions::Get()->viewModel.enableVirtualInstancesObject());
        ImGui::Checkbox("Perspective Correction", &RtxOptions::Get()->viewModel.perspectiveCorrectionObject());
        ImGui::Checkbox("Separate Rays", &RtxOptions::Get()->viewModel.separateRaysObject());
        if (RtxOptions::Get()->viewModel.separateRays()) {
          ImGui::DragFloat("Range [m]", &RtxOptions::Get()->viewModel.rangeMetersObject(), 0.01f, 0.0f, 1000.0f);
        } else {
          ImGui::DragFloat("Scale", &RtxOptions::Get()->viewModel.scaleObject(), 0.01f, 0.01f, 2.0f);
        }
        ImGui::Unindent();
      }

      auto common = ctx->getCommonObjects();
      common->getSceneManager().getLightManager().showImguiSettings();
      ImGui::Unindent();
    }

    if (ImGui::CollapsingHeader("Step 3: Sky Parameters (optional)", collapsingHeaderClosedFlags)) {
      ImGui::Indent();

      ImGui::DragFloat("Sky Brightness", &RtxOptions::Get()->skyBrightnessObject(), 0.01f, 0.01f, FLT_MAX, "%.3f", sliderFlags);
      {
        static const char* exts[] = { "256 (1.5MB vidmem)", "512 (6MB vidmem)", "1024 (24MB vidmem)",
          "2048 (96MB vidmem)", "4096 (384MB vidmem)", "8192 (1.5GB vidmem)" };

        static int extIdx;
        extIdx = std::clamp(bit::tzcnt(RtxOptions::Get()->skyProbeSide()), 8u, 13u) - 8;

        ImGui::Combo("Sky Probe Extent", &extIdx, exts, IM_ARRAYSIZE(exts));
        RtxOptions::Get()->skyProbeSideRef() = 1 << (extIdx + 8);
      }

      ImGui::Checkbox("Force HDR sky", &RtxOptions::Get()->skyForceHDRObject());

      ImGui::Separator();
      ImGui::InputInt("First N untextured drawcalls", &RtxOptions::Get()->skyDrawcallIdThresholdObject(), 1, 1, 0);
      ImGui::Separator();

      if (IMGUI_ADD_TOOLTIP(ImGui::CollapsingHeader("Sky Textures", collapsingHeaderClosedFlags), RtxOptions::Get()->skyBoxTexturesDescription())) {
        showTextureSelectionGrid("skytextures", numThumbnailsPerRow, thumbnailSize, RtxOptions::Get()->skyBoxTexturesRef());
      }

      ImGui::Unindent();
    }

    if (IMGUI_ADD_TOOLTIP(ImGui::CollapsingHeader("Step 4: Ignore Textures (optional)", collapsingHeaderClosedFlags), RtxOptions::Get()->ignoreTexturesDescription())) {
      showTextureSelectionGrid("ignoretextures", numThumbnailsPerRow, thumbnailSize, RtxOptions::Get()->ignoreTexturesRef());
    }

    if (IMGUI_ADD_TOOLTIP(ImGui::CollapsingHeader("Step 5: Ignore Lights (optional)", collapsingHeaderClosedFlags), RtxOptions::Get()->ignoreLightsDescription())) {
      showTextureSelectionGrid("ignorelights", numThumbnailsPerRow, thumbnailSize, RtxOptions::Get()->ignoreLightsRef());
    }

    if (IMGUI_ADD_TOOLTIP(ImGui::CollapsingHeader("Step 6: Particle Textures (optional)", collapsingHeaderClosedFlags), RtxOptions::Get()->particleTexturesDescription())) {
      showTextureSelectionGrid("particletextures", numThumbnailsPerRow, thumbnailSize, RtxOptions::Get()->particleTexturesRef());
    }

    if (IMGUI_ADD_TOOLTIP(ImGui::CollapsingHeader("Step 6.1: Beam Textures (optional)", collapsingHeaderClosedFlags), RtxOptions::Get()->beamTexturesDescription())) {
      showTextureSelectionGrid("beamtextures", numThumbnailsPerRow, thumbnailSize, RtxOptions::Get()->beamTexturesRef());
    }

    if (IMGUI_ADD_TOOLTIP(ImGui::CollapsingHeader("Step 6.2: Add Lights to Textures (optional)", collapsingHeaderClosedFlags), RtxOptions::Get()->lightConverterDescription())) {
      showTextureSelectionGrid("lightconvertertextures", numThumbnailsPerRow, thumbnailSize, RtxOptions::Get()->lightConverterRef());
    }

    if (IMGUI_ADD_TOOLTIP(ImGui::CollapsingHeader("Step 7: Decal Textures (optional)", collapsingHeaderClosedFlags), RtxOptions::Get()->decalTexturesDescription())) {
      showTextureSelectionGrid("decaltextures", numThumbnailsPerRow, thumbnailSize, RtxOptions::Get()->decalTexturesRef());
    }

    if (IMGUI_ADD_TOOLTIP(ImGui::CollapsingHeader("Step 7.1: Dynamic Decal Textures", collapsingHeaderClosedFlags), RtxOptions::Get()->dynamicDecalTexturesDescription())) {
      showTextureSelectionGrid("dynamicdecaltextures", numThumbnailsPerRow, thumbnailSize, RtxOptions::Get()->dynamicDecalTexturesRef());
    }

    if (IMGUI_ADD_TOOLTIP(ImGui::CollapsingHeader("Step 7.2: Non-Offset Decal Textures", collapsingHeaderClosedFlags), RtxOptions::Get()->nonOffsetDecalTexturesDescription())) {
      showTextureSelectionGrid("nonoffsetdecaltextures", numThumbnailsPerRow, thumbnailSize, RtxOptions::Get()->nonOffsetDecalTexturesRef());
    }

    if (IMGUI_ADD_TOOLTIP(ImGui::CollapsingHeader("Step 8.1: Legacy Cutout Textures (optional)", collapsingHeaderClosedFlags), RtxOptions::Get()->cutoutTexturesDescription())) {
      ImGui::DragFloat("Force Cutout Alpha", &RtxOptions::Get()->forceCutoutAlphaObject(), 0.01f, 0.0f, 1.0f, "%.3f", sliderFlags);
      showTextureSelectionGrid("cutouttextures", numThumbnailsPerRow, thumbnailSize, RtxOptions::Get()->cutoutTexturesRef());
    }
    
    if (IMGUI_ADD_TOOLTIP(ImGui::CollapsingHeader("Step 8.2: Terrain Textures", collapsingHeaderClosedFlags), RtxOptions::Get()->terrainTexturesDescription())) {
      showTextureSelectionGrid("terraintextures", numThumbnailsPerRow, thumbnailSize, RtxOptions::Get()->terrainTexturesRef());
    }

    if (IMGUI_ADD_TOOLTIP(ImGui::CollapsingHeader("Step 8.3: Water Textures (optional)", collapsingHeaderClosedFlags), RtxOptions::Get()->animatedWaterTexturesDescription())) {
      showTextureSelectionGrid("watertextures", numThumbnailsPerRow, thumbnailSize, RtxOptions::Get()->animatedWaterTexturesRef());
    }

    if (IMGUI_ADD_TOOLTIP(ImGui::CollapsingHeader("Step 9.1: Player Model Textures (optional)", collapsingHeaderClosedFlags), RtxOptions::Get()->playerModelTexturesDescription())) {
      showTextureSelectionGrid("playermodeltextures", numThumbnailsPerRow, thumbnailSize, RtxOptions::Get()->playerModelTexturesRef());
    }

    if (IMGUI_ADD_TOOLTIP(ImGui::CollapsingHeader("Step 9.2: Player Model Body Textures (optional)", collapsingHeaderClosedFlags), RtxOptions::Get()->playerModelBodyTexturesDescription())) {
      showTextureSelectionGrid("playermodelbodytextures", numThumbnailsPerRow, thumbnailSize, RtxOptions::Get()->playerModelBodyTexturesRef());
    }

    if (IMGUI_ADD_TOOLTIP(ImGui::CollapsingHeader("Step 10: Opacity Micromap Ignore Textures (optional)", collapsingHeaderClosedFlags), RtxOptions::Get()->opacityMicromapIgnoreTexturesDescription())) {
      showTextureSelectionGrid("opacitymicromapignoretextures", numThumbnailsPerRow, thumbnailSize, RtxOptions::Get()->opacityMicromapIgnoreTexturesRef());
    }

    if (ImGui::CollapsingHeader("Step 11: Material Options (optional)", collapsingHeaderClosedFlags)) {
      ImGui::Indent();

      if (ImGui::CollapsingHeader("Legacy Material Defaults", collapsingHeaderFlags)) {
        ImGui::Indent();

        LegacyMaterialDefaults& legacyMaterial = RtxOptions::Get()->legacyMaterial;
        ImGui::Checkbox("Use Albedo/Opacity Texture (if present)", &legacyMaterial.useAlbedoTextureIfPresentObject());
        ImGui::ColorEdit3("Albedo", &legacyMaterial.albedoConstantObject());
        ImGui::DragFloat("Opacity", &legacyMaterial.opacityConstantObject());
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
            // Note: Convert from normalized thickness (used on the GPU) to thickness in nanometers. Ideally this should not need to be done as we should not be
            // modifying data given to the GPU directly here (and rather it should simply be modifying an option which is later translated to GPU data), but this is
            // how it is currently.
            float currentThinFilmThicknessOverride =
              opaqueMaterialOptions.thinFilmNormalizedThicknessOverride() * OPAQUE_SURFACE_MATERIAL_THIN_FILM_MAX_THICKNESS;

            if (IMGUI_ADD_TOOLTIP(
                  ImGui::SliderFloat("Thin Film Thickness", &currentThinFilmThicknessOverride, 0.0f, OPAQUE_SURFACE_MATERIAL_THIN_FILM_MAX_THICKNESS, "%.1f nm", sliderFlags),
                  opaqueMaterialOptions.thinFilmNormalizedThicknessOverrideDescription())) {
              // Note: Renormalize only on value update to avoid potential cyclic behavior with denormalizing/renormalizing every iteration this code is run.
              opaqueMaterialOptions.thinFilmNormalizedThicknessOverrideRef() = std::clamp(currentThinFilmThicknessOverride / OPAQUE_SURFACE_MATERIAL_THIN_FILM_MAX_THICKNESS, 0.0f, 1.0f);
            }
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

    ImGui::PopItemWidth();
  }

  void ImGUI::setupStyle(ImGuiStyle* dst) {
    ImGuiStyle* style = dst ? dst : &ImGui::GetStyle();

    style->Colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.f, 0.f, 0.f, 0.4f);
    style->TabRounding = 1;
  }

  void ImGUI::showReflexOptions() {
    if (RtxOptions::Get()->isReflexSupported()) {
      m_userGraphicsSettingChanged |= ImGui::Combo("Reflex", &RtxOptions::Get()->reflexModeObject(), "Disabled\0Enabled\0Enabled + Boost\0");
    }
  }

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
      ImGui::Indent();

#ifdef REMIX_DEVELOPMENT
      ImGui::Checkbox("Raytracing Enabled", & RtxOptions::Get()->enableRaytracingObject()); 

      renderPassGBufferRaytraceModeCombo.getKey(&RtxOptions::Get()->renderPassGBufferRaytraceModeObject());
      renderPassIntegrateDirectRaytraceModeCombo.getKey(&RtxOptions::Get()->renderPassIntegrateDirectRaytraceModeObject());
      renderPassIntegrateIndirectRaytraceModeCombo.getKey(&RtxOptions::Get()->renderPassIntegrateIndirectRaytraceModeObject());

      ImGui::Separator();
#endif

      showReflexOptions();

      if (ctx->getCommonObjects()->metaDLSS().supportsDLSS()) {
        upscalerCombo.getKey(&RtxOptions::Get()->upscalerTypeObject());
      } else {
        upscalerDlssUnsupportCombo.getKey(&RtxOptions::Get()->upscalerTypeObject());
      }

      RtxOptions::Get()->updatePresetFromUpscaler();

      if (RtxOptions::Get()->upscalerType() == UpscalerType::DLSS && !ctx->getCommonObjects()->metaDLSS().supportsDLSS())
        RtxOptions::Get()->upscalerTypeRef() = UpscalerType::TAAU;

      if (RtxOptions::Get()->upscalerType() == UpscalerType::DLSS) {
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
        ImGui::Checkbox("Indirect Translucent Shadows", &RtxOptions::Get()->enableIndirectTranslucentShadowsObject());
        ImGui::Checkbox("Decal Material Blending", &RtxOptions::Get()->enableDecalMaterialBlendingObject());
        ImGui::Checkbox("Billboard Orientation Correction", &RtxOptions::Get()->enableBillboardOrientationCorrectionObject());
        if (RtxOptions::Get()->enableBillboardOrientationCorrection()) {
          ImGui::Indent();
          ImGui::Checkbox("Dev: Use i-prims on primary rays", &RtxOptions::Get()->useIntersectionBillboardsOnPrimaryRaysObject());
          ImGui::Unindent();
        }

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
        ImGui::Checkbox("Unordered Resolve in Indirect Rays", &RtxOptions::Get()->enableUnorderedResolveInIndirectRaysObject());
        ImGui::Checkbox("Emissive Particles in Indirect Rays", &RtxOptions::Get()->enableEmissiveParticlesInIndirectRaysObject());
        // # bounces limitted by 4b allocation in payload
        // Note: it's possible get up to 16 bounces => will require logic adjustment
        ImGui::DragInt("Minimum Path Bounces", &RtxOptions::Get()->pathMinBouncesObject(), 1.0f, 0, 15, "%d", sliderFlags);
        ImGui::DragInt("Maximum Path Bounces", &RtxOptions::Get()->pathMaxBouncesObject(), 1.0f, RtxOptions::Get()->pathMinBounces(), 15, "%d", sliderFlags);
        ImGui::DragFloat("Firefly Filtering Luminance Threshold", &RtxOptions::Get()->fireflyFilteringLuminanceThresholdObject(), 0.1f, 0.0f, FLT_MAX, "%.3f", sliderFlags);
        ImGui::DragFloat("Opaque Diffuse Lobe Probability Zero Threshold", &RtxOptions::Get()->opaqueDiffuseLobeSamplingProbabilityZeroThresholdObject(), 0.001f, 0.0f, 1.0f, "%.3f", sliderFlags);
        ImGui::DragFloat("Min Opaque Diffuse Lobe Probability", &RtxOptions::Get()->minOpaqueDiffuseLobeSamplingProbabilityObject(), 0.001f, 0.0f, 1.0f, "%.3f", sliderFlags);
        ImGui::DragFloat("Opaque Specular Lobe Probability Zero Threshold", &RtxOptions::Get()->opaqueSpecularLobeSamplingProbabilityZeroThresholdObject(), 0.001f, 0.0f, 1.0f, "%.3f", sliderFlags);
        ImGui::DragFloat("Min Opaque Specular Lobe Probability", &RtxOptions::Get()->minOpaqueSpecularLobeSamplingProbabilityObject(), 0.001f, 0.0f, 1.0f, "%.3f", sliderFlags);
        ImGui::DragFloat("Opaque Opacity Transmission Lobe Probability Zero Threshold", &RtxOptions::Get()->opaqueOpacityTransmissionLobeSamplingProbabilityZeroThresholdObject(), 0.001f, 0.0f, 1.0f, "%.3f", sliderFlags);
        ImGui::DragFloat("Min Opaque Opacity Transmission Lobe Probability", &RtxOptions::Get()->minOpaqueOpacityTransmissionLobeSamplingProbabilityObject(), 0.001f, 0.0f, 1.0f, "%.3f", sliderFlags);
        ImGui::DragFloat("Translucent Specular Lobe Probability Zero Threshold", &RtxOptions::Get()->translucentSpecularLobeSamplingProbabilityZeroThresholdObject(), 0.001f, 0.0f, 1.0f, "%.3f", sliderFlags);
        ImGui::DragFloat("Min Translucent Specular Lobe Probability", &RtxOptions::Get()->minTranslucentSpecularLobeSamplingProbabilityObject(), 0.001f, 0.0f, 1.0f, "%.3f", sliderFlags);
        ImGui::DragFloat("Translucent Transmission Lobe Probability Zero Threshold", &RtxOptions::Get()->translucentTransmissionLobeSamplingProbabilityZeroThresholdObject(), 0.001f, 0.0f, 1.0f, "%.3f", sliderFlags);
        ImGui::DragFloat("Min Translucent Transmission Lobe Probability", &RtxOptions::Get()->minTranslucentTransmissionLobeSamplingProbabilityObject(), 0.001f, 0.0f, 1.0f, "%.3f", sliderFlags);
        ImGui::DragFloat("Indirect Ray Spread Angle Factor", &RtxOptions::Get()->indirectRaySpreadAngleFactorObject(), 0.001f, 0.0f, 1.0f, "%.3f", sliderFlags);

        if (RtxOptions::Get()->enableRussianRoulette() && ImGui::CollapsingHeader("Russian Roulette", collapsingHeaderClosedFlags)) {
          ImGui::Indent();

          ImGui::DragFloat("1st bounce: Min Continue Probability", &RtxOptions::Get()->russianRoulette1stBounceMinContinueProbabilityObject(), 0.01f, 0.0f, 1.0f, "%.3f", sliderFlags);
          ImGui::DragFloat("1st bounce: Max Continue Probability", &RtxOptions::Get()->russianRoulette1stBounceMaxContinueProbabilityObject(), 0.01f, 0.0f, 1.0f, "%.3f", sliderFlags);
          ImGui::DragFloat("2nd+ bounce: Max Continue Probability", &RtxOptions::Get()->russianRouletteMaxContinueProbabilityObject(), 0.01f, 0.0f, 1.0f, "%.3f", sliderFlags);

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
      ImGui::DragFloat("Effect Light Intensity", &RtxOptions::Get()->effectLightIntensityObject(), 0.01f, 0.0f, FLT_MAX, "%.3f", sliderFlags);
      ImGui::DragFloat("Effect Light Radius", &RtxOptions::Get()->effectLightRadiusObject(), 0.01f, 0.01f, FLT_MAX, "%.3f", sliderFlags);

      ImGui::DragFloat("Emissive Intensity", &RtxOptions::Get()->emissiveIntensityObject(), 0.01f, 0.0f, FLT_MAX, "%.3f", sliderFlags);
      ImGui::Separator();
      ImGui::SliderInt("RIS Light Sample Count", &RtxOptions::Get()->risLightSampleCountObject(), 0, 64);
      ImGui::Separator();
      ImGui::Checkbox("Direct Lighting Enabled", &RtxOptions::Get()->enableDirectLightingObject());
      ImGui::Checkbox("Indirect Lighting Enabled", &RtxOptions::Get()->enableSecondaryBouncesObject());

      if (ImGui::CollapsingHeader("RTXDI", collapsingHeaderClosedFlags)) {
        ImGui::Indent();

        ImGui::Checkbox("Enable RTXDI", &RtxOptions::Get()->useRTXDIObject());
        ImGui::Checkbox("Use Previous TLAS", &RtxOptions::Get()->enablePreviousTLASObject());

        auto& rtxdi = common->metaRtxdiRayQuery();
        rtxdi.showImguiSettings();
        ImGui::Unindent();
      }

      if (ImGui::CollapsingHeader("ReSTIR GI", collapsingHeaderClosedFlags)) {
        ImGui::Indent();

        ImGui::Checkbox("Enable ReSTIR GI", &RtxOptions::Get()->useReSTIRGIObject());

        ImGui::PushID("ReSTIR GI");
        auto& restirGI = common->metaReSTIRGIRayQuery();
        restirGI.showImguiSettings();
        ImGui::PopID();
        ImGui::Unindent();
      }
      ImGui::Unindent();
    }

    if (ImGui::CollapsingHeader("Volumetrics", collapsingHeaderClosedFlags)) {
      ImGui::Indent();

      ImGui::DragInt("Froxel Grid Resolution Scale", &RtxOptions::Get()->froxelGridResolutionScaleObject(), 0.1f, 1);
      ImGui::DragInt("Froxel Depth Slices", &RtxOptions::Get()->froxelDepthSlicesObject(), 0.1f, 1, UINT16_MAX);
      ImGui::DragInt("Max Accumulation Frames", &RtxOptions::Get()->maxAccumulationFramesObject(), 0.1f, 1, UINT8_MAX);
      ImGui::DragFloat("Froxel Depth Slice Distribution Exponent", &RtxOptions::Get()->froxelDepthSliceDistributionExponentObject(), 0.01f, 0.0f, FLT_MAX, "%.3f", sliderFlags);
      ImGui::DragFloat("Froxel Max Distance", &RtxOptions::Get()->froxelMaxDistanceObject(), 0.25f, 0.0f, FLT_MAX, "%.2f", sliderFlags);
      ImGui::DragFloat("Froxel Firefly Filtering Luminance Threshold", &RtxOptions::Get()->froxelFireflyFilteringLuminanceThresholdObject(), 0.1f, 0.0f, FLT_MAX, "%.3f", sliderFlags);
      ImGui::DragFloat("Froxel Filter Gaussian Sigma", &RtxOptions::Get()->froxelFilterGaussianSigmaObject(), 0.01f, 0.0f, FLT_MAX, "%.2f", sliderFlags);
      ImGui::Checkbox("Per-Portal Volumes", &RtxOptions::Get()->enableVolumetricsInPortalsObject());

      ImGui::Separator();

      ImGui::DragInt("Initial RIS Sample Count", &RtxOptions::Get()->volumetricInitialRISSampleCountObject(), 0.05f, 1, UINT8_MAX);
      ImGui::Checkbox("Enable Initial Visibility", &RtxOptions::Get()->volumetricEnableInitialVisibilityObject());
      ImGui::Checkbox("Enable Temporal Resampling", &RtxOptions::Get()->volumetricEnableTemporalResamplingObject());
      ImGui::DragInt("Temporal Reuse Max Sample Count", &RtxOptions::Get()->volumetricTemporalReuseMaxSampleCountObject(), 1.0f, 1, UINT16_MAX);
      ImGui::DragFloat("Clamped Reprojection Confidence Pentalty", &RtxOptions::Get()->volumetricClampedReprojectionConfidencePenaltyObject(), 0.01f, 0.0f, 1.0f, "%.3f", sliderFlags);

      ImGui::Separator();

      ImGui::DragInt("Min Reservoir Samples", &RtxOptions::Get()->froxelMinReservoirSamplesObject(), 0.05f, 1, UINT8_MAX);
      ImGui::DragInt("Max Reservoir Samples", &RtxOptions::Get()->froxelMaxReservoirSamplesObject(), 0.05f, 1, UINT8_MAX);
      ImGui::DragInt("Min Reservoir Samples Stability History", &RtxOptions::Get()->froxelMinReservoirSamplesStabilityHistoryObject(), 0.1f, 1, UINT8_MAX);
      ImGui::DragInt("Max Reservoir Samples Stability History", &RtxOptions::Get()->froxelMaxReservoirSamplesStabilityHistoryObject(), 0.1f, 1, UINT8_MAX);
      ImGui::DragFloat("Reservoir Samples Stability History Power", &RtxOptions::Get()->froxelReservoirSamplesStabilityHistoryPowerObject(), 0.01f, 0.0f, FLT_MAX, "%.2f", sliderFlags);

      ImGui::DragInt("Min Kernel Radius", &RtxOptions::Get()->froxelMinKernelRadiusObject(), 0.05f, 1, UINT8_MAX);
      ImGui::DragInt("Max Kernel Radius", &RtxOptions::Get()->froxelMaxKernelRadiusObject(), 0.05f, 1, UINT8_MAX);
      ImGui::DragInt("Min Kernel Radius Stability History", &RtxOptions::Get()->froxelMinKernelRadiusStabilityHistoryObject(), 0.1f, 1, UINT8_MAX);
      ImGui::DragInt("Min Kernel Radius Stability History", &RtxOptions::Get()->froxelMaxKernelRadiusStabilityHistoryObject(), 0.1f, 1, UINT8_MAX);
      ImGui::DragFloat("Kernel Radius Stability History Power", &RtxOptions::Get()->froxelKernelRadiusStabilityHistoryPowerObject(), 0.01f, 0.0f, FLT_MAX, "%.2f", sliderFlags);

      ImGui::Separator();

      ImGui::Checkbox("Enable Volumetric Lighting", &RtxOptions::Get()->enableVolumetricLightingObject());
      ImGui::DragFloat3("Transmittance Color", &RtxOptions::Get()->volumetricTransmittanceColorObject(), 0.01f, 0.0f, 1.0f, "%.3f");
      ImGui::DragFloat("Transmittance Measurement Distance", &RtxOptions::Get()->volumetricTransmittanceMeasurementDistanceObject(), 0.25f, 0.0f, FLT_MAX, "%.2f", sliderFlags);
      ImGui::DragFloat3("Single Scattering Albedo", &RtxOptions::Get()->volumetricSingleScatteringAlbedoObject(), 0.01f, 0.0f, 1.0f, "%.3f");
      ImGui::DragFloat("Anisotropy", &RtxOptions::Get()->volumetricAnisotropyObject(), 0.01f, -1.0f, 1.0f, "%.3f", sliderFlags);

      ImGui::Separator();

      ImGui::Checkbox("Enable Legacy Fog Remapping", &RtxOptions::Get()->enableFogRemapObject());
      ImGui::DragFloat("Color Strength", &RtxOptions::Get()->fogRemapColorStrengthObject(), 0.0f, 0.0f, FLT_MAX, "%.2f", sliderFlags);
      ImGui::DragFloat("Legacy Max Distance Min", &RtxOptions::Get()->fogRemapMaxDistanceMinObject(), 0.25f, 0.0f, FLT_MAX, "%.2f", sliderFlags);
      ImGui::DragFloat("Legacy Max Distance Max", &RtxOptions::Get()->fogRemapMaxDistanceMaxObject(), 0.25f, 0.0f, FLT_MAX, "%.2f", sliderFlags);
      ImGui::DragFloat("Remapped Transmittance Measurement Distance Min", &RtxOptions::Get()->fogRemapTransmittanceMeasurementDistanceMinObject(), 0.25f, 0.0f, FLT_MAX, "%.2f", sliderFlags);
      ImGui::DragFloat("Remapped Transmittance Measurement Distance Max", &RtxOptions::Get()->fogRemapTransmittanceMeasurementDistanceMaxObject(), 0.25f, 0.0f, FLT_MAX, "%.2f", sliderFlags);

      // Note: Must be called if the volumetrics options changed.
      RtxOptions::Get()->updateCachedVolumetricOptions();

      ImGui::Unindent();
    }

    if (ImGui::CollapsingHeader("Alpha Test/Blending", collapsingHeaderClosedFlags)) {
      ImGui::Indent();

      ImGui::Checkbox("Render Alpha Blended", &RtxOptions::Get()->enableAlphaBlendObject());
      ImGui::Checkbox("Render Alpha Tested", &RtxOptions::Get()->enableAlphaTestObject());
      ImGui::Separator();
      ImGui::Checkbox("Emissive Blend Override", &RtxOptions::Get()->enableEmissiveBlendEmissiveOverrideObject());
      ImGui::DragFloat("Emissive Blend Override Intensity", &RtxOptions::Get()->emissiveBlendOverrideEmissiveIntensityObject(), 0.001f, 0.0f, FLT_MAX, "%.3f", sliderFlags);
      ImGui::Separator();
      ImGui::SliderFloat("Particle Softness", &RtxOptions::Get()->particleSoftnessFactorObject(), 0.f, 0.5f);

      common->metaComposite().showStochasticAlphaBlendImguiSettings();
      ImGui::Unindent();
    }

    if (ImGui::CollapsingHeader("Denoising", collapsingHeaderFlags)) {
      ImGui::Indent();
      ImGui::Checkbox("Denoising Enabled", &RtxOptions::Get()->useDenoiserObject());
      ImGui::Checkbox("Reference Mode", &RtxOptions::Get()->useDenoiserReferenceModeObject());
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
        bool useDoubleDenoisers = RtxOptions::Get()->isSeparatedDenoiserEnabled();
        bool isReferenceMode = RtxOptions::Get()->useDenoiserReferenceMode();
        if (isReferenceMode) {
          if (ImGui::CollapsingHeader("Reference Denoiser", collapsingHeaderFlags)) {
            ImGui::Indent();
            ImGui::PushID("Reference Denoiser");
            common->metaReferenceDenoiser().showImguiSettings();
            ImGui::PopID();
            ImGui::Unindent();
          }
        }
        else if(useDoubleDenoisers) {
          if(ImGui::CollapsingHeader("Primary Direct Light Denoiser", collapsingHeaderClosedFlags)) {
            ImGui::Indent();
            ImGui::PushID("Primary Direct Light Denoiser");
            common->metaPrimaryDirectLightDenoiser().showImguiSettings();
            ImGui::PopID();
            ImGui::Unindent();
          }

          if(!isReferenceMode && ImGui::CollapsingHeader("Primary Indirect Light Denoiser", collapsingHeaderClosedFlags)) {
            ImGui::Indent();
            ImGui::PushID("Primary Indirect Light Denoiser");
            common->metaPrimaryIndirectLightDenoiser().showImguiSettings();
            ImGui::PopID();
            ImGui::Unindent();
          }
        }
        else {
          if(ImGui::CollapsingHeader("Primary Direct/Indirect Light Denoiser", collapsingHeaderClosedFlags)) {
            ImGui::Indent();
            ImGui::PushID("Primary Direct/Indirect Light Denoiser");
            common->metaPrimaryCombinedLightDenoiser().showImguiSettings();
            ImGui::PopID();
            ImGui::Unindent();
          }
        }

        if(!isReferenceMode && ImGui::CollapsingHeader("Secondary Direct/Indirect Light Denoiser", collapsingHeaderClosedFlags)) {
          ImGui::Indent();
          ImGui::PushID("Secondary Direct/Indirect Light Denoiser");
          common->metaSecondaryCombinedLightDenoiser().showImguiSettings();
          ImGui::PopID();
          ImGui::Unindent();
        }

        ImGui::Unindent();
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
        ImGui::Combo("Tonemapping Mode", &RtxOptions::Get()->tonemappingModeObject(), "Global\0Local\0");
        if (RtxOptions::Get()->tonemappingMode() == TonemappingMode::Global) {
          common->metaToneMapping().showImguiSettings();
        } else {
          common->metaLocalToneMapping().showImguiSettings();
        }
      }

      if (ImGui::CollapsingHeader("Post FX", collapsingHeaderClosedFlags))
        common->metaPostFx().showImguiSettings();
      
      ImGui::Unindent();
    }

    if (ImGui::CollapsingHeader("Debug", collapsingHeaderFlags)) {
      ImGui::Indent();
      common->metaDebugView().showImguiSettings();
      ImGui::Unindent();
    }

    if (ImGui::CollapsingHeader("Geometry", collapsingHeaderClosedFlags)) {
      ImGui::Indent();
      ImGui::Checkbox("Enable Triangle Culling (Globally)", &RtxOptions::Get()->enableCullingObject());
      ImGui::Checkbox("Enable Triangle Culling (Override Secondary Rays)", &RtxOptions::Get()->enableCullingInSecondaryRaysObject());
      ImGui::Separator();
      ImGui::DragInt("Min Prims in Static BLAS", &RtxOptions::Get()->minPrimsInStaticBLASObject(), 1.f, 100, 0);
      ImGui::Checkbox("Portals: Virtual Instance Matching", &RtxOptions::Get()->useRayPortalVirtualInstanceMatchingObject());
      ImGui::Checkbox("Portals: Fade In Effect", &RtxOptions::Get()->enablePortalFadeInEffectObject());
      ImGui::Checkbox("Reset Buffer Cache Every Frame", &RtxOptions::Get()->resetBufferCacheOnEveryFrameObject());
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
        ImGui::DragFloat("Max Anisotropy Level", &RtxOptions::Get()->maxAnisotropyLevelObject(), 0.5f, 1.0f, 16.f, "%.3f", sliderFlags);
      }
      ImGui::DragFloat("Translucent Decal Albedo Factor", &RtxOptions::Get()->translucentDecalAlbedoFactorObject(), 0.01f);
      ImGui::DragFloat("Decal Normal Offset", &RtxOptions::Get()->decalNormalOffsetObject(), 0.0001f, 0.f, 0.f, "%.4f");

      ImGui::Unindent();
    }

    ImGui::PopItemWidth();
  }

  void ImGUI::render(
    const HWND hwnd,
    const Rc<DxvkContext>& ctx,
    VkSurfaceFormatKHR surfaceFormat,
    VkExtent2D        surfaceSize) {
    ZoneScoped;
    ScopedGpuProfileZone(ctx, "ImGUI Render");

    // Sometimes games can change windows on us, so we need to check that here and tell ImGUI
    if (m_hwnd != hwnd) {
      m_hwnd = hwnd;
      ImGui_ImplWin32_Shutdown();
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
      init_info.MinImageCount = 2;
      init_info.ImageCount = 2;
      init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

      ImGui_ImplVulkan_Init(&init_info, ctx->getFramebufferInfo().renderPass()->getDefaultHandle());

      //execute a gpu command to upload imgui font textures
      createFontsTexture(ctx);

      m_init = true;
    }

    RtxOptions::Get()->highlightedTextureRef() = kEmptyHash;

    update(ctx);

    this->setupRendererState(ctx, surfaceFormat, surfaceSize);

    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), ctx->getCmdBuffer(DxvkCmdBuffer::ExecBuffer));

    this->resetRendererState(ctx);
  }
  
  Rc<ImGUI> ImGUI::createGUI(const Rc<DxvkDevice>& device, const HWND& hwnd) {
    return new ImGUI(device, hwnd);
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
      m_fontTexture = m_device->createImage(info, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, DxvkMemoryStats::Category::AppTexture);
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

  bool ImGUI::checkHotkeyState(const VirtualKeys& virtKeys) {
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
          result =
            result && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGui_ImplWin32_VirtualKeyToImGuiKey(vk.val)), false);
        }
      }
    }
    return result;
  }
}
