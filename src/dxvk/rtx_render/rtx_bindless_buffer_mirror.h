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

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <utility>
#include <vector>

#include "rtx_pending_slot_queues.h"

// CPU mirror of the bindless buffer table, which turns changed slots into descriptor writes for each frame in flight.
// It holds a reference to every buffer in the table, and hands replaced buffers to the caller to keep alive until the GPU is done.
// Traits provides the Buffer, Descriptor and Resource types, and these static functions for a defined buffer:
// descriptor(buffer), resource(buffer), and isSameResource(stored, buffer), which must not create a Resource.
template<typename Traits>
class BindlessBufferMirror {
public:
  using Buffer = typename Traits::Buffer;
  using Descriptor = typename Traits::Descriptor;
  using Resource = typename Traits::Resource;

  BindlessBufferMirror(uint32_t copyCount, uint32_t maxSlots)
    : m_pendingSlots(copyCount), m_maxSlots(maxSlots) { }

  // Applies the slots that changed since the last update.
  // Every slot is applied on the first update after construction or release(), because the copies do not match the mirror yet.
  void update(const std::vector<Buffer>& buffers, const std::vector<uint32_t>& updatedSlots, const Descriptor& dummyDescriptor) {
    m_dummyDescriptor = dummyDescriptor;

    if (!m_populated) {
      m_populated = true;

      // Slot 0 is always applied so that every copy has a valid binding, even for an empty table.
      const uint32_t slotCount = std::max(static_cast<uint32_t>(buffers.size()), 1u);
      for (uint32_t slot = 0; slot < slotCount; ++slot) {
        applySlot(buffers, slot);
      }
      return;
    }

    for (uint32_t slot : updatedSlots) {
      applySlot(buffers, slot);
    }
  }

  // Drops the mirror and moves every buffer it referenced to the retired list.
  // The next update applies every slot again.
  void release() {
    m_descriptors.clear();
    for (Resource& resource : m_resources) {
      if (resource != Resource {}) {
        m_retired.push_back(std::move(resource));
      }
    }
    m_resources.clear();
    m_populated = false;
    m_pendingSlots.clear();
  }

  // Calls write(firstSlot, count, pDescriptors) for each run of consecutive slots the copy still needs, and marks them as done.
  // The descriptors stay valid until the next update or release.
  template<typename WriteFn>
  void takeWrites(uint32_t copy, WriteFn&& write) {
    m_pendingSlots.takeRuns(copy, [&](uint32_t firstSlot, uint32_t count) {
      assert(firstSlot + count <= m_descriptors.size());
      write(firstSlot, count, &m_descriptors[firstSlot]);
    });
  }

  // Returns the buffers that left the table since the last call.
  std::vector<Resource> takeRetired() {
    return std::exchange(m_retired, {});
  }

private:
  void applySlot(const std::vector<Buffer>& buffers, uint32_t slot) {
    if (slot >= m_maxSlots) {
      assert(false);
      return;
    }
    if (m_descriptors.size() <= slot) {
      m_descriptors.resize(slot + 1, m_dummyDescriptor);
    }
    if (m_resources.size() <= slot) {
      m_resources.resize(slot + 1);
    }

    const bool isDefined = slot < buffers.size() && buffers[slot].defined();
    m_descriptors[slot] = isDefined ? Traits::descriptor(buffers[slot]) : m_dummyDescriptor;

    // The replaced buffer may still be read by earlier frames in flight.
    Resource& stored = m_resources[slot];
    const bool isSame = isDefined ? Traits::isSameResource(stored, buffers[slot]) : stored == Resource {};
    if (!isSame) {
      if (stored != Resource {}) {
        m_retired.push_back(std::move(stored));
      }
      stored = isDefined ? Traits::resource(buffers[slot]) : Resource {};
    }

    m_pendingSlots.mark(slot);
  }

  PendingSlotQueues m_pendingSlots;
  std::vector<Descriptor> m_descriptors;
  std::vector<Resource> m_resources;
  std::vector<Resource> m_retired;
  Descriptor m_dummyDescriptor {};
  uint32_t m_maxSlots;
  bool m_populated = false;
};
