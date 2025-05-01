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
#ifndef D3D9_LSS_H_
#define D3D9_LSS_H_

#include <d3d9.h>

#include "base.h"
#include "d3d9_base_texture.h"
#include "d3d9_resource.h"
#include "util_process.h"

#include <unordered_map>
#include <vector>
#include <tuple>

using namespace bridge_util;

#define DLLEXPORT

typedef IDirect3D9* (WINAPI* D3DC9)(UINT);
extern D3DC9 orig_Direct3DCreate9;

typedef HRESULT(WINAPI* LPDirect3DCreate9Ex)(UINT SDKVersion, IDirect3D9Ex** ppD3D);
extern LPDirect3DCreate9Ex orig_Direct3DCreate9Ex;

extern Process* gpServer;
extern bool gbBridgeRunning;

void SetupExceptionHandler();

// Some games do not use Begin-/EndScene which is required by some d3d9 API calls such as D3DXFont::DrawText()
// This state will be used to track whether we need to spoof it in games
enum SceneState {
  WaitBeginScene = 0,
  SceneInProgress = 1,
  SceneEnded = 2
};
extern SceneState gSceneState;

/*
 * Direct3D9Create9* LSS Implementations
 */
HRESULT LssDirect3DCreate9Ex(UINT SDKVersion, IDirect3D9Ex** ppDeviceEx);
IDirect3D9* LssDirect3DCreate9(UINT SDKVersion);

/*
 * IDirect3D9 LSS Interceptor Class
 */
class Direct3D9Ex_LSS: public D3DBase<IDirect3D9Ex> {
  const bool m_ex;
  UINT m_adapterCount = 0;
  std::unordered_map<size_t, D3DADAPTER_IDENTIFIER9> m_adapterIdentifiers;
  std::unordered_map<size_t, UINT> m_adapterModeCount;
  std::unordered_map<size_t, D3DDISPLAYMODE> m_enumAdapterMode;
  std::unordered_map<size_t, D3DCAPS9> m_deviceCaps;
  std::unordered_map<UINT, D3DDISPLAYMODE> m_adapterDisplayMode;
  void onDestroy() override;

public:
  Direct3D9Ex_LSS(IDirect3D9Ex* const pDevice)
    : D3DBase(pDevice, nullptr)
    , m_ex(true) {
  }

  Direct3D9Ex_LSS(IDirect3D9* const pDevice)
    : D3DBase((IDirect3D9Ex*) pDevice, nullptr)
    , m_ex(false) {
  }

  /*** IUnknown methods ***/
  STDMETHOD(QueryInterface)(THIS_ REFIID riid, void** ppvObj);
  STDMETHOD_(ULONG, AddRef)(THIS);
  STDMETHOD_(ULONG, Release)(THIS);

  /*** IDirect3D9 methods ***/
  STDMETHOD(RegisterSoftwareDevice)(THIS_ void* pInitializeFunction);
  STDMETHOD_(UINT, GetAdapterCount)(THIS);
  STDMETHOD(GetAdapterIdentifier)(THIS_ UINT Adapter, DWORD Flags, D3DADAPTER_IDENTIFIER9* pIdentifier);
  STDMETHOD_(UINT, GetAdapterModeCount)(THIS_ UINT Adapter, D3DFORMAT Format);
  STDMETHOD(EnumAdapterModes)(THIS_ UINT Adapter, D3DFORMAT Format, UINT Mode, D3DDISPLAYMODE* pMode);
  STDMETHOD(GetAdapterDisplayMode)(THIS_ UINT Adapter, D3DDISPLAYMODE* pMode);
  STDMETHOD(CheckDeviceType)(THIS_ UINT Adapter, D3DDEVTYPE DevType, D3DFORMAT AdapterFormat, D3DFORMAT BackBufferFormat, BOOL bWindowed);
  STDMETHOD(CheckDeviceFormat)(THIS_ UINT Adapter, D3DDEVTYPE DeviceType, D3DFORMAT AdapterFormat, DWORD Usage, D3DRESOURCETYPE RType, D3DFORMAT CheckFormat);
  STDMETHOD(CheckDeviceMultiSampleType)(THIS_ UINT Adapter, D3DDEVTYPE DeviceType, D3DFORMAT SurfaceFormat, BOOL Windowed, D3DMULTISAMPLE_TYPE MultiSampleType, DWORD* pQualityLevels);
  STDMETHOD(CheckDepthStencilMatch)(THIS_ UINT Adapter, D3DDEVTYPE DeviceType, D3DFORMAT AdapterFormat, D3DFORMAT RenderTargetFormat, D3DFORMAT DepthStencilFormat);
  STDMETHOD(CheckDeviceFormatConversion)(THIS_ UINT Adapter, D3DDEVTYPE DeviceType, D3DFORMAT SourceFormat, D3DFORMAT TargetFormat);
  STDMETHOD(GetDeviceCaps)(THIS_ UINT Adapter, D3DDEVTYPE DeviceType, D3DCAPS9* pCaps);
  STDMETHOD_(HMONITOR, GetAdapterMonitor)(THIS_ UINT Adapter);
  STDMETHOD(CreateDevice)(THIS_ UINT Adapter, D3DDEVTYPE DeviceType, HWND hFocusWindow, DWORD BehaviorFlags, D3DPRESENT_PARAMETERS* pPresentationParameters, IDirect3DDevice9** ppReturnedDeviceInterface);
  STDMETHOD_(UINT, GetAdapterModeCountEx)(THIS_ UINT Adapter, CONST D3DDISPLAYMODEFILTER* pFilter);
  STDMETHOD(EnumAdapterModesEx)(THIS_ UINT Adapter, CONST D3DDISPLAYMODEFILTER* pFilter, UINT Mode, D3DDISPLAYMODEEX* pMode);
  STDMETHOD(GetAdapterDisplayModeEx)(THIS_ UINT Adapter, D3DDISPLAYMODEEX* pMode, D3DDISPLAYROTATION* pRotation);
  STDMETHOD(CreateDeviceEx)(THIS_ UINT Adapter, D3DDEVTYPE DeviceType, HWND hFocusWindow, DWORD BehaviorFlags, D3DPRESENT_PARAMETERS* pPresentationParameters, D3DDISPLAYMODEEX* pFullscreenDisplayMode, IDirect3DDevice9Ex** ppReturnedDeviceInterface);
  STDMETHOD(GetAdapterLUID)(THIS_ UINT Adapter, LUID* pLUID);
private:
  static std::tuple<HRESULT, BaseDirect3DDevice9Ex_LSS*> createDevice(
    const bool bExtended,
                        Direct3D9Ex_LSS* const pDirect3D,
    const D3DDEVICE_CREATION_PARAMETERS& createParams,
    const D3DPRESENT_PARAMETERS& presParam,
    const D3DDISPLAYMODEEX* const pFullscreenDisplayMode
  );
};

/*
 * IDirect3DStateBlock9 LSS Interceptor Class
 */
class Direct3DStateBlock9_LSS: public D3DBase<IDirect3DStateBlock9> {
  void onDestroy() override;
protected:
  BaseDirect3DDevice9Ex_LSS* const m_pDevice = nullptr;
public:
  Direct3DStateBlock9_LSS(BaseDirect3DDevice9Ex_LSS* const pDevice)
    : D3DBase((IDirect3DStateBlock9*) nullptr, pDevice)
    , m_pDevice(pDevice) {
  }

  friend BaseDirect3DDevice9Ex_LSS;

  /*** IUnknown methods ***/
  STDMETHOD(QueryInterface)(THIS_ REFIID riid, void** ppvObj);
  STDMETHOD_(ULONG, AddRef)(THIS);
  STDMETHOD_(ULONG, Release)(THIS);

  /*** IDirect3DStateBlock9 methods ***/
  STDMETHOD(GetDevice)(THIS_ IDirect3DDevice9** ppDevice);
  STDMETHOD(Capture)(THIS);
  STDMETHOD(Apply)(THIS);

  void LocalCapture();

  struct BaseDirect3DDevice9Ex_LSS::StateCaptureDirtyFlags m_dirtyFlags = { 0 };
  struct BaseDirect3DDevice9Ex_LSS::State m_captureState;
  void StateTransfer(const BaseDirect3DDevice9Ex_LSS::StateCaptureDirtyFlags& flags, BaseDirect3DDevice9Ex_LSS::State& src, BaseDirect3DDevice9Ex_LSS::State& dst);

};

#endif // D3D9_LSS_H_
