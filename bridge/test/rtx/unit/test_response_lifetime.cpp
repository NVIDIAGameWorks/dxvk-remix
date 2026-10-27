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
#include <array>
#include <atomic>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>

#include "util_bridgecommand.h"

bridge_util::Guid gUniqueIdentifier;
bool gbBridgeRunning = true;

namespace {
  using TestBridge = Bridge<BridgeId::Module>;
  using namespace bridge_util;

  static_assert(std::is_same_v<decltype(TestBridge::get_data()), uint32_t>);
  static_assert(std::is_same_v<decltype(TestBridge::get_data(nullptr)), uint32_t>);
  static_assert(std::is_same_v<decltype(TestBridge::copy_data(std::declval<uint32_t&>())), size_t>);
  static_assert(std::is_same_v<decltype(std::declval<DataQueue&>().pull()), uint32_t>);
  static_assert(std::is_same_v<decltype(std::declval<DataQueue&>().pull(nullptr)), uint32_t>);

  void check(bool condition, const std::string& message) {
    if (!condition) {
      throw message;
    }
  }

  void pushResponse(WriterChannel& channel, uint32_t uid) {
    const Header header { Commands::Bridge_Response, 0, (uint32_t) channel.data->get_pos(), uid };
    check(channel.commands->push(header) == Result::Success, "Could not publish the test response header.");
  }

  void testResponseHandoff(WriterChannel& producerChannel) {
    const auto& reader = TestBridge::getReaderChannel();
    for (uint32_t iteration = 0; iteration < 32; ++iteration) {
      const uint32_t firstUid = 100 + 2 * iteration;
      const uint32_t secondUid = firstUid + 1;
      const std::array<uint32_t, 4> payload { firstUid, 17, 29, iteration };
      producerChannel.data->push(firstUid);
      producerChannel.data->push(sizeof(payload), payload.data());
      pushResponse(producerChannel, firstUid);
      producerChannel.data->push(secondUid);
      pushResponse(producerChannel, secondUid);

      HANDLE hBorrowed = CreateEventA(nullptr, TRUE, FALSE, nullptr);
      HANDLE hNextWaiting = CreateEventA(nullptr, TRUE, FALSE, nullptr);
      check(hBorrowed != nullptr && hNextWaiting != nullptr, "Could not create handoff rendezvous events.");
      bool firstCorrect = false;
      bool secondCorrect = false;
      std::thread first([&]() {
        if (TestBridge::waitForCommand(Commands::Bridge_Response, 1, nullptr, true, firstUid) != Result::Success) {
          SetEvent(hBorrowed);
          return;
        }
        const uint32_t scalar = TestBridge::get_data();
        void* pBlob = nullptr;
        const uint32_t size = TestBridge::get_data(&pBlob);
        SetEvent(hBorrowed);
        const bool nextWaiting = WaitForSingleObject(hNextWaiting, 5000) == WAIT_OBJECT_0;
        const bool blobCorrect = size == sizeof(payload) && pBlob != nullptr && memcmp(pBlob, payload.data(), sizeof(payload)) == 0;
        const Header consumed = TestBridge::consumeResponse();
        firstCorrect = nextWaiting && blobCorrect && scalar == firstUid && consumed.pHandle == firstUid;
      });
      std::thread second([&]() {
        if (WaitForSingleObject(hBorrowed, 5000) != WAIT_OBJECT_0) {
          SetEvent(hNextWaiting);
          return;
        }
        Result result;
        const Header pending = reader.commands->peek(result, 1000);
        const bool firstStillQueued = result == Result::Success && pending.pHandle == firstUid;
        SetEvent(hNextWaiting);
        if (TestBridge::waitForCommand(Commands::Bridge_Response, 1, nullptr, true, secondUid) == Result::Success) {
          const uint32_t scalar = TestBridge::get_data();
          const Header consumed = TestBridge::consumeResponse();
          secondCorrect = firstStillQueued && scalar == secondUid && consumed.pHandle == secondUid;
        }
      });
      first.join();
      second.join();
      CloseHandle(hBorrowed);
      CloseHandle(hNextWaiting);
      check(firstCorrect && secondCorrect, "Cross-thread response handoff corrupted a payload cursor or header.");
    }
  }

  void testBorrowedResponse() {
    constexpr size_t kRingWords = 16;
    constexpr size_t kCommandSlots = 8;
    using ReaderCommands = AtomicCircularQueue<Header, Accessor::Reader>;
    const size_t memorySize = kCommandSlots * sizeof(Header) + ReaderCommands::getExtraMemoryRequirements() + kRingWords * sizeof(uint32_t);
    const std::string name = "ResponseLifetime";
    TestBridge::init(name, memorySize, kCommandSlots, kRingWords, memorySize, kCommandSlots, kRingWords);
#ifdef REMIX_BRIDGE_CLIENT
    WriterChannel producerChannel(name + "Server2Client", memorySize, kCommandSlots, kRingWords);
#else
    WriterChannel producerChannel(name + "Client2Server", memorySize, kCommandSlots, kRingWords);
#endif
    auto& writer = *producerChannel.data;
    const auto& reader = TestBridge::getReaderChannel();
    check(writer.get_total_size() == kRingWords, "The test requires a small data ring.");

    const uint32_t originalScalar = 0x12345678;
    const std::array<uint32_t, 4> originalBlob { 11, 22, 33, 44 };
    const uint32_t originalTail = 0xabcdef01;
    writer.push(originalScalar);
    writer.push(sizeof(originalBlob), originalBlob.data());
    writer.push(sizeof(originalTail), &originalTail);
    pushResponse(producerChannel, 1);

    const std::array<uint32_t, 4> queuedResponse { 101, 102, 103, 104 };
    for (uint32_t value : queuedResponse) {
      writer.push(value);
    }
    pushResponse(producerChannel, 2);
    check(writer.get_abs_pos() == 12, "The queued responses must leave four words at the ring tail.");

    const auto& scalar = TestBridge::get_data();
    void* pBorrowedBlob = nullptr;
    const auto& blobBytes = TestBridge::get_data(&pBorrowedBlob);
    uint32_t tail = 0;
    const auto& copiedBytes = TestBridge::copy_data(tail);
    check(pBorrowedBlob != nullptr, "The first response did not return its borrowed blob.");
    check(reader.serverDataPos->load(std::memory_order_acquire) == 0,
          "A later scalar or copied-object read released an outstanding borrowed blob.");

    const std::array<uint32_t, 3> wrappingBlob { 201, 202, 203 };
    const uint64_t writeEnd = writer.abs_pos_after(wrappingBlob.size() + 1, true);
    const int64_t requiredPosition = int64_t(writeEnd - kRingWords);
    check(requiredPosition > 0 && requiredPosition <= int64_t(TestBridge::get_data_abs_pos()),
          "The wrapping write must require the borrowed response to be consumed.");
    reader.clientDataExpectedPos->store(requiredPosition, std::memory_order_release);
    HANDLE hAttempted = CreateEventA(nullptr, TRUE, FALSE, nullptr);
    check(hAttempted != nullptr, "Could not create the producer rendezvous event.");
    std::atomic<bool> producerBlocked { false };
    std::atomic<bool> producerFinished { false };
    std::thread producer([&]() {
      const auto hasSpace = [&]() {
        return writeEnd <= uint64_t(producerChannel.serverDataPos->load(std::memory_order_acquire)) + kRingWords;
      };
      producerBlocked.store(!hasSpace(), std::memory_order_release);
      SetEvent(hAttempted);
      const ULONGLONG deadline = GetTickCount64() + 5000;
      while (!hasSpace() && GetTickCount64() < deadline) {
        producerChannel.dataSemaphore->wait(2);
      }
      if (hasSpace()) {
        if (writer.push(sizeof(wrappingBlob), wrappingBlob.data()) == Result::Success) {
          const Header header { Commands::Bridge_Response, 0, (uint32_t) writer.get_pos(), 3 };
          producerFinished.store(producerChannel.commands->push(header) == Result::Success, std::memory_order_release);
        }
      }
    });

    const bool producerAttempted = WaitForSingleObject(hAttempted, 5000) == WAIT_OBJECT_0;
    const bool blockedWhileBorrowed = producerBlocked.load(std::memory_order_acquire);
    std::array<uint32_t, 4> copiedBlob {};
    if (producerAttempted && blockedWhileBorrowed) {
      memcpy(copiedBlob.data(), pBorrowedBlob, sizeof(copiedBlob));
    }
    const bool retainedUntilCopied = !producerFinished.load(std::memory_order_acquire);
    const Header consumed = TestBridge::consumeResponse();
    producer.join();
    reader.clientDataExpectedPos->store(-1, std::memory_order_release);
    CloseHandle(hAttempted);

    check(producerAttempted && blockedWhileBorrowed && retainedUntilCopied,
          "The wrapping producer reused the first response before its final borrowed-data use.");
    check(consumed.pHandle == 1, "The consumer completed the wrong response.");
    check(producerFinished.load(std::memory_order_acquire), "The producer did not resume after response consumption.");
    check(copiedBlob == originalBlob && tail == originalTail, "The paused response data was corrupted.");
    check(scalar == originalScalar && blobBytes == sizeof(originalBlob) && copiedBytes == sizeof(originalTail),
          "Returned scalar values or lengths changed after their ring words were reused.");
    check(writer.get_abs_pos() == writeEnd && writer.get_pos() < wrappingBlob.size() + 1,
          "The producer did not wrap over the original response storage.");

    for (uint32_t value : queuedResponse) {
      check(TestBridge::get_data() == value, "The queued response arrived corrupted.");
    }
    check(TestBridge::consumeResponse().pHandle == 2, "The second response header was lost.");
    void* pWrappedBlob = nullptr;
    check(TestBridge::get_data(&pWrappedBlob) == sizeof(wrappingBlob), "The wrapping blob length is incorrect.");
    check(memcmp(pWrappedBlob, wrappingBlob.data(), sizeof(wrappingBlob)) == 0, "The wrapping blob arrived corrupted.");
    check(TestBridge::consumeResponse().pHandle == 3, "The wrapping response header was lost.");

    writer.push(0, nullptr);
    pushResponse(producerChannel, 4);
    void* pEmptyBlob = reinterpret_cast<void*>(uintptr_t(1));
    check(TestBridge::get_data(&pEmptyBlob) == 0 && pEmptyBlob == nullptr, "An empty blob must return a null pointer.");
    TestBridge::consumeResponse();

    const int64_t priorPublication = reader.serverDataPos->load(std::memory_order_acquire);
    writer.push(301u);
    pushResponse(producerChannel, 5);
    TestBridge::pop_front();
    check(TestBridge::get_data() == 301u, "The request payload was not read correctly.");
    check(reader.serverDataPos->load(std::memory_order_acquire) == priorPublication,
          "A raw header pop released request storage before dispatch completed.");
    TestBridge::publishReadPos();
    check(reader.serverDataPos->load(std::memory_order_acquire) == int64_t(writer.get_abs_pos()),
          "Post-dispatch publication did not release the consumed request.");
    testResponseHandoff(producerChannel);
  }
}

int main() {
  try {
    testBorrowedResponse();
  } catch (const std::string& message) {
    std::cerr << "Response lifetime test failed: " << message << std::endl;
    return -1;
  }
  std::cout << "Response lifetime tests passed." << std::endl;
  return 0;
}
