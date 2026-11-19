/*
* Copyright (c) 2026, NVIDIA CORPORATION. All rights reserved.
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

#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstring>

namespace dxvk {
  // The class provides an atomic value (lock-free) of up to 16 bytes, with a constraint on usage:
  // Only one writer thread at a time; any number of readers.
  // NOTE: 0-8 byte value is read with a single atomic load.
  //       9-16 byte value is read with two atomic loads, retried if a write overlapped.
  //       The class ensures that 9-16 byte values are not torn
  //       (stored as two atomic uint64, guarded by a writer sequence number).
  class LockFreeSingleWriterValue {
  public:
    // Callers serialize the writes.
    void write(const void* data, size_t size) {
      assert(size <= sizeof(m_words));
      uint64_t words[kWordCount] = {};
      memcpy(words, data, size);
      const uint32_t sequence = m_sequence.load(std::memory_order_relaxed);
      m_sequence.store(sequence + 1, std::memory_order_relaxed);
      std::atomic_thread_fence(std::memory_order_release);
      for (uint32_t i = 0; i < kWordCount; ++i) {
        m_words[i].store(words[i], std::memory_order_release);
      }
      m_sequence.store(sequence + 2, std::memory_order_release);
    }

    template<typename T>
    T read() const {
      static_assert(sizeof(T) <= sizeof(m_words), "The value does not fit the published words.");
      constexpr uint32_t wordCount = (sizeof(T) + sizeof(uint64_t) - 1) / sizeof(uint64_t);
      uint64_t words[wordCount];
      if constexpr (wordCount == 1) {
        words[0] = m_words[0].load(std::memory_order_acquire);
      } else {
        uint32_t sequence;
        do {
          sequence = m_sequence.load(std::memory_order_acquire);
          for (uint32_t i = 0; i < wordCount; ++i) {
            words[i] = m_words[i].load(std::memory_order_relaxed);
          }
          std::atomic_thread_fence(std::memory_order_acquire);
        } while ((sequence & 1) != 0 || sequence != m_sequence.load(std::memory_order_relaxed));
      }
      T value;
      memcpy(&value, words, sizeof(T));
      return value;
    }

  private:
    static_assert(std::atomic<uint64_t>::is_always_lock_free, "The published words must be lock-free.");
    // Two 8-byte words: values of up to 8 bytes (bool to Vector2i) need no sequence check.
    static constexpr uint32_t kWordCount = 2;
    std::atomic<uint64_t> m_words[kWordCount] = {};
    // Odd while a write is in progress
    std::atomic<uint32_t> m_sequence { 0 };
  };
}
