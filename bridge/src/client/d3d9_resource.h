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


#include "base.h"
#include "d3d9_util.h"
#include "d3d9_device_base.h"
#include "d3d9_privatedata.h"

#include "util_devicecommand.h"
#include "util_common.h"
#include "util_scopedlock.h"

#include <d3d9.h>

class BaseDirect3DDevice9Ex_LSS;

/*
 * IDirect3DResource9 LSS Interceptor Class
 */
template<typename T>
class Direct3DResource9_LSS: public D3DBase<T> {
protected:
  BaseDirect3DDevice9Ex_LSS* m_pDevice = nullptr;

  DWORD m_priority = 0;

  PrivateDataFactory m_privateData;

public:
  Direct3DResource9_LSS(T* const pResource,
                        BaseDirect3DDevice9Ex_LSS* const pDevice)
    : D3DBase(pResource, pDevice)
    , m_pDevice(pDevice) {
  }

  template<typename ContainerType>
  Direct3DResource9_LSS(T* const pResource,
                        BaseDirect3DDevice9Ex_LSS* const pDevice,
                        ContainerType* const pContainer)
    : D3DBase(pResource, pDevice, pContainer)
    , m_pDevice(pDevice) {
  }

  STDMETHOD(QueryInterface)(THIS_ REFIID riid, void** ppvObj) override {
    if (ppvObj == nullptr) {
      return E_POINTER;
    }

    *ppvObj = nullptr;

    if (riid == __uuidof(IUnknown)
      || riid == __uuidof(IDirect3DResource9)
      || riid == __uuidof(T)) {
      *ppvObj = bridge_cast<IDirect3DResource9*>(this);
      AddRef();
      return S_OK;
    }

    return E_NOINTERFACE;
  }

  STDMETHOD(GetDevice)(THIS_ IDirect3DDevice9** ppDevice) {
    LogFunctionCall();
    if (ppDevice == nullptr) {
      return D3DERR_INVALIDCALL;
    }
    m_pDevice->AddRef();
    (*ppDevice) = m_pDevice;
    return S_OK;
  }

  STDMETHOD(SetPrivateData)(THIS_ REFGUID refguid, CONST void* pData, DWORD SizeOfData, DWORD Flags) {
    LogFunctionCall();
    return m_privateData.setData(refguid, pData, SizeOfData, Flags);
  }

  STDMETHOD(GetPrivateData)(THIS_ REFGUID refguid, void* pData, DWORD* pSizeOfData) {
    LogFunctionCall();
    if (pData == nullptr) {
      return D3DERR_INVALIDCALL;
    }
    return m_privateData.getData(refguid, pData, pSizeOfData);
  }

  STDMETHOD(FreePrivateData)(THIS_ REFGUID refguid) {
    LogFunctionCall();
    return m_privateData.freeData(refguid);
  }

  STDMETHOD_(DWORD, SetPriority)(THIS_ DWORD PriorityNew) {
    LogFunctionCall();

    DWORD oldPriority = m_priority;
    m_priority = PriorityNew;

    if (oldPriority != m_priority) {
      ClientMessage c(Commands::IDirect3DResource9_SetPriority, getId());
      c.send_data(PriorityNew);
    }

    return oldPriority;
  }

  STDMETHOD_(DWORD, GetPriority)(THIS) {
    LogFunctionCall();

    return m_priority;
  }

  STDMETHOD_(void, PreLoad)(THIS) {
    LogFunctionCall();

    ClientMessage c(Commands::IDirect3DResource9_PreLoad, getId());
  }

  STDMETHOD_(D3DRESOURCETYPE, GetType)(THIS) = 0;
};

// A D3D object that may contain other D3D objects.
// The object itself may or may not be a resource object. The reference
// counts of every contained object are always equal to the reference
// count of the container object.
// 
// The container objects are:
//   IDirect3DTexture9, IDirect3DCubeTexture9, IDirect3DVolumeTexture9,
//   IDirect3DSwapChain9.
template<typename BaseType, typename ChildType>
class Direct3DContainer9_LSS: public BaseType {
public:
  Direct3DContainer9_LSS(IUnknown* const pObj,
                         BaseDirect3DDevice9Ex_LSS* const pDevice)
    : BaseType(static_cast<BaseType::BaseD3DType* const>(pObj), pDevice)
    , m_pDevice { pDevice } {
  }

protected:
  ~Direct3DContainer9_LSS() override {
    BRIDGE_PARENT_DEVICE_LOCKGUARD();
    // Container is about to be destroyed, need to destroy its children, if any.
    for (auto child : m_children) {
      if (child) {
        child->destroy();
      }
    }
  }

  inline ChildType* getChild(uint32_t idx) const {
    BRIDGE_PARENT_DEVICE_LOCKGUARD();
    assert(idx < m_children.size() && "Child index overrun!");
    return m_children[idx];
  }

  inline void setChild(uint32_t idx, ChildType* child) {
    BRIDGE_PARENT_DEVICE_LOCKGUARD();
    assert(idx < m_children.size() && "Child index overrun!");
    assert(m_children[idx] == nullptr && "Child object may be only set once!");

    m_children[idx] = child;
  }

  BaseDirect3DDevice9Ex_LSS* const m_pDevice;
  std::vector<ChildType*> m_children;
};
