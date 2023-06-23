/*
* Copyright (c) 2023, NVIDIA CORPORATION. All rights reserved.
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
#include <atomic>
#include "thread.h"

namespace dxvk {
  /**
    * \brief Implements a generic thread watchdog for tracking
    *        conditional changes where latency isn't a problem.
    *
    *  TimeoutMS: Time between each query of the condition
    * 
    *  condition: Conditions under which we should signal the watchdog
    *  threadName: Name of the watchdog thread
    * 
    *  Example usage:
    *   // Writes "Ping" into the log every second.
    *   Watchdog<1000> watchdog([]{return true;}, "ping every second");
    *   while(1) { if(watchdog.hasSignaled()) Logger::Info("Ping"); }
    * 
    */
  template<uint32_t TimeoutMS>
  class Watchdog {
    const std::chrono::milliseconds m_timeout { TimeoutMS };

    const std::function<bool()> m_condition;
    std::atomic<bool> m_running = false;
    std::atomic<bool> m_signaled = false;
    std::string m_threadName = "watchdog";
    dxvk::thread m_thread;

    void threadFunc() {
      env::setThreadName(m_threadName);

      while (m_running) {
        std::this_thread::sleep_for(m_timeout);

        if (m_running && m_condition()) {
          m_signaled = true;
        }
      }
    }

  public:
    Watchdog(const std::function<bool()>& condition, const std::string threadName) 
      : m_running (false)
      , m_condition (condition)
      , m_threadName (threadName) {
    }

    ~Watchdog() {
      stop();
    }

    // Start the watchdog thread.
    void start() {
      if (m_running)
        return;

      m_running = true;
      m_thread = dxvk::thread([this] { this->threadFunc(); });
    }

    // Stop the watchdog thread.
    void stop() {
      if (!m_running)
        return;

      m_running = false;
      
      if(m_thread.joinable())
        m_thread.join();
    }

    // Check whether the watchdog has signaled and clear the signal flag.
    bool hasSignaled() {
      return m_signaled.exchange(false);
    }
  };
} //dxvk