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
#pragma once

#include "util_singleton.h"
#include <functional>
#include <chrono>

namespace dxvk {
  class GlobalTime : public Singleton<GlobalTime> {
    friend class Singleton<GlobalTime>;

  public:
    void init(const float timeDeltaBetweenFrames);

    // Call once per frame before queries
    void update();

    // scaled delta-t (s)
    float deltaTime() const;

    // scaled delta-t (ms)
    float deltaTimeMs() const;

    // actual frame delta-t (s), ignoring deterministic time settings
    float realDeltaTime() const;

    // actual frame delta-t (ms), ignoring deterministic time settings
    float realDeltaTimeMs() const;

    // whole micro-seconds since startup
    uint64_t absoluteTimeUs() const;

    // whole milliseconds since startup
    uint64_t absoluteTimeMs() const;

    // whole milliseconds since startup, ignoring deterministic time settings.
    uint64_t realTimeSinceStartMs() const;

    void setAdvanceTime(bool advanceTime);

  private:
    static uint64_t defaultSource();
    static uint64_t deterministicTimeSource();
    static uint64_t frozenTimeSource();
    GlobalTime(const GlobalTime& other) = delete;
    GlobalTime();
    void resetDeterministicTimeSource();
    void resetRawTimeSource();

    uint64_t                    m_lastUs = 0;
    uint64_t                    m_currentUs = 0;
    float                       m_deltaSec = 0.0f;
    uint64_t                    m_realLastUs = 0;
    uint64_t                    m_realCurrentUs = 0;
    float                       m_realDeltaSec = 0.0f;
    uint64_t                    m_frameIdx = 0;
    uint64_t                    m_timeDeltaBetweenFramesUs = 0;
    std::function<uint64_t()>   m_source;  

    // The time at which the application started. (really the first time GlobalTime::get() is called)
    uint64_t                    m_startTimeUs = 0;
  };
}