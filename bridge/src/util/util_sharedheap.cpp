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
#include "util_sharedheap.h"

#include "util_bytes.h"
#include "util_devicecommand.h"
#include "config/global_options.h"

#include <assert.h>

using namespace bridge_util;

using ChunkId = SharedHeap::ChunkId;

void SharedHeap::init() {
  static bool bIsInit = false;
  if (bIsInit) {
    assert(!"SharedHeap already initialized! An attempt to re-init has been made!");
    Logger::warn("SharedHeap already initialized! An attempt to re-init has been made!");
  }
  get();
  bIsInit = true;
}

SharedHeap::Instance::Instance()
  : m_chunkSize(GlobalOptions::getSharedHeapChunkSize())
  , m_defaultSegmentSize(GlobalOptions::getSharedHeapDefaultSegmentSize())
  , m_nChunks(0)
  , m_metaShMem("SharedHeap_meta", (kMax32BitHeapSize / m_chunkSize)) {
#ifdef REMIX_BRIDGE_CLIENT
  assert(GlobalOptions::getUseSharedHeap());
  assert(m_defaultSegmentSize % m_chunkSize == 0);
  for (ChunkId chunkId = 0; chunkId < (kMax32BitHeapSize / m_chunkSize); ++chunkId) {
    setChunkState(chunkId, ChunkState::Unallocated);
  }
  addNewHeapSegment();
  assert(m_segments.size() == 1);
#endif
}

BYTE* SharedHeap::Instance::getBuf(const AllocId id) {
  assert(m_cache.count(id) != 0);
  const auto firstChunk = m_cache[id];
  const auto segId = chunkIdToSegId(firstChunk);
  BYTE* const pBuf = m_segments[segId].getBuf(firstChunk);
  return pBuf;
}

size_t SharedHeap::Instance::getTotalHeapSize() const {
  size_t size = 0;
  for (const auto& shMem : m_segments) {
    size += shMem.getSize();
  }
  return size;
}

SharedHeap::Id SharedHeap::Instance::chunkIdToSegId(const ChunkId chunkId) const {
  for (Id segId = 0; segId < m_segments.size(); ++segId) {
    const auto& seg = m_segments[segId];
    const auto baseChunkId = seg.getBaseChunkId();
    if ((baseChunkId <= chunkId) &&
        (chunkId < baseChunkId + seg.getNumChunks())) {
      return segId;
    }
  }
  assert(!"chunkIdToSegId failed!");
  return -1;
}

#ifdef REMIX_BRIDGE_CLIENT
bool SharedHeap::Instance::addNewHeapSegment() {
  const std::string shMemName =
    std::string("SharedHeap_data_") + std::to_string(m_segments.size());
  bool bSuccess = false;
  const size_t segmentSizeUnaligned = std::min((kMax32BitHeapSize - getTotalHeapSize()), m_defaultSegmentSize);
  // Align segment size to chunk size
  size_t segmentSize = segmentSizeUnaligned & ~(m_chunkSize - 1);
  Logger::debug("[SharedHeap][addNewHeapSegment] Attempting to create new SharedHeap segment.");
  while (!bSuccess && (segmentSize > m_chunkSize)) {
    try {
      m_segments.emplace_back(shMemName, segmentSize, m_chunkSize, m_nChunks);
      bSuccess = true;
    }
    catch (const char* const errMsg) {
      Logger::debug(format_string(
        "[SharedHeap][addNewHeapSegment] Failed to create SharedHeap segment of size: %s",
        bridge_util::toByteUnitString(segmentSize).c_str()));
      segmentSize = segmentSize >> 1; // cut in half
    }
  }
  if (bSuccess) {
    {
      ClientMessage c(Commands::Bridge_SharedHeap_AddSeg, segmentSize);
    }
    Logger::debug(format_string(
      "[SharedHeap][addNewHeapSegment] Successfully allocated SharedHeap segment of size: %s",
      bridge_util::toByteUnitString(segmentSize).c_str()));
    const auto  newSegId = m_segments.size() - 1;
    const auto& newSeg = m_segments[newSegId];
    m_mapChunkToSeg[newSegId] = newSeg.getBaseChunkId();
    m_nChunks += newSeg.getNumChunks();
  } else {
    Logger::err("[SharedHeap][addNewHeapSegment] Failed to create new SharedHeap segment. Crash may be imminent.");
  }
  return bSuccess;
}
#endif
#ifdef REMIX_BRIDGE_SERVER
void SharedHeap::Instance::addNewHeapSegment(const uint32_t segmentSize) {
  bool bSuccess = false;
  try {
    const std::string shMemName =
      std::string("SharedHeap_data_") + std::to_string(m_segments.size());
    m_segments.emplace_back(shMemName, segmentSize, m_chunkSize, m_nChunks);
    bSuccess = true;
  }
  catch (const char* const errMsg) {
    Logger::err(format_string(
      "[SharedHeap][addNewHeapSegment] Failed to create SharedHeap segment of size: %s",
      bridge_util::toByteUnitString(segmentSize).c_str()));
  }
  if (bSuccess) {
    const auto newSegId = m_segments.size() - 1;
    const auto& newSeg = m_segments[newSegId];
    m_mapChunkToSeg[newSeg.getBaseChunkId()] = newSegId;
    m_nChunks += newSeg.getNumChunks();
  }
}
#endif

#ifdef REMIX_BRIDGE_CLIENT
SharedHeap::AllocId SharedHeap::Instance::allocate(const size_t size) {
  if (size > m_defaultSegmentSize) {
    size_t newDefaultSegmentSize = m_defaultSegmentSize;
    while (size > newDefaultSegmentSize) {
      newDefaultSegmentSize = newDefaultSegmentSize << 1;
    }
    std::stringstream ss;
    ss << "[SharedHeap][allocate] Allocating size: ";
    ss << bridge_util::toByteUnitString(size);
    ss << ", which is larger than current default segment size: ";
    ss << bridge_util::toByteUnitString(m_defaultSegmentSize);
    ss << ". New default segment size: ";
    ss << bridge_util::toByteUnitString(newDefaultSegmentSize);
    Logger::warn(ss.str());
    m_defaultSegmentSize = newDefaultSegmentSize;
  }
  // Resolve the number of chunks we need to allocate
  const uint32_t numChunks =
    ((size % m_chunkSize) == 0) ? (size / m_chunkSize) : (size / m_chunkSize + 1);

  const auto alloc = findAllocation(numChunks);
  assert(isValidAllocation(alloc));
  if (!isValidAllocation(alloc)) {
    std::stringstream ss;
    ss << "[SharedHeap][allocate] Failed allocation. Size: ";
    ss << bridge_util::toByteUnitString(size);
    Logger::err(ss.str());
    return kInvalidId;
  }

  const auto id = m_nextUid++;
  m_cache[id] = alloc.firstChunk;
  m_allocations[alloc.firstChunk] = alloc.finalChunk;
  {
    ClientMessage c(Commands::Bridge_SharedHeap_Alloc, id);
    c.send_data(alloc.firstChunk);
  }

  assert(getChunkState(alloc.firstChunk) == ChunkState::Unallocated);
  setChunkState(alloc.firstChunk, ChunkState::Allocated);

  const size_t sizeAllocated = numChunks * m_chunkSize;
  m_sizeAllocated += sizeAllocated;
#ifdef _DEBUG
  memset(getBuf(id), 0, sizeAllocated);
#endif
  return id;
}
void SharedHeap::Instance::deallocate(const AllocId id) {
  ClientMessage c(Commands::Bridge_SharedHeap_Dealloc, id);
}
#endif

#ifdef REMIX_BRIDGE_SERVER
void SharedHeap::Instance::allocate(const AllocId id, const ChunkId firstChunk) {
  m_cache[id] = firstChunk;
}
void SharedHeap::Instance::deallocate(const AllocId id) {
  assert(m_cache.count(id) != 0);
  const auto firstChunk = m_cache[id];
  assert(getChunkState(firstChunk) == ChunkState::Allocated);
  setChunkState(firstChunk, ChunkState::Deallocated);
  m_cache.erase(id);
}
#endif

#ifdef REMIX_BRIDGE_CLIENT
SharedHeap::Instance::Allocation
SharedHeap::Instance::findAllocation(const size_t numChunks) {
  Allocation alloc;
  const bool bFirstAllocation = m_allocations.size() == 0; // Trivial case
  if (bFirstAllocation) {
    return createAllocation(0, numChunks);
  } else {
    size_t nFailedIterations = 0;
    bool bTimedOut = false;
    const auto timeoutStart = GetTickCount64();
    do {
      if (isValidAllocation(alloc = findFreeInMiddle(numChunks)) ||
          isValidAllocation(alloc = findFreeOnEnd(numChunks))) {
        break;
      }
      if (nFailedIterations == 1) {
        std::stringstream ss;
        ss << "[SharedHeap][findAllocation] Unable to allocate ";
        ss << bridge_util::toByteUnitString(numChunks * m_chunkSize);
        ss << ". Will continue retrying until timeout...";
        Logger::warn(ss.str());
      }
      freeDeallocations();
      constexpr size_t kAttemptIncrease = 2;
      if (nFailedIterations == kAttemptIncrease) {
        Logger::info("[SharedHeap][findAllocation] Attempting to increase SharedHeap size.");
        if (addNewHeapSegment()) {
          const auto firstChunkNewHeap = m_segments[m_segments.size() - 1].getBaseChunkId();
          alloc = createAllocation(firstChunkNewHeap, numChunks);
          Logger::info("[SharedHeap][findAllocation] Allocating at beginning of new segment.");
          break;
        } else {
          Logger::err("[SharedHeap][findAllocation] Failed to increase SharedHeap size.");
        }
      }
      const auto dt = GetTickCount64() - timeoutStart;
      bTimedOut =
        dt / 1000 >= GlobalOptions::getSharedHeapFreeChunkWaitTimeout();
      nFailedIterations++;
    } while (!bTimedOut);
    if (bTimedOut) {
      Logger::err("[SharedHeap][findAllocation] Timeout!");
#ifdef SHARED_HEAP_DIAG
      dumpState();
#endif
      return { kInvalidId, kInvalidId };
    }
  }
  return alloc;
}

SharedHeap::Instance::Allocation
SharedHeap::Instance::findFreeInMiddle(const size_t numChunks) {
  assert(m_allocations.size() > 0);
  // Seek for internally fragmented strip of chunks
  ChunkId prevAllocatedFinalChunk = (ChunkId) -1;
  for (const auto [allocatedFirstChunk, allocatedFinalChunk] : m_allocations) {
    const auto potentiallyFreeFirstChunk = prevAllocatedFinalChunk + 1;
    if (potentiallyFreeFirstChunk < allocatedFirstChunk) {
      const size_t numChunksFound = allocatedFirstChunk - potentiallyFreeFirstChunk;
      if (numChunksFound >= numChunks) {
        const auto foundAlloc = createAllocation(potentiallyFreeFirstChunk, numChunks);
        return foundAlloc;
      }
    }
    prevAllocatedFinalChunk = allocatedFinalChunk;
  }
  // Sanity check that we successfully iterated through all allocations
  assert((--m_allocations.cend())->second == prevAllocatedFinalChunk);
  return { kInvalidId, kInvalidId };
}

SharedHeap::Instance::Allocation
SharedHeap::Instance::findFreeOnEnd(const size_t numChunks) {
  assert(m_allocations.size() > 0);
  // Snag last allocated chunk + 1
  const auto firstFreeEndChunk = (--m_allocations.cend())->second + 1;
  return createAllocation(firstFreeEndChunk, numChunks);
}

void SharedHeap::Instance::freeDeallocations() {
  std::vector<AllocId> deallocatedIds;
  for (const auto [id, firstChunk] : m_cache) {
    if (getChunkState(firstChunk) == ChunkState::Deallocated) {
      deallocatedIds.push_back(id);
    }
  }
  for (const auto deallocatedId : deallocatedIds) {
    const auto firstChunk = m_cache[deallocatedId];
    m_cache.erase(deallocatedId);
    assert(m_allocations.count(firstChunk) > 0);
    const auto finalChunk = m_allocations[firstChunk];
    const size_t numChunks = finalChunk - firstChunk + 1;
    m_allocations.erase(firstChunk);
    setChunkState(firstChunk, ChunkState::Unallocated);
    m_sizeAllocated -= numChunks * m_chunkSize;
  }
}

bool SharedHeap::Instance::isValidAllocation(const Allocation& alloc) {
  if (alloc.firstChunk >= m_nChunks ||
      alloc.finalChunk >= m_nChunks ||
      alloc.finalChunk < alloc.firstChunk ||
      allocationCrossesHeapSegBound(alloc)) {
    return false;
  } else {
    return true;
  }
}

bool SharedHeap::Instance::allocationCrossesHeapSegBound(const Allocation& alloc) {
  const auto firstSegId = chunkIdToSegId(alloc.firstChunk);
  const auto finalSegId = chunkIdToSegId(alloc.finalChunk);
  assert(firstSegId <= finalSegId);
  const bool bDiffSegs = firstSegId != finalSegId;
  return bDiffSegs;
}
#endif

void SharedHeap::Instance::setChunkState(const ChunkId& chunkId, const ChunkState state) {
  std::atomic<uint8_t>* const pHeapBase =
    static_cast<std::atomic<uint8_t>*>(m_metaShMem.data());
  pHeapBase[chunkId].store(
    (uint8_t) state, std::memory_order::memory_order_seq_cst);
}

SharedHeap::Instance::ChunkState SharedHeap::Instance::getChunkState(const ChunkId& chunkId) const {
  auto* const pHeapBase = static_cast<std::atomic<uint8_t>*>(m_metaShMem.data());
  return (ChunkState) pHeapBase[chunkId].load(std::memory_order::memory_order_seq_cst);
}

#ifdef SHARED_HEAP_DIAG
#define SS_NEWL(MORE_SS_ARGS) ss << MORE_SS_ARGS << std::endl;
#define SS_ADD(MORE_SS_ARGS) ss << MORE_SS_ARGS;

void SharedHeap::dumpState() {
  std::stringstream ss;
  SS_NEWL("");
  SS_NEWL("============================");
  SS_NEWL("| Dumping SharedHeap State |");
  SS_NEWL("============================");
  SS_NEWL("");
  dumpHeapViz(ss);
  dumpHeapFragmentation(ss);
  Logger::err(ss.str());
}
void SharedHeap::dumpHeapViz(std::stringstream& ss) {
#ifdef REMIX_BRIDGE_CLIENT
  SS_NEWL("Viz:");
  static constexpr size_t kMaxPerLine = 128;
  ChunkId prevFinalChunk = (ChunkId) -1;
  for (const auto [firstChunk, finalChunk] : m_allocations) {
    for (ChunkId preFirstChunkPos = prevFinalChunk + 1;
        preFirstChunkPos < firstChunk;
        ++preFirstChunkPos) {
      if (preFirstChunkPos % kMaxPerLine == 0 && preFirstChunkPos != 0) {
        SS_NEWL("");
      }
      SS_ADD("U");
    }

    const char allocatedChar = (getChunkState(firstChunk) == ChunkState::Allocated) ? 'A' : 'D';
    for (ChunkId allocPos = firstChunk;
        allocPos <= finalChunk;
        ++allocPos) {
      if (allocPos % kMaxPerLine == 0 && allocPos != 0) {
        SS_NEWL("");
      }
      SS_ADD(allocatedChar);
    }
    prevFinalChunk = finalChunk;
  }
  SS_NEWL("");
  SS_NEWL("");
#endif
}
void SharedHeap::dumpHeapFragmentation(std::stringstream& ss) {
#ifdef REMIX_BRIDGE_CLIENT
  SS_NEWL("Fragmentation:");
  ChunkId prevFinalChunk = (ChunkId) -1;
  size_t runningBytesAllocated = 0;
  size_t largestContiguousUnallocated = 0;
  size_t totalUnallocated = 0;
  for (const auto [firstChunk, finalChunk] : m_allocations) {
    ChunkId preFirstChunkPos = prevFinalChunk + 1;
    if (preFirstChunkPos < firstChunk) {
      if (runningBytesAllocated > 0) {
        SS_NEWL("  Allocated:   " << bridge_util::toByteUnitString(runningBytesAllocated));
        runningBytesAllocated = 0;
      }
      const size_t numBytesUnallocated = (firstChunk - preFirstChunkPos) * m_chunkSize;
      SS_NEWL("  Unallocated: " << bridge_util::toByteUnitString(numBytesUnallocated));
      if (numBytesUnallocated > largestContiguousUnallocated) {
        largestContiguousUnallocated = numBytesUnallocated;
      }
      totalUnallocated += numBytesUnallocated;
    }
    runningBytesAllocated += (finalChunk - firstChunk + 1) * m_chunkSize;
    prevFinalChunk = finalChunk;
  }
  SS_NEWL("");
  SS_NEWL("  Total Unallocated:            " << bridge_util::toByteUnitString(totalUnallocated));
  SS_NEWL("  Largest block of unallocated: " << bridge_util::toByteUnitString(largestContiguousUnallocated));
  // Follows simple formula from: https://stackoverflow.com/a/4587077
  const double fragPercent = ((double) (totalUnallocated - largestContiguousUnallocated) / (double) (totalUnallocated)) * 100;
  SS_NEWL("  % fragmented:                 " << fragPercent << "%");
  SS_NEWL("");
#endif
}

#undef SS_NEWL
#undef SS_ADD
#endif
