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
#include "util_sharedmemory.h"

#include <unordered_map>
#include <map>

namespace bridge_util {
  class SharedHeap {
  public:
    using Id = uint32_t;
    static constexpr Id kInvalidId = (Id) -1;
    using AllocId = Id;
    using ChunkId = Id;

    static void init();
    static BYTE* getBuf(const AllocId id) {
      return get().getBuf(id);
    }
#ifdef REMIX_BRIDGE_CLIENT
    static AllocId allocate(const size_t size) {
      return get().allocate(size);
    }
    static void deallocate(const AllocId id) {
      get().deallocate(id);
    }
#endif
#ifdef REMIX_BRIDGE_SERVER
    static void allocate(const AllocId id, const ChunkId firstChunk) {
      get().allocate(id, firstChunk);
    }
    static void deallocate(const AllocId id) {
      get().deallocate(id);
    }
    static void addNewHeapSegment(const uint32_t segmentSize) {
      get().addNewHeapSegment(segmentSize);
    }
#endif

  private:
    SharedHeap() = delete;
    SharedHeap(const SharedHeap& b) = delete;
    SharedHeap(const SharedHeap&& b) = delete;

    class Instance {
    public:
      Instance();
      BYTE* getBuf(const AllocId id);
#ifdef REMIX_BRIDGE_CLIENT
      AllocId allocate(const size_t size);
      void deallocate(const AllocId id);
#endif
#ifdef REMIX_BRIDGE_SERVER
      void allocate(const AllocId id, const ChunkId firstChunk);
      void deallocate(const AllocId id);
      void addNewHeapSegment(const uint32_t segmentSize);
#endif

    private:
      // Constants
      static constexpr uint32_t kMax32BitHeapSize = 2 << 30; // 2GB

      // Members
      const uint32_t m_chunkSize;
      uint32_t m_defaultSegmentSize;
      uint32_t m_nChunks;
      std::unordered_map<AllocId, ChunkId> m_cache;
#ifdef REMIX_BRIDGE_CLIENT
      AllocId m_nextUid = 0;
      std::map<ChunkId, ChunkId> m_allocations;
      size_t m_sizeAllocated = 0;
#endif

      // Delete other ctors
      Instance(const Instance& b) = delete;
      Instance(const Instance&& b) = delete;

      // Helpers
      size_t getTotalHeapSize() const;
      Id chunkIdToSegId(const ChunkId chunkId) const;
#ifdef REMIX_BRIDGE_CLIENT
      bool addNewHeapSegment();
      struct Allocation {
        ChunkId firstChunk = kInvalidId;
        ChunkId finalChunk = kInvalidId;
      };
      static inline Allocation createAllocation(const ChunkId firstChunk,
                                                const size_t numChunks) {
        return { firstChunk, firstChunk + numChunks - 1 };
      }
      Allocation findAllocation(const size_t numChunks);
      Allocation findFreeInMiddle(const size_t numChunks);
      Allocation findFreeOnEnd(const size_t numChunks);
      void freeDeallocations();
      bool isValidAllocation(const Allocation& alloc);
      bool allocationCrossesHeapSegBound(const Allocation& alloc);
#endif

      // State Helpers
      enum class ChunkState: uint8_t {
        Unallocated = 0,
        Allocated = 1,
        Deallocated = 2,
        Invalid = 0xff
      };
      void setChunkState(const ChunkId& chunkId, const ChunkState state);
      ChunkState getChunkState(const ChunkId& chunkId) const;

      // Diagnostic + Debug helpers
#ifdef SHARED_HEAP_DIAG
      void dumpState();
      void dumpHeapFragmentation(std::stringstream& ss);
#endif

      // Shared Memory members
      SharedMemory m_metaShMem;
      class Segment {
      public:
        Segment(const std::string shMemName,
                const size_t segmentSize,
                const size_t chunkSize,
                const ChunkId baseChunkId)
          : m_shMem(shMemName, segmentSize)
          , m_baseChunkId(baseChunkId)
          , m_chunkSize(chunkSize)
          , m_nChunks(segmentSize / m_chunkSize) {
        }
        size_t getSize() const {
          return m_shMem.getSize();
        }
        size_t getNumChunks() const {
          return m_nChunks;
        }
        ChunkId getBaseChunkId() const {
          return m_baseChunkId;
        }
        BYTE* getBuf(const ChunkId chunkId) const {
          BYTE* const pSegBase = static_cast<BYTE*>(m_shMem.data());
          const auto segChunkId = chunkId - m_baseChunkId;
          BYTE* const pBuf = pSegBase + (segChunkId * m_chunkSize);
          return pBuf;
        }
      private:
        SharedMemory m_shMem;
        const ChunkId m_baseChunkId;
        const size_t m_chunkSize;
        const size_t m_nChunks;
      };
      std::vector<Segment> m_segments;
      std::map<ChunkId, Id> m_mapChunkToSeg;
    };
    static Instance& get() {
      static Instance inst;
      return inst;
    }
  };
}
