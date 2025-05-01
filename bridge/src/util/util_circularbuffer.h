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
#ifndef UTIL_CIRCULARBUFFER_H_
#define UTIL_CIRCULARBUFFER_H_

#include <cstdio>
#include <mutex>
#include <string>

#include "util_circularqueue.h"
#include "log/log_strings.h"
#include "log/log.h"

namespace bridge_util {

  template<typename T>
  class CircularBuffer: public CircularQueue<T> {
  public:
    using CircularQueue::push;
    using CircularQueue::pull;
    using BaseType = T;

    CircularBuffer(const std::string& name, Accessor access, void* pMemory,
      const size_t memSize, const size_t queueSize):
      CircularQueue(name, access, pMemory, memSize, queueSize) {
    }

    CircularBuffer(const CircularBuffer& q) = delete;

    ~CircularBuffer() {
    }

    template<typename DataType>
    Result begin_blob_push(const size_t size, DataType*& blobPtr) {
      auto result = push(static_cast<T>(size));

      if (result == Result::Success) {
        if (const size_t ensured_space = ensure_space(size)) {
          blobPtr = reinterpret_cast<DataType*>(m_data + m_pos);
          advance<true>(ensured_space);
          return Result::Success;
        }
      }
      return Result::Failure;
    }

    void end_blob_push() {
      // Nothing to do. Left for future checks.
    }

    // Push object of variable size to queue
    // First writes size of object then actual bytes
    Result push(const size_t size, const void* obj) {
      if (obj) {
        auto result = push(static_cast<T>(size));

        if (result == Result::Success) {
          const size_t ensured_space = ensure_space(size);
          // memcpy_s() is redundant due to ensure_space()
          memcpy(m_data + m_pos, obj, size);
          advance<true>(ensured_space);
        }
        return result;
      } else {
        auto result = push(0);

        if (result == Result::Success)
          return result;
      }
      return Result::Failure;
    }

    // Returns the size of the variable size object and sets the pointer
    // to the beginning of the object.
    const T& pull(void** obj) {
      const T& size = pull();

      if (const size_t ensured_space = ensure_space(size)) {
        *obj = &m_data[m_pos];
        advance<true>(ensured_space);
      } else {
        *obj = nullptr;
      }

      return size;
    }

    // Returns the size of the variable size object and copies the
    // data into the given destination object.
    template<typename V>
    const T& pull_and_copy(V& obj) {
      const T& size = pull();

      if (const size_t ensured_space = ensure_space(size)) {
        obj = reinterpret_cast<V&>(m_data[m_pos]);
        advance<true>(ensured_space);
      } else {
        obj = V();
      }

      return size;
    }

    size_t get_total_size() const {
      return m_size;
    }

    size_t get_pos() const {
      return m_pos;
    }

  private:
    FORCEINLINE size_t ensure_space(size_t size) {
      size_t space_needed = chunk_size(size);
      if (m_pos + space_needed >= m_size) {
        if (space_needed > m_size) {
          // FATAL Condition, Message User and Exit
          Logger::errLogMessageBoxAndExit(std::string(logger_strings::OutOfBufferMemory) + std::string(logger_strings::OutOfBufferMemory1) + logger_strings::bufferNameToOption(m_name));
        }
        // Roll over immediately if not enough space left
        m_pos = 0;
      }
      return space_needed;
    }

    template<bool space_ensured>
    inline void advance(size_t step) {
      m_pos += step;

      if (!space_ensured) {
        m_pos %= m_size;
      }
    }

    constexpr size_t chunk_size(const size_t size) const {
      return align(size, sizeof(T)) / sizeof(T);
    }
  };

  typedef CircularBuffer<uint32_t> DataQueue;
}

#endif // UTIL_CIRCULARBUFFER_H_
