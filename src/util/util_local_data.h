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
#include <functional>
#include <mutex>
#include <string>

#include "config/config.h"

namespace dxvk {

  // Persistent key-value storage in %LOCALAPPDATA%\NVIDIA\RTXRemix\user_settings.
  // Two scopes are available:
  //   LocalData::app()    -- per-game file, keyed by a hash of RtxFileSys::rootPath().
  //   LocalData::shared() -- single file shared across all Remix apps on this machine.
  class LocalData {
  public:
    class Scope {
    public:
      using PathFn = std::function<std::string()>;

      explicit Scope(PathFn pathFn)
        : m_pathFn(std::move(pathFn)) {
      }

      void load();
      void save();

      template<typename T>
      T get(const char* key, T fallback = T()) {
        ensureLoaded();
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_config.getOption<T>(key, fallback);
      }

      template<typename T>
      void set(const char* key, T value) {
        ensureLoaded();
        std::lock_guard<std::mutex> lock(m_mutex);
        m_config.setOption(key, value);
      }

    private:
      void ensureLoaded();

      PathFn m_pathFn;
      Config m_config;
      std::atomic<bool> m_loaded{ false };
      std::mutex m_mutex;
    };

    static Scope& app();
    static Scope& shared();
  };

}
