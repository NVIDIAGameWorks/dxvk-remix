#include "../dxvk/dxvk_instance.h"

#include "d3d9_interface.h"
#include "d3d9_shader_validator.h"
#include "d3d9_device.h"

#include <windows.h>

class D3DFE_PROCESSVERTICES;
using PSGPERRORID = UINT;

namespace dxvk {
  Logger Logger::s_instance("d3d9.log");

  HRESULT CreateD3D9(
          bool           Extended,
          IDirect3D9Ex** ppDirect3D9Ex,
// NV-DXVK start: external API
          bool           WithExternalSwapchain = false,
          bool           WithDrawCallConversion = true,
          bool           WithRemixAPI = false) {
// NV-DXVK end
    if (!ppDirect3D9Ex)
      return D3DERR_INVALIDCALL;

// NV-DXVK start: external API / provide error code on exception
    try {
      *ppDirect3D9Ex = ref(new D3D9InterfaceEx(Extended, WithExternalSwapchain, WithDrawCallConversion, WithRemixAPI));
    }
    catch (const dxvk::DxvkErrorWithId& err) {
      Logger::err(err.message());
      return err.id();
    }
    catch (const dxvk::DxvkError& err) {
      dxvk::Logger::err(err.message());
      return D3DERR_NOTAVAILABLE;
    }
// NV-DXVK end
    return D3D_OK;
  }
}

extern "C" {

  DLLEXPORT IDirect3D9* __stdcall Direct3DCreate9(UINT nSDKVersion) {
    IDirect3D9Ex* pDirect3D = nullptr;
    dxvk::CreateD3D9(false, &pDirect3D);

    return pDirect3D;
  }

  DLLEXPORT HRESULT __stdcall Direct3DCreate9Ex(UINT nSDKVersion, IDirect3D9Ex** ppDirect3D9Ex) {
    return dxvk::CreateD3D9(true, ppDirect3D9Ex);
  }

  DLLEXPORT int __stdcall D3DPERF_BeginEvent(D3DCOLOR col, LPCWSTR wszName) {
    return 0;
  }

  DLLEXPORT int __stdcall D3DPERF_EndEvent(void) {
    return 0;
  }

  DLLEXPORT void __stdcall D3DPERF_SetMarker(D3DCOLOR col, LPCWSTR wszName) {
  }

  DLLEXPORT void __stdcall D3DPERF_SetRegion(D3DCOLOR col, LPCWSTR wszName) {
  }

  DLLEXPORT BOOL __stdcall D3DPERF_QueryRepeatFrame(void) {
    return FALSE;
  }

  DLLEXPORT void __stdcall D3DPERF_SetOptions(DWORD dwOptions) {
  }

  DLLEXPORT DWORD __stdcall D3DPERF_GetStatus(void) {
    return 0;
  }


  DLLEXPORT void __stdcall DebugSetMute(void) {
  }

  DLLEXPORT int __stdcall DebugSetLevel(void) {
    return 0;
  }

  // Processor Specific Geometry Pipeline
  // for P3 SIMD/AMD 3DNow.

  DLLEXPORT void __stdcall PSGPError(D3DFE_PROCESSVERTICES* a, PSGPERRORID b, UINT c) {
  }

  DLLEXPORT void __stdcall PSGPSampleTexture(D3DFE_PROCESSVERTICES* a, UINT b, float(*const c)[4], UINT d, float(*const e)[4]) {
  }

  DLLEXPORT dxvk::D3D9ShaderValidator* __stdcall Direct3DShaderValidatorCreate9(void) {
    return ref(new dxvk::D3D9ShaderValidator());
  }

  DLLEXPORT int __stdcall Direct3D9EnableMaximizedWindowedModeShim(UINT a) {
    return 0;
  }
}

// NV-DXVK start: functional versioning export + external API
#include <remix/remix_c.h>
#include "../util/util_messagechannel.h"
#include "../util/util_version.h"

// https://stackoverflow.com/a/27490954
constexpr bool strings_equal(char const * a, char const * b) {
  return *a == *b && (*a == '\0' || strings_equal(a + 1, b + 1));
}

extern "C" {
  DLLEXPORT uint64_t __stdcall QueryFeatureVersion(version::Feature feat) {
    static_assert(strings_equal(__func__, version::QueryFuncName));
    static_assert(std::is_same_v< decltype(&QueryFeatureVersion), version::QueryFunc >);
    switch(feat){
      case version::MessageChannel:
      {
        return version::messageChannelV;
      }
      case version::RemixApi:
      {
        static constexpr uint64_t remixApiV = REMIXAPI_VERSION_MAKE(REMIXAPI_VERSION_MAJOR, REMIXAPI_VERSION_MINOR, REMIXAPI_VERSION_PATCH);
        return remixApiV;
      }
      default:
      {
        dxvk::Logger::err(dxvk::str::format("Could not find feature version for: ", feat));
        return 0ull;
      }
    }
  }
}

void dummy() {
  // need to reference a function so it's exported from d3d9.dll
  remixapi_InitializeLibrary(nullptr, nullptr);
}
// NV-DXVK end
