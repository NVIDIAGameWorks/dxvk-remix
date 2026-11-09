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
#include <chrono>
#include <iostream>

#include "../../test_utils.h"
#include "../../../src/dxvk/rtx_render/rtx_capture_termination_guard.h"

namespace dxvk {
  // Note: Logger needed by some shared code used in this Unit Test.
  Logger Logger::s_instance("test_capture_termination_guard.log");
}

namespace {
  using Action = CaptureTerminationGuard::Action;
  using Clock = CaptureTerminationGuard::Clock;

  constexpr Clock::duration kTimeout = std::chrono::seconds(10);
  const Clock::time_point kStart = Clock::time_point(std::chrono::hours(1));

  Clock::time_point at(int seconds) {
    return kStart + std::chrono::seconds(seconds);
  }

  void check(bool condition, const char* message) {
    if (!condition) {
      throw dxvk::DxvkError(message);
    }
  }

  void testIdleCapturerTerminates() {
    CaptureTerminationGuard guard;
    check(guard.update(false, false, at(0), kTimeout) == Action::Terminate, "an idle capturer must not delay termination");
  }

  void testActiveCaptureIsNeverCancelled() {
    CaptureTerminationGuard guard;
    check(guard.update(true, false, at(0), kTimeout) == Action::Wait, "an active capture must delay termination");
    check(guard.update(true, false, at(1000), kTimeout) == Action::Wait, "an active capture must delay termination past the timeout");
    check(guard.update(true, true, at(2000), kTimeout) == Action::Wait, "a pending request must not cancel an active capture");
  }

  // The prerequisites are met before the timeout, so the request starts a capture that finishes normally.
  void testDelayedReadinessRecovers() {
    CaptureTerminationGuard guard;
    check(guard.update(false, true, at(0), kTimeout) == Action::Wait, "a new pending request must delay termination");
    check(guard.update(false, true, at(9), kTimeout) == Action::Wait, "a pending request must delay termination until the timeout");
    check(guard.update(true, false, at(30), kTimeout) == Action::Wait, "the capture started by the request must finish");
    check(guard.update(false, false, at(31), kTimeout) == Action::Terminate, "termination must proceed once the capture finishes");
  }

  // The prerequisites are never met, so the request is cancelled once it has waited for the timeout.
  void testNeverReadyIsCancelled() {
    CaptureTerminationGuard guard;
    check(guard.update(false, true, at(0), kTimeout) == Action::Wait, "a new pending request must delay termination");
    check(guard.update(false, true, at(5), kTimeout) == Action::Wait, "a pending request must delay termination until the timeout");
    check(guard.update(false, true, at(10), kTimeout) == Action::CancelPendingRequest, "a pending request must be cancelled at the timeout");
  }

  // The timeout counts from when termination starts waiting on the request, not from earlier requests or captures.
  void testTimeoutRestartsForNewRequest() {
    CaptureTerminationGuard guard;
    check(guard.update(false, true, at(0), kTimeout) == Action::Wait, "a new pending request must delay termination");
    check(guard.update(true, false, at(8), kTimeout) == Action::Wait, "the capture started by the request must finish");
    check(guard.update(false, true, at(20), kTimeout) == Action::Wait, "a later request must get a full timeout");
    check(guard.update(false, true, at(29), kTimeout) == Action::Wait, "a later request must get a full timeout");
    check(guard.update(false, true, at(30), kTimeout) == Action::CancelPendingRequest, "a later request must be cancelled at its own timeout");
  }
}

int main() {
  try {
    testIdleCapturerTerminates();
    testActiveCaptureIsNeverCancelled();
    testDelayedReadinessRecovers();
    testNeverReadyIsCancelled();
    testTimeoutRestartsForNewRequest();
    std::cout << "All passed\n";
  } catch (const dxvk::DxvkError& error) {
    std::cerr << error.message() << std::endl;
    return 1;
  }

  return 0;
}
