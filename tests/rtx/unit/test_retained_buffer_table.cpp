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
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <vector>

#include "../../test_utils.h"
#include "../../../src/dxvk/rtx_render/rtx_retained_buffer_table.h"

namespace dxvk {
  // Note: Logger needed by some shared code used in this Unit Test.
  Logger Logger::s_instance("test_retained_buffer_table.log");
}

namespace {
  // Minimal stand-in for a buffer slice, providing the interface the table relies on.
  struct FakeBuffer {
    struct Handle {
      void* ptr() const { return pObject; }
      void* pObject = nullptr;
    };

    FakeBuffer() = default;
    FakeBuffer(uintptr_t id, size_t offset, size_t length)
      : m_handle{ reinterpret_cast<void*>(id) }, m_offset(offset), m_length(length) { }

    bool defined() const { return m_handle.pObject != nullptr; }
    Handle buffer() const { return m_handle; }
    size_t offset() const { return m_offset; }
    size_t length() const { return m_length; }
    bool matches(const FakeBuffer& other) const {
      return m_handle.pObject == other.m_handle.pObject && m_offset == other.m_offset && m_length == other.m_length;
    }

    Handle m_handle;
    size_t m_offset = 0;
    size_t m_length = 0;
  };

  using Table = RetainedBufferTable<FakeBuffer>;

  void check(bool condition, const char* message) {
    if (!condition) {
      throw dxvk::DxvkError(message);
    }
  }

  std::vector<uint32_t> sorted(const std::vector<uint32_t>& slots) {
    std::vector<uint32_t> result = slots;
    std::sort(result.begin(), result.end());
    return result;
  }

  void testAcquireMarksSlot() {
    Table table;
    const uint32_t a = table.acquire(FakeBuffer(1, 0, 16));
    const uint32_t b = table.acquire(FakeBuffer(2, 0, 16));
    check(a != b, "distinct buffers must get distinct slots");
    check(sorted(table.getUpdatedSlots()) == sorted({ a, b }), "acquire must mark each new slot as updated");
  }

  void testReacquireDoesNotMarkSlot() {
    Table table;
    const uint32_t slot = table.acquire(FakeBuffer(1, 0, 16));
    table.clearUpdatedSlots();

    check(table.acquire(FakeBuffer(1, 0, 16)) == slot, "re-acquire must return the same slot");
    check(table.getUpdatedSlots().empty(), "re-acquire of a live slot must not mark it as updated");
  }

  void testReleaseMarksOnlyOnLastRef() {
    Table table;
    const uint32_t slot = table.acquire(FakeBuffer(1, 0, 16));
    table.acquire(FakeBuffer(1, 0, 16));
    table.clearUpdatedSlots();

    table.release(slot);
    check(table.getUpdatedSlots().empty(), "release with references remaining must not mark the slot");

    table.release(slot);
    check(table.getUpdatedSlots() == std::vector<uint32_t>{ slot }, "releasing the last reference must mark the slot");
    check(!table.getObjectTable()[slot].defined(), "freed slot must hold an undefined buffer");
  }

  void testSlotMarkedOnce() {
    Table table;
    const uint32_t slot = table.acquire(FakeBuffer(1, 0, 16));
    table.release(slot);
    const uint32_t reused = table.acquire(FakeBuffer(2, 0, 16));

    check(reused == slot, "freed slot must be reused");
    check(table.getUpdatedSlots() == std::vector<uint32_t>{ slot }, "a slot changed several times must be listed once");
  }

  void testClearUpdatedSlots() {
    Table table;
    const uint32_t slot = table.acquire(FakeBuffer(1, 0, 16));
    table.clearUpdatedSlots();
    check(table.getUpdatedSlots().empty(), "clearUpdatedSlots must empty the list");

    table.release(slot);
    check(table.getUpdatedSlots() == std::vector<uint32_t>{ slot }, "a cleared slot must be markable again");
    check(table.getObjectTable().size() == 1, "clearUpdatedSlots must not touch the table");
  }

  void testClearResetsUpdatedSlots() {
    Table table;
    table.acquire(FakeBuffer(1, 0, 16));
    table.clear();
    check(table.getUpdatedSlots().empty(), "clear must drop pending updated slots");

    const uint32_t slot = table.acquire(FakeBuffer(3, 0, 16));
    check(slot == 0, "clear must reset slot allocation");
    check(table.getUpdatedSlots() == std::vector<uint32_t>{ 0 }, "slots must be markable after clear");
  }
}

int main() {
  try {
    testAcquireMarksSlot();
    testReacquireDoesNotMarkSlot();
    testReleaseMarksOnlyOnLastRef();
    testSlotMarkedOnce();
    testClearUpdatedSlots();
    testClearResetsUpdatedSlots();
    std::cout << "All passed\n";
  } catch (const dxvk::DxvkError& error) {
    std::cerr << error.message() << std::endl;
    return 1;
  }

  return 0;
}
