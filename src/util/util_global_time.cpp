/*
* Copyright (c) 2025-2026, NVIDIA CORPORATION. All rights reserved.
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

#include "util_global_time.h"

namespace dxvk {
  // default steady_clock in microseconds
  uint64_t GlobalTime::defaultSource() {
    using namespace std::chrono;
    auto e = steady_clock::now().time_since_epoch();
    return static_cast<uint64_t>(duration_cast<microseconds>(e).count());
  }

  uint64_t GlobalTime::deterministicTimeSource() {
    return get().m_frameIdx * get().m_timeDeltaBetweenFramesUs;
  }

  uint64_t GlobalTime::frozenTimeSource() {
    return get().m_lastUs;
  }

  GlobalTime::GlobalTime()
    : m_source(&GlobalTime::defaultSource) {
    m_lastUs = m_source();
    m_currentUs = m_lastUs;
    m_realLastUs = m_lastUs;
    m_realCurrentUs = m_lastUs;
    m_startTimeUs = m_lastUs;
  }

  void GlobalTime::init(const float timeDeltaBetweenFrames) {
    if (timeDeltaBetweenFrames != 0.f) {
      m_timeDeltaBetweenFramesUs = static_cast<uint64_t>(timeDeltaBetweenFrames * 1000000.f);
      resetDeterministicTimeSource();
    } else {
      resetRawTimeSource();
    }
  }

  // Call once per frame before queries
  void GlobalTime::update() {
    m_realLastUs = m_realCurrentUs;
    m_realCurrentUs = defaultSource();
    m_realDeltaSec = (m_realCurrentUs - m_realLastUs) * 1e-6f;

    m_lastUs = m_currentUs;
    m_currentUs = m_source();
    m_deltaSec = (m_currentUs - m_lastUs) * 1e-6f;
    ++m_frameIdx;
  }

  // scaled delta-t (s)
  float GlobalTime::deltaTime() const {
    return m_deltaSec;
  }

  // scaled delta-t (ms)
  float GlobalTime::deltaTimeMs() const {
    return m_deltaSec * 1000.f;
  }

  // actual frame delta-t (s), ignoring deterministic time settings
  float GlobalTime::realDeltaTime() const {
    return m_realDeltaSec;
  }

  // actual frame delta-t (ms), ignoring deterministic time settings
  float GlobalTime::realDeltaTimeMs() const {
    return m_realDeltaSec * 1000.f;
  }

  // whole micro-seconds since startup
  uint64_t GlobalTime::absoluteTimeUs() const {
    return m_currentUs;
  }

  // whole milliseconds since startup
  uint64_t GlobalTime::absoluteTimeMs() const {
    return m_currentUs / 1000;
  }

  // whole milliseconds since startup, ignoring deterministic time settings.
  uint64_t GlobalTime::realTimeSinceStartMs() const {
    // Note: this returns the actual time since the application started.  This should be used for profiling and metrics.
    return (defaultSource() - m_startTimeUs) / 1000;
  }

  void GlobalTime::setAdvanceTime(bool advanceTime) {
    if (advanceTime) {
      if (m_timeDeltaBetweenFramesUs != 0) {
        resetDeterministicTimeSource();
      } else {
        resetRawTimeSource();
      }
    } else {
      m_source = &GlobalTime::frozenTimeSource;
    }
  }

  // override underlying time source (for tests)
  void GlobalTime::resetDeterministicTimeSource() {
    m_source = &GlobalTime::deterministicTimeSource;
    m_lastUs = m_source();
    m_currentUs = m_lastUs;
  }

  // use standard real time
  void GlobalTime::resetRawTimeSource() {
    m_source = &GlobalTime::defaultSource;
    m_lastUs = m_source();
    m_currentUs = m_lastUs;
  }
}