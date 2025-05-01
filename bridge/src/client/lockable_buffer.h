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

#include "util_bridgecommand.h"
#include "util_sharedheap.h"

#include "d3d9_util.h"

#include <d3d9.h>
#include <queue>

template <typename T>
class LockableBuffer: public Direct3DResource9_LSS<T> {
  static constexpr bool bIsVertexBuffer = std::is_same_v<IDirect3DVertexBuffer9, T>;
  static constexpr Commands::D3D9Command LockCmd = bIsVertexBuffer ?
    Commands::IDirect3DVertexBuffer9_Lock : Commands::IDirect3DIndexBuffer9_Lock;
  static constexpr Commands::D3D9Command UnlockCmd = bIsVertexBuffer ?
    Commands::IDirect3DVertexBuffer9_Unlock : Commands::IDirect3DIndexBuffer9_Unlock;
  static constexpr size_t kSIMDAlign = 16;
  static constexpr uint32_t kLockCheckValue = 0xbaadf00d;
  using DescType = std::conditional_t<bIsVertexBuffer, D3DVERTEXBUFFER_DESC, D3DINDEXBUFFER_DESC>;

  struct LockInfo {
    UINT offsetToLock;
    UINT sizeToLock;
    void* pbData;
    DWORD flags;
    uint32_t* checkPtr;
    SharedHeap::AllocId bufferId = SharedHeap::kInvalidId;
    SharedHeap::AllocId discardedBufferId = SharedHeap::kInvalidId;
  };
  std::queue<LockInfo> m_lockInfos;

  const bool m_bUseSharedHeap = false;
  std::unique_ptr<uint8_t[]> m_shadow;
  inline static size_t g_totalBufferShadow = 0;

public:
  DescType getDesc() {
    return m_desc;
  }

private:
  static bool getSharedHeapPolicy(const DescType& desc) {
    if (GlobalOptions::getUseSharedHeap()) {
      return (desc.Usage & D3DUSAGE_DYNAMIC) ?
        GlobalOptions::getUseSharedHeapForDynamicBuffers() :
        GlobalOptions::getUseSharedHeapForStaticBuffers();
    } else {
      return false;
    }
  }

  void initShadowMem() {
    m_shadow = std::make_unique<uint8_t[]>(m_desc.Size);
    g_totalBufferShadow += m_desc.Size;
    Logger::trace(format_string("Allocated a shadow for dynamic %s buffer [%p] "
                                "(size: %zd, total shadow size: %zd)",
                                bIsVertexBuffer ? "vertex" : "index",
                                this, m_desc.Size, g_totalBufferShadow));
  }

protected:
  const DescType m_desc;
  SharedHeap::AllocId m_bufferId = SharedHeap::kInvalidId;
  const bool m_sendWhole = false;
  const bool m_optimizedLock = false;

  LockableBuffer(T* const pD3dBuf, BaseDirect3DDevice9Ex_LSS* const pDevice, const DescType& desc)
    : Direct3DResource9_LSS<T>(pD3dBuf, pDevice)
    , m_desc(desc)
    , m_bUseSharedHeap(getSharedHeapPolicy(m_desc))
    , m_sendWhole((desc.Usage& D3DUSAGE_DYNAMIC) == 0 && GlobalOptions::getAlwaysCopyEntireStaticBuffer())
    , m_optimizedLock((desc.Usage& D3DUSAGE_DYNAMIC) != 0 && ClientOptions::getOptimizedDynamicLock()) {
    if (!m_bUseSharedHeap) {
      initShadowMem();
    }
  }

  ~LockableBuffer() {
    if (m_bUseSharedHeap) {
      if (m_bufferId != SharedHeap::kInvalidId) {
        SharedHeap::deallocate(m_bufferId);
      }
    } else if (m_shadow) {
      g_totalBufferShadow -= m_desc.Size;
      Logger::trace(format_string("Released shadow of dynamic %s buffer [%p] "
                                  "(size: %zd, total shadow size: %zd)",
                                  bIsVertexBuffer ? "vertex" : "index",
                                  this, m_desc.Size, g_totalBufferShadow));
    }
  }

  HRESULT lock(const UINT offset, const UINT size, void** ppbData, const DWORD flags) {
    if (ppbData == nullptr) {
      return D3DERR_INVALIDCALL;
    }

    uint32_t* checkPtr = nullptr;

    if (m_bUseSharedHeap) {
      SharedHeap::AllocId discardedBufferId = SharedHeap::kInvalidId;
      const bool bDiscard = (flags & D3DLOCK_DISCARD) != 0;
      if (bDiscard && (m_bufferId != SharedHeap::kInvalidId)) {
        // If D3DLOCK_DISCARD is an active flag, we must begin the process of dealloc'ing
        // and freeing that buffer from the shared heap
        discardedBufferId = m_bufferId;
      }
      const auto nextBufId = (bDiscard || (m_bufferId == SharedHeap::kInvalidId)) ?
        m_bufferId = SharedHeap::allocate(m_desc.Size) : m_bufferId;
      if (nextBufId == SharedHeap::kInvalidId) {
        std::stringstream ss;
        ss << "[LockableBuffer][Lock] Failed to allocate on SharedHeap: {";
        ss << "offset=" << offset << ",";
        ss << "size=" << size << ",";
        ss << "flags=" << flags << ",";
        ss << "m_desc.Size=" << m_desc.Size << ",";
        ss << "m_bufferId=" << m_bufferId << "}";
        Logger::err(ss.str());
        return E_FAIL;
      }
      m_bufferId = nextBufId;
      *ppbData = SharedHeap::getBuf(m_bufferId) + offset;
      m_lockInfos.push({ offset, size, nullptr, flags, checkPtr, m_bufferId, discardedBufferId });
    } else {
      *ppbData = m_shadow.get() + offset;

      if (m_optimizedLock) {
        const size_t dataSize = (size == 0) ? m_desc.Size : size;

        // Send the buffer lock parameters and handle
        ClientMessage c(LockCmd, getId(), 0);
        // Reserve blob in data stream
        uintptr_t blobAddr = reinterpret_cast<uintptr_t>(
          c.begin_data_blob(dataSize + sizeof(kLockCheckValue) + kSIMDAlign));
        c.end_data_blob();

        // Push buffer check value in front. If front gets corrupted the entire region deemed invalid.
        checkPtr = reinterpret_cast<uint32_t*>(blobAddr);
        blobAddr += sizeof(kLockCheckValue);
        *checkPtr = kLockCheckValue;

        // Align data blob for SIMD ops
        blobAddr = align<uintptr_t>(blobAddr, kSIMDAlign);
        *ppbData = reinterpret_cast<void*>(blobAddr);
      }
      m_lockInfos.push({ offset, size, *ppbData, flags, checkPtr });
    }
    // Store locked buffer pointer locally so we can copy the data on unlock
    return S_OK;
  }

  void unlock() {
    // Some engines may attempt to Unlock a non-locked resource "just in case"
    if (m_lockInfos.empty()) {
      return;
    }
    // Get most recent locked buffer and grab data before we unlock
    const auto& lockInfo = m_lockInfos.front();
    uint32_t offset = lockInfo.offsetToLock;
    // Clamp the size since some applications can request unreasonably large lock sizes that are not actually used
    size_t size = (lockInfo.sizeToLock == 0) ? m_desc.Size - offset : std::min(lockInfo.sizeToLock, m_desc.Size - offset);
    void* ptr = lockInfo.pbData;

    if (m_sendWhole) {
      size = m_desc.Size;
      offset = 0;
      ptr = m_shadow.get();
    }

    // If this is a read only access then don't bother sending
    if ((lockInfo.flags & D3DLOCK_READONLY) == 0) {
      {
        Commands::Flags cmdFlags = 0;

        if (m_bUseSharedHeap) {
          cmdFlags = Commands::FlagBits::DataInSharedHeap;
        } else if (m_optimizedLock) {
          cmdFlags = Commands::FlagBits::DataIsReserved;

          if (kLockCheckValue != *lockInfo.checkPtr) {
            Logger::err("Fatal: reserved buffer region has been corrupted! "
                        "Application will now exit.");
            throw;
          }
        }

        // Send the buffer lock parameters and handle
        ClientMessage c(UnlockCmd, getId(), cmdFlags);
        c.send_many(offset, size, lockInfo.flags);

        if (m_bUseSharedHeap) {
          c.send_data(lockInfo.bufferId);
        } else if (m_optimizedLock) {
          // Now send data offset in the channel
          const uint32_t dataOffset = static_cast<uint32_t*>(ptr) -
            DeviceBridge::getWriterChannel().get_data_ptr();
          c.send_many(dataOffset);
        } else {
          // Now send the buffer bytes
          c.send_data(size, ptr);
        }
      }
      if (lockInfo.discardedBufferId != SharedHeap::kInvalidId) {
        SharedHeap::deallocate(lockInfo.discardedBufferId);
      }
    }
    m_lockInfos.pop();
  }
};