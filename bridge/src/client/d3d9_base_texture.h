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
#pragma once

#include "util_common.h"
#include "util_scopedlock.h"

#include "d3d9_resource.h"
#include "d3d9_surface.h"
#include "util_devicecommand.h"

#include <d3d9.h>

struct TEXTURE_DESC {
  UINT Width;
  UINT Height;
  UINT Depth;
  UINT Levels;
  DWORD Usage;
  D3DFORMAT Format;
  D3DPOOL Pool;
};

/*
 * IDirect3DBaseTexture9 LSS Interceptor Class
 */
template<typename TextureType, typename LevelType>
class Direct3DBaseTexture9_LSS: public Direct3DContainer9_LSS<Direct3DResource9_LSS<TextureType>, LevelType> {
  DWORD m_lod = 0;
  D3DTEXTUREFILTERTYPE m_mipFilter = D3DTEXF_LINEAR;

  TEXTURE_DESC m_desc;

public:
  Direct3DBaseTexture9_LSS(BaseDirect3DDevice9Ex_LSS* const pDevice, const TEXTURE_DESC& desc)
    : Direct3DContainer9_LSS(nullptr, pDevice)
    , m_desc(desc) {
    // Sanitize
    m_desc.Width = std::max(m_desc.Width, 1u);
    m_desc.Height = std::max(m_desc.Height, 1u);
    m_desc.Depth = std::max(m_desc.Depth, 1u);
    m_desc.Levels = std::max(m_desc.Levels, 1u);
  }

  const TEXTURE_DESC& getDesc() const {
    return m_desc;
  }

  /*** IUnknown methods ***/
  STDMETHOD(QueryInterface)(THIS_ REFIID riid, void** ppvObj) {
    LogFunctionCall();

    if (ppvObj == nullptr) {
      return E_POINTER;
    }

    *ppvObj = nullptr;

    if (riid == __uuidof(IUnknown)
     || riid == __uuidof(IDirect3DResource9)
     || riid == __uuidof(IDirect3DBaseTexture9)
     || riid == __uuidof(TextureType)) {
      *ppvObj = bridge_cast<TextureType*>(this);
      AddRef();
      return S_OK;
    }

    return E_NOINTERFACE;
  }

  /*** IDirect3DBaseTexture9 methods ***/
  STDMETHOD_(DWORD, SetLOD)(THIS_ DWORD LODNew) {
    LogFunctionCall();

    DWORD oldLod;
    {
      BRIDGE_PARENT_DEVICE_LOCKGUARD();
      oldLod = m_lod;
      m_lod = LODNew;
    }
    if (oldLod != LODNew) {
      ClientMessage c(Commands::IDirect3DBaseTexture9_SetLOD, getId());
      c.send_data(LODNew);
    }

    return oldLod;
  }

  STDMETHOD_(DWORD, GetLOD)(THIS) {
    LogFunctionCall();
    BRIDGE_PARENT_DEVICE_LOCKGUARD();
    return m_lod;
  }

  STDMETHOD_(DWORD, GetLevelCount)(THIS) {
    if (m_desc.Usage & D3DUSAGE_AUTOGENMIPMAP)
      return 1;

    return m_desc.Levels;
  }

  STDMETHOD(SetAutoGenFilterType)(THIS_ D3DTEXTUREFILTERTYPE FilterType) {
    LogFunctionCall();

    if (FilterType == D3DTEXF_NONE)
      return D3DERR_INVALIDCALL;

    if (FilterType > D3DTEXF_CONVOLUTIONMONO) {
      return D3DERR_INVALIDCALL;
    }

    {
      BRIDGE_PARENT_DEVICE_LOCKGUARD();
      if (m_mipFilter == FilterType) {
        return S_OK;
      } else {
        m_mipFilter = FilterType;
      }
    }
 
    UID currentUID = 0;
    {
      ClientMessage c(Commands::IDirect3DBaseTexture9_SetAutoGenFilterType, getId());
      currentUID = c.get_uid();
      c.send_data(FilterType);
    }
    WAIT_FOR_OPTIONAL_SERVER_RESPONSE("SetAutoGenFilterType()", D3DERR_INVALIDCALL, currentUID);
  }

  STDMETHOD_(D3DTEXTUREFILTERTYPE, GetAutoGenFilterType)(THIS) {
    LogFunctionCall();

    BRIDGE_PARENT_DEVICE_LOCKGUARD();
    return m_mipFilter;
  }

  STDMETHOD_(void, GenerateMipSubLevels)(THIS) {
    LogFunctionCall();

    if (m_desc.Usage & D3DUSAGE_AUTOGENMIPMAP) {
      ClientMessage c(Commands::IDirect3DBaseTexture9_GenerateMipSubLevels, getId());
    }
  }
};

template<typename TextureType>
using LssBaseTexture2D = Direct3DBaseTexture9_LSS<TextureType, Direct3DSurface9_LSS>;
class Direct3DVolumeTexture9_LSS;
class Direct3DVolume9_LSS;
using LssBaseTexture3D = Direct3DBaseTexture9_LSS<IDirect3DVolumeTexture9, Direct3DVolume9_LSS>;