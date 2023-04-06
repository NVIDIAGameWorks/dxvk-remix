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
#include <atomic>
#include <cstring>
#include <utility>

namespace dxvk {
  /**
    * \brief Implements a (SPSC) queue with similar functionality to STL.
    *        Not implemented as a linked list, but  as a ring buffer 
    *        of fixed size to avoid runtime alloc overhead.
    *        Since this object is SPSC, only a single thread may "push",
    *        while another thread may "pop" simultaneously.
    *  T: Type of the object
    *  Capacity: Number of elements in the ring buffer.
    */
  template <typename T, uint32_t Capacity>
  class AtomicQueue {
  public:
    AtomicQueue() {
      m_head = m_tail = 0;
    }

    bool push(const T& item) {
      auto tail = m_tail.load();
      auto nextTail = (tail + 1) % Capacity;
      if (nextTail == m_head.load()) {
        return false;  // queue is full
      }
      m_data[tail] = item;
      m_tail.store(nextTail);
      return true;
    }

    bool pop(T& item) {
      uint32_t head, nextHead;
      do {
        head = m_head.load();
        if (head == m_tail.load()) {
          return false;  // queue is empty
        }
        nextHead = (head + 1) % Capacity;
      } while (!m_head.compare_exchange_weak(head, nextHead));
      item = m_data[head];
      return true;
    }

  private:
    std::array<T, Capacity> m_data;
    std::atomic<uint32_t> m_head;
    std::atomic<uint32_t> m_tail;
  };
} //dxvk
