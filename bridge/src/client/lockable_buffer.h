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
#include <mutex>
#include <queue>
#include <unordered_map>
#include <unordered_set>

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
  std::unique_ptr<uint8_t[], ClientCopyDeleter> m_shadow;
  inline static size_t g_totalBufferShadow = 0;

  // Static buffers whose copy may be freed while idle (client.freeIdleBufferCopiesSec).
  // A freed copy is fetched back from the server on the next lock.
  // Lock order: the device lock, then s_idleCopiesLock.
  static constexpr size_t kMaxIdleCopySize = size_t(4) << 20;
  uint64_t m_lastLockMs = 0;
  uint32_t m_freedCrc = 0;
  bool m_copyFreed = false;
  bool m_inIdleCopies = false;
  inline static std::mutex s_idleCopiesLock;
  inline static std::unordered_set<LockableBuffer*> s_idleCopies;
  // The time of each device's last pass, guarded by s_idleCopiesLock.
  inline static std::unordered_map<const BaseDirect3DDevice9Ex_LSS*, uint64_t> s_lastPassMs;

public:
  DescType getDesc() {
    return m_desc;
  }

  // Frees the copies of pDevice's static buffers that were not locked for idleMs. Call under pDevice's lock.
  static void freeIdleCopies(const uint32_t idleMs, const BaseDirect3DDevice9Ex_LSS* const pDevice) {
    const uint64_t nowMs = GetTickCount64();
    std::lock_guard<std::mutex> lock(s_idleCopiesLock);
    if (!isIdlePassDue(s_lastPassMs[pDevice], nowMs, idleMs)) {
      return;
    }
    for (auto it = s_idleCopies.begin(); it != s_idleCopies.end();) {
      LockableBuffer* const pBuffer = *it;
      // Only the presenting device's own buffers: the device lock does not serialize two devices that were
      // created without D3DCREATE_MULTITHREADED, so another device may be inside a lock of its own buffer.
      if (pBuffer->m_pDevice == pDevice &&
          pBuffer->m_lockInfos.empty() && nowMs - pBuffer->m_lastLockMs >= idleMs) {
        pBuffer->m_freedCrc = calcCrc32c(pBuffer->m_shadow.get(), pBuffer->m_desc.Size);
        pBuffer->releaseShadowMem();
        pBuffer->m_copyFreed = true;
        pBuffer->m_inIdleCopies = false;
        it = s_idleCopies.erase(it);
      } else {
        ++it;
      }
    }
  }

private:
  // A write-only buffer keeps its copy: the server reads a freed copy back with D3DLOCK_READONLY,
  // which D3D9 does not allow on a D3DUSAGE_WRITEONLY buffer.
  bool freesIdleCopy() const {
    return (m_desc.Usage & (D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY)) == 0 && ClientOptions::getFreeIdleBufferCopiesMs();
  }

  void releaseShadowMem() {
    g_totalBufferShadow -= m_desc.Size;
    m_shadow.reset();
  }

  // Returns false when the server sent no whole buffer.
  // The server answers a lock with the ReturnData flag with the HRESULT, then the whole buffer as one blob.
  bool fetchCopy() {
    UID uid = 0;
    {
      ClientMessage c(LockCmd, getId(), Commands::FlagBits::ReturnData);
      uid = c.get_uid();
    }
    if (Result::Success != DeviceBridge::waitForCommand(Commands::Bridge_Response, GlobalOptions::getAckTimeout(), nullptr, true, uid)) {
      Logger::err(format_string("Fetching the freed copy of %s buffer [%p] failed: no response from server.",
                                bIsVertexBuffer ? "vertex" : "index", this));
      return false;
    }
    bool whole = false;
    const HRESULT hr = (HRESULT) DeviceBridge::get_data();
    size_t pulledSize = 0;
    if (SUCCEEDED(hr)) {
      void* pData = nullptr;
      pulledSize = DeviceBridge::get_data(&pData);
      whole = pulledSize >= m_desc.Size;
      if (whole) {
        memcpy(m_shadow.get(), pData, m_desc.Size);
        if (calcCrc32c(m_shadow.get(), m_desc.Size) != m_freedCrc) {
          ONCE(Logger::warn("A fetched buffer copy differs from the freed one. The server's bytes are kept."));
        }
      }
    }
    if (!whole) {
      Logger::err(format_string("Fetching the freed copy of %s buffer [%p] failed: hr 0x%08x, %zu of %u bytes.",
                                bIsVertexBuffer ? "vertex" : "index", this, hr, pulledSize, m_desc.Size));
    }
    DeviceBridge::consumeResponse();
    return whole;
  }
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
    uint8_t* pCopy = nullptr;
    if (isIdleCopySize(m_desc.Size) && m_desc.Size <= kMaxIdleCopySize && freesIdleCopy()) {
      pCopy = static_cast<uint8_t*>(VirtualAlloc(nullptr, m_desc.Size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    }
    m_shadow.get_deleter().virtualAlloc = pCopy != nullptr;
    // VirtualAlloc returns zeroed pages, as the value-initialized heap copy is.
    m_shadow.reset(pCopy != nullptr ? pCopy : new uint8_t[m_desc.Size]());
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
    // Erased first, so that a running freeIdleCopies() pass can no longer reach the buffer.
    if (freesIdleCopy()) {
      std::lock_guard<std::mutex> lock(s_idleCopiesLock);
      s_idleCopies.erase(this);
    }
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
      // The lock fails when the fetch does, so that Unlock cannot upload stale bytes over the server's.
      if (m_copyFreed) {
        initShadowMem();
        if (!fetchCopy()) {
          releaseShadowMem();
          return E_FAIL;
        }
        m_copyFreed = false;
      }
      if (m_shadow.get_deleter().virtualAlloc) {
        m_lastLockMs = GetTickCount64();
      }
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

  HRESULT unlock() {
    // Some engines may attempt to Unlock a non-locked resource "just in case"
    if (m_lockInfos.empty()) {
      return S_OK;
    }
    const auto lockInfo = m_lockInfos.front();
    m_lockInfos.pop();

    // A static buffer's copy becomes freeable after its first unlock, when it holds the application's data.
    if (m_shadow.get_deleter().virtualAlloc) {
      m_lastLockMs = GetTickCount64();
      if (!m_inIdleCopies) {
        std::lock_guard<std::mutex> lock(s_idleCopiesLock);
        s_idleCopies.insert(this);
        m_inIdleCopies = true;
      }
    }

    // Earlier uploads retain their own snapshots, so the discarded buffer can be retired here.
    if (lockInfo.discardedBufferId != SharedHeap::kInvalidId) {
      SharedHeap::deallocate(lockInfo.discardedBufferId);
    }

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
      auto snapshotId = SharedHeap::kInvalidId;
      if (m_bUseSharedHeap) {
        snapshotId = SharedHeap::allocate(std::max<size_t>(size, 1));
        if (snapshotId == SharedHeap::kInvalidId) {
          return E_OUTOFMEMORY;
        }
        memcpy(SharedHeap::getBuf(snapshotId), SharedHeap::getBuf(lockInfo.bufferId) + offset, size);
      }
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
          c.send_data(snapshotId);
        } else if (m_optimizedLock) {
          // Now send data offset in the channel
          const uint32_t dataOffset = static_cast<uint32_t*>(ptr) -
            DeviceBridge::getWriterChannel().get_data_ptr();
          c.send_many(dataOffset);
        } else {
          // Now send the buffer bytes
          c.send_data(size, ptr);
        }
        if (bridge_util::perf::enabled()) {
          bridge_util::perf::noteUpload(size);
        }
      }
      if (snapshotId != SharedHeap::kInvalidId) {
        // Server-ordered deallocation keeps the snapshot immutable until the upload has consumed it.
        SharedHeap::deallocate(snapshotId);
      }
    }
    return S_OK;
  }
};
