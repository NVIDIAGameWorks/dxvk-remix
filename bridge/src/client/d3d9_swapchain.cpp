/*
 * Copyright (c) 2022-2024, NVIDIA CORPORATION. All rights reserved.
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
#include "pch.h"
#include "d3d9_lss.h"
#include "d3d9_swapchain.h"
#include "d3d9_surface.h"
#include "d3d9_surfacebuffer_helper.h"
#include "swapchain_map.h"

extern std::mutex gSwapChainMapMutex;
extern SwapChainMap gSwapChainMap;

/*
 * Direct3DSwapChain9_LSS Interface Implementation
 */

HRESULT Direct3DSwapChain9_LSS::changeDisplayMode(const D3DPRESENT_PARAMETERS& presParams) {
  /*
  * Some of the following lines are adapted from source in the DXVK repo
  * at https://github.com/doitsujin/dxvk/blob/master/src/d3d9/d3d9_swapchain.cpp
  */

  //Change monitor's resolution
  DEVMODEW devMode = { };
  devMode.dmSize = sizeof(devMode);
  devMode.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT | DM_BITSPERPEL;
  devMode.dmPelsWidth = presParams.BackBufferWidth;
  devMode.dmPelsHeight = presParams.BackBufferHeight;
  devMode.dmBitsPerPel = getBytesFromFormat(presParams.BackBufferFormat) * 8;

  if (presParams.FullScreen_RefreshRateInHz != 0) {
    devMode.dmFields |= DM_DISPLAYFREQUENCY;
    devMode.dmDisplayFrequency = presParams.FullScreen_RefreshRateInHz;
  }

  HMONITOR monitor = GetDefaultMonitor();

  if (!bridge_util::SetMonitorDisplayMode(monitor, &devMode)) {
    Logger::warn("Error in setting monitor display mode!");
    return D3DERR_NOTAVAILABLE;
  }
  return D3D_OK;
}

Direct3DSwapChain9_LSS::~Direct3DSwapChain9_LSS() {
  if (m_monitor) {
    RestoreMonitorDisplayMode();
    m_monitor = nullptr;
  }
  gSwapChainMapMutex.lock();
  if (gSwapChainMap.find(m_window) != gSwapChainMap.end()) {
    if (gSwapChainMap[m_window].swapChainId == this->getId()) {
      gSwapChainMap.erase(m_window);
    }
  }
  gSwapChainMapMutex.unlock();
}

HRESULT Direct3DSwapChain9_LSS::reset(const D3DPRESENT_PARAMETERS &pPresentParams) {
  /*
  * Some of the following lines are adapted from source in the DXVK repo
  * at https://github.com/doitsujin/dxvk/blob/master/src/wsi/win32/wsi_window_win32.cpp
  */

  D3DPRESENT_PARAMETERS prevPresentParams = m_pDevice->getPreviousPresentParameter();
  bool changeFullscreen = (prevPresentParams.Windowed != pPresentParams.Windowed);
  const bool modifyWindow = !(m_behaviorFlag & D3DCREATE_NOWINDOWCHANGES);
  if (pPresentParams.Windowed) {
    if (modifyWindow && changeFullscreen) {
      if (!IsWindow(m_window)) {
        return D3DERR_INVALIDCALL;
      }

      if (m_monitor == nullptr || !bridge_util::RestoreMonitorDisplayMode())
        Logger::warn("Failed to restore display mode");

      m_monitor = nullptr;
    }
  }
  else {
    if (modifyWindow) {
      if (FAILED(changeDisplayMode(pPresentParams))) {
        Logger::warn("Failed to change display mode");
        return D3DERR_INVALIDCALL;
      }

      if (changeFullscreen) {
        m_monitor = bridge_util::GetDefaultMonitor();
      }
      // Move the window so that it covers the entire output
      RECT rect;
      bridge_util::GetMonitorRect(bridge_util::GetDefaultMonitor(), &rect);

      ::SetWindowPos(m_window, HWND_TOP,
        rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top,
        SWP_FRAMECHANGED | SWP_SHOWWINDOW | SWP_NOACTIVATE | SWP_ASYNCWINDOWPOS);
      Logger::info(format_string("Window's position is reset. New window's position - Left: %d, Top: %d, Right: %d, Bottom: %d", rect.left, rect.top, rect.right, rect.bottom));
    }
  }
  return D3D_OK;
}

HRESULT Direct3DSwapChain9_LSS::QueryInterface(REFIID riid, LPVOID* ppvObj) {
  LogFunctionCall();
  if (ppvObj == nullptr) {
    return E_POINTER;
  }

  *ppvObj = nullptr;

  if (riid == __uuidof(IUnknown)
    || riid == __uuidof(IDirect3DSwapChain9)) {
    *ppvObj = bridge_cast<IDirect3DSwapChain9*>(this);
    AddRef();
    return S_OK;
  }
  return E_NOINTERFACE;
}

ULONG Direct3DSwapChain9_LSS::AddRef() {
  LogFunctionCall();
  return Direct3DContainer9_LSS::AddRef();
}

ULONG Direct3DSwapChain9_LSS::Release() {
  LogFunctionCall();
  return Direct3DContainer9_LSS::Release();
}

void Direct3DSwapChain9_LSS::onDestroy() {
  ClientMessage c(Commands::IDirect3DSwapChain9_Destroy, getId());
}

HRESULT Direct3DSwapChain9_LSS::Present(CONST RECT* pSourceRect, CONST RECT* pDestRect,
                                        HWND hDestWindowOverride, CONST RGNDATA* pDirtyRegion,
                                        DWORD dwFlags) {
  ZoneScoped;
  LogFunctionCall();
#ifdef ENABLE_PRESENT_SEMAPHORE_TRACE
  Logger::trace(format_string("Present(): ClientMessage counter is at %d.", ClientMessage::get_counter()));
#endif
  ClientMessage::reset_counter();
  gSceneState = WaitBeginScene;

  // If the bridge was disabled in the meantime for some reason we want to bail
  // out here so we don't spend time waiting on the Present semaphore or trying
  // to send keyboard state to the server.
  if (!gbBridgeRunning) {
    return D3D_OK;
  }

  // Send present first
  {
    ClientMessage c(Commands::IDirect3DSwapChain9_Present, getId());
    c.send_data(sizeof(RECT), (void*) pSourceRect);
    c.send_data(sizeof(RECT), (void*) pDestRect);
    c.send_data((uint32_t) hDestWindowOverride);
    c.send_data(sizeof(RGNDATA), (void*) pDirtyRegion);
    c.send_data(dwFlags);
  }

  extern HRESULT syncOnPresent();
  const auto syncResult = syncOnPresent();
  if (syncResult == ERROR_SEM_TIMEOUT) {
    return ERROR_SEM_TIMEOUT;
  }

  FrameMark;

  return D3D_OK;
}

HRESULT Direct3DSwapChain9_LSS::GetFrontBufferData(IDirect3DSurface9* pDestSurface) {
  LogFunctionCall();

  const auto pLssDestinationSurface = bridge_cast<Direct3DSurface9_LSS*>(pDestSurface);
  const auto pIDestinationSurface = pLssDestinationSurface->D3D<IDirect3DSurface9>();

  UID currentUID = 0;
  {
    ClientMessage c(Commands::IDirect3DSwapChain9_GetFrontBufferData, getId());
    currentUID = c.get_uid();
    c.send_data((uint32_t) pIDestinationSurface);
  }

  return copyServerSurfaceRawData(pLssDestinationSurface, currentUID);
}

HRESULT Direct3DSwapChain9_LSS::GetBackBuffer(UINT iBackBuffer, D3DBACKBUFFER_TYPE Type,
                                              IDirect3DSurface9** ppBackBuffer) {
  ZoneScoped;
  LogFunctionCall();

  if (ppBackBuffer == nullptr) {
    return D3DERR_INVALIDCALL;
  }

  if (auto surface = getChild(iBackBuffer)) {
    surface->AddRef();
    *ppBackBuffer = surface;
    return D3D_OK;
  }

  // Insert our own IDirect3DSurface9 interface implementation
  D3DSURFACE_DESC desc;
  desc.Width = m_presParam.BackBufferWidth;
  desc.Height = m_presParam.BackBufferHeight;
  desc.MultiSampleQuality = m_presParam.MultiSampleQuality;
  desc.MultiSampleType = m_presParam.MultiSampleType;
  desc.Format = m_presParam.BackBufferFormat;
  desc.Usage = D3DUSAGE_RENDERTARGET;
  desc.Pool = D3DPOOL_DEFAULT;
  desc.Type = D3DRTYPE_SURFACE;

  Direct3DSurface9_LSS* pLssSurface = trackWrapper(new Direct3DSurface9_LSS(m_pDevice, this, desc, true));
  setChild(iBackBuffer, pLssSurface);
    
  (*ppBackBuffer) = pLssSurface;

  UID currentUID = 0;
  // Add handles for backbuffer
  {
    ClientMessage c(Commands::IDirect3DSwapChain9_GetBackBuffer, getId());
    currentUID = c.get_uid();
    c.send_data(iBackBuffer);
    c.send_data(Type);
    c.send_data(pLssSurface->getId());
  }
  
  WAIT_FOR_OPTIONAL_SERVER_RESPONSE("GetBackBuffer()", D3DERR_INVALIDCALL, currentUID);
}

HRESULT Direct3DSwapChain9_LSS::GetRasterStatus(D3DRASTER_STATUS* pRasterStatus) {
  LogFunctionCall();
  // NOTE: Borrowed from DXVK
  // 
  // We could use D3DKMTGetScanLine but Wine doesn't implement that.
  // So... we lie here and make some stuff up
  // enough that it makes games work.

  // Assume there's 20 lines in a vBlank.
  constexpr uint32_t vBlankLineCount = 20;

  if (pRasterStatus == nullptr)
    return D3DERR_INVALIDCALL;

  D3DDISPLAYMODEEX mode;
  mode.Size = sizeof(mode);
  if (m_pDevice->GetDisplayModeEx(0, &mode, nullptr) != S_OK)
    return D3DERR_INVALIDCALL;

  uint32_t scanLineCount = mode.Height + vBlankLineCount;

  auto nowUs = std::chrono::time_point_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now()).time_since_epoch();

  auto frametimeUs = std::chrono::microseconds(1000000u / mode.RefreshRate);
  auto scanLineUs = frametimeUs / scanLineCount;

  pRasterStatus->ScanLine = (nowUs % frametimeUs) / scanLineUs;
  pRasterStatus->InVBlank = pRasterStatus->ScanLine >= mode.Height;

  if (pRasterStatus->InVBlank)
    pRasterStatus->ScanLine = 0;

  return D3D_OK;
}

HRESULT Direct3DSwapChain9_LSS::GetDisplayMode(D3DDISPLAYMODE* pMode) {
  LogFunctionCall();
  if (pMode == nullptr) {
    return D3DERR_INVALIDCALL;
  }
  return m_pDevice->GetDisplayMode(0, pMode);
}

HRESULT Direct3DSwapChain9_LSS::GetDevice(IDirect3DDevice9** ppDevice) {
  LogFunctionCall();
  if (ppDevice == nullptr) {
    return D3DERR_INVALIDCALL;
  }
  m_pDevice->AddRef();
  (*ppDevice) = m_pDevice;
  return D3D_OK;
}

HRESULT Direct3DSwapChain9_LSS::GetPresentParameters(D3DPRESENT_PARAMETERS* pPresentationParameters) {
  LogFunctionCall();
  if (pPresentationParameters == nullptr)
    return D3DERR_INVALIDCALL;
  *pPresentationParameters = m_presParam;
  return D3D_OK;
}