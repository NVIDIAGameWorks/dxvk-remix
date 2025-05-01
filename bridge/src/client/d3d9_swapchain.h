/*
 * Copyright (c) 2023-2024, NVIDIA CORPORATION. All rights reserved.
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

#include "d3d9_resource.h"
#include "base.h"
#include "util_monitor.h"
#include <algorithm>

class Direct3DSwapChain9_LSS: public Direct3DContainer9_LSS<D3DBase<IDirect3DSwapChain9>, Direct3DSurface9_LSS> {
  D3DPRESENT_PARAMETERS m_presParam;
  HMONITOR m_monitor = nullptr;
  HWND m_window = nullptr;
  void onDestroy() override;
  DWORD m_behaviorFlag;
public:
  Direct3DSwapChain9_LSS(BaseDirect3DDevice9Ex_LSS* const pDevice,
                         const D3DPRESENT_PARAMETERS& presParam)
    : Direct3DContainer9_LSS(nullptr, pDevice)
    , m_presParam(sanitizePresentationParameters(presParam, pDevice->getCreateParams())) {
    m_window = m_presParam.hDeviceWindow;
    if (!presParam.Windowed) {
      m_monitor = bridge_util::GetDefaultMonitor();
    }
    m_behaviorFlag = pDevice->getCreateParams().BehaviorFlags;

    m_children.resize(m_presParam.BackBufferCount);
    {
      ClientMessage c(Commands::IDirect3DDevice9Ex_LinkSwapchain, pDevice->getId());
      c.send_data(getId());
    }
    for (size_t childIdx = 0; childIdx < m_children.size(); ++childIdx) {
      const D3DSURFACE_DESC backBufferDesc {
        m_presParam.BackBufferFormat,
        D3DRTYPE_SURFACE,
        D3DUSAGE_RENDERTARGET,
        D3DPOOL_DEFAULT,
        m_presParam.MultiSampleType,
        m_presParam.MultiSampleQuality,
        m_presParam.BackBufferWidth,
        m_presParam.BackBufferHeight
      };
      auto* const pLssBackBuffer = trackWrapper(new Direct3DSurface9_LSS(pDevice, this, backBufferDesc, true));

      setChild(childIdx, pLssBackBuffer);
      UID currentUID = 0;
      {
        ClientMessage c(Commands::IDirect3DSwapChain9_GetBackBuffer, getId());
        currentUID = c.get_uid();
        c.send_data(childIdx);
        c.send_data(D3DBACKBUFFER_TYPE_MONO);
        c.send_data(pLssBackBuffer->getId());
      }
      if (GlobalOptions::getSendAllServerResponses()) {
        const uint32_t timeoutMs = GlobalOptions::getAckTimeout();
        if (Result::Success != DeviceBridge::waitForCommand(Commands::Bridge_Response, timeoutMs, nullptr, true, currentUID)) {
          Logger::err("Direct3DSwapChain9_LSS() failed with : no response from server.");
        }
        HRESULT res = (HRESULT) DeviceBridge::get_data();
        DeviceBridge::pop_front();
      }
    }
  }

  static D3DPRESENT_PARAMETERS sanitizePresentationParameters(const D3DPRESENT_PARAMETERS& presParam, const D3DDEVICE_CREATION_PARAMETERS& createParams) {
    D3DPRESENT_PARAMETERS localPresParam = presParam;
    // 0 is treated as 1, as per the spec.
    localPresParam.BackBufferCount = std::max(localPresParam.BackBufferCount, 1u);
    if (ClientOptions::getForceWindowed()) {
      localPresParam.Windowed = TRUE;
      localPresParam.FullScreen_RefreshRateInHz = 0;
    }
    // NOTE(https://learn.microsoft.com/en-us/windows/win32/direct3d9/d3dpresent-parameters)
    //  "If this handle is NULL, the focus window will be taken."
    if (localPresParam.hDeviceWindow == NULL)
      localPresParam.hDeviceWindow = createParams.hFocusWindow;

    // NOTE(https://learn.microsoft.com/en-us/windows/win32/direct3d9/d3dpresent-parameters)
    //  If Windowed is TRUE and either BackBufferWidth or BackBufferHeight is zero, the corresponding dimension of the client area of the hDeviceWindow
    if (localPresParam.Windowed && (presParam.BackBufferWidth == 0 || presParam.BackBufferHeight == 0)) {
      RECT clientArea;
      GetClientRect(localPresParam.hDeviceWindow, &clientArea);
      localPresParam.BackBufferWidth = clientArea.right - clientArea.left;
      localPresParam.BackBufferHeight = clientArea.bottom - clientArea.top;
    }

    return localPresParam;
  }

  const D3DPRESENT_PARAMETERS& getPresentationParameters() {
    return m_presParam;
  }

  const D3DDEVICE_CREATION_PARAMETERS& getDeviceCreationParameters() {
    return m_pDevice->getCreateParams();
  }

  void setPresentationParameters(const D3DPRESENT_PARAMETERS& presParam) {
    m_presParam = sanitizePresentationParameters(presParam, m_pDevice->getCreateParams());
  }

  ~Direct3DSwapChain9_LSS();
  HRESULT changeDisplayMode(const D3DPRESENT_PARAMETERS& presParams);
  HRESULT reset(const D3DPRESENT_PARAMETERS &pPresentParams);
  /*** IUnknown methods ***/
  STDMETHOD(QueryInterface)(THIS_ REFIID riid, void** ppvObj);
  STDMETHOD_(ULONG, AddRef)(THIS);
  STDMETHOD_(ULONG, Release)(THIS);

  /*** IDirect3DSwapChain9 methods ***/
  STDMETHOD(Present)(THIS_ CONST RECT* pSourceRect, CONST RECT* pDestRect, HWND hDestWindowOverride, CONST RGNDATA* pDirtyRegion, DWORD dwFlags);
  STDMETHOD(GetFrontBufferData)(THIS_ IDirect3DSurface9* pDestSurface);
  STDMETHOD(GetBackBuffer)(THIS_ UINT iBackBuffer, D3DBACKBUFFER_TYPE Type, IDirect3DSurface9** ppBackBuffer);
  STDMETHOD(GetRasterStatus)(THIS_ D3DRASTER_STATUS* pRasterStatus);
  STDMETHOD(GetDisplayMode)(THIS_ D3DDISPLAYMODE* pMode);
  STDMETHOD(GetDevice)(THIS_ IDirect3DDevice9** ppDevice);
  STDMETHOD(GetPresentParameters)(THIS_ D3DPRESENT_PARAMETERS* pPresentationParameters);
};