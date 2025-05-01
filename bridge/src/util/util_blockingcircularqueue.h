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
#ifndef UTIL_BLOCKINGCIRCULARQUEUE_H_
#define UTIL_BLOCKINGCIRCULARQUEUE_H_

#include <cstdio>
#include <mutex>

#include "util_semaphore.h"
#include "util_circularqueue.h"
#include "../tracy/tracy.hpp"

namespace bridge_util {

  // Intra/Inter-process thread safe, shared circular queue.
  // Constructed from a shared pool of memory - and synchronized using named semaphores for IPC.
  template<typename T, bridge_util::Accessor Accessor>
  class BlockingCircularQueue: public CircularQueue<T> {
    NamedSemaphore m_write, m_read;
    T m_default;
  public:
    static size_t getExtraMemoryRequirements() {
      return 0;
    }

    BlockingCircularQueue(const std::string& name, void* pMemory, const size_t memSize, const size_t queueSize)
      : CircularQueue(name, Accessor, pMemory, memSize, queueSize)
      , m_write(("Circular_Write_" + name).c_str(), queueSize, queueSize)
      , m_read(("Circular_Read_" + name).c_str(), 0, queueSize) {
    }

    BlockingCircularQueue(const BlockingCircularQueue& q) = delete;

    ~BlockingCircularQueue() {
    }

    bool empty() const {
      ZoneScoped;
      return !m_read.available();
    }
    bool full() const {
      ZoneScoped;
      return !m_write.available();
    }

    // Push object to queue
    // Note: Blocks if queue is full
    virtual Result push(const T& obj) override {
      ZoneScoped;
      auto result = wait_on_writer();
      if (RESULT_FAILURE(result)) {
        return result;
      }

      result = CircularQueue<T>::push(obj);
      release_reader();
      return result;
    }

    // Does nothing but wait for the next command to come in
    Result try_peek(const DWORD timeoutMS = 0) {
      ZoneScoped;
      const auto result = wait_on_reader(timeoutMS);
      if (RESULT_FAILURE(result)) {
        return result;
      }

      release_reader();
      return result;
    }

    // Returns a ref to the first element in the queue
    // Note: Blocks if the queue is empty
    const T& peek(Result& result, const DWORD timeoutMS = 0) {
      ZoneScoped;
      result = wait_on_reader(timeoutMS);
      const T& data = CircularQueue<T>::peek();
      if (RESULT_FAILURE(result)) {
        return data;
      }

      release_reader();
      return data;
    }

    // Removes object from queue
    // Note: Blocks if the queue is empty
    Result pop(const DWORD timeoutMS = 0) {
      ZoneScoped;
      const auto result = wait_on_reader(timeoutMS);
      if (RESULT_FAILURE(result)) {
        return result;
      }

      result = CircularQueue<T>::pop();
      release_writer();
      return result;
    }

    // Returns a copy to the first element in queue, AND removes it
    // Note: Blocks if queue is empty
    const T& pull(Result& result, const DWORD timeoutMS = 0) {
      ZoneScoped;
      result = wait_on_reader(timeoutMS);
      if (RESULT_FAILURE(result)) {
        return m_default;
      }

      const T& retval = CircularQueue<T>::pull();
      release_writer();
      return retval;
    }

    Result begin_write_batch() {
      ZoneScoped;
      return begin_batch(true);
    }

    size_t end_write_batch() {
      ZoneScoped;
      return end_batch(true);
    }

    Result begin_read_batch() {
      ZoneScoped;
      return begin_batch(false);
    }

    size_t end_read_batch() {
      return end_batch(false);
    }

  private:
    Result begin_batch(bool isWriteBatch) {
      ZoneScoped;
      const auto result = isWriteBatch ? wait_on_writer() : wait_on_reader();
      if (RESULT_SUCCESS(result)) {
        result = CircularQueue<T>::begin_batch();
      }
      return result;
    }

    size_t end_batch(bool isWriteBatch) {
      if (!m_batchInProgress) {
#ifdef ENABLE_DATA_BATCHING_TRACE
        Logger::trace("Cannot end a batch when none is currently in progress!");
#endif
        return 0;
      }

      if (m_batchSize == 0) {
        // The batch is empty so go ahead and release the writer again,
        // and no need to trigger the reader either.
        isWriteBatch ? release_writer() : release_reader();
      } else {
        isWriteBatch ? release_reader() : release_writer();
      }

      const auto batchSize = CircularQueue<T>::end_batch();
      return batchSize;
    }

    Result wait_on_writer() {
      ZoneScoped;
      if (m_batchInProgress) {
        return Result::Success;
      } else if (m_queueSize == 0) {
        return Result::Success;
      } else {
        return m_write.wait();
      }
    }

    void release_writer() {
      ZoneScoped;
      if (m_queueSize > 0) {
        m_write.release();
      }
    }

    Result wait_on_reader(DWORD timeoutMS = 0) {
      ZoneScoped;
      if (m_batchInProgress) {
        return Result::Success;
      } else if (m_queueSize == 0) {
        return Result::Success;
      } else if (timeoutMS == 0) {
        return m_read.wait();
      } else {
        return m_read.wait(timeoutMS);
      }
    }

    void release_reader(size_t batchSize = 1) {
      ZoneScoped;
      if (m_queueSize > 0) {
        m_read.release(batchSize);
      }
    }
  };
}

#endif // UTIL_BLOCKINGCIRCULARQUEUE_H_
