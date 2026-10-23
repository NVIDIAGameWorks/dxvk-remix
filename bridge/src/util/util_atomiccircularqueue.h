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
#include "util_perfcounters.h"

#include "../tracy/tracy.hpp"

#include <cstdio>
#include <atomic>
#include <assert.h>
#include <thread>
#include <vector>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace bridge_util {

  // Intra/Inter-process thread safe, shared circular queue.
  // Constructed from a shared pool of memory - and synchronized using static atomics
  // Single Producer, Single Consumer ONLY!
  // A waiting side spins, then sets its sleeping flag and blocks on an event that the other side signals
  // only while that flag is set. Seq_cst fences on both sides keep a wake-up from being lost.
  template<typename T, bridge_util::Accessor Accessor>
  class AtomicCircularQueue {
    // m_write holds the consumer's position and m_read holds the producer's position.
    std::atomic<uint32_t>* m_write;
    std::atomic<uint32_t>* m_read;

    std::atomic<uint32_t>* m_readerSleeping;
    std::atomic<uint32_t>* m_writerSleeping;

    T* m_data;
    T m_default;

    const size_t m_queueSize;

    HANDLE m_hDataAvailable = nullptr;
    HANDLE m_hSpaceAvailable = nullptr;
    int64_t m_spinTicks = 0;

    QueueWaitStats m_fullStats;
    QueueWaitStats m_emptyStats;

    static const size_t kAlignment = 128;
    static const size_t kWriteAtomicOffset = 0;
    static const size_t kReadAtomicOffset = kAlignment + kWriteAtomicOffset;
    static const size_t kReaderSleepingOffset = kAlignment + kReadAtomicOffset;
    static const size_t kWriterSleepingOffset = kAlignment + kReaderSleepingOffset;
    static const size_t kMemoryPoolOffset = kAlignment + kWriterSleepingOffset;

    // Blocking waits are sliced so that early-out signals, timeouts and wake-ups lost during init are still seen.
    static constexpr DWORD kSleepSliceMs = 2;

  public:
    static size_t getExtraMemoryRequirements() {
      return kMemoryPoolOffset;
    }

    AtomicCircularQueue(const std::string& name, void* pMemory, const size_t memSize, const size_t queueSize)
      : m_queueSize(queueSize)
      , m_data(nullptr) // INIT
    {
      // Ensure we have enough memory
      assert(memSize > kMemoryPoolOffset);
      assert(memSize - kMemoryPoolOffset >= queueSize * sizeof(T));

      // Writers own the memory, Readers are consumers
      if constexpr(IS_READER(Accessor)) {
        m_data = (T*) ((uintptr_t) pMemory + kMemoryPoolOffset);
        m_write = (std::atomic<uint32_t>*)((uintptr_t) pMemory + kWriteAtomicOffset);
        m_read = (std::atomic<uint32_t>*)((uintptr_t) pMemory + kReadAtomicOffset);
        m_readerSleeping = (std::atomic<uint32_t>*)((uintptr_t) pMemory + kReaderSleepingOffset);
        m_writerSleeping = (std::atomic<uint32_t>*)((uintptr_t) pMemory + kWriterSleepingOffset);
      } else if constexpr (IS_WRITER(Accessor)) {
        m_data = new((void*) ((uintptr_t) pMemory + kMemoryPoolOffset)) T[m_queueSize];
        m_write = new((void*) ((uintptr_t) pMemory + kWriteAtomicOffset)) std::atomic<uint32_t>(0);
        m_read = new((void*) ((uintptr_t) pMemory + kReadAtomicOffset)) std::atomic<uint32_t>(0);
        m_readerSleeping = new((void*) ((uintptr_t) pMemory + kReaderSleepingOffset)) std::atomic<uint32_t>(0);
        m_writerSleeping = new((void*) ((uintptr_t) pMemory + kWriterSleepingOffset)) std::atomic<uint32_t>(0);
      }

      assert(m_read->is_lock_free() && m_write->is_lock_free()); // Must be runtime check as it's CPU specific

      LARGE_INTEGER frequency;
      QueryPerformanceFrequency(&frequency);
      m_spinTicks = int64_t(double(frequency.QuadPart) * double(GlobalOptions::getIpcSpinMicroseconds()) / 1e6);
    }

    AtomicCircularQueue(const AtomicCircularQueue& q) = delete;

    ~AtomicCircularQueue() {
    }

    void attachWakeEvents(HANDLE hDataAvailable, HANDLE hSpaceAvailable) {
      m_hDataAvailable = hDataAvailable;
      m_hSpaceAvailable = hSpaceAvailable;
    }

    const QueueWaitStats& fullWaitStats() const {
      return m_fullStats;
    }

    const QueueWaitStats& emptyWaitStats() const {
      return m_emptyStats;
    }

    // Push object to queue
    Result push(const T& obj) {
      const auto currentRead = m_read->load(std::memory_order_relaxed);
      const auto nextRead = queueIdxInc(currentRead);
      const auto hasRoom = [&]() { return nextRead != m_write->load(std::memory_order_acquire); };
      if (!hasRoom() &&
          !waitUntil(hasRoom, m_writerSleeping, m_hSpaceAvailable, GlobalOptions::getCommandTimeout(), nullptr, m_fullStats)) {
        return Result::Failure;
      }
      m_data[currentRead] = obj;
      // The store above is not atomic. Issue a membar after it to ensure
      // it is not reordered.
      std::atomic_thread_fence(std::memory_order_seq_cst);
      m_read->store(nextRead, std::memory_order_release);
      wakeIfSleeping(m_readerSleeping, m_hDataAvailable, m_fullStats);
      return Result::Success;
    }

    // Does nothing but wait for the next command to come in
    Result try_peek(const DWORD timeoutMS = 0) {
      return Result::Success;
    }

    // Returns a ref to the first element in the queue
    // Note: Blocks if the queue is empty
    const T& peek(Result& result, const DWORD timeoutMS = 0, std::atomic<bool>* const pbEarlyOutSignal = nullptr) {
      if (!waitForData(timeoutMS, pbEarlyOutSignal)) {
        result = Result::Timeout;
        return m_default;
      }
      // Issue a membar before reading the data since it is not atomic
      std::atomic_thread_fence(std::memory_order_seq_cst);
      result = Result::Success;
      return m_data[m_write->load(std::memory_order_acquire)];
    }

    // Returns a copy to the first element in queue, AND removes it
    // Note: Blocks if queue is empty
    T pull(Result& result, const DWORD timeoutMS = 0, std::atomic<bool>* const pbEarlyOutSignal = nullptr) {
      if (!waitForData(timeoutMS, pbEarlyOutSignal)) {
        result = Result::Timeout;
        return m_default;
      }
      const auto currentWrite = m_write->load(std::memory_order_relaxed);
      // Issue a membar before reading the data since it is not atomic
      std::atomic_thread_fence(std::memory_order_seq_cst);
      // The producer may overwrite the slot once the index moves, so it is copied first.
      const T pulled = m_data[currentWrite];
      m_write->store(queueIdxInc(currentWrite), std::memory_order_release);
      wakeIfSleeping(m_writerSleeping, m_hSpaceAvailable, m_emptyStats);
      result = Result::Success;
      return pulled;
    }

    // Check for queue emptiness. The function may guarantee a correct result
    // ONLY when the queue is stalled on either end and only a single index
    // is advancing.
    bool isEmpty() const {
      const auto currentWrite = m_write->load(std::memory_order_relaxed);
      return currentWrite == m_read->load(std::memory_order_acquire);
    }

    std::vector<Commands::D3D9Command> buildQueueData(int maxQueueElements, int currentIndex) {
      std::vector<Commands::D3D9Command> commandHistory;
      int itemCount = 0;
      while (itemCount < m_queueSize && itemCount < maxQueueElements) {
        // To prevent adding default commands in the Queue to the command list
        if (m_data[currentIndex].command == Commands::Bridge_Invalid)
          break;
        commandHistory.push_back(m_data[currentIndex].command);
        currentIndex = queueIdxDec(currentIndex);
        ++itemCount;
      }
      return commandHistory;
    }

    std::vector<Commands::D3D9Command> getWriterQueueData(int maxQueueElements=10) {
      const auto currentRead = m_read->load(std::memory_order_relaxed);
      int currentIndex = queueIdxDec(currentRead);
      return buildQueueData(maxQueueElements, currentIndex);
    }

    std::vector<Commands::D3D9Command> getReaderQueueData(int maxQueueElements = 10) {
      const auto currentWrite = m_write->load(std::memory_order_relaxed);
      int currentIndex = queueIdxDec(currentWrite);
      return buildQueueData(maxQueueElements, currentIndex);
    }

    uint32_t queueIdxInc(uint32_t idx) const {
      return idx + 1 < m_queueSize ? idx + 1 : 0;
    }

    uint32_t queueIdxDec(uint32_t idx) const {
      return idx == 0 ?  m_queueSize - 1 : idx - 1 ;
    }

  private:
    bool waitForData(const DWORD timeoutMS, std::atomic<bool>* const pbEarlyOutSignal) {
      const auto hasData = [&]() {
        return m_write->load(std::memory_order_relaxed) != m_read->load(std::memory_order_acquire);
      };
      return hasData() || waitUntil(hasData, m_readerSleeping, m_hDataAvailable, timeoutMS, pbEarlyOutSignal, m_emptyStats);
    }

    // The fence orders the caller's index store before the sleeping flag load.
    void wakeIfSleeping(std::atomic<uint32_t>* sleeping, HANDLE hEvent, QueueWaitStats& stats) {
      std::atomic_thread_fence(std::memory_order_seq_cst);
      if (hEvent != nullptr && sleeping->load(std::memory_order_relaxed)) {
        SetEvent(hEvent);
        stats.wakesSent.fetch_add(1, std::memory_order_relaxed);
      }
    }

    // A timeoutMS of 0 waits without a time limit.
    template<typename Ready>
    bool waitUntil(const Ready& ready, std::atomic<uint32_t>* sleeping, HANDLE hEvent, const DWORD timeoutMS,
                   std::atomic<bool>* const pbEarlyOutSignal, QueueWaitStats& stats) {
      ZoneScoped;
      stats.waits.fetch_add(1, std::memory_order_relaxed);
      const ULONGLONG startTick = GetTickCount64();
      const auto timedOut = [&]() {
        return timeoutMS != 0 && GetTickCount64() - startTick >= timeoutMS;
      };
      const auto earlyOut = [&]() {
        return pbEarlyOutSignal != nullptr && pbEarlyOutSignal->load();
      };

      LARGE_INTEGER spinStart, now;
      QueryPerformanceCounter(&spinStart);
      now = spinStart;
      uint32_t iteration = 0;
      while (true) {
        YieldProcessor();
        if (ready()) {
          QueryPerformanceCounter(&now);
          stats.spinTicks.fetch_add(uint64_t(now.QuadPart - spinStart.QuadPart), std::memory_order_relaxed);
          return true;
        }
        if ((++iteration & 63) == 0) {
          QueryPerformanceCounter(&now);
          if (now.QuadPart - spinStart.QuadPart >= m_spinTicks) {
            break;
          }
          std::this_thread::yield();
          if (earlyOut()) {
            stats.spinTicks.fetch_add(uint64_t(now.QuadPart - spinStart.QuadPart), std::memory_order_relaxed);
            return false;
          }
        }
      }
      stats.spinTicks.fetch_add(uint64_t(now.QuadPart - spinStart.QuadPart), std::memory_order_relaxed);

      const LARGE_INTEGER blockStart = now;
      bool success = false;
      while (true) {
        if (hEvent != nullptr) {
          sleeping->store(1, std::memory_order_relaxed);
          std::atomic_thread_fence(std::memory_order_seq_cst);
          if (ready()) {
            sleeping->store(0, std::memory_order_relaxed);
            success = true;
            break;
          }
          stats.blocks.fetch_add(1, std::memory_order_relaxed);
          WaitForSingleObject(hEvent, kSleepSliceMs);
          sleeping->store(0, std::memory_order_relaxed);
        } else {
          std::this_thread::yield();
        }
        if (ready()) {
          success = true;
          break;
        }
        if (earlyOut() || timedOut()) {
          break;
        }
      }
      QueryPerformanceCounter(&now);
      stats.blockTicks.fetch_add(uint64_t(now.QuadPart - blockStart.QuadPart), std::memory_order_relaxed);
      return success;
    }
  };

}