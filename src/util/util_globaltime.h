#pragma once

#include "util_singleton.h"
#include <functional>
#include <chrono>

namespace dxvk {
  class GlobalTime : public Singleton<GlobalTime> {
    friend class Singleton<GlobalTime>;

    uint64_t                    m_lastUs = 0;
    uint64_t                    m_currentUs = 0;
    float                       m_deltaSec = 0.0f;
    uint64_t                    m_frameIdx = 0;
    float                       m_timeDeltaBetweenFrames = 0.f;
    std::function<uint64_t()>   m_source;

    // The time at which the application started. (really the first time GlobalTime::get() is called)
    uint64_t                    m_startTimeUs = 0;

    // default steady_clock in microseconds
    static uint64_t defaultSource() {
      using namespace std::chrono;
      auto e = steady_clock::now().time_since_epoch();
      return static_cast<uint64_t>(duration_cast<microseconds>(e).count());
    }

    // using a fixed time assuming a 60 FPS experience
    static uint64_t deterministicTimeSource() {
      return get().m_frameIdx * (1000000 / 60);
    }

    GlobalTime(const GlobalTime& other) = delete;

    GlobalTime() 
      : m_source(&GlobalTime::defaultSource) {
      m_lastUs = m_source();
      m_currentUs = m_lastUs;
      m_startTimeUs = m_lastUs;
    }

  public:

    void init(const float timeDeltaBetweenFrames) {
      if (timeDeltaBetweenFrames != 0.f) {
        m_timeDeltaBetweenFrames = timeDeltaBetweenFrames;
        resetDeterministicTimeSource();
      } else {
        resetRawTimeSource();
      }
    }

    // Call once per frame before queries
    void update() {
      m_lastUs = m_currentUs;
      m_currentUs = m_source();
      m_deltaSec = (m_currentUs - m_lastUs) * 1e-6f;
      ++m_frameIdx;
    }

    // scaled delta-t (s)
    float deltaTime() const {
      return m_deltaSec;
    }

    // scaled delta-t (ms)
    float deltaTimeMs() const {
      return m_deltaSec * 1000.f;
    }

    // whole micro-seconds since startup
    uint64_t absoluteTimeUs() const {
      return m_currentUs;
    }

    // whole milliseconds since startup
    uint64_t absoluteTimeMs() const {
      return m_currentUs / 1000;
    }

    // whole milliseconds since startup, ignoring deterministic time settings.
    uint64_t realTimeSinceStartMs() const {
      // Note: this returns the actual time since the application started.  This should be used for profiling and metrics.
      return (defaultSource() - m_startTimeUs) / 1000;
    }

  private:

    // override underlying time source (for tests)
    void resetDeterministicTimeSource() {
      m_source = &GlobalTime::deterministicTimeSource;
      m_lastUs = m_source();
      m_currentUs = m_lastUs;
    }

    // use standard real time
    void resetRawTimeSource() {
      m_source = &GlobalTime::defaultSource;
      m_lastUs = m_source();
      m_currentUs = m_lastUs;
    }
  };
}