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
#ifndef UTIL_CIRCULARQUEUE_H_
#define UTIL_CIRCULARQUEUE_H_

#include <cstdio>
#include <type_traits>

#include "util_common.h"

namespace bridge_util {

  enum class Accessor {
    Reader,
    Writer
  };
#define IS_READER(ACCESSOR) (ACCESSOR == bridge_util::Accessor::Reader)
#define IS_WRITER(ACCESSOR) (ACCESSOR == bridge_util::Accessor::Writer)

  template<typename T>
  class CircularQueue {
    static_assert(std::is_integral_v<T>, "Queue data type must be integral.");
  protected:
    T* m_data;

    const std::string m_name;
    const size_t m_size;
    const size_t m_queueSize;
    const Accessor m_access;

    size_t m_pos = 0;
    size_t m_batchSize = 0;

    bool m_batchInProgress = false;

  public:
    CircularQueue(const std::string& name, Accessor access, void* pMemory, const size_t memSize, const size_t queueSize)
      : m_name(name)
      , m_size(memSize / sizeof(T))
      , m_access(access)
      , m_queueSize(queueSize)
      , m_data(nullptr) // INIT
    {
      // Writers own the memory, Readers are consumers
      if (access == Accessor::Reader) {
        m_data = (T*) pMemory;
      } else if (access == Accessor::Writer) {
        m_data = (new (pMemory) T[m_size]);
      }
    }

    CircularQueue(const CircularQueue& q) = delete;

    ~CircularQueue() {
    }

    const std::string& getName() {
      return m_name;
    }

    // Push object to queue
    Result push(const T obj) {
      if (m_batchInProgress) {
        return pushImpl<true>(obj);
      }
      return pushImpl<false>(obj);
    }

    template<typename... Ts>
    Result push_many(const Ts... objs) {
      constexpr size_t count = sizeof...(Ts);
      if (m_batchInProgress) {
        m_batchSize += count;
      }

      // Using > instead of >= so if m_pos needs to wrap around it is done by pushImpl()
      if (m_size - m_pos > count) {
        // This is a likely condition: construct param array right in the m_data at m_pos
        new (m_data + m_pos) std::array{ static_cast<T>(objs)... };
        m_pos += count;
      } else {
        // Worst case at a boundary: do fold
        (pushImpl<false>(static_cast<T>(objs)), ...);
      }

      // TODO: fix me. At the moment push() cannot fail, and neither can push_many()
      return Result::Success;
    }

    // Returns a ref to the first element in the queue
    // Note: May be stale data!
    const T& peek() {
      const T& data = m_data[m_pos];
      return data;
    }

    // Removes an object from the queue
    Result pop() {
      m_pos = m_pos + 1 < m_size ? m_pos + 1 : 0;
      return Result::Success;
    }

    // Returns a copy to the first element in queue, AND removes it
    const T& pull() {
      const T& retval = m_data[m_pos];
      pop();
      return retval;
    }

    Result begin_batch() {
      if (m_batchInProgress) {
        throw std::exception("Cannot start a new batch while one is already in progress!");
      }

      m_batchInProgress = true;
      m_batchSize = 0;
      return Result::Success;
    }

    size_t end_batch() {
      if (!m_batchInProgress) {
#ifdef ENABLE_DATA_BATCHING_TRACE
        Logger::trace("Cannot end a batch when none is currently in progress!");
#endif
        return 0;
      }

      const auto batchSize = m_batchSize;
      m_batchInProgress = false;
      m_batchSize = 0;
      return batchSize;
    }

    T* data() const {
      return m_data;
    }

  private:
    template<bool BatchInProgress>
    Result pushImpl(const T obj) {
      if (BatchInProgress) {
        m_batchSize++;
      }
      m_data[m_pos] = obj;
      return pop();
    }
  };
}

#endif // UTIL_CIRCULARQUEUE_H_
