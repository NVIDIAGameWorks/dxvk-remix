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
#pragma once

#include "d3d9_device_base.h"

#include "util_common.h"
#include "util_scopedlock.h"

#include <type_traits>

template<bool EnableSync>
class Direct3DDevice9Ex_LSS: public BaseDirect3DDevice9Ex_LSS {

#ifdef WITH_MULTITHREADED_DEVICE
  // Using a std::recursive_mutex at the moment. On a 3GHz Threadripper system measured
  // ~22ns for lock/unlock sequence with no contention.
  typedef std::conditional_t<EnableSync, std::recursive_mutex, bridge_util::nop_sync> LockType;

  // TODO: The lock is global because currently the bridge and the transport queues
  // are NOT thread-safe.This mutex can be made device-local or completely removed
  // when the bridge has been made thread-safe.
  inline static LockType s_globalLock;

public:
  void lock() override {
    lockImpl();
  }
  void unlock() override {
    unlockImpl();
  }
  void lockImpl() {
    s_globalLock.lock();
  }
  void unlockImpl() {
    s_globalLock.unlock();
  }
#endif

protected:
  // Releases the internal objects, if any.
  void releaseInternalObjects(bool resetState = true);
  void onDestroy() override;

public:
  Direct3DDevice9Ex_LSS(const bool bExtended,
                        Direct3D9Ex_LSS* const pDirect3D,
                        const D3DDEVICE_CREATION_PARAMETERS& createParams,
                        const D3DPRESENT_PARAMETERS& presParams,
                        const D3DDISPLAYMODEEX* const pFullscreenDisplayMode,
                        HRESULT& hresultOut)
    : BaseDirect3DDevice9Ex_LSS(bExtended, pDirect3D, createParams, presParams, pFullscreenDisplayMode, hresultOut) {
    if (FAILED(hresultOut)) {
      return;
    }
    initImplicitObjects(presParams);
    ResetState();

    if (!(createParams.BehaviorFlags & D3DCREATE_FPU_PRESERVE)) {
      setupFPU();
    }

    // These are immutable so collect them once and re-use
    internalGetDeviceCaps(&m_caps);
  }

  void StateBlockSetVertexCaptureFlags(BaseDirect3DDevice9Ex_LSS::StateCaptureDirtyFlags& flags);
  void StateBlockSetPixelCaptureFlags(BaseDirect3DDevice9Ex_LSS::StateCaptureDirtyFlags& flags);
  void StateBlockSetCaptureFlags(D3DSTATEBLOCKTYPE Type, BaseDirect3DDevice9Ex_LSS::StateCaptureDirtyFlags& flags);

  /*** IUnknown methods ***/
  STDMETHOD(QueryInterface)(THIS_ REFIID riid, void** ppvObj);
  STDMETHOD_(ULONG, AddRef)(THIS);
  STDMETHOD_(ULONG, Release)(THIS);

  /*** IDirect3DDevice9 methods ***/
  STDMETHOD(TestCooperativeLevel)(THIS);
  STDMETHOD_(UINT, GetAvailableTextureMem)(THIS);
  STDMETHOD(EvictManagedResources)(THIS);
  STDMETHOD(GetDirect3D)(THIS_ IDirect3D9** ppD3D9);
  STDMETHOD(GetDeviceCaps)(THIS_ D3DCAPS9* pCaps);
  STDMETHOD(GetDisplayMode)(THIS_ UINT iSwapChain, D3DDISPLAYMODE* pMode);
  STDMETHOD(GetCreationParameters)(THIS_ D3DDEVICE_CREATION_PARAMETERS* pParameters);
  STDMETHOD(SetCursorProperties)(THIS_ UINT XHotSpot, UINT YHotSpot, IDirect3DSurface9* pCursorBitmap);
  STDMETHOD_(void, SetCursorPosition)(THIS_ int X, int Y, DWORD Flags);
  STDMETHOD_(BOOL, ShowCursor)(THIS_ BOOL bShow);
  STDMETHOD(CreateAdditionalSwapChain)(THIS_ D3DPRESENT_PARAMETERS* pPresentationParameters, IDirect3DSwapChain9** pSwapChain);
  STDMETHOD(GetSwapChain)(THIS_ UINT iSwapChain, IDirect3DSwapChain9** pSwapChain);
  STDMETHOD_(UINT, GetNumberOfSwapChains)(THIS);
  STDMETHOD(Reset)(THIS_ D3DPRESENT_PARAMETERS* pPresentationParameters);
  STDMETHOD(Present)(THIS_ CONST RECT* pSourceRect, CONST RECT* pDestRect, HWND hDestWindowOverride, CONST RGNDATA* pDirtyRegion);
  STDMETHOD(GetBackBuffer)(THIS_ UINT iSwapChain, UINT iBackBuffer, D3DBACKBUFFER_TYPE Type, IDirect3DSurface9** ppBackBuffer);
  STDMETHOD(GetRasterStatus)(THIS_ UINT iSwapChain, D3DRASTER_STATUS* pRasterStatus);
  STDMETHOD(SetDialogBoxMode)(THIS_ BOOL bEnableDialogs);
  STDMETHOD_(void, SetGammaRamp)(THIS_ UINT iSwapChain, DWORD Flags, CONST D3DGAMMARAMP* pRamp);
  STDMETHOD_(void, GetGammaRamp)(THIS_ UINT iSwapChain, D3DGAMMARAMP* pRamp);
  STDMETHOD(CreateTexture)(THIS_ UINT Width, UINT Height, UINT Levels, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool, IDirect3DTexture9** ppTexture, HANDLE* pSharedHandle);
  STDMETHOD(CreateVolumeTexture)(THIS_ UINT Width, UINT Height, UINT Depth, UINT Levels, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool, IDirect3DVolumeTexture9** ppVolumeTexture, HANDLE* pSharedHandle);
  STDMETHOD(CreateCubeTexture)(THIS_ UINT EdgeLength, UINT Levels, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool, IDirect3DCubeTexture9** ppCubeTexture, HANDLE* pSharedHandle);
  STDMETHOD(CreateVertexBuffer)(THIS_ UINT Length, DWORD Usage, DWORD FVF, D3DPOOL Pool, IDirect3DVertexBuffer9** ppVertexBuffer, HANDLE* pSharedHandle);
  STDMETHOD(CreateIndexBuffer)(THIS_ UINT Length, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool, IDirect3DIndexBuffer9** ppIndexBuffer, HANDLE* pSharedHandle);
  STDMETHOD(CreateRenderTarget)(THIS_ UINT Width, UINT Height, D3DFORMAT Format, D3DMULTISAMPLE_TYPE MultiSample, DWORD MultisampleQuality, BOOL Lockable, IDirect3DSurface9** ppSurface, HANDLE* pSharedHandle);
  STDMETHOD(CreateDepthStencilSurface)(THIS_ UINT Width, UINT Height, D3DFORMAT Format, D3DMULTISAMPLE_TYPE MultiSample, DWORD MultisampleQuality, BOOL Discard, IDirect3DSurface9** ppSurface, HANDLE* pSharedHandle);
  STDMETHOD(UpdateSurface)(THIS_ IDirect3DSurface9* pSourceSurface, CONST RECT* pSourceRect, IDirect3DSurface9* pDestinationSurface, CONST POINT* pDestPoint);
  STDMETHOD(UpdateTexture)(THIS_ IDirect3DBaseTexture9* pSourceTexture, IDirect3DBaseTexture9* pDestinationTexture);
  STDMETHOD(GetRenderTargetData)(THIS_ IDirect3DSurface9* pRenderTarget, IDirect3DSurface9* pDestSurface);
  STDMETHOD(GetFrontBufferData)(THIS_ UINT iSwapChain, IDirect3DSurface9* pDestSurface);
  STDMETHOD(StretchRect)(THIS_ IDirect3DSurface9* pSourceSurface, CONST RECT* pSourceRect, IDirect3DSurface9* pDestSurface, CONST RECT* pDestRect, D3DTEXTUREFILTERTYPE Filter);
  STDMETHOD(ColorFill)(THIS_ IDirect3DSurface9* pSurface, CONST RECT* pRect, D3DCOLOR color);
  STDMETHOD(CreateOffscreenPlainSurface)(THIS_ UINT Width, UINT Height, D3DFORMAT Format, D3DPOOL Pool, IDirect3DSurface9** ppSurface, HANDLE* pSharedHandle);
  STDMETHOD(SetRenderTarget)(THIS_ DWORD RenderTargetIndex, IDirect3DSurface9* pRenderTarget);
  STDMETHOD(GetRenderTarget)(THIS_ DWORD RenderTargetIndex, IDirect3DSurface9** ppRenderTarget);
  STDMETHOD(SetDepthStencilSurface)(THIS_ IDirect3DSurface9* pNewZStencil);
  STDMETHOD(GetDepthStencilSurface)(THIS_ IDirect3DSurface9** ppZStencilSurface);
  STDMETHOD(BeginScene)(THIS);
  STDMETHOD(EndScene)(THIS);
  STDMETHOD(Clear)(THIS_ DWORD Count, CONST D3DRECT* pRects, DWORD Flags, D3DCOLOR Color, float Z, DWORD Stencil);
  STDMETHOD(SetTransform)(THIS_ D3DTRANSFORMSTATETYPE State, CONST D3DMATRIX* pMatrix);
  STDMETHOD(GetTransform)(THIS_ D3DTRANSFORMSTATETYPE State, D3DMATRIX* pMatrix);
  STDMETHOD(MultiplyTransform)(THIS_ D3DTRANSFORMSTATETYPE, CONST D3DMATRIX*);
  STDMETHOD(SetViewport)(THIS_ CONST D3DVIEWPORT9* pViewport);
  STDMETHOD(GetViewport)(THIS_ D3DVIEWPORT9* pViewport);
  STDMETHOD(SetMaterial)(THIS_ CONST D3DMATERIAL9* pMaterial);
  STDMETHOD(GetMaterial)(THIS_ D3DMATERIAL9* pMaterial);
  STDMETHOD(SetLight)(THIS_ DWORD Index, CONST D3DLIGHT9*);
  STDMETHOD(GetLight)(THIS_ DWORD Index, D3DLIGHT9*);
  STDMETHOD(LightEnable)(THIS_ DWORD Index, BOOL Enable);
  STDMETHOD(GetLightEnable)(THIS_ DWORD Index, BOOL* pEnable);
  STDMETHOD(SetClipPlane)(THIS_ DWORD Index, CONST float* pPlane);
  STDMETHOD(GetClipPlane)(THIS_ DWORD Index, float* pPlane);
  STDMETHOD(SetRenderState)(THIS_ D3DRENDERSTATETYPE State, DWORD Value);
  STDMETHOD(GetRenderState)(THIS_ D3DRENDERSTATETYPE State, DWORD* pValue);
  STDMETHOD(CreateStateBlock)(THIS_ D3DSTATEBLOCKTYPE Type, IDirect3DStateBlock9** ppSB);
  STDMETHOD(BeginStateBlock)(THIS);
  STDMETHOD(EndStateBlock)(THIS_ IDirect3DStateBlock9** ppSB);
  STDMETHOD(SetClipStatus)(THIS_ CONST D3DCLIPSTATUS9* pClipStatus);
  STDMETHOD(GetClipStatus)(THIS_ D3DCLIPSTATUS9* pClipStatus);
  STDMETHOD(GetTexture)(THIS_ DWORD Stage, IDirect3DBaseTexture9** ppTexture);
  STDMETHOD(SetTexture)(THIS_ DWORD Stage, IDirect3DBaseTexture9* pTexture);
  STDMETHOD(GetTextureStageState)(THIS_ DWORD Stage, D3DTEXTURESTAGESTATETYPE Type, DWORD* pValue);
  STDMETHOD(SetTextureStageState)(THIS_ DWORD Stage, D3DTEXTURESTAGESTATETYPE Type, DWORD Value);
  STDMETHOD(GetSamplerState)(THIS_ DWORD Sampler, D3DSAMPLERSTATETYPE Type, DWORD* pValue);
  STDMETHOD(SetSamplerState)(THIS_ DWORD Sampler, D3DSAMPLERSTATETYPE Type, DWORD Value);
  STDMETHOD(ValidateDevice)(THIS_ DWORD* pNumPasses);
  STDMETHOD(SetPaletteEntries)(THIS_ UINT PaletteNumber, CONST PALETTEENTRY* pEntries);
  STDMETHOD(GetPaletteEntries)(THIS_ UINT PaletteNumber, PALETTEENTRY* pEntries);
  STDMETHOD(SetCurrentTexturePalette)(THIS_ UINT PaletteNumber);
  STDMETHOD(GetCurrentTexturePalette)(THIS_ UINT* PaletteNumber);
  STDMETHOD(SetScissorRect)(THIS_ CONST RECT* pRect);
  STDMETHOD(GetScissorRect)(THIS_ RECT* pRect);
  STDMETHOD(SetSoftwareVertexProcessing)(THIS_ BOOL bSoftware);
  STDMETHOD_(BOOL, GetSoftwareVertexProcessing)(THIS);
  STDMETHOD(SetNPatchMode)(THIS_ float nSegments);
  STDMETHOD_(float, GetNPatchMode)(THIS);
  STDMETHOD(DrawPrimitive)(THIS_ D3DPRIMITIVETYPE PrimitiveType, UINT StartVertex, UINT PrimitiveCount);
  STDMETHOD(DrawIndexedPrimitive)(THIS_ D3DPRIMITIVETYPE, INT BaseVertexIndex, UINT MinVertexIndex, UINT NumVertices, UINT startIndex, UINT primCount);
  STDMETHOD(DrawPrimitiveUP)(THIS_ D3DPRIMITIVETYPE PrimitiveType, UINT PrimitiveCount, CONST void* pVertexStreamZeroData, UINT VertexStreamZeroStride);
  STDMETHOD(DrawIndexedPrimitiveUP)(THIS_ D3DPRIMITIVETYPE PrimitiveType, UINT MinVertexIndex, UINT NumVertices, UINT PrimitiveCount, CONST void* pIndexData, D3DFORMAT IndexDataFormat, CONST void* pVertexStreamZeroData, UINT VertexStreamZeroStride);
  STDMETHOD(ProcessVertices)(THIS_ UINT SrcStartIndex, UINT DestIndex, UINT VertexCount, IDirect3DVertexBuffer9* pDestBuffer, IDirect3DVertexDeclaration9* pVertexDecl, DWORD Flags);
  STDMETHOD(CreateVertexDeclaration)(THIS_ CONST D3DVERTEXELEMENT9* pVertexElements, IDirect3DVertexDeclaration9** ppDecl);
  STDMETHOD(SetVertexDeclaration)(THIS_ IDirect3DVertexDeclaration9* pDecl);
  STDMETHOD(GetVertexDeclaration)(THIS_ IDirect3DVertexDeclaration9** ppDecl);
  STDMETHOD(SetFVF)(THIS_ DWORD FVF);
  STDMETHOD(GetFVF)(THIS_ DWORD* pFVF);
  STDMETHOD(CreateVertexShader)(THIS_ CONST DWORD* pFunction, IDirect3DVertexShader9** ppShader);
  STDMETHOD(SetVertexShader)(THIS_ IDirect3DVertexShader9* pShader);
  STDMETHOD(GetVertexShader)(THIS_ IDirect3DVertexShader9** ppShader);
  STDMETHOD(SetVertexShaderConstantF)(THIS_ UINT StartRegister, CONST float* pConstantData, UINT Vector4fCount);
  STDMETHOD(GetVertexShaderConstantF)(THIS_ UINT StartRegister, float* pConstantData, UINT Vector4fCount);
  STDMETHOD(SetVertexShaderConstantI)(THIS_ UINT StartRegister, CONST int* pConstantData, UINT Vector4iCount);
  STDMETHOD(GetVertexShaderConstantI)(THIS_ UINT StartRegister, int* pConstantData, UINT Vector4iCount);
  STDMETHOD(SetVertexShaderConstantB)(THIS_ UINT StartRegister, CONST BOOL* pConstantData, UINT  BoolCount);
  STDMETHOD(GetVertexShaderConstantB)(THIS_ UINT StartRegister, BOOL* pConstantData, UINT BoolCount);
  STDMETHOD(SetStreamSource)(THIS_ UINT StreamNumber, IDirect3DVertexBuffer9* pStreamData, UINT OffsetInBytes, UINT Stride);
  STDMETHOD(GetStreamSource)(THIS_ UINT StreamNumber, IDirect3DVertexBuffer9** ppStreamData, UINT* pOffsetInBytes, UINT* pStride);
  STDMETHOD(SetStreamSourceFreq)(THIS_ UINT StreamNumber, UINT Setting);
  STDMETHOD(GetStreamSourceFreq)(THIS_ UINT StreamNumber, UINT* pSetting);
  STDMETHOD(SetIndices)(THIS_ IDirect3DIndexBuffer9* pIndexData);
  STDMETHOD(GetIndices)(THIS_ IDirect3DIndexBuffer9** ppIndexData);
  STDMETHOD(CreatePixelShader)(THIS_ CONST DWORD* pFunction, IDirect3DPixelShader9** ppShader);
  STDMETHOD(SetPixelShader)(THIS_ IDirect3DPixelShader9* pShader);
  STDMETHOD(GetPixelShader)(THIS_ IDirect3DPixelShader9** ppShader);
  STDMETHOD(SetPixelShaderConstantF)(THIS_ UINT StartRegister, CONST float* pConstantData, UINT Vector4fCount);
  STDMETHOD(GetPixelShaderConstantF)(THIS_ UINT StartRegister, float* pConstantData, UINT Vector4fCount);
  STDMETHOD(SetPixelShaderConstantI)(THIS_ UINT StartRegister, CONST int* pConstantData, UINT Vector4iCount);
  STDMETHOD(GetPixelShaderConstantI)(THIS_ UINT StartRegister, int* pConstantData, UINT Vector4iCount);
  STDMETHOD(SetPixelShaderConstantB)(THIS_ UINT StartRegister, CONST BOOL* pConstantData, UINT  BoolCount);
  STDMETHOD(GetPixelShaderConstantB)(THIS_ UINT StartRegister, BOOL* pConstantData, UINT BoolCount);
  STDMETHOD(DrawRectPatch)(THIS_ UINT Handle, CONST float* pNumSegs, CONST D3DRECTPATCH_INFO* pRectPatchInfo);
  STDMETHOD(DrawTriPatch)(THIS_ UINT Handle, CONST float* pNumSegs, CONST D3DTRIPATCH_INFO* pTriPatchInfo);
  STDMETHOD(DeletePatch)(THIS_ UINT Handle);
  STDMETHOD(CreateQuery)(THIS_ D3DQUERYTYPE Type, IDirect3DQuery9** ppQuery);
  STDMETHOD(SetConvolutionMonoKernel)(THIS_ UINT width, UINT height, float* rows, float* columns);
  STDMETHOD(ComposeRects)(THIS_ IDirect3DSurface9* pSrc, IDirect3DSurface9* pDst, IDirect3DVertexBuffer9* pSrcRectDescs, UINT NumRects, IDirect3DVertexBuffer9* pDstRectDescs, D3DCOMPOSERECTSOP Operation, int Xoffset, int Yoffset);
  STDMETHOD(PresentEx)(THIS_ CONST RECT* pSourceRect, CONST RECT* pDestRect, HWND hDestWindowOverride, CONST RGNDATA* pDirtyRegion, DWORD dwFlags);
  STDMETHOD(GetGPUThreadPriority)(THIS_ INT* pPriority);
  STDMETHOD(SetGPUThreadPriority)(THIS_ INT Priority);
  STDMETHOD(WaitForVBlank)(THIS_ UINT iSwapChain);
  STDMETHOD(CheckResourceResidency)(THIS_ IDirect3DResource9** pResourceArray, UINT32 NumResources);
  STDMETHOD(SetMaximumFrameLatency)(THIS_ UINT MaxLatency);
  STDMETHOD(GetMaximumFrameLatency)(THIS_ UINT* pMaxLatency);
  STDMETHOD(CheckDeviceState)(THIS_ HWND hDestinationWindow);
  STDMETHOD(CreateRenderTargetEx)(THIS_ UINT Width, UINT Height, D3DFORMAT Format, D3DMULTISAMPLE_TYPE MultiSample, DWORD MultisampleQuality, BOOL Lockable, IDirect3DSurface9** ppSurface, HANDLE* pSharedHandle, DWORD Usage);
  STDMETHOD(CreateOffscreenPlainSurfaceEx)(THIS_ UINT Width, UINT Height, D3DFORMAT Format, D3DPOOL Pool, IDirect3DSurface9** ppSurface, HANDLE* pSharedHandle, DWORD Usage);
  STDMETHOD(CreateDepthStencilSurfaceEx)(THIS_ UINT Width, UINT Height, D3DFORMAT Format, D3DMULTISAMPLE_TYPE MultiSample, DWORD MultisampleQuality, BOOL Discard, IDirect3DSurface9** ppSurface, HANDLE* pSharedHandle, DWORD Usage);
  STDMETHOD(ResetEx)(THIS_ D3DPRESENT_PARAMETERS* pPresentationParameters, D3DDISPLAYMODEEX* pFullscreenDisplayMode);
  STDMETHOD(GetDisplayModeEx)(THIS_ UINT iSwapChain, D3DDISPLAYMODEEX* pMode, D3DDISPLAYROTATION* pRotation);
  STDMETHOD(ResetState)();

private:
  bool m_bIsDestroying = false;

  D3DCAPS9 m_caps;
  HRESULT internalGetDeviceCaps(D3DCAPS9* pCaps);

  void initImplicitObjects(const D3DPRESENT_PARAMETERS& presParam);
  void initImplicitSwapchain(const D3DPRESENT_PARAMETERS& presParam);
  void initImplicitRenderTarget();
  void initImplicitDepthStencil();
  void destroyImplicitObjects();
  template<typename T>
  HRESULT UpdateTextureImpl(IDirect3DBaseTexture9* pSourceTexture, IDirect3DBaseTexture9* pDestinationTexture);
  void setupFPU();
};

