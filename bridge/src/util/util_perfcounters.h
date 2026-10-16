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
#include <cstdint>

// Counters of the device bridge, logged every perfLogIntervalFrames frames when that option is set.

namespace bridge_util {

  // Only waits that did not find the queue ready on entry are counted.
  struct QueueWaitStats {
    std::atomic<uint64_t> waits { 0 };
    std::atomic<uint64_t> spinTicks { 0 };
    std::atomic<uint64_t> blockTicks { 0 };
    std::atomic<uint64_t> blocks { 0 };
    std::atomic<uint64_t> wakesSent { 0 };
  };

  namespace perf {
    static constexpr uint32_t kMaxCommands = 1024;

    extern std::atomic<uint32_t> g_intervalFrames;

    inline bool enabled() {
      return g_intervalFrames.load(std::memory_order_relaxed);
    }

    void init(uint32_t intervalFrames);

    int64_t now();

    void noteCommand(uint16_t command, uint32_t dataWords, int64_t ticks);

    void noteDropped(uint16_t command);

    // Records of a Bridge_StatePacket are counted by type, but not in the number of commands.
    void noteRecord(uint16_t command);

    // The next response wait is attributed to this command.
    void noteSent(uint16_t command);

    void noteResponseWait(int64_t ticks);

    void notePresentWait(int64_t ticks);

    // Describes how the client answered an IDirect3DQuery9::GetData call.
    enum class QueryPath : uint32_t {
      ClientEvent,
      AsyncCached,
      AsyncRequest,
      Blocking,
      Count
    };
    void noteQuery(QueryPath path, bool signalled);

    void noteUpload(uint64_t bytes);

    // Failures of create calls whose result the client did not wait for are always reported.
    void noteAsyncCreateFailure(uint16_t command, long hresult);

    void onFrame(const QueueWaitStats& writerFull, const QueueWaitStats& readerEmpty);
  }
}
