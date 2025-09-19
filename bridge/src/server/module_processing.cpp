#pragma once

#include <windows.h>

#include "module_processing.h"

#include "remix_api.h"

#include "util_bridge_assert.h"
#include "util_modulecommand.h"

#include "log/log.h"

#include <d3d9.h>

using namespace Commands;

// Mapping between client and server pointer addresses
extern LPDIRECT3D9 gpD3D;

#define PULL(type, name) const auto& name = (type)ModuleBridge::get_data()
#define PULL_I(name) PULL(INT, name)
#define PULL_U(name) PULL(UINT, name)
#define PULL_D(name) PULL(DWORD, name)
#define PULL_H(name) PULL(HRESULT, name)
#define PULL_HND(name) \
            PULL_U(name); \
            assert(name != NULL)
#define PULL_DATA(size, name) \
            uint32_t name##_len = ModuleBridge::get_data((void**)&name); \
            assert(name##_len == 0 || size == name##_len)
#define PULL_OBJ(type, name) \
            type* name = nullptr; \
            PULL_DATA(sizeof(type), name)
#define CHECK_DATA_OFFSET (ModuleBridge::get_data_pos() == rpcHeader.dataOffset)
#define GET_HND(name) \
            const auto& name = rpcHeader.pHandle; \
            assert(name != NULL)
#define GET_HDR_VAL(name) \
            const DWORD& name = rpcHeader.pHandle;
#define GET_RES(name, map) \
            GET_HND(name##Handle); \
            const auto& name = map[name##Handle]; \
            assert(name != NULL)

// NOTE: MSDN states HWNDs are safe to cross x86-->x64 boundary, and that a truncating cast should be used: https://docs.microsoft.com/en-us/windows/win32/winprog64/interprocess-communication?redirectedfrom=MSDN
#define TRUNCATE_HANDLE(type, input) (type)(size_t)(input)

namespace {
  D3DPRESENT_PARAMETERS getPresParamFromRaw(const uint32_t* rawPresentationParameters) {
    D3DPRESENT_PARAMETERS presParam;
    // Set up presentation parameters. We can't just directly cast the structure because the hDeviceWindow
    // handle is 32-bit in the data coming in but 64-bit in the x64 version of the struct.
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
}

void processModuleCommandQueue(std::atomic<bool>* const pbSignalEnd) {
  bool destroyReceived = false;
  while (RESULT_SUCCESS(ModuleBridge::waitForCommand(
    Commands::Bridge_Any, 0, pbSignalEnd))) {
    const Header rpcHeader = ModuleBridge::pop_front();
    PULL_U(currentUID);
#if defined(_DEBUG) || defined(DEBUGOPT)
    if (GlobalOptions::getLogServerCommands()) {
      Logger::info("Module Processing: " + toString(rpcHeader.command) + " UID: " + std::to_string(currentUID));
    }
#endif
    // The mother of all switch statements - every call in the D3D9 interface is mapped here...
    switch (rpcHeader.command) {
      /*
        * IDirect3D9 interface
        */
      case IDirect3D9Ex_QueryInterface:
        break;
      case IDirect3D9Ex_AddRef:
      {
        // The server controls its own device lifetime completely - no op
        break;
      }
      case IDirect3D9Ex_Destroy:
      {
        bridge_util::Logger::info("D3D9 Module destroyed.");
        destroyReceived = true;
        break;
      }
      case IDirect3D9Ex_RegisterSoftwareDevice:
        break;
      case IDirect3D9Ex_GetAdapterCount:
      {
        const auto cnt = gpD3D->GetAdapterCount();
        {
          ModuleServerCommand c(Commands::Bridge_Response, currentUID);
          c.send_data(cnt);
        }
        break;
      }
      case IDirect3D9Ex_GetAdapterIdentifier:
      {
        PULL_U(Adapter);
        PULL_D(Flags);
        D3DADAPTER_IDENTIFIER9 pIdentifier;
        auto hresult = gpD3D->GetAdapterIdentifier(IN Adapter, IN Flags, OUT & pIdentifier);
        {
          ModuleServerCommand c(Commands::Bridge_Response, currentUID);
          c.send_data(hresult);
          if (SUCCEEDED(hresult)) {
            c.send_data(sizeof(D3DADAPTER_IDENTIFIER9), &pIdentifier);
          }
        }
        break;
      }
      case IDirect3D9Ex_GetAdapterModeCount:
      {
        PULL_U(Adapter);
        PULL(D3DFORMAT, Format);
        const auto cnt = gpD3D->GetAdapterModeCount(IN Adapter, IN Format);
        {
          ModuleServerCommand c(Commands::Bridge_Response, currentUID);
          c.send_data(cnt);
        }
        break;
      }
      case IDirect3D9Ex_EnumAdapterModes:
      {
        PULL_U(Adapter);
        PULL(D3DFORMAT, Format);
        PULL_U(Mode);
        D3DDISPLAYMODE pMode;
        const auto hresult = gpD3D->EnumAdapterModes(IN Adapter, IN Format, IN Mode, OUT & pMode);
        BRIDGE_ASSERT_LOG(SUCCEEDED(hresult), "Issue checking Adapter compatibility with required format");
        {
          ModuleServerCommand c(Commands::Bridge_Response, currentUID);
          c.send_data(hresult);
          if (SUCCEEDED(hresult)) {
            c.send_data(sizeof(D3DDISPLAYMODE), &pMode);
          }
        }
        break;
      }
      case IDirect3D9Ex_GetAdapterDisplayMode:
      {
        PULL_U(Adapter);
        D3DDISPLAYMODE pMode;
        const auto hresult = gpD3D->GetAdapterDisplayMode(IN Adapter, OUT & pMode);
        BRIDGE_ASSERT_LOG(SUCCEEDED(hresult), "Issue retrieving Adapter display mode");
        {
          ModuleServerCommand c(Commands::Bridge_Response, currentUID);
          c.send_data(hresult);
          if (SUCCEEDED(hresult)) {
            c.send_data(sizeof(D3DDISPLAYMODE), &pMode);
          }
        }
        break;
      }
      case IDirect3D9Ex_CheckDeviceType:
      {
        PULL_U(Adapter);
        PULL(D3DDEVTYPE, DevType);
        PULL(D3DFORMAT, AdapterFormat);
        PULL(D3DFORMAT, BackBufferFormat);
        PULL(BOOL, bWindowed);
        const auto hresult = gpD3D->CheckDeviceType(IN Adapter, IN DevType, IN AdapterFormat, IN BackBufferFormat, IN bWindowed);
        {
          ModuleServerCommand c(Commands::Bridge_Response, currentUID);
          c.send_data(hresult);
        }
        break;
      }
      case IDirect3D9Ex_CheckDeviceFormat:
      {
        PULL_U(Adapter);
        PULL(D3DDEVTYPE, DeviceType);
        PULL(D3DFORMAT, AdapterFormat);
        PULL_D(Usage);
        PULL(D3DRESOURCETYPE, RType);
        PULL(D3DFORMAT, CheckFormat);
        const auto hresult = gpD3D->CheckDeviceFormat(IN Adapter, IN DeviceType, IN AdapterFormat, IN Usage, IN RType, IN CheckFormat);
        {
          ModuleServerCommand c(Commands::Bridge_Response, currentUID);
          c.send_data(hresult);
        }
        break;
      }
      case IDirect3D9Ex_CheckDeviceMultiSampleType:
      {
        PULL_U(Adapter);
        PULL(D3DDEVTYPE, DeviceType);
        PULL(D3DFORMAT, SurfaceFormat);
        PULL(BOOL, Windowed);
        PULL(D3DMULTISAMPLE_TYPE, MultiSampleType);

        DWORD QualityLevels;
        const auto hresult = gpD3D->CheckDeviceMultiSampleType(IN Adapter, IN DeviceType, IN SurfaceFormat, IN Windowed, IN MultiSampleType, OUT & QualityLevels);
        {
          ModuleServerCommand c(Commands::Bridge_Response, currentUID);
          c.send_data(hresult);
          c.send_data(QualityLevels);
        }
        break;
      }
      case IDirect3D9Ex_CheckDepthStencilMatch:
      {
        PULL_U(Adapter);
        PULL(D3DDEVTYPE, DeviceType);
        PULL(D3DFORMAT, AdapterFormat);
        PULL(D3DFORMAT, RenderTargetFormat);
        PULL(D3DFORMAT, DepthStencilFormat);
        const auto hresult = gpD3D->CheckDepthStencilMatch(IN Adapter, IN DeviceType, IN AdapterFormat, IN RenderTargetFormat, IN DepthStencilFormat);
        {
          ModuleServerCommand c(Commands::Bridge_Response, currentUID);
          c.send_data(hresult);
        }
        break;
      }
      case IDirect3D9Ex_CheckDeviceFormatConversion:
      {
        PULL_U(Adapter);
        PULL(D3DDEVTYPE, DeviceType);
        PULL(D3DFORMAT, SourceFormat);
        PULL(D3DFORMAT, TargetFormat);
        const auto hresult = gpD3D->CheckDeviceFormatConversion(IN Adapter, IN DeviceType, IN SourceFormat, IN TargetFormat);
        {
          ModuleServerCommand c(Commands::Bridge_Response, currentUID);
          c.send_data(hresult);
        }
        break;
      }
      case IDirect3D9Ex_GetDeviceCaps:
      {
        PULL_U(Adapter);
        PULL(D3DDEVTYPE, DeviceType);

        D3DCAPS9 pCaps;
        // Too many members in D3DCAPS so we just check the return value for now.
        const auto hresult = gpD3D->GetDeviceCaps(IN Adapter, IN DeviceType, OUT & pCaps);
        BRIDGE_ASSERT_LOG(SUCCEEDED(hresult), "Issue retrieving D3D9 device specific information");
        {
          ModuleServerCommand c(Commands::Bridge_Response, currentUID);
          c.send_data(hresult);
          if (SUCCEEDED(hresult)) {
            c.send_data(sizeof(D3DCAPS9), &pCaps);
          }
        }
        break;
      }
      case IDirect3D9Ex_GetAdapterMonitor:
      {
        PULL_U(Adapter);
        HMONITOR hmonitor = gpD3D->GetAdapterMonitor(IN Adapter);
        {
          ModuleServerCommand c(Commands::Bridge_Response, currentUID);
          // Truncate handle before sending back to client because it expects a 32-bit size handle
          c.send_data(TRUNCATE_HANDLE(uint32_t, hmonitor));
        }
        break;
      }
      case IDirect3D9Ex_GetAdapterModeCountEx:
      {
        PULL_U(Adapter);
        D3DDISPLAYMODEFILTER modeFilter;
        PULL_DATA(sizeof(D3DDISPLAYMODEFILTER), modeFilter);
        const auto cnt = ((IDirect3D9Ex*) gpD3D)->GetAdapterModeCountEx(Adapter, &modeFilter);
        {
          ModuleServerCommand c(Commands::Bridge_Response, currentUID);
          c.send_data(cnt);
        }
        break;
      }
      case IDirect3D9Ex_GetAdapterLUID:
      {
        PULL_U(Adapter);
        LUID pLUID;
        HRESULT hresult = ((IDirect3D9Ex*) gpD3D)->GetAdapterLUID(Adapter, &pLUID);
        {
          ModuleServerCommand c(Commands::Bridge_Response, currentUID);
          c.send_data(hresult);
          if (SUCCEEDED(hresult)) {
            c.send_data(sizeof(LUID), &pLUID);
          }
        }
        break;
      }
      case IDirect3D9Ex_EnumAdapterModesEx:
      {
        PULL_U(Adapter);
        PULL_U(Mode);
        D3DDISPLAYMODEFILTER* pFilter = nullptr;
        PULL_DATA(sizeof(D3DDISPLAYMODEFILTER), pFilter);
        D3DDISPLAYMODEEX pMode;
        HRESULT hresult = ((IDirect3D9Ex*) gpD3D)->EnumAdapterModesEx(Adapter, pFilter, Mode, &pMode);
        {
          ModuleServerCommand c(Commands::Bridge_Response, currentUID);
          c.send_data(hresult);
          if (SUCCEEDED(hresult)) {
            c.send_data(sizeof(D3DDISPLAYMODEEX), &pMode);
          }
        }
        break;
      }
      case IDirect3D9Ex_GetAdapterDisplayModeEx:
      {
        PULL_U(Adapter);
        D3DDISPLAYMODEEX* pMode = nullptr;
        PULL_DATA(sizeof(D3DDISPLAYMODEEX), pMode);
        D3DDISPLAYROTATION* pRotation = nullptr;
        PULL_DATA(sizeof(D3DDISPLAYROTATION), pRotation);
        HRESULT hresult = ((IDirect3D9Ex*) gpD3D)->GetAdapterDisplayModeEx(Adapter, pMode, pRotation);
        {
          ModuleServerCommand c(Commands::Bridge_Response, currentUID);
          c.send_data(hresult);
          if (SUCCEEDED(hresult)) {
            c.send_data(sizeof(D3DDISPLAYMODEEX), pMode);
            c.send_data(sizeof(D3DDISPLAYROTATION), pRotation);
          }
        }
        break;
      }
    }
  }
  // Check if we exited the command processing loop unexpectedly while the bridge is still enabled
  if (!destroyReceived && gbBridgeRunning) {
    Logger::info("The module command processing loop was exited unexpectedly, either due to timing out or some other command queue issue.");
  }
}
