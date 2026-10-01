/*
* Copyright (c) 2021-2026, NVIDIA CORPORATION. All rights reserved.
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

// This file builds a thin d3d9.dll proxy. It loads the real dxvk-remix D3D9 implementation
// DLL from the same directory as itself and forwards every export to it via GetProcAddress.
// This lets arm systems to use arm64ec implementation without client app changes.

#include <filesystem>
#include <mutex>

#include "d3d9_shader_validator.h"
#include "../util/util_version.h"

class D3DFE_PROCESSVERTICES;
using PSGPERRORID = UINT;

#include <remix/remix_c.h>

// Note: the suffixes must match meson.build config
static constexpr wchar_t* kX64Dll = L"_x64.dll";
static constexpr wchar_t* kArm64ecDll = L"_a64ec.dll";

dxvk::Logger dxvk::Logger::s_instance("remix-shim.log", LogLevel::Error);

namespace {

  using PFN_D3DPERF_BeginEvent                       = int(__stdcall*)(D3DCOLOR, LPCWSTR);
  using PFN_D3DPERF_EndEvent                         = int(__stdcall*)(void);
  using PFN_D3DPERF_GetStatus                        = DWORD(__stdcall*)(void);
  using PFN_D3DPERF_QueryRepeatFrame                 = BOOL(__stdcall*)(void);
  using PFN_D3DPERF_SetMarker                        = void(__stdcall*)(D3DCOLOR, LPCWSTR);
  using PFN_D3DPERF_SetOptions                       = void(__stdcall*)(DWORD);
  using PFN_D3DPERF_SetRegion                        = void(__stdcall*)(D3DCOLOR, LPCWSTR);
  using PFN_DebugSetLevel                            = int(__stdcall*)(void);
  using PFN_DebugSetMute                             = void(__stdcall*)(void);
  using PFN_Direct3D9EnableMaximizedWindowedModeShim = int(__stdcall*)(UINT);
  using PFN_Direct3DCreate9                          = IDirect3D9*(__stdcall*)(UINT);
  using PFN_Direct3DCreate9Ex                        = HRESULT(__stdcall*)(UINT, IDirect3D9Ex**);
  using PFN_Direct3DShaderValidatorCreate9           = dxvk::D3D9ShaderValidator*(__stdcall*)(void);
  using PFN_PSGPError                                = void(__stdcall*)(D3DFE_PROCESSVERTICES*, PSGPERRORID, UINT);
  using PFN_PSGPSampleTexture                        = void(__stdcall*)(D3DFE_PROCESSVERTICES*, UINT, float(*const)[4], UINT, float(*const)[4]);

  using PFN_QueryFeatureVersion                      = uint64_t(REMIXAPI_CALL*)(version::Feature);
  using PFN_RtxSentryRunUploadHelper                 = void(REMIXAPI_CALL*)(const char*, const char*);
  using PFN_RtxSentryShutdown                        = void(REMIXAPI_CALL*)();
  using PFN_remixinternal_GetNrcStatus               = int(*)(uint32_t*);
  using PFN_remixinternal_GetShaderCompilationCount  = uint32_t(*)();
  using PFN_writeAllMarkdownDocs                     = bool(*)(const char*);
  using PFN_writeAllOGNSchemas                       = bool(*)(const char*);
  using PFN_writeMarkdownDocumentation               = bool(*)(const char*);
  using PFN_writeRemixCategoriesSchemaUsda           = bool(*)(const char*);
  using PFN_remixinternal_GetDlssNeuralRenderingStatus = int(*)();

  struct ShimExports {
    PFN_D3DPERF_BeginEvent                       D3DPERF_BeginEvent = nullptr;
    PFN_D3DPERF_EndEvent                         D3DPERF_EndEvent = nullptr;
    PFN_D3DPERF_GetStatus                        D3DPERF_GetStatus = nullptr;
    PFN_D3DPERF_QueryRepeatFrame                 D3DPERF_QueryRepeatFrame = nullptr;
    PFN_D3DPERF_SetMarker                        D3DPERF_SetMarker = nullptr;
    PFN_D3DPERF_SetOptions                       D3DPERF_SetOptions = nullptr;
    PFN_D3DPERF_SetRegion                        D3DPERF_SetRegion = nullptr;
    PFN_DebugSetLevel                            DebugSetLevel = nullptr;
    PFN_DebugSetMute                             DebugSetMute = nullptr;
    PFN_Direct3D9EnableMaximizedWindowedModeShim Direct3D9EnableMaximizedWindowedModeShim = nullptr;
    PFN_Direct3DCreate9                          Direct3DCreate9 = nullptr;
    PFN_Direct3DCreate9Ex                        Direct3DCreate9Ex = nullptr;
    PFN_Direct3DShaderValidatorCreate9           Direct3DShaderValidatorCreate9 = nullptr;
    PFN_PSGPError                                PSGPError = nullptr;
    PFN_PSGPSampleTexture                        PSGPSampleTexture = nullptr;

    PFN_QueryFeatureVersion                      QueryFeatureVersion = nullptr;
    PFN_RtxSentryRunUploadHelper                 RtxSentryRunUploadHelper = nullptr;
    PFN_RtxSentryShutdown                        RtxSentryShutdown = nullptr;
    PFN_remixapi_InitializeLibrary               remixapi_InitializeLibrary = nullptr;
    PFN_remixinternal_GetNrcStatus               remixinternal_GetNrcStatus = nullptr;
    PFN_remixinternal_GetShaderCompilationCount  remixinternal_GetShaderCompilationCount = nullptr;
    PFN_writeAllMarkdownDocs                     writeAllMarkdownDocs = nullptr;
    PFN_writeAllOGNSchemas                       writeAllOGNSchemas = nullptr;
    PFN_writeMarkdownDocumentation               writeMarkdownDocumentation = nullptr;
    PFN_writeRemixCategoriesSchemaUsda           writeRemixCategoriesSchemaUsda = nullptr;
    PFN_remixinternal_GetDlssNeuralRenderingStatus remixinternal_GetDlssNeuralRenderingStatus = nullptr;
  };

  std::once_flag g_shimInitFlag;
  ShimExports g_shimExports;
  HMODULE g_shimModule = nullptr;
  std::filesystem::path g_shimTargetDllPath;

  void fatalError(std::string errMsg) {
      dxvk::Logger::err(errMsg);
      throw dxvk::DxvkError(std::move(errMsg));
  }

  template<typename T>
  void loadExport(T& outFn, const char* name) {
    outFn = reinterpret_cast<T>(GetProcAddress(g_shimModule, name));
    if (outFn == nullptr) {
      fatalError(dxvk::str::format("d3d9 shim: failed to resolve export '", name,
                                   "' from the implementation DLL ", g_shimTargetDllPath.string(), "."));
    }
  }

  void initShim() {
    HMODULE selfModule = nullptr;
    GetModuleHandleExW(
      GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
      reinterpret_cast<LPCWSTR>(&initShim),
      &selfModule);

    wchar_t modulePathBuffer[MAX_PATH];
    const DWORD pathLen = GetModuleFileNameW(selfModule, modulePathBuffer, MAX_PATH);
    if (pathLen == 0 || pathLen == MAX_PATH) {
      fatalError("d3d9 shim: failed to resolve the shim's own module path.");
    }

    USHORT curMachine, nativeMachine = IMAGE_FILE_MACHINE_AMD64;
    IsWow64Process2(GetCurrentProcess(), &curMachine, &nativeMachine);

    const wchar_t* shimTargetArchDll = 
      (nativeMachine == IMAGE_FILE_MACHINE_ARM64) ? kArm64ecDll : kX64Dll;

    g_shimTargetDllPath = std::filesystem::path(modulePathBuffer).replace_extension().concat(shimTargetArchDll);

    // Now check if arm64ec binary exists and fallback to x64 if it does not.
    std::error_code ec;
    if (nativeMachine == IMAGE_FILE_MACHINE_ARM64 && !std::filesystem::exists(g_shimTargetDllPath, ec)) {
      // Fallback to x64 binary which must always exist
      g_shimTargetDllPath = std::filesystem::path(modulePathBuffer).replace_extension().concat(kX64Dll);
    }

    g_shimModule = LoadLibraryW(g_shimTargetDllPath.c_str());
    if (g_shimModule == nullptr) {
      fatalError(dxvk::str::format("d3d9 shim: failed to load implementation DLL at ", g_shimTargetDllPath.string(), "."));
    }

#define LOAD_SHIM_EXPORT(name) loadExport(g_shimExports.name, #name)
    LOAD_SHIM_EXPORT(D3DPERF_BeginEvent);
    LOAD_SHIM_EXPORT(D3DPERF_EndEvent);
    LOAD_SHIM_EXPORT(D3DPERF_GetStatus);
    LOAD_SHIM_EXPORT(D3DPERF_QueryRepeatFrame);
    LOAD_SHIM_EXPORT(D3DPERF_SetMarker);
    LOAD_SHIM_EXPORT(D3DPERF_SetOptions);
    LOAD_SHIM_EXPORT(D3DPERF_SetRegion);
    LOAD_SHIM_EXPORT(DebugSetLevel);
    LOAD_SHIM_EXPORT(DebugSetMute);
    LOAD_SHIM_EXPORT(Direct3D9EnableMaximizedWindowedModeShim);
    LOAD_SHIM_EXPORT(Direct3DCreate9);
    LOAD_SHIM_EXPORT(Direct3DCreate9Ex);
    LOAD_SHIM_EXPORT(Direct3DShaderValidatorCreate9);
    LOAD_SHIM_EXPORT(PSGPError);
    LOAD_SHIM_EXPORT(PSGPSampleTexture);

    LOAD_SHIM_EXPORT(QueryFeatureVersion);
    LOAD_SHIM_EXPORT(RtxSentryRunUploadHelper);
    LOAD_SHIM_EXPORT(RtxSentryShutdown);
    LOAD_SHIM_EXPORT(remixapi_InitializeLibrary);
    LOAD_SHIM_EXPORT(remixinternal_GetNrcStatus);
    LOAD_SHIM_EXPORT(remixinternal_GetShaderCompilationCount);
    LOAD_SHIM_EXPORT(writeAllMarkdownDocs);
    LOAD_SHIM_EXPORT(writeAllOGNSchemas);
    LOAD_SHIM_EXPORT(writeMarkdownDocumentation);
    LOAD_SHIM_EXPORT(writeRemixCategoriesSchemaUsda);
    LOAD_SHIM_EXPORT(remixinternal_GetDlssNeuralRenderingStatus);
#undef LOAD_SHIM_EXPORT
  }

  const ShimExports& shim() {
    std::call_once(g_shimInitFlag, initShim);
    return g_shimExports;
  }

}

// Standard D3D9 exports. Do not modify, add new Remix function to the section below.

extern "C" {

  DLLEXPORT int __stdcall D3DPERF_BeginEvent(D3DCOLOR col, LPCWSTR wszName) {
    return shim().D3DPERF_BeginEvent ? shim().D3DPERF_BeginEvent(col, wszName) : 0;
  }

  DLLEXPORT int __stdcall D3DPERF_EndEvent(void) {
    return shim().D3DPERF_EndEvent ? shim().D3DPERF_EndEvent() : 0;
  }

  DLLEXPORT DWORD __stdcall D3DPERF_GetStatus(void) {
    return shim().D3DPERF_GetStatus ? shim().D3DPERF_GetStatus() : 0;
  }

  DLLEXPORT BOOL __stdcall D3DPERF_QueryRepeatFrame(void) {
    return shim().D3DPERF_QueryRepeatFrame ? shim().D3DPERF_QueryRepeatFrame() : FALSE;
  }

  DLLEXPORT void __stdcall D3DPERF_SetMarker(D3DCOLOR col, LPCWSTR wszName) {
    if (shim().D3DPERF_SetMarker) {
      shim().D3DPERF_SetMarker(col, wszName);
    }
  }

  DLLEXPORT void __stdcall D3DPERF_SetOptions(DWORD dwOptions) {
    if (shim().D3DPERF_SetOptions) {
      shim().D3DPERF_SetOptions(dwOptions);
    }
  }

  DLLEXPORT void __stdcall D3DPERF_SetRegion(D3DCOLOR col, LPCWSTR wszName) {
    if (shim().D3DPERF_SetRegion) {
      shim().D3DPERF_SetRegion(col, wszName);
    }
  }

  DLLEXPORT int __stdcall DebugSetLevel(void) {
    return shim().DebugSetLevel ? shim().DebugSetLevel() : 0;
  }

  DLLEXPORT void __stdcall DebugSetMute(void) {
    if (shim().DebugSetMute) {
      shim().DebugSetMute();
    }
  }

  DLLEXPORT int __stdcall Direct3D9EnableMaximizedWindowedModeShim(UINT a) {
    return shim().Direct3D9EnableMaximizedWindowedModeShim ? shim().Direct3D9EnableMaximizedWindowedModeShim(a) : 0;
  }

  DLLEXPORT IDirect3D9* __stdcall Direct3DCreate9(UINT nSDKVersion) {
    return shim().Direct3DCreate9 ? shim().Direct3DCreate9(nSDKVersion) : nullptr;
  }

  DLLEXPORT HRESULT __stdcall Direct3DCreate9Ex(UINT nSDKVersion, IDirect3D9Ex** ppDirect3D9Ex) {
    return shim().Direct3DCreate9Ex ? shim().Direct3DCreate9Ex(nSDKVersion, ppDirect3D9Ex) : D3DERR_NOTAVAILABLE;
  }

  DLLEXPORT dxvk::D3D9ShaderValidator* __stdcall Direct3DShaderValidatorCreate9(void) {
    return shim().Direct3DShaderValidatorCreate9 ? shim().Direct3DShaderValidatorCreate9() : nullptr;
  }

  DLLEXPORT void __stdcall PSGPError(D3DFE_PROCESSVERTICES* a, PSGPERRORID b, UINT c) {
    if (shim().PSGPError) {
      shim().PSGPError(a, b, c);
    }
  }

  DLLEXPORT void __stdcall PSGPSampleTexture(D3DFE_PROCESSVERTICES* a, UINT b, float(*const c)[4], UINT d, float(*const e)[4]) {
    if (shim().PSGPSampleTexture) {
      shim().PSGPSampleTexture(a, b, c, d, e);
    }
  }

}

// Remix exports

extern "C" {

  REMIXAPI uint64_t REMIXAPI_CALL QueryFeatureVersion(version::Feature feat) {
    return shim().QueryFeatureVersion ? shim().QueryFeatureVersion(feat) : 0ull;
  }

  REMIXAPI void REMIXAPI_CALL RtxSentryRunUploadHelper(const char* sentryDatabasePathUtf8, const char* crashType) {
    if (shim().RtxSentryRunUploadHelper) {
      shim().RtxSentryRunUploadHelper(sentryDatabasePathUtf8, crashType);
    }
  }

  REMIXAPI void REMIXAPI_CALL RtxSentryShutdown() {
    if (shim().RtxSentryShutdown) {
      shim().RtxSentryShutdown();
    }
  }

  REMIXAPI remixapi_ErrorCode REMIXAPI_CALL remixapi_InitializeLibrary(
      const remixapi_InitializeLibraryInfo* info,
      remixapi_Interface*                   out_result) {
    return shim().remixapi_InitializeLibrary
      ? shim().remixapi_InitializeLibrary(info, out_result)
      : REMIXAPI_ERROR_CODE_GENERAL_FAILURE;
  }

  REMIXAPI int remixinternal_GetNrcStatus(uint32_t* outTrainingRecords) {
    return shim().remixinternal_GetNrcStatus ? shim().remixinternal_GetNrcStatus(outTrainingRecords) : -1;
  }

  REMIXAPI uint32_t remixinternal_GetShaderCompilationCount() {
    return shim().remixinternal_GetShaderCompilationCount ? shim().remixinternal_GetShaderCompilationCount() : UINT32_MAX;
  }

  REMIXAPI bool writeAllMarkdownDocs(const char* outputFolderPath) {
    return shim().writeAllMarkdownDocs ? shim().writeAllMarkdownDocs(outputFolderPath) : false;
  }

  REMIXAPI bool writeAllOGNSchemas(const char* outputFolderPath) {
    return shim().writeAllOGNSchemas ? shim().writeAllOGNSchemas(outputFolderPath) : false;
  }

  REMIXAPI bool writeMarkdownDocumentation(const char* outputMarkdownFilePath) {
    return shim().writeMarkdownDocumentation ? shim().writeMarkdownDocumentation(outputMarkdownFilePath) : false;
  }

  REMIXAPI bool writeRemixCategoriesSchemaUsda(const char* outputFilePath) {
    return shim().writeRemixCategoriesSchemaUsda ? shim().writeRemixCategoriesSchemaUsda(outputFilePath) : false;
  }

  REMIXAPI int remixinternal_GetDlssNeuralRenderingStatus() {
    return shim().remixinternal_GetDlssNeuralRenderingStatus ? shim().remixinternal_GetDlssNeuralRenderingStatus() : -1;
  }

}
