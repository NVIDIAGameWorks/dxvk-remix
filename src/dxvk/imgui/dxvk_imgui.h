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

#pragma once

#include "imgui.h"

#define WIN32_LEAN_AND_MEAN 
#include <windows.h>
#include <unordered_set>

#include "../util/rc/util_rc.h"
#include "../util/rc/util_rc_ptr.h"
#include "../util/util_keybind.h"

#include "../dxvk_format.h"
#include "../dxvk_util.h"
#include "../dxvk_bind_mask.h"
#include "../dxvk_graphics_state.h"
#include "../dxvk_constant_state.h"
#include "../rtx_render/rtx_options.h"

struct ImPlotContext;

namespace dxvk {
  class ImGuiAbout;
  class ImGuiSplash;
  class ImGuiCapture;
  class DxvkDevice;
  class DxvkContext;

  /**
   * \brief DXVK ImGUI
   * 
   * GUI for manipulating settings at runtime
   */
  class ImGUI {
    
  public:
    
    ImGUI(DxvkDevice* device);
    ~ImGUI();

    bool isInit() const {
      return m_init;
    }
    
    /**
     * \brief Handle windows messages
     * 
     * see: https://docs.microsoft.com/en-us/previous-versions/windows/desktop/legacy/ms633573(v=vs.85)
     */
    void wndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

    /**
     * \brief Render ImGUI
     * 
     * Renders the ImGUI to the given context.
     * \param [in] hwnd Window currently being drawn into
     * \param [in] ctx Device context
     * \param [in] surfaceSize Image size, in pixels
     * \param [in] vsync swapchain vsync enabled
     */
    void render(
            const HWND hwnd,
            const Rc<DxvkContext>&  ctx,
            VkSurfaceFormatKHR surfaceFormat,
            VkExtent2D         surfaceSize,
            bool               vsync);
    
    static void AddTexture(const XXH64_hash_t hash, const Rc<DxvkImageView>& imageView);
    static void ReleaseTexture(const XXH64_hash_t hash);
    static bool checkHotkeyState(const VirtualKeys& virtKeys);

    void switchMenu(UIType type, bool force = false);
    
    enum Tabs {
      kTab_Rendering = 0,
      kTab_Setup,
      kTab_Enhancements,
      kTab_About,
      kTab_Development,
      kTab_Count
    };
    template<Tabs tab>
    void openTab() {
      RtxOptions::Get()->m_showUI.getValue() = UIType::Advanced;
      triggerTab(tab);
    }
    template<Tabs tab>
    bool isTabOpen() const {
      if(RtxOptions::Get()->m_showUI.getValue() != UIType::Advanced) {
        return false;
      } else {
        return m_curTab == tab;
      }
    }

  private:
    
    DxvkDevice*           m_device;
    
    Rc<DxvkImage>         m_fontTexture;
    Rc<DxvkImageView>     m_fontTextureView;
    DxvkRasterizerState   m_rsState;
    DxvkBlendMode         m_blendMode;
    VkDescriptorPool      m_imguiPool;
    Rc<ImGuiAbout>        m_about;
    Rc<ImGuiSplash>       m_splash;
    Rc<ImGuiCapture>      m_capture;
    // Note: May be NULL until the font loads, needs to be checked before use.
    ImFont*               m_largeFont = nullptr;

    ImGuiContext*         m_context;
    ImPlotContext*        m_plotContext;

    HWND                  m_hwnd;
    bool                  m_init = false;

    bool                  m_windowOnRight = true;
    float                 m_windowWidth = 450.f;
    float                 m_userWindowWidth = 600.f;
    float                 m_userWindowHeight = 550.f;
    const char*           m_userGraphicsWindowTitle = "User Graphics Settings";
    bool                  m_userGraphicsSettingChanged = false;
    bool m_reflexRangesInitialized = false;
    float m_currentGameToRenderDurationMin;
    float m_currentGameToRenderDurationMax;
    float m_currentCombinedDurationMin;
    float m_currentCombinedDurationMax;
    float m_reflexLatencyStatsWindowWidth = 500.f;
    float m_reflexLatencyStatsWindowHeight = 650.f;
    bool m_reflexLatencyStatsOpen = false;
    bool m_lastRenderVsyncStatus = false;

    static constexpr char* tabNames[] = { "Rendering", "Game Setup", "Enhancements", "About" , "Dev Settings"};
    Tabs m_curTab = kTab_Count;
    Tabs m_triggerTab = kTab_Count;
    void triggerTab(const Tabs tab) {
      m_triggerTab = tab;
    }

    void update(const Rc<DxvkContext>& ctx);

    void updateQuickActions(const Rc<DxvkContext>& ctx);

    void showDebugVisualizations(const Rc<DxvkContext>& ctx);

    void showMainMenu(const Rc<DxvkContext>& ctx);

    void showUserMenu(const Rc<DxvkContext>& ctx);
    void showUserGeneralSettings(
      const Rc<DxvkContext>& ctx,
      const int subItemWidth,
      const int subItemIndent);
    void showUserRenderingSettings(
      const Rc<DxvkContext>& ctx,
      const int subItemWidth,
      const int subItemIndent);
    void showUserContentSettings(
      const Rc<DxvkContext>& ctx,
      const int subItemWidth,
      const int subItemIndent);


    void showErrorStatus(const Rc<DxvkContext>& ctx);

    void setupRendererState(
      const Rc<DxvkContext>&  ctx,
            VkSurfaceFormatKHR surfaceFormat,
            VkExtent2D        surfaceSize);

    void resetRendererState(
      const Rc<DxvkContext>&  ctx);

    void showRenderingSettings(const Rc<DxvkContext>& ctx);

    void showDLFGOptions(const Rc<DxvkContext>& ctx);
    void showReflexOptions(const Rc<DxvkContext>& ctx, bool displayStatsWindowToggle);
    void showReflexLatencyStats();

    void showSetupWindow(const Rc<DxvkContext>& ctx);

    void showMaterialOptions();

    void showEnhancementsWindow(const Rc<DxvkContext>& ctx);
    void showEnhancementsTab(const Rc<DxvkContext>& ctx);
    void showAppConfig(const Rc<DxvkContext>& ctx);

    // helper to display a configurable grid of all textures currently hooked to ImGUI
    void showTextureSelectionGrid(const Rc<DxvkContext>& ctx, const char* uniqueId, const uint32_t texturesPerRow, const float thumbnailSize, const float minChildHeight = 600.0f);

    void createFontsTexture(const Rc<DxvkContext>& ctx);

    void setupStyle(ImGuiStyle* dst = NULL);      // custom style
    void showVsyncOptions(bool enableDLFGGuard);

    void processHotkeys();

    void sendUIActivationMessage();

    void showMemoryStats() const;

    RTX_OPTION("rtx.gui", bool, showLegacyTextureGui, false, "A setting to toggle the old texture selection GUI, where each texture category is represented as its own list.");
    RTX_OPTION("rtx.gui", bool, legacyTextureGuiShowAssignedOnly, false, "A setting to show only the textures in a category that are assigned to it (Unassigned textures are found in the \"Uncategorized\" list at the bottom).");
    RTX_OPTION("rtx.gui", float, reflexStatRangeInterpolationRate, 0.05f, "A value controlling the interpolation rate applied to the Reflex stat graph ranges for smoother visualization.");
    RTX_OPTION("rtx.gui", float, reflexStatRangePaddingRatio, 0.05f, "A value specifying the amount of padding applied to the Reflex stat graph ranges as a ratio to the calculated range.");
  
  };
  
}