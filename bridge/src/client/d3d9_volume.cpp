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
#include "d3d9_volume.h"

#include "util_bridge_assert.h"

/*
 * Direct3DVolume9_LSS Interface Implementation
 */

HRESULT Direct3DVolume9_LSS::QueryInterface(REFIID riid, LPVOID* ppvObj) {
  LogFunctionCall();
  if (ppvObj == nullptr) {
    return E_POINTER;
  }

  *ppvObj = nullptr;

  if (riid == __uuidof(IUnknown)
    || riid == __uuidof(IDirect3DVolume9)) {
    *ppvObj = bridge_cast<IDirect3DVolume9*>(this);
    AddRef();
    return S_OK;
  }

  return E_NOINTERFACE;
}

ULONG Direct3DVolume9_LSS::AddRef() {
  LogFunctionCall();
  // No push since we only care about the last Release call
  return D3DBase::AddRef();
}

ULONG Direct3DVolume9_LSS::Release() {
  LogFunctionCall();
  return D3DBase::Release();
}

void Direct3DVolume9_LSS::onDestroy() {
  // The standalone volumes use normal destroy command, however child volumes
  // are completely owned and managed by their parent container, and so only need
  // to be unlinked from x64 counterpart to prevent hash collisions at server side.
  const auto command = isStandalone() ? Commands::IDirect3DVolume9_Destroy :
    Commands::Bridge_UnlinkVolumeResource;

  ClientMessage { command, getId() };
}

HRESULT Direct3DVolume9_LSS::GetDevice(IDirect3DDevice9** ppDevice) {
  LogFunctionCall();
  m_pDevice->AddRef();
  (*ppDevice) = m_pDevice;
  return S_OK;
}

HRESULT Direct3DVolume9_LSS::SetPrivateData(REFGUID refguid, CONST void* pData, DWORD SizeOfData, DWORD Flags) {
  LogFunctionCall();
  return m_privateData.setData(refguid, pData, SizeOfData, Flags);
}

HRESULT Direct3DVolume9_LSS::GetPrivateData(REFGUID refguid, void* pData, DWORD* pSizeOfData) {
  LogFunctionCall();
  return m_privateData.getData(refguid, pData, pSizeOfData);
}

HRESULT Direct3DVolume9_LSS::FreePrivateData(REFGUID refguid) {
  LogFunctionCall();
  return m_privateData.freeData(refguid);
}

HRESULT Direct3DVolume9_LSS::GetContainer(REFIID riid, void** ppContainer) {
  LogFunctionCall();
  return getParent()->QueryInterface(riid, ppContainer);
}

HRESULT Direct3DVolume9_LSS::GetDesc(D3DVOLUME_DESC* pDesc) {
  LogFunctionCall();
  *pDesc = m_desc;
  return S_OK;
}

HRESULT Direct3DVolume9_LSS::LockBox(D3DLOCKED_BOX* pLockedVolume, CONST D3DBOX* pBox, DWORD Flags) {
  LogFunctionCall();
  // Store locked rect pointer locally so we can copy the data on unlock
  {
    BRIDGE_PARENT_DEVICE_LOCKGUARD();
    if (!lock(*pLockedVolume, pBox, Flags)) {
      std::stringstream ss;
      ss << "[Direct3DVolume9_LSS][LockBox] Failed!";
      Logger::err(ss.str());
      return E_FAIL;
    }
  }
  // Do nothing on lock on server side for now, all logic happens on unlock!
  return S_OK;
}

HRESULT Direct3DVolume9_LSS::UnlockBox() {
  LogFunctionCall();
  {
    BRIDGE_PARENT_DEVICE_LOCKGUARD();
    unlock();
  }
  return S_OK;
}

bool Direct3DVolume9_LSS::lock(D3DLOCKED_BOX& lockedVolume, const D3DBOX* const pBox, const DWORD flags) {
  const D3DBOX box = resolveLockInfoBox(pBox, m_desc);
  const auto [width, height, depth] = getBoxDimensions(box);
  const auto pixelsPerBlock = bridge_util::getBlockSize(m_desc.Format);
  const auto bytesPerPixel = bridge_util::getBytesFromFormat(m_desc.Format);
  const auto rowStride = ((width + pixelsPerBlock - 1) / pixelsPerBlock);
  const auto columnStride = ((height + pixelsPerBlock - 1) / pixelsPerBlock);
  const auto size = depth * columnStride * rowStride * bytesPerPixel;
  void* const pData = new uint8_t[size];
  lockedVolume.RowPitch = rowStride;
  lockedVolume.SlicePitch = columnStride;
  lockedVolume.pBits = pData;
  m_lockInfoQueue.push({ lockedVolume, box, flags });
  return true;
}

void Direct3DVolume9_LSS::unlock() {
  // Some game engines may attempt to Unlock a non-locked resource "just in case"
  if (m_lockInfoQueue.empty()) {
    return;
  }
  const auto lockInfo = m_lockInfoQueue.front();
  m_lockInfoQueue.pop();

  // Prep command send
  const auto& lockedVolume = lockInfo.lockedVolume;
  const auto& box = lockInfo.box;
  const auto [width, height, depth] = getBoxDimensions(box);
  const auto bytesPerPixel = bridge_util::getBytesFromFormat(m_desc.Format);
  const auto rowSize = lockedVolume.RowPitch * bytesPerPixel;
  const auto totalSize = depth * lockedVolume.SlicePitch * rowSize;

  // Now send the box dimensions and surface handle
  if ((lockInfo.flags & D3DLOCK_READONLY) == 0) {
    ClientMessage c(Commands::IDirect3DVolume9_UnlockBox, getId());

    c.send_data(sizeof(D3DBOX), &box);
    c.send_data(lockInfo.flags);

    // Now push actual bytes
    c.send_many(bytesPerPixel, lockedVolume.RowPitch, lockedVolume.SlicePitch, depth);
#ifdef SEND_ALL_LOCK_DATA_AT_ONCE
    if (auto* blobPacketPtr = c.begin_data_blob(totalSize))
#endif
      for (uint32_t z = 0; z < depth; z++) {
        for (uint32_t y = 0; y < lockedVolume.SlicePitch; y++) {
          auto ptr = static_cast<uint8_t*>(lockedVolume.pBits) +
            y * lockedVolume.RowPitch + z * lockedVolume.SlicePitch;
#ifdef SEND_ALL_LOCK_DATA_AT_ONCE
          memcpy(blobPacketPtr, ptr, rowSize);
          blobPacketPtr += rowSize;
#else
          c.send_data(rowSize, ptr);
#endif
        }
      }
#ifdef SEND_ALL_LOCK_DATA_AT_ONCE
    c.end_data_blob();
#endif
  }
  delete lockedVolume.pBits;
}

D3DBOX Direct3DVolume9_LSS::resolveLockInfoBox(const D3DBOX* const pBox, const D3DVOLUME_DESC& desc) {
  return (pBox != nullptr) ? *pBox : D3DBOX { 0, 0, desc.Width, desc.Height, 0, desc.Depth };
}

std::tuple<size_t, size_t, size_t> Direct3DVolume9_LSS::getBoxDimensions(const D3DBOX& box) {
  return { box.Right  - box.Left,
           box.Bottom - box.Top,
           box.Back   - box.Front };
}
