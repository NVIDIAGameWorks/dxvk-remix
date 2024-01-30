/*
* Copyright (c) 2021-2022, NVIDIA CORPORATION. All rights reserved.
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
#include "d3d9_interface.h"

#include "d3d9_monitor.h"
#include "d3d9_caps.h"
#include "d3d9_device.h"

#include <algorithm>

namespace dxvk {

// NV-DXVK start: external API
  D3D9InterfaceEx::D3D9InterfaceEx(bool bExtended, bool WithExternalSwapchain, bool WithDrawCallConversion)
    : m_instance    ( new DxvkInstance() )
    , m_extended    ( bExtended ) 
    , m_d3d9Options ( nullptr, m_instance->config() )
    , m_withExternalSwapchain { WithExternalSwapchain }
    , m_withDrawCallConversion { WithDrawCallConversion }  {
// NV-DXVK end
    // D3D9 doesn't enumerate adapters like physical adapters...
    // only as connected displays.

    // Let's create some "adapters" for the amount of displays we have.
    // We'll go through and match up displays -> our adapters in order.
    // If we run out of adapters, then we'll just make repeats of the first one.
    // We can't match up by names on Linux/Wine as they don't match at all
    // like on Windows, so this is our best option.
    if (m_d3d9Options.enumerateByDisplays) {
      DISPLAY_DEVICEA device = { };
      device.cb = sizeof(device);

      uint32_t adapterOrdinal = 0;
      uint32_t i = 0;
      while (::EnumDisplayDevicesA(nullptr, i++, &device, 0)) {
        // If we aren't attached, skip over.
        if (!(device.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP))
          continue;

        // If we are a mirror, skip over this device.
        if (device.StateFlags & DISPLAY_DEVICE_MIRRORING_DRIVER)
          continue;

        Rc<DxvkAdapter> adapter = adapterOrdinal >= m_instance->adapterCount()
          ? m_instance->enumAdapters(0)
          : m_instance->enumAdapters(adapterOrdinal);

        if (adapter != nullptr)
          m_adapters.emplace_back(this, adapter, adapterOrdinal++, i - 1);
      }
    }
    else
    {
      const uint32_t adapterCount = m_instance->adapterCount();
      m_adapters.reserve(adapterCount);

      for (uint32_t i = 0; i < adapterCount; i++)
        m_adapters.emplace_back(this, m_instance->enumAdapters(i), i, 0);
    }

    if (m_d3d9Options.dpiAware) {
      Logger::info("Process set as DPI aware");

      // NV-DXVK start: use SetProcessDpiAwareness
      static HINSTANCE shcore_dll = ::LoadLibraryA("shcore.dll");
      typedef HRESULT(WINAPI* PFN_SetProcessDpiAwareness)(int);
      if (PFN_SetProcessDpiAwareness SetProcessDpiAwarenessFn = (PFN_SetProcessDpiAwareness)::GetProcAddress(shcore_dll, "SetProcessDpiAwareness")) {
        const int PROCESS_PER_MONITOR_DPI_AWARE = 2;
        SetProcessDpiAwarenessFn(PROCESS_PER_MONITOR_DPI_AWARE);
      }
      else {
        SetProcessDPIAware();
      }
      // NV-DXVK end
    }
  }


  HRESULT STDMETHODCALLTYPE D3D9InterfaceEx::QueryInterface(REFIID riid, void** ppvObject) {
    if (ppvObject == nullptr)
      return E_POINTER;

    *ppvObject = nullptr;

    if (riid == __uuidof(IUnknown)
     || riid == __uuidof(IDirect3D9)
     || (m_extended && riid == __uuidof(IDirect3D9Ex))) {
      *ppvObject = ref(this);
      return S_OK;
    }

    Logger::warn("D3D9InterfaceEx::QueryInterface: Unknown interface query");
    Logger::warn(str::format(riid));
    return E_NOINTERFACE;
  }


  HRESULT STDMETHODCALLTYPE D3D9InterfaceEx::RegisterSoftwareDevice(void* pInitializeFunction) {
    Logger::warn("D3D9InterfaceEx::RegisterSoftwareDevice: Stub");
    return D3D_OK;
  }


  UINT STDMETHODCALLTYPE D3D9InterfaceEx::GetAdapterCount() {
    return UINT(m_adapters.size());
  }


  HRESULT STDMETHODCALLTYPE D3D9InterfaceEx::GetAdapterIdentifier(
          UINT                    Adapter,
          DWORD                   Flags,
          D3DADAPTER_IDENTIFIER9* pIdentifier) {
    if (auto* adapter = GetAdapter(Adapter))
      return adapter->GetAdapterIdentifier(Flags, pIdentifier);

    return D3DERR_INVALIDCALL;
  }


  UINT STDMETHODCALLTYPE D3D9InterfaceEx::GetAdapterModeCount(UINT Adapter, D3DFORMAT Format) {
    D3DDISPLAYMODEFILTER filter;
    filter.Size             = sizeof(D3DDISPLAYMODEFILTER);
    filter.Format           = Format;
    filter.ScanLineOrdering = D3DSCANLINEORDERING_PROGRESSIVE;
    
    return this->GetAdapterModeCountEx(Adapter, &filter);
  }


  HRESULT STDMETHODCALLTYPE D3D9InterfaceEx::GetAdapterDisplayMode(UINT Adapter, D3DDISPLAYMODE* pMode) {
    if (auto* adapter = GetAdapter(Adapter)) {
      D3DDISPLAYMODEEX modeEx = { };
      modeEx.Size = sizeof(D3DDISPLAYMODEEX);
      HRESULT hr = adapter->GetAdapterDisplayModeEx(&modeEx, nullptr);

      if (FAILED(hr))
        return hr;

      pMode->Width       = modeEx.Width;
      pMode->Height      = modeEx.Height;
      pMode->RefreshRate = modeEx.RefreshRate;
      pMode->Format      = modeEx.Format;

      return D3D_OK;
    }

    return D3DERR_INVALIDCALL;
  }


  HRESULT STDMETHODCALLTYPE D3D9InterfaceEx::CheckDeviceType(
          UINT       Adapter,
          D3DDEVTYPE DevType,
          D3DFORMAT  AdapterFormat,
          D3DFORMAT  BackBufferFormat,
          BOOL       bWindowed) {
    if (auto* adapter = GetAdapter(Adapter))
      return adapter->CheckDeviceType(
        DevType, EnumerateFormat(AdapterFormat),
        EnumerateFormat(BackBufferFormat), bWindowed);

    return D3DERR_INVALIDCALL;
  }


  HRESULT STDMETHODCALLTYPE D3D9InterfaceEx::CheckDeviceFormat(
          UINT            Adapter,
          D3DDEVTYPE      DeviceType,
          D3DFORMAT       AdapterFormat,
          DWORD           Usage,
          D3DRESOURCETYPE RType,
          D3DFORMAT       CheckFormat) {
    if (auto* adapter = GetAdapter(Adapter))
      return adapter->CheckDeviceFormat(
        DeviceType, EnumerateFormat(AdapterFormat),
        Usage, RType,
        EnumerateFormat(CheckFormat));

    return D3DERR_INVALIDCALL;
  }


  HRESULT STDMETHODCALLTYPE D3D9InterfaceEx::CheckDeviceMultiSampleType(
          UINT                Adapter,
          D3DDEVTYPE          DeviceType,
          D3DFORMAT           SurfaceFormat,
          BOOL                Windowed,
          D3DMULTISAMPLE_TYPE MultiSampleType,
          DWORD*              pQualityLevels) { 
    if (auto* adapter = GetAdapter(Adapter))
      return adapter->CheckDeviceMultiSampleType(
        DeviceType, EnumerateFormat(SurfaceFormat),
        Windowed, MultiSampleType,
        pQualityLevels);

    return D3DERR_INVALIDCALL;
  }


  HRESULT STDMETHODCALLTYPE D3D9InterfaceEx::CheckDepthStencilMatch(
          UINT       Adapter,
          D3DDEVTYPE DeviceType,
          D3DFORMAT  AdapterFormat,
          D3DFORMAT  RenderTargetFormat,
          D3DFORMAT  DepthStencilFormat) {
    if (auto* adapter = GetAdapter(Adapter))
      return adapter->CheckDepthStencilMatch(
        DeviceType, EnumerateFormat(AdapterFormat),
        EnumerateFormat(RenderTargetFormat),
        EnumerateFormat(DepthStencilFormat));

    return D3DERR_INVALIDCALL;
  }


  HRESULT STDMETHODCALLTYPE D3D9InterfaceEx::CheckDeviceFormatConversion(
          UINT       Adapter,
          D3DDEVTYPE DeviceType,
          D3DFORMAT  SourceFormat,
          D3DFORMAT  TargetFormat) {
    if (auto* adapter = GetAdapter(Adapter))
      return adapter->CheckDeviceFormatConversion(
        DeviceType, EnumerateFormat(SourceFormat),
        EnumerateFormat(TargetFormat));

    return D3DERR_INVALIDCALL;
  }


  HRESULT STDMETHODCALLTYPE D3D9InterfaceEx::GetDeviceCaps(
          UINT       Adapter,
          D3DDEVTYPE DeviceType,
          D3DCAPS9*  pCaps) {
    if (auto* adapter = GetAdapter(Adapter))
      return adapter->GetDeviceCaps(
        DeviceType, pCaps); 

    return D3DERR_INVALIDCALL;
  }


  HMONITOR STDMETHODCALLTYPE D3D9InterfaceEx::GetAdapterMonitor(UINT Adapter) {
    if (auto* adapter = GetAdapter(Adapter))
      return adapter->GetMonitor();

    return nullptr;
  }


  HRESULT STDMETHODCALLTYPE D3D9InterfaceEx::CreateDevice(
          UINT                   Adapter,
          D3DDEVTYPE             DeviceType,
          HWND                   hFocusWindow,
          DWORD                  BehaviorFlags,
          D3DPRESENT_PARAMETERS* pPresentationParameters,
          IDirect3DDevice9**     ppReturnedDeviceInterface) {
    return this->CreateDeviceEx(
      Adapter,
      DeviceType,
      hFocusWindow,
      BehaviorFlags,
      pPresentationParameters,
      nullptr, // <-- pFullscreenDisplayMode
      reinterpret_cast<IDirect3DDevice9Ex**>(ppReturnedDeviceInterface));
  }


  HRESULT STDMETHODCALLTYPE D3D9InterfaceEx::EnumAdapterModes(
          UINT            Adapter,
          D3DFORMAT       Format,
          UINT            Mode,
          D3DDISPLAYMODE* pMode) {
    if (pMode == nullptr)
      return D3DERR_INVALIDCALL;

    D3DDISPLAYMODEFILTER filter;
    filter.Format           = Format;
    filter.ScanLineOrdering = D3DSCANLINEORDERING_PROGRESSIVE;
    filter.Size             = sizeof(D3DDISPLAYMODEFILTER);

    D3DDISPLAYMODEEX modeEx = { };
    modeEx.Size = sizeof(D3DDISPLAYMODEEX);
    HRESULT hr = this->EnumAdapterModesEx(Adapter, &filter, Mode, &modeEx);

    if (FAILED(hr))
      return hr;

    pMode->Width       = modeEx.Width;
    pMode->Height      = modeEx.Height;
    pMode->RefreshRate = modeEx.RefreshRate;
    pMode->Format      = modeEx.Format;

    return D3D_OK;
  }


  // Ex Methods


  UINT STDMETHODCALLTYPE D3D9InterfaceEx::GetAdapterModeCountEx(UINT Adapter, CONST D3DDISPLAYMODEFILTER* pFilter) {
    if (auto* adapter = GetAdapter(Adapter))
      return adapter->GetAdapterModeCountEx(pFilter);

    return 0;
  }


  HRESULT STDMETHODCALLTYPE D3D9InterfaceEx::EnumAdapterModesEx(
          UINT                  Adapter,
    const D3DDISPLAYMODEFILTER* pFilter,
          UINT                  Mode,
          D3DDISPLAYMODEEX*     pMode) {
    if (auto* adapter = GetAdapter(Adapter))
      return adapter->EnumAdapterModesEx(pFilter, Mode, pMode);

    return D3DERR_INVALIDCALL;
  }


  HRESULT STDMETHODCALLTYPE D3D9InterfaceEx::GetAdapterDisplayModeEx(
          UINT                Adapter,
          D3DDISPLAYMODEEX*   pMode,
          D3DDISPLAYROTATION* pRotation) {
    if (auto* adapter = GetAdapter(Adapter))
      return adapter->GetAdapterDisplayModeEx(pMode, pRotation);

    return D3DERR_INVALIDCALL;
  }


  HRESULT STDMETHODCALLTYPE D3D9InterfaceEx::CreateDeviceEx(
          UINT                   Adapter,
          D3DDEVTYPE             DeviceType,
          HWND                   hFocusWindow,
          DWORD                  BehaviorFlags,
          D3DPRESENT_PARAMETERS* pPresentationParameters,
          D3DDISPLAYMODEEX*      pFullscreenDisplayMode,
          IDirect3DDevice9Ex**   ppReturnedDeviceInterface) {
    InitReturnPtr(ppReturnedDeviceInterface);

    if (ppReturnedDeviceInterface == nullptr
    || pPresentationParameters    == nullptr)
      return D3DERR_INVALIDCALL;

    // NV-DXVK start: adapter override conf
    if (m_d3d9Options.adapterOverride != -1) {
      Adapter = (UINT) std::min((int) m_adapters.size() - 1, std::max(0, m_d3d9Options.adapterOverride));
    }
    // NV-DXVK end

    auto* adapter = GetAdapter(Adapter);

    if (adapter == nullptr)
      return D3DERR_INVALIDCALL;

    auto dxvkAdapter = adapter->GetDXVKAdapter();

    try {
      auto dxvkDevice = dxvkAdapter->createDevice(m_instance, D3D9DeviceEx::GetDeviceFeatures(dxvkAdapter));

      auto* device = new D3D9DeviceEx(
        this,
        adapter,
        DeviceType,
        hFocusWindow,
        BehaviorFlags,
        dxvkDevice,
// NV-DXVK start: external API
        m_withExternalSwapchain,
        m_withDrawCallConversion);
// NV-DXVK end

      HRESULT hr = device->InitialReset(pPresentationParameters, pFullscreenDisplayMode);

      if (FAILED(hr))
        return hr;

      *ppReturnedDeviceInterface = ref(device);
    }
// NV-DXVK start: provide error code on exception
    catch (const DxvkErrorWithId& e) {
      Logger::err(e.message());
      return e.id();
    }
// NV-DXVK end
    catch (const DxvkError& e) {
      Logger::err(e.message());
      return D3DERR_NOTAVAILABLE;
    }

    return D3D_OK;
  }


  HRESULT STDMETHODCALLTYPE D3D9InterfaceEx::GetAdapterLUID(UINT Adapter, LUID* pLUID) {
    if (auto* adapter = GetAdapter(Adapter))
      return adapter->GetAdapterLUID(pLUID);

    return D3DERR_INVALIDCALL;
  }

}