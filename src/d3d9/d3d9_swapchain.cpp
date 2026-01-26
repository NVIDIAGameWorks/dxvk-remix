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
#include "d3d9_swapchain.h"
#include "d3d9_surface.h"
#include "d3d9_monitor.h"
#include "d3d9_hud.h"

#include "../dxvk/dxvk_device.h"
#include "../dxvk/dxvk_objects.h"
#include "../util/util_env.h"
#include "../util/util_once.h"
#include "../util/util_string.h"
#include "../dxvk/rtx_render/rtx_bridge_message_channel.h"
#include "../dxvk/dxvk_scoped_annotation.h"

// NV-DXVK start: DLFG integration
#include "../dxvk/rtx_render/rtx_dlfg.h"
// NV-DXVK end

namespace dxvk {
  // NV-DXVK start: App Controlled FSE
  enum FSEState {
    Acquire = 0,
    Release,
    Unchanged
  };

  static FSEState ProcessFullscreenExclusiveMessages(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    // Only required in bridge mode!
    assert(env::isRemixBridgeActive());

    FSEState state = FSEState::Unchanged;

    switch (message) {
    case WM_ACTIVATEAPP:
    {
      if (wParam) {
        Logger::debug("FSE State (Acquire): ACTIVATEAPP = TRUE");
        state = FSEState::Acquire;
      } else {
        Logger::debug("FSE State (Release): ACTIVATEAPP = FALSE");
        state = FSEState::Release;
      }
      break;
    }
    case WM_WINDOWPOSCHANGING:
    case WM_WINDOWPOSCHANGED:
    {
      WINDOWPOS* windowPos = (WINDOWPOS*) lParam;
      if (0 == (windowPos->flags & SWP_NOZORDER)) {
        HWND prev = GetWindow(windowPos->hwnd, GW_HWNDPREV);
        bool zorderChanged = (prev != windowPos->hwndInsertAfter);
        if (zorderChanged) {
          Logger::debug("FSE State (Release): zorderChanged");
          state = FSEState::Release;
        }
      }
      break;
    }
    case WM_EXITMENULOOP:
    case WM_SETFOCUS:
    {
      Logger::debug("FSE State (Acquire): EXITMENULOOP/SETFOCUS");
      state = FSEState::Acquire;
      break;
    }
    case WM_ENTERMENULOOP:
    case WM_NCDESTROY:
    case WM_KILLFOCUS:
    {
      Logger::debug("FSE State (Release): ENTERMENULOOP/KILLFOCUS/NCDESTROY");
      state = FSEState::Release;
      break;
    }
    }

    return state;
  }
  // NV-DXVK end

  struct D3D9WindowData {
    bool unicode;
    bool filter;
    WNDPROC proc;
    D3D9SwapChainEx* swapchain;
  };


  static dxvk::recursive_mutex g_windowProcMapMutex;
  static std::unordered_map<HWND, D3D9WindowData> g_windowProcMap;


  template <typename T, typename J, typename ... Args>
  auto CallCharsetFunction(T unicode, J ascii, bool isUnicode, Args... args) {
    return isUnicode
      ? unicode(args...)
      : ascii  (args...);
  }


  class D3D9WindowMessageFilter {

  public:

    D3D9WindowMessageFilter(HWND window, bool filter = true)
      : m_window(window) {
      std::lock_guard lock(g_windowProcMapMutex);
      auto it = g_windowProcMap.find(m_window);
      m_filter = std::exchange(it->second.filter, filter);
    }

    ~D3D9WindowMessageFilter() {
      std::lock_guard lock(g_windowProcMapMutex);
      auto it = g_windowProcMap.find(m_window);
      it->second.filter = m_filter;
    }

    D3D9WindowMessageFilter             (const D3D9WindowMessageFilter&) = delete;
    D3D9WindowMessageFilter& operator = (const D3D9WindowMessageFilter&) = delete;

  private:

    HWND m_window;
    bool m_filter;

  };


  LRESULT CALLBACK D3D9WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam);


  void ResetWindowProc(HWND window) {
    std::lock_guard lock(g_windowProcMapMutex);

    auto it = g_windowProcMap.find(window);
    if (it == g_windowProcMap.end())
      return;

    auto proc = reinterpret_cast<WNDPROC>(
      CallCharsetFunction(
      GetWindowLongPtrW, GetWindowLongPtrA, it->second.unicode,
        window, GWLP_WNDPROC));


    if (proc == D3D9WindowProc)
      CallCharsetFunction(
        SetWindowLongPtrW, SetWindowLongPtrA, it->second.unicode,
          window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(it->second.proc));

    g_windowProcMap.erase(window);
  }


  void D3D9SwapChainEx::HookWindowProc(HWND window) {
    std::lock_guard lock(g_windowProcMapMutex);

    ResetWindowProc(window);

    D3D9WindowData windowData;
    windowData.unicode = IsWindowUnicode(window);
    windowData.filter  = false;
    windowData.proc = reinterpret_cast<WNDPROC>(
      CallCharsetFunction(
      SetWindowLongPtrW, SetWindowLongPtrA, windowData.unicode,
        window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(D3D9WindowProc)));
    windowData.swapchain = this;
    g_windowProcMap[window] = std::move(windowData);

    // NV-DXVK start: App Controlled FSE
    if (windowData.proc == nullptr) {
      Logger::info(str::format("No winproc detected, initiating bridge message channel for: ", getWinProcHwnd()));

      if (BridgeMessageChannel::get().init(getWinProcHwnd(), D3D9WindowProc)) {
        // Send the initial state messages
        auto& gui = getDxvkDevice()->getCommon()->getImgui();
        gui.switchMenu(RtxOptions::showUI(), true);
      } else {
        Logger::err("Unable to init bridge message channel. FSE and input capture may not work!");
      }
    }
    // NV-DXVK end
  }

  LRESULT CALLBACK D3D9WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    D3D9WindowData windowData = {};

    {
      std::lock_guard lock(g_windowProcMapMutex);

      auto it = g_windowProcMap.find(window);
      if (it != g_windowProcMap.end())
        windowData = it->second;
      else
        return 0;
    }

    bool unicode = windowData.proc
      ? windowData.unicode
      : IsWindowUnicode(window);

    // NV-DXVK start: Handling stale Swapchains.
    // Majority of NV-DXVK changes below are related to bSkipSwapchainActions

    // Swapchain may be publicly dead, but is kept internally alive for some reason,
    // so it wasn't removed from g_windowProcMap. Attempting to reference windowData.swapchain
    // may result in referencing invalidated handles/values.
    windowData.swapchain->AddRef();
    const auto swapchainRefCnt = windowData.swapchain->Release();
    const bool bSkipSwapchainActions = (swapchainRefCnt == 0);
    if(bSkipSwapchainActions) {
      ONCE(Logger::warn("[D3D9WindowProc] Swapchain handle is invalid, some of its values may not be correct."));
    }

    // It is potentially unsafe to access the swapchain in this function and may result in
    // bad params given the above; however, this is currently dependent behavior. The param
    // valuesbelow *should* stay consistent, but it's entirely possible that this thread yields
    // at some point OR another thread invalidates via DTOR the parameter data below. The
    // best we can do for now is cache the param data ASAP.
    D3DDEVICE_CREATION_PARAMETERS create_parms; D3DPRESENT_PARAMETERS present_parms;
    windowData.swapchain->GetDevice()->GetCreationParameters(&create_parms);
    windowData.swapchain->GetPresentParameters(&present_parms);
    
    if (!present_parms.Windowed && !(message == WM_NCCALCSIZE && wParam == TRUE)) {
      if (message == WM_DESTROY)
        ResetWindowProc(window);
      else if (message == WM_ACTIVATEAPP) {

        if (!(create_parms.BehaviorFlags & D3DCREATE_NOWINDOWCHANGES)) {
          if (wParam) {
            // Heroes of Might and Magic V needs this to resume drawing after a focus loss
            RECT rect;
            GetMonitorRect(GetDefaultMonitor(), &rect);
            SetWindowPos(window, nullptr, rect.left, rect.top, present_parms.BackBufferWidth, present_parms.BackBufferHeight,
              SWP_NOACTIVATE | SWP_NOZORDER | SWP_ASYNCWINDOWPOS);
          }
          else {
            if (IsWindowVisible(window))
              ShowWindow(window, SW_MINIMIZE);
          }
        }
      }
    }
    else if (message == WM_SIZE)
    {
      if (!(create_parms.BehaviorFlags & D3DCREATE_NOWINDOWCHANGES) && !IsIconic(window))
        PostMessageW(window, WM_ACTIVATEAPP, 1, GetCurrentThreadId());
    }

    // Safe from bSkipSwapchainActions as we're just getting a handle that shouldn't
    // be invalidated
    auto& gui = windowData.swapchain->getDxvkDevice()->getCommon()->getImgui();
    if(gui.isInit()) {
      gui.wndProcHandler(window, message, wParam, lParam);
    }

    if(!bSkipSwapchainActions) {
      if (!present_parms.Windowed && env::isRemixBridgeActive()) {
        FSEState state = ProcessFullscreenExclusiveMessages(window, message, wParam, lParam);

        // Update FSE state
        if (state == FSEState::Acquire) {
          windowData.swapchain->AcquireFullscreenExclusive();
        } else if (state == FSEState::Release) {
          windowData.swapchain->ReleaseFullscreenExclusive();
        }
      }
    }

    if (windowData.proc) {
      return CallCharsetFunction(
        CallWindowProcW, CallWindowProcA, unicode,
          windowData.proc, window, message, wParam, lParam);
    }

    if(!bSkipSwapchainActions) {
      windowData.swapchain->onWindowMessageEvent(message, wParam);
    }

    // NV-DXVK end

    return 0;
  }


  static uint16_t MapGammaControlPoint(float x) {
    if (x < 0.0f) x = 0.0f;
    if (x > 1.0f) x = 1.0f;
    return uint16_t(65535.0f * x);
  }


  struct D3D9PresentInfo {
    float scale[2];
    float offset[2];
  };


  D3D9SwapChainEx::D3D9SwapChainEx(
          D3D9DeviceEx*          pDevice,
          D3DPRESENT_PARAMETERS* pPresentParams,
    const D3DDISPLAYMODEEX*      pFullscreenDisplayMode)
    : D3D9SwapChainExBase(pDevice)
    , m_device           (pDevice->GetDXVKDevice())
    , m_context          (m_device->createContext())
    , m_frameLatencyCap  (pDevice->GetOptions()->maxFrameLatency)
    , m_frameLatencySignal(new sync::Fence(m_frameId))
    , m_dialog            (pDevice->GetOptions()->enableDialogMode)
    , m_originalWidth(pPresentParams->BackBufferWidth)
    , m_originalHeight(pPresentParams->BackBufferHeight) {
    this->NormalizePresentParameters(pPresentParams);
    m_presentParams = *pPresentParams;
    m_window = m_presentParams.hDeviceWindow;

    // NV-DXVK start: DLFG integration
    if (RtxOptions::enableVsync() == EnableVsync::WaitingForImplicitSwapchain) {
      // save the vsync state when the first swapchain is created, to act as the default
      RtxOptions::enableVsyncState = m_presentParams.PresentationInterval ? EnableVsync::On : EnableVsync::Off;
    }
    // NV-DXVK end

    UpdatePresentRegion(nullptr, nullptr);
    
    if (m_window) {
      CreatePresenter();

      if (!pDevice->GetOptions()->deferSurfaceCreation)
        RecreateSwapChain(m_vsync);
    }

    CreateBackBuffers(m_presentParams.BackBufferCount);
    CreateBlitter();
    CreateHud();

    InitRamp();

    // Apply initial window mode and fullscreen state
    const bool modifyWindow = !(m_parent->m_behaviorFlags & D3DCREATE_NOWINDOWCHANGES);
    if (!m_presentParams.Windowed && (modifyWindow && FAILED(EnterFullscreenMode(pPresentParams, pFullscreenDisplayMode)))) {
      throw DxvkError("D3D9: Failed to set initial fullscreen state");
    } else {
      HookWindowProc(m_window);
    }
  }


  D3D9SwapChainEx::~D3D9SwapChainEx() {
    DestroyBackBuffers();

    ResetWindowProc(m_window);
    RestoreDisplayMode(m_monitor);

    m_device->waitForSubmission(&m_presentStatus);

    // NV-DXVK start: DLFG integration
    if (m_dlfgPresenter != nullptr) {
      // if the DLFG presenter is running, idle it before waitForIdle, otherwise we'll violate synchronization requirements on VkQueue
      m_dlfgPresenter->synchronize();
    }
    // NV-DXVK end

    m_device->waitForIdle();
  }


  HRESULT STDMETHODCALLTYPE D3D9SwapChainEx::QueryInterface(REFIID riid, void** ppvObject) {
    if (ppvObject == nullptr)
      return E_POINTER;

    *ppvObject = nullptr;

    if (riid == __uuidof(IUnknown)
     || riid == __uuidof(IDirect3DSwapChain9)
     || (GetParent()->IsExtended() && riid == __uuidof(IDirect3DSwapChain9Ex))) {
      *ppvObject = ref(this);
      return S_OK;
    }

    Logger::warn("D3D9SwapChainEx::QueryInterface: Unknown interface query");
    Logger::warn(str::format(riid));
    return E_NOINTERFACE;
  }


  // NV-DXVK start: DLFG integration
  bool D3D9SwapChainEx::NeedRecreatePresenter() {
    if (m_context->isDLFGEnabled()) {
      if (m_dlfgPresenter == nullptr) {
        return true;
      }
    } else {
      if (m_presenter == nullptr) {
        return true;
      }
    }

    // one must be null, one must be non-null
    assert(m_presenter != nullptr || m_dlfgPresenter != nullptr);
    assert(m_presenter == nullptr || m_dlfgPresenter == nullptr);
    return false;
  }

  vk::Presenter* D3D9SwapChainEx::GetPresenter() const {
    const auto presenter = m_presenter != nullptr ? m_presenter.ptr() : m_dlfgPresenter.ptr();

    // Note: The returned presenter must be non-null as one of the two presenters must be non-null at all times,
    // and because code will blindly dereference this returned pointer.
    assert(presenter != nullptr);

    return presenter;
  }

  HRESULT STDMETHODCALLTYPE D3D9SwapChainEx::Present(
    const RECT*    pSourceRect,
    const RECT*    pDestRect,
          HWND     hDestWindowOverride,
    const RGNDATA* pDirtyRegion,
          DWORD    dwFlags) {
    ScopedCpuProfileZone();

    // NV-DXVK start: Restart RTX capture on the new frame
    m_parent->m_rtx.EndFrame(m_backBuffers[0]->GetCommonTexture()->GetImage());
    // NV-DXVK end

    D3D9DeviceLock lock = m_parent->LockDevice();

    uint32_t presentInterval = m_presentParams.PresentationInterval;

    // This is not true directly in d3d9 to to timing differences that don't matter for us.
    // For our purposes...
    // D3DPRESENT_INTERVAL_DEFAULT (0) == D3DPRESENT_INTERVAL_ONE (1) which means VSYNC.
    presentInterval = std::max(presentInterval, 1u);

    if (presentInterval == D3DPRESENT_INTERVAL_IMMEDIATE || (dwFlags & D3DPRESENT_FORCEIMMEDIATE))
      presentInterval = 0;

    auto options = m_parent->GetOptions();

    if (options->presentInterval >= 0)
      presentInterval = options->presentInterval;

    // NV-DXVK start: Reflex integration
    switch (RtxOptions::enableVsyncState) {
    case EnableVsync::Off:
      presentInterval = 0;
      break;

    case EnableVsync::On:
      presentInterval = 1;
      break;

    default:
      // this should never happen
      assert(!"invalid vsync enable state");
      break;
    }
    // NV-DXVK end

    bool vsync  = presentInterval != 0;

    HWND window = m_presentParams.hDeviceWindow;
    if (hDestWindowOverride != nullptr)
      window    = hDestWindowOverride;

    bool recreate = false;
    // NV-DXVK start: DLFG integration
    recreate    = NeedRecreatePresenter();
    // NV-DXVK end
    recreate   |= window != m_window;    
    recreate   |= m_dialog != m_lastDialog;

    // NV-DXVK start: Support games changing the HWND at runtime.
    if (window != m_window) {
      // Reinstall window hook that was removed in LeaveFullscreenMode() above
      HookWindowProc(window);
    }
    // NV-DXVK end

    m_window    = window;

    m_dirty    |= vsync != m_vsync;
    m_dirty    |= UpdatePresentRegion(pSourceRect, pDestRect);
    m_dirty    |= recreate;
    // NV-DXVK start: DLFG integration
    m_dirty    |= GetPresenter() != nullptr &&
                  !GetPresenter()->hasSwapChain();
    // NV-DXVK end

    m_vsync     = vsync;

    m_lastDialog = m_dialog;

    try {
      if (recreate)
        CreatePresenter();

      if (std::exchange(m_dirty, false))
        RecreateSwapChain(vsync);

      // We aren't going to device loss simply because
      // 99% of D3D9 games don't handle this properly and
      // just end up crashing (like with alt-tab loss)
      
      // NV-DXVK start: DLFG integration
      if (!GetPresenter()->hasSwapChain())
      // NV-DXVK end
        return D3D_OK;

      PresentImage(presentInterval);
      return D3D_OK;
    } catch (const DxvkError& e) {
      Logger::err(e.message());
      return D3DERR_DEVICEREMOVED;
    }
  }


  HRESULT STDMETHODCALLTYPE D3D9SwapChainEx::GetFrontBufferData(IDirect3DSurface9* pDestSurface) {
    D3D9DeviceLock lock = m_parent->LockDevice();

    // This function can do absolutely everything!
    // Copies the front buffer between formats with an implicit resolve.
    // Oh, and the dest is systemmem...
    // This is a slow function anyway, it waits for the copy to finish.
    // so there's no reason to not just make and throwaway temp images.

    // If extent of dst > src, then we blit to a subrect of the size
    // of src onto a temp image of dst's extents,
    // then copy buffer back to dst (given dst is subresource)

    D3D9Surface* dst = static_cast<D3D9Surface*>(pDestSurface);

    if (unlikely(dst == nullptr))
      return D3DERR_INVALIDCALL;

    D3D9CommonTexture* dstTexInfo = dst->GetCommonTexture();
    D3D9CommonTexture* srcTexInfo = m_backBuffers.back()->GetCommonTexture();

    if (unlikely(dstTexInfo->Desc()->Pool != D3DPOOL_SYSTEMMEM))
      return D3DERR_INVALIDCALL;

    Rc<DxvkBuffer> dstBuffer = dstTexInfo->GetBuffer(dst->GetSubresource());
    Rc<DxvkImage>  srcImage  = srcTexInfo->GetImage();

    if (srcImage->info().sampleCount != VK_SAMPLE_COUNT_1_BIT) {
      DxvkImageCreateInfo resolveInfo;
      resolveInfo.type          = VK_IMAGE_TYPE_2D;
      resolveInfo.format        = srcImage->info().format;
      resolveInfo.flags         = 0;
      resolveInfo.sampleCount   = VK_SAMPLE_COUNT_1_BIT;
      resolveInfo.extent        = srcImage->info().extent;
      resolveInfo.numLayers     = 1;
      resolveInfo.mipLevels     = 1;
      resolveInfo.usage         = VK_IMAGE_USAGE_SAMPLED_BIT
                                | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT
                                | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
      resolveInfo.stages        = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
                                | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
                                | VK_PIPELINE_STAGE_TRANSFER_BIT;
      resolveInfo.access        = VK_ACCESS_SHADER_READ_BIT
                                | VK_ACCESS_TRANSFER_WRITE_BIT
                                | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT
                                | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
      resolveInfo.tiling        = VK_IMAGE_TILING_OPTIMAL;
      resolveInfo.layout        = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

      // NV-DXVK start: add debug names to VkImage objects
      Rc<DxvkImage> resolvedSrc = m_device->createImage(
        resolveInfo, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, DxvkMemoryStats::Category::AppTexture, "GetFrontBufferData resolved src");
      // NV-DXVK end

      m_parent->EmitCs([
        cDstImage = resolvedSrc,
        cSrcImage = srcImage
      ] (DxvkContext* ctx) {
        VkImageSubresourceLayers resolveSubresource;
        resolveSubresource.aspectMask      = VK_IMAGE_ASPECT_COLOR_BIT;
        resolveSubresource.mipLevel        = 0;
        resolveSubresource.baseArrayLayer  = 0;
        resolveSubresource.layerCount      = 1;

        VkImageResolve resolveRegion;
        resolveRegion.srcSubresource = resolveSubresource;
        resolveRegion.srcOffset      = VkOffset3D { 0, 0, 0 };
        resolveRegion.dstSubresource = resolveSubresource;
        resolveRegion.dstOffset      = VkOffset3D { 0, 0, 0 };
        resolveRegion.extent         = cSrcImage->info().extent;

        ctx->resolveImage(
          cDstImage, cSrcImage,
          resolveRegion, VK_FORMAT_UNDEFINED);
      });

      srcImage = std::move(resolvedSrc);
    }

    D3D9Format srcFormat = srcTexInfo->Desc()->Format;
    D3D9Format dstFormat = dstTexInfo->Desc()->Format;

    bool similar = AreFormatsSimilar(srcFormat, dstFormat);

    if (!similar || srcImage->info().extent != dstTexInfo->GetExtent()) {
      DxvkImageCreateInfo blitCreateInfo;
      blitCreateInfo.type          = VK_IMAGE_TYPE_2D;
      blitCreateInfo.format        = dstTexInfo->GetFormatMapping().FormatColor;
      blitCreateInfo.flags         = 0;
      blitCreateInfo.sampleCount   = VK_SAMPLE_COUNT_1_BIT;
      blitCreateInfo.extent        = dstTexInfo->GetExtent();
      blitCreateInfo.numLayers     = 1;
      blitCreateInfo.mipLevels     = 1;
      blitCreateInfo.usage         = VK_IMAGE_USAGE_SAMPLED_BIT
                                   | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT
                                   | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
      blitCreateInfo.stages        = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
                                   | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
                                   | VK_PIPELINE_STAGE_TRANSFER_BIT;
      blitCreateInfo.access        = VK_ACCESS_SHADER_READ_BIT
                                   | VK_ACCESS_TRANSFER_WRITE_BIT
                                   | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT
                                   | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
      blitCreateInfo.tiling        = VK_IMAGE_TILING_OPTIMAL;
      blitCreateInfo.layout        = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

      // NV-DXVK start: add debug names to VkImage objects
      Rc<DxvkImage> blittedSrc = m_device->createImage(
        blitCreateInfo, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, DxvkMemoryStats::Category::AppTexture, "GetFrontBufferData blit src");
      // NV-DXVK end

      const DxvkFormatInfo* dstFormatInfo = imageFormatInfo(blittedSrc->info().format);
      const DxvkFormatInfo* srcFormatInfo = imageFormatInfo(srcImage->info().format);

      const VkImageSubresource dstSubresource = dstTexInfo->GetSubresourceFromIndex(dstFormatInfo->aspectMask, 0);
      const VkImageSubresource srcSubresource = srcTexInfo->GetSubresourceFromIndex(srcFormatInfo->aspectMask, 0);

      VkImageSubresourceLayers dstSubresourceLayers = {
        dstSubresource.aspectMask,
        dstSubresource.mipLevel,
        dstSubresource.arrayLayer, 1 };

      VkImageSubresourceLayers srcSubresourceLayers = {
        srcSubresource.aspectMask,
        srcSubresource.mipLevel,
        srcSubresource.arrayLayer, 1 };

      VkExtent3D srcExtent = srcImage->mipLevelExtent(srcSubresource.mipLevel);

      // Blit to a subrect of the src extents
      VkImageBlit blitInfo;
      blitInfo.dstSubresource = dstSubresourceLayers;
      blitInfo.srcSubresource = srcSubresourceLayers;
      blitInfo.dstOffsets[0] = VkOffset3D{ 0, 0, 0 };
      blitInfo.dstOffsets[1] = VkOffset3D{ int32_t(srcExtent.width),  int32_t(srcExtent.height),  1 };
      blitInfo.srcOffsets[0] = VkOffset3D{ 0, 0, 0 };
      blitInfo.srcOffsets[1] = VkOffset3D{ int32_t(srcExtent.width),  int32_t(srcExtent.height),  1 };

      m_parent->EmitCs([
        cDstImage = blittedSrc,
        cDstMap   = dstTexInfo->GetMapping().Swizzle,
        cSrcImage = srcImage,
        cSrcMap   = srcTexInfo->GetMapping().Swizzle,
        cBlitInfo = blitInfo
      ] (DxvkContext* ctx) {
        ctx->blitImage(
          cDstImage, cDstMap,
          cSrcImage, cSrcMap,
          cBlitInfo, VK_FILTER_NEAREST);
      });

      srcImage = std::move(blittedSrc);
    }

    const DxvkFormatInfo* srcFormatInfo = imageFormatInfo(srcImage->info().format);
    const VkImageSubresource srcSubresource = srcTexInfo->GetSubresourceFromIndex(srcFormatInfo->aspectMask, 0);
    VkImageSubresourceLayers srcSubresourceLayers = {
      srcSubresource.aspectMask,
      srcSubresource.mipLevel,
      srcSubresource.arrayLayer, 1 };
    VkExtent3D srcExtent = srcImage->mipLevelExtent(srcSubresource.mipLevel);

    m_parent->EmitCs([
      cBuffer       = dstBuffer,
      cImage        = srcImage,
      cSubresources = srcSubresourceLayers,
      cLevelExtent  = srcExtent
    ] (DxvkContext* ctx) {
      ctx->copyImageToBuffer(cBuffer, 0, 4, 0,
        cImage, cSubresources, VkOffset3D { 0, 0, 0 },
        cLevelExtent);
    });
    
    dstTexInfo->SetWrittenByGPU(dst->GetSubresource(), true);

    return D3D_OK;
  }


  HRESULT STDMETHODCALLTYPE D3D9SwapChainEx::GetBackBuffer(
          UINT                iBackBuffer,
          D3DBACKBUFFER_TYPE  Type,
          IDirect3DSurface9** ppBackBuffer) {
    // Could be doing a device reset...
    D3D9DeviceLock lock = m_parent->LockDevice();

    if (unlikely(ppBackBuffer == nullptr))
      return D3DERR_INVALIDCALL;

    if (unlikely(iBackBuffer >= m_presentParams.BackBufferCount)) {
      Logger::err(str::format("D3D9: GetBackBuffer: Invalid back buffer index: ", iBackBuffer));
      return D3DERR_INVALIDCALL;
    }

    *ppBackBuffer = ref(m_backBuffers[iBackBuffer].ptr());
    return D3D_OK;
  }


  HRESULT STDMETHODCALLTYPE D3D9SwapChainEx::GetRasterStatus(D3DRASTER_STATUS* pRasterStatus) {
    // We could use D3DKMTGetScanLine but Wine doesn't implement that.
    // So... we lie here and make some stuff up
    // enough that it makes games work.

    // Assume there's 20 lines in a vBlank.
    constexpr uint32_t vBlankLineCount = 20;

    if (pRasterStatus == nullptr)
      return D3DERR_INVALIDCALL;

    D3DDISPLAYMODEEX mode;
    mode.Size = sizeof(mode);
    if (FAILED(this->GetDisplayModeEx(&mode, nullptr)))
      return D3DERR_INVALIDCALL;

    uint32_t scanLineCount = mode.Height + vBlankLineCount;

    auto nowUs = std::chrono::time_point_cast<std::chrono::microseconds>(
      dxvk::high_resolution_clock::now())
      .time_since_epoch();

    auto frametimeUs = std::chrono::microseconds(1000000u / mode.RefreshRate);
    auto scanLineUs  = frametimeUs / scanLineCount;

    pRasterStatus->ScanLine = (nowUs % frametimeUs) / scanLineUs;
    pRasterStatus->InVBlank = pRasterStatus->ScanLine >= mode.Height;

    if (pRasterStatus->InVBlank)
      pRasterStatus->ScanLine = 0;

    return D3D_OK;
  }

  
  HRESULT STDMETHODCALLTYPE D3D9SwapChainEx::GetDisplayMode(D3DDISPLAYMODE* pMode) {
    if (pMode == nullptr)
      return D3DERR_INVALIDCALL;

    *pMode = D3DDISPLAYMODE();

    D3DDISPLAYMODEEX mode;
    mode.Size = sizeof(mode);
    HRESULT hr = this->GetDisplayModeEx(&mode, nullptr);

    if (FAILED(hr))
      return hr;

    pMode->Width       = mode.Width;
    pMode->Height      = mode.Height;
    pMode->Format      = mode.Format;
    pMode->RefreshRate = mode.RefreshRate;

    return D3D_OK;
  }


  HRESULT STDMETHODCALLTYPE D3D9SwapChainEx::GetPresentParameters(D3DPRESENT_PARAMETERS* pPresentationParameters) {
    if (pPresentationParameters == nullptr)
      return D3DERR_INVALIDCALL;

    *pPresentationParameters = m_presentParams;

    return D3D_OK;
  }


  HRESULT STDMETHODCALLTYPE D3D9SwapChainEx::GetLastPresentCount(UINT* pLastPresentCount) {
    Logger::warn("D3D9SwapChainEx::GetLastPresentCount: Stub");
    return D3D_OK;
  }


  HRESULT STDMETHODCALLTYPE D3D9SwapChainEx::GetPresentStats(D3DPRESENTSTATS* pPresentationStatistics) {
    Logger::warn("D3D9SwapChainEx::GetPresentStats: Stub");
    return D3D_OK;
  }


  HRESULT STDMETHODCALLTYPE D3D9SwapChainEx::GetDisplayModeEx(D3DDISPLAYMODEEX* pMode, D3DDISPLAYROTATION* pRotation) {
    if (pMode == nullptr && pRotation == nullptr)
      return D3DERR_INVALIDCALL;

    if (pRotation != nullptr)
      *pRotation = D3DDISPLAYROTATION_IDENTITY;

    if (pMode != nullptr) {
      DEVMODEW devMode = DEVMODEW();
      devMode.dmSize = sizeof(devMode);

      if (!GetMonitorDisplayMode(GetDefaultMonitor(), ENUM_CURRENT_SETTINGS, &devMode)) {
        Logger::err("D3D9SwapChainEx::GetDisplayModeEx: Failed to enum display settings");
        return D3DERR_INVALIDCALL;
      }

      pMode->Size             = sizeof(D3DDISPLAYMODEEX);
      pMode->Width            = devMode.dmPelsWidth;
      pMode->Height           = devMode.dmPelsHeight;
      pMode->RefreshRate      = devMode.dmDisplayFrequency;
      pMode->Format           = D3DFMT_X8R8G8B8;
      pMode->ScanLineOrdering = D3DSCANLINEORDERING_PROGRESSIVE;
    }

    return D3D_OK;
  }

  HRESULT D3D9SwapchainExternal::Reset(D3DPRESENT_PARAMETERS* pPresentParams, D3DDISPLAYMODEEX* pFullscreenDisplayMode, bool forceWindowReset) {
    D3D9DeviceLock lock = m_parent->LockDevice();

    this->SynchronizePresent();
    this->NormalizePresentParameters(pPresentParams);

    if (pPresentParams->hDeviceWindow != nullptr && m_window != pPresentParams->hDeviceWindow) {
      ResetWindowProc(m_window);
      m_window = m_parent->m_window = pPresentParams->hDeviceWindow;
      HookWindowProc(m_window);
    }

    m_presentParams = *pPresentParams;

    CreateBackBuffers(m_presentParams.BackBufferCount);

    return S_OK;
  }

  HRESULT D3D9SwapChainEx::Reset(
          D3DPRESENT_PARAMETERS* pPresentParams,
          D3DDISPLAYMODEEX*      pFullscreenDisplayMode,
          bool forceWindowReset) {
    D3D9DeviceLock lock = m_parent->LockDevice();

    this->SynchronizePresent();
    this->NormalizePresentParameters(pPresentParams);

    m_dirty    |= m_presentParams.BackBufferFormat   != pPresentParams->BackBufferFormat
               || m_presentParams.BackBufferCount    != pPresentParams->BackBufferCount;

    const bool modifyWindow = !(m_parent->m_behaviorFlags & D3DCREATE_NOWINDOWCHANGES);

    bool changeFullscreen = m_presentParams.Windowed != pPresentParams->Windowed;

    if (pPresentParams->Windowed) {
      if (modifyWindow && changeFullscreen)
        this->LeaveFullscreenMode();

      // NV-DXVK start: Adjust window pos only on backbuffer resolution changes to minimize
      // negative effects from such adjustments.
      //
      // NOTE: Unfortunately there's no reliable way in Windows to determine 
      // the window rect size for a given client area rect size. Every method including
      // user32 AdjustWindowRectEx() and DWM may give incorrect client rect sizes for custom
      // window styles (like the one used in Portal) which may result in swapchain recreation
      // with a wrong extent at first Present() where the swapchain will be deemed dirty when
      // the present extent != adjusted window client extent even though there was no intent
      // from d3d client to actually change the present extent.
      //
      if (modifyWindow && (forceWindowReset ||
          pPresentParams->BackBufferWidth != m_presentParams.BackBufferWidth ||
          pPresentParams->BackBufferHeight != m_presentParams.BackBufferHeight)) {
        // Adjust window position and size
        RECT newRect = { 0, 0, 0, 0 };
        RECT oldRect = { 0, 0, 0, 0 };
      
        ::GetWindowRect(m_window, &oldRect);
        ::MapWindowPoints(HWND_DESKTOP, ::GetParent(m_window), reinterpret_cast<POINT*>(&oldRect), 1);
        ::SetRect(&newRect, 0, 0, pPresentParams->BackBufferWidth, pPresentParams->BackBufferHeight);
        ::AdjustWindowRectEx(&newRect,
          ::GetWindowLongW(m_window, GWL_STYLE), FALSE,
          ::GetWindowLongW(m_window, GWL_EXSTYLE));
        ::SetRect(&newRect, 0, 0, newRect.right - newRect.left, newRect.bottom - newRect.top);
        ::OffsetRect(&newRect, oldRect.left, oldRect.top);
        // Should use SetWindowPos rather than MoveWindow to avoid cross process deadlock
        ::SetWindowPos(m_window, nullptr, newRect.left, newRect.top,
            newRect.right - newRect.left, newRect.bottom - newRect.top, SWP_NOACTIVATE | SWP_NOZORDER | SWP_ASYNCWINDOWPOS);
      }
      
      // NV-DXVK end
    } else {
      if (modifyWindow && changeFullscreen) {
        if (FAILED(this->EnterFullscreenMode(pPresentParams, pFullscreenDisplayMode)))
          return D3DERR_INVALIDCALL;
      }

      D3D9WindowMessageFilter filter(m_window);

      if (modifyWindow) {
        if (!changeFullscreen) {
          if (FAILED(ChangeDisplayMode(pPresentParams, pFullscreenDisplayMode)))
            return D3DERR_INVALIDCALL;
        }

        // Move the window so that it covers the entire output
        RECT rect;
        GetMonitorRect(GetDefaultMonitor(), &rect);

        ::SetWindowPos(m_window, HWND_TOPMOST,
          rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top,
          SWP_FRAMECHANGED | SWP_SHOWWINDOW | SWP_NOACTIVATE | SWP_ASYNCWINDOWPOS);
      }

      if (changeFullscreen) {
        // Reinstall window hook that was removed in LeaveFullscreenMode() above
        HookWindowProc(m_window);
      }
    }

    m_presentParams = *pPresentParams;

    if (modifyWindow && changeFullscreen)
      SetGammaRamp(0, &m_ramp);

    CreateBackBuffers(m_presentParams.BackBufferCount);

    return D3D_OK;
  }


  HRESULT D3D9SwapChainEx::WaitForVBlank() {
    static bool s_errorShown = false;

    if (!std::exchange(s_errorShown, true))
      Logger::warn("D3D9SwapChainEx::WaitForVBlank: Stub");

    return D3D_OK;
  }

  static bool validateGammaRamp(const WORD (&ramp)[256]) {
    if (ramp[0] >= ramp[std::size(ramp) - 1]) {
      Logger::err("validateGammaRamp: ramp inverted or flat");
      return false;
    }

    for (size_t i = 1; i < std::size(ramp); i++) {
      if (ramp[i] < ramp[i - 1]) {
        Logger::err("validateGammaRamp: ramp not monotonically increasing");
        return false;
      }
      if (ramp[i] - ramp[i - 1] >= UINT16_MAX / 2) {
        Logger::err("validateGammaRamp: huuuge jump");
        return false;
      }
    }

    return true;
  }


  void    D3D9SwapChainEx::SetGammaRamp(
            DWORD         Flags,
      const D3DGAMMARAMP* pRamp) {
    D3D9DeviceLock lock = m_parent->LockDevice();

    if (unlikely(pRamp == nullptr))
      return;

    if (unlikely(!validateGammaRamp(pRamp->red)
              && !validateGammaRamp(pRamp->blue)
              && !validateGammaRamp(pRamp->green)))
      return;

    m_ramp = *pRamp;

    bool isIdentity = true;

    std::array<DxvkGammaCp, NumControlPoints> cp;
      
    for (uint32_t i = 0; i < NumControlPoints; i++) {
      uint16_t identity = MapGammaControlPoint(float(i) / float(NumControlPoints - 1));

      cp[i].r = pRamp->red[i];
      cp[i].g = pRamp->green[i];
      cp[i].b = pRamp->blue[i];
      cp[i].a = 0;

      isIdentity &= cp[i].r == identity
                 && cp[i].g == identity
                 && cp[i].b == identity;
    }

    if (!isIdentity && !m_presentParams.Windowed)
      m_blitter->setGammaRamp(NumControlPoints, cp.data());
    else
      m_blitter->setGammaRamp(0, nullptr);
  }


  void    D3D9SwapChainEx::GetGammaRamp(D3DGAMMARAMP* pRamp) {
    D3D9DeviceLock lock = m_parent->LockDevice();

    if (likely(pRamp != nullptr))
      *pRamp = m_ramp;
  }


  void    D3D9SwapChainEx::Invalidate(HWND hWindow) {
    if (hWindow == nullptr)
      hWindow = m_parent->GetWindow();

    if (m_presentParams.hDeviceWindow == hWindow) {
      // NV-DXVK start: DLFG integration
      m_device->synchronizePresenter();

      if (m_presenter != nullptr) {
        assert(m_dlfgPresenter == nullptr);
        m_presenter = nullptr;
      }

      if (m_dlfgPresenter != nullptr) {
        assert(m_presenter == nullptr);
        m_dlfgPresenter = nullptr;
      }

      m_device->waitForSubmission(&m_presentStatus);
      m_device->waitForIdle();
    }
  }


  HRESULT D3D9SwapChainEx::SetDialogBoxMode(bool bEnableDialogs) {
    D3D9DeviceLock lock = m_parent->LockDevice();

    // https://docs.microsoft.com/en-us/windows/win32/api/d3d9/nf-d3d9-idirect3ddevice9-setdialogboxmode
    // The MSDN documentation says this will error out under many weird conditions.
    // However it doesn't appear to error at all in any of my tests of these
    // cases described in the documentation.

    m_dialog = bEnableDialogs;

    return D3D_OK;
  }


  D3D9Surface* D3D9SwapChainEx::GetBackBuffer(UINT iBackBuffer) {
    if (iBackBuffer >= m_presentParams.BackBufferCount)
      return nullptr;

    return m_backBuffers[iBackBuffer].ptr();
  }


  void D3D9SwapChainEx::NormalizePresentParameters(D3DPRESENT_PARAMETERS* pPresentParams) {
    if (pPresentParams->hDeviceWindow == nullptr)
      pPresentParams->hDeviceWindow    = m_parent->GetWindow();

    pPresentParams->BackBufferCount    = std::max(pPresentParams->BackBufferCount, 1u);

    const int32_t forcedMSAA = m_parent->GetOptions()->forceSwapchainMSAA;
    if (forcedMSAA != -1) {
      pPresentParams->MultiSampleType    = D3DMULTISAMPLE_TYPE(forcedMSAA);
      pPresentParams->MultiSampleQuality = 0;
    }

    if (pPresentParams->Windowed) {
      GetWindowClientSize(pPresentParams->hDeviceWindow,
        pPresentParams->BackBufferWidth  ? nullptr : &pPresentParams->BackBufferWidth,
        pPresentParams->BackBufferHeight ? nullptr : &pPresentParams->BackBufferHeight);
    }
    else {
      GetMonitorClientSize(GetDefaultMonitor(),
        pPresentParams->BackBufferWidth  ? nullptr : &pPresentParams->BackBufferWidth,
        pPresentParams->BackBufferHeight ? nullptr : &pPresentParams->BackBufferHeight);
    }

    if (pPresentParams->BackBufferFormat == D3DFMT_UNKNOWN)
      pPresentParams->BackBufferFormat = D3DFMT_X8R8G8B8;

    if (env::getEnvVar("DXVK_FORCE_WINDOWED") == "1")
      pPresentParams->Windowed         = TRUE;

    if (env::getEnvVar("DXVK_RESOLUTION_WIDTH") != "")
      pPresentParams->BackBufferWidth = stoul(env::getEnvVar("DXVK_RESOLUTION_WIDTH"));

    if (env::getEnvVar("DXVK_RESOLUTION_HEIGHT") != "")
      pPresentParams->BackBufferHeight = stoul(env::getEnvVar("DXVK_RESOLUTION_HEIGHT"));

    m_widthScale = (float)pPresentParams->BackBufferWidth / m_originalWidth;
    m_heightScale = (float)pPresentParams->BackBufferHeight / m_originalHeight;
  }


  void D3D9SwapChainEx::PresentImage(UINT SyncInterval) {
    ScopedCpuProfileZone();
    m_parent->Flush();

    // NV-DXVK start: Reflex integration
    auto& reflex = m_device->getCommon()->metaReflex();
    auto& d3d9Rtx = m_parent->m_rtx;

    // Note: Set the latency ping thread to this thread. This is not a great place to do this as this will be called every present, but this operation
    // should be fairly cheap. Additionally this thread can be changed dynamically without issue so not setting it further in advance on the thread
    // this function is called on is fine too.
    reflex.setLatencyPingThread();

    // Note: Simulation ended on the same thread it started on (the main thread). This is put here specifically as when the application
    // calls into DXVK's Present this typically means it is done all its work for the frame. The application's own rendering calls are counted
    // as simulation here not rendering as they are really just mapped into Remix's geometry and other processing rather than rendering directly,
    // instead the actual rendering happens on the DXVK side once the CS thread kicks off Vulkan work. The Reflex markers for rendering
    // are tracked on that thread in parallel which may mean some overlap with the simulation region occurs, but this is allowed in Reflex (same
    // goes for the present markers).
    reflex.endSimulation(d3d9Rtx.GetReflexFrameId());

    // NV-DXVK end

    // Retrieve the image and image view to present
    auto swapImage = m_backBuffers[0]->GetCommonTexture()->GetImage();
    auto swapImageView = m_backBuffers[0]->GetImageView(false);

    // Bump our frame id.
    ++m_frameId;
    SyncFrameLatency();

    for (uint32_t i = 0; i < SyncInterval || i < 1; i++) {
      SynchronizePresent();

      // NV-DXVK start: DLFG integration
      vk::Presenter* presenter = GetPresenter();
      // NV-DXVK end
      
      // Presentation semaphores and WSI swap chain image
      // NV-DXVK start: DLFG integration
      vk::PresenterInfo info = presenter->info();
      // NV-DXVK end
      vk::PresenterSync sync;

      uint32_t imageIndex = 0;

      // NV-DXVK start: DLFG integration
      VkResult status = presenter->acquireNextImage(sync, imageIndex);
      // NV-DXVK end

      while (status != VK_SUCCESS) {
        RecreateSwapChain(m_vsync);
        
        // NV-DXVK start: DLFG integration
        info = presenter->info();
        status = presenter->acquireNextImage(sync, imageIndex);
        // NV-DXVK end

        if (status == VK_SUBOPTIMAL_KHR)
          break;
      }

      m_context->beginRecording(
        m_device->createCommandList());

      VkRect2D srcRect = {
        {  int32_t(m_srcRect.left),                    int32_t(m_srcRect.top)                    },
        { uint32_t(m_srcRect.right - m_srcRect.left), uint32_t(m_srcRect.bottom - m_srcRect.top) } };

      VkRect2D dstRect = {
        {  int32_t(m_dstRect.left),                    int32_t(m_dstRect.top)                    },
        { uint32_t(m_dstRect.right - m_dstRect.left), uint32_t(m_dstRect.bottom - m_dstRect.top) } };
      

      m_blitter->presentImage(m_context.ptr(),
        m_imageViews.at(imageIndex), dstRect,
        swapImageView, srcRect);

      if (m_hud != nullptr)
        m_hud->render(m_context, info.format, info.imageExtent);

      auto& gui = m_device->getCommon()->getImgui();
      gui.render(m_window, m_context, info.imageExtent, m_vsync);

      // NV-DXVK start
      m_parent->m_rtx.OnPresent(m_imageViews.at(imageIndex)->image());
      // NV-DXVK end

      if (i + 1 >= SyncInterval)
        m_context->signal(m_frameLatencySignal, m_frameId);

      SubmitPresent(sync, i, imageIndex);
    }

    // Rotate swap chain buffers so that the back
    // buffer at index 0 becomes the front buffer.
    for (uint32_t i = 1; i < m_backBuffers.size(); i++)
      m_backBuffers[i]->Swap(m_backBuffers[i - 1].ptr());

    m_parent->m_flags.set(D3D9DeviceFlag::DirtyFramebuffer);

    // NV-DXVK start: Reflex integration
    // Note: Sleeping here in the present function essentially makes it so when the application calls into a D3D Present function it will block for the desired amount of time Reflex indicates.
    // This helps accomplish what Reflex desires by delaying the point at which the application does input sampling likely near the start of its simulation on the next frame, thus reducing latency.
    reflex.sleep();

    // Note: Increment the Reflex Frame ID to prepare for the next frame, now that this Reflex frame has ended.
    // Take care to ensure this happens after all other application thread operations call GetReflexFrameId for this frame
    // otherwise inconsistent frame IDs may be passed to Reflex.
    d3d9Rtx.IncrementReflexFrameId();

    // Note: After presentation is typically where the application calling into DXVK will start its next frame simulation wise, so we put the marker here
    // on the main thread after the Reflex sleep has completed to encompass this region.
    reflex.beginSimulation(d3d9Rtx.GetReflexFrameId());
    reflex.latencyPing(d3d9Rtx.GetReflexFrameId());

    // Tell tracy its the end of the frame
    FrameMark;
    // NV-DXVK end
  }


  void D3D9SwapChainEx::SubmitPresent(const vk::PresenterSync& Sync, uint32_t FrameId, uint32_t imageIndex) {
    ScopedCpuProfileZone();

    // NV-DXVK start: Reflex integration
    auto& d3d9Rtx = m_parent->m_rtx;

    const auto currentReflexFrameId = d3d9Rtx.GetReflexFrameId();
    // NV-DXVK end

    // Present from CS thread so that we don't
    // have to synchronize with it first.
    m_presentStatus.result = VK_NOT_READY;

    m_parent->EmitCs([this,
      cReflexFrameId = currentReflexFrameId,
      cAcquiredImageIndex = imageIndex,
      cFrameId     = FrameId,
      cSync        = Sync,
      cHud         = m_hud,
      cCommandList = m_context->endRecording()
    ] (DxvkContext* ctx) {
      ScopedGpuProfileZone(ctx, "Queue Present");

      m_device->submitCommandList(cCommandList,
        cSync.acquire, cSync.present);
      
      // NV-DXVK start: DLFG integration
      if (cHud != nullptr && !cFrameId) {
        if (m_dlfgPresenter != nullptr) {
          cHud->update(m_dlfgPresenter->getPresentFrameCount());
        } else {
          cHud->update(1);
        }
      }
      // NV-DXVK end

      // NV-DXVK start: DLFG integration
      // Note: Do not insert Reflex present markers when DLFG is enabled, the DLFG Presenter will insert its own Reflex markers
      // (unless the workaround is enabled as this requires that the Present markers stay where they usually are).
      const bool insertReflexPresentMarkers = !m_context->isDLFGEnabled() || (__DLFG_REFLEX_WORKAROUND != 0);

      m_device->presentImage(cReflexFrameId, insertReflexPresentMarkers, cAcquiredImageIndex, GetPresenter(), &m_presentStatus);
      // NV-DXVK end
    });

    m_parent->FlushCsChunk();
  }

  void D3D9SwapChainEx::SynchronizePresent() {
    ScopedCpuProfileZone();
    // Recreate swap chain if the previous present call failed
    VkResult status = m_device->waitForSubmission(&m_presentStatus);

    if (status != VK_SUCCESS
        // NV-DXVK start: DLFG integration
        && status != VK_EVENT_SET
        )
      RecreateSwapChain(m_vsync);
  }


  void D3D9SwapChainEx::RecreateSwapChain(BOOL Vsync) {
    // Ensure that we can safely destroy the swap chain
    VkResult status = m_device->waitForSubmission(&m_presentStatus);

    // NV-DXVK start: DLFG integration
    if (m_dlfgPresenter != nullptr) {
      // synchronize DLFG presenter after flushing the submit queue to ensure m_presentStatus is up to date
      m_dlfgPresenter->synchronize();

      if (status == VK_EVENT_SET) {
        // if we got a present queued status from DLFG, it must have updated it now
        assert(m_presentStatus.result != VK_EVENT_SET);
      }
    } else {
      assert(status != VK_EVENT_SET);
    }
    // NV-DXVK end

    m_presentStatus.result = VK_SUCCESS;

    vk::PresenterDesc presenterDesc;
    presenterDesc.imageExtent     = GetPresentExtent();
    presenterDesc.imageCount      = PickImageCount(m_presentParams.BackBufferCount + 1);
    presenterDesc.numFormats      = PickFormats(EnumerateFormat(m_presentParams.BackBufferFormat), presenterDesc.formats);
    presenterDesc.numPresentModes = PickPresentModes(Vsync, presenterDesc.presentModes);
    presenterDesc.fullScreenExclusive = PickFullscreenMode();

    if (GetPresenter()->recreateSwapChain(presenterDesc) != VK_SUCCESS)
      throw DxvkError("D3D9SwapChainEx: Failed to recreate swap chain");
    // NV-DXVK end
    

    CreateRenderTargetViews();
  }


  void D3D9SwapChainEx::CreatePresenter() {
    // Ensure that we can safely destroy the swap chain
    m_device->waitForSubmission(&m_presentStatus);
    
    // NV-DXVK start: DLFG integration
    if (m_dlfgPresenter != nullptr) {
      // need to synchronize DLFG presenter explicitly
      // to ensure pacer thread is idle
      m_dlfgPresenter->synchronize();
    }

    // flush all pending CS work
    // this ensures any work that relies on the FG presenter is submitted before we
    // synchronize the submission queue + GPU below
    m_parent->SynchronizeCsThread();
    // NV-DXVK end

    m_device->waitForIdle();

    m_presenter = nullptr;
    m_presentStatus.result = VK_SUCCESS;

    // NV-DXVK start: DLFG integration
    m_dlfgPresenter = nullptr;
    const bool dlfgEnabled = m_context->isDLFGEnabled();
    DxvkDeviceQueue presentQueue = dlfgEnabled ? m_device->queues().present : m_device->queues().graphics;
    
    vk::PresenterDevice presenterDevice;
    presenterDevice.queueFamily   = presentQueue.queueFamily;
    presenterDevice.queue         = presentQueue.queueHandle;
    // NV-DXVK end
    presenterDevice.adapter       = m_device->adapter()->handle();
    presenterDevice.features.fullScreenExclusive = m_device->extensions().extFullScreenExclusive;

    vk::PresenterDesc presenterDesc;
    presenterDesc.imageExtent     = GetPresentExtent();
    presenterDesc.imageCount      = PickImageCount(m_presentParams.BackBufferCount + 1);
    presenterDesc.numFormats      = PickFormats(EnumerateFormat(m_presentParams.BackBufferFormat), presenterDesc.formats);
    presenterDesc.numPresentModes = PickPresentModes(false, presenterDesc.presentModes);
    presenterDesc.fullScreenExclusive = PickFullscreenMode();

    // NV-DXVK start: DLFG integration
    if (dlfgEnabled) {
      // DLFG presents 2 times (1 more frame) in each real frame,
      // increase image count by 1 to avoid resource waiting.
      presenterDesc.imageCount++;
      m_dlfgPresenter = new DxvkDLFGPresenter(m_device,
                                              m_context,
                                              m_window,
                                              m_device->adapter()->vki(),
                                              m_device->vkd(),
                                              presenterDevice,
                                              presenterDesc);
    } else {
      m_presenter = new vk::Presenter(m_window,
        m_device->adapter()->vki(),
        m_device->vkd(),
        presenterDevice,
        presenterDesc);
    }

    GetPresenter()->setFrameRateLimit(m_parent->GetOptions()->maxFrameRate);
    GetPresenter()->setFrameRateLimiterRefreshRate(m_displayRefreshRate);
    // NV-DXVK end

    CreateRenderTargetViews();
  }


  void D3D9SwapChainEx::CreateRenderTargetViews() {
    // NV-DXVK start: DLFG integration
    vk::PresenterInfo info = GetPresenter()->info();
    // NV-DXVK end
    
    m_imageViews.clear();
    m_imageViews.resize(info.imageCount);

    DxvkImageCreateInfo imageInfo;
    imageInfo.type        = VK_IMAGE_TYPE_2D;
    imageInfo.format      = info.format.format;
    imageInfo.flags       = 0;
    imageInfo.sampleCount = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.extent      = { info.imageExtent.width, info.imageExtent.height, 1 };
    imageInfo.numLayers   = 1;
    imageInfo.mipLevels   = 1;
    imageInfo.usage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    imageInfo.stages      = 0;
    imageInfo.access      = 0;
    imageInfo.tiling      = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.layout      = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    imageInfo.shared      = VK_TRUE;

    DxvkImageViewCreateInfo viewInfo;
    viewInfo.type         = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format       = info.format.format;
    viewInfo.usage        = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    viewInfo.aspect       = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.minLevel     = 0;
    viewInfo.numLevels    = 1;
    viewInfo.minLayer     = 0;
    viewInfo.numLayers    = 1;

    for (uint32_t i = 0; i < info.imageCount; i++) {
      // NV-DXVK start: DLFG integration
      VkImage imageHandle = GetPresenter()->getImage(i).image;
      // NV-DXVK end

      Rc<DxvkImage> image = new DxvkImage(
        m_device.ptr(), imageInfo, imageHandle);

      m_imageViews[i] = new DxvkImageView(
        m_device->vkd(), image, viewInfo);
    }
  }


  void D3D9SwapChainEx::DestroyBackBuffers() {
    for (auto& backBuffer : m_backBuffers)
      backBuffer->ClearContainer();

    m_backBuffers.clear();
  }


  void D3D9SwapChainEx::CreateBackBuffers(uint32_t NumBackBuffers) {
    // Explicitly destroy current swap image before
    // creating a new one to free up resources
    DestroyBackBuffers();

    int NumFrontBuffer = NumFrontBuffers();

    m_backBuffers.resize(NumBackBuffers + NumFrontBuffer);

    // Create new back buffer
    D3D9_COMMON_TEXTURE_DESC desc;
    desc.Width              = std::max(m_presentParams.BackBufferWidth,  1u);
    desc.Height             = std::max(m_presentParams.BackBufferHeight, 1u);
    desc.Depth              = 1;
    desc.MipLevels          = 1;
    desc.ArraySize          = 1;
    desc.Format             = EnumerateFormat(m_presentParams.BackBufferFormat);
    desc.MultiSample        = m_presentParams.MultiSampleType;
    desc.MultisampleQuality = m_presentParams.MultiSampleQuality;
    desc.Pool               = D3DPOOL_DEFAULT;
    desc.Usage              = D3DUSAGE_RENDERTARGET;
    desc.Discard            = FALSE;
    desc.IsBackBuffer       = TRUE;
    desc.IsAttachmentOnly   = FALSE;

    for (uint32_t i = 0; i < m_backBuffers.size(); i++)
      m_backBuffers[i] = new D3D9Surface(m_parent, &desc, this, nullptr);

    auto swapImage = m_backBuffers[0]->GetCommonTexture()->GetImage();

    // Initialize the image so that we can use it. Clearing
    // to black prevents garbled output for the first frame.
    VkImageSubresourceRange subresources;
    subresources.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    subresources.baseMipLevel   = 0;
    subresources.levelCount     = 1;
    subresources.baseArrayLayer = 0;
    subresources.layerCount     = 1;

    VkClearColorValue clearColor;
    clearColor.float32[0] = 0.0f;
    clearColor.float32[1] = 0.0f;
    clearColor.float32[2] = 0.0f;
    clearColor.float32[3] = 0.0f;

    m_context->beginRecording(
      m_device->createCommandList());
    
    for (uint32_t i = 0; i < m_backBuffers.size(); i++) {
      m_context->clearColorImage(
        m_backBuffers[i]->GetCommonTexture()->GetImage(),
        clearColor, subresources);
    }

    m_device->submitCommandList(
      m_context->endRecording(),
      VK_NULL_HANDLE,
      VK_NULL_HANDLE);
  }


  int D3D9SwapChainEx::NumFrontBuffers() {
    return m_parent->GetOptions()->noExplicitFrontBuffer ? 0 : 1;
  }

  void D3D9SwapChainEx::CreateBlitter() {
    m_blitter = new DxvkSwapchainBlitter(m_device);
  }


  void D3D9SwapChainEx::CreateHud() {
    m_hud = hud::Hud::createHud(m_device);

    if (m_hud != nullptr) {
      m_hud->addItem<hud::HudClientApiItem>("api", 1, GetApiName());
      m_hud->addItem<hud::HudSamplerCount>("samplers", -1, m_parent);
    }
  }


  void D3D9SwapChainEx::InitRamp() {
    for (uint32_t i = 0; i < NumControlPoints; i++) {
      DWORD identity = DWORD(MapGammaControlPoint(float(i) / float(NumControlPoints - 1)));

      m_ramp.red[i]   = identity;
      m_ramp.green[i] = identity;
      m_ramp.blue[i]  = identity;
    }
  }


  void D3D9SwapChainEx::SyncFrameLatency() {
    ScopedCpuProfileZone();

    // Wait for the sync event so that we respect the maximum frame latency
    m_frameLatencySignal->wait(m_frameId - GetActualFrameLatency());
  }


  uint32_t D3D9SwapChainEx::GetActualFrameLatency() {
    uint32_t maxFrameLatency = m_parent->GetFrameLatency();

    if (m_frameLatencyCap)
      maxFrameLatency = std::min(maxFrameLatency, m_frameLatencyCap);

    maxFrameLatency = std::min(maxFrameLatency, m_presentParams.BackBufferCount + 1);
    return maxFrameLatency;
  }


  uint32_t D3D9SwapChainEx::PickFormats(
          D3D9Format                Format,
          VkSurfaceFormatKHR*       pDstFormats) {
    uint32_t n = 0;

    switch (Format) {
      default:
        Logger::warn(str::format("D3D9SwapChainEx: Unexpected format: ", Format));
        
      case D3D9Format::A8R8G8B8:
      case D3D9Format::X8R8G8B8:
      case D3D9Format::A8B8G8R8:
      case D3D9Format::X8B8G8R8: {
        pDstFormats[n++] = { VK_FORMAT_R8G8B8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR };
        pDstFormats[n++] = { VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR };
      } break;

      case D3D9Format::A2R10G10B10:
      case D3D9Format::A2B10G10R10: {
        pDstFormats[n++] = { VK_FORMAT_A2B10G10R10_UNORM_PACK32, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR };
        pDstFormats[n++] = { VK_FORMAT_A2R10G10B10_UNORM_PACK32, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR };
      } break;

      case D3D9Format::X1R5G5B5:
      case D3D9Format::A1R5G5B5: {
        pDstFormats[n++] = { VK_FORMAT_B5G5R5A1_UNORM_PACK16, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR };
        pDstFormats[n++] = { VK_FORMAT_R5G5B5A1_UNORM_PACK16, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR };
        pDstFormats[n++] = { VK_FORMAT_A1R5G5B5_UNORM_PACK16, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR };
      }

      case D3D9Format::R5G6B5: {
        pDstFormats[n++] = { VK_FORMAT_B5G6R5_UNORM_PACK16, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR };
        pDstFormats[n++] = { VK_FORMAT_R5G6B5_UNORM_PACK16, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR };
      }
    }

    return n;
  }


  uint32_t D3D9SwapChainEx::PickPresentModes(
          BOOL                      Vsync,
          VkPresentModeKHR*         pDstModes) {
    uint32_t n = 0;

    if (Vsync) {
      if (m_parent->GetOptions()->tearFree == Tristate::False)
        pDstModes[n++] = VK_PRESENT_MODE_FIFO_RELAXED_KHR;
      pDstModes[n++] = VK_PRESENT_MODE_FIFO_KHR;
    } else {
      if (m_parent->GetOptions()->tearFree != Tristate::True)
        pDstModes[n++] = VK_PRESENT_MODE_IMMEDIATE_KHR;
      pDstModes[n++] = VK_PRESENT_MODE_MAILBOX_KHR;
    }

    return n;
  }


  uint32_t D3D9SwapChainEx::PickImageCount(
          UINT                      Preferred) {
    int32_t option = m_parent->GetOptions()->numBackBuffers;
    return option > 0 ? uint32_t(option) : uint32_t(Preferred);
  }


  void D3D9SwapChainEx::NotifyDisplayRefreshRate(
          double                  RefreshRate) {
    m_displayRefreshRate = RefreshRate;

    // NV-DXVK start: DLFG integration
    if (GetPresenter() != nullptr)
      GetPresenter()->setFrameRateLimiterRefreshRate(RefreshRate);
  }


  HRESULT D3D9SwapChainEx::EnterFullscreenMode(
          D3DPRESENT_PARAMETERS* pPresentParams,
    const D3DDISPLAYMODEEX*      pFullscreenDisplayMode) {    
    // Find a display mode that matches what we need
    ::GetWindowRect(m_window, &m_windowState.rect);
      
    if (FAILED(ChangeDisplayMode(pPresentParams, pFullscreenDisplayMode))) {
      Logger::err("D3D9: EnterFullscreenMode: Failed to change display mode");
      return D3DERR_INVALIDCALL;
    }

    // Testing shows we shouldn't hook WM_NCCALCSIZE but we shouldn't change
    // windows style either.
    //
    // Some games restore window styles after we have changed it, so hooking is
    // also required. Doing it will allow us to create fullscreen windows
    // regardless of their style and it also appears to work on Windows.
    HookWindowProc(m_window);

   if(!env::isRemixBridgeActive()) {
      D3D9WindowMessageFilter filter(m_window);

      // Change the window flags to remove the decoration etc.
      LONG style = ::GetWindowLong(m_window, GWL_STYLE);
      LONG exstyle = ::GetWindowLong(m_window, GWL_EXSTYLE);

      m_windowState.style = style;
      m_windowState.exstyle = exstyle;

      style &= ~WS_OVERLAPPEDWINDOW;
      exstyle &= ~WS_EX_OVERLAPPEDWINDOW;
      
      ::SetWindowLong(m_window, GWL_STYLE, style);
      ::SetWindowLong(m_window, GWL_EXSTYLE, exstyle);

      // Move the window so that it covers the entire output    
      RECT rect;
      GetMonitorRect(GetDefaultMonitor(), &rect);

      ::SetWindowPos(m_window, HWND_TOPMOST,
        rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top,
        SWP_FRAMECHANGED | SWP_SHOWWINDOW | SWP_NOACTIVATE | SWP_ASYNCWINDOWPOS);
    }
    
    m_monitor = GetDefaultMonitor();

    GetPresenter()->acquireFullscreenExclusive();

    return D3D_OK;
  }
  
  
  HRESULT D3D9SwapChainEx::LeaveFullscreenMode() {
    if (!IsWindow(m_window))
      return D3DERR_INVALIDCALL;
    
    if (FAILED(RestoreDisplayMode(m_monitor)))
      Logger::warn("D3D9: LeaveFullscreenMode: Failed to restore display mode");
    
    m_monitor = nullptr;

    ResetWindowProc(m_window);
    
    if (!env::isRemixBridgeActive()) {
      // Only restore the window style if the application hasn't
      // changed them. This is in line with what native D3D9 does.
      LONG curStyle = ::GetWindowLongW(m_window, GWL_STYLE) & ~WS_VISIBLE;
      LONG curExstyle = ::GetWindowLongW(m_window, GWL_EXSTYLE) & ~WS_EX_TOPMOST;

      if (curStyle == (m_windowState.style & ~(WS_VISIBLE | WS_OVERLAPPEDWINDOW))
       && curExstyle == (m_windowState.exstyle & ~(WS_EX_TOPMOST | WS_EX_OVERLAPPEDWINDOW))) {
        ::SetWindowLongW(m_window, GWL_STYLE, m_windowState.style);
        ::SetWindowLongW(m_window, GWL_EXSTYLE, m_windowState.exstyle);
      }

      // Restore window position and apply the style
      const RECT rect = m_windowState.rect;

      ::SetWindowPos(m_window, 0,
        rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top,
        SWP_FRAMECHANGED | SWP_NOZORDER | SWP_NOACTIVATE | SWP_ASYNCWINDOWPOS);
    }

    GetPresenter()->releaseFullscreenExclusive();
    
    return D3D_OK;
  }
  
  // NV-DXVK start: 
  void D3D9SwapChainEx::onWindowMessageEvent(UINT message, WPARAM wParam) {
  
    // Ensure RTX end of frame events happen when the app window minimizes or loses focus when in fullscreen mode.
    // RTX logic assumes that present() occurs every frame and calls end of frame events there to ensure valid state for the subsequent frame.
    // Therefore call the required end of frame events explicitly on such events.
    const bool triggerRtxEndOfFrameEvents =
      (message == WM_ACTIVATE && wParam == WA_INACTIVE) ||
      (message == WM_SIZE && (wParam == SIZE_MINIMIZED || wParam == SIZE_RESTORED));
  
    if (triggerRtxEndOfFrameEvents) {
      // Don't artificially and unnecessarily inject RTX when no present is called
      const bool callInjectRtx = false;
      
      m_parent->m_rtx.EndFrame(m_backBuffers[0]->GetCommonTexture()->GetImage(), callInjectRtx);

      // Need to increment present counter as it's used to reject repeated injectRtx calls.
      // Failing to do that will make next frame injection get rejected
      m_parent->EmitCs([](DxvkContext* ctx) {
        ctx->getDevice()->incrementPresentCount();
      });
    }
  }
    // NV-DXVK end
  
  HRESULT D3D9SwapChainEx::ChangeDisplayMode(
          D3DPRESENT_PARAMETERS* pPresentParams,
    const D3DDISPLAYMODEEX*      pFullscreenDisplayMode) {
    D3DDISPLAYMODEEX mode;

    if (pFullscreenDisplayMode) {
      mode = *pFullscreenDisplayMode;
    } else {
      mode.Width            = pPresentParams->BackBufferWidth;
      mode.Height           = pPresentParams->BackBufferHeight;
      mode.Format           = pPresentParams->BackBufferFormat;
      mode.RefreshRate      = pPresentParams->FullScreen_RefreshRateInHz;
      mode.ScanLineOrdering = D3DSCANLINEORDERING_PROGRESSIVE;
      mode.Size             = sizeof(D3DDISPLAYMODEEX);
    }

    DEVMODEW devMode = { };
    devMode.dmSize       = sizeof(devMode);
    devMode.dmFields     = DM_PELSWIDTH | DM_PELSHEIGHT | DM_BITSPERPEL;
    devMode.dmPelsWidth  = mode.Width;
    devMode.dmPelsHeight = mode.Height;
    devMode.dmBitsPerPel = GetMonitorFormatBpp(EnumerateFormat(mode.Format));
    
    if (mode.RefreshRate != 0)  {
      devMode.dmFields |= DM_DISPLAYFREQUENCY;
      devMode.dmDisplayFrequency = mode.RefreshRate;
    }
    
    HMONITOR monitor = GetDefaultMonitor();

    if (!SetMonitorDisplayMode(monitor, &devMode))
      return D3DERR_NOTAVAILABLE;

    devMode.dmFields = DM_DISPLAYFREQUENCY;
    
    if (GetMonitorDisplayMode(monitor, ENUM_CURRENT_SETTINGS, &devMode))
      NotifyDisplayRefreshRate(double(devMode.dmDisplayFrequency));
    else
      NotifyDisplayRefreshRate(0.0);

    return D3D_OK;
  }
  
  
  HRESULT D3D9SwapChainEx::RestoreDisplayMode(HMONITOR hMonitor) {
    if (hMonitor == nullptr)
      return D3DERR_INVALIDCALL;
    
    if (!RestoreMonitorDisplayMode())
      return D3DERR_NOTAVAILABLE;

    NotifyDisplayRefreshRate(0.0);
    return D3D_OK;
  }

  bool    D3D9SwapChainEx::UpdatePresentRegion(const RECT* pSourceRect, const RECT* pDestRect) {
    if (pSourceRect == nullptr) {
      m_srcRect.top    = 0;
      m_srcRect.left   = 0;
      m_srcRect.right  = m_presentParams.BackBufferWidth;
      m_srcRect.bottom = m_presentParams.BackBufferHeight;
    }
    else
      m_srcRect = *pSourceRect;

    RECT dstRect;
    if (pDestRect == nullptr) {
      // TODO: Should we hook WM_SIZE message for this?
      UINT width, height;
      GetWindowClientSize(m_window, &width, &height);
      
      dstRect.top    = 0;
      dstRect.left   = 0;
      dstRect.right  = LONG(width);
      dstRect.bottom = LONG(height);
    }
    else
      dstRect = *pDestRect;

    // Update window client size if need be
    UINT width = dstRect.right - dstRect.left;
    UINT height = dstRect.bottom - dstRect.top;
    const bool isDxvkResolutionEnvVarSet = env::getEnvVar("DXVK_RESOLUTION_WIDTH") != "" || env::getEnvVar("DXVK_RESOLUTION_HEIGHT") != "";
    if (isDxvkResolutionEnvVarSet &&
      (width != m_presentParams.BackBufferWidth || height != m_presentParams.BackBufferHeight)) {
       
      UINT windowWidth, windowHeight;
      GetWindowClientSize(m_window, &windowWidth, &windowHeight);
      if (windowWidth != m_presentParams.BackBufferWidth || windowHeight != m_presentParams.BackBufferHeight) {

        D3DDISPLAYMODEEX displayMode;
        GetDisplayModeEx(&displayMode, nullptr);
        Reset(&m_presentParams, &displayMode, isDxvkResolutionEnvVarSet);

        // Update backbuffer resolution with the actual created client window resolution, 
        // which can be different from the requested one 
        // (i.e. when window doesn't fit the available desktop resolution real-estate)
        // so that we don't keep resetting the window resolution
        UINT newWindowWidth, newWindowHeight;
        GetWindowClientSize(m_window, &newWindowWidth, &newWindowHeight);
        m_presentParams.BackBufferWidth = newWindowWidth;
        m_presentParams.BackBufferHeight = newWindowHeight;
      }

      dstRect.right = dstRect.left + m_presentParams.BackBufferWidth;
      dstRect.bottom = dstRect.top + m_presentParams.BackBufferHeight;
    }

    bool recreate = 
       m_dstRect.left   != dstRect.left
    || m_dstRect.top    != dstRect.top
    || m_dstRect.right  != dstRect.right
    || m_dstRect.bottom != dstRect.bottom;

    m_dstRect = dstRect;

    return recreate;
  }

  VkExtent2D D3D9SwapChainEx::GetPresentExtent() {
    return VkExtent2D {
      std::max<uint32_t>(m_dstRect.right  - m_dstRect.left, 1u),
      std::max<uint32_t>(m_dstRect.bottom - m_dstRect.top,  1u) };
  }


  VkFullScreenExclusiveEXT D3D9SwapChainEx::PickFullscreenMode() {
    // NV-DXVK start: App controlled FSE
    if (!RtxOptions::allowFSE() || m_dialog || m_presentParams.Windowed)
      return VK_FULL_SCREEN_EXCLUSIVE_DISALLOWED_EXT;

    if (env::isRemixBridgeActive()) {
      if(m_presentParams.BackBufferWidth == GetSystemMetrics(SM_CXSCREEN) &&
         m_presentParams.BackBufferHeight == GetSystemMetrics(SM_CYSCREEN))
        return VK_FULL_SCREEN_EXCLUSIVE_APPLICATION_CONTROLLED_EXT;
      else
        return VK_FULL_SCREEN_EXCLUSIVE_DISALLOWED_EXT;
    }

    return VK_FULL_SCREEN_EXCLUSIVE_DEFAULT_EXT;
    // NV-DXVK end
  }


  std::string D3D9SwapChainEx::GetApiName() {
    return this->GetParent()->IsExtended() ? "D3D9Ex" : "D3D9";
  }

}
