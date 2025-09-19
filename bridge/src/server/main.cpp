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
#include <windows.h>

#include "version.h"
#include "module_processing.h"
#include "remix_api.h"

#include "util_bridge_assert.h"
#include "util_circularbuffer.h"
#include "util_commands.h"
#include "util_common.h"
#include "util_devicecommand.h"
#include "util_filesys.h"
#include "util_guid.h"
#include "util_hack_d3d_debug.h"
#include "util_messagechannel.h"
#include "util_modulecommand.h"
#include "util_process.h"
#include "util_remixapi.h"
#include "util_seh.h"
#include "util_semaphore.h"
#include "util_sharedheap.h"
#include "util_sharedmemory.h"
#include "util_texture_and_volume.h"
#include "util_version.h"

#include "log/log.h"
#include "config/config.h"
#include "config/global_options.h"
#include "server_options.h"
#include "../client/client_options.h"

#include "../tracy/tracy.hpp"
#include <iostream>
#include <d3d9.h>
#include <assert.h>
#include <map>
#include <atomic>
#include <array>

using namespace Commands;
using namespace bridge_util;
using namespace remixapi::util;

// NOTE: This extension is really useful for debugging the Bridge child process from the parent process:
// https://marketplace.visualstudio.com/items?itemName=vsdbgplat.MicrosoftChildProcessDebuggingPowerTool

#define SEND_OPTIONAL_SERVER_RESPONSE(hresult, uid) { \
    if (GlobalOptions::getSendAllServerResponses()) { \
      ServerMessage c(Commands::Bridge_Response, uid); \
      c.send_data(hresult); \
    } \
  } 

#define SEND_OPTIONAL_CREATE_FUNCTION_SERVER_RESPONSE(hresult, uid) { \
    if (GlobalOptions::getSendCreateFunctionServerResponses() || GlobalOptions::getSendAllServerResponses()) { \
      ServerMessage c(Commands::Bridge_Response, uid); \
      c.send_data(hresult); \
    } \
  } 

#define PULL(type, name) const auto& name = (type)DeviceBridge::get_data()
#define PULL_I(name) PULL(INT, name)
#define PULL_U(name) PULL(UINT, name)
#define PULL_D(name) PULL(DWORD, name)
#define PULL_H(name) PULL(HRESULT, name)
#define PULL_HND(name) \
            PULL_U(name); \
            assert(name != NULL)
#define PULL_DATA(size, name) \
            uint32_t name##_len = DeviceBridge::get_data((void**)&name); \
            assert(name##_len == 0 || size == name##_len)
#define PULL_OBJ(type, name) \
            type* name = nullptr; \
            PULL_DATA(sizeof(type), name)
#define CHECK_DATA_OFFSET (DeviceBridge::get_data_pos() == rpcHeader.dataOffset)
#define GET_HND(name) \
            const auto& name = rpcHeader.pHandle; \
            assert(name != NULL)
#define GET_HDR_VAL(name) \
            const DWORD& name = rpcHeader.pHandle;
#define GET_RES(name, map) \
            GET_HND(name##Handle); \
            const auto& name = map[name##Handle]; \
            assert(name != NULL)

// NOTE: MSDN states HWNDs are safe to cross x86-->x64 boundary, and that a truncating cast should be used:
// https://docs.microsoft.com/en-us/windows/win32/winprog64/interprocess-communication?redirectedfrom=MSDN
#define TRUNCATE_HANDLE(type, input) (type)(size_t)(input)

bool bDxvkModuleLoaded = false;
std::chrono::steady_clock::time_point gTimeStart;

// Shared memory and IPC channels
Guid gUniqueIdentifier;
NamedSemaphore* gpPresent = nullptr;
std::unique_ptr<MessageChannelServer> gpClientMessageChannel;
// D3D Library handle
typedef IDirect3D9* (WINAPI* D3DC9)(UINT);
typedef HRESULT(WINAPI* D3DC9Ex)(UINT, IDirect3D9Ex**);
HMODULE ghModule;
LPDIRECT3D9 gpD3D;

bool gOverwriteConditionAlreadyActive = false;

// Mapping between client and server pointer addresses
std::unordered_map<uint32_t, IDirect3DDevice9*> gpD3DDevices;
std::unordered_map<uint32_t, IDirect3DResource9*> gpD3DResources; // For Textures, Buffers, and Surfaces
std::unordered_map<uint32_t, IDirect3DVolume9*> gpD3DVolumes;
std::unordered_map<uint32_t, IDirect3DVertexDeclaration9*> gpD3DVertexDeclarations;
std::unordered_map<uint32_t, IDirect3DStateBlock9*> gpD3DStateBlocks;
std::unordered_map<uint32_t, IDirect3DVertexShader9*> gpD3DVertexShaders;
std::unordered_map<uint32_t, IDirect3DPixelShader9*> gpD3DPixelShaders;
std::unordered_map<uint32_t, IDirect3DSwapChain9*> gpD3DSwapChains;
std::unordered_map<uint32_t, IDirect3DQuery9*> gpD3DQuery;
std::unordered_map<uint32_t, void*> gMapRemixApi;

// Global state
bool gbBridgeRunning = true;
HANDLE hWait;

namespace {
template<typename SerializableT>
static void deserializeFromQueue(SerializableT& serializableT) {
  static_assert(is_serializable_v<SerializableT>, "deserializeRemixApiT(...)  may only be called with defined Serializable<T> types");
  void* pSlzdData = nullptr;
  const auto size = DeviceBridge::get_data(&pSlzdData);
  SerializableT dslz(pSlzdData);
  assert(size == dslz.size());
  dslz.deserialize();
  serializableT = std::move(dslz);
}
}

static inline void safeDestroy(IUnknown* obj, uint32_t x86handle) {
  // Note: in DXVK the refcounts of non-standalone objects may go negative!
  // We need to handle such objects appropriately, even though this is not
  // the case in regular system D3D9.

#if defined(_DEBUG) && defined(VERBOSE)
  if (obj) {
    const LONG cnt = static_cast<LONG>(obj->Release());
    if (cnt > 0) {
      Logger::trace(format_string("Object [%p/%lx] refcount at destroy is %d > 1.",
                                  obj, x86handle, cnt + 1));
    }
  }
#endif

  while (obj && static_cast<LONG>(obj->Release()) > 0);
}

D3DPRESENT_PARAMETERS getPresParamFromRaw(const uint32_t* rawPresentationParameters) {
  D3DPRESENT_PARAMETERS presParam;
  // Set up presentation parameters. We can't just directly cast the structure because the hDeviceWindow
  // handle is 4 bytes in the data coming in but 8 bytes in the x64 version of the struct.
  presParam.BackBufferWidth = *reinterpret_cast<const UINT*>(rawPresentationParameters);
  presParam.BackBufferHeight = *reinterpret_cast<const UINT*>(rawPresentationParameters + 1);
  presParam.BackBufferFormat = *reinterpret_cast<const D3DFORMAT*>(rawPresentationParameters + 2);
  presParam.BackBufferCount = *reinterpret_cast<const UINT*>(rawPresentationParameters + 3);

  presParam.MultiSampleType = *reinterpret_cast<const D3DMULTISAMPLE_TYPE*>(rawPresentationParameters + 4);
  presParam.MultiSampleQuality = *reinterpret_cast<const DWORD*>(rawPresentationParameters + 5);

  presParam.SwapEffect = *reinterpret_cast<const D3DSWAPEFFECT*>(rawPresentationParameters + 6);
  presParam.hDeviceWindow = *reinterpret_cast<const HWND*>(rawPresentationParameters + 7);
  presParam.Windowed = *reinterpret_cast<const BOOL*>(rawPresentationParameters + 8);
  presParam.EnableAutoDepthStencil = *reinterpret_cast<const BOOL*>(rawPresentationParameters + 9);
  presParam.AutoDepthStencilFormat = *reinterpret_cast<const D3DFORMAT*>(rawPresentationParameters + 10);
  presParam.Flags = *reinterpret_cast<const DWORD*>(rawPresentationParameters + 11);

  presParam.FullScreen_RefreshRateInHz = *reinterpret_cast<const UINT*>(rawPresentationParameters + 12);
  presParam.PresentationInterval = (UINT) * reinterpret_cast<const UINT*>(rawPresentationParameters + 13);

  return presParam;
}

HRESULT ReturnSurfaceDataToClient(IDirect3DSurface9* pReturnSurfaceData, HRESULT hresult, UINT currentUID) {
  // We send the HRESULT response back to the client even in case of failure
  ServerMessage c(Commands::Bridge_Response, currentUID);

  if (!SUCCEEDED(hresult)) {
    c.send_data(hresult);
    return hresult;
  }
  // Using surface desc to get width, height of the surface
  D3DSURFACE_DESC pDesc;
  hresult = pReturnSurfaceData->GetDesc(OUT & pDesc);
  if (!SUCCEEDED(hresult)) {
    c.send_data(hresult);
    return hresult;
  }

  uint32_t width = pDesc.Width;
  uint32_t height = pDesc.Height;
  D3DFORMAT format = pDesc.Format;

  // Obtaining raw buffer from the surface and we send this data to client
  D3DLOCKED_RECT lockedRect;
  hresult = pReturnSurfaceData->LockRect(OUT & lockedRect, NULL, IN D3DLOCK_READONLY);
  if (!SUCCEEDED(hresult)) {
    c.send_data(hresult);
    return hresult;
  }

  // Sending raw surface buffer details to client
  const uint32_t totalSize = bridge_util::calcTotalSizeOfRect(width, height, format);
  const uint32_t rowSize = bridge_util::calcRowSize(width, format);
  c.send_data(hresult);
  c.send_data(width);
  c.send_data(height);
  c.send_data(format);
  if (auto* blobPacketPtr = c.begin_data_blob(totalSize)) {
    FOR_EACH_RECT_ROW(lockedRect, height, format, {
      memcpy(blobPacketPtr, ptr, rowSize);
      blobPacketPtr += rowSize;
    });
    c.end_data_blob();
  }

  hresult = pReturnSurfaceData->UnlockRect();
  return hresult;
}

template<typename T>
static bool dumpLeakedObjects(const char* name, const T& map) {
  if (!map.empty()) {
    bridge_util::Logger::err(format_string("%zd objects discovered in %s map at "
                              "Direct3D module eviction:", map.size(), name));
    for (auto& [handle, obj] : map) {
      bridge_util::Logger::err(format_string("\t%x -> %p", handle, obj));
    }
    return true;
  }
  return false;
}

static bool dumpLeakedObjects() {
  bool anyLeaked = false;

  anyLeaked |= dumpLeakedObjects("Resource", gpD3DResources);
  anyLeaked |= dumpLeakedObjects("Vertex Declaration", gpD3DVertexDeclarations);
  anyLeaked |= dumpLeakedObjects("State Block", gpD3DStateBlocks);
  anyLeaked |= dumpLeakedObjects("Vertex Shader", gpD3DVertexShaders);
  anyLeaked |= dumpLeakedObjects("Pixel Shader", gpD3DPixelShaders);
  anyLeaked |= dumpLeakedObjects("Swapchain", gpD3DSwapChains);
  anyLeaked |= dumpLeakedObjects("Volume", gpD3DVolumes);

  anyLeaked |= dumpLeakedObjects("Device", gpD3DDevices);

  return anyLeaked;
}

void ProcessDeviceCommandQueue() {
  // Loop until the client sends terminate instruction
  bool done = false;
  while (!done && DeviceBridge::waitForCommand() == Result::Success) {
    ZoneScopedN("Process Command");
#ifdef LOG_SERVER_COMMAND_TIME
    // Take a snapshot of the current tick count for profiling purposes
    const auto start = GetTickCount64();
#endif

    const Header rpcHeader = DeviceBridge::pop_front();

#ifdef _DEBUG
    // If data batching is enabled and the data offset on the comamnd is different from
    // our current offset we know there must be data to read, so we start a data batch
    // read operation on the data queue buffer.
    if (!CHECK_DATA_OFFSET) {
      const auto result = DeviceBridge::begin_read_data();
      assert(RESULT_SUCCESS(result));
    }
#endif

    {
      ZoneScoped;
      if (ZoneIsActive) {
        const std::string commandStr = toString(rpcHeader.command);
        ZoneName(commandStr.c_str(), commandStr.size());
      }
      PULL_U(currentUID);
#if defined(_DEBUG) || defined(DEBUGOPT)
      if (GlobalOptions::getLogServerCommands()) {
        Logger::info("Device Processing: " + toString(rpcHeader.command) + " UID: " + std::to_string(currentUID));
      }
#endif
      // The mother of all switch statements - every call in the D3D9 interface is mapped here...
      switch (rpcHeader.command) {
      case IDirect3D9Ex_CreateDeviceEx:
      {
        GET_HND(pHandle);
        PULL_U(Adapter);
        PULL(D3DDEVTYPE, DeviceType);
        PULL(uint32_t, hFocusWindow);
        PULL_D(BehaviorFlags);
        D3DDISPLAYMODEEX* pFullscreenDisplayMode = nullptr;
        PULL_DATA(sizeof(D3DDISPLAYMODEEX), pFullscreenDisplayMode);

        uint32_t* rawPresentationParameters = nullptr;
        DeviceBridge::get_data((void**) &rawPresentationParameters);
        D3DPRESENT_PARAMETERS PresentationParameters = getPresParamFromRaw(rawPresentationParameters);

        IDirect3DDevice9Ex* pD3DDevice = nullptr;
        const auto hresult = ((IDirect3D9Ex*) gpD3D)->CreateDeviceEx(IN Adapter, IN DeviceType, IN TRUNCATE_HANDLE(HWND, hFocusWindow), IN BehaviorFlags, IN OUT & PresentationParameters, IN pFullscreenDisplayMode, OUT & pD3DDevice);
        if (!SUCCEEDED(hresult)) {
          std::stringstream ss;
          ss << format_string("CreateDeviceEx() call failed with error code 0x%x", hresult) << std::endl;
          Logger::err(ss.str());
        } else {
          Logger::info("Server side D3D9 DeviceEx created successfully!");
          gpD3DDevices[pHandle] = pD3DDevice;
          if(GlobalOptions::getExposeRemixApi()) {
            remixapi::g_device = pD3DDevice;
            remixapi::g_remix.dxvk_RegisterD3D9Device(remixapi::g_device);
          }
        }

        // Send response back to the client
        Logger::debug("Sending CreateDevice ack response back to client.");
        {
          ServerMessage c(Commands::Bridge_Response, currentUID);
          c.send_data(hresult);
        }
        break;
      }
      case IDirect3D9Ex_CreateDevice:
      {
        GET_HND(pHandle);
        PULL_U(Adapter);
        PULL(D3DDEVTYPE, DeviceType);
        PULL(uint32_t, hFocusWindow);
        PULL_D(BehaviorFlags);

        uint32_t* rawPresentationParameters = nullptr;
        DeviceBridge::get_data((void**) &rawPresentationParameters);
        D3DPRESENT_PARAMETERS PresentationParameters = getPresParamFromRaw(rawPresentationParameters);

        IDirect3DDevice9* pD3DDevice = nullptr;
        const auto hresult = gpD3D->CreateDevice(IN Adapter, IN DeviceType, IN TRUNCATE_HANDLE(HWND, hFocusWindow), IN BehaviorFlags, IN OUT & PresentationParameters, OUT & pD3DDevice);
        if (!SUCCEEDED(hresult)) {
          std::stringstream ss;
          ss << format_string("CreateDevice() call failed with error code 0x%x", hresult) << std::endl;
          Logger::err(ss.str());
        } else {
          Logger::info("Server side D3D9 Device created successfully!");
          gpD3DDevices[pHandle] = (IDirect3DDevice9Ex*) pD3DDevice;
          if(GlobalOptions::getExposeRemixApi()) {
            remixapi::g_device = (IDirect3DDevice9Ex*) pD3DDevice;
            remixapi::g_remix.dxvk_RegisterD3D9Device(remixapi::g_device);
          }
        }

        // Send response back to the client
        Logger::debug("Sending CreateDevice ack response back to client.");
        {
          ServerMessage c(Commands::Bridge_Response, currentUID);
          c.send_data(hresult);
        }
        break;
      }
      case IDirect3DDevice9Ex_GetDisplayModeEx:
      {
        GET_RES(pD3DDevice, gpD3DDevices);
        PULL_U(iSwapChain);
        D3DDISPLAYMODEEX pMode;
        D3DDISPLAYROTATION pRotation;
        HRESULT hresult = ((IDirect3DDevice9Ex*) pD3DDevice)->GetDisplayModeEx(iSwapChain, &pMode, &pRotation);
        {
          ServerMessage c(Commands::Bridge_Response, currentUID);
          c.send_data(hresult);
          if (SUCCEEDED(hresult)) {
            c.send_data(sizeof(D3DDISPLAYMODEEX), &pMode);
            c.send_data(sizeof(D3DDISPLAYROTATION), &pRotation);
          }
        }
        break;
      }
      case IDirect3DDevice9Ex_CreateRenderTargetEx:
      {
        GET_RES(pD3DDevice, gpD3DDevices);
        PULL_U(Width);
        PULL_U(Height);
        PULL(D3DFORMAT, Format);
        PULL(D3DMULTISAMPLE_TYPE, MultiSample);
        PULL_D(MultisampleQuality);
        PULL(BOOL, Lockable);
        PULL(DWORD, Usage);
        PULL_HND(pHandle);
        LPDIRECT3DSURFACE9 pSurface;
        const auto hresult = ((IDirect3DDevice9Ex*) pD3DDevice)->CreateRenderTargetEx(IN Width, IN Height, IN Format, IN MultiSample, IN MultisampleQuality, IN Lockable, OUT & pSurface, IN nullptr, IN Usage);
        if (SUCCEEDED(hresult)) {
          gpD3DResources[pHandle] = pSurface;
        }
        assert(SUCCEEDED(hresult));
        SEND_OPTIONAL_CREATE_FUNCTION_SERVER_RESPONSE(hresult, currentUID);
        break;
      }
      case IDirect3DDevice9Ex_CreateOffscreenPlainSurfaceEx:
      {
        GET_RES(pD3DDevice, gpD3DDevices);
        PULL_U(Width);
        PULL_U(Height);
        PULL(D3DFORMAT, Format);
        PULL(D3DPOOL, Pool);
        PULL(DWORD, Usage);
        PULL_HND(pHandle);
        LPDIRECT3DSURFACE9 pSurface;
        const auto hresult = ((IDirect3DDevice9Ex*) pD3DDevice)->CreateOffscreenPlainSurfaceEx(IN Width, IN Height, IN Format, IN Pool, OUT & pSurface, IN nullptr, IN Usage);
        if (SUCCEEDED(hresult)) {
          gpD3DResources[pHandle] = pSurface;
        }
        assert(SUCCEEDED(hresult));
        SEND_OPTIONAL_CREATE_FUNCTION_SERVER_RESPONSE(hresult, currentUID);
        break;
      }
      case IDirect3DDevice9Ex_CreateDepthStencilSurfaceEx:
      {
        GET_RES(pD3DDevice, gpD3DDevices);
        PULL_U(Width);
        PULL_U(Height);
        PULL(D3DFORMAT, Format);
        PULL(D3DMULTISAMPLE_TYPE, MultiSample);
        PULL_D(MultisampleQuality);
        PULL(BOOL, Discard);
        PULL(DWORD, Usage);
        PULL_HND(pHandle);
        LPDIRECT3DSURFACE9 pSurface;
        const auto hresult = ((IDirect3DDevice9Ex*) pD3DDevice)->CreateDepthStencilSurfaceEx(IN Width, IN Height, IN Format, IN MultiSample, IN MultisampleQuality, IN Discard, OUT & pSurface, IN nullptr, IN Usage);
        if (SUCCEEDED(hresult)) {
          gpD3DResources[pHandle] = pSurface;
        }
        assert(SUCCEEDED(hresult));
        SEND_OPTIONAL_CREATE_FUNCTION_SERVER_RESPONSE(hresult, currentUID);
        break;
      }

      /*
       * IDirect3DDevice9 interface
       */
      case IDirect3DDevice9Ex_LinkSwapchain:
      {
        GET_RES(pD3DDevice, gpD3DDevices);
        PULL_HND(pClientSwapchain);
        IDirect3DSwapChain9* pSwapChain = nullptr;
        const auto hresult = pD3DDevice->GetSwapChain(0, &pSwapChain);
        if (SUCCEEDED(hresult)) {
          gpD3DSwapChains[pClientSwapchain] = pSwapChain;
        }
        break;
      }
      case IDirect3DDevice9Ex_LinkBackBuffer:
      {
        GET_RES(pD3DDevice, gpD3DDevices);
        PULL(uint32_t, index);
        PULL_HND(pSurfaceHandle);
        IDirect3DSurface9* pBackbuffer = nullptr;
        const auto hresult = pD3DDevice->GetBackBuffer(0, index, D3DBACKBUFFER_TYPE_MONO, &pBackbuffer);
        if (SUCCEEDED(hresult)) {
          gpD3DResources[pSurfaceHandle] = pBackbuffer;
        }
        assert(SUCCEEDED(hresult));
        break;
      }
      case IDirect3DDevice9Ex_LinkAutoDepthStencil:
      {
        GET_RES(pD3DDevice, gpD3DDevices);
        PULL_HND(pSurfaceHandle);
        IDirect3DSurface9* pDepthStencil = nullptr;
        const auto hresult = pD3DDevice->GetDepthStencilSurface(&pDepthStencil);
        if (SUCCEEDED(hresult)) {
          gpD3DResources[pSurfaceHandle] = pDepthStencil;
        }
        assert(SUCCEEDED(hresult));
        break;
      }
      case IDirect3DDevice9Ex_QueryInterface:
        break;
      case IDirect3DDevice9Ex_AddRef:
        break;
      case IDirect3DDevice9Ex_Destroy:
      {
        GET_RES(pD3DDevice, gpD3DDevices);
        safeDestroy(pD3DDevice, pD3DDeviceHandle);
        gpD3DDevices.erase(pD3DDeviceHandle);
        break;
      }
      case IDirect3DDevice9Ex_TestCooperativeLevel:
      {
        GET_RES(pD3DDevice, gpD3DDevices);
        const auto hresult = pD3DDevice->TestCooperativeLevel();
        assert(SUCCEEDED(hresult));
        break;
      }
      case IDirect3DDevice9Ex_GetAvailableTextureMem:
      {
        GET_RES(pD3DDevice, gpD3DDevices);
        const auto mem = pD3DDevice->GetAvailableTextureMem();
        {
          ServerMessage c(Commands::Bridge_Response, currentUID);
          c.send_data(mem);
        }
        break;
      }
      case IDirect3DDevice9Ex_EvictManagedResources:
      {
        GET_RES(pD3DDevice, gpD3DDevices);
        auto const hresult = pD3DDevice->EvictManagedResources();
        assert(SUCCEEDED(hresult));
        SEND_OPTIONAL_SERVER_RESPONSE(hresult, currentUID);
        break;
      }
      case IDirect3DDevice9Ex_GetDirect3D:
      {
        GET_RES(pD3DDevice, gpD3DDevices);
        IDirect3D9* pD3D = nullptr;
        const auto hresult = pD3DDevice->GetDirect3D(OUT & pD3D);
        assert(SUCCEEDED(hresult));
        assert(gpD3D == pD3D); // The two pointers should be identical
        break;
      }
      case IDirect3DDevice9Ex_GetDeviceCaps:
      {

        GET_RES(pD3DDevice, gpD3DDevices);
        D3DCAPS9 pCaps;
        const auto hresult = pD3DDevice->GetDeviceCaps(OUT & pCaps);
        BRIDGE_ASSERT_LOG(SUCCEEDED(hresult), "Issue retrieving D3D9 device specific information");
        {
          ServerMessage c(Commands::Bridge_Response, currentUID);
          c.send_data(hresult);
          if (SUCCEEDED(hresult)) {
            c.send_data(sizeof(D3DCAPS9), &pCaps);
          }
        }
        break;
      }
      case IDirect3DDevice9Ex_GetDisplayMode:
      {
        GET_RES(pD3DDevice, gpD3DDevices);
        PULL_U(iSwapChain);
        D3DDISPLAYMODE pMode;
        const auto hresult = pD3DDevice->GetDisplayMode(IN iSwapChain, OUT & pMode);
        BRIDGE_ASSERT_LOG(SUCCEEDED(hresult), "Issue retrieving information about D3D9 display mode of the adapter");
        {
          ServerMessage c(Commands::Bridge_Response, currentUID);
          c.send_data(hresult);
          if (SUCCEEDED(hresult)) {
            c.send_data(sizeof(D3DDISPLAYMODE), &pMode);
          }
        }
        break;
      }
      case IDirect3DDevice9Ex_GetCreationParameters:
        break;
      case IDirect3DDevice9Ex_SetCursorProperties:
      {
        GET_RES(pD3DDevice, gpD3DDevices);
        PULL(UINT, XHotSpot);
        PULL(UINT, YHotSpot);
        PULL_U(pHandle);
        IDirect3DSurface9* pCursorBitmap = nullptr;
        if (pHandle != NULL) {
          pCursorBitmap = (IDirect3DSurface9*) gpD3DResources[pHandle];
        }
        const auto hresult = pD3DDevice->SetCursorProperties(XHotSpot, YHotSpot, pCursorBitmap);
        SEND_OPTIONAL_SERVER_RESPONSE(hresult, currentUID);
        break;
      }
      case IDirect3DDevice9Ex_SetCursorPosition:
      {
        GET_RES(pD3DDevice, gpD3DDevices);
        PULL(int, X);
        PULL(int, Y);
        PULL(DWORD, Flags);
        pD3DDevice->SetCursorPosition(X, Y, Flags);
        break;
      }
      case IDirect3DDevice9Ex_ShowCursor:
      {
        GET_RES(pD3DDevice, gpD3DDevices);
        PULL(BOOL, bShow);
        const BOOL prevShow = pD3DDevice->ShowCursor(bShow);
        {
          ServerMessage c(Commands::Bridge_Response, currentUID);
          c.send_data(prevShow);
        }
        break;
      }
      case IDirect3DDevice9Ex_CreateAdditionalSwapChain:
      {
        GET_RES(pD3DDevice, gpD3DDevices);
        PULL_HND(pHandle);
        uint32_t* rawPresentationParameters = nullptr;
        DeviceBridge::get_data((void**) &rawPresentationParameters);
        D3DPRESENT_PARAMETERS PresentationParameters = getPresParamFromRaw(rawPresentationParameters);
        IDirect3DSwapChain9* pSwapChain = nullptr;
        const auto hresult = pD3DDevice->CreateAdditionalSwapChain(&PresentationParameters, &pSwapChain);
        if (SUCCEEDED(hresult)) {
          gpD3DSwapChains[pHandle] = pSwapChain;
        }
        SEND_OPTIONAL_CREATE_FUNCTION_SERVER_RESPONSE(hresult, currentUID);
        break;
      }
      case IDirect3DDevice9Ex_GetSwapChain:
      {
        GET_RES(pD3DDevice, gpD3DDevices);
        PULL_U(iSwapChain);
        IDirect3DSwapChain9* pSwapChain = nullptr;
        const auto hresult = pD3DDevice->GetSwapChain(iSwapChain, &pSwapChain);
        assert(SUCCEEDED(hresult));
        assert(pSwapChain != nullptr);
        break;
      }
      case IDirect3DDevice9Ex_GetNumberOfSwapChains:
      {
        GET_RES(pD3DDevice, gpD3DDevices);
        PULL_U(orig_cnt);
        const auto cnt = pD3DDevice->GetNumberOfSwapChains();
        assert(orig_cnt == cnt);
        break;
      }
      case IDirect3DDevice9Ex_Reset:
      {
        GET_RES(pD3DDevice, gpD3DDevices);
        uint32_t* rawPresentationParameters = nullptr;
        DeviceBridge::get_data((void**) &rawPresentationParameters);
        D3DPRESENT_PARAMETERS PresentationParameters = getPresParamFromRaw(rawPresentationParameters);
        if (!PresentationParameters.Windowed && !bDxvkModuleLoaded) {
          bridge_util::Logger::err("Fullscreen is not yet supported for non-DXVK uses of the bridge. This is not recoverable. Exiting.");
          done = true;
        }

        //Release implicit swapchain
        IDirect3DSwapChain9* pSwapChain = nullptr;
        pD3DDevice->GetSwapChain(0, &pSwapChain);
        pSwapChain->Release();

        const auto hresult = pD3DDevice->Reset(&PresentationParameters);
        assert(SUCCEEDED(hresult));
        SEND_OPTIONAL_SERVER_RESPONSE(hresult, currentUID);
        break;
      }
      case IDirect3DDevice9Ex_ResetEx:
      {
        GET_RES(pD3DDevice, gpD3DDevices);
        uint32_t* rawPresentationParameters = nullptr;
        DeviceBridge::get_data((void**) &rawPresentationParameters);

        D3DDISPLAYMODEEX* pFullscreenDisplayMode = nullptr;
        PULL_DATA(sizeof(D3DDISPLAYMODEEX), pFullscreenDisplayMode);

        D3DPRESENT_PARAMETERS PresentationParameters = getPresParamFromRaw(rawPresentationParameters);
        if (!PresentationParameters.Windowed && !bDxvkModuleLoaded) {
          bridge_util::Logger::err("Fullscreen is not yet supported for non-DXVK uses of the bridge. This is not recoverable. Exiting.");
          done = true;
        }

        //Release implicit swapchain
        IDirect3DSwapChain9* pSwapChain = nullptr;
        pD3DDevice->GetSwapChain(0, &pSwapChain);
        pSwapChain->Release();

        const auto hresult = ((IDirect3DDevice9Ex*) pD3DDevice)->ResetEx(&PresentationParameters, pFullscreenDisplayMode);
        assert(SUCCEEDED(hresult));
        SEND_OPTIONAL_SERVER_RESPONSE(hresult, currentUID);
        break;
      }
      case IDirect3DDevice9Ex_Present:
      {
        FrameMark;
#ifdef ENABLE_PRESENT_SEMAPHORE_TRACE
        Logger::trace("Server side Present call received, releasing semaphore...");
#endif

        GET_RES(pD3DDevice, gpD3DDevices);
        PULL_OBJ(RECT, pSourceRect);
        PULL_OBJ(RECT, pDestRect);
        PULL(uint32_t, hDestWindowOverride);
        PULL_OBJ(RGNDATA, pDirtyRegion);

        HWND hwnd = TRUNCATE_HANDLE(HWND, hDestWindowOverride);

        const auto hresult = pD3DDevice->Present(pSourceRect, pDestRect, hwnd, pDirtyRegion);
        if (!SUCCEEDED(hresult)) {
          std::stringstream ss;
          ss << "Present() failed! Check all logs for reported errors.";
          Logger::err(ss.str());
        }

        // If we're syncing with the client on Present() then trigger the semaphore now
        if (GlobalOptions::getPresentSemaphoreEnabled()) {
          gpPresent->release();
#ifdef ENABLE_PRESENT_SEMAPHORE_TRACE
          Logger::trace("Present semaphore released successfully.");
#endif
        }
        break;
      }
      case IDirect3DDevice9Ex_GetBackBuffer:
      {
        GET_RES(pD3DDevice, gpD3DDevices);
        PULL(uint32_t, iSwapChain);
        PULL(uint32_t, iBackBuffer);
        PULL_HND(pSurfaceHandle);
        IDirect3DSurface9* pBackbuffer = nullptr;
        const auto hresult = pD3DDevice->GetBackBuffer(iSwapChain, iBackBuffer, D3DBACKBUFFER_TYPE_MONO, &pBackbuffer);
        assert(SUCCEEDED(hresult));
        if (SUCCEEDED(hresult)) {
          gpD3DResources[pSurfaceHandle] = pBackbuffer;
        }
        break;
      }
      case IDirect3DDevice9Ex_GetRasterStatus:
        break;
      case IDirect3DDevice9Ex_SetDialogBoxMode:
      {
        GET_RES(pD3DDevice, gpD3DDevices);
        PULL(BOOL, bEnableDialogs);
        const auto hresult = pD3DDevice->SetDialogBoxMode(bEnableDialogs);
        SEND_OPTIONAL_SERVER_RESPONSE(hresult, currentUID);
        assert(SUCCEEDED(hresult));
        break;
      }
      case IDirect3DDevice9Ex_SetGammaRamp:
      {
        GET_RES(pD3DDevice, gpD3DDevices);
        PULL_U(iSwapChain);
        PULL_D(Flags);
        PULL_OBJ(D3DGAMMARAMP, pRamp);
        pD3DDevice->SetGammaRamp(iSwapChain, Flags, pRamp);
        break;
      }
      case IDirect3DDevice9Ex_GetGammaRamp:
      {
        GET_RES(pD3DDevice, gpD3DDevices);
        PULL_U(iSwapChain);
        D3DGAMMARAMP pRamp;
        pD3DDevice->GetGammaRamp(iSwapChain, &pRamp);
        break;
      }
      case IDirect3DDevice9Ex_CreateTexture:
      {
        GET_RES(pD3DDevice, gpD3DDevices);
        PULL_U(Width);
        PULL_U(Height);
        PULL_U(Levels);
        PULL_D(Usage);
        PULL(D3DFORMAT, Format);
        PULL(D3DPOOL, Pool);
        PULL_HND(pHandle);
        LPDIRECT3DTEXTURE9 pTexture;
        const auto hresult = pD3DDevice->CreateTexture(IN Width, IN Height, IN Levels, IN Usage, IN Format, IN Pool, OUT & pTexture, IN nullptr);
        if (SUCCEEDED(hresult)) {
          gpD3DResources[pHandle] = pTexture;
        }
        assert(SUCCEEDED(hresult));
        SEND_OPTIONAL_CREATE_FUNCTION_SERVER_RESPONSE(hresult, currentUID);
        break;
      }
      case IDirect3DDevice9Ex_CreateVolumeTexture:
      {
        GET_RES(pD3DDevice, gpD3DDevices);
        PULL_U(Width);
        PULL_U(Height);
        PULL_U(Depth);
        PULL_U(Levels);
        PULL_D(Usage);
        PULL(D3DFORMAT, Format);
        PULL(D3DPOOL, Pool);
        PULL_HND(pHandle);
        LPDIRECT3DVOLUMETEXTURE9 pVolumeTexture;
        const auto hresult = pD3DDevice->CreateVolumeTexture(IN Width, IN Height, IN Depth, IN Levels, IN Usage, IN Format, IN Pool, OUT & pVolumeTexture, IN nullptr);
        if (SUCCEEDED(hresult)) {
          gpD3DResources[pHandle] = pVolumeTexture;
        }
        assert(SUCCEEDED(hresult));
        SEND_OPTIONAL_CREATE_FUNCTION_SERVER_RESPONSE(hresult, currentUID);
        break;
      }
      case IDirect3DDevice9Ex_CreateCubeTexture:
      {
        GET_RES(pD3DDevice, gpD3DDevices);
        PULL_U(EdgeLength);
        PULL_U(Levels);
        PULL_D(Usage);
        PULL(D3DFORMAT, Format);
        PULL(D3DPOOL, Pool);
        PULL_HND(pHandle);
        LPDIRECT3DCUBETEXTURE9 pCubeTexture;
        const auto hresult = pD3DDevice->CreateCubeTexture(IN EdgeLength, IN Levels, IN Usage, IN Format, IN Pool, OUT & pCubeTexture, IN nullptr);
        if (SUCCEEDED(hresult)) {
          gpD3DResources[pHandle] = pCubeTexture;
        }
        assert(SUCCEEDED(hresult));
        SEND_OPTIONAL_CREATE_FUNCTION_SERVER_RESPONSE(hresult, currentUID);
        break;
      }
      case IDirect3DDevice9Ex_CreateVertexBuffer:
      {
        GET_RES(pD3DDevice, gpD3DDevices);
        PULL_U(Length);
        PULL_D(Usage);
        PULL_D(FVF);
        PULL(D3DPOOL, Pool);
        PULL_HND(pHandle);
        LPDIRECT3DVERTEXBUFFER9 pVertexBuffer;
        const auto hresult = pD3DDevice->CreateVertexBuffer(IN Length, IN Usage, IN FVF, IN Pool, OUT & pVertexBuffer, IN nullptr);
        if (SUCCEEDED(hresult)) {
          gpD3DResources[pHandle] = pVertexBuffer;
        }
        assert(SUCCEEDED(hresult));
        SEND_OPTIONAL_CREATE_FUNCTION_SERVER_RESPONSE(hresult, currentUID);
        break;
      }
      case IDirect3DDevice9Ex_CreateIndexBuffer:
      {
        GET_RES(pD3DDevice, gpD3DDevices);
        PULL_U(Length);
        PULL_D(Usage);
        PULL(D3DFORMAT, Format);
        PULL(D3DPOOL, Pool);
        PULL_HND(pHandle);
        LPDIRECT3DINDEXBUFFER9 pIndexBuffer;
        const auto hresult = pD3DDevice->CreateIndexBuffer(IN Length, IN Usage, IN Format, IN Pool, OUT & pIndexBuffer, IN nullptr);
        if (SUCCEEDED(hresult)) {
          gpD3DResources[pHandle] = pIndexBuffer;
        }
        assert(SUCCEEDED(hresult));
        SEND_OPTIONAL_CREATE_FUNCTION_SERVER_RESPONSE(hresult, currentUID);
        break;
      }
      case IDirect3DDevice9Ex_CreateRenderTarget:
      {
        GET_RES(pD3DDevice, gpD3DDevices);
        PULL_U(Width);
        PULL_U(Height);
        PULL(D3DFORMAT, Format);
        PULL(D3DMULTISAMPLE_TYPE, MultiSample);
        PULL_D(MultisampleQuality);
        PULL(BOOL, Lockable);
        PULL_HND(pHandle);
        LPDIRECT3DSURFACE9 pSurface;
        const auto hresult = pD3DDevice->CreateRenderTarget(IN Width, IN Height, IN Format, IN MultiSample, IN MultisampleQuality, IN Lockable, OUT & pSurface, IN nullptr);
        if (SUCCEEDED(hresult)) {
          gpD3DResources[pHandle] = pSurface;
        }
        assert(SUCCEEDED(hresult));
        SEND_OPTIONAL_CREATE_FUNCTION_SERVER_RESPONSE(hresult, currentUID);
        break;
      }
      case IDirect3DDevice9Ex_CreateDepthStencilSurface:
      {
        GET_RES(pD3DDevice, gpD3DDevices);
        PULL_U(Width);
        PULL_U(Height);
        PULL(D3DFORMAT, Format);
        PULL(D3DMULTISAMPLE_TYPE, MultiSample);
        PULL_D(MultisampleQuality);
        PULL(BOOL, Discard);
        PULL_HND(pHandle);
        LPDIRECT3DSURFACE9 pSurface;
        const auto hresult = pD3DDevice->CreateDepthStencilSurface(IN Width, IN Height, IN Format, IN MultiSample, IN MultisampleQuality, IN Discard, OUT & pSurface, IN nullptr);
        if (SUCCEEDED(hresult)) {
          gpD3DResources[pHandle] = pSurface;
        }
        assert(SUCCEEDED(hresult));
        SEND_OPTIONAL_CREATE_FUNCTION_SERVER_RESPONSE(hresult, currentUID);
        break;
      }
      case IDirect3DDevice9Ex_UpdateSurface:
      {
        HRESULT hresult = D3DERR_INVALIDCALL;
        GET_RES(pD3DDevice, gpD3DDevices);
        PULL_HND(pSourceHandle);
        PULL_OBJ(RECT, pSourceRect);
        PULL_HND(pDestHandle);
        PULL_OBJ(POINT, pDestPoint);
        const auto& pSourceSurface = (IDirect3DSurface9*) gpD3DResources[pSourceHandle];
        assert(pSourceSurface != nullptr);
        const auto& pDestinationSurface = (IDirect3DSurface9*) gpD3DResources[pDestHandle];
        assert(pDestinationSurface != nullptr);
        if (pSourceSurface != nullptr && pDestinationSurface != nullptr) {
          hresult = pD3DDevice->UpdateSurface(IN pSourceSurface, IN pSourceRect, IN pDestinationSurface, IN pDestPoint);
          assert(SUCCEEDED(hresult));
        }
        SEND_OPTIONAL_SERVER_RESPONSE(hresult, currentUID);
        break;
      }
      case IDirect3DDevice9Ex_UpdateTexture:
      {
        HRESULT hresult = D3DERR_INVALIDCALL;
        GET_RES(pD3DDevice, gpD3DDevices);
        PULL_HND(pSourceTextureHandle);
        PULL_HND(pDestinationTextureHandle);
        const auto& pSourceTexture = (IDirect3DBaseTexture9*) gpD3DResources[pSourceTextureHandle];
        assert(pSourceTexture != nullptr);
        const auto& pDestinationTexture = (IDirect3DBaseTexture9*) gpD3DResources[pDestinationTextureHandle];
        assert(pDestinationTexture != nullptr);
        if (pSourceTexture != nullptr && pDestinationTexture != nullptr) {
          hresult = pD3DDevice->UpdateTexture(IN pSourceTexture, IN pDestinationTexture);
          assert(SUCCEEDED(hresult));
        }
        SEND_OPTIONAL_SERVER_RESPONSE(hresult, currentUID);
        break;
      }
      case IDirect3DDevice9Ex_GetRenderTargetData:
      {
        GET_RES(pD3DDevice, gpD3DDevices);
        PULL_HND(pRenderTargetHandle);
        PULL_HND(pDestSurfaceHandle);
        const auto& pRenderTarget = (IDirect3DSurface9*) gpD3DResources[pRenderTargetHandle];
        const auto& pDestSurface = (IDirect3DSurface9*) gpD3DResources[pDestSurfaceHandle];
        auto hresult = pD3DDevice->GetRenderTargetData(IN pRenderTarget, IN pDestSurface);
        hresult = ReturnSurfaceDataToClient(pDestSurface, hresult, currentUID);
        assert(SUCCEEDED(hresult));
        break;
      }
      case IDirect3DDevice9Ex_GetFrontBufferData:
      {
        GET_RES(pD3DDevice, gpD3DDevices);
        PULL(uint32_t, iSwapChain);
        PULL_HND(pDestSurfaceHandle);
        const auto& pDestSurface = (IDirect3DSurface9*) gpD3DResources[pDestSurfaceHandle];
        IDirect3DSurface9* pBackbuffer = nullptr;
        auto hresult = pD3DDevice->GetFrontBufferData(IN iSwapChain, IN pDestSurface);
        hresult = ReturnSurfaceDataToClient(pDestSurface, hresult, currentUID);
        assert(SUCCEEDED(hresult));
        break;
      }
      case IDirect3DDevice9Ex_StretchRect:
      {
        GET_RES(pD3DDevice, gpD3DDevices);
        PULL_HND(pSourceHandle);
        PULL_OBJ(RECT, pSourceRect);
        PULL_HND(pDestHandle);
        PULL_OBJ(RECT, pDestRect);
        PULL(D3DTEXTUREFILTERTYPE, Filter);
        const auto& pSourceSurface = (IDirect3DSurface9*) gpD3DResources[pSourceHandle];
        const auto& pDestSurface = (IDirect3DSurface9*) gpD3DResources[pDestHandle];
        const auto hresult = pD3DDevice->StretchRect(IN pSourceSurface, IN pSourceRect, IN pDestSurface, IN pDestRect, IN Filter);
        assert(SUCCEEDED(hresult));
        SEND_OPTIONAL_SERVER_RESPONSE(hresult, currentUID);
        break;
      }
      case IDirect3DDevice9Ex_ColorFill:
      {
        GET_RES(pD3DDevice, gpD3DDevices);
        PULL_HND(pHandle);
        PULL_OBJ(RECT, pRect);
        PULL_OBJ(D3DCOLOR, color);
        const auto& pSurface = (IDirect3DSurface9*) gpD3DResources[pHandle];
        const auto hresult = pD3DDevice->ColorFill(IN pSurface, IN pRect, IN * color);
        assert(SUCCEEDED(hresult));
        SEND_OPTIONAL_SERVER_RESPONSE(hresult, currentUID);
        break;
      }
      case IDirect3DDevice9Ex_CreateOffscreenPlainSurface:
      {
        GET_RES(pD3DDevice, gpD3DDevices);
        PULL_U(Width);
        PULL_U(Height);
        PULL(D3DFORMAT, Format);
        PULL(D3DPOOL, Pool);
        PULL_HND(pHandle);
        LPDIRECT3DSURFACE9 pSurface;
        const auto hresult = pD3DDevice->CreateOffscreenPlainSurface(IN Width, IN Height, IN Format, IN Pool, OUT & pSurface, IN nullptr);
        if (SUCCEEDED(hresult)) {
          gpD3DResources[pHandle] = pSurface;
        }
        assert(SUCCEEDED(hresult));
        SEND_OPTIONAL_CREATE_FUNCTION_SERVER_RESPONSE(hresult, currentUID);
        break;
      }
      case IDirect3DDevice9Ex_SetRenderTarget:
      {
        HRESULT hresult = D3DERR_INVALIDCALL;
        GET_RES(pD3DDevice, gpD3DDevices);
        PULL_D(RenderTargetIndex);
        PULL_U(pHandle);
        IDirect3DSurface9* pRenderTarget = nullptr;
        if (pHandle != NULL) {
          pRenderTarget = (IDirect3DSurface9*) gpD3DResources[pHandle];
        }
        assert((pHandle != 0 && pRenderTarget != 0) || pHandle == 0);
        if ((pHandle != 0 && pRenderTarget != 0) || pHandle == 0) {
          hresult = pD3DDevice->SetRenderTarget(IN RenderTargetIndex, IN pRenderTarget);
          assert(SUCCEEDED(hresult));
        }
        SEND_OPTIONAL_SERVER_RESPONSE(hresult, currentUID);
        break;
      }
      case IDirect3DDevice9Ex_GetRenderTarget:
      {
        GET_RES(pD3DDevice, gpD3DDevices);
        PULL_D(RenderTargetIndex);
        PULL_HND(pSurfaceHandle);
        IDirect3DSurface9* pRenderTarget = nullptr;
        const auto hresult = pD3DDevice->GetRenderTarget(RenderTargetIndex, &pRenderTarget);
        if (SUCCEEDED(hresult)) {
          gpD3DResources[pSurfaceHandle] = pRenderTarget;
        }
        SEND_OPTIONAL_SERVER_RESPONSE(hresult, currentUID);
        assert(SUCCEEDED(hresult));
        break;
      }
      case IDirect3DDevice9Ex_SetDepthStencilSurface:
      {
        HRESULT hresult = D3DERR_INVALIDCALL;
        GET_RES(pD3DDevice, gpD3DDevices);
        PULL_U(pHandle);
        IDirect3DSurface9* pDepthStencil = nullptr;
        if (pHandle != NULL) {
          pDepthStencil = (IDirect3DSurface9*) gpD3DResources[pHandle];
        }
        assert((pHandle != 0 && pDepthStencil != 0) || pHandle == 0);
        if ((pHandle != 0 && pDepthStencil != 0) || pHandle == 0) {
          hresult = pD3DDevice->SetDepthStencilSurface(IN pDepthStencil);
          assert(SUCCEEDED(hresult));
        }
        SEND_OPTIONAL_SERVER_RESPONSE(hresult, currentUID);
        break;
      }
      case IDirect3DDevice9Ex_GetDepthStencilSurface:
      {
        GET_RES(pD3DDevice, gpD3DDevices);
        PULL_HND(pSurfaceHandle);
        IDirect3DSurface9* pZStencilSurface = nullptr;
        const auto hresult = pD3DDevice->GetDepthStencilSurface(&pZStencilSurface);
        if (SUCCEEDED(hresult)) {
          gpD3DResources[pSurfaceHandle] = pZStencilSurface;
        }
        assert(SUCCEEDED(hresult));
        SEND_OPTIONAL_SERVER_RESPONSE(hresult, currentUID);
        break;
      }
      case IDirect3DDevice9Ex_BeginScene:
      {
        GET_RES(pD3DDevice, gpD3DDevices);
        const auto hresult = pD3DDevice->BeginScene();
        assert(SUCCEEDED(hresult));
        SEND_OPTIONAL_SERVER_RESPONSE(hresult, currentUID);
        break;
      }
      case IDirect3DDevice9Ex_EndScene:
      {
        GET_RES(pD3DDevice, gpD3DDevices);
        const auto hresult = pD3DDevice->EndScene();
        assert(SUCCEEDED(hresult));
        SEND_OPTIONAL_SERVER_RESPONSE(hresult, currentUID);
        break;
      }
      case IDirect3DDevice9Ex_Clear:
      {
        GET_RES(pD3DDevice, gpD3DDevices);
        PULL_D(Count);
        PULL_D(Flags);
        PULL_OBJ(float, Z);
        PULL_D(Stencil);
        D3DRECT* pRects = nullptr;
        PULL_DATA(sizeof(D3DRECT) * Count, pRects);
        D3DCOLOR* Color = nullptr;
        PULL_DATA(sizeof(D3DCOLOR), Color);
        const auto hresult = pD3DDevice->Clear(Count, pRects, Flags, *Color, *Z, Stencil);
        assert(SUCCEEDED(hresult));
        SEND_OPTIONAL_SERVER_RESPONSE(hresult, currentUID);
        break;
      }
      case IDirect3DDevice9Ex_SetTransform:
      {
        GET_RES(pD3DDevice, gpD3DDevices);
        PULL(D3DTRANSFORMSTATETYPE, State);
        D3DMATRIX* pMatrix = nullptr;
        PULL_DATA(sizeof(D3DMATRIX), pMatrix);
        const auto hresult = pD3DDevice->SetTransform(State, pMatrix);
        assert(SUCCEEDED(hresult));
        SEND_OPTIONAL_SERVER_RESPONSE(hresult, currentUID);
        break;
      }
      case IDirect3DDevice9Ex_GetTransform:
        break;
      case IDirect3DDevice9Ex_MultiplyTransform:
        break;
      case IDirect3DDevice9Ex_SetViewport:
      {
        GET_RES(pD3DDevice, gpD3DDevices);
        PULL_OBJ(D3DVIEWPORT9, pViewport);
        const auto hresult = pD3DDevice->SetViewport(pViewport);
        assert(SUCCEEDED(hresult));
        SEND_OPTIONAL_SERVER_RESPONSE(hresult, currentUID);
        break;
      }
      case IDirect3DDevice9Ex_GetViewport:
        break;
      case IDirect3DDevice9Ex_SetMaterial:
      {
        GET_RES(pD3DDevice, gpD3DDevices);
        D3DMATERIAL9* pMaterial = nullptr;
        PULL_DATA(sizeof(D3DMATERIAL9), pMaterial);
        const auto hresult = pD3DDevice->SetMaterial(IN pMaterial);
        assert(SUCCEEDED(hresult));
        SEND_OPTIONAL_SERVER_RESPONSE(hresult, currentUID);
        break;
      }
      case IDirect3DDevice9Ex_GetMaterial:
        break;
      case IDirect3DDevice9Ex_SetLight:
      {
        GET_RES(pD3DDevice, gpD3DDevices);
        PULL_D(Index);
        D3DLIGHT9* pLight = nullptr;
        PULL_DATA(sizeof(D3DLIGHT9), pLight);
        const auto hresult = pD3DDevice->SetLight(IN Index, IN pLight);
        assert(SUCCEEDED(hresult));
        SEND_OPTIONAL_SERVER_RESPONSE(hresult, currentUID);
        break;
      }
      case IDirect3DDevice9Ex_GetLight:
        break;
      case IDirect3DDevice9Ex_LightEnable:
      {
        GET_RES(pD3DDevice, gpD3DDevices);
        PULL_D(LightIndex);
        PULL_U(bEnable);
        const auto hresult = pD3DDevice->LightEnable(IN LightIndex, IN bEnable);
        assert(SUCCEEDED(hresult));
        SEND_OPTIONAL_SERVER_RESPONSE(hresult, currentUID);
        break;
      }
      case IDirect3DDevice9Ex_GetLightEnable:
        break;
      case IDirect3DDevice9Ex_SetClipPlane:
      {
        GET_RES(pD3DDevice, gpD3DDevices);
        PULL_D(Index);
        float* pPlane = nullptr;
        PULL_DATA(sizeof(float) * 4, pPlane);
        const auto hresult = pD3DDevice->SetClipPlane(IN Index, IN pPlane);
        assert(SUCCEEDED(hresult));
        SEND_OPTIONAL_SERVER_RESPONSE(hresult, currentUID);
        break;
      }
      case IDirect3DDevice9Ex_GetClipPlane:
        break;
      case IDirect3DDevice9Ex_SetRenderState:
      {
        GET_RES(pD3DDevice, gpD3DDevices);
        PULL(D3DRENDERSTATETYPE, State);
        PULL_D(Value);
        const auto hresult = pD3DDevice->SetRenderState(IN State, IN Value);
        assert(SUCCEEDED(hresult));
        SEND_OPTIONAL_SERVER_RESPONSE(hresult, currentUID);
        break;
      }
      case IDirect3DDevice9Ex_GetRenderState:
        break;
      case IDirect3DDevice9Ex_CreateStateBlock:
      {
        GET_RES(pD3DDevice, gpD3DDevices);
        PULL_D(Type);
        PULL_HND(pHandle);
        IDirect3DStateBlock9* pSB;
        const auto hresult = pD3DDevice->CreateStateBlock((D3DSTATEBLOCKTYPE) Type, &pSB);
        if (SUCCEEDED(hresult)) {
          gpD3DStateBlocks[pHandle] = pSB;
        }
        assert(SUCCEEDED(hresult));
        SEND_OPTIONAL_CREATE_FUNCTION_SERVER_RESPONSE(hresult, currentUID);
        break;
      }
      case IDirect3DDevice9Ex_BeginStateBlock:
      {
        GET_RES(pD3DDevice, gpD3DDevices);
        const auto hresult = pD3DDevice->BeginStateBlock();
        assert(SUCCEEDED(hresult));
        SEND_OPTIONAL_SERVER_RESPONSE(hresult, currentUID);
        break;
      }
      case IDirect3DDevice9Ex_EndStateBlock:
      {
        GET_RES(pD3DDevice, gpD3DDevices);
        PULL_HND(pHandle);
        IDirect3DStateBlock9* pSB;
        const auto hresult = pD3DDevice->EndStateBlock(&pSB);
        if (SUCCEEDED(hresult)) {
          gpD3DStateBlocks[pHandle] = pSB;
        }
        assert(SUCCEEDED(hresult));
        SEND_OPTIONAL_SERVER_RESPONSE(hresult, currentUID);
        break;
      }
      case IDirect3DDevice9Ex_SetClipStatus:
        break;
      case IDirect3DDevice9Ex_GetClipStatus:
        break;
      case IDirect3DDevice9Ex_GetTexture:
        break;
      case IDirect3DDevice9Ex_SetTexture:
      {
        GET_RES(pD3DDevice, gpD3DDevices);
        PULL_D(Stage);
        PULL_U(pHandle);
        IDirect3DBaseTexture9* pTexture = nullptr;
        if (pHandle != NULL) {
          pTexture = (IDirect3DBaseTexture9*) gpD3DResources[pHandle];
          assert(pTexture != nullptr);
        }
        const auto hresult = pD3DDevice->SetTexture(IN Stage, IN pTexture);
        assert(SUCCEEDED(hresult));
        SEND_OPTIONAL_SERVER_RESPONSE(hresult, currentUID);
        break;
      }
      case IDirect3DDevice9Ex_GetTextureStageState:
        break;
      case IDirect3DDevice9Ex_SetTextureStageState:
      {
        GET_RES(pD3DDevice, gpD3DDevices);
        PULL_D(Stage);
        PULL(D3DTEXTURESTAGESTATETYPE, Type);
        PULL_D(Value);
        const auto hresult = pD3DDevice->SetTextureStageState(IN Stage, IN Type, IN Value);
        assert(SUCCEEDED(hresult));
        SEND_OPTIONAL_SERVER_RESPONSE(hresult, currentUID);
        break;
      }
      case IDirect3DDevice9Ex_GetSamplerState:
        break;
      case IDirect3DDevice9Ex_SetSamplerState:
      {
        GET_RES(pD3DDevice, gpD3DDevices);
        PULL_D(Sampler);
        PULL(D3DSAMPLERSTATETYPE, Type);
        PULL_D(Value);
        const auto hresult = pD3DDevice->SetSamplerState(IN Sampler, IN Type, IN Value);
        assert(SUCCEEDED(hresult));
        SEND_OPTIONAL_SERVER_RESPONSE(hresult, currentUID);
        break;
      }
      case IDirect3DDevice9Ex_ValidateDevice:
        break;
      case IDirect3DDevice9Ex_SetPaletteEntries:
        break;
      case IDirect3DDevice9Ex_GetPaletteEntries:
        break;
      case IDirect3DDevice9Ex_SetCurrentTexturePalette:
        break;
      case IDirect3DDevice9Ex_GetCurrentTexturePalette:
        break;
      case IDirect3DDevice9Ex_SetScissorRect:
      {
        GET_RES(pD3DDevice, gpD3DDevices);
        PULL_OBJ(RECT, pRect);
        const auto hresult = pD3DDevice->SetScissorRect(pRect);
        assert(SUCCEEDED(hresult));
        SEND_OPTIONAL_SERVER_RESPONSE(hresult, currentUID);
        break;
      }
      case IDirect3DDevice9Ex_GetScissorRect:
        break;
      case IDirect3DDevice9Ex_SetSoftwareVertexProcessing:
      {
        GET_RES(pD3DDevice, gpD3DDevices);
        PULL(BOOL, bSoftware);
        const auto hresult = pD3DDevice->SetSoftwareVertexProcessing(bSoftware);
        assert(SUCCEEDED(hresult));
        {
          ServerMessage c(Commands::Bridge_Response, currentUID);
          c.send_data(hresult);
        }
        break;
      }
      case IDirect3DDevice9Ex_GetSoftwareVertexProcessing:
        break;
      case IDirect3DDevice9Ex_SetNPatchMode:
      {
        GET_RES(pD3DDevice, gpD3DDevices);
        PULL_OBJ(float, nSegments);
        const auto hresult = pD3DDevice->SetNPatchMode(*nSegments);
        assert(SUCCEEDED(hresult));
        SEND_OPTIONAL_SERVER_RESPONSE(hresult, currentUID);
        break;
      }
      case IDirect3DDevice9Ex_GetNPatchMode:
        break;
      case IDirect3DDevice9Ex_DrawPrimitive:
      {
        GET_RES(pD3DDevice, gpD3DDevices);
        PULL(D3DPRIMITIVETYPE, PrimitiveType);
        PULL_U(StartVertex);
        PULL_U(PrimitiveCount);
        const auto hresult = pD3DDevice->DrawPrimitive(IN PrimitiveType, IN StartVertex, IN PrimitiveCount);
        assert(SUCCEEDED(hresult));
        SEND_OPTIONAL_SERVER_RESPONSE(hresult, currentUID);
        break;
      }
      case IDirect3DDevice9Ex_DrawIndexedPrimitive:
      {
        GET_RES(pD3DDevice, gpD3DDevices);
        PULL(D3DPRIMITIVETYPE, Type);
        PULL_I(BaseVertexIndex);
        PULL_U(MinVertexIndex);
        PULL_U(NumVertices);
        PULL_U(startIndex);
        PULL_U(primCount);
        const auto hresult = pD3DDevice->DrawIndexedPrimitive(IN Type, IN BaseVertexIndex, IN MinVertexIndex, IN NumVertices, IN startIndex, IN primCount);
        assert(SUCCEEDED(hresult));
        SEND_OPTIONAL_SERVER_RESPONSE(hresult, currentUID);
        break;
      }
      case IDirect3DDevice9Ex_DrawPrimitiveUP:
      {
        GET_RES(pD3DDevice, gpD3DDevices);
        PULL(D3DPRIMITIVETYPE, PrimitiveType);
        PULL_U(PrimitiveCount);
        void* pVertexStreamZeroData = nullptr;
        DeviceBridge::get_data(&pVertexStreamZeroData);
        PULL_U(VertexStreamZeroStride);
        const auto hresult = pD3DDevice->DrawPrimitiveUP(IN PrimitiveType, IN PrimitiveCount, IN pVertexStreamZeroData, IN VertexStreamZeroStride);
        assert(SUCCEEDED(hresult));
        SEND_OPTIONAL_SERVER_RESPONSE(hresult, currentUID);
        break;
      }
      case IDirect3DDevice9Ex_DrawIndexedPrimitiveUP:
      {
        GET_RES(pD3DDevice, gpD3DDevices);
        PULL(D3DPRIMITIVETYPE, PrimitiveType);
        PULL_U(MinVertexIndex);
        PULL_U(NumVertices);
        PULL_U(PrimitiveCount);
        PULL(D3DFORMAT, IndexDataFormat);
        PULL_U(VertexStreamZeroStride);

        void* pIndexData = nullptr;
        DeviceBridge::get_data(&pIndexData);
        void* pVertexStreamZeroData = nullptr;
        DeviceBridge::get_data(&pVertexStreamZeroData);

        const auto hresult = pD3DDevice->DrawIndexedPrimitiveUP(IN PrimitiveType, IN MinVertexIndex, IN NumVertices, IN PrimitiveCount, IN pIndexData, IN IndexDataFormat, IN pVertexStreamZeroData, IN VertexStreamZeroStride);
        assert(SUCCEEDED(hresult));
        SEND_OPTIONAL_SERVER_RESPONSE(hresult, currentUID);
        break;
      }
      case IDirect3DDevice9Ex_ProcessVertices:
      {
        GET_RES(pD3DDevice, gpD3DDevices);
        PULL_U(SrcStartIndex);
        PULL_U(DestIndex);
        PULL_U(VertexCount);
        PULL_HND(pVertexBufferHandle);
        PULL_HND(pVertexDeclHandle);
        PULL(DWORD, Flags);

        const auto& pVertexDecl = gpD3DVertexDeclarations[pVertexDeclHandle];
        const auto& pDestBuffer = (IDirect3DVertexBuffer9*) gpD3DResources[pVertexBufferHandle];

        const auto hresult = pD3DDevice->ProcessVertices(SrcStartIndex, DestIndex, VertexCount, pDestBuffer, pVertexDecl, Flags);
        assert(SUCCEEDED(hresult));
        SEND_OPTIONAL_SERVER_RESPONSE(hresult, currentUID);
        break;
      }
      case IDirect3DDevice9Ex_CreateVertexDeclaration:
      {
        GET_RES(pD3DDevice, gpD3DDevices);
        PULL_U(numOfElements);
        D3DVERTEXELEMENT9* pVertexElements = nullptr;
        PULL_DATA(sizeof(D3DVERTEXELEMENT9) * numOfElements, pVertexElements);
        PULL_HND(pHandle);
        LPDIRECT3DVERTEXDECLARATION9 pDecl;
        const auto hresult = pD3DDevice->CreateVertexDeclaration(IN pVertexElements, OUT & pDecl);
        if (SUCCEEDED(hresult)) {
          gpD3DVertexDeclarations[pHandle] = pDecl;
        }
        assert(SUCCEEDED(hresult));
        SEND_OPTIONAL_CREATE_FUNCTION_SERVER_RESPONSE(hresult, currentUID);
        break;
      }
      case IDirect3DDevice9Ex_SetVertexDeclaration:
      {
        GET_RES(pD3DDevice, gpD3DDevices);
        PULL_U(pHandle);
        IDirect3DVertexDeclaration9* pVertexDecl = nullptr;
        if (pHandle != NULL) {
          pVertexDecl = (IDirect3DVertexDeclaration9*) gpD3DVertexDeclarations[pHandle];
        }
        const auto hresult = pD3DDevice->SetVertexDeclaration(IN pVertexDecl);
        assert(SUCCEEDED(hresult));
        SEND_OPTIONAL_SERVER_RESPONSE(hresult, currentUID);
        break;
      }
      case IDirect3DDevice9Ex_GetVertexDeclaration:
        break;
      case IDirect3DDevice9Ex_SetFVF:
      {
        GET_RES(pD3DDevice, gpD3DDevices);
        PULL_D(FVF);
        const auto hresult = pD3DDevice->SetFVF(IN FVF);
        assert(SUCCEEDED(hresult));
        SEND_OPTIONAL_SERVER_RESPONSE(hresult, currentUID);
        break;
      }
      case IDirect3DDevice9Ex_GetFVF:
        break;
      case IDirect3DDevice9Ex_CreateVertexShader:
      {
        GET_RES(pD3DDevice, gpD3DDevices);
        PULL_HND(pHandle);
        PULL_U(dataSize);
        DWORD* pFunction = nullptr;
        PULL_DATA(dataSize, pFunction);
        IDirect3DVertexShader9* pShader = nullptr;
        const auto hresult = pD3DDevice->CreateVertexShader(IN pFunction, OUT & pShader);
        if (SUCCEEDED(hresult)) {
          gpD3DVertexShaders[pHandle] = pShader;
        }
        assert(SUCCEEDED(hresult));
        SEND_OPTIONAL_CREATE_FUNCTION_SERVER_RESPONSE(hresult, currentUID);
        break;
      }
      case IDirect3DDevice9Ex_SetVertexShader:
      {
        GET_RES(pD3DDevice, gpD3DDevices);
        PULL_U(pHandle);
        IDirect3DVertexShader9* pShader = nullptr;
        if (pHandle != NULL) {
          pShader = gpD3DVertexShaders[pHandle];
        }
        const auto hresult = pD3DDevice->SetVertexShader(IN pShader);
        assert(SUCCEEDED(hresult));
        SEND_OPTIONAL_SERVER_RESPONSE(hresult, currentUID);
        break;
      }
      case IDirect3DDevice9Ex_GetVertexShader:
        break;
      case IDirect3DDevice9Ex_SetVertexShaderConstantF:
      {
        GET_RES(pD3DDevice, gpD3DDevices);
        PULL_U(StartRegister);
        PULL_U(Count);
        float* pConstantData = nullptr;
        PULL_DATA(Count * sizeof(float) * 4, pConstantData);
        const auto hresult = pD3DDevice->SetVertexShaderConstantF(IN StartRegister, IN pConstantData, IN Count);
        assert(SUCCEEDED(hresult));
        SEND_OPTIONAL_SERVER_RESPONSE(hresult, currentUID);
        break;
      }
      case IDirect3DDevice9Ex_GetVertexShaderConstantF:
        break;
      case IDirect3DDevice9Ex_SetVertexShaderConstantI:
      {
        GET_RES(pD3DDevice, gpD3DDevices);
        PULL_U(StartRegister);
        PULL_U(Count);
        int* pConstantData = nullptr;
        PULL_DATA(Count * sizeof(int) * 4, pConstantData);
        const auto hresult = pD3DDevice->SetVertexShaderConstantI(IN StartRegister, IN pConstantData, IN Count);
        assert(SUCCEEDED(hresult));
        SEND_OPTIONAL_SERVER_RESPONSE(hresult, currentUID);
        break;
      }
      case IDirect3DDevice9Ex_GetVertexShaderConstantI:
        break;
      case IDirect3DDevice9Ex_SetVertexShaderConstantB:
      {
        GET_RES(pD3DDevice, gpD3DDevices);
        PULL_U(StartRegister);
        PULL_U(Count);
        BOOL* pConstantData = nullptr;
        PULL_DATA(Count * sizeof(BOOL), pConstantData);
        const auto hresult = pD3DDevice->SetVertexShaderConstantB(IN StartRegister, IN pConstantData, IN Count);
        assert(SUCCEEDED(hresult));
        SEND_OPTIONAL_SERVER_RESPONSE(hresult, currentUID);
        break;
      }
      case IDirect3DDevice9Ex_GetVertexShaderConstantB:
        break;
      case IDirect3DDevice9Ex_SetStreamSource:
      {
        GET_RES(pD3DDevice, gpD3DDevices);
        PULL_U(StreamNumber);
        PULL_U(pHandle);
        PULL_U(OffsetInBytes);
        PULL_U(Stride);
        IDirect3DVertexBuffer9* pStreamData = nullptr;
        if (pHandle != NULL) {
          pStreamData = (IDirect3DVertexBuffer9*) gpD3DResources[pHandle];
        }
        const auto hresult = pD3DDevice->SetStreamSource(IN StreamNumber, IN pStreamData, IN OffsetInBytes, IN Stride);
        assert(SUCCEEDED(hresult));
        SEND_OPTIONAL_SERVER_RESPONSE(hresult, currentUID);
        break;
      }
      case IDirect3DDevice9Ex_GetStreamSource:
        break;
      case IDirect3DDevice9Ex_SetStreamSourceFreq:
      {
        GET_RES(pD3DDevice, gpD3DDevices);
        PULL_U(StreamNumber);
        PULL_U(Divider);
        const auto hresult = pD3DDevice->SetStreamSourceFreq(StreamNumber, Divider);
        assert(SUCCEEDED(hresult));
        SEND_OPTIONAL_SERVER_RESPONSE(hresult, currentUID);
        break;
      }
      case IDirect3DDevice9Ex_GetStreamSourceFreq:
        break;
      case IDirect3DDevice9Ex_SetIndices:
      {
        GET_RES(pD3DDevice, gpD3DDevices);
        PULL_U(pHandle);
        IDirect3DIndexBuffer9* pIndexData = NULL;
        if (pHandle != NULL) {
          pIndexData = (IDirect3DIndexBuffer9*) gpD3DResources[pHandle];
        }
        const auto hresult = pD3DDevice->SetIndices(IN pIndexData);
        assert(SUCCEEDED(hresult));
        SEND_OPTIONAL_SERVER_RESPONSE(hresult, currentUID);
        break;
      }
      case IDirect3DDevice9Ex_GetIndices:
        break;
      case IDirect3DDevice9Ex_CreatePixelShader:
      {
        GET_RES(pD3DDevice, gpD3DDevices);
        PULL_HND(pHandle);
        PULL_U(dataSize);
        DWORD* pFunction = nullptr;
        PULL_DATA(dataSize, pFunction);
        IDirect3DPixelShader9* pShader = nullptr;
        const auto hresult = pD3DDevice->CreatePixelShader(IN pFunction, OUT & pShader);
        if (SUCCEEDED(hresult)) {
          gpD3DPixelShaders[pHandle] = pShader;
        }
        assert(SUCCEEDED(hresult));
        SEND_OPTIONAL_CREATE_FUNCTION_SERVER_RESPONSE(hresult, currentUID);
        break;
      }
      case IDirect3DDevice9Ex_SetPixelShader:
      {
        GET_RES(pD3DDevice, gpD3DDevices);
        PULL_U(pHandle);
        IDirect3DPixelShader9* pShader = nullptr;
        if (pHandle != NULL) {
          pShader = gpD3DPixelShaders[pHandle];
        }
        const auto hresult = pD3DDevice->SetPixelShader(IN pShader);
        assert(SUCCEEDED(hresult));
        SEND_OPTIONAL_SERVER_RESPONSE(hresult, currentUID);
        break;
      }
      case IDirect3DDevice9Ex_GetPixelShader:
        break;
      case IDirect3DDevice9Ex_SetPixelShaderConstantF:
      {
        GET_RES(pD3DDevice, gpD3DDevices);
        PULL_U(StartRegister);
        PULL_U(Count);
        float* pConstantData = nullptr;
        PULL_DATA(Count * sizeof(float) * 4, pConstantData);
        const auto hresult = pD3DDevice->SetPixelShaderConstantF(IN StartRegister, IN pConstantData, IN Count);
        assert(SUCCEEDED(hresult));
        SEND_OPTIONAL_SERVER_RESPONSE(hresult, currentUID);
        break;
      }
      case IDirect3DDevice9Ex_GetPixelShaderConstantF:
        break;
      case IDirect3DDevice9Ex_SetPixelShaderConstantI:
      {
        GET_RES(pD3DDevice, gpD3DDevices);
        PULL_U(StartRegister);
        PULL_U(Count);
        int* pConstantData = nullptr;
        PULL_DATA(Count * sizeof(int) * 4, pConstantData);
        const auto hresult = pD3DDevice->SetPixelShaderConstantI(IN StartRegister, IN pConstantData, IN Count);
        assert(SUCCEEDED(hresult));
        SEND_OPTIONAL_SERVER_RESPONSE(hresult, currentUID);
        break;
      }
      case IDirect3DDevice9Ex_GetPixelShaderConstantI:
        break;
      case IDirect3DDevice9Ex_SetPixelShaderConstantB:
      {
        GET_RES(pD3DDevice, gpD3DDevices);
        PULL_U(StartRegister);
        PULL_U(Count);
        BOOL* pConstantData = nullptr;
        PULL_DATA(Count * sizeof(BOOL), pConstantData);
        const auto hresult = pD3DDevice->SetPixelShaderConstantB(IN StartRegister, IN pConstantData, IN Count);
        assert(SUCCEEDED(hresult));
        SEND_OPTIONAL_SERVER_RESPONSE(hresult, currentUID);
        break;
      }
      case IDirect3DDevice9Ex_GetPixelShaderConstantB:
        break;
      case IDirect3DDevice9Ex_DrawRectPatch:
        break;
      case IDirect3DDevice9Ex_DrawTriPatch:
        break;
      case IDirect3DDevice9Ex_DeletePatch:
        break;
      case IDirect3DDevice9Ex_WaitForVBlank:
      {
        GET_RES(pD3DDevice, gpD3DDevices);
        PULL_U(ISwapChain);
        const auto hresult = ((IDirect3DDevice9Ex*) pD3DDevice)->WaitForVBlank(IN ISwapChain);
        assert(SUCCEEDED(hresult));
        break;
      }
      case IDirect3DDevice9Ex_SetConvolutionMonoKernel:
      {
        GET_RES(pD3DDevice, gpD3DDevices);
        PULL_U(WIDTH);
        PULL_U(HEIGHT);
        float* pRows = nullptr;
        PULL_DATA(sizeof(float) * WIDTH, pRows);
        float* pColumns = nullptr;
        PULL_DATA(sizeof(float) * HEIGHT, pColumns);
        const auto hresult = ((IDirect3DDevice9Ex*) pD3DDevice)->SetConvolutionMonoKernel(IN WIDTH, IN HEIGHT, IN pRows, IN pColumns);
        assert(SUCCEEDED(hresult));
        SEND_OPTIONAL_SERVER_RESPONSE(hresult, currentUID);
        break;
      }
      case IDirect3DDevice9Ex_ComposeRects:
      {
        GET_RES(pD3DDevice, gpD3DDevices);
        PULL_U(pSrcSurface);
        PULL_U(pDestSurface);
        PULL_HND(pSrcRect);
        PULL_HND(pDestRect);
        PULL_U(NumRects);
        PULL(D3DCOMPOSERECTSOP, Operation);
        PULL(int, Xoffset);
        PULL(int, Yoffset);
        const auto& srcSurface = (IDirect3DSurface9*) gpD3DResources[pSrcSurface];
        const auto& destSurface = (IDirect3DSurface9*) gpD3DResources[pDestSurface];

        const auto& srcVertexBuffer = (IDirect3DVertexBuffer9*) gpD3DResources[pSrcRect];
        const auto& destVertexBuffer = (IDirect3DVertexBuffer9*) gpD3DResources[pDestRect];

        const auto hresult = ((IDirect3DDevice9Ex*) pD3DDevice)->ComposeRects(IN srcSurface, IN destSurface, IN srcVertexBuffer, IN NumRects, IN destVertexBuffer, IN Operation, IN Xoffset, IN Yoffset);
        SEND_OPTIONAL_SERVER_RESPONSE(hresult, currentUID);
        break;
      }
      case IDirect3DDevice9Ex_CheckDeviceState:
      {
        GET_RES(pD3DDevice, gpD3DDevices);
        PULL(uint32_t, hDestinationWindow);
        HWND hwnd = TRUNCATE_HANDLE(HWND, hDestinationWindow);
        const auto hresult = ((IDirect3DDevice9Ex*) pD3DDevice)->CheckDeviceState(IN hwnd);
        assert(SUCCEEDED(hresult));
        {
          ServerMessage c(Commands::Bridge_Response, currentUID);
          c.send_data(hresult);
        }
        break;
      }
      case IDirect3DDevice9Ex_CreateQuery:
      {
        GET_RES(pD3DDevice, gpD3DDevices);
        PULL(D3DQUERYTYPE, Type);
        PULL_HND(pHandle);
        IDirect3DQuery9* ppQuery;
        const auto hresult = pD3DDevice->CreateQuery(IN Type, OUT & ppQuery);
        if (SUCCEEDED(hresult)) {
          gpD3DQuery[pHandle] = ppQuery;
        }
        break;
      }
      /*
       * IDirect3DStateBlock9 interface
       */
      case IDirect3DStateBlock9_QueryInterface:
        break;
      case IDirect3DStateBlock9_AddRef:
        break;
      case IDirect3DStateBlock9_Destroy:
      {
        GET_HND(pHandle);
        const auto& pSB = gpD3DStateBlocks[pHandle];
        safeDestroy(pSB, pHandle);
        gpD3DStateBlocks.erase(pHandle);
        break;
      }
      case IDirect3DStateBlock9_GetDevice:
        break;
      case IDirect3DStateBlock9_Capture:
      {
        GET_HND(pHandle);
        const auto& pSB = gpD3DStateBlocks[pHandle];
        const auto hresult = pSB->Capture();
        assert(SUCCEEDED(hresult));
        break;
      }
      case IDirect3DStateBlock9_Apply:
      {
        GET_HND(pHandle);
        const auto& pSB = gpD3DStateBlocks[pHandle];
        assert(pSB != nullptr);
        const auto hresult = pSB->Apply();
        assert(SUCCEEDED(hresult));
        break;
      }

      /*
       * IDirect3DSwapChain9 interface
       */
      case IDirect3DSwapChain9_QueryInterface:
        break;
      case IDirect3DSwapChain9_AddRef:
        break;
      case IDirect3DSwapChain9_Destroy:
      {
        GET_HND(pHandle);
        const auto& pSwapChain = gpD3DSwapChains[pHandle];
        safeDestroy(pSwapChain, pHandle);
        gpD3DSwapChains.erase(pHandle);
        break;
      }
      case IDirect3DSwapChain9_Present:
      {
        FrameMark;
#ifdef ENABLE_PRESENT_SEMAPHORE_TRACE
        Logger::trace("Server side Present call received, releasing semaphore...");
#endif

        GET_RES(pSwapChain, gpD3DSwapChains);
        PULL_OBJ(RECT, pSourceRect);
        PULL_OBJ(RECT, pDestRect);
        PULL(uint32_t, hDestWindowOverride);
        PULL_OBJ(RGNDATA, pDirtyRegion);
        PULL(uint32_t, dwFlags);

        HWND hwnd = TRUNCATE_HANDLE(HWND, hDestWindowOverride);

        const auto hresult = pSwapChain->Present(pSourceRect, pDestRect, hwnd, pDirtyRegion, dwFlags);

        if (!SUCCEEDED(hresult)) {
          std::stringstream ss;
          ss << "Present() failed! Check all logs for reported errors.";
        }

        // If we're syncing with the client on Present() then trigger the semaphore now
        if (GlobalOptions::getPresentSemaphoreEnabled()) {
          gpPresent->release();
#ifdef ENABLE_PRESENT_SEMAPHORE_TRACE
          Logger::trace("Present semaphore released successfully.");
#endif
        }
        break;
      }
      case IDirect3DSwapChain9_GetFrontBufferData:
      {
        GET_RES(pSwapChain, gpD3DSwapChains);
        PULL_HND(pDestSurfaceHandle);
        const auto& pDestSurface = (IDirect3DSurface9*) gpD3DResources[pDestSurfaceHandle];
        auto hresult = pSwapChain->GetFrontBufferData(pDestSurface);
        if (SUCCEEDED(hresult)) {
          gpD3DResources[pDestSurfaceHandle] = pDestSurface;
        }
        hresult = ReturnSurfaceDataToClient(pDestSurface, hresult, currentUID);
        assert(SUCCEEDED(hresult));
        break;
      }
      case IDirect3DSwapChain9_GetBackBuffer:
      {
        GET_RES(pSwapChain, gpD3DSwapChains);
        PULL(uint32_t, iBackBuffer);
        PULL(D3DBACKBUFFER_TYPE, Type);
        PULL_HND(pSurfaceHandle);
        IDirect3DSurface9* pBackbuffer = nullptr;
        const auto hresult = pSwapChain->GetBackBuffer(iBackBuffer, Type, &pBackbuffer);
        if (SUCCEEDED(hresult)) {
          gpD3DResources[pSurfaceHandle] = pBackbuffer;
        }
        assert(SUCCEEDED(hresult));
        SEND_OPTIONAL_SERVER_RESPONSE(hresult, currentUID);
        break;
      }
      case IDirect3DSwapChain9_GetRasterStatus:
        break;
      case IDirect3DSwapChain9_GetDisplayMode:
        break;
      case IDirect3DSwapChain9_GetDevice:
        break;
      case IDirect3DSwapChain9_GetPresentParameters:
        break;

      /*
       * IDirect3DResource9 interface
       */
      case IDirect3DResource9_QueryInterface:
        break;
      case IDirect3DResource9_AddRef:
        break;
      case IDirect3DResource9_Destroy:
        break;
      case IDirect3DResource9_GetDevice:
        break;
      // We shouldn't ever need to send private data across the bridge
      case IDirect3DResource9_SetPrivateData:
      case IDirect3DResource9_GetPrivateData:
      case IDirect3DResource9_FreePrivateData:
        break;
      case IDirect3DResource9_SetPriority:
      {
        GET_HND(pHandle);
        PULL(DWORD, PriorityNew);
        const auto& pResource = gpD3DResources[pHandle];
        assert(pResource != nullptr);
        pResource->SetPriority(PriorityNew);
        break;
      }
      case IDirect3DResource9_GetPriority:
        break;
      case IDirect3DResource9_PreLoad:
      {
        GET_HND(pHandle);
        const auto& pResource = gpD3DResources[pHandle];
        assert(pResource != nullptr);
        pResource->PreLoad();
        break;
      }
      case IDirect3DResource9_GetType:
        break;

      /*
       * IDirect3DVertexDeclaration9 interface
       */
      case IDirect3DVertexDeclaration9_QueryInterface:
        break;
      case IDirect3DVertexDeclaration9_AddRef:
      {
        GET_HND(pHandle);
        const auto& pVertexDeclaration = gpD3DVertexDeclarations[pHandle];
        pVertexDeclaration->AddRef();
        break;
      }
      case IDirect3DVertexDeclaration9_Destroy:
      {
        GET_HND(pHandle);
        const auto& pVertexDeclaration = gpD3DVertexDeclarations[pHandle];
        safeDestroy(pVertexDeclaration, pHandle);
        gpD3DVertexDeclarations.erase(pHandle);
        break;
      }
      case IDirect3DVertexDeclaration9_GetDevice:
        break;
      case IDirect3DVertexDeclaration9_GetDeclaration:
        break;

      /*
       * IDirect3DVertexShader9 interface
       */
      case IDirect3DVertexShader9_QueryInterface:
        break;
      case IDirect3DVertexShader9_AddRef:
      {
        GET_HND(pHandle);
        const auto& pVertexShader = gpD3DVertexShaders[pHandle];
        pVertexShader->AddRef();
        break;
      }
      case IDirect3DVertexShader9_Destroy:
      {
        GET_HND(pHandle);
        const auto& pVertexShader = gpD3DVertexShaders[pHandle];
        safeDestroy(pVertexShader, pHandle);
        gpD3DVertexShaders.erase(pHandle);
        break;
      }
      case IDirect3DVertexShader9_GetDevice:
        break;
      case IDirect3DVertexShader9_GetFunction:
        break;

      /*
       * IDirect3DPixelShader9 interface
       */
      case IDirect3DPixelShader9_QueryInterface:
        break;
      case IDirect3DPixelShader9_AddRef:
      {
        GET_HND(pHandle);
        const auto& pPixelShader = gpD3DPixelShaders[pHandle];
        pPixelShader->AddRef();
        break;
      }
      case IDirect3DPixelShader9_Destroy:
      {
        GET_HND(pHandle);
        const auto& pPixelShader = gpD3DPixelShaders[pHandle];
        safeDestroy(pPixelShader, pHandle);
        gpD3DPixelShaders.erase(pHandle);
        break;
      }
      case IDirect3DPixelShader9_GetDevice:
        break;
      case IDirect3DPixelShader9_GetFunction:
        break;

      /*
       * IDirect3DBaseTexture9 interface
       */
      case IDirect3DBaseTexture9_QueryInterface:
        break;
      case IDirect3DBaseTexture9_AddRef:
        break;
      case IDirect3DBaseTexture9_Destroy:
        break;
      case IDirect3DBaseTexture9_GetDevice:
        break;
      case IDirect3DBaseTexture9_SetPrivateData:
        break;
      case IDirect3DBaseTexture9_GetPrivateData:
        break;
      case IDirect3DBaseTexture9_FreePrivateData:
        break;
      case IDirect3DBaseTexture9_SetPriority:
        break;
      case IDirect3DBaseTexture9_GetPriority:
        break;
      case IDirect3DBaseTexture9_PreLoad:
        break;
      case IDirect3DBaseTexture9_GetType:
        break;
      case IDirect3DBaseTexture9_SetLOD:
      {
        GET_HND(pHandle);
        PULL(DWORD, LODNew);
        const auto& pResource = gpD3DResources[pHandle];
        if (pResource != nullptr) {
          ((IDirect3DBaseTexture9*) pResource)->SetLOD(LODNew);
        }
        assert(pResource != nullptr);
        break;
      }
      case IDirect3DBaseTexture9_GetLOD:
        break;
      case IDirect3DBaseTexture9_GetLevelCount:
        break;
      case IDirect3DBaseTexture9_SetAutoGenFilterType:
      {
        HRESULT hresult = D3DERR_INVALIDCALL;
        GET_HND(pHandle);
        PULL(D3DTEXTUREFILTERTYPE, FilterType);
        const auto& pResource = gpD3DResources[pHandle];
        if (pResource != nullptr) {
          hresult = ((IDirect3DBaseTexture9*) pResource)->SetAutoGenFilterType(FilterType);
          SEND_OPTIONAL_SERVER_RESPONSE(hresult, currentUID);
        }
        assert(pResource != nullptr);
        break;
      }
      case IDirect3DBaseTexture9_GetAutoGenFilterType:
        break;
      case IDirect3DBaseTexture9_GenerateMipSubLevels:
      {
        GET_HND(pHandle);
        const auto& pResource = gpD3DResources[pHandle];
        if (pResource != nullptr) {
          ((IDirect3DBaseTexture9*) pResource)->GenerateMipSubLevels();
        }
        assert(pResource != nullptr);
        break;
      }

      /*
       * IDirect3DTexture9 interface
       */
      case IDirect3DTexture9_QueryInterface:
        break;
      case IDirect3DTexture9_AddRef:
      {
        GET_HND(pHandle);
        const auto& pTexture = (IDirect3DTexture9*) gpD3DResources[pHandle];
        pTexture->AddRef();
        break;
      }
      case IDirect3DTexture9_Destroy:
      {
        GET_HND(pHandle);
        const auto& pTexture = (IDirect3DTexture9*) gpD3DResources[pHandle];
        safeDestroy(pTexture, pHandle);
        gpD3DResources.erase(pHandle);
        break;
      }
      case IDirect3DTexture9_GetDevice:
        break;
      case IDirect3DTexture9_SetPrivateData:
        break;
      case IDirect3DTexture9_GetPrivateData:
        break;
      case IDirect3DTexture9_FreePrivateData:
        break;
      case IDirect3DTexture9_SetPriority:
        break;
      case IDirect3DTexture9_GetPriority:
        break;
      case IDirect3DTexture9_PreLoad:
        break;
      case IDirect3DTexture9_GetType:
        break;
      case IDirect3DTexture9_SetLOD:
        break;
      case IDirect3DTexture9_GetLOD:
        break;
      case IDirect3DTexture9_GetLevelCount:
      {
        GET_HND(pHandle);
        PULL_D(orig_cnt);
        const auto& pTexture = (IDirect3DTexture9*) gpD3DResources[pHandle];
        const auto cnt = pTexture->GetLevelCount();
        assert(orig_cnt == cnt);
        break;
      }
      case IDirect3DTexture9_SetAutoGenFilterType:
        break;
      case IDirect3DTexture9_GetAutoGenFilterType:
        break;
      case IDirect3DTexture9_GenerateMipSubLevels:
        break;
      case IDirect3DTexture9_GetLevelDesc:
      {
        GET_HND(pHandle);
        PULL_OBJ(D3DSURFACE_DESC, orig_desc);
        PULL_U(Level);
        const auto& pTexture = (IDirect3DTexture9*) gpD3DResources[pHandle];
        D3DSURFACE_DESC pDesc;
        const auto hresult = pTexture->GetLevelDesc(Level, &pDesc);
        assert(SUCCEEDED(hresult));
        break;
      }
      case IDirect3DTexture9_GetSurfaceLevel:
      {
        GET_HND(pTextureHandle);
        PULL_U(Level);
        PULL_HND(pSurfaceHandle);
        const auto& pTexture = (IDirect3DTexture9*) gpD3DResources[pTextureHandle];
        LPDIRECT3DSURFACE9 pSurfaceLevel;
        const auto hresult = pTexture->GetSurfaceLevel(IN Level, OUT & pSurfaceLevel);
        if (SUCCEEDED(hresult)) {
          gpD3DResources[pSurfaceHandle] = pSurfaceLevel;
        }
        assert(SUCCEEDED(hresult));
        break;
      }
      case IDirect3DTexture9_LockRect:
      {
        // This is a no-op right now because we're doing all the logic on Unlock
        break;
      }
      case IDirect3DTexture9_UnlockRect:
      {
        assert(0 && "IDirect3DTexture9::UnlockRect should be handled via IDirect3DSurface9::UnlockRect");
        break;
      }
      case IDirect3DTexture9_AddDirtyRect:
      {
        GET_HND(pHandle);
        PULL_OBJ(RECT, pDirtyRect);
        const auto& pTexture = (IDirect3DTexture9*) gpD3DResources[pHandle];
        const auto hresult = pTexture->AddDirtyRect(IN pDirtyRect);
        SEND_OPTIONAL_SERVER_RESPONSE(hresult, currentUID);
        assert(SUCCEEDED(hresult));
        break;
      }

      /*
       * IDirect3DVolumeTexture9 interface
       */
      case IDirect3DVolumeTexture9_QueryInterface:
        break;
      case IDirect3DVolumeTexture9_AddRef:
      {
        GET_HND(pHandle);
        const auto& pVolumeTexture = (IDirect3DVolumeTexture9*) gpD3DResources[pHandle];
        pVolumeTexture->AddRef();
        break;
      }
      case IDirect3DVolumeTexture9_Destroy:
      {
        GET_HND(pHandle);
        const auto& pVolumeTexture = (IDirect3DVolumeTexture9*) gpD3DResources[pHandle];
        safeDestroy(pVolumeTexture, pHandle);
        gpD3DResources.erase(pHandle);
        break;
      }
      case IDirect3DVolumeTexture9_GetDevice:
        break;
      case IDirect3DVolumeTexture9_SetPrivateData:
        break;
      case IDirect3DVolumeTexture9_GetPrivateData:
        break;
      case IDirect3DVolumeTexture9_FreePrivateData:
        break;
      case IDirect3DVolumeTexture9_SetPriority:
        break;
      case IDirect3DVolumeTexture9_GetPriority:
        break;
      case IDirect3DVolumeTexture9_PreLoad:
        break;
      case IDirect3DVolumeTexture9_GetType:
        break;
      case IDirect3DVolumeTexture9_SetLOD:
        break;
      case IDirect3DVolumeTexture9_GetLOD:
        break;
      case IDirect3DVolumeTexture9_GetLevelCount:
      {
        GET_HND(pHandle);
        PULL_D(orig_cnt);
        const auto& pVolumeTexture = (IDirect3DVolumeTexture9*) gpD3DResources[pHandle];
        const auto cnt = pVolumeTexture->GetLevelCount();
        assert(orig_cnt == cnt);
        break;
      }
      case IDirect3DVolumeTexture9_SetAutoGenFilterType:
        break;
      case IDirect3DVolumeTexture9_GetAutoGenFilterType:
        break;
      case IDirect3DVolumeTexture9_GenerateMipSubLevels:
        break;
      case IDirect3DVolumeTexture9_GetLevelDesc:
      {
        GET_HND(pHandle);
        PULL_OBJ(D3DVOLUME_DESC, orig_desc);
        PULL_U(Level);
        const auto& pVolumeTexture = (IDirect3DVolumeTexture9*) gpD3DResources[pHandle];
        D3DVOLUME_DESC pDesc;
        const auto hresult = pVolumeTexture->GetLevelDesc(Level, &pDesc);
        assert(SUCCEEDED(hresult));
        break;
      }
      case IDirect3DVolumeTexture9_GetVolumeLevel:
      {
        GET_HND(pVolumeTextureHandle);
        PULL_U(Level);
        PULL_HND(pVolumeLevelHandle);
        const auto& pVolumeTexture = (IDirect3DVolumeTexture9*) gpD3DResources[pVolumeTextureHandle];
        LPDIRECT3DVOLUME9 pVolumeLevel;
        const auto hresult = pVolumeTexture->GetVolumeLevel(IN Level, OUT & pVolumeLevel);
        if (SUCCEEDED(hresult)) {
          gpD3DVolumes[pVolumeLevelHandle] = pVolumeLevel;
        }
        assert(SUCCEEDED(hresult));
        break;
      }
      case IDirect3DVolumeTexture9_LockBox:
      {
        // This is a no-op right now because we're doing all the logic on Unlock
        break;
      }
      case IDirect3DVolumeTexture9_UnlockBox:
      {
        GET_HND(pHandle);
        PULL_U(Level);
        PULL_OBJ(D3DBOX, pBox);
        PULL_D(Flags);
        const auto& pVolumeTexture = (IDirect3DVolumeTexture9*) gpD3DResources[pHandle];
        // Now lock the box so we can copy the data into it
        D3DLOCKED_BOX pLockedVolume;
        auto hresult = pVolumeTexture->LockBox(IN Level, IN & pLockedVolume, IN pBox, IN Flags);
        assert(S_OK == hresult);
        // Copy the data over
        PULL_U(bytesPerPixel);
        PULL_U(width);
        PULL_U(height);
        PULL_U(depth);
        const auto row_size = width * bytesPerPixel;
#ifdef SEND_ALL_LOCK_DATA_AT_ONCE
        void* data = nullptr;
        const auto slice_size = row_size * height;
        size_t pulledSize = DeviceBridge::get_data(&data);
#endif
        for (uint32_t z = 0; z < depth; z++) {
          for (uint32_t y = 0; y < height; y++) {
            auto ptr = (uintptr_t) pLockedVolume.pBits + y * pLockedVolume.RowPitch + z * pLockedVolume.SlicePitch;
#ifdef SEND_ALL_LOCK_DATA_AT_ONCE
            auto row = (uintptr_t) data + y * row_size + z * slice_size;
#else
            void* row = nullptr;
            const auto read_size = DeviceBridge::get_data(&row);
            assert(row_size == read_size);
#endif
            memcpy((void*) ptr, (void*) row, row_size);
          }
        }
#ifdef SEND_ALL_LOCK_DATA_AT_ONCE
        assert(pulledSize == depth * slice_size);
#endif
        hresult = pVolumeTexture->UnlockBox(Level);
        assert(SUCCEEDED(hresult));
        break;
      }
      case IDirect3DVolumeTexture9_AddDirtyBox:
      {
        GET_HND(pHandle);
        PULL_U(Level);
        PULL_OBJ(D3DBOX, pBox);
        const auto& pVolumeTexture = (IDirect3DVolumeTexture9*) gpD3DResources[pHandle];
        const auto hresult = pVolumeTexture->AddDirtyBox(IN pBox);
        SEND_OPTIONAL_SERVER_RESPONSE(hresult, currentUID);
        assert(SUCCEEDED(hresult));
        break;
      }

      /*
       * IDirect3DCubeTexture9 interface
       */
      case IDirect3DCubeTexture9_QueryInterface:
        break;
      case IDirect3DCubeTexture9_AddRef:
      {
        GET_HND(pHandle);
        const auto& pCubeTexture = (IDirect3DCubeTexture9*) gpD3DResources[pHandle];
        pCubeTexture->AddRef();
        break;
      }
      case IDirect3DCubeTexture9_Destroy:
      {
        GET_HND(pHandle);
        const auto& pCubeTexture = (IDirect3DCubeTexture9*) gpD3DResources[pHandle];
        safeDestroy(pCubeTexture, pHandle);
        gpD3DResources.erase(pHandle);
        break;
      }
      case IDirect3DCubeTexture9_GetDevice:
        break;
      case IDirect3DCubeTexture9_SetPrivateData:
        break;
      case IDirect3DCubeTexture9_GetPrivateData:
        break;
      case IDirect3DCubeTexture9_FreePrivateData:
        break;
      case IDirect3DCubeTexture9_SetPriority:
        break;
      case IDirect3DCubeTexture9_GetPriority:
        break;
      case IDirect3DCubeTexture9_PreLoad:
        break;
      case IDirect3DCubeTexture9_GetType:
        break;
      case IDirect3DCubeTexture9_SetLOD:
        break;
      case IDirect3DCubeTexture9_GetLOD:
        break;
      case IDirect3DCubeTexture9_GetLevelCount:
      {
        GET_HND(pHandle);
        PULL_D(orig_cnt);
        const auto& pCubeTexture = (IDirect3DCubeTexture9*) gpD3DResources[pHandle];
        const auto cnt = pCubeTexture->GetLevelCount();
        assert(orig_cnt == cnt);
        break;
      }
      case IDirect3DCubeTexture9_SetAutoGenFilterType:
        break;
      case IDirect3DCubeTexture9_GetAutoGenFilterType:
        break;
      case IDirect3DCubeTexture9_GenerateMipSubLevels:
        break;
      case IDirect3DCubeTexture9_GetLevelDesc:
      {
        PULL_OBJ(D3DSURFACE_DESC, orig_desc);
        PULL_U(Level);
        GET_HND(pHandle);
        const auto& pCubeTexture = (IDirect3DCubeTexture9*) gpD3DResources[pHandle];
        D3DSURFACE_DESC pDesc;
        const auto hresult = pCubeTexture->GetLevelDesc(Level, &pDesc);
        assert(SUCCEEDED(hresult));
        break;
      }
      case IDirect3DCubeTexture9_GetCubeMapSurface:
      {
        PULL(D3DCUBEMAP_FACES, FaceType);
        PULL_U(Level);
        GET_HND(pCubeTextureHandle);
        PULL_HND(pCubeMapSurfaceHandle);
        const auto& pCubeTexture = (IDirect3DCubeTexture9*) gpD3DResources[pCubeTextureHandle];
        LPDIRECT3DSURFACE9 pCubeMapSurface;
        const auto hresult = pCubeTexture->GetCubeMapSurface(IN FaceType, IN Level, OUT & pCubeMapSurface);
        if (SUCCEEDED(hresult)) {
          gpD3DResources[pCubeMapSurfaceHandle] = pCubeMapSurface;
        }
        assert(SUCCEEDED(hresult));
        break;
      }
      case IDirect3DCubeTexture9_LockRect:
      {
        // This is a no-op right now because we're doing all the logic on Unlock
        break;
      }
      case IDirect3DCubeTexture9_UnlockRect:
      {
        assert(0 && "IDirect3DCubeTexture9::UnlockRect should be handled via IDirect3DSurface9::UnlockRect");
        break;
      }
      case IDirect3DCubeTexture9_AddDirtyRect:
      {
        GET_HND(pCubeTextureHandle);
        PULL(D3DCUBEMAP_FACES, FaceType);
        PULL_OBJ(RECT, pDirtyRect);
        const auto& pCubeTexture = (IDirect3DCubeTexture9*) gpD3DResources[pCubeTextureHandle];
        const auto hresult = pCubeTexture->AddDirtyRect(IN FaceType, IN pDirtyRect);
        SEND_OPTIONAL_SERVER_RESPONSE(hresult, currentUID);
        assert(SUCCEEDED(hresult));
        break;
      }

        /*
         * IDirect3DVertexBuffer9 interface
         */
      case IDirect3DVertexBuffer9_QueryInterface:
        break;
      case IDirect3DVertexBuffer9_AddRef:
      {
        GET_HND(pHandle);
        const auto& pVertexBuffer = (IDirect3DVertexBuffer9*) gpD3DResources[pHandle];
        pVertexBuffer->AddRef();
        break;
      }
      case IDirect3DVertexBuffer9_Destroy:
      {
        GET_HND(pHandle);
        const auto& pVertexBuffer = (IDirect3DVertexBuffer9*) gpD3DResources[pHandle];
        safeDestroy(pVertexBuffer, pHandle);
        gpD3DResources.erase(pHandle);
        break;
      }
      case IDirect3DVertexBuffer9_GetDevice:
        break;
      case IDirect3DVertexBuffer9_SetPrivateData:
        break;
      case IDirect3DVertexBuffer9_GetPrivateData:
        break;
      case IDirect3DVertexBuffer9_FreePrivateData:
        break;
      case IDirect3DVertexBuffer9_SetPriority:
        break;
      case IDirect3DVertexBuffer9_GetPriority:
        break;
      case IDirect3DVertexBuffer9_PreLoad:
        break;
      case IDirect3DVertexBuffer9_GetType:
        break;
      case IDirect3DVertexBuffer9_Lock:
      {
        // This is a no-op right now because we're doing all the logic on Unlock
        GET_HND(pHandle);
        void* data = nullptr;
        DeviceBridge::get_data(&data);
        break;
      }
      case IDirect3DVertexBuffer9_Unlock:
      {
        GET_HND(pHandle);
        PULL_U(OffsetToLock);
        PULL_U(SizeToLock);
        PULL_D(Flags);
        const auto& pVertexBuffer = (IDirect3DVertexBuffer9*) gpD3DResources[pHandle];

        // Now lock the buffer so we can copy the data into it
        void* pbData = nullptr;
        auto hresult = pVertexBuffer->Lock(IN OffsetToLock, IN SizeToLock, IN & pbData, IN Flags);
        assert(S_OK == hresult);

        // Copy the data over
        void* data = nullptr;
        if (Commands::IsDataReserved(rpcHeader.flags)) {
          PULL_D(DataOffset);
          data = DeviceBridge::Bridge::getReaderChannel().get_data_ptr() + DataOffset;
        } else if (Commands::IsDataInSharedHeap(rpcHeader.flags)) {
          PULL_U(allocId);
          data = SharedHeap::getBuf(allocId) + OffsetToLock;
        } else {
          const auto size = DeviceBridge::get_data(&data);
          assert(SizeToLock == size);
        }
        memcpy(pbData, data, SizeToLock);
        hresult = pVertexBuffer->Unlock();
        assert(SUCCEEDED(hresult));

        break;
      }
      case IDirect3DVertexBuffer9_GetDesc:
      {
        GET_HND(pHandle);
        PULL_OBJ(D3DVERTEXBUFFER_DESC, orig_desc);
        const auto& pVertexBuffer = (IDirect3DVertexBuffer9*) gpD3DResources[pHandle];
        D3DVERTEXBUFFER_DESC pDesc;
        const auto hresult = pVertexBuffer->GetDesc(OUT & pDesc);
        assert(SUCCEEDED(hresult));
        break;
      }

      /*
       * IDirect3DIndexBuffer9 interface
       */
      case IDirect3DIndexBuffer9_QueryInterface:
        break;
      case IDirect3DIndexBuffer9_AddRef:
      {
        GET_HND(pHandle);
        const auto& pIndexBuffer = (IDirect3DIndexBuffer9*) gpD3DResources[pHandle];
        pIndexBuffer->AddRef();
        break;
      }
      case IDirect3DIndexBuffer9_Destroy:
      {
        GET_HND(pHandle);
        const auto& pIndexBuffer = (IDirect3DIndexBuffer9*) gpD3DResources[pHandle];
        safeDestroy(pIndexBuffer, pHandle);
        gpD3DResources.erase(pHandle);
        break;
      }
      case IDirect3DIndexBuffer9_GetDevice:
        break;
      case IDirect3DIndexBuffer9_SetPrivateData:
        break;
      case IDirect3DIndexBuffer9_GetPrivateData:
        break;
      case IDirect3DIndexBuffer9_FreePrivateData:
        break;
      case IDirect3DIndexBuffer9_SetPriority:
        break;
      case IDirect3DIndexBuffer9_GetPriority:
        break;
      case IDirect3DIndexBuffer9_PreLoad:
        break;
      case IDirect3DIndexBuffer9_GetType:
        break;
      case IDirect3DIndexBuffer9_Lock:
      {
        // This is a no-op right now because we're doing all the logic on Unlock
        GET_HND(pHandle);
        void* data = nullptr;
        DeviceBridge::get_data(&data);
        break;
      }
      case IDirect3DIndexBuffer9_Unlock:
      {
        GET_HND(pHandle);
        PULL_U(OffsetToLock);
        PULL_U(SizeToLock);
        PULL_D(Flags);
        const auto& pIndexBuffer = (IDirect3DIndexBuffer9*) gpD3DResources[pHandle];

        // Now lock the buffer so we can copy the data into it
        void* pbData = nullptr;
        auto hresult = pIndexBuffer->Lock(IN OffsetToLock, IN SizeToLock, IN & pbData, IN Flags);
        assert(S_OK == hresult);

        // Copy the data over
        void* data = nullptr;
        if (Commands::IsDataReserved(rpcHeader.flags)) {
          PULL_D(DataOffset);
          data = DeviceBridge::Bridge::getReaderChannel().get_data_ptr() + DataOffset;
        } else if (Commands::IsDataInSharedHeap(rpcHeader.flags)) {
          PULL_U(allocId);
          data = SharedHeap::getBuf(allocId) + OffsetToLock;
        } else {
          const auto size = DeviceBridge::get_data(&data);
          assert(SizeToLock == size);
        }
        memcpy(pbData, data, SizeToLock);
        hresult = pIndexBuffer->Unlock();
        assert(SUCCEEDED(hresult));
        break;
      }
      case IDirect3DIndexBuffer9_GetDesc:
      {
        GET_HND(pHandle);
        PULL_OBJ(D3DINDEXBUFFER_DESC, orig_desc);
        const auto& pIndexBuffer = (IDirect3DIndexBuffer9*) gpD3DResources[pHandle];
        D3DINDEXBUFFER_DESC pDesc;
        const auto hresult = pIndexBuffer->GetDesc(OUT & pDesc);
        assert(SUCCEEDED(hresult));
        break;
      }

      /*
       * IDirect3DSurface9 interface
       */
      case IDirect3DSurface9_QueryInterface:
        break;
      case IDirect3DSurface9_AddRef:
      {
        GET_HND(pHandle);
        const auto& pSurface = (IDirect3DSurface9*) gpD3DResources[pHandle];
        pSurface->AddRef();
        break;
      }
      case IDirect3DSurface9_Destroy:
      {
        GET_HND(pHandle);
        const auto& pSurface = (IDirect3DSurface9*) gpD3DResources[pHandle];
        safeDestroy(pSurface, pHandle);
        gpD3DResources.erase(pHandle);
        break;
      }
      case IDirect3DSurface9_GetDevice:
        break;
      case IDirect3DSurface9_SetPrivateData:
        break;
      case IDirect3DSurface9_GetPrivateData:
        break;
      case IDirect3DSurface9_FreePrivateData:
        break;
      case IDirect3DSurface9_SetPriority:
        break;
      case IDirect3DSurface9_GetPriority:
        break;
      case IDirect3DSurface9_PreLoad:
        break;
      case IDirect3DSurface9_GetType:
        break;
      case IDirect3DSurface9_GetContainer:
        break;
      case IDirect3DSurface9_GetDesc:
      {
        GET_HND(pHandle);
        PULL_OBJ(D3DSURFACE_DESC, orig_desc);
        const auto& pSurface = (IDirect3DSurface9*) gpD3DResources[pHandle];
        D3DSURFACE_DESC pDesc;
        const auto hresult = pSurface->GetDesc(OUT & pDesc);
        assert(SUCCEEDED(hresult));
        break;
      }
      case IDirect3DSurface9_LockRect:
      {
        // Currently we only recieve calls for LockRect in cases where backbuffer data is to be copied for screenshots
        GET_HND(pHandle);
        const auto pSurface = (IDirect3DSurface9*) gpD3DResources[pHandle];
        HRESULT hresult = ReturnSurfaceDataToClient(pSurface, S_OK, currentUID);
        assert(SUCCEEDED(hresult));
        break;
      }
      case IDirect3DSurface9_UnlockRect:
      {
        GET_HND(pHandle);
        PULL_OBJ(RECT, pRect);
        PULL_D(Flags);
        const auto pSurface = (IDirect3DSurface9*) gpD3DResources[pHandle];
        // Now lock the rect so we can copy the data into it
        D3DLOCKED_RECT lockedRect;
        auto hresult = pSurface->LockRect(OUT & lockedRect, IN pRect, IN Flags);
        assert(S_OK == hresult);
        // Copy the data over
        const uint32_t width = pRect->right - pRect->left;
        const uint32_t height = pRect->bottom - pRect->top;
        PULL_D(dFormat);
        PULL_D(IncomingPitch);
        const D3DFORMAT format = (D3DFORMAT) dFormat;
        const size_t rowSize = bridge_util::calcRowSize(width, format);
        void* pData = nullptr;
        // If we're using the shared heap, then pData will be pointing
        // to the equivalent of a fully allocated pitch line. If we're
        // using the data queue then we've only allocated just enough 
        // space as the requested rect would fill. 
        const bool useSharedHeap = Commands::IsDataInSharedHeap(rpcHeader.flags);
        if (useSharedHeap) {
          PULL_U(allocId);
          const size_t byteOffset = bridge_util::calcImageByteOffset(IncomingPitch, *pRect, format);
          pData = SharedHeap::getBuf(allocId) + byteOffset;
        } else {
          size_t pulledSize = DeviceBridge::get_data(&pData);
          const size_t numRows = bridge_util::calcStride(height, format);
          assert(pulledSize == numRows * IncomingPitch);
        }
        FOR_EACH_RECT_ROW(lockedRect, height, format,
          memcpy(ptr, (PBYTE) pData + y * IncomingPitch, rowSize);
        )
        hresult = pSurface->UnlockRect();
        assert(SUCCEEDED(hresult));

        break;
      }
      case IDirect3DSurface9_GetDC:
        break;
      case IDirect3DSurface9_ReleaseDC:
        break;

      /*
       * IDirect3DVolume9 interface
       */
      case IDirect3DVolume9_QueryInterface:
        break;
      case IDirect3DVolume9_AddRef:
      {
        GET_HND(pHandle);
        const auto& pVolume = gpD3DVolumes[pHandle];
        pVolume->AddRef();
        break;
      }
      case IDirect3DVolume9_Destroy:
      {
        GET_HND(pHandle);
        const auto& pVolume = gpD3DVolumes[pHandle];
        safeDestroy(pVolume, pHandle);
        gpD3DVolumes.erase(pHandle);
        break;
      }
      case IDirect3DVolume9_GetDevice:
        break;
      case IDirect3DVolume9_SetPrivateData:
        break;
      case IDirect3DVolume9_GetPrivateData:
        break;
      case IDirect3DVolume9_FreePrivateData:
        break;
      case IDirect3DVolume9_GetContainer:
        break;
      case IDirect3DVolume9_GetDesc:
      {
        GET_HND(pHandle);
        PULL_OBJ(D3DVOLUME_DESC, orig_desc);
        const auto& pVolume = gpD3DVolumes[pHandle];
        D3DVOLUME_DESC pDesc;
        const auto hresult = pVolume->GetDesc(OUT & pDesc);
        assert(SUCCEEDED(hresult));
        break;
      }
      case IDirect3DVolume9_LockBox:
      {
        // This is a no-op right now because we're doing all the logic on Unlock
        break;
      }
      case IDirect3DVolume9_UnlockBox:
      {
        GET_HND(pHandle);
        PULL_OBJ(D3DBOX, pBox);
        PULL_D(Flags);
        const auto& pVolume = gpD3DVolumes[pHandle];
        // Now lock the box so we can copy the data into it
        D3DLOCKED_BOX pLockedVolume;
        auto hresult = pVolume->LockBox(IN & pLockedVolume, IN pBox, IN Flags);
        assert(S_OK == hresult);
        // Copy the data over
        PULL_U(bytesPerPixel);
        PULL_U(width);
        PULL_U(height);
        PULL_U(depth);
        const auto row_size = width * bytesPerPixel;
#ifdef SEND_ALL_LOCK_DATA_AT_ONCE
        void* data = nullptr;
        const auto slice_size = row_size * height;
        size_t pulledSize = DeviceBridge::get_data(&data);
#endif
        for (uint32_t z = 0; z < depth; z++) {
          for (uint32_t y = 0; y < height; y++) {
            auto ptr = (uintptr_t) pLockedVolume.pBits + y * pLockedVolume.RowPitch + z * pLockedVolume.SlicePitch;
#ifdef SEND_ALL_LOCK_DATA_AT_ONCE
            auto row = (uintptr_t) data + y * row_size + z * slice_size;
#else
            void* row = nullptr;
            const auto read_size = DeviceBridge::get_data(&row);
            assert(row_size == read_size);
#endif
            memcpy((void*) ptr, (void*) row, row_size);
          }
        }
#ifdef SEND_ALL_LOCK_DATA_AT_ONCE
        assert(pulledSize == depth * slice_size);
#endif
        hresult = pVolume->UnlockBox();
        assert(SUCCEEDED(hresult));
        break;
      }

      /*
       * IDirect3DQuery9 interface
       */
      case IDirect3DQuery9_QueryInterface:
        break;
      case IDirect3DQuery9_AddRef:
        break;
      case IDirect3DQuery9_Destroy:
      {
        GET_HND(pHandle);
        const auto& pQuery = (IDirect3DQuery9*) gpD3DQuery[pHandle];
        safeDestroy(pQuery, pHandle);
        gpD3DQuery.erase(pHandle);
        break;
      }
      case IDirect3DQuery9_GetDevice:
        break;
      case IDirect3DQuery9_GetType:
        break;
      case IDirect3DQuery9_GetDataSize:
        break;
      case IDirect3DQuery9_Issue:
      {
        GET_HND(pHandle);
        PULL(DWORD, dwIssueFlags);
        const auto &pQuery = gpD3DQuery[pHandle];
        const auto hresult = pQuery->Issue(dwIssueFlags);
        SEND_OPTIONAL_SERVER_RESPONSE(hresult, currentUID);
        break;
      }
      case IDirect3DQuery9_GetData:
      {
        GET_HND(pHandle);
        PULL(DWORD, dwSize);
        PULL(DWORD, dwGetDataFlags);
        const auto& pQuery = gpD3DQuery[pHandle];
        void* pData = NULL;
        if (dwSize > 0) {
          pData = new char[dwSize];
        }
        const auto hresult = pQuery->GetData(pData, dwSize, dwGetDataFlags);

        ServerMessage c(Commands::Bridge_Response, currentUID);
        c.send_data(hresult);
        if (SUCCEEDED(hresult) && dwSize > 0) {
          if (auto* blobPacketPtr = c.begin_data_blob(dwSize)) {
            memcpy(blobPacketPtr, pData, dwSize);
            c.end_data_blob();
          }
        }
      
        if (dwSize > 0) {
          delete[]pData;
        }
        break;
      }

      /*
       * Other commands
       */
      case Bridge_DebugMessage:
      {
        PULL_U(i);
        const int length = DeviceBridge::getReaderChannel().data->peek();
        void* text = nullptr;
        const int size = DeviceBridge::getReaderChannel().data->pull(&text);
        std::stringstream ss;
        ss << "DebugMessage. i = " << i << ", length = " << length << " = " << size << ", text = '" << (char*) text << "'";
        Logger::info(ss.str().c_str());
        break;
      }
      case Bridge_Terminate:
      {
        done = true;
        break;
      }
      case Bridge_SharedHeap_AddSeg:
      {
        GET_HDR_VAL(_segmentSize);
        const uint32_t segmentSize = (uint32_t) _segmentSize;
        SharedHeap::addNewHeapSegment(segmentSize);
        break;
      }
      case Bridge_SharedHeap_Alloc:
      {
        GET_HDR_VAL(_allocId);
        const auto allocId = (SharedHeap::AllocId) _allocId;
        PULL_U(chunkId);
        SharedHeap::allocate(allocId, chunkId);
        break;
      }
      case Bridge_SharedHeap_Dealloc:
      {
        GET_HDR_VAL(_allocId);
        const auto allocId = (SharedHeap::AllocId) _allocId;
        SharedHeap::deallocate(allocId);
        break;
      }
      case Bridge_UnlinkResource:
      {
        GET_HND(pHandle);
        gpD3DResources.erase(pHandle);
        break;
      }
      case Bridge_UnlinkVolumeResource:
      {
        GET_HND(pHandle);
        gpD3DVolumes.erase(pHandle);
        break;
      }
      /*
       * BridgeApi commands
       */
      case RemixApi_CreateMaterial:
      {
        // Rather than allocate deserialized struct extensions on the heap,
        // allocate them locally, since we know only one instance will be
        // supported at a time
        struct MaterialExtensions {
          serialize::MaterialInfoOpaque opaque;
          serialize::MaterialInfoOpaqueSubsurface opaqueSubsurface;
          serialize::MaterialInfoTranslucent translucent;
          serialize::MaterialInfoPortal portal;
        } exts;
        memset(&exts, 0, sizeof(MaterialExtensions));

        const auto matInfoSType = remixapi::pullSType();
        assert(matInfoSType == REMIXAPI_STRUCT_TYPE_MATERIAL_INFO);
        serialize::MaterialInfo matInfo;
        deserializeFromQueue(matInfo);
        
        matInfo.pNext = nullptr;

        bool bMatExtExists = remixapi::pullBool();
        auto* pInfoProto = &getInfoProto(matInfo);
        while(bMatExtExists) {
          const auto extSType = remixapi::pullSType();
          switch (extSType) {
            case REMIXAPI_STRUCT_TYPE_MATERIAL_INFO_OPAQUE_EXT:
            {
              assert(!exts.opaque.pNext);
              deserializeFromQueue(exts.opaque);
              pInfoProto->pNext = &(exts.opaque);
              pInfoProto = &getInfoProto(exts.opaque);
              break;
            }
            case REMIXAPI_STRUCT_TYPE_MATERIAL_INFO_OPAQUE_SUBSURFACE_EXT:
            {
              assert(!exts.opaqueSubsurface.pNext);
              deserializeFromQueue(exts.opaqueSubsurface);
              pInfoProto->pNext = &(exts.opaqueSubsurface);
              pInfoProto = &getInfoProto(exts.opaqueSubsurface);
              break;
            }
            case REMIXAPI_STRUCT_TYPE_MATERIAL_INFO_TRANSLUCENT_EXT:
            {
              assert(!exts.translucent.pNext);
              deserializeFromQueue(exts.translucent);
              pInfoProto->pNext = &(exts.translucent);
              pInfoProto = &getInfoProto(exts.translucent);
              break;
            }
            case REMIXAPI_STRUCT_TYPE_MATERIAL_INFO_PORTAL_EXT:
            {
              assert(!exts.portal.pNext);
              deserializeFromQueue(exts.portal);
              pInfoProto->pNext = &(exts.portal);
              pInfoProto = &getInfoProto(exts.portal);
              break;
            }
            default:
            {
              Logger::warn("[Api_CreateMaterial] Unknown sType. Skipping.");
              break;
            }
          }
          bMatExtExists = remixapi::pullBool();
        }

        auto bridgeHandle = DeviceBridge::get_data();
        remixapi_MaterialHandle remixApiHandle = nullptr;
        if(remixapi::g_remix.CreateMaterial(&matInfo, &remixApiHandle) == REMIXAPI_ERROR_CODE_SUCCESS) {
          MaterialHandle(bridgeHandle, remixApiHandle);
        } else {
          Logger::err("[RemixApi_CreateMaterial] Remix API call failed!");
        }
        
        break;
      }

      case RemixApi_DestroyMaterial:
      {
        MaterialHandle handle(DeviceBridge::get_data());
        if(handle.isValid()) {
          remixapi::g_remix.DestroyMaterial(handle);
          handle.invalidate();
        } else {
          Logger::err("[RemixApi_DestroyMaterial] Invalid material handle!" );
        }
        break;
      }

      case RemixApi_CreateMesh:
      {
        const auto meshInfoSType = remixapi::pullSType();
        assert(meshInfoSType == REMIXAPI_STRUCT_TYPE_MESH_INFO);
        serialize::MeshInfo meshInfo;
        deserializeFromQueue(meshInfo);
        meshInfo.pNext = nullptr;
        
        for(size_t iSurf = 0; iSurf < meshInfo.surfaces_count; ++iSurf) {
          // If we don't const_cast, we'd have to copy the entire
          // remixapi_MeshInfo::surfaces_values array in order to get around
          // the remixapi_MeshInfo's member const qualifier and reassign
          // remixapi_MeshInfoSurfaceTriangles::material.
          auto& surf = const_cast<remixapi_MeshInfoSurfaceTriangles&>(meshInfo.surfaces_values[iSurf]);
          MaterialHandle matHandle(surf.material);
          surf.material = matHandle;
        }

        auto bridgeHandle = DeviceBridge::get_data();
        remixapi_MeshHandle remixApiHandle = nullptr;
        if(remixapi::g_remix.CreateMesh(&meshInfo, &remixApiHandle) == REMIXAPI_ERROR_CODE_SUCCESS) {
          MeshHandle handle(bridgeHandle, remixApiHandle);
        } else {
          Logger::err("[RemixApi_CreateMesh] Remix API call failed!");
        }

        break;
      }

      case RemixApi_DestroyMesh:
      {
        MeshHandle handle(DeviceBridge::get_data());
        if(handle.isValid()) {
          remixapi::g_remix.DestroyMesh(handle);
          handle.invalidate();
        } else {
          Logger::err("[RemixApi_DestroyMesh] Invalid mesh handle!" );
        }
        break;
      }

      case RemixApi_DrawInstance:
      {
        // Rather than allocate deserialized struct extensions on the heap,
        // allocate them locally, since we know only one instance will be
        // supported at a time
        struct InstanceExtensions {
          serialize::InstanceInfoObjectPicking objectPicking;
          serialize::InstanceInfoBlend blend;
          serialize::InstanceInfoTransforms boneXforms;
        } exts;
        memset(&exts, 0, sizeof(InstanceExtensions));

        const auto instSType = remixapi::pullSType();
        assert(instSType == REMIXAPI_STRUCT_TYPE_INSTANCE_INFO);
        serialize::InstanceInfo instInfo;
        deserializeFromQueue(instInfo);
        
        MeshHandle meshHandle(instInfo.mesh);
        if(meshHandle.isValid()) {
          instInfo.mesh = meshHandle;
        } else {
          Logger::err("[RemixApi_DrawInstance] Invalid mesh handle!" );
        }

        instInfo.pNext = nullptr;
        
        bool bInstExtExists = remixapi::pullBool();
        auto* pInfoProto = &getInfoProto(instInfo);
        while(bInstExtExists) {
          const auto extSType = remixapi::pullSType();
          switch (extSType) {
            case REMIXAPI_STRUCT_TYPE_INSTANCE_INFO_OBJECT_PICKING_EXT:
            {
              assert(!exts.objectPicking.pNext);
              deserializeFromQueue(exts.objectPicking);
              pInfoProto->pNext = &(exts.objectPicking);
              pInfoProto = &getInfoProto(exts.objectPicking);
              break;
            }
            case REMIXAPI_STRUCT_TYPE_INSTANCE_INFO_BLEND_EXT:
            {
              assert(!exts.blend.pNext);
              deserializeFromQueue(exts.blend);
              pInfoProto->pNext = &(exts.blend);
              pInfoProto = &getInfoProto(exts.blend);
              break;
            }
            case REMIXAPI_STRUCT_TYPE_INSTANCE_INFO_BONE_TRANSFORMS_EXT:
            {
              assert(!exts.boneXforms.pNext);
              deserializeFromQueue(exts.boneXforms);
              pInfoProto->pNext = &(exts.boneXforms);
              pInfoProto = &getInfoProto(exts.boneXforms);
              break;
            }
            default:
            {
              Logger::warn("[RemixApi_DrawInstance] Unknown sType. Skipping.");
              break;
            }
          }
          bInstExtExists = remixapi::pullBool();
        }

        if(remixapi::g_remix.DrawInstance(&instInfo) != REMIXAPI_ERROR_CODE_SUCCESS) {
          Logger::err("[RemixApi_DrawInstance] Remix API call failed!");
        }

        break;
      }

      case RemixApi_CreateLight:
      {
        // Rather than allocate deserialized struct extensions on the heap,
        // allocate them locally, since we know only one instance will be
        // supported at a time
        struct LightExtensions {
          serialize::LightInfoSphere sphere;
          serialize::LightInfoRect rect;
          serialize::LightInfoDisk disk;
          serialize::LightInfoCylinder cylinder;
          serialize::LightInfoDistant distant;
          serialize::LightInfoDome dome;
          serialize::LightInfoUSD usd;
        } exts;
        memset(&exts, 0, sizeof(LightExtensions));

        const auto lightSType = remixapi::pullSType();
        assert(lightSType == REMIXAPI_STRUCT_TYPE_LIGHT_INFO);
        serialize::LightInfo lightInfo;
        deserializeFromQueue(lightInfo);
        lightInfo.pNext = nullptr;

        bool bLightExtExists = remixapi::pullBool();
        auto* pInfoProto = &getInfoProto(lightInfo);
        while(bLightExtExists) {
          const auto extSType = remixapi::pullSType();
          switch (extSType) {
            case REMIXAPI_STRUCT_TYPE_LIGHT_INFO_SPHERE_EXT:
            {
              assert(!exts.sphere.pNext);
              deserializeFromQueue(exts.sphere);
              pInfoProto->pNext = &(exts.sphere);
              pInfoProto = &getInfoProto(exts.sphere);
              break;
            }
            case REMIXAPI_STRUCT_TYPE_LIGHT_INFO_RECT_EXT:
            {
              assert(!exts.rect.pNext);
              deserializeFromQueue(exts.rect);
              pInfoProto->pNext = &(exts.rect);
              pInfoProto = &getInfoProto(exts.rect);
              break;
            }
            case REMIXAPI_STRUCT_TYPE_LIGHT_INFO_DISK_EXT:
            {
              assert(!exts.disk.pNext);
              deserializeFromQueue(exts.disk);
              pInfoProto->pNext = &(exts.disk);
              pInfoProto = &getInfoProto(exts.disk);
              break;
            }
            case REMIXAPI_STRUCT_TYPE_LIGHT_INFO_CYLINDER_EXT:
            {
              assert(!exts.cylinder.pNext);
              deserializeFromQueue(exts.cylinder);
              pInfoProto->pNext = &(exts.cylinder);
              pInfoProto = &getInfoProto(exts.cylinder);
              break;
            }
            case REMIXAPI_STRUCT_TYPE_LIGHT_INFO_DISTANT_EXT:
            {
              assert(!exts.distant.pNext);
              deserializeFromQueue(exts.distant);
              pInfoProto->pNext = &(exts.distant);
              pInfoProto = &getInfoProto(exts.distant);
              break;
            }
            case REMIXAPI_STRUCT_TYPE_LIGHT_INFO_DOME_EXT:
            {
              assert(!exts.dome.pNext);
              deserializeFromQueue(exts.dome);
              pInfoProto->pNext = &(exts.dome);
              pInfoProto = &getInfoProto(exts.dome);
              break;
            }
            case REMIXAPI_STRUCT_TYPE_LIGHT_INFO_USD_EXT:
            {
              assert(!exts.usd.pNext);
              deserializeFromQueue(exts.usd);
              pInfoProto->pNext = &(exts.usd);
              pInfoProto = &getInfoProto(exts.usd);
              break;
            }
            default:
            {
              Logger::warn("[RemixApi_CreateLight] Unknown sType. Skipping.");
              break;
            }
          }
          bLightExtExists = remixapi::pullBool();
        }

        auto bridgeHandle = DeviceBridge::get_data();
        remixapi_LightHandle lightHandle = nullptr;
        if(remixapi::g_remix.CreateLight(&lightInfo, &lightHandle) == REMIXAPI_ERROR_CODE_SUCCESS) {
          LightHandle handle(bridgeHandle, lightHandle);
        } else {
          Logger::err("[RemixApi_CreateLight] Remix API call failed!");
        }
        
        break;
      }

      case RemixApi_DestroyLight:
      {
        LightHandle handle(DeviceBridge::get_data());
        if(handle.isValid()) {
          remixapi::g_remix.DestroyLight(handle);
          handle.invalidate();
        } else {
          Logger::err("[RemixApi_DestroyLight] Invalid light handle!" );
        }
        break;
      }

      case RemixApi_DrawLightInstance:
      {
        LightHandle handle(DeviceBridge::get_data());
        if(handle.isValid()) {
          remixapi::g_remix.DrawLightInstance(handle);
        } else {
          Logger::err("[RemixApi_DrawLightInstance] Invalid light handle!" );
        }
        break;
      }

      case RemixApi_SetConfigVariable:
      {
        void* var_ptr = nullptr;
        const uint32_t var_size = DeviceBridge::getReaderChannel().data->pull(&var_ptr);
        std::string var_str((const char*) var_ptr, var_size);

        void* value_ptr = nullptr;
        const uint32_t value_size = DeviceBridge::getReaderChannel().data->pull(&value_ptr);
        std::string value_str((const char*) value_ptr, value_size);

        remixapi::g_remix.SetConfigVariable(var_str.c_str(), value_str.c_str());
        break;
      }

      case RemixApi_CreateD3D9:
      {
        Logger::err("[RemixApi_CreateD3D9] Not yet supported. Device used by Remix API defaults to "
                                          "most recently created by client application.");
        break;
      }

      case RemixApi_RegisterDevice:
      {
        Logger::err("[RemixApi_RegisterDevice] Not yet supported. Device used by Remix API defaults to "
                                              "most recently created by client application.");
        break;
      }
      
      default:
        break;
      }
    }

    // Ensure the data position between client and server is in sync after processing the command
    if (!CHECK_DATA_OFFSET)       {
      Logger::warn("Data not in sync");
    }
    assert(CHECK_DATA_OFFSET);
    *DeviceBridge::getReaderChannel().serverDataPos = DeviceBridge::get_data_pos();
    // Check if overwrite condition was met
    if (*DeviceBridge::getReaderChannel().clientDataExpectedPos != -1) {
      if (!gOverwriteConditionAlreadyActive) {
        gOverwriteConditionAlreadyActive = true;
        Logger::warn("Data Queue overwrite condition triggered");
      }
      // Check if server needs to complete a loop and the position was read
      if (*DeviceBridge::getReaderChannel().serverDataPos > *DeviceBridge::getReaderChannel().clientDataExpectedPos && !(*DeviceBridge::getReaderChannel().serverResetPosRequired)) {
        DeviceBridge::getReaderChannel().dataSemaphore->release(1);
        *DeviceBridge::getReaderChannel().clientDataExpectedPos = -1;
        gOverwriteConditionAlreadyActive = false;
        Logger::info("DataQueue overwrite condition resolved");
      }
    }

    const auto count = DeviceBridge::end_read_data();

#ifdef ENABLE_DATA_BATCHING_TRACE
    Logger::trace(format_string("Finished batch data read with %d data items.", count));
#endif

#ifdef LOG_SERVER_COMMAND_TIME
    // See how long processing this command took
    const auto diff = GetTickCount64() - start;
    if (diff > SERVER_COMMAND_THRESHOLD_MS) {
      std::string command = Commands::toString(rpcHeader.command);
      Logger::trace(format_string("Command %s took %d milliseconds to process!", command.c_str(), diff));
    }
#endif
  }

  // Check if we exited the command processing loop unexpectedly while the bridge is still enabled
  if (!done && gbBridgeRunning) {
    Logger::debug("The device command processing loop was exited unexpectedly, either due to timing out or some other command queue issue.");
  }
}

void CheckD3D9Type(HMODULE d3d9Module) {
  char d3d9Path[MAX_PATH];
  GetModuleFileName(d3d9Module, d3d9Path, sizeof(d3d9Path));
  DWORD rsvd = 0;
  DWORD verSize = GetFileVersionInfoSize(d3d9Path, &rsvd);
  BRIDGE_ASSERT_LOG((verSize > 0), "Issue retrieving D3D9_LS version info");
  BRIDGE_ASSERT_LOG((rsvd == 0), "Issue retrieving D3D9_LS version info");
  Logger::info(format_string("Loaded D3D9 from %s", d3d9Path));
  LPSTR verData = new char[verSize];
  if (GetFileVersionInfo(d3d9Path, rsvd, verSize, verData)) {
    UINT size = 0;
    LPBYTE translationBuffer = nullptr;
    if (VerQueryValue(verData, TEXT("\\VarFileInfo\\Translation"), (LPVOID*) &translationBuffer, &size)) {
      BRIDGE_ASSERT_LOG((size > 0), "Invalid size obtained while retrieving D3D9_ls version data");
      std::stringstream ss;
      ss << std::hex << std::setw(4) << std::setfill('0') << ((uint16_t*) (translationBuffer))[0];
      ss << std::hex << std::setw(4) << std::setfill('0') << ((uint16_t*) (translationBuffer))[1];
      const std::string langCodepageStr = ss.str();
      const std::string verDataProdNameLookupStr = std::string("\\StringFileInfo\\") + ss.str() + std::string("\\ProductName");
      LPBYTE productNameBuffer = nullptr;
      if (VerQueryValue(verData, verDataProdNameLookupStr.c_str(), (LPVOID*) &productNameBuffer, &size)) {
        BRIDGE_ASSERT_LOG((size > 0), "Invalid size obtained while retrieving D3D9_ls version data");
        const std::string productName((char*) productNameBuffer);
        // Assume for now that any d3d9 DLL that doesn't have Microsoft product naming is DXVK.
        bDxvkModuleLoaded = productName.find("Microsoft") == std::string::npos;
        if (!bDxvkModuleLoaded) {
          Logger::warn("Please note that the version of d3d9 loaded is NOT DXVK. Functional restrictions may apply.");
        } else {
          Logger::info("Version of d3d9 loaded is DXVK");
        }
      }
    }
  }
}

bool InitializeD3D() {
  // If vanilla dxvk is enabled attempt to load that first.
  if (ServerOptions::getUseVanillaDxvk()) {
    Logger::info("Loading standard Non-RTX DXVK d3d9 dll.");
    ghModule = LoadLibrary("d3d9vk_x64.dll");
    if (ghModule) {
      Logger::info("Non-RTX standard d3d9vk_x64.dll loaded");
    } else {
      Logger::err("d3d9vk_x64.dll loading failed!");
      return false;
    }
  } else {
    // Since vanilla dxvk is diabled attempt loading regular d3d9.dll which
    // could be either the system d3d9 one or our own Remix dxvk flavor of it.
    ghModule = LoadLibrary("d3d9.dll");
  }
  // Now check if loading the dll actually succeeded or not, and try to
  // create the D3D instance used for the lifetime of this process.
  if (ghModule != nullptr) {
    auto Direct3DCreate9 = (D3DC9) GetProcAddress(ghModule, "Direct3DCreate9");
    if (nullptr == (gpD3D = Direct3DCreate9(D3D_SDK_VERSION))) {
      Logger::err(format_string("D3D9 interface object creation failed: %ld\n", GetLastError()));
      return false;
    } else {
      Logger::info("D3D9 interface object creation succeeded!");
    }
    // Initialize remixApi
    if (GlobalOptions::getExposeRemixApi()) {
      remixapi_ErrorCode status = remixapi_lib_loadRemixDllAndInitialize(L"d3d9.dll", &remixapi::g_remix, &remixapi::g_remix_dll);
      if (status != REMIXAPI_ERROR_CODE_SUCCESS) {
        Logger::err(format_string("[RemixApi] RemixApi initialization failed: %d\n", status));
      } else {
        remixapi::g_remix_initialized = true;
        Logger::info("[RemixApi] Initialized RemixApi.");
      }
    }
  } else {
    Logger::err(format_string("d3d9.dll loading failed: %ld\n", GetLastError()));
    return false;
  }

  if (!ServerOptions::getUseVanillaDxvk()) {
    FixD3DRecordHRESULT("d3d9.dll", ghModule);
  }

  CheckD3D9Type(ghModule);
  if (bDxvkModuleLoaded) {
    auto queryFeatureVersion = (version::QueryFunc)GetProcAddress(ghModule, version::QueryFuncName);
    if(queryFeatureVersion == NULL) {
      Logger::err(format_string("Unable to resolve %s, may be the result of an outdated Remix DXVK *or* loading vanilla DXVK.\n", version::QueryFuncName));
      return true; // Not necessarily fatal
    }
    std::array<uint64_t,version::nFeatures> dxvkVersions;
    for(size_t feat = 0; feat < version::nFeatures; ++feat) {
      dxvkVersions[feat] = queryFeatureVersion((version::Feature)feat);
    }
    bool bMismatchDetected = false;
    if(version::messageChannelV != dxvkVersions[version::MessageChannel]) {
      Logger::err(format_string("MessageChannel version mismatch! Bridge: 0x%X, DXVK: 0x%X\n",
                                version::messageChannelV, dxvkVersions[version::MessageChannel]));
      bMismatchDetected = true;
    }
    if(version::fileSysV != dxvkVersions[version::FileSys]) {
      Logger::err(format_string("FileSys version mismatch! Bridge: 0x%X, DXVK: 0x%X\n",
                                version::fileSysV, dxvkVersions[version::FileSys]));
      bMismatchDetected = true;
    }
    if(bMismatchDetected) {
      Logger::warn("One or more functional version mismatches detected. If you experience problems, consider updating either bridge or dxvk.");
    } else {
      Logger::info("Feature version parity confirmed!");
    }
  }

  return true;
}

void CALLBACK OnClientExited(void* context, BOOLEAN isTimeout) {
  Logger::err("The client process has unexpectedly exited, shutting down server as well!");
  gbBridgeRunning = false;

  // Log history of recent client side commands sent and received by the server
  Logger::info("Most recent Device Queue commands sent from Client");
  DeviceBridge::Command::print_reader_data_sent();
  Logger::info("Most recent Device Queue commands received by Server");
  DeviceBridge::Command::print_reader_data_received();
  Logger::info("Most recent Module Queue commands sent from Client");
  ModuleBridge::Command::print_reader_data_sent();
  Logger::info("Most recent Module Queue commands received by Server");
  ModuleBridge::Command::print_reader_data_received();

  // Give the server some time to shut down, but then force quit so it doesn't hang forever
  uint32_t numRetries = 0;
  uint32_t maxRetries = ServerOptions::getShutdownRetries();
  uint32_t timeout = ServerOptions::getShutdownTimeout();
  while (ghModule && numRetries++ < maxRetries) {
    Sleep(timeout);
  }
  // We rely on the d3d9 module having been unloaded successfully for this to work
  if (ghModule && numRetries >= maxRetries) {
    // Terminate is stronger than ExitProcess in case some thread doesn't cleanly exit
    TerminateProcess(GetCurrentProcess(), 1);
  }
}

bool RegisterExitCallback(const uint32_t hProcess) {
  BOOL result = RegisterWaitForSingleObject(&hWait, TRUNCATE_HANDLE(HANDLE, hProcess), OnClientExited, NULL, INFINITE, WT_EXECUTEONLYONCE);
  if (!result) {
    DWORD error = GetLastError();
    Logger::err(format_string("RegisterExitCallback() failed with error code %d", error));

    const auto timeClientEnd = std::chrono::high_resolution_clock::now();
    std::stringstream uptimeSS;
    uptimeSS << "[Uptime] Client (estimated): ";
    uptimeSS <<
      std::chrono::duration_cast<std::chrono::seconds>(timeClientEnd - gTimeStart).count();
    uptimeSS << "s";
    Logger::info(uptimeSS.str());
  }

  return result;
}

static bool RegisterMessageChannel() {
  Logger::info("Registering message channel for asynchronous message handling.");

  gpClientMessageChannel = std::make_unique<MessageChannelServer>("MessageChannelServer");

  if (!gpClientMessageChannel->init(nullptr, nullptr)) {
    Logger::err("Unable to register message channel.");
    return false;
  }

  gpClientMessageChannel->registerHandler(WM_KILLFOCUS, [](uint32_t, uint32_t) {
    Logger::info("Client window became inactive, disabling timeouts for bridge server...");
    GlobalOptions::setInfiniteRetries(true);
    return true;
  });

  gpClientMessageChannel->registerHandler(WM_SETFOCUS, [](uint32_t, uint32_t) {
    Logger::info("Client window became active, reenabling timeouts for bridge server!");
    GlobalOptions::setInfiniteRetries(false);
    return true;
  });

  return true;
}

static inline bool initFileSys() {
  auto parentPid = bridge_util::getParentPid();
  DWORD accessRights = PROCESS_QUERY_INFORMATION | PROCESS_VM_READ;
  HANDLE processHandle = OpenProcess(accessRights, FALSE, parentPid);
  {
    auto executable32PathVec = createPathVec();
    if(GetModuleFileNameEx(processHandle, NULL, executable32PathVec.data(), executable32PathVec.size()) == 0) {
      Logger::err("Failed to find executable path!");
      return false;
    }
    const fspath executable32Path(executable32PathVec.data());
    const auto exe32Dir = executable32Path.parent_path();
    dxvk::util::RtxFileSys::init(exe32Dir.string());
  }
  return true;
}

int WINAPI wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ PWSTR pCmdLine, _In_ int nCmdShow) {
  gTimeStart = std::chrono::high_resolution_clock::now();
  
  if (!initFileSys()) {
    Logger::err("Failed to initialize rtx filesystem!");
    return 1;
  }

  Config::init(Config::App::Server);
  GlobalOptions::init();
  Logger::init();

  // Always setup exception handler on server
  ExceptionHandler::get().init();

  // Identify yourself
  Logger::info("==================\nNVIDIA RTX Remix Bridge Server\n==================");
  Logger::info(std::string("Version: ") + std::string(BRIDGE_VERSION));
#ifdef _WIN64
  Logger::info("Running in x64 mode!");
#else
  Logger::warn("Running in x86 mode! Are you sure this is what you want? RTX will not work this way, please run the 64-bit server instead!");
#endif

  int argCount;
  LPWSTR* argList = CommandLineToArgvW(pCmdLine, &argCount);
  BRIDGE_ASSERT_LOG((argCount >= 2), "Command line argument count received to launch server is not as expected");
  if (gUniqueIdentifier.setGuid(&argList[0])) {
    Logger::info("Launched server with GUID " + gUniqueIdentifier.toString());
  } else {
    Logger::err("Server was invoked with invalid GUID! Unable to establish bridge, exiting...");
    return 1;
  }
  if (wcscmp(argList[1], BRIDGE_VERSION_W) != 0) {
    Logger::err(format_string("Client (%s) and server (%s) version numbers do not match. Mixed version runtime execution is currently not supported! Exiting...", argList[1], BRIDGE_VERSION));
    return 1;
  }
  LocalFree(argList);

  initModuleBridge();
  initDeviceBridge();

  if (GlobalOptions::getUseSharedHeap()) {
    SharedHeap::init();
  }

  gpPresent = new NamedSemaphore("Present", GlobalOptions::getPresentSemaphoreMaxFrames(), GlobalOptions::getPresentSemaphoreMaxFrames());

  // Initialize our shared client command queue as a Reader.
  // (1) Wait for connection for client.
  Logger::info("Server started up, waiting for connection from client...");
  const auto waitForSynResult = DeviceBridge::waitForCommand(Bridge_Syn, GlobalOptions::getStartupTimeout());
  switch (waitForSynResult) {
  case Result::Timeout:
  {
    Logger::err("Timeout. Connection not established to client application/game.");
    Logger::err("Are you sure a client application/game is running and invoked this application?");
    return 1;
  }
  case Result::Failure:
  {
    Logger::err("Failed to connect to client.");
    return 1;
  }
  }
  const auto synResponse = DeviceBridge::pop_front(); // Get process handle from Syn response
  // Pulling default data sent from client to have the data queue in sync
  {
    PULL_U(uid);
  }
  Logger::info("Registering exit callback in case client exits unexpectedly.");
  RegisterExitCallback(synResponse.pHandle);

  RegisterMessageChannel();

  // (2) Load d3d9.dll, which could be original system, dxvk-remix, or something else...
  Logger::info("Initializing D3D9...");
  if (!InitializeD3D()) {
    return 1;
  }

  // (3) Send ACK to Client. Connection has been established
  Logger::info("Sync request received, sending ACK response...");
  ServerMessage { Commands::Bridge_Ack, (uintptr_t) gpClientMessageChannel->getWorkerThreadId() };

  // (4) Wait for second expected cmd: CONTINUE (ACK v. 2)
  Logger::info("Done! Now waiting for client to consume the response...");
  const auto WaitForContinueResult = DeviceBridge::waitForCommandAndDiscard(Bridge_Continue, GlobalOptions::getStartupTimeout());
  switch (WaitForContinueResult) {
  case Result::Timeout:
  {
    Logger::err("Timeout. Application failed to give go-ahead (CONTINUE) to operate.");
    return 1;
  }
  case Result::Failure:
  {
    Logger::err("Connection could to client application/game could not be finalized.");
    return 1;
  }
  }
  // Pulling default data sent from client to have the data queue in sync
  {
    PULL_U(uid);
  }
  // (5) Ready to listen for incoming commands
  Logger::info("Handshake completed! Now waiting for incoming commands...");

  std::atomic<bool> bSignalDone(false);
  auto moduleCmdProcessingThread = std::thread([&]() {
    processModuleCommandQueue(&bSignalDone);
  });
  // Process device commands
  ProcessDeviceCommandQueue();
  bSignalDone.store(true);
  moduleCmdProcessingThread.join();

  if (!dumpLeakedObjects()) {
    bridge_util::Logger::debug("No leaked objects dicovered at Direct3D module eviction.");
  }

  // Command processing finished, clean up and exit
  Logger::info("Command processing loop finished, cleaning up and exiting...");
  if (ghModule) {
    // Skip unloading the d3d9.dll for now, since it seems to be doing more harm than good
    // especially with other dependencies loaded by dxvk and threads that may deadlock due
    // to being unable to acquire certain locks during unloading.
    //FreeLibrary(ghModule);
    ghModule = nullptr;
  }

  // Clean up client exit callback handler
  if (hWait) {
    // According to MSDN docs INVALID_HANDLE_VALUE means the function
    // waits for all callback functions to complete before returning.
    UnregisterWaitEx(hWait, INVALID_HANDLE_VALUE);
    hWait = NULL;
  }

  Logger::info("Shutdown cleanup successful, exiting now!");

  const auto timeEnd = std::chrono::high_resolution_clock::now();
  std::stringstream uptimeSS;
  uptimeSS << "[Uptime]: ";
  uptimeSS <<
    std::chrono::duration_cast<std::chrono::seconds>(timeEnd - gTimeStart).count();
  uptimeSS << "s";
  Logger::info(uptimeSS.str());

  {
    ServerMessage { Commands::Bridge_Ack };
  }
  return 0;
}