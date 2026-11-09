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

#include <chrono>
#include <optional>

// Decides whether automated termination may proceed while the game capturer has outstanding work.
// An active capture always delays termination, so its export is never cut short.
// A pending request delays termination only until it has waited for the timeout,
// because its prerequisites, such as a valid camera, may never be met.
class CaptureTerminationGuard {
public:
  using Clock = std::chrono::steady_clock;

  enum class Action {
    Wait,
    Terminate,
    CancelPendingRequest
  };

  // Called once per frame after termination is due.
  Action update(const bool captureActive, const bool requestPending, const Clock::time_point now, const Clock::duration timeout) {
    if (!requestPending) {
      m_pendingWaitStart.reset();
    }
    if (captureActive) {
      return Action::Wait;
    }
    if (!requestPending) {
      return Action::Terminate;
    }
    if (!m_pendingWaitStart.has_value()) {
      m_pendingWaitStart = now;
    }
    return now - *m_pendingWaitStart >= timeout
      ? Action::CancelPendingRequest
      : Action::Wait;
  }

private:
  std::optional<Clock::time_point> m_pendingWaitStart;
};
