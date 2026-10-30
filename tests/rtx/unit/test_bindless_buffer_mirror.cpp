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
#include <cstdint>
#include <iostream>
#include <memory>
#include <vector>

#include "../../test_utils.h"
#include "../../../src/dxvk/rtx_render/rtx_bindless_buffer_mirror.h"

namespace dxvk {
  // Note: Logger needed by some shared code used in this Unit Test.
  Logger Logger::s_instance("test_bindless_buffer_mirror.log");
}

namespace {
  constexpr uint32_t kCopyCount = 3;
  constexpr uint32_t kMaxSlots = 64;
  constexpr int kDummy = -1;

  // Minimal stand-in for a buffer, whose descriptor is a plain id and whose resource is reference counted.
  struct FakeBuffer {
    FakeBuffer() = default;
    explicit FakeBuffer(int id)
      : m_descriptor(id), m_resource(std::make_shared<int>(id)) { }

    bool defined() const { return m_resource != nullptr; }

    int m_descriptor = kDummy;
    std::shared_ptr<int> m_resource;
  };

  struct FakeTraits {
    using Buffer = FakeBuffer;
    using Descriptor = int;
    using Resource = std::shared_ptr<int>;

    static Descriptor descriptor(const Buffer& buffer) { return buffer.m_descriptor; }
    static Resource resource(const Buffer& buffer) { return buffer.m_resource; }
    static bool isSameResource(const Resource& stored, const Buffer& buffer) { return stored.get() == buffer.m_resource.get(); }
  };

  using Mirror = BindlessBufferMirror<FakeTraits>;

  // A copy of the table, which applies writes the way vkUpdateDescriptorSets would.
  using Copy = std::vector<int>;

  struct Write {
    uint32_t firstSlot;
    uint32_t count;
    std::vector<int> descriptors;
  };

  void check(bool condition, const char* message) {
    if (!condition) {
      throw dxvk::DxvkError(message);
    }
  }

  std::vector<Write> takeWrites(Mirror& mirror, uint32_t copyIndex) {
    std::vector<Write> writes;
    mirror.takeWrites(copyIndex, [&writes](uint32_t firstSlot, uint32_t count, const int* pDescriptors) {
      writes.push_back({ firstSlot, count, std::vector<int>(pDescriptors, pDescriptors + count) });
    });
    return writes;
  }

  // Applies the copy's pending writes and returns how many writes there were.
  size_t applyWrites(Mirror& mirror, uint32_t copyIndex, Copy& copy) {
    const std::vector<Write> writes = takeWrites(mirror, copyIndex);
    for (const Write& write : writes) {
      if (copy.size() < write.firstSlot + write.count) {
        copy.resize(write.firstSlot + write.count, kDummy);
      }
      for (uint32_t i = 0; i < write.count; ++i) {
        copy[write.firstSlot + i] = write.descriptors[i];
      }
    }
    return writes.size();
  }

  // The descriptors a copy must hold for the buffers, with undefined buffers as dummies.
  Copy expected(const std::vector<FakeBuffer>& buffers) {
    Copy result;
    for (const FakeBuffer& buffer : buffers) {
      result.push_back(buffer.defined() ? buffer.m_descriptor : kDummy);
    }
    return result;
  }

  size_t takeRetiredCount(Mirror& mirror) {
    return mirror.takeRetired().size();
  }

  void testEmptyStartWritesSlotZero() {
    Mirror mirror(kCopyCount, kMaxSlots);
    mirror.update({}, {}, kDummy);

    for (uint32_t i = 0; i < kCopyCount; ++i) {
      Copy copy;
      check(applyWrites(mirror, i, copy) == 1, "an empty table must still write one range");
      check(copy == Copy { kDummy }, "an empty table must write the dummy descriptor into slot 0");
    }
  }

  void testStartWritesEveryCopyOnce() {
    const std::vector<FakeBuffer> buffers = { FakeBuffer(10), FakeBuffer(11), FakeBuffer(), FakeBuffer(13) };
    Mirror mirror(kCopyCount, kMaxSlots);
    Copy copies[kCopyCount];

    mirror.update(buffers, {}, kDummy);
    for (uint32_t i = 0; i < kCopyCount; ++i) {
      check(applyWrites(mirror, i, copies[i]) == 1, "the first update must write each copy as one coalesced range");
      check(copies[i] == expected(buffers), "the first update must write every slot of each copy");
    }

    mirror.update(buffers, {}, kDummy);
    for (uint32_t i = 0; i < kCopyCount; ++i) {
      check(applyWrites(mirror, i, copies[i]) == 0, "a rotation without changes must not write anything");
    }
  }

  void testReleaseAndReuseBeforeRotation() {
    std::vector<FakeBuffer> buffers = { FakeBuffer(10), FakeBuffer(11), FakeBuffer(12) };
    Mirror mirror(kCopyCount, kMaxSlots);
    Copy copies[kCopyCount];
    mirror.update(buffers, {}, kDummy);
    for (uint32_t i = 0; i < kCopyCount; ++i) {
      applyWrites(mirror, i, copies[i]);
    }

    // Slot 1 is released, and copy 0 is written.
    std::weak_ptr<int> released = buffers[1].m_resource;
    buffers[1] = FakeBuffer();
    mirror.update(buffers, { 1 }, kDummy);
    check(applyWrites(mirror, 0, copies[0]) == 1, "a released slot must be written");
    check(copies[0] == expected(buffers), "a released slot must hold the dummy descriptor");
    check(takeRetiredCount(mirror) == 1, "a released buffer must be retired once");
    check(released.expired(), "the mirror must not keep a retired buffer alive");

    // Slot 1 is reused before copies 1 and 2 have been written.
    buffers[1] = FakeBuffer(21);
    mirror.update(buffers, { 1 }, kDummy);
    for (uint32_t i = 0; i < kCopyCount; ++i) {
      check(applyWrites(mirror, i, copies[i]) == 1, "a reused slot must be written once per copy");
      check(copies[i] == expected(buffers), "every copy must end up with the reused slot's latest descriptor");
    }
    check(takeRetiredCount(mirror) == 0, "a slot reused after its release must not retire anything");
  }

  void testReplacedBufferIsRetiredAndLiveBuffersAreKept() {
    std::vector<FakeBuffer> buffers = { FakeBuffer(10), FakeBuffer(11) };
    Mirror mirror(kCopyCount, kMaxSlots);
    mirror.update(buffers, {}, kDummy);

    std::weak_ptr<int> live = buffers[0].m_resource;
    std::weak_ptr<int> replaced = buffers[1].m_resource;
    buffers[1] = FakeBuffer(21);
    mirror.update(buffers, { 1 }, kDummy);
    buffers.clear();

    check(!live.expired(), "the mirror must keep the buffers in the table alive");

    const std::vector<std::shared_ptr<int>> retired = mirror.takeRetired();
    check(retired.size() == 1 && retired[0].get() == replaced.lock().get(), "a replaced buffer must be handed over as retired");
  }

  void testNewRangeOfSameBufferIsNotRetired() {
    std::vector<FakeBuffer> buffers = { FakeBuffer(10) };
    Mirror mirror(kCopyCount, kMaxSlots);
    Copy copy;
    mirror.update(buffers, {}, kDummy);
    applyWrites(mirror, 0, copy);

    // The slot now refers to another range of the same buffer.
    buffers[0].m_descriptor = 20;
    mirror.update(buffers, { 0 }, kDummy);
    applyWrites(mirror, 0, copy);
    check(copy == Copy { 20 }, "a new range of the same buffer must be written");
    check(takeRetiredCount(mirror) == 0, "a new range of the same buffer must not retire it");
  }

  void testSparseUpdatesCoalesce() {
    std::vector<FakeBuffer> buffers;
    for (int i = 0; i < 8; ++i) {
      buffers.push_back(FakeBuffer(10 + i));
    }
    Mirror mirror(kCopyCount, kMaxSlots);
    Copy copy;
    mirror.update(buffers, {}, kDummy);
    applyWrites(mirror, 0, copy);

    for (uint32_t slot : { 6u, 2u, 3u }) {
      buffers[slot] = FakeBuffer(30 + static_cast<int>(slot));
    }
    mirror.update(buffers, { 6, 2, 3 }, kDummy);

    const std::vector<Write> writes = takeWrites(mirror, 0);
    check(writes.size() == 2, "adjacent slots must be coalesced and gaps excluded");
    check(writes[0].firstSlot == 2 && writes[0].count == 2 && writes[0].descriptors == std::vector<int> { 32, 33 },
          "the first range must cover slots 2 and 3");
    check(writes[1].firstSlot == 6 && writes[1].count == 1 && writes[1].descriptors == std::vector<int> { 36 }, "the second range must cover slot 6");
  }

  void testReleaseToSmallerTable() {
    std::vector<FakeBuffer> buffers = { FakeBuffer(10), FakeBuffer(11), FakeBuffer(12) };
    Mirror mirror(kCopyCount, kMaxSlots);
    Copy copies[kCopyCount];
    mirror.update(buffers, {}, kDummy);
    for (uint32_t i = 0; i < kCopyCount; ++i) {
      applyWrites(mirror, i, copies[i]);
    }

    mirror.release();
    check(takeRetiredCount(mirror) == 3, "release must retire every buffer in the table");

    buffers = { FakeBuffer(20) };
    mirror.update(buffers, {}, kDummy);
    for (uint32_t i = 0; i < kCopyCount; ++i) {
      Copy written;
      applyWrites(mirror, i, written);
      check(written == expected(buffers), "after a release, every copy must receive the new table");
    }
  }
}

int main() {
  try {
    testEmptyStartWritesSlotZero();
    testStartWritesEveryCopyOnce();
    testReleaseAndReuseBeforeRotation();
    testReplacedBufferIsRetiredAndLiveBuffersAreKept();
    testNewRangeOfSameBufferIsNotRetired();
    testSparseUpdatesCoalesce();
    testReleaseToSmallerTable();
    std::cout << "All passed\n";
  } catch (const dxvk::DxvkError& error) {
    std::cerr << error.message() << std::endl;
    return 1;
  }

  return 0;
}
