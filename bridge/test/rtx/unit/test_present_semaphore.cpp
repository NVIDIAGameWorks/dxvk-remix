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
#include <memory>
#include <stdexcept>

#include "util_semaphore.h"

bridge_util::Guid gUniqueIdentifier;

namespace {
  using bridge_util::NamedSemaphore;
  using bridge_util::Result;

  void check(const bool condition, const char* message) {
    if (!condition) {
      throw std::runtime_error(message);
    }
  }

  void checkCredits(NamedSemaphore& semaphore, const size_t framesAhead) {
    for (size_t i = 0; i < framesAhead; ++i) {
      check(semaphore.wait(0) == Result::Success, "A completed present lost a run-ahead credit.");
    }
    check(semaphore.wait(0) == Result::Timeout, "Present pacing granted an extra run-ahead credit.");
  }

  void testCompletionBeforeWait(const size_t framesAhead) {
    const std::unique_ptr<NamedSemaphore> pClient(NamedSemaphore::createPresent(framesAhead));
    const std::unique_ptr<NamedSemaphore> pServer(NamedSemaphore::createPresent(framesAhead));
    for (size_t i = 0; i < 64; ++i) {
      pServer->release();
      check(pClient->wait(0) == Result::Success, "The completion signal was lost before the client waited.");
    }
    checkCredits(*pClient, framesAhead);
  }

  void testWaitBeforeCompletion(const size_t framesAhead) {
    const std::unique_ptr<NamedSemaphore> pClient(NamedSemaphore::createPresent(framesAhead));
    const std::unique_ptr<NamedSemaphore> pServer(NamedSemaphore::createPresent(framesAhead));
    for (size_t i = 0; i < 64; ++i) {
      const Result result = pClient->wait(0);
      check(result == (framesAhead ? Result::Success : Result::Timeout),
            "The initial present credit does not match the configured run-ahead allowance.");
      pServer->release();
      if (!framesAhead) {
        check(pClient->wait(0) == Result::Success, "A lockstep present did not receive its completion signal.");
      }
    }
    checkCredits(*pClient, framesAhead);
  }
}

int main() {
  try {
    for (const size_t framesAhead : { 0u, 1u }) {
      testCompletionBeforeWait(framesAhead);
      testWaitBeforeCompletion(framesAhead);
    }
  } catch (const std::exception& error) {
    std::cerr << "Present semaphore test failed: " << error.what() << std::endl;
    return 1;
  }
  std::cout << "Present semaphore tests passed." << std::endl;
  return 0;
}
