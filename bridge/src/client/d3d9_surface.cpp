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
#include "pch.h"

#include "d3d9_lss.h"
#include "d3d9_util.h"
#include "d3d9_surface.h"
#include "d3d9_texture.h"
#include "d3d9_cubetexture.h"

#include "d3d9_surfacebuffer_helper.h"
#include "util_bridge_assert.h"
#include "util_gdi.h"

Direct3DSurface9_LSS::Direct3DSurface9_LSS(BaseDirect3DDevice9Ex_LSS* const pDevice,
                                           const D3DSURFACE_DESC& desc, bool isBackBuffer)
  : Direct3DResource9_LSS((IDirect3DSurface9*)nullptr, pDevice)
  , m_bUseSharedHeap(GlobalOptions::getUseSharedHeapForTextures())
  , m_desc(desc)
  , m_isBackBuffer(isBackBuffer)
{
}

Direct3DSurface9_LSS::~Direct3DSurface9_LSS() {
  if (m_bUseSharedHeap) {
    if (m_bufferId != SharedHeap::kInvalidId) {
      SharedHeap::deallocate(m_bufferId);
    }
  } else if (m_shadow) {
    const auto surfaceSize =
      bridge_util::calcTotalSizeOfRect(m_desc.Width, m_desc.Height, m_desc.Format);

    g_totalSurfaceShadow -= surfaceSize;
    Logger::trace(format_string("Releasing shadow of surface [%p] "
                                "(size: %zd, total surface shadow size: %zd)",
                                this, surfaceSize, g_totalSurfaceShadow));
  }
}

HRESULT Direct3DSurface9_LSS::QueryInterface(REFIID riid, LPVOID* ppvObj) {
  LogFunctionCall();
  if (ppvObj == nullptr) {
    return E_POINTER;
  }

  *ppvObj = nullptr;

  if (riid == __uuidof(IUnknown)
    || riid == __uuidof(IDirect3DResource9)
    || riid == __uuidof(IDirect3DSurface9)) {
    *ppvObj = bridge_cast<IDirect3DSurface9*>(this);
    AddRef();
    return S_OK;
  }

  return E_NOINTERFACE;
}

ULONG Direct3DSurface9_LSS::AddRef() {
  LogFunctionCall();
  // No push since we only care about the last Release call
  return Direct3DResource9_LSS::AddRef();
}

ULONG Direct3DSurface9_LSS::Release() {
  LogFunctionCall();
  return Direct3DResource9_LSS::Release();
}

void Direct3DSurface9_LSS::onDestroy() {
  // The standalone surfaces use normal destroy command, however child surfaces
  // are completely owned and managed by their parent container, and so only need
  // to be unlinked from x64 counterpart to prevent hash collisions at server side.
  const auto command = isStandalone() ? Commands::IDirect3DSurface9_Destroy :
    Commands::Bridge_UnlinkResource;

  ClientMessage { command, getId() };
}

HRESULT Direct3DSurface9_LSS::GetContainer(REFIID riid, void** ppContainer) {
  LogFunctionCall();
  if (ppContainer == nullptr) {
    return D3DERR_INVALIDCALL;
  }
  return getParent()->QueryInterface(riid, ppContainer);
}

HRESULT Direct3DSurface9_LSS::GetDesc(D3DSURFACE_DESC* pDesc) {
  LogFunctionCall();

  if (pDesc == nullptr) {
    return D3DERR_INVALIDCALL;
  }
  (*pDesc) = m_desc;

  if (GlobalOptions::getSendReadOnlyCalls()) {
    // Add surface handle
    ClientMessage c(Commands::IDirect3DSurface9_GetDesc, getId());
    c.send_data(sizeof(D3DSURFACE_DESC), pDesc);
  }
  return S_OK;
}

HRESULT Direct3DSurface9_LSS::LockRect(D3DLOCKED_RECT* pLockedRect, CONST RECT* pRect, DWORD Flags) {
  LogFunctionCall();
  // Store locked rect pointer locally so we can copy the data on unlock
  {
    BRIDGE_PARENT_DEVICE_LOCKGUARD();
    if (!lock(*pLockedRect, pRect, Flags)) {
      std::stringstream ss;
      ss << "[Direct3DSurface9_LSS][LockRect] Failed!";
      Logger::err(ss.str());
      return E_FAIL;
    }
  }

  // We send LockRect() calls to server in cases wherein backbuffer is used to capture the screenshot
  if (m_isBackBuffer && ClientOptions::getEnableBackbufferCapture() && !(Flags & D3DLOCK_DISCARD)) {
    UID currentUID;
    {
      ClientMessage c(Commands::IDirect3DSurface9_LockRect, getId());
      currentUID = c.get_uid();
    }

    return copyServerSurfaceRawData(this, currentUID);
  }

  return S_OK;
}

HRESULT Direct3DSurface9_LSS::UnlockRect() {
  LogFunctionCall();
  {
    BRIDGE_PARENT_DEVICE_LOCKGUARD();
    unlock();
  }
  return S_OK;
}

HRESULT Direct3DSurface9_LSS::GetDC(HDC* phdc) {
  LogFunctionCall();
  if (phdc == nullptr) {
    return D3DERR_INVALIDCALL;
  }
  D3DLOCKED_RECT lockedRect;
  HRESULT hr = LockRect(&lockedRect, nullptr, 0);
  if (FAILED(hr)) {
    return hr;
  }
  gdi::D3DKMT_CREATEDCFROMMEMORY createInfo;
  // In...
  createInfo.pMemory = lockedRect.pBits;
  createInfo.Format = m_desc.Format;
  createInfo.Width = m_desc.Width;
  createInfo.Height = m_desc.Height;
  createInfo.Pitch = lockedRect.Pitch;
  createInfo.hDeviceDc = CreateCompatibleDC(NULL);
  createInfo.pColorTable = nullptr;

  // Out...
  createInfo.hBitmap = nullptr;
  createInfo.hDc = nullptr;

  gdi::D3DKMTCreateDCFromMemory(&createInfo);
  DeleteDC(createInfo.hDeviceDc);

  // These should now be set...
  m_dcDesc.hDC = createInfo.hDc;
  m_dcDesc.hBitmap = createInfo.hBitmap;

  *phdc = m_dcDesc.hDC;
  return D3D_OK;
}

HRESULT Direct3DSurface9_LSS::ReleaseDC(HDC hdc) {
  LogFunctionCall();
  assert(m_dcDesc.hDC == hdc);
  gdi::D3DKMTDestroyDCFromMemory(&m_dcDesc);
  HRESULT hr = UnlockRect();
  if (FAILED(hr)) {
    return hr;
  }
  return hr;
}

bool Direct3DSurface9_LSS::lock(D3DLOCKED_RECT& lockedRect, const RECT* pRect, const DWORD& flags) {
  const RECT rect = resolveLockInfoRect(pRect, m_desc);
  lockedRect.Pitch = bridge_util::calcRowSize(m_desc.Width, m_desc.Format);
  const auto surfaceSize =
    bridge_util::calcTotalSizeOfRect(m_desc.Width, m_desc.Height, m_desc.Format);
  if (m_bUseSharedHeap) {
    auto discardBufId = SharedHeap::kInvalidId;
    const bool bDiscard = (flags & D3DLOCK_DISCARD) != 0;
    if (bDiscard || (m_bufferId == SharedHeap::kInvalidId)) {
      discardBufId = m_bufferId;
      m_bufferId = SharedHeap::allocate(surfaceSize);
    }
    if (m_bufferId == SharedHeap::kInvalidId) {
      return false;
    }
    lockedRect.pBits = getBufPtr(lockedRect.Pitch, rect);
    m_lockInfoQueue.push({ lockedRect, rect, flags, m_bufferId, discardBufId });
  } else {
    if (!m_shadow) {
      m_shadow.reset(new uint8_t[surfaceSize]);
      g_totalSurfaceShadow += surfaceSize;
      Logger::trace(format_string("Allocated a shadow for surface [%p] "
                                  "(size: %zd, total surface shadow size: %zd)",
                                  this, surfaceSize, g_totalSurfaceShadow));
    }

    const size_t byteOffset = bridge_util::calcImageByteOffset(lockedRect.Pitch, rect, m_desc.Format);

    lockedRect.pBits = m_shadow.get() + byteOffset;
    m_lockInfoQueue.push({ lockedRect, rect, flags });
  }
  return true;
}

void Direct3DSurface9_LSS::unlock() {
  // Some game engines may attempt to Unlock a non-locked resource "just in case"
  if (m_lockInfoQueue.empty()) {
    return;
  }
  const auto lockInfo = m_lockInfoQueue.front();
  m_lockInfoQueue.pop();
  // If this is a read only access then don't bother sending anything to the server
  if ((lockInfo.flags & D3DLOCK_READONLY) == 0) {
    sendDataToServer(lockInfo);
  }
}

RECT Direct3DSurface9_LSS::resolveLockInfoRect(const RECT* const pRect, const D3DSURFACE_DESC& desc) {
  return (pRect != nullptr) ?
    RECT { pRect->left, pRect->top, pRect->right, pRect->bottom } :
    RECT { 0, 0, static_cast<LONG>(desc.Width), static_cast<LONG>(desc.Height) };
}

void* Direct3DSurface9_LSS::getBufPtr(const int pitch, const RECT& rect) {
  const size_t byteOffset = bridge_util::calcImageByteOffset(pitch, rect, m_desc.Format);
  void* const ptr = SharedHeap::getBuf(m_bufferId) + byteOffset;
  return ptr;
}

void Direct3DSurface9_LSS::sendDataToServer(const LockInfo& lockInfo) const {
  const auto dataFlag = m_bUseSharedHeap ? Commands::FlagBits::DataInSharedHeap : 0;
  {
    ClientMessage c(Commands::IDirect3DSurface9_UnlockRect, getId(), dataFlag);
    c.send_data(sizeof(RECT), &lockInfo.rect);
    c.send_data(lockInfo.flags);
    c.send_data(m_desc.Format);
    if (m_bUseSharedHeap) {
      c.send_data(lockInfo.lockedRect.Pitch);
      c.send_data(lockInfo.bufId);
    } else {
      const auto [width, height] = getRectDimensions(lockInfo.rect);
      const size_t totalSize = bridge_util::calcTotalSizeOfRect(width, height, m_desc.Format);
      const size_t rowSize = bridge_util::calcRowSize(width, m_desc.Format);
      c.send_data(rowSize);
      if (auto* blobPacketPtr = c.begin_data_blob(totalSize)) {
        FOR_EACH_RECT_ROW(lockInfo.lockedRect, height, m_desc.Format, {
          memcpy(blobPacketPtr, ptr, rowSize);
          blobPacketPtr += rowSize;
        });
        c.end_data_blob();
      }
    }
  }

  if (m_bUseSharedHeap) {
    if (lockInfo.discardBufId != SharedHeap::kInvalidId) {
      SharedHeap::deallocate(lockInfo.discardBufId);
    }
  }
}

std::tuple<size_t, size_t> Direct3DSurface9_LSS::getRectDimensions(const RECT& rect) {
  return { rect.right  - rect.left,
           rect.bottom - rect.top  };
}