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
#pragma once

#include "util_common.h"
#include "util_scopedlock.h"

#include "d3d9.h"
#include "base.h"
#include "shadow_map.h"

#include <array>

class Direct3D9Ex_LSS;
class Direct3DSwapChain9_LSS;
class Direct3DStateBlock9_LSS;
class Direct3DSurface9_LSS;

class BaseDirect3DDevice9Ex_LSS: public D3DBase<IDirect3DDevice9Ex>
#ifdef WITH_MULTITHREADED_DEVICE
  , public bridge_util::Syncable
#endif
{
  using D3DBase<IDirect3DDevice9Ex>::D3DBase;
public:
  static constexpr uint32_t kNumRenderStates = 256;
  static constexpr size_t kNumStageSamplers = caps::MaxTexturesPS + caps::MaxTexturesVS + 1;
  static constexpr size_t kMaxTexStageStateTypes = 18;
  static constexpr size_t kMaxStageSamplerStateTypes = D3DSAMP_DMAPOFFSET + 1;
  static constexpr uint32_t NumControlPoints = 256;

  /*
  * Some of the following lines are adapted from source in the DXVK repo
  * at https://github.com/doitsujin/dxvk/blob/master/src/d3d9/d3d9_swapchain.cpp
  */
  static uint16_t MapGammaControlPoint(float x) {
    if (x < 0.0f) x = 0.0f;
    if (x > 1.0f) x = 1.0f;
    return uint16_t(65535.0f * x);
  }

  const D3DDEVICE_CREATION_PARAMETERS& getCreateParams() const {
    return m_createParams;
  }

  const D3DPRESENT_PARAMETERS& getPreviousPresentParameter() const {
    return m_previousPresentParams;
  }

  struct ShaderConstants {
    template<typename T>
    struct Vec4 {
      T data[4];
    };
    static_assert(sizeof(float) * 4 == sizeof(Vec4<float>));
    static_assert(sizeof(int) * 4 == sizeof(Vec4<int>));

    struct VertexConstants {
      Vec4<float> fConsts[caps::MaxFloatConstantsSoftware];
      Vec4<int>   iConsts[caps::MaxOtherConstantsSoftware];
      uint32_t    bConsts[caps::MaxOtherConstantsSoftware / 32];
    };
    struct PixelConstants {
      Vec4<float> fConsts[caps::MaxFloatConstantsPS];
      Vec4<int>   iConsts[caps::MaxOtherConstants];
      uint32_t    bConsts[std::max(caps::MaxOtherConstants / 32, 1U)];
    };

    enum class ShaderType {
      Vertex,
      Pixel,
      kCount
    };
    enum class ConstantType {
      Float,
      Int,
      Bool
    };
    struct ConstantLayout {
      uint32_t floatCount;
      uint32_t intCount;
      uint32_t boolCount;
    };
    inline static const ConstantLayout kVtxConstLayout{caps::MaxFloatConstantsVS,
                                                       caps::MaxOtherConstants,
                                                       caps::MaxOtherConstants};
    inline static const ConstantLayout kPxlConstLayout{caps::MaxFloatConstantsPS,
                                                       caps::MaxOtherConstants,
                                                       caps::MaxOtherConstants};
    template <ShaderType   ShaderT,
              ConstantType ConstT>
    inline static uint32_t getHardwareRegCount() {
      const auto& layout = ShaderT == ShaderType::Vertex
        ? kVtxConstLayout : kPxlConstLayout;
      switch (ConstT) {
      default:
      case ConstantType::Float: return layout.floatCount;
      case ConstantType::Int:   return layout.intCount;
      case ConstantType::Bool:  return layout.boolCount;
      }
    }
    template <ShaderType   ShaderT,
              ConstantType ConstT>
    inline static constexpr uint32_t getSoftwareRegCount() {
      constexpr bool isVS = ShaderT == ShaderType::Vertex;
      switch (ConstT) {
      default:
      case ConstantType::Float: return isVS ? caps::MaxFloatConstantsSoftware : caps::MaxFloatConstantsPS;
      case ConstantType::Int:   return isVS ? caps::MaxOtherConstantsSoftware : caps::MaxOtherConstants;
      case ConstantType::Bool:  return isVS ? caps::MaxOtherConstantsSoftware : caps::MaxOtherConstants;
      }
    }
  };

  friend class Direct3DStateBlock9_LSS;

protected:
  BaseDirect3DDevice9Ex_LSS(const bool bExtended,
                            Direct3D9Ex_LSS* const pDirect3D,
                            const D3DDEVICE_CREATION_PARAMETERS& createParams,
                            const D3DPRESENT_PARAMETERS& presParams,
                            const D3DDISPLAYMODEEX* const pFullscreenDisplayMode,
                            HRESULT& hresultOut);

  HWND getFocusHwnd() const { return m_createParams.hFocusWindow; }
  HWND getPresentationHwnd() const { return m_presParams.hDeviceWindow; }
  HWND getWinProcHwnd() const { return getPresentationHwnd() ? getPresentationHwnd() : getFocusHwnd(); }

  void InitRamp();
  
  using ShaderType = ShaderConstants::ShaderType;
  using ConstantType = ShaderConstants::ConstantType;

  template <ShaderType   ShaderT,
            ConstantType ConstantT,
            typename     T>
  HRESULT setShaderConstants(const uint32_t startRegister,
                             const T* const pConstantData,
                             const uint32_t count);
  template <ShaderType   ShaderT,
            ConstantType ConstantT,
            typename     T>
  HRESULT getShaderConstants(const uint32_t startRegister,
                                   T* const pConstantData,
                             const uint32_t count);
  template <ShaderType   ShaderT,
            ConstantType ConstantT,
            typename     T>
  std::tuple<HRESULT, size_t> commonGetSetConstants(const uint32_t startRegister,
                                                    const T* const pConstantData,
                                                    const uint32_t count);

  // Implicitly created Device objects
  size_t m_implicitRefCnt = 0;
  Direct3DSwapChain9_LSS* m_pSwapchain = nullptr;
  Direct3DSurface9_LSS* m_pImplicitRenderTarget = nullptr;
  Direct3DSurface9_LSS* m_pImplicitDepthStencil = nullptr;

  // Constant members
  const bool m_ex;
  Direct3D9Ex_LSS* const m_pDirect3D = nullptr;
  const D3DDEVICE_CREATION_PARAMETERS m_createParams;
  D3DPRESENT_PARAMETERS m_presParams;

  D3DGAMMARAMP m_gammaRamp;
  D3DPRESENT_PARAMETERS m_previousPresentParams;
  std::unordered_map<UINT, PALETTEENTRY> m_paletteEntries;
  UINT m_curTexPalette;
  BOOL m_bSoftwareVtxProcessing;
  D3DCLIPSTATUS9 m_clipStatus;
  float m_NPatchMode;
  DWORD m_FVF;
  INT m_gpuThreadPriority;
  UINT m_maxFrameLatency;

  struct StateCaptureDirtyFlags {
    // Vertex Decl
    bool vertexDecl;
    // Indices
    bool indices;
    // Render State
    std::array<bool, kNumRenderStates> renderStates = { false };
    // Sampler States
    std::array<std::array<bool, kMaxStageSamplerStateTypes>, kNumStageSamplers> samplerStates;
    // Streams
    std::array<bool, caps::MaxStreams> streams;
    std::array<bool, caps::MaxStreams> streamOffsetsAndStrides;
    std::array<bool, caps::MaxStreams> streamFreqs;
    // Textures
    std::array<bool, kNumStageSamplers> textures;
    // Vertex Shader
    bool vertexShader;
    // Pixel Shader
    bool pixelShader;
    // Material
    bool material;
    // Lights
    std::unordered_map<DWORD, bool> lights;
    // Light Enables
    std::unordered_map<DWORD, bool> bLightEnables;
    // Transforms
    std::array<bool, caps::MaxTransforms> transforms;
    // Texture Stage State
    using TextureStateArray = std::array<bool, kMaxTexStageStateTypes>;
    std::array<TextureStateArray, kNumStageSamplers> textureStageStates;
    // Viewport
    bool viewport;
    // Scissor Rect
    bool scissorRect;
    std::array<bool, caps::MaxClipPlanes> clipPlanes;
    // Pixel Shader Constants
    struct VertexConstants {
      std::array<bool, caps::MaxFloatConstantsSoftware> fConsts;
      std::array<bool, caps::MaxOtherConstantsSoftware> iConsts;
      std::array<bool, caps::MaxOtherConstantsSoftware> bConsts;
    } vertexConstants;
    struct PixelConstants {
      std::array<bool, caps::MaxFloatConstantsPS> fConsts;
      std::array<bool, caps::MaxOtherConstants> iConsts;
      std::array<bool, caps::MaxOtherConstants> bConsts;
    } pixelConstants;
  };

  struct State {
    // Render Targets
    std::array<D3DAutoPtr, caps::MaxSimultaneousRenderTargets> renderTargets;
    // Depth Stencil Surface
    D3DAutoPtr depthStencil;
    // Transforms
    std::array<D3DMATRIX, caps::MaxTransforms> transforms;
    // Viewport
    D3DVIEWPORT9 viewport;
    // Material
    D3DMATERIAL9 material;
    // Lights
    std::unordered_map<DWORD, D3DLIGHT9> lights;
    // Light Enables
    std::unordered_map<DWORD, bool> bLightEnables;
    // Clip Plane
    std::array<float[4], caps::MaxClipPlanes> clipPlanes;
    // Render State
    std::array<DWORD, kNumRenderStates> renderStates;
    // Textures
    std::array<D3DAutoPtr, kNumStageSamplers> textures;
    std::array<D3DRESOURCETYPE, kNumStageSamplers> textureTypes;
    // Texture Stage State
    using TextureStateArray = std::array<DWORD, kMaxTexStageStateTypes>;
    std::array<TextureStateArray, kNumStageSamplers> textureStageStates;
    // Sampler States
    using SamplerStateArray = std::array<DWORD, kMaxStageSamplerStateTypes>;
    std::array<SamplerStateArray, kNumStageSamplers> samplerStates;
    // Scissor Rect
    RECT scissorRect;
    // Vertex Decl
    D3DAutoPtr vertexDecl;
    // Vertex Shader
    D3DAutoPtr vertexShader;
    // Vertex Shader Constants
    ShaderConstants::VertexConstants vertexConstants;
    // Streams
    std::array<D3DAutoPtr, caps::MaxStreams> streams;
    std::array<UINT, caps::MaxStreams> streamFreqs;
    std::array<UINT, caps::MaxStreams> streamOffsets;
    std::array<UINT, caps::MaxStreams> streamStrides;
    // Vertex Buffers
    // Indices
    D3DAutoPtr indices;
    // Pixel Shader
    D3DAutoPtr pixelShader;
    // Pixel Shader Constants
    ShaderConstants::PixelConstants pixelConstants;
  };

  State m_state;
  Direct3DStateBlock9_LSS* m_stateRecording = nullptr;
};