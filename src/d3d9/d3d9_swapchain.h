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

#include "d3d9_device_child.h"
#include "d3d9_device.h"
#include "d3d9_format.h"

#include "../dxvk/hud/dxvk_hud.h"
#include "../dxvk/imgui/dxvk_imgui.h"

#include "../dxvk/dxvk_swapchain_blitter.h"

#include "../dxvk/dxvk_swapchain_blitter.h"

#include "../util/sync/sync_signal.h"

#include <vector>

namespace dxvk {

  class D3D9Surface;

  using D3D9SwapChainExBase = D3D9DeviceChild<IDirect3DSwapChain9Ex>;
  class D3D9SwapChainEx : public D3D9SwapChainExBase {
    static constexpr uint32_t NumControlPoints = 256;
  public:

    D3D9SwapChainEx(
            D3D9DeviceEx*          pDevice,
            D3DPRESENT_PARAMETERS* pPresentParams,
      const D3DDISPLAYMODEEX*      pFullscreenDisplayMode);

    ~D3D9SwapChainEx();

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject);

    HRESULT STDMETHODCALLTYPE Present(
      const RECT*    pSourceRect,
      const RECT*    pDestRect,
            HWND     hDestWindowOverride,
      const RGNDATA* pDirtyRegion,
            DWORD    dwFlags);

    HRESULT STDMETHODCALLTYPE GetFrontBufferData(IDirect3DSurface9* pDestSurface);

    HRESULT STDMETHODCALLTYPE GetBackBuffer(
            UINT                iBackBuffer,
            D3DBACKBUFFER_TYPE  Type,
            IDirect3DSurface9** ppBackBuffer);

    HRESULT STDMETHODCALLTYPE GetRasterStatus(D3DRASTER_STATUS* pRasterStatus);

    HRESULT STDMETHODCALLTYPE GetDisplayMode(D3DDISPLAYMODE* pMode);

    HRESULT STDMETHODCALLTYPE GetPresentParameters(D3DPRESENT_PARAMETERS* pPresentationParameters);

    HRESULT STDMETHODCALLTYPE GetLastPresentCount(UINT* pLastPresentCount);

    HRESULT STDMETHODCALLTYPE GetPresentStats(D3DPRESENTSTATS* pPresentationStatistics);

    HRESULT STDMETHODCALLTYPE GetDisplayModeEx(D3DDISPLAYMODEEX* pMode, D3DDISPLAYROTATION* pRotation);

    // NV-DXVK start
   virtual HRESULT Reset(
            D3DPRESENT_PARAMETERS* pPresentParams,
            D3DDISPLAYMODEEX* pFullscreenDisplayMode,
            bool forceWindowReset = false);
    // NV-DXVK end

    HRESULT WaitForVBlank();

    void    SetGammaRamp(
            DWORD         Flags,
      const D3DGAMMARAMP* pRamp);

    void    GetGammaRamp(D3DGAMMARAMP* pRamp);

    void    Invalidate(HWND hWindow);

    HRESULT SetDialogBoxMode(bool bEnableDialogs);

    D3D9Surface* GetBackBuffer(UINT iBackBuffer);

    const D3DPRESENT_PARAMETERS* GetPresentParams() const { return &m_presentParams; }

    float GetWidthScale() const { return m_widthScale; }
    float GetHeightScale() const { return m_heightScale; }

    // NV-DXVK start: App Controlled FSE
    bool AcquireFullscreenExclusive() const {
      return GetPresenter()->acquireFullscreenExclusive() == VK_SUCCESS;
    }
    bool ReleaseFullscreenExclusive() const {
      return GetPresenter()->releaseFullscreenExclusive() == VK_SUCCESS;
    }

    void onWindowMessageEvent(UINT message, WPARAM wParam);
    // NV-DXVK end

    void SyncFrameLatency();

    Rc<DxvkDevice> getDxvkDevice() {
      return m_device;
    }

  protected:

    enum BindingIds : uint32_t {
      Image = 0,
      Gamma = 1,
    };

    
    struct WindowState {
      LONG style   = 0;
      LONG exstyle = 0;
      RECT rect    = { 0, 0, 0, 0 };
    };

    D3DPRESENT_PARAMETERS     m_presentParams;
    D3DGAMMARAMP              m_ramp;

    Rc<DxvkDevice>            m_device;
    Rc<DxvkContext>           m_context;
    Rc<DxvkSwapchainBlitter>  m_blitter;

    Rc<vk::Presenter>         m_presenter;

    // NV-DXVK begin: DLFG integration
    Rc <DxvkDLFGPresenter>    m_dlfgPresenter;
    // NV-DXVK end

    Rc<hud::Hud>              m_hud;

    std::vector<Com<D3D9Surface, false>> m_backBuffers;
    
    // NV-DXVK begin: Uninitialized read fix
    // Note: Initialized to a size of 0 to prevent uninitialized reads when these are first used.
    // Not a perfect solution (ideally an optional would be more useful to say they haven't been
    // set yet), but fine enough for the comparisons and operations involved to determine if the
    // rect has been modified or not (assuming the new rect is never 0, 0, 0, 0 like this).
    RECT                      m_srcRect{ 0, 0, 0, 0 };
    RECT                      m_dstRect{ 0, 0, 0, 0 };
    // NV-DXVK end

    DxvkSubmitStatus          m_presentStatus;

    std::vector<Rc<DxvkImageView>> m_imageViews;


    uint64_t                  m_frameId           = D3D9DeviceEx::MaxFrameLatency;
    uint32_t                  m_frameLatencyCap   = 0;
    Rc<sync::Fence>           m_frameLatencySignal;

    bool                      m_dirty    = true;
    bool                      m_vsync    = true;

    bool                      m_dialog;
    bool                      m_lastDialog = false;

    HWND                      m_window   = nullptr;
    HMONITOR                  m_monitor  = nullptr;

    WindowState               m_windowState;

    uint32_t                  m_originalWidth;
    uint32_t                  m_originalHeight;

    float                     m_widthScale = 1, m_heightScale = 1;

    double                    m_displayRefreshRate = 0.0;

    void PresentImage(UINT PresentInterval);
    
    void SubmitPresent(const vk::PresenterSync& Sync, uint32_t FrameId, uint32_t imageIndex);

    void SynchronizePresent();

    void RecreateSwapChain(
        BOOL                      Vsync);

    void CreatePresenter();

    void CreateRenderTargetViews();

    void DestroyBackBuffers();

    void CreateBackBuffers(
            uint32_t            NumBackBuffers);

    virtual int NumFrontBuffers();

    void CreateBlitter();

    void CreateHud();

    void InitRamp();

    uint32_t GetActualFrameLatency();

    uint32_t PickFormats(
            D3D9Format                Format,
            VkSurfaceFormatKHR*       pDstFormats);
    
    uint32_t PickPresentModes(
            BOOL                      Vsync,
            VkPresentModeKHR*         pDstModes);
    
    uint32_t PickImageCount(
            UINT                      Preferred);

    void NormalizePresentParameters(D3DPRESENT_PARAMETERS* pPresentParams);

    void NotifyDisplayRefreshRate(
            double                  RefreshRate);

    HRESULT EnterFullscreenMode(
            D3DPRESENT_PARAMETERS*  pPresentParams,
      const D3DDISPLAYMODEEX*       pFullscreenDisplayMode);
    
    HRESULT LeaveFullscreenMode();
    
    HRESULT ChangeDisplayMode(
            D3DPRESENT_PARAMETERS*  pPresentParams,
      const D3DDISPLAYMODEEX*       pFullscreenDisplayMode);
    
    HRESULT RestoreDisplayMode(HMONITOR hMonitor);

    bool    UpdatePresentRegion(const RECT* pSourceRect, const RECT* pDestRect);

    VkExtent2D GetPresentExtent();

    VkFullScreenExclusiveEXT PickFullscreenMode();

    std::string GetApiName();

    // NV-DXVK start: DLFG integration
    bool NeedRecreatePresenter();
    vk::Presenter* GetPresenter() const;
    // NV-DXVK end
  };

  class D3D9SwapchainExternal final : public D3D9SwapChainEx {
    Rc<RtxSemaphore> m_frameEndSemaphore;
    Rc<RtxSemaphore> m_frameResumeSemaphore;

  protected:
    int NumFrontBuffers() override { return 0; };

  public:

    D3D9SwapchainExternal(
            D3D9DeviceEx* pDevice,
            D3DPRESENT_PARAMETERS* pPresentParams,
      const D3DDISPLAYMODEEX* pFullscreenDisplayMode);

    HRESULT Reset(
            D3DPRESENT_PARAMETERS* pPresentParams,
            D3DDISPLAYMODEEX* pFullscreenDisplayMode,
            bool forceWindowReset = false) override;

    HRESULT STDMETHODCALLTYPE Present(
      const RECT* pSourceRect,
      const RECT* pDestRect,
            HWND     hDestWindowOverride,
      const RGNDATA* pDirtyRegion,
            DWORD    dwFlags) override;

// NV-DXVK start: external API
    VkImage GetVkImage(uint32_t Index) const {
      D3D9DeviceLock lock = m_parent->LockDevice();
      if (Index < m_backBuffers.size()) {
        return m_backBuffers[Index]->GetCommonTexture()->GetImage()->handle();
      }
      return VK_NULL_HANDLE;
    }

    VkSemaphore GetFrameResumeVkSemaphore() const {
      return m_frameEndSemaphore->handle();
    }

    VkSemaphore GetFrameCompleteVkSemaphore() const {
      return m_frameResumeSemaphore->handle();
    }
// NV-DXVK end
  };

}