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
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "config/global_options.h"
#include "util_atomiccircularqueue.h"
#include "util_commands.h"

using namespace bridge_util;

namespace {
  constexpr size_t kQueueSize = 4;
  constexpr uint32_t kItemCount = 20000;

  void check(const bool condition, const std::string& message) {
    if (!condition) {
      throw message;
    }
  }

  // The queue is small, so both ends have to wait for each other.
  void runProducerConsumer(const bool useWakeEvents) {
    using WriterQueue = AtomicCircularQueue<Header, Accessor::Writer>;
    using ReaderQueue = AtomicCircularQueue<Header, Accessor::Reader>;

    const size_t memSize = WriterQueue::getExtraMemoryRequirements() + kQueueSize * sizeof(Header) + 64;
    std::vector<uint8_t> memory(memSize);
    WriterQueue writer("TestQueue", memory.data(), memSize, kQueueSize);
    ReaderQueue reader("TestQueue", memory.data(), memSize, kQueueSize);

    HANDLE hDataAvailable = nullptr;
    HANDLE hSpaceAvailable = nullptr;
    if (useWakeEvents) {
      hDataAvailable = CreateEventA(nullptr, FALSE, FALSE, nullptr);
      hSpaceAvailable = CreateEventA(nullptr, FALSE, FALSE, nullptr);
      check(hDataAvailable != nullptr && hSpaceAvailable != nullptr, "CreateEvent failed.");
      writer.attachWakeEvents(hDataAvailable, hSpaceAvailable);
      reader.attachWakeEvents(hDataAvailable, hSpaceAvailable);
    }

    bool producerFailed = false;
    std::thread producer([&]() {
      for (uint32_t i = 0; i < kItemCount; ++i) {
        if (writer.push({ Commands::Bridge_Any, 0, i, i ^ 0x5a5a5a5au }) != Result::Success) {
          producerFailed = true;
          return;
        }
      }
    });

    std::string consumerError;
    for (uint32_t i = 0; i < kItemCount; ++i) {
      Result result = Result::Failure;
      const Header header = reader.pull(result, 5000);
      if (result != Result::Success) {
        consumerError = "The consumer timed out at element " + std::to_string(i) + ".";
        break;
      }
      if (header.dataOffset != i || header.pHandle != (i ^ 0x5a5a5a5au)) {
        consumerError = "Element " + std::to_string(i) + " arrived out of order or corrupted.";
        break;
      }
    }
    producer.join();

    if (hDataAvailable != nullptr) {
      CloseHandle(hDataAvailable);
    }
    if (hSpaceAvailable != nullptr) {
      CloseHandle(hSpaceAvailable);
    }

    check(!producerFailed, "The producer failed to push.");
    check(consumerError.empty(), consumerError);
    check(reader.isEmpty(), "The queue must be empty after every element was pulled.");
  }

  void testEmptyTimeout() {
    using WriterQueue = AtomicCircularQueue<Header, Accessor::Writer>;

    const size_t memSize = WriterQueue::getExtraMemoryRequirements() + kQueueSize * sizeof(Header) + 64;
    std::vector<uint8_t> memory(memSize);
    WriterQueue queue("TestQueue", memory.data(), memSize, kQueueSize);
    HANDLE hEvent = CreateEventA(nullptr, FALSE, FALSE, nullptr);
    check(hEvent != nullptr, "CreateEvent failed.");
    queue.attachWakeEvents(hEvent, hEvent);

    Result result = Result::Success;
    queue.pull(result, 20);
    CloseHandle(hEvent);
    check(result == Result::Timeout, "A pull on an empty queue must time out.");
    check(queue.emptyWaitStats().waits.load() == 1, "The timed-out pull must be counted as one wait.");
  }
}

int main() {
  try {
    runProducerConsumer(true);
    runProducerConsumer(false);
    testEmptyTimeout();
  } catch (const std::string& errorMessage) {
    std::cerr << "IPC queue wait test failed: " << errorMessage << std::endl;
    return -1;
  }
  std::cout << "IPC queue wait tests passed." << std::endl;
  return 0;
}
