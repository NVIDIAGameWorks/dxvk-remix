/*
 * Copyright (c) 2022-2023, NVIDIA CORPORATION. All rights reserved.
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

#include "config/config.h"
#include "log/log.h"

namespace ServerOptions {
  inline bool getUseVanillaDxvk() {
    static const bool useVanillaDxvk =
      bridge_util::Config::getOption<bool>("server.useVanillaDxvk", false);
    return useVanillaDxvk;
  }

  // When the server shuts down after the client process has exited due to a crash
  // or other unexpected event it will try to shut itself down gracefully by disabling
  // the bridge and letting the command processing loop exit cleanly. However, in certain
  // cases this may never happen because the command loop itself might be in a deadlock
  // waiting on another thread to complete, and therefore the server would never exit.
  // These shutdown values are used to give the server a certain amount of time before
  // force quitting the application.
  inline uint32_t getShutdownTimeout() {
    static const uint32_t shutdownTimeout =
      bridge_util::Config::getOption<uint32_t>("server.shutdownTimeout", 100);
    return shutdownTimeout;
  }
  inline uint32_t getShutdownRetries() {
    static const uint32_t shutdownRetries =
      bridge_util::Config::getOption<uint32_t>("server.shutdownRetries", 50);
    return shutdownRetries;
  }
}