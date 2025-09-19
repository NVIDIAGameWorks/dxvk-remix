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

#include "util_common.h"
#include "util_scopedlock.h"

#include "base.h"
#include <unordered_map>
#include <comip.h>

// Specialize std::hash
namespace std {
  template<> struct hash<GUID> {
    size_t operator()(const GUID& guid) const noexcept {
      const std::uint64_t* p = reinterpret_cast<const std::uint64_t*>(&guid);
      std::hash<std::uint64_t> hash;
      return hash(p[0]) ^ hash(p[1]);
    }
  };
}


class PrivateDataFactory {
  struct PrivateData {
    void* pData;
    DWORD SizeOfData;

    PrivateData(CONST void* _pData, DWORD size)
      : pData(std::malloc(size)), SizeOfData(size) {
      memcpy(pData, _pData, SizeOfData);
    }

    ~PrivateData() {
      std::free(pData);
    }
  };

  struct PrivateInterface {
    IUnknown* pInterface;
    inline static const DWORD SizeOfData = sizeof(IUnknown*);

    PrivateInterface(IUnknown* _pData)
      : pInterface(_pData) {
      if (pInterface)
        pInterface->AddRef();
    }

    ~PrivateInterface() {
      if (pInterface)
        pInterface->Release();
    }
  };

  std::unordered_map<GUID, PrivateData> m_privateData;
  std::unordered_map<GUID, PrivateInterface> m_privateInterfaces;

public:

  ~PrivateDataFactory() {
    m_privateData.clear();
    m_privateInterfaces.clear();
  }


  HRESULT setData(REFGUID refguid, CONST void* pData, DWORD SizeOfData, DWORD Flags) {
    if (Flags & D3DSPD_IUNKNOWN) {
      if (SizeOfData != sizeof(IUnknown*))
        return D3DERR_INVALIDCALL;

      if (pData != nullptr) {
        IUnknown* pInterface = const_cast<IUnknown*>(reinterpret_cast<const IUnknown*>(pData));
        m_privateInterfaces.insert_or_assign(refguid, pInterface);
      }
    } else {
      m_privateData.insert_or_assign(refguid, PrivateData(pData, SizeOfData));
    }

    return S_OK;
  }

  HRESULT getData(REFGUID refguid, void* pData, DWORD* pSizeOfData) {
    if (m_privateData.count(refguid)) {
      const PrivateData data = m_privateData.at(refguid);
      if (pData != nullptr)
        memcpy(pData, data.pData, data.SizeOfData);

      (*pSizeOfData) = data.SizeOfData;
      return S_OK;
    }

    if (m_privateInterfaces.count(refguid)) {
      const PrivateInterface& data = m_privateInterfaces.at(refguid);
      if (pData != nullptr) {
        data.pInterface->AddRef();
        memcpy(pData, &data.pInterface, data.SizeOfData);
      }

      (*pSizeOfData) = data.SizeOfData;
      return S_OK;
    }

    return D3DERR_INVALIDCALL;
  }

  HRESULT freeData(REFGUID refguid) {
    if (m_privateData.count(refguid)) {
      m_privateData.erase(refguid);
      return S_OK;
    }

    if (m_privateInterfaces.count(refguid)) {
      m_privateInterfaces.erase(refguid);
      return S_OK;
    }

    return D3DERR_INVALIDCALL;
  }
};
