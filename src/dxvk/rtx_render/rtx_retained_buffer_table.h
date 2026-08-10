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

#include <cassert>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include "../util/util_struct_hash.h"

// Retained buffer registration table. Each distinct (buffer ptr, offset, length) triple
// gets a stable slot index that persists across frames. acquire() is ref-counted; the slot
// is returned to a free-list when the last release() fires. Slots left in the table by
// free-listed entries are default-constructed (not defined()), which causes
// BindlessResourceManager to write a dummy descriptor for that slot — safe.
template<typename BufferType>
class RetainedBufferTable {
  struct Key {
    void*  ptr;
    size_t offset;
    size_t length;
    bool operator==(const Key& o) const noexcept {
      return ptr == o.ptr && offset == o.offset && length == o.length;
    }
  };
  struct KeyHash {
    size_t operator()(const Key& k) const noexcept {
      return dxvk::hashStructByMemory<Key, &Key::ptr, &Key::offset, &Key::length>(k);
    }
  };

  static Key makeKey(const BufferType& buffer) {
    return { static_cast<void*>(buffer.buffer().ptr()), buffer.offset(), buffer.length() };
  }

public:
  // Register buffer and return its stable slot index. buffer must be defined().
  // Increments the ref count if the buffer is already registered.
  uint32_t acquire(const BufferType& buffer) {
    assert(buffer.defined());
    auto [it, inserted] = m_indexMap.emplace(makeKey(buffer), uint32_t(0));
    if (!inserted) {
      ++m_refCounts[it->second];
      return it->second;
    }
    uint32_t slot;
    if (!m_freeSlots.empty()) {
      slot = m_freeSlots.back();
      m_freeSlots.pop_back();
      m_table[slot]     = buffer;
      m_refCounts[slot] = 1;
    } else {
      slot = static_cast<uint32_t>(m_table.size());
      m_table.push_back(buffer);
      m_refCounts.push_back(1);
    }
    it->second = slot;
    return slot;
  }

  // Decrement the ref count for slot. Frees the slot when it reaches zero.
  // slot must be a value previously returned by acquire() (not a sentinel).
  void release(uint32_t slot) {
    if (slot >= m_table.size()) {
      assert(false && "RetainedBufferTable::release: invalid slot");
      return;
    }
    if (m_refCounts[slot] == 0) {
      assert(false && "RetainedBufferTable::release: over-release of slot");
      return;
    }
    if (--m_refCounts[slot] == 0) {
      m_indexMap.erase(makeKey(m_table[slot]));
      m_table[slot] = BufferType{};
      m_freeSlots.push_back(slot);
    }
  }

  // Returns true if slot is still registered for exactly buffer (same buffer slice).
  // Returns false if slot is out of range (e.g. kSurfaceInvalidBufferIndex).
  bool isRegistered(uint32_t slot, const BufferType& buffer) const {
    if (slot >= m_table.size()) {
      return false;
    }
    return buffer.matches(m_table[slot]);
  }

  // Reset all registrations. Only call after GPU idle (full scene clear).
  void clear() {
    m_indexMap.clear();
    m_table.clear();
    m_refCounts.clear();
    m_freeSlots.clear();
  }

  const std::vector<BufferType>& getObjectTable() const { return m_table; }
  uint32_t getActiveCount() const { return static_cast<uint32_t>(m_indexMap.size()); }
  uint32_t getTotalCount()  const { return static_cast<uint32_t>(m_table.size()); }

private:
  std::unordered_map<Key, uint32_t, KeyHash> m_indexMap;
  std::vector<BufferType> m_table;
  std::vector<uint32_t>   m_refCounts;
  std::vector<uint32_t>   m_freeSlots;
};
