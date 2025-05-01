/*
 * Copyright (c) 2023, NVIDIA CORPORATION. All rights reserved.
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
#include "d3d9_lss.h"

#include "d3d9_device.h"
#include "d3d9_swapchain.h"
#include "remix_state.h"

#include "util_bridge_assert.h"
#include "util_modulecommand.h"

#undef WAIT_FOR_SERVER_RESPONSE
#define WAIT_FOR_SERVER_RESPONSE(func, value, uidVal) \
  { \
    const uint32_t timeoutMs = GlobalOptions::getAckTimeout(); \
    if (Result::Success != ModuleBridge::waitForCommand(Commands::Bridge_Response, timeoutMs, nullptr, true, uidVal)) { \
      Logger::err(func " failed with: no response from server."); \
      return value; \
    } \
  }

// This is a modified version of the original hash_combine function
// from Boost. See: https://github.com/boostorg/container_hash
template <typename T, typename... Rest>
void hash_combine(std::size_t& seed, const T& v, const Rest&... rest) {
  seed ^= std::hash<T>{}(v) +0x9e3779b9 + (seed << 6) + (seed >> 2);
  (hash_combine(seed, rest), ...);
}

#define GET_HASH(hashname, ...) \
  size_t hashname = 0; \
  hash_combine(hashname, __VA_ARGS__);

HRESULT Direct3D9Ex_LSS::QueryInterface(REFIID riid, LPVOID* ppvObj) {
  LogFunctionCall();
  if (ppvObj == nullptr) {
    return E_POINTER;
  }

  *ppvObj = nullptr;

  if (riid == __uuidof(IUnknown)
    || riid == __uuidof(IDirect3D9)
    || (m_ex && riid == __uuidof(IDirect3D9Ex))) {
    *ppvObj = bridge_cast<IDirect3D9*>(this);
    AddRef();
    return S_OK;
  }

  return E_NOINTERFACE;
}

ULONG Direct3D9Ex_LSS::AddRef() {
  LogFunctionCall();
  // Let the server control its own device lifetime completely - no push
  return D3DBase::AddRef();
}

ULONG Direct3D9Ex_LSS::Release() {
  LogFunctionCall();
  // Let the server control its own device lifetime completely - no push
  return D3DBase::Release();
}

void Direct3D9Ex_LSS::onDestroy() {
  // Make sure server processed all pending commands
  if (ModuleBridge::ensureQueueEmpty() != Result::Success) {
    Logger::warn("Command queue was not flushed at Direct3D module eviction.");
  }

  {
    ModuleClientCommand { Commands::IDirect3D9Ex_Destroy, getId() };
  }

  // Make sure server consumed IDirect3D9Ex_Destroy
  ModuleBridge::ensureQueueEmpty();
}

HRESULT Direct3D9Ex_LSS::RegisterSoftwareDevice(void* pInitializeFunction) {
  LogMissingFunctionCall();
  return D3DERR_NOTAVAILABLE;
}

UINT Direct3D9Ex_LSS::GetAdapterCount() {
  LogFunctionCall();

  // Return cached result if available
  if (m_adapterCount != 0) {
    return m_adapterCount;
  }

  UID currentUID = 0;
  // Send command to server and wait for response
  {
    ModuleClientCommand c(Commands::IDirect3D9Ex_GetAdapterCount);
    currentUID = c.get_uid();
  }
  WAIT_FOR_SERVER_RESPONSE("GetAdapterCount()", 1, currentUID);

  m_adapterCount = (UINT) ModuleBridge::get_data();
  ModuleBridge::pop_front();
  return m_adapterCount;
}

HRESULT Direct3D9Ex_LSS::GetAdapterIdentifier(UINT Adapter, DWORD Flags, D3DADAPTER_IDENTIFIER9* pIdentifier) {
  LogFunctionCall();

  if (pIdentifier == nullptr) {
    return D3DERR_INVALIDCALL;
  }

  // Check Adapter Count if cached
  if (m_adapterCount != 0 && Adapter >= m_adapterCount) {
    return D3DERR_INVALIDCALL;
  }

  GET_HASH(key, Adapter, Flags);
  if (m_adapterIdentifiers.find(key) != m_adapterIdentifiers.end()) {
    *pIdentifier = m_adapterIdentifiers[key];
    return S_OK;
  }

  UID currentUID = 0;
  // Send command to server and wait for response
  {
    ModuleClientCommand c(Commands::IDirect3D9Ex_GetAdapterIdentifier);
    currentUID = c.get_uid();
    c.send_many(Adapter, Flags);
  }
  WAIT_FOR_SERVER_RESPONSE("GetAdapterIdentifier()", E_FAIL, currentUID);

  HRESULT hresult = ModuleBridge::get_data();
  if (SUCCEEDED(hresult)) {
    D3DADAPTER_IDENTIFIER9& adapterIdentifier = m_adapterIdentifiers[key];
    uint32_t len = ModuleBridge::copy_data(adapterIdentifier, false);
    // The structs are essentially the same, but the x64 side adds 4 extra bytes for padding
    if (len != (sizeof(D3DADAPTER_IDENTIFIER9) + 4) && len != 0) {
      Logger::err("GetAdapterIdentifier() failed due to issue with data returned from server.");
      hresult = D3DERR_INVALIDCALL;
    }
    *pIdentifier = adapterIdentifier;
  }
  ModuleBridge::pop_front();
  return hresult;
}

UINT Direct3D9Ex_LSS::GetAdapterModeCount(UINT Adapter, D3DFORMAT Format) {
  LogFunctionCall();

  // Check Adapter Count if cached
  if (m_adapterCount != 0 && Adapter >= m_adapterCount) {
    return 0;
  }

  GET_HASH(key, Adapter, Format);
  if (m_adapterModeCount.find(key) != m_adapterModeCount.end()) {
    return m_adapterModeCount[key];
  }

  UID currentUID = 0;
  // Send command to server and wait for response
  {
    ModuleClientCommand c(Commands::IDirect3D9Ex_GetAdapterModeCount);
    currentUID = c.get_uid();
    c.send_many(Adapter, Format);
  }
  WAIT_FOR_SERVER_RESPONSE("GetAdapterModeCount()", 0, currentUID);

  m_adapterModeCount[key] = (UINT) ModuleBridge::get_data();
  ModuleBridge::pop_front();
  return m_adapterModeCount[key];
}

HRESULT Direct3D9Ex_LSS::EnumAdapterModes(UINT Adapter, D3DFORMAT Format, UINT Mode, D3DDISPLAYMODE* pMode) {
  LogFunctionCall();

  // Check Adapter Count if cached
  if (m_adapterCount != 0 && Adapter >= m_adapterCount) {
    return  D3DERR_INVALIDCALL;
  }

  GET_HASH(key, Adapter, Format, Mode);
  if (m_enumAdapterMode.find(key) != m_enumAdapterMode.end()) {
    *pMode = m_enumAdapterMode[key];
    return S_OK;
  }

  UID currentUID = 0;
  // Send command to server and wait for response
  {
    ModuleClientCommand c(Commands::IDirect3D9Ex_EnumAdapterModes);
    currentUID = c.get_uid();
    c.send_many(Adapter, Format, Mode);
  }
  WAIT_FOR_SERVER_RESPONSE("EnumAdapterModes()", D3DERR_INVALIDCALL, currentUID);

  HRESULT hresult = ModuleBridge::get_data();
  if (SUCCEEDED(hresult)) {
    D3DDISPLAYMODE& adapterMode = m_enumAdapterMode[key];
    uint32_t len = ModuleBridge::copy_data(adapterMode);
    if (len != sizeof(D3DDISPLAYMODE) && len != 0) {
      Logger::err("EnumAdapterModes() failed due to issue with data returned from server.");
      hresult = D3DERR_INVALIDCALL;
    }
    *pMode = adapterMode;
  }
  ModuleBridge::pop_front();
  return hresult;
}

HRESULT Direct3D9Ex_LSS::GetAdapterDisplayMode(UINT Adapter, D3DDISPLAYMODE* pMode) {
  LogFunctionCall();

  // Check Adapter Count if cached
  if (m_adapterCount != 0 && Adapter >= m_adapterCount) {
    return  D3DERR_INVALIDCALL;
  }

  if (m_adapterDisplayMode.find(Adapter) != m_adapterDisplayMode.end()) {
    *pMode = m_adapterDisplayMode[Adapter];
    return S_OK;
  }

  UID currentUID = 0;
  // Send command to server and wait for response
  {
    ModuleClientCommand c(Commands::IDirect3D9Ex_GetAdapterDisplayMode);
    currentUID = c.get_uid();
    c.send_data(Adapter);
  }
  WAIT_FOR_SERVER_RESPONSE("GetAdapterDisplayMode()", D3DERR_INVALIDCALL, currentUID);

  HRESULT hresult = ModuleBridge::get_data();
  if (SUCCEEDED(hresult)) {
    D3DDISPLAYMODE& displayMode = m_adapterDisplayMode[Adapter];
    uint32_t len = ModuleBridge::copy_data(displayMode);
    if (len != sizeof(D3DDISPLAYMODE) && len != 0) {
      Logger::err("GetAdapterDisplayMode() failed due to issue with data returned from server.");
      hresult =  D3DERR_INVALIDCALL;
    }
    *pMode = displayMode;
  }
  ModuleBridge::pop_front();
  return hresult;
}

HRESULT Direct3D9Ex_LSS::CheckDeviceType(UINT Adapter, D3DDEVTYPE CheckType, D3DFORMAT DisplayFormat, D3DFORMAT BackBufferFormat, BOOL Windowed) {
  LogFunctionCall();

  // Check Adapter Count if cached
  if (m_adapterCount != 0 && Adapter >= m_adapterCount) {
    return  D3DERR_INVALIDCALL;
  }

  UID currentUID = 0;
  // Send command to server and wait for response
  {
    ModuleClientCommand c(Commands::IDirect3D9Ex_CheckDeviceType);
    currentUID = c.get_uid();
    c.send_many(Adapter, CheckType, DisplayFormat, BackBufferFormat, Windowed);
  }
  WAIT_FOR_SERVER_RESPONSE("CheckDeviceType()", E_FAIL, currentUID);

  HRESULT res = (HRESULT) ModuleBridge::get_data();
  ModuleBridge::pop_front();
  return res;
}

HRESULT Direct3D9Ex_LSS::CheckDeviceFormat(UINT Adapter, D3DDEVTYPE DeviceType, D3DFORMAT AdapterFormat, DWORD Usage, D3DRESOURCETYPE RType, D3DFORMAT CheckFormat) {
  LogFunctionCall();

  // Check Adapter Count if cached
  if (m_adapterCount != 0 && Adapter >= m_adapterCount) {
    return  D3DERR_INVALIDCALL;
  }

  UID currentUID = 0;
  // Send command to server and wait for response
  {
    ModuleClientCommand c(Commands::IDirect3D9Ex_CheckDeviceFormat);
    currentUID = c.get_uid();
    c.send_many(Adapter, DeviceType, AdapterFormat, Usage, RType, CheckFormat);
  }
  WAIT_FOR_SERVER_RESPONSE("CheckDeviceFormat()", D3DERR_NOTAVAILABLE, currentUID);

  HRESULT res = (HRESULT) ModuleBridge::get_data();
  ModuleBridge::pop_front();
  return res;
}

HRESULT Direct3D9Ex_LSS::CheckDeviceMultiSampleType(UINT Adapter, D3DDEVTYPE DeviceType, D3DFORMAT SurfaceFormat, BOOL Windowed, D3DMULTISAMPLE_TYPE MultiSampleType, DWORD* pQualityLevels) {
  LogFunctionCall();

  // Check Adapter Count if cached
  if (m_adapterCount != 0 && Adapter >= m_adapterCount) {
    return  D3DERR_INVALIDCALL;
  }

  if (MultiSampleType > D3DMULTISAMPLE_16_SAMPLES) {
    return  D3DERR_INVALIDCALL;
  }

  UID currentUID = 0;
  // Send command to server and wait for response
  {
    ModuleClientCommand c(Commands::IDirect3D9Ex_CheckDeviceMultiSampleType);
    currentUID = c.get_uid();
    c.send_many(Adapter, DeviceType, SurfaceFormat, Windowed, MultiSampleType);
  }
  WAIT_FOR_SERVER_RESPONSE("CheckDeviceMultiSampleType()", E_FAIL, currentUID);

  HRESULT res = (HRESULT) ModuleBridge::get_data();
  DWORD QualityLevelsLocal = (DWORD) ModuleBridge::get_data();

  if (pQualityLevels != NULL) {
    *pQualityLevels = QualityLevelsLocal;
  }
  ModuleBridge::pop_front();

  return res;
}

HRESULT Direct3D9Ex_LSS::CheckDepthStencilMatch(UINT Adapter, D3DDEVTYPE DeviceType, D3DFORMAT AdapterFormat, D3DFORMAT RenderTargetFormat, D3DFORMAT DepthStencilFormat) {
  LogFunctionCall();

  // Check Adapter Count if cached
  if (m_adapterCount != 0 && Adapter >= m_adapterCount) {
    return  D3DERR_INVALIDCALL;
  }

  UID currentUID = 0;
  // Send command to server and wait for response
  {
    ModuleClientCommand c(Commands::IDirect3D9Ex_CheckDepthStencilMatch);
    currentUID = c.get_uid();
    c.send_many(Adapter, DeviceType, AdapterFormat, RenderTargetFormat, DepthStencilFormat);
  }
  WAIT_FOR_SERVER_RESPONSE("CheckDepthStencilMatch()", E_FAIL, currentUID);

  HRESULT res = (HRESULT) ModuleBridge::get_data();
  ModuleBridge::pop_front();
  return res;
}

HRESULT Direct3D9Ex_LSS::CheckDeviceFormatConversion(UINT Adapter, D3DDEVTYPE DeviceType, D3DFORMAT SourceFormat, D3DFORMAT TargetFormat) {
  LogFunctionCall();

  // Check Adapter Count if cached
  if (m_adapterCount != 0 && Adapter >= m_adapterCount) {
    return  D3DERR_INVALIDCALL;
  }

  UID currentUID = 0;
  // Send command to server and wait for response
  {
    ModuleClientCommand c(Commands::IDirect3D9Ex_CheckDeviceFormatConversion);
    currentUID = c.get_uid();
    c.send_many(Adapter, DeviceType, SourceFormat, TargetFormat);
  }
  WAIT_FOR_SERVER_RESPONSE("CheckDeviceFormatConversion()", E_FAIL, currentUID);

  HRESULT res = (HRESULT) ModuleBridge::get_data();
  ModuleBridge::pop_front();
  return res;
}

HRESULT Direct3D9Ex_LSS::GetDeviceCaps(UINT Adapter, D3DDEVTYPE DeviceType, D3DCAPS9* pCaps) {
  LogFunctionCall();

  if (pCaps == NULL)
    return D3DERR_INVALIDCALL;

  // Check Adapter Count if cached
  if (m_adapterCount != 0 && Adapter >= m_adapterCount) {
    return  D3DERR_INVALIDCALL;
  }

  GET_HASH(key, Adapter, DeviceType);
  if (m_deviceCaps.find(key) != m_deviceCaps.end()) {
    *pCaps = m_deviceCaps[key];
    return S_OK;
  }

  UID currentUID = 0;
  // Send command to server and wait for response
  {
    ModuleClientCommand c(Commands::IDirect3D9Ex_GetDeviceCaps);
    currentUID = c.get_uid();
    c.send_many(Adapter, DeviceType);
  }

  WAIT_FOR_SERVER_RESPONSE("GetDeviceCaps()", D3DERR_INVALIDCALL, currentUID);

  HRESULT hresult = ModuleBridge::get_data();
  if (SUCCEEDED(hresult)) {
    D3DCAPS9& deviceCaps = m_deviceCaps[key];
    uint32_t len = ModuleBridge::copy_data(deviceCaps);
    if (len != sizeof(D3DCAPS9) && len != 0) {
      Logger::err("GetDeviceCaps() failed due to issue with data returned from server.");
      hresult = D3DERR_INVALIDCALL;
    }
    *pCaps = deviceCaps;
  }
  ModuleBridge::pop_front();
  return hresult;
}

HMONITOR Direct3D9Ex_LSS::GetAdapterMonitor(UINT Adapter) {
  LogFunctionCall();

  UID currentUID = 0;
  // Send command to server and wait for response
  {
    ModuleClientCommand c(Commands::IDirect3D9Ex_GetAdapterMonitor);
    currentUID = c.get_uid();
    c.send_data(Adapter);
  }
  WAIT_FOR_SERVER_RESPONSE("GetAdapterMonitor()", 0, currentUID);

  HMONITOR monitor = (HMONITOR) ModuleBridge::get_data();
  ModuleBridge::pop_front();
  return monitor;
}

HRESULT Direct3D9Ex_LSS::CreateDevice(
  UINT Adapter, D3DDEVTYPE DeviceType,
  HWND hFocusWindow, DWORD BehaviorFlags,
  D3DPRESENT_PARAMETERS* pPresentationParameters,
  IDirect3DDevice9** ppReturnedDeviceInterface
) {
  LogFunctionCall();
  const D3DDEVICE_CREATION_PARAMETERS createParams { Adapter, DeviceType, hFocusWindow, BehaviorFlags };
  auto [hresult, pLssDevice] = createDevice(false, this, createParams, *pPresentationParameters, nullptr);
  if (SUCCEEDED(hresult)) {
    (*ppReturnedDeviceInterface) = pLssDevice;
  }
  return hresult;
}

UINT Direct3D9Ex_LSS::GetAdapterModeCountEx(UINT Adapter, CONST D3DDISPLAYMODEFILTER* pFilter) {
  assert(m_ex);
  LogFunctionCall();

  // Check Adapter Count if cached
  if (m_adapterCount != 0 && Adapter >= m_adapterCount) {
    return  D3DERR_INVALIDCALL;
  }

  UINT cnt = 0;
  UID currentUID = 0;
  // Send command to server and wait for response
  {
    ModuleClientCommand c(Commands::IDirect3D9Ex_GetAdapterModeCountEx, getId());
    currentUID = c.get_uid();
    c.send_data(Adapter);
    c.send_data(sizeof(D3DDISPLAYMODEFILTER), pFilter);
  }
  WAIT_FOR_SERVER_RESPONSE("GetAdapterModeCountEx()", cnt, currentUID);

  cnt = (UINT) ModuleBridge::get_data();
  ModuleBridge::pop_front();
  return cnt;
}

HRESULT Direct3D9Ex_LSS::EnumAdapterModesEx(UINT Adapter, CONST D3DDISPLAYMODEFILTER* pFilter, UINT Mode, D3DDISPLAYMODEEX* pMode) {
  assert(m_ex);
  LogFunctionCall();

  // Check Adapter Count if cached
  if (m_adapterCount != 0 && Adapter >= m_adapterCount) {
    return  D3DERR_INVALIDCALL;
  }

  if (pFilter == nullptr || pMode == nullptr) {
    return  D3DERR_INVALIDCALL;
  }

  UID currentUID = 0;
  // Send command to server and wait for response
  {
    ModuleClientCommand c(Commands::IDirect3D9Ex_EnumAdapterModesEx);
    currentUID = c.get_uid();
    c.send_data(Adapter);
    c.send_data(Mode);
    c.send_data(sizeof(D3DDISPLAYMODEFILTER), pFilter);
  }
  WAIT_FOR_SERVER_RESPONSE("EnumAdapterModesEx()", D3DERR_INVALIDCALL, currentUID);

  HRESULT hresult = ModuleBridge::get_data();
  if (SUCCEEDED(hresult)) {
    uint32_t len = ModuleBridge::copy_data(*pMode);
    if (len != sizeof(D3DDISPLAYMODEEX) && len != 0) {
      Logger::err("EnumAdapterModesEx() failed due to issue with data returned from server.");
      hresult = D3DERR_INVALIDCALL;
    }
  }
  ModuleBridge::pop_front();
  return hresult;
}

HRESULT Direct3D9Ex_LSS::GetAdapterDisplayModeEx(UINT Adapter, D3DDISPLAYMODEEX* pMode, D3DDISPLAYROTATION* pRotation) {
  assert(m_ex);
  LogFunctionCall();

  // Check Adapter Count if cached
  if (m_adapterCount != 0 && Adapter >= m_adapterCount) {
    return  D3DERR_INVALIDCALL;
  }

  if (pMode == nullptr) {
    return  D3DERR_INVALIDCALL;
  }

  UID currentUID = 0;
  // Send command to server and wait for response
  {
    ModuleClientCommand c(Commands::IDirect3D9Ex_GetAdapterDisplayModeEx);
    currentUID = c.get_uid();
    c.send_data(Adapter);
    c.send_data(sizeof(D3DDISPLAYMODEEX), pMode);
    c.send_data(sizeof(D3DDISPLAYROTATION), pRotation);
  }
  WAIT_FOR_SERVER_RESPONSE("GetAdapterDisplayModeEx()", D3DERR_INVALIDCALL, currentUID);

  HRESULT hresult = ModuleBridge::get_data();
  if (SUCCEEDED(hresult)) {
    uint32_t len = ModuleBridge::copy_data(*pMode);
    if (len != sizeof(D3DDISPLAYMODEEX) && len != 0) {
      Logger::err("GetAdapterDisplayModeEx() failed due to issue with data returned from server.");
      hresult = D3DERR_INVALIDCALL;
    }

    len = ModuleBridge::copy_data(*pRotation);
    if (len != sizeof(D3DDISPLAYROTATION) && len != 0) {
      Logger::err("GetAdapterDisplayModeEx() failed due to issue with data returned from server.");
      hresult = D3DERR_INVALIDCALL;
    }
  }
  ModuleBridge::pop_front();
  return hresult;
}

HRESULT Direct3D9Ex_LSS::CreateDeviceEx(
  UINT Adapter, D3DDEVTYPE DeviceType,
  HWND hFocusWindow, DWORD BehaviorFlags,
  D3DPRESENT_PARAMETERS* pPresentationParameters,
  D3DDISPLAYMODEEX* pFullscreenDisplayMode,
  IDirect3DDevice9Ex** ppReturnedDeviceInterface
) {
  assert(m_ex);
  LogFunctionCall();
  const D3DDEVICE_CREATION_PARAMETERS createParams { Adapter, DeviceType, hFocusWindow, BehaviorFlags };
  auto [hresult, pLssDevice] = createDevice(true, this, createParams, *pPresentationParameters, pFullscreenDisplayMode);
  if (SUCCEEDED(hresult)) {
    (*ppReturnedDeviceInterface) = pLssDevice;
  }
  return hresult;
}

HRESULT Direct3D9Ex_LSS::GetAdapterLUID(UINT Adapter, LUID* pLUID) {
  LogFunctionCall();

  // https://learn.microsoft.com/en-us/windows/win32/api/d3d9/nf-d3d9-idirect3d9ex-getadapterluid
  // No mention the pLUID must be valid but checking anyway
  
  if (pLUID == nullptr) {
    return  D3DERR_INVALIDCALL;
  }

  UID currentUID = 0;
  // Send command to server and wait for response
  {
    ModuleClientCommand c(Commands::IDirect3D9Ex_GetAdapterLUID);
    currentUID = c.get_uid();
    c.send_data(Adapter);
  }
  WAIT_FOR_SERVER_RESPONSE("IDirect3D9Ex_GetAdapterLUID()", D3DERR_INVALIDCALL, currentUID);

  HRESULT hresult = ModuleBridge::get_data();
  if (SUCCEEDED(hresult)) {
    uint32_t len = ModuleBridge::copy_data(*pLUID);
    if (len != sizeof(pLUID) && len != 0) {
      Logger::err("IDirect3D9Ex_GetAdapterLUID() failed due to issue with data returned from server.");
      hresult = D3DERR_INVALIDCALL;
    }
  }
  ModuleBridge::pop_front();
  return hresult;
}

std::tuple<HRESULT, BaseDirect3DDevice9Ex_LSS*> Direct3D9Ex_LSS::createDevice(
  const bool bExtended,
  Direct3D9Ex_LSS* const pDirect3D,
  const D3DDEVICE_CREATION_PARAMETERS& createParams,
  const D3DPRESENT_PARAMETERS& presParam,
  const D3DDISPLAYMODEEX* const pFullscreenDisplayMode
) {
  auto localPresParam = Direct3DSwapChain9_LSS::sanitizePresentationParameters(presParam, createParams);

  if (localPresParam.Windowed == FALSE) {
    RECT rect = { };
    ::GetClientRect(localPresParam.hDeviceWindow, &rect);

    if (rect.right - rect.left != localPresParam.BackBufferWidth ||
        rect.bottom - rect.top != localPresParam.BackBufferHeight) {

      Logger::warn(format_string("Window extent != backbuffer extent in fullscreen mode. "
                                 "Forcing window extent to backbuffer size (%dx%d).",
                                 localPresParam.BackBufferWidth, localPresParam.BackBufferHeight));

      SetWindowMode(localPresParam.hDeviceWindow, false, localPresParam.BackBufferWidth,
                    localPresParam.BackBufferHeight);
    }
  }

  HRESULT createDeviceHresult = D3DERR_DEVICELOST;
  BaseDirect3DDevice9Ex_LSS* pNewDevice = nullptr;

  const auto policy = GlobalOptions::getThreadSafetyPolicy();
  if ((0 != (createParams.BehaviorFlags & D3DCREATE_MULTITHREADED) && policy == 0) || policy == 1) {
#ifdef WITH_MULTITHREADED_DEVICE
    Logger::info("Creating a thread-safe D3D9 device.");
    pNewDevice = new Direct3DDevice9Ex_LSS<true>(
      bExtended, pDirect3D, createParams, localPresParam, pFullscreenDisplayMode, createDeviceHresult);
#else
    Logger::warn("A thread-safe D3D9 device has been requested "
                 "while the bridge was not built with thread-safety support enabled. "
                 "The client should run fine if used on a single thread, but may "
                 "otherwise likely crash as a result.");
#endif
  } else {
    Logger::info("Creating a NON thread-safe D3D9 device.");
    pNewDevice = new Direct3DDevice9Ex_LSS<false>(
      bExtended, pDirect3D, createParams, localPresParam, pFullscreenDisplayMode, createDeviceHresult);
  }
  return { createDeviceHresult, pNewDevice };
}