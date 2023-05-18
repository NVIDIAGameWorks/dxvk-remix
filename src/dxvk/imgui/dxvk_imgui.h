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

namespace dxvk {
  class ImGuiAbout;
  class ImGuiSplash;
  class DxvkDevice;
  class DxvkContext;

  /**
   * \brief DXVK ImGUI
   * 
   * GUI for manipulating settings at runtime
   */
  class ImGUI : public RcObject {
    
  public:
    
    ImGUI(const Rc<DxvkDevice>& device, const HWND& hwnd);
    
    ~ImGUI();
    
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
     */
    void render(
            const HWND hwnd,
            const Rc<DxvkContext>&  ctx,
            VkSurfaceFormatKHR surfaceFormat,
            VkExtent2D         surfaceSize);

    /**
     * \brief Creates the GUI
     *
     * Creates and initializes the GUI if the
     * \param [in] device The DXVK device
     * \param [in] hwnd The window handle
     * \returns GUI object, if it was created.
     */
    static Rc<ImGUI> createGUI(const Rc<DxvkDevice>& device, const HWND& hwnd);
    
    static void AddTexture(const XXH64_hash_t hash, const Rc<DxvkImageView>& imageView);
    static void ReleaseTexture(const XXH64_hash_t hash);
    static bool checkHotkeyState(const VirtualKeys& virtKeys);

    void switchMenu(UIType type, bool force = false);

  private:
    
    const Rc<DxvkDevice>  m_device;
    
    Rc<DxvkImage>         m_fontTexture;
    Rc<DxvkImageView>     m_fontTextureView;
    DxvkRasterizerState   m_rsState;
    DxvkBlendMode         m_blendMode;
    VkDescriptorPool      m_imguiPool;
    Rc<ImGuiAbout>        m_about;
    Rc<ImGuiSplash>       m_splash;
    // Note: May be NULL until the font loads, needs to be checked before use.
    ImFont*               m_largeFont = nullptr;

    HWND                  m_hwnd;
    bool                  m_init = false;

    bool                  m_windowOnRight = true;
    float                 m_windowWidth = 450.f;
    float                 m_userWindowWidth = 600.f;
    float                 m_userWindowHeight = 550.f;
    const char*           m_userGraphicsWindowTitle = "User Graphics Settings";
    bool                  m_userGraphicsSettingChanged = false;

    void update(const Rc<DxvkContext>& ctx);

    void updateQuickActions(const Rc<DxvkContext>& ctx);

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

    void showReflexOptions();

    void showSetupWindow(const Rc<DxvkContext>& ctx);
    void showEnhancementsWindow(const Rc<DxvkContext>& ctx);
    void showAppConfig();

    // helper to display a configurable grid of all textures currently hooked to ImGUI
    void showTextureSelectionGrid(const Rc<DxvkContext>& ctx, const char* uniqueId, const uint32_t texturesPerRow, const float thumbnailSize);

    void toggleTextureSelection(XXH64_hash_t textureHash, const char* uniqueId, std::unordered_set<XXH64_hash_t>& textureSet);

    void createFontsTexture(const Rc<DxvkContext>& ctx);

    void setupStyle(ImGuiStyle* dst = NULL);      // custom style

    void processHotkeys();

    void sendUIActivationMessage();

    void showMemoryStats() const;
  };
  
}