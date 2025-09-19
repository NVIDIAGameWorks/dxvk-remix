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
#ifndef UTIL_COMMANDS_H_
#define UTIL_COMMANDS_H_

#include <cstdint>
#include <string>
#include <limits>

namespace Commands {
  // The complete set of D3D9 interfaces
  enum D3D9Command : uint16_t {
    Bridge_Terminate = std::numeric_limits<uint16_t>::max(),
    Bridge_Invalid = 0,
    Bridge_Syn,
    Bridge_Ack,
    Bridge_Continue,
    Bridge_Any,
    Bridge_Response,
    Bridge_DebugMessage,

    RemixApi_CreateMaterial,
    RemixApi_DestroyMaterial,
    RemixApi_CreateMesh,
    RemixApi_DestroyMesh,
    RemixApi_DrawInstance,
    RemixApi_CreateLight,
    RemixApi_DestroyLight,
    RemixApi_DrawLightInstance,
    RemixApi_SetConfigVariable,
    RemixApi_CreateD3D9,
    RemixApi_RegisterDevice,

    Bridge_SharedHeap_AddSeg,
    Bridge_SharedHeap_Alloc,
    Bridge_SharedHeap_Dealloc,

    // Unlink x86 d3d9 resource from x64 counterpart to prevent hash
    // collisions at server side. The resource must be properly
    // desposed of, or known to be released before the unlink to
    // prevent leaks.
    Bridge_UnlinkResource,
    Bridge_UnlinkVolumeResource,
    // These are not actually official D3D9 API calls.
    IDirect3DDevice9Ex_LinkSwapchain,
    IDirect3DDevice9Ex_LinkBackBuffer,
    IDirect3DDevice9Ex_LinkAutoDepthStencil,


    IDirect3D9Ex_QueryInterface,
    IDirect3D9Ex_AddRef,
    IDirect3D9Ex_Destroy,
    IDirect3D9Ex_RegisterSoftwareDevice,
    IDirect3D9Ex_GetAdapterCount,
    IDirect3D9Ex_GetAdapterIdentifier,
    IDirect3D9Ex_GetAdapterModeCount,
    IDirect3D9Ex_EnumAdapterModes,
    IDirect3D9Ex_GetAdapterDisplayMode,
    IDirect3D9Ex_CheckDeviceType,
    IDirect3D9Ex_CheckDeviceFormat,
    IDirect3D9Ex_CheckDeviceMultiSampleType,
    IDirect3D9Ex_CheckDepthStencilMatch,
    IDirect3D9Ex_CheckDeviceFormatConversion,
    IDirect3D9Ex_GetDeviceCaps,
    IDirect3D9Ex_GetAdapterMonitor,
    IDirect3D9Ex_CreateDevice,
    IDirect3D9Ex_GetAdapterModeCountEx,
    IDirect3D9Ex_EnumAdapterModesEx,
    IDirect3D9Ex_GetAdapterDisplayModeEx,
    IDirect3D9Ex_CreateDeviceEx,
    IDirect3D9Ex_GetAdapterLUID,


    IDirect3DDevice9Ex_QueryInterface,
    IDirect3DDevice9Ex_AddRef,
    IDirect3DDevice9Ex_Destroy,
    IDirect3DDevice9Ex_TestCooperativeLevel,
    IDirect3DDevice9Ex_GetAvailableTextureMem,
    IDirect3DDevice9Ex_EvictManagedResources,
    IDirect3DDevice9Ex_GetDirect3D,
    IDirect3DDevice9Ex_GetDeviceCaps,
    IDirect3DDevice9Ex_GetDisplayMode,
    IDirect3DDevice9Ex_GetCreationParameters,
    IDirect3DDevice9Ex_SetCursorProperties,
    IDirect3DDevice9Ex_SetCursorPosition,
    IDirect3DDevice9Ex_ShowCursor,
    IDirect3DDevice9Ex_CreateAdditionalSwapChain,
    IDirect3DDevice9Ex_GetSwapChain,
    IDirect3DDevice9Ex_GetNumberOfSwapChains,
    IDirect3DDevice9Ex_Reset,
    IDirect3DDevice9Ex_Present,
    IDirect3DDevice9Ex_GetBackBuffer,
    IDirect3DDevice9Ex_GetRasterStatus,
    IDirect3DDevice9Ex_SetDialogBoxMode,
    IDirect3DDevice9Ex_SetGammaRamp,
    IDirect3DDevice9Ex_GetGammaRamp,
    IDirect3DDevice9Ex_CreateTexture,
    IDirect3DDevice9Ex_CreateVolumeTexture,
    IDirect3DDevice9Ex_CreateCubeTexture,
    IDirect3DDevice9Ex_CreateVertexBuffer,
    IDirect3DDevice9Ex_CreateIndexBuffer,
    IDirect3DDevice9Ex_CreateRenderTarget,
    IDirect3DDevice9Ex_CreateDepthStencilSurface,
    IDirect3DDevice9Ex_UpdateSurface,
    IDirect3DDevice9Ex_UpdateTexture,
    IDirect3DDevice9Ex_GetRenderTargetData,
    IDirect3DDevice9Ex_GetFrontBufferData,
    IDirect3DDevice9Ex_StretchRect,
    IDirect3DDevice9Ex_ColorFill,
    IDirect3DDevice9Ex_CreateOffscreenPlainSurface,
    IDirect3DDevice9Ex_SetRenderTarget,
    IDirect3DDevice9Ex_GetRenderTarget,
    IDirect3DDevice9Ex_SetDepthStencilSurface,
    IDirect3DDevice9Ex_GetDepthStencilSurface,
    IDirect3DDevice9Ex_BeginScene,
    IDirect3DDevice9Ex_EndScene,
    IDirect3DDevice9Ex_Clear,
    IDirect3DDevice9Ex_SetTransform,
    IDirect3DDevice9Ex_GetTransform,
    IDirect3DDevice9Ex_MultiplyTransform,
    IDirect3DDevice9Ex_SetViewport,
    IDirect3DDevice9Ex_GetViewport,
    IDirect3DDevice9Ex_SetMaterial,
    IDirect3DDevice9Ex_GetMaterial,
    IDirect3DDevice9Ex_SetLight,
    IDirect3DDevice9Ex_GetLight,
    IDirect3DDevice9Ex_LightEnable,
    IDirect3DDevice9Ex_GetLightEnable,
    IDirect3DDevice9Ex_SetClipPlane,
    IDirect3DDevice9Ex_GetClipPlane,
    IDirect3DDevice9Ex_SetRenderState,
    IDirect3DDevice9Ex_GetRenderState,
    IDirect3DDevice9Ex_CreateStateBlock,
    IDirect3DDevice9Ex_BeginStateBlock,
    IDirect3DDevice9Ex_EndStateBlock,
    IDirect3DDevice9Ex_SetClipStatus,
    IDirect3DDevice9Ex_GetClipStatus,
    IDirect3DDevice9Ex_GetTexture,
    IDirect3DDevice9Ex_SetTexture,
    IDirect3DDevice9Ex_GetTextureStageState,
    IDirect3DDevice9Ex_SetTextureStageState,
    IDirect3DDevice9Ex_GetSamplerState,
    IDirect3DDevice9Ex_SetSamplerState,
    IDirect3DDevice9Ex_ValidateDevice,
    IDirect3DDevice9Ex_SetPaletteEntries,
    IDirect3DDevice9Ex_GetPaletteEntries,
    IDirect3DDevice9Ex_SetCurrentTexturePalette,
    IDirect3DDevice9Ex_GetCurrentTexturePalette,
    IDirect3DDevice9Ex_SetScissorRect,
    IDirect3DDevice9Ex_GetScissorRect,
    IDirect3DDevice9Ex_SetSoftwareVertexProcessing,
    IDirect3DDevice9Ex_GetSoftwareVertexProcessing,
    IDirect3DDevice9Ex_SetNPatchMode,
    IDirect3DDevice9Ex_GetNPatchMode,
    IDirect3DDevice9Ex_DrawPrimitive,
    IDirect3DDevice9Ex_DrawIndexedPrimitive,
    IDirect3DDevice9Ex_DrawPrimitiveUP,
    IDirect3DDevice9Ex_DrawIndexedPrimitiveUP,
    IDirect3DDevice9Ex_ProcessVertices,
    IDirect3DDevice9Ex_CreateVertexDeclaration,
    IDirect3DDevice9Ex_SetVertexDeclaration,
    IDirect3DDevice9Ex_GetVertexDeclaration,
    IDirect3DDevice9Ex_SetFVF,
    IDirect3DDevice9Ex_GetFVF,
    IDirect3DDevice9Ex_CreateVertexShader,
    IDirect3DDevice9Ex_SetVertexShader,
    IDirect3DDevice9Ex_GetVertexShader,
    IDirect3DDevice9Ex_SetVertexShaderConstantF,
    IDirect3DDevice9Ex_GetVertexShaderConstantF,
    IDirect3DDevice9Ex_SetVertexShaderConstantI,
    IDirect3DDevice9Ex_GetVertexShaderConstantI,
    IDirect3DDevice9Ex_SetVertexShaderConstantB,
    IDirect3DDevice9Ex_GetVertexShaderConstantB,
    IDirect3DDevice9Ex_SetStreamSource,
    IDirect3DDevice9Ex_GetStreamSource,
    IDirect3DDevice9Ex_SetStreamSourceFreq,
    IDirect3DDevice9Ex_GetStreamSourceFreq,
    IDirect3DDevice9Ex_SetIndices,
    IDirect3DDevice9Ex_GetIndices,
    IDirect3DDevice9Ex_CreatePixelShader,
    IDirect3DDevice9Ex_SetPixelShader,
    IDirect3DDevice9Ex_GetPixelShader,
    IDirect3DDevice9Ex_SetPixelShaderConstantF,
    IDirect3DDevice9Ex_GetPixelShaderConstantF,
    IDirect3DDevice9Ex_SetPixelShaderConstantI,
    IDirect3DDevice9Ex_GetPixelShaderConstantI,
    IDirect3DDevice9Ex_SetPixelShaderConstantB,
    IDirect3DDevice9Ex_GetPixelShaderConstantB,
    IDirect3DDevice9Ex_DrawRectPatch,
    IDirect3DDevice9Ex_DrawTriPatch,
    IDirect3DDevice9Ex_DeletePatch,
    IDirect3DDevice9Ex_CreateQuery,
    IDirect3DDevice9Ex_SetConvolutionMonoKernel,
    IDirect3DDevice9Ex_ComposeRects,
    IDirect3DDevice9Ex_PresentEx,
    IDirect3DDevice9Ex_GetGPUThreadPriority,
    IDirect3DDevice9Ex_SetGPUThreadPriority,
    IDirect3DDevice9Ex_WaitForVBlank,
    IDirect3DDevice9Ex_CheckResourceResidency,
    IDirect3DDevice9Ex_SetMaximumFrameLatency,
    IDirect3DDevice9Ex_GetMaximumFrameLatency,
    IDirect3DDevice9Ex_CheckDeviceState,
    IDirect3DDevice9Ex_CreateRenderTargetEx,
    IDirect3DDevice9Ex_CreateOffscreenPlainSurfaceEx,
    IDirect3DDevice9Ex_CreateDepthStencilSurfaceEx,
    IDirect3DDevice9Ex_ResetEx,
    IDirect3DDevice9Ex_GetDisplayModeEx,


    IDirect3DStateBlock9_QueryInterface,
    IDirect3DStateBlock9_AddRef,
    IDirect3DStateBlock9_Destroy,
    IDirect3DStateBlock9_GetDevice,
    IDirect3DStateBlock9_Capture,
    IDirect3DStateBlock9_Apply,


    IDirect3DSwapChain9_QueryInterface,
    IDirect3DSwapChain9_AddRef,
    IDirect3DSwapChain9_Destroy,
    IDirect3DSwapChain9_Present,
    IDirect3DSwapChain9_GetFrontBufferData,
    IDirect3DSwapChain9_GetBackBuffer,
    IDirect3DSwapChain9_GetRasterStatus,
    IDirect3DSwapChain9_GetDisplayMode,
    IDirect3DSwapChain9_GetDevice,
    IDirect3DSwapChain9_GetPresentParameters,


    IDirect3DResource9_QueryInterface,
    IDirect3DResource9_AddRef,
    IDirect3DResource9_Destroy,
    IDirect3DResource9_GetDevice,
    IDirect3DResource9_SetPrivateData,
    IDirect3DResource9_GetPrivateData,
    IDirect3DResource9_FreePrivateData,
    IDirect3DResource9_SetPriority,
    IDirect3DResource9_GetPriority,
    IDirect3DResource9_PreLoad,
    IDirect3DResource9_GetType,


    IDirect3DVertexDeclaration9_QueryInterface,
    IDirect3DVertexDeclaration9_AddRef,
    IDirect3DVertexDeclaration9_Destroy,
    IDirect3DVertexDeclaration9_GetDevice,
    IDirect3DVertexDeclaration9_GetDeclaration,


    IDirect3DVertexShader9_QueryInterface,
    IDirect3DVertexShader9_AddRef,
    IDirect3DVertexShader9_Destroy,
    IDirect3DVertexShader9_GetDevice,
    IDirect3DVertexShader9_GetFunction,


    IDirect3DPixelShader9_QueryInterface,
    IDirect3DPixelShader9_AddRef,
    IDirect3DPixelShader9_Destroy,
    IDirect3DPixelShader9_GetDevice,
    IDirect3DPixelShader9_GetFunction,


    IDirect3DBaseTexture9_QueryInterface,
    IDirect3DBaseTexture9_AddRef,
    IDirect3DBaseTexture9_Destroy,
    IDirect3DBaseTexture9_GetDevice,
    IDirect3DBaseTexture9_SetPrivateData,
    IDirect3DBaseTexture9_GetPrivateData,
    IDirect3DBaseTexture9_FreePrivateData,
    IDirect3DBaseTexture9_SetPriority,
    IDirect3DBaseTexture9_GetPriority,
    IDirect3DBaseTexture9_PreLoad,
    IDirect3DBaseTexture9_GetType,
    IDirect3DBaseTexture9_SetLOD,
    IDirect3DBaseTexture9_GetLOD,
    IDirect3DBaseTexture9_GetLevelCount,
    IDirect3DBaseTexture9_SetAutoGenFilterType,
    IDirect3DBaseTexture9_GetAutoGenFilterType,
    IDirect3DBaseTexture9_GenerateMipSubLevels,


    IDirect3DTexture9_QueryInterface,
    IDirect3DTexture9_AddRef,
    IDirect3DTexture9_Destroy,
    IDirect3DTexture9_GetDevice,
    IDirect3DTexture9_SetPrivateData,
    IDirect3DTexture9_GetPrivateData,
    IDirect3DTexture9_FreePrivateData,
    IDirect3DTexture9_SetPriority,
    IDirect3DTexture9_GetPriority,
    IDirect3DTexture9_PreLoad,
    IDirect3DTexture9_GetType,
    IDirect3DTexture9_SetLOD,
    IDirect3DTexture9_GetLOD,
    IDirect3DTexture9_GetLevelCount,
    IDirect3DTexture9_SetAutoGenFilterType,
    IDirect3DTexture9_GetAutoGenFilterType,
    IDirect3DTexture9_GenerateMipSubLevels,
    IDirect3DTexture9_GetLevelDesc,
    IDirect3DTexture9_GetSurfaceLevel,
    IDirect3DTexture9_LockRect,
    IDirect3DTexture9_UnlockRect,
    IDirect3DTexture9_AddDirtyRect,


    IDirect3DVolumeTexture9_QueryInterface,
    IDirect3DVolumeTexture9_AddRef,
    IDirect3DVolumeTexture9_Destroy,
    IDirect3DVolumeTexture9_GetDevice,
    IDirect3DVolumeTexture9_SetPrivateData,
    IDirect3DVolumeTexture9_GetPrivateData,
    IDirect3DVolumeTexture9_FreePrivateData,
    IDirect3DVolumeTexture9_SetPriority,
    IDirect3DVolumeTexture9_GetPriority,
    IDirect3DVolumeTexture9_PreLoad,
    IDirect3DVolumeTexture9_GetType,
    IDirect3DVolumeTexture9_SetLOD,
    IDirect3DVolumeTexture9_GetLOD,
    IDirect3DVolumeTexture9_GetLevelCount,
    IDirect3DVolumeTexture9_SetAutoGenFilterType,
    IDirect3DVolumeTexture9_GetAutoGenFilterType,
    IDirect3DVolumeTexture9_GenerateMipSubLevels,
    IDirect3DVolumeTexture9_GetLevelDesc,
    IDirect3DVolumeTexture9_GetVolumeLevel,
    IDirect3DVolumeTexture9_LockBox,
    IDirect3DVolumeTexture9_UnlockBox,
    IDirect3DVolumeTexture9_AddDirtyBox,


    IDirect3DCubeTexture9_QueryInterface,
    IDirect3DCubeTexture9_AddRef,
    IDirect3DCubeTexture9_Destroy,
    IDirect3DCubeTexture9_GetDevice,
    IDirect3DCubeTexture9_SetPrivateData,
    IDirect3DCubeTexture9_GetPrivateData,
    IDirect3DCubeTexture9_FreePrivateData,
    IDirect3DCubeTexture9_SetPriority,
    IDirect3DCubeTexture9_GetPriority,
    IDirect3DCubeTexture9_PreLoad,
    IDirect3DCubeTexture9_GetType,
    IDirect3DCubeTexture9_SetLOD,
    IDirect3DCubeTexture9_GetLOD,
    IDirect3DCubeTexture9_GetLevelCount,
    IDirect3DCubeTexture9_SetAutoGenFilterType,
    IDirect3DCubeTexture9_GetAutoGenFilterType,
    IDirect3DCubeTexture9_GenerateMipSubLevels,
    IDirect3DCubeTexture9_GetLevelDesc,
    IDirect3DCubeTexture9_GetCubeMapSurface,
    IDirect3DCubeTexture9_LockRect,
    IDirect3DCubeTexture9_UnlockRect,
    IDirect3DCubeTexture9_AddDirtyRect,


    IDirect3DVertexBuffer9_QueryInterface,
    IDirect3DVertexBuffer9_AddRef,
    IDirect3DVertexBuffer9_Destroy,
    IDirect3DVertexBuffer9_GetDevice,
    IDirect3DVertexBuffer9_SetPrivateData,
    IDirect3DVertexBuffer9_GetPrivateData,
    IDirect3DVertexBuffer9_FreePrivateData,
    IDirect3DVertexBuffer9_SetPriority,
    IDirect3DVertexBuffer9_GetPriority,
    IDirect3DVertexBuffer9_PreLoad,
    IDirect3DVertexBuffer9_GetType,
    IDirect3DVertexBuffer9_Lock,
    IDirect3DVertexBuffer9_Unlock,
    IDirect3DVertexBuffer9_GetDesc,


    IDirect3DIndexBuffer9_QueryInterface,
    IDirect3DIndexBuffer9_AddRef,
    IDirect3DIndexBuffer9_Destroy,
    IDirect3DIndexBuffer9_GetDevice,
    IDirect3DIndexBuffer9_SetPrivateData,
    IDirect3DIndexBuffer9_GetPrivateData,
    IDirect3DIndexBuffer9_FreePrivateData,
    IDirect3DIndexBuffer9_SetPriority,
    IDirect3DIndexBuffer9_GetPriority,
    IDirect3DIndexBuffer9_PreLoad,
    IDirect3DIndexBuffer9_GetType,
    IDirect3DIndexBuffer9_Lock,
    IDirect3DIndexBuffer9_Unlock,
    IDirect3DIndexBuffer9_GetDesc,


    IDirect3DSurface9_QueryInterface,
    IDirect3DSurface9_AddRef,
    IDirect3DSurface9_Destroy,
    IDirect3DSurface9_GetDevice,
    IDirect3DSurface9_SetPrivateData,
    IDirect3DSurface9_GetPrivateData,
    IDirect3DSurface9_FreePrivateData,
    IDirect3DSurface9_SetPriority,
    IDirect3DSurface9_GetPriority,
    IDirect3DSurface9_PreLoad,
    IDirect3DSurface9_GetType,
    IDirect3DSurface9_GetContainer,
    IDirect3DSurface9_GetDesc,
    IDirect3DSurface9_LockRect,
    IDirect3DSurface9_UnlockRect,
    IDirect3DSurface9_GetDC,
    IDirect3DSurface9_ReleaseDC,


    IDirect3DVolume9_QueryInterface,
    IDirect3DVolume9_AddRef,
    IDirect3DVolume9_Destroy,
    IDirect3DVolume9_GetDevice,
    IDirect3DVolume9_SetPrivateData,
    IDirect3DVolume9_GetPrivateData,
    IDirect3DVolume9_FreePrivateData,
    IDirect3DVolume9_GetContainer,
    IDirect3DVolume9_GetDesc,
    IDirect3DVolume9_LockBox,
    IDirect3DVolume9_UnlockBox,


    IDirect3DQuery9_QueryInterface,
    IDirect3DQuery9_AddRef,
    IDirect3DQuery9_Destroy,
    IDirect3DQuery9_GetDevice,
    IDirect3DQuery9_GetType,
    IDirect3DQuery9_GetDataSize,
    IDirect3DQuery9_Issue,
    IDirect3DQuery9_GetData,
  };

  // Maybe this will be useful...  
  enum Type {
    kIDirect3D9 = IDirect3D9Ex_QueryInterface,
    kIDirect3DDevice9 = IDirect3DDevice9Ex_QueryInterface,
    kIDirect3DStateBlock9 = IDirect3DStateBlock9_QueryInterface,
    kIDirect3DSwapChain9 = IDirect3DSwapChain9_QueryInterface,
    kIDirect3DVertexDeclaration9 = IDirect3DVertexDeclaration9_QueryInterface,
    kIDirect3DVertexShader9 = IDirect3DVertexShader9_QueryInterface,
    kIDirect3DPixelShader9 = IDirect3DPixelShader9_QueryInterface,
    kIDirect3DBaseTexture9 = IDirect3DBaseTexture9_QueryInterface,
    kIDirect3DTexture9 = IDirect3DTexture9_QueryInterface,
    kIDirect3DVolumeTexture9 = IDirect3DVolumeTexture9_QueryInterface,
    kIDirect3DCubeTexture9 = IDirect3DCubeTexture9_QueryInterface,
    kIDirect3DVertexBuffer9 = IDirect3DVertexBuffer9_QueryInterface,
    kIDirect3DIndexBuffer9 = IDirect3DIndexBuffer9_QueryInterface,
    kIDirect3DSurface9 = IDirect3DSurface9_QueryInterface,
    kIDirect3DVolume9 = IDirect3DVolume9_QueryInterface,
    kIDirect3DQuery9 = IDirect3DQuery9_QueryInterface
  };

  inline static std::string toString(const D3D9Command& command) {
    switch (command) {
    case Bridge_Terminate: return "Terminate";
    case Bridge_Invalid: return "Invalid";
    case Bridge_Syn: return "Syn";
    case Bridge_Ack: return "Ack";
    case Bridge_Continue: return "Continue";
    case Bridge_Any: return "Any";
    case Bridge_Response: return "Response";
    case Bridge_DebugMessage: return "DebugMessage";

    case RemixApi_CreateMaterial: return "RemixApi_CreateMaterial";
    case RemixApi_DestroyMaterial: return "RemixApi_DestroyMaterial";
    case RemixApi_CreateMesh: return "RemixApi_CreateMesh";
    case RemixApi_DestroyMesh: return "RemixApi_DestroyMesh";
    case RemixApi_DrawInstance: return "RemixApi_DrawInstance";
    case RemixApi_CreateLight: return "RemixApi_CreateLight";
    case RemixApi_DestroyLight: return "RemixApi_DestroyLight";
    case RemixApi_DrawLightInstance: return "DrawLightInstance";
    case RemixApi_SetConfigVariable: return "RemixApi_SetConfigVariable";
    case RemixApi_CreateD3D9: return "RemixApi_CreateD3D9";
    case RemixApi_RegisterDevice: return "RemixApi_RegisterDevice";

    case Bridge_SharedHeap_AddSeg: return "SharedHeap_AddSeg";
    case Bridge_SharedHeap_Alloc: return "SharedHeap_Alloc";
    case Bridge_SharedHeap_Dealloc: return "SharedHeap_Dealloc";
    
    case Bridge_UnlinkResource: return "Bridge_UnlinkResource";
    case Bridge_UnlinkVolumeResource: return "Bridge_UnlinkVolumeResource";

    case IDirect3DDevice9Ex_LinkSwapchain: return "IDirect3DDevice9Ex_LinkSwapchain";
    case IDirect3DDevice9Ex_LinkBackBuffer: return "IDirect3DDevice9Ex_LinkBackBuffer";
    case IDirect3DDevice9Ex_LinkAutoDepthStencil: return "IDirect3DDevice9Ex_LinkAutoDepthStencil";

    case IDirect3D9Ex_QueryInterface: return "IDirect3D9Ex_QueryInterface";
    case IDirect3D9Ex_AddRef: return "IDirect3D9Ex_AddRef";
    case IDirect3D9Ex_Destroy: return "IDirect3D9Ex_Destroy";
    case IDirect3D9Ex_RegisterSoftwareDevice: return "IDirect3D9Ex_RegisterSoftwareDevice";
    case IDirect3D9Ex_GetAdapterCount: return "IDirect3D9Ex_GetAdapterCount";
    case IDirect3D9Ex_GetAdapterIdentifier: return "IDirect3D9Ex_GetAdapterIdentifier";
    case IDirect3D9Ex_GetAdapterModeCount: return "IDirect3D9Ex_GetAdapterModeCount";
    case IDirect3D9Ex_EnumAdapterModes: return "IDirect3D9Ex_EnumAdapterModes";
    case IDirect3D9Ex_GetAdapterDisplayMode: return "IDirect3D9Ex_GetAdapterDisplayMode";
    case IDirect3D9Ex_CheckDeviceType: return "IDirect3D9Ex_CheckDeviceType";
    case IDirect3D9Ex_CheckDeviceFormat: return "IDirect3D9Ex_CheckDeviceFormat";
    case IDirect3D9Ex_CheckDeviceMultiSampleType: return "IDirect3D9Ex_CheckDeviceMultiSampleType";
    case IDirect3D9Ex_CheckDepthStencilMatch: return "IDirect3D9Ex_CheckDepthStencilMatch";
    case IDirect3D9Ex_CheckDeviceFormatConversion: return "IDirect3D9Ex_CheckDeviceFormatConversion";
    case IDirect3D9Ex_GetDeviceCaps: return "IDirect3D9Ex_GetDeviceCaps";
    case IDirect3D9Ex_GetAdapterMonitor: return "IDirect3D9Ex_GetAdapterMonitor";
    case IDirect3D9Ex_CreateDevice: return "IDirect3D9Ex_CreateDevice";
    case IDirect3D9Ex_GetAdapterModeCountEx: return "IDirect3D9Ex_GetAdapterModeCountEx";
    case IDirect3D9Ex_EnumAdapterModesEx: return "IDirect3D9Ex_EnumAdapterModesEx";
    case IDirect3D9Ex_GetAdapterDisplayModeEx: return "IDirect3D9Ex_GetAdapterDisplayModeEx";
    case IDirect3D9Ex_CreateDeviceEx: return "IDirect3D9Ex_CreateDeviceEx";
    case IDirect3D9Ex_GetAdapterLUID: return "IDirect3D9Ex_GetAdapterLUID";

    case IDirect3DDevice9Ex_QueryInterface: return "IDirect3DDevice9Ex_QueryInterface";
    case IDirect3DDevice9Ex_AddRef: return "IDirect3DDevice9Ex_AddRef";
    case IDirect3DDevice9Ex_Destroy: return "IDirect3DDevice9Ex_Destroy";
    case IDirect3DDevice9Ex_TestCooperativeLevel: return "IDirect3DDevice9Ex_TestCooperativeLevel";
    case IDirect3DDevice9Ex_GetAvailableTextureMem: return "IDirect3DDevice9Ex_GetAvailableTextureMem";
    case IDirect3DDevice9Ex_EvictManagedResources: return "IDirect3DDevice9Ex_EvictManagedResources";
    case IDirect3DDevice9Ex_GetDirect3D: return "IDirect3DDevice9Ex_GetDirect3D";
    case IDirect3DDevice9Ex_GetDeviceCaps: return "IDirect3DDevice9Ex_GetDeviceCaps";
    case IDirect3DDevice9Ex_GetDisplayMode: return "IDirect3DDevice9Ex_GetDisplayMode";
    case IDirect3DDevice9Ex_GetCreationParameters: return "IDirect3DDevice9Ex_GetCreationParameters";
    case IDirect3DDevice9Ex_SetCursorProperties: return "IDirect3DDevice9Ex_SetCursorProperties";
    case IDirect3DDevice9Ex_SetCursorPosition: return "IDirect3DDevice9Ex_SetCursorPosition";
    case IDirect3DDevice9Ex_ShowCursor: return "IDirect3DDevice9Ex_ShowCursor";
    case IDirect3DDevice9Ex_CreateAdditionalSwapChain: return "IDirect3DDevice9Ex_CreateAdditionalSwapChain";
    case IDirect3DDevice9Ex_GetSwapChain: return "IDirect3DDevice9Ex_GetSwapChain";
    case IDirect3DDevice9Ex_GetNumberOfSwapChains: return "IDirect3DDevice9Ex_GetNumberOfSwapChains";
    case IDirect3DDevice9Ex_Reset: return "IDirect3DDevice9Ex_Reset";
    case IDirect3DDevice9Ex_Present: return "IDirect3DDevice9Ex_Present";
    case IDirect3DDevice9Ex_GetBackBuffer: return "IDirect3DDevice9Ex_GetBackBuffer";
    case IDirect3DDevice9Ex_GetRasterStatus: return "IDirect3DDevice9Ex_GetRasterStatus";
    case IDirect3DDevice9Ex_SetDialogBoxMode: return "IDirect3DDevice9Ex_SetDialogBoxMode";
    case IDirect3DDevice9Ex_SetGammaRamp: return "IDirect3DDevice9Ex_SetGammaRamp";
    case IDirect3DDevice9Ex_GetGammaRamp: return "IDirect3DDevice9Ex_GetGammaRamp";
    case IDirect3DDevice9Ex_CreateTexture: return "IDirect3DDevice9Ex_CreateTexture";
    case IDirect3DDevice9Ex_CreateVolumeTexture: return "IDirect3DDevice9Ex_CreateVolumeTexture";
    case IDirect3DDevice9Ex_CreateCubeTexture: return "IDirect3DDevice9Ex_CreateCubeTexture";
    case IDirect3DDevice9Ex_CreateVertexBuffer: return "IDirect3DDevice9Ex_CreateVertexBuffer";
    case IDirect3DDevice9Ex_CreateIndexBuffer: return "IDirect3DDevice9Ex_CreateIndexBuffer";
    case IDirect3DDevice9Ex_CreateRenderTarget: return "IDirect3DDevice9Ex_CreateRenderTarget";
    case IDirect3DDevice9Ex_CreateDepthStencilSurface: return "IDirect3DDevice9Ex_CreateDepthStencilSurface";
    case IDirect3DDevice9Ex_UpdateSurface: return "IDirect3DDevice9Ex_UpdateSurface";
    case IDirect3DDevice9Ex_UpdateTexture: return "IDirect3DDevice9Ex_UpdateTexture";
    case IDirect3DDevice9Ex_GetRenderTargetData: return "IDirect3DDevice9Ex_GetRenderTargetData";
    case IDirect3DDevice9Ex_GetFrontBufferData: return "IDirect3DDevice9Ex_GetFrontBufferData";
    case IDirect3DDevice9Ex_StretchRect: return "IDirect3DDevice9Ex_StretchRect";
    case IDirect3DDevice9Ex_ColorFill: return "IDirect3DDevice9Ex_ColorFill";
    case IDirect3DDevice9Ex_CreateOffscreenPlainSurface: return "IDirect3DDevice9Ex_CreateOffscreenPlainSurface";
    case IDirect3DDevice9Ex_SetRenderTarget: return "IDirect3DDevice9Ex_SetRenderTarget";
    case IDirect3DDevice9Ex_GetRenderTarget: return "IDirect3DDevice9Ex_GetRenderTarget";
    case IDirect3DDevice9Ex_SetDepthStencilSurface: return "IDirect3DDevice9Ex_SetDepthStencilSurface";
    case IDirect3DDevice9Ex_GetDepthStencilSurface: return "IDirect3DDevice9Ex_GetDepthStencilSurface";
    case IDirect3DDevice9Ex_BeginScene: return "IDirect3DDevice9Ex_BeginScene";
    case IDirect3DDevice9Ex_EndScene: return "IDirect3DDevice9Ex_EndScene";
    case IDirect3DDevice9Ex_Clear: return "IDirect3DDevice9Ex_Clear";
    case IDirect3DDevice9Ex_SetTransform: return "IDirect3DDevice9Ex_SetTransform";
    case IDirect3DDevice9Ex_GetTransform: return "IDirect3DDevice9Ex_GetTransform";
    case IDirect3DDevice9Ex_MultiplyTransform: return "IDirect3DDevice9Ex_MultiplyTransform";
    case IDirect3DDevice9Ex_SetViewport: return "IDirect3DDevice9Ex_SetViewport";
    case IDirect3DDevice9Ex_GetViewport: return "IDirect3DDevice9Ex_GetViewport";
    case IDirect3DDevice9Ex_SetMaterial: return "IDirect3DDevice9Ex_SetMaterial";
    case IDirect3DDevice9Ex_GetMaterial: return "IDirect3DDevice9Ex_GetMaterial";
    case IDirect3DDevice9Ex_SetLight: return "IDirect3DDevice9Ex_SetLight";
    case IDirect3DDevice9Ex_GetLight: return "IDirect3DDevice9Ex_GetLight";
    case IDirect3DDevice9Ex_LightEnable: return "IDirect3DDevice9Ex_LightEnable";
    case IDirect3DDevice9Ex_GetLightEnable: return "IDirect3DDevice9Ex_GetLightEnable";
    case IDirect3DDevice9Ex_SetClipPlane: return "IDirect3DDevice9Ex_SetClipPlane";
    case IDirect3DDevice9Ex_GetClipPlane: return "IDirect3DDevice9Ex_GetClipPlane";
    case IDirect3DDevice9Ex_SetRenderState: return "IDirect3DDevice9Ex_SetRenderState";
    case IDirect3DDevice9Ex_GetRenderState: return "IDirect3DDevice9Ex_GetRenderState";
    case IDirect3DDevice9Ex_CreateStateBlock: return "IDirect3DDevice9Ex_CreateStateBlock";
    case IDirect3DDevice9Ex_BeginStateBlock: return "IDirect3DDevice9Ex_BeginStateBlock";
    case IDirect3DDevice9Ex_EndStateBlock: return "IDirect3DDevice9Ex_EndStateBlock";
    case IDirect3DDevice9Ex_SetClipStatus: return "IDirect3DDevice9Ex_SetClipStatus";
    case IDirect3DDevice9Ex_GetClipStatus: return "IDirect3DDevice9Ex_GetClipStatus";
    case IDirect3DDevice9Ex_GetTexture: return "IDirect3DDevice9Ex_GetTexture";
    case IDirect3DDevice9Ex_SetTexture: return "IDirect3DDevice9Ex_SetTexture";
    case IDirect3DDevice9Ex_GetTextureStageState: return "IDirect3DDevice9Ex_GetTextureStageState";
    case IDirect3DDevice9Ex_SetTextureStageState: return "IDirect3DDevice9Ex_SetTextureStageState";
    case IDirect3DDevice9Ex_GetSamplerState: return "IDirect3DDevice9Ex_GetSamplerState";
    case IDirect3DDevice9Ex_SetSamplerState: return "IDirect3DDevice9Ex_SetSamplerState";
    case IDirect3DDevice9Ex_ValidateDevice: return "IDirect3DDevice9Ex_ValidateDevice";
    case IDirect3DDevice9Ex_SetPaletteEntries: return "IDirect3DDevice9Ex_SetPaletteEntries";
    case IDirect3DDevice9Ex_GetPaletteEntries: return "IDirect3DDevice9Ex_GetPaletteEntries";
    case IDirect3DDevice9Ex_SetCurrentTexturePalette: return "IDirect3DDevice9Ex_SetCurrentTexturePalette";
    case IDirect3DDevice9Ex_GetCurrentTexturePalette: return "IDirect3DDevice9Ex_GetCurrentTexturePalette";
    case IDirect3DDevice9Ex_SetScissorRect: return "IDirect3DDevice9Ex_SetScissorRect";
    case IDirect3DDevice9Ex_GetScissorRect: return "IDirect3DDevice9Ex_GetScissorRect";
    case IDirect3DDevice9Ex_SetSoftwareVertexProcessing: return "IDirect3DDevice9Ex_SetSoftwareVertexProcessing";
    case IDirect3DDevice9Ex_GetSoftwareVertexProcessing: return "IDirect3DDevice9Ex_GetSoftwareVertexProcessing";
    case IDirect3DDevice9Ex_SetNPatchMode: return "IDirect3DDevice9Ex_SetNPatchMode";
    case IDirect3DDevice9Ex_GetNPatchMode: return "IDirect3DDevice9Ex_GetNPatchMode";
    case IDirect3DDevice9Ex_DrawPrimitive: return "IDirect3DDevice9Ex_DrawPrimitive";
    case IDirect3DDevice9Ex_DrawIndexedPrimitive: return "IDirect3DDevice9Ex_DrawIndexedPrimitive";
    case IDirect3DDevice9Ex_DrawPrimitiveUP: return "IDirect3DDevice9Ex_DrawPrimitiveUP";
    case IDirect3DDevice9Ex_DrawIndexedPrimitiveUP: return "IDirect3DDevice9Ex_DrawIndexedPrimitiveUP";
    case IDirect3DDevice9Ex_ProcessVertices: return "IDirect3DDevice9Ex_ProcessVertices";
    case IDirect3DDevice9Ex_CreateVertexDeclaration: return "IDirect3DDevice9Ex_CreateVertexDeclaration";
    case IDirect3DDevice9Ex_SetVertexDeclaration: return "IDirect3DDevice9Ex_SetVertexDeclaration";
    case IDirect3DDevice9Ex_GetVertexDeclaration: return "IDirect3DDevice9Ex_GetVertexDeclaration";
    case IDirect3DDevice9Ex_SetFVF: return "IDirect3DDevice9Ex_SetFVF";
    case IDirect3DDevice9Ex_GetFVF: return "IDirect3DDevice9Ex_GetFVF";
    case IDirect3DDevice9Ex_CreateVertexShader: return "IDirect3DDevice9Ex_CreateVertexShader";
    case IDirect3DDevice9Ex_SetVertexShader: return "IDirect3DDevice9Ex_SetVertexShader";
    case IDirect3DDevice9Ex_GetVertexShader: return "IDirect3DDevice9Ex_GetVertexShader";
    case IDirect3DDevice9Ex_SetVertexShaderConstantF: return "IDirect3DDevice9Ex_SetVertexShaderConstantF";
    case IDirect3DDevice9Ex_GetVertexShaderConstantF: return "IDirect3DDevice9Ex_GetVertexShaderConstantF";
    case IDirect3DDevice9Ex_SetVertexShaderConstantI: return "IDirect3DDevice9Ex_SetVertexShaderConstantI";
    case IDirect3DDevice9Ex_GetVertexShaderConstantI: return "IDirect3DDevice9Ex_GetVertexShaderConstantI";
    case IDirect3DDevice9Ex_SetVertexShaderConstantB: return "IDirect3DDevice9Ex_SetVertexShaderConstantB";
    case IDirect3DDevice9Ex_GetVertexShaderConstantB: return "IDirect3DDevice9Ex_GetVertexShaderConstantB";
    case IDirect3DDevice9Ex_SetStreamSource: return "IDirect3DDevice9Ex_SetStreamSource";
    case IDirect3DDevice9Ex_GetStreamSource: return "IDirect3DDevice9Ex_GetStreamSource";
    case IDirect3DDevice9Ex_SetStreamSourceFreq: return "IDirect3DDevice9Ex_SetStreamSourceFreq";
    case IDirect3DDevice9Ex_GetStreamSourceFreq: return "IDirect3DDevice9Ex_GetStreamSourceFreq";
    case IDirect3DDevice9Ex_SetIndices: return "IDirect3DDevice9Ex_SetIndices";
    case IDirect3DDevice9Ex_GetIndices: return "IDirect3DDevice9Ex_GetIndices";
    case IDirect3DDevice9Ex_CreatePixelShader: return "IDirect3DDevice9Ex_CreatePixelShader";
    case IDirect3DDevice9Ex_SetPixelShader: return "IDirect3DDevice9Ex_SetPixelShader";
    case IDirect3DDevice9Ex_GetPixelShader: return "IDirect3DDevice9Ex_GetPixelShader";
    case IDirect3DDevice9Ex_SetPixelShaderConstantF: return "IDirect3DDevice9Ex_SetPixelShaderConstantF";
    case IDirect3DDevice9Ex_GetPixelShaderConstantF: return "IDirect3DDevice9Ex_GetPixelShaderConstantF";
    case IDirect3DDevice9Ex_SetPixelShaderConstantI: return "IDirect3DDevice9Ex_SetPixelShaderConstantI";
    case IDirect3DDevice9Ex_GetPixelShaderConstantI: return "IDirect3DDevice9Ex_GetPixelShaderConstantI";
    case IDirect3DDevice9Ex_SetPixelShaderConstantB: return "IDirect3DDevice9Ex_SetPixelShaderConstantB";
    case IDirect3DDevice9Ex_GetPixelShaderConstantB: return "IDirect3DDevice9Ex_GetPixelShaderConstantB";
    case IDirect3DDevice9Ex_DrawRectPatch: return "IDirect3DDevice9Ex_DrawRectPatch";
    case IDirect3DDevice9Ex_DrawTriPatch: return "IDirect3DDevice9Ex_DrawTriPatch";
    case IDirect3DDevice9Ex_DeletePatch: return "IDirect3DDevice9Ex_DeletePatch";
    case IDirect3DDevice9Ex_CreateQuery: return "IDirect3DDevice9Ex_CreateQuery";
    case IDirect3DDevice9Ex_SetConvolutionMonoKernel: return "IDirect3DDevice9Ex_SetConvolutionMonoKernel";
    case IDirect3DDevice9Ex_ComposeRects: return "IDirect3DDevice9Ex_ComposeRects";
    case IDirect3DDevice9Ex_PresentEx: return "IDirect3DDevice9Ex_PresentEx";
    case IDirect3DDevice9Ex_GetGPUThreadPriority: return "IDirect3DDevice9Ex_GetGPUThreadPriority";
    case IDirect3DDevice9Ex_SetGPUThreadPriority: return "IDirect3DDevice9Ex_SetGPUThreadPriority";
    case IDirect3DDevice9Ex_WaitForVBlank: return "IDirect3DDevice9Ex_WaitForVBlank";
    case IDirect3DDevice9Ex_CheckResourceResidency: return "IDirect3DDevice9Ex_CheckResourceResidency";
    case IDirect3DDevice9Ex_SetMaximumFrameLatency: return "IDirect3DDevice9Ex_SetMaximumFrameLatency";
    case IDirect3DDevice9Ex_GetMaximumFrameLatency: return "IDirect3DDevice9Ex_GetMaximumFrameLatency";
    case IDirect3DDevice9Ex_CheckDeviceState: return "IDirect3DDevice9Ex_CheckDeviceState";
    case IDirect3DDevice9Ex_CreateRenderTargetEx: return "IDirect3DDevice9Ex_CreateRenderTargetEx";
    case IDirect3DDevice9Ex_CreateOffscreenPlainSurfaceEx: return "IDirect3DDevice9Ex_CreateOffscreenPlainSurfaceEx";
    case IDirect3DDevice9Ex_CreateDepthStencilSurfaceEx: return "IDirect3DDevice9Ex_CreateDepthStencilSurfaceEx";
    case IDirect3DDevice9Ex_ResetEx: return "IDirect3DDevice9Ex_ResetEx";
    case IDirect3DDevice9Ex_GetDisplayModeEx: return "IDirect3DDevice9Ex_GetDisplayModeEx";

    case IDirect3DStateBlock9_QueryInterface: return "IDirect3DStateBlock9_QueryInterface";
    case IDirect3DStateBlock9_AddRef: return "IDirect3DStateBlock9_AddRef";
    case IDirect3DStateBlock9_Destroy: return "IDirect3DStateBlock9_Destroy";
    case IDirect3DStateBlock9_GetDevice: return "IDirect3DStateBlock9_GetDevice";
    case IDirect3DStateBlock9_Capture: return "IDirect3DStateBlock9_Capture";
    case IDirect3DStateBlock9_Apply: return "IDirect3DStateBlock9_Apply";

    case IDirect3DSwapChain9_QueryInterface: return "IDirect3DSwapChain9_QueryInterface";
    case IDirect3DSwapChain9_AddRef: return "IDirect3DSwapChain9_AddRef";
    case IDirect3DSwapChain9_Destroy: return "IDirect3DSwapChain9_Destroy";
    case IDirect3DSwapChain9_Present: return "IDirect3DSwapChain9_Present";
    case IDirect3DSwapChain9_GetFrontBufferData: return "IDirect3DSwapChain9_GetFrontBufferData";
    case IDirect3DSwapChain9_GetBackBuffer: return "IDirect3DSwapChain9_GetBackBuffer";
    case IDirect3DSwapChain9_GetRasterStatus: return "IDirect3DSwapChain9_GetRasterStatus";
    case IDirect3DSwapChain9_GetDisplayMode: return "IDirect3DSwapChain9_GetDisplayMode";
    case IDirect3DSwapChain9_GetDevice: return "IDirect3DSwapChain9_GetDevice";
    case IDirect3DSwapChain9_GetPresentParameters: return "IDirect3DSwapChain9_GetPresentParameters";

    case IDirect3DResource9_QueryInterface: return "IDirect3DResource9_QueryInterface";
    case IDirect3DResource9_AddRef: return "IDirect3DResource9_AddRef";
    case IDirect3DResource9_Destroy: return "IDirect3DResource9_Destroy";
    case IDirect3DResource9_GetDevice: return "IDirect3DResource9_GetDevice";
    case IDirect3DResource9_SetPrivateData: return "IDirect3DResource9_SetPrivateData";
    case IDirect3DResource9_GetPrivateData: return "IDirect3DResource9_GetPrivateData";
    case IDirect3DResource9_FreePrivateData: return "IDirect3DResource9_FreePrivateData";
    case IDirect3DResource9_SetPriority: return "IDirect3DResource9_SetPriority";
    case IDirect3DResource9_GetPriority: return "IDirect3DResource9_GetPriority";
    case IDirect3DResource9_PreLoad: return "IDirect3DResource9_PreLoad";
    case IDirect3DResource9_GetType: return "IDirect3DResource9_GetType";

    case IDirect3DVertexDeclaration9_QueryInterface: return "IDirect3DVertexDeclaration9_QueryInterface";
    case IDirect3DVertexDeclaration9_AddRef: return "IDirect3DVertexDeclaration9_AddRef";
    case IDirect3DVertexDeclaration9_Destroy: return "IDirect3DVertexDeclaration9_Destroy";
    case IDirect3DVertexDeclaration9_GetDevice: return "IDirect3DVertexDeclaration9_GetDevice";
    case IDirect3DVertexDeclaration9_GetDeclaration: return "IDirect3DVertexDeclaration9_GetDeclaration";

    case IDirect3DVertexShader9_QueryInterface: return "IDirect3DVertexShader9_QueryInterface";
    case IDirect3DVertexShader9_AddRef: return "IDirect3DVertexShader9_AddRef";
    case IDirect3DVertexShader9_Destroy: return "IDirect3DVertexShader9_Destroy";
    case IDirect3DVertexShader9_GetDevice: return "IDirect3DVertexShader9_GetDevice";
    case IDirect3DVertexShader9_GetFunction: return "IDirect3DVertexShader9_GetFunction";

    case IDirect3DPixelShader9_QueryInterface: return "IDirect3DPixelShader9_QueryInterface";
    case IDirect3DPixelShader9_AddRef: return "IDirect3DPixelShader9_AddRef";
    case IDirect3DPixelShader9_Destroy: return "IDirect3DPixelShader9_Destroy";
    case IDirect3DPixelShader9_GetDevice: return "IDirect3DPixelShader9_GetDevice";
    case IDirect3DPixelShader9_GetFunction: return "IDirect3DPixelShader9_GetFunction";

    case IDirect3DBaseTexture9_QueryInterface: return "IDirect3DBaseTexture9_QueryInterface";
    case IDirect3DBaseTexture9_AddRef: return "IDirect3DBaseTexture9_AddRef";
    case IDirect3DBaseTexture9_Destroy: return "IDirect3DBaseTexture9_Destroy";
    case IDirect3DBaseTexture9_GetDevice: return "IDirect3DBaseTexture9_GetDevice";
    case IDirect3DBaseTexture9_SetPrivateData: return "IDirect3DBaseTexture9_SetPrivateData";
    case IDirect3DBaseTexture9_GetPrivateData: return "IDirect3DBaseTexture9_GetPrivateData";
    case IDirect3DBaseTexture9_FreePrivateData: return "IDirect3DBaseTexture9_FreePrivateData";
    case IDirect3DBaseTexture9_SetPriority: return "IDirect3DBaseTexture9_SetPriority";
    case IDirect3DBaseTexture9_GetPriority: return "IDirect3DBaseTexture9_GetPriority";
    case IDirect3DBaseTexture9_PreLoad: return "IDirect3DBaseTexture9_PreLoad";
    case IDirect3DBaseTexture9_GetType: return "IDirect3DBaseTexture9_GetType";
    case IDirect3DBaseTexture9_SetLOD: return "IDirect3DBaseTexture9_SetLOD";
    case IDirect3DBaseTexture9_GetLOD: return "IDirect3DBaseTexture9_GetLOD";
    case IDirect3DBaseTexture9_GetLevelCount: return "IDirect3DBaseTexture9_GetLevelCount";
    case IDirect3DBaseTexture9_SetAutoGenFilterType: return "IDirect3DBaseTexture9_SetAutoGenFilterType";
    case IDirect3DBaseTexture9_GetAutoGenFilterType: return "IDirect3DBaseTexture9_GetAutoGenFilterType";
    case IDirect3DBaseTexture9_GenerateMipSubLevels: return "IDirect3DBaseTexture9_GenerateMipSubLevels";

    case IDirect3DTexture9_QueryInterface: return "IDirect3DTexture9_QueryInterface";
    case IDirect3DTexture9_AddRef: return "IDirect3DTexture9_AddRef";
    case IDirect3DTexture9_Destroy: return "IDirect3DTexture9_Destroy";
    case IDirect3DTexture9_GetDevice: return "IDirect3DTexture9_GetDevice";
    case IDirect3DTexture9_SetPrivateData: return "IDirect3DTexture9_SetPrivateData";
    case IDirect3DTexture9_GetPrivateData: return "IDirect3DTexture9_GetPrivateData";
    case IDirect3DTexture9_FreePrivateData: return "IDirect3DTexture9_FreePrivateData";
    case IDirect3DTexture9_SetPriority: return "IDirect3DTexture9_SetPriority";
    case IDirect3DTexture9_GetPriority: return "IDirect3DTexture9_GetPriority";
    case IDirect3DTexture9_PreLoad: return "IDirect3DTexture9_PreLoad";
    case IDirect3DTexture9_GetType: return "IDirect3DTexture9_GetType";
    case IDirect3DTexture9_SetLOD: return "IDirect3DTexture9_SetLOD";
    case IDirect3DTexture9_GetLOD: return "IDirect3DTexture9_GetLOD";
    case IDirect3DTexture9_GetLevelCount: return "IDirect3DTexture9_GetLevelCount";
    case IDirect3DTexture9_SetAutoGenFilterType: return "IDirect3DTexture9_SetAutoGenFilterType";
    case IDirect3DTexture9_GetAutoGenFilterType: return "IDirect3DTexture9_GetAutoGenFilterType";
    case IDirect3DTexture9_GenerateMipSubLevels: return "IDirect3DTexture9_GenerateMipSubLevels";
    case IDirect3DTexture9_GetLevelDesc: return "IDirect3DTexture9_GetLevelDesc";
    case IDirect3DTexture9_GetSurfaceLevel: return "IDirect3DTexture9_GetSurfaceLevel";
    case IDirect3DTexture9_LockRect: return "IDirect3DTexture9_LockRect";
    case IDirect3DTexture9_UnlockRect: return "IDirect3DTexture9_UnlockRect";
    case IDirect3DTexture9_AddDirtyRect: return "IDirect3DTexture9_AddDirtyRect";

    case IDirect3DVolumeTexture9_QueryInterface: return "IDirect3DVolumeTexture9_QueryInterface";
    case IDirect3DVolumeTexture9_AddRef: return "IDirect3DVolumeTexture9_AddRef";
    case IDirect3DVolumeTexture9_Destroy: return "IDirect3DVolumeTexture9_Destroy";
    case IDirect3DVolumeTexture9_GetDevice: return "IDirect3DVolumeTexture9_GetDevice";
    case IDirect3DVolumeTexture9_SetPrivateData: return "IDirect3DVolumeTexture9_SetPrivateData";
    case IDirect3DVolumeTexture9_GetPrivateData: return "IDirect3DVolumeTexture9_GetPrivateData";
    case IDirect3DVolumeTexture9_FreePrivateData: return "IDirect3DVolumeTexture9_FreePrivateData";
    case IDirect3DVolumeTexture9_SetPriority: return "IDirect3DVolumeTexture9_SetPriority";
    case IDirect3DVolumeTexture9_GetPriority: return "IDirect3DVolumeTexture9_GetPriority";
    case IDirect3DVolumeTexture9_PreLoad: return "IDirect3DVolumeTexture9_PreLoad";
    case IDirect3DVolumeTexture9_GetType: return "IDirect3DVolumeTexture9_GetType";
    case IDirect3DVolumeTexture9_SetLOD: return "IDirect3DVolumeTexture9_SetLOD";
    case IDirect3DVolumeTexture9_GetLOD: return "IDirect3DVolumeTexture9_GetLOD";
    case IDirect3DVolumeTexture9_GetLevelCount: return "IDirect3DVolumeTexture9_GetLevelCount";
    case IDirect3DVolumeTexture9_SetAutoGenFilterType: return "IDirect3DVolumeTexture9_SetAutoGenFilterType";
    case IDirect3DVolumeTexture9_GetAutoGenFilterType: return "IDirect3DVolumeTexture9_GetAutoGenFilterType";
    case IDirect3DVolumeTexture9_GenerateMipSubLevels: return "IDirect3DVolumeTexture9_GenerateMipSubLevels";
    case IDirect3DVolumeTexture9_GetLevelDesc: return "IDirect3DVolumeTexture9_GetLevelDesc";
    case IDirect3DVolumeTexture9_GetVolumeLevel: return "IDirect3DVolumeTexture9_GetVolumeLevel";
    case IDirect3DVolumeTexture9_LockBox: return "IDirect3DVolumeTexture9_LockBox";
    case IDirect3DVolumeTexture9_UnlockBox: return "IDirect3DVolumeTexture9_UnlockBox";
    case IDirect3DVolumeTexture9_AddDirtyBox: return "IDirect3DVolumeTexture9_AddDirtyBox";

    case IDirect3DCubeTexture9_QueryInterface: return "IDirect3DCubeTexture9_QueryInterface";
    case IDirect3DCubeTexture9_AddRef: return "IDirect3DCubeTexture9_AddRef";
    case IDirect3DCubeTexture9_Destroy: return "IDirect3DCubeTexture9_Destroy";
    case IDirect3DCubeTexture9_GetDevice: return "IDirect3DCubeTexture9_GetDevice";
    case IDirect3DCubeTexture9_SetPrivateData: return "IDirect3DCubeTexture9_SetPrivateData";
    case IDirect3DCubeTexture9_GetPrivateData: return "IDirect3DCubeTexture9_GetPrivateData";
    case IDirect3DCubeTexture9_FreePrivateData: return "IDirect3DCubeTexture9_FreePrivateData";
    case IDirect3DCubeTexture9_SetPriority: return "IDirect3DCubeTexture9_SetPriority";
    case IDirect3DCubeTexture9_GetPriority: return "IDirect3DCubeTexture9_GetPriority";
    case IDirect3DCubeTexture9_PreLoad: return "IDirect3DCubeTexture9_PreLoad";
    case IDirect3DCubeTexture9_GetType: return "IDirect3DCubeTexture9_GetType";
    case IDirect3DCubeTexture9_SetLOD: return "IDirect3DCubeTexture9_SetLOD";
    case IDirect3DCubeTexture9_GetLOD: return "IDirect3DCubeTexture9_GetLOD";
    case IDirect3DCubeTexture9_GetLevelCount: return "IDirect3DCubeTexture9_GetLevelCount";
    case IDirect3DCubeTexture9_SetAutoGenFilterType: return "IDirect3DCubeTexture9_SetAutoGenFilterType";
    case IDirect3DCubeTexture9_GetAutoGenFilterType: return "IDirect3DCubeTexture9_GetAutoGenFilterType";
    case IDirect3DCubeTexture9_GenerateMipSubLevels: return "IDirect3DCubeTexture9_GenerateMipSubLevels";
    case IDirect3DCubeTexture9_GetLevelDesc: return "IDirect3DCubeTexture9_GetLevelDesc";
    case IDirect3DCubeTexture9_GetCubeMapSurface: return "IDirect3DCubeTexture9_GetCubeMapSurface";
    case IDirect3DCubeTexture9_LockRect: return "IDirect3DCubeTexture9_LockRect";
    case IDirect3DCubeTexture9_UnlockRect: return "IDirect3DCubeTexture9_UnlockRect";
    case IDirect3DCubeTexture9_AddDirtyRect: return "IDirect3DCubeTexture9_AddDirtyRect";

    case IDirect3DVertexBuffer9_QueryInterface: return "IDirect3DVertexBuffer9_QueryInterface";
    case IDirect3DVertexBuffer9_AddRef: return "IDirect3DVertexBuffer9_AddRef";
    case IDirect3DVertexBuffer9_Destroy: return "IDirect3DVertexBuffer9_Destroy";
    case IDirect3DVertexBuffer9_GetDevice: return "IDirect3DVertexBuffer9_GetDevice";
    case IDirect3DVertexBuffer9_SetPrivateData: return "IDirect3DVertexBuffer9_SetPrivateData";
    case IDirect3DVertexBuffer9_GetPrivateData: return "IDirect3DVertexBuffer9_GetPrivateData";
    case IDirect3DVertexBuffer9_FreePrivateData: return "IDirect3DVertexBuffer9_FreePrivateData";
    case IDirect3DVertexBuffer9_SetPriority: return "IDirect3DVertexBuffer9_SetPriority";
    case IDirect3DVertexBuffer9_GetPriority: return "IDirect3DVertexBuffer9_GetPriority";
    case IDirect3DVertexBuffer9_PreLoad: return "IDirect3DVertexBuffer9_PreLoad";
    case IDirect3DVertexBuffer9_GetType: return "IDirect3DVertexBuffer9_GetType";
    case IDirect3DVertexBuffer9_Lock: return "IDirect3DVertexBuffer9_Lock";
    case IDirect3DVertexBuffer9_Unlock: return "IDirect3DVertexBuffer9_Unlock";
    case IDirect3DVertexBuffer9_GetDesc: return "IDirect3DVertexBuffer9_GetDesc";

    case IDirect3DIndexBuffer9_QueryInterface: return "IDirect3DIndexBuffer9_QueryInterface";
    case IDirect3DIndexBuffer9_AddRef: return "IDirect3DIndexBuffer9_AddRef";
    case IDirect3DIndexBuffer9_Destroy: return "IDirect3DIndexBuffer9_Destroy";
    case IDirect3DIndexBuffer9_GetDevice: return "IDirect3DIndexBuffer9_GetDevice";
    case IDirect3DIndexBuffer9_SetPrivateData: return "IDirect3DIndexBuffer9_SetPrivateData";
    case IDirect3DIndexBuffer9_GetPrivateData: return "IDirect3DIndexBuffer9_GetPrivateData";
    case IDirect3DIndexBuffer9_FreePrivateData: return "IDirect3DIndexBuffer9_FreePrivateData";
    case IDirect3DIndexBuffer9_SetPriority: return "IDirect3DIndexBuffer9_SetPriority";
    case IDirect3DIndexBuffer9_GetPriority: return "IDirect3DIndexBuffer9_GetPriority";
    case IDirect3DIndexBuffer9_PreLoad: return "IDirect3DIndexBuffer9_PreLoad";
    case IDirect3DIndexBuffer9_GetType: return "IDirect3DIndexBuffer9_GetType";
    case IDirect3DIndexBuffer9_Lock: return "IDirect3DIndexBuffer9_Lock";
    case IDirect3DIndexBuffer9_Unlock: return "IDirect3DIndexBuffer9_Unlock";
    case IDirect3DIndexBuffer9_GetDesc: return "IDirect3DIndexBuffer9_GetDesc";

    case IDirect3DSurface9_QueryInterface: return "IDirect3DSurface9_QueryInterface";
    case IDirect3DSurface9_AddRef: return "IDirect3DSurface9_AddRef";
    case IDirect3DSurface9_Destroy: return "IDirect3DSurface9_Destroy";
    case IDirect3DSurface9_GetDevice: return "IDirect3DSurface9_GetDevice";
    case IDirect3DSurface9_SetPrivateData: return "IDirect3DSurface9_SetPrivateData";
    case IDirect3DSurface9_GetPrivateData: return "IDirect3DSurface9_GetPrivateData";
    case IDirect3DSurface9_FreePrivateData: return "IDirect3DSurface9_FreePrivateData";
    case IDirect3DSurface9_SetPriority: return "IDirect3DSurface9_SetPriority";
    case IDirect3DSurface9_GetPriority: return "IDirect3DSurface9_GetPriority";
    case IDirect3DSurface9_PreLoad: return "IDirect3DSurface9_PreLoad";
    case IDirect3DSurface9_GetType: return "IDirect3DSurface9_GetType";
    case IDirect3DSurface9_GetContainer: return "IDirect3DSurface9_GetContainer";
    case IDirect3DSurface9_GetDesc: return "IDirect3DSurface9_GetDesc";
    case IDirect3DSurface9_LockRect: return "IDirect3DSurface9_LockRect";
    case IDirect3DSurface9_UnlockRect: return "IDirect3DSurface9_UnlockRect";
    case IDirect3DSurface9_GetDC: return "IDirect3DSurface9_GetDC";
    case IDirect3DSurface9_ReleaseDC: return "IDirect3DSurface9_ReleaseDC";

    case IDirect3DVolume9_QueryInterface: return "IDirect3DVolume9_QueryInterface";
    case IDirect3DVolume9_AddRef: return "IDirect3DVolume9_AddRef";
    case IDirect3DVolume9_Destroy: return "IDirect3DVolume9_Destroy";
    case IDirect3DVolume9_GetDevice: return "IDirect3DVolume9_GetDevice";
    case IDirect3DVolume9_SetPrivateData: return "IDirect3DVolume9_SetPrivateData";
    case IDirect3DVolume9_GetPrivateData: return "IDirect3DVolume9_GetPrivateData";
    case IDirect3DVolume9_FreePrivateData: return "IDirect3DVolume9_FreePrivateData";
    case IDirect3DVolume9_GetContainer: return "IDirect3DVolume9_GetContainer";
    case IDirect3DVolume9_GetDesc: return "IDirect3DVolume9_GetDesc";
    case IDirect3DVolume9_LockBox: return "IDirect3DVolume9_LockBox";
    case IDirect3DVolume9_UnlockBox: return "IDirect3DVolume9_UnlockBox";

    case IDirect3DQuery9_QueryInterface: return "IDirect3DQuery9_QueryInterface";
    case IDirect3DQuery9_AddRef: return "IDirect3DQuery9_AddRef";
    case IDirect3DQuery9_Destroy: return "IDirect3DQuery9_Destroy";
    case IDirect3DQuery9_GetDevice: return "IDirect3DQuery9_GetDevice";
    case IDirect3DQuery9_GetType: return "IDirect3DQuery9_GetType";
    case IDirect3DQuery9_GetDataSize: return "IDirect3DQuery9_GetDataSize";
    case IDirect3DQuery9_Issue: return "IDirect3DQuery9_Issue";
    case IDirect3DQuery9_GetData: return "IDirect3DQuery9_GetData";

    default: return "Unknown Command";
    }
  }

  typedef uint16_t Flags;

  enum FlagBits: Flags {
    DataInSharedHeap = 0b00000001,  // Any data a command operates with is stored in shared heap
                                    // and only allocation id(s) is transferred on the queue
    DataIsReserved   = 0b00000010,  // Data was already reserved in data queue and only its
                                    // offset is transferred
  };

  inline bool IsDataInSharedHeap(Flags flags) {
    return (flags & FlagBits::DataInSharedHeap) != 0;
  }

  inline bool IsDataReserved(Flags flags) {
    return (flags & FlagBits::DataIsReserved) != 0;
  }
}

struct Header {
  Commands::D3D9Command command = Commands::Bridge_Invalid; // Named function
  Commands::Flags flags = 0; // Command flags
  uint32_t dataOffset = 0;   // Current data queue position value to ensure client and server are in sync
  uint32_t pHandle = 0;      // Handle for client side resource invoking the command, which we map to matching resource on server side
};

#endif // UTIL_COMMANDS_H_
