/*
 * Copyright (c) 2024, NVIDIA CORPORATION. All rights reserved.
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

#include "util_messagechannel.h"
#include "window.h"

#include "log/log.h"
#include "config/global_options.h"

#include "../../../src/util/util_crash_report.h"

#include <memory>
#include <stdint.h>

extern std::unique_ptr<bridge_util::MessageChannelClient> gpServerMessageChannel; // Message channel with the Bridge server
extern std::unique_ptr<bridge_util::MessageChannelClient> gpRemixMessageChannel;  // Message channel with the Remix renderer

static void initServerMessageChannel(const uint32_t serverThreadId) {
  assert(!gpServerMessageChannel);
  gpServerMessageChannel =
    std::make_unique<bridge_util::MessageChannelClient>(static_cast<uint32_t>(serverThreadId));
  {
    // Special handling for certain window messages to disable semaphore timeouts
    // when the game window is not currently active or in the foreground. Note
    // that using keyboard focus is more reliable than WM_ACTIVATE and also does
    // not lead to duplicate messages.
    gpServerMessageChannel->registerHandler(WM_KILLFOCUS, [](uintptr_t wParam, intptr_t lParam) {
      bridge_util::Logger::info("Client window became inactive, disabling timeouts for bridge client...");
      GlobalOptions::setInfiniteRetries(true);
      gpServerMessageChannel->send(WM_KILLFOCUS, wParam, lParam);
      return true;
    });

    gpServerMessageChannel->registerHandler((UINT) WM_SETFOCUS, [](uintptr_t wParam, intptr_t lParam) {
      Logger::info("Client window became active, reenabling timeouts for bridge client...");
      GlobalOptions::setInfiniteRetries(false);
      gpServerMessageChannel->send(WM_SETFOCUS, wParam, lParam);
      return true;
    });
  }
}

static void initRemixMessageChannel() {
  assert(!gpRemixMessageChannel);
  gpRemixMessageChannel = std::make_unique<MessageChannelClient>("UWM_REMIX_BRIDGE_REGISTER_THREADPROC_MSG");

  // The renderer sends this the moment it detects a GPU crash, before the slow Sentry capture.
  // Acting here — before OnServerExited fires — prevents Windows from ghosting the window during
  // the 10-15 seconds the server spends on Sentry upload before it exits. This is specifically
  // for GPU crashes; CPU crashes are handled via OnServerExited directly since the server dies
  // immediately and there is no early notification.
  gpRemixMessageChannel->registerHandler(dxvk::kGpuCrashNotifyMsgName, [](uintptr_t, intptr_t) {
    Logger::warn("Crash handler (GPU, early): preparing window for crash dialog");
    // This handler runs on the game's main (window-owning) thread via the WndProc, so the synchronous
    // window path is safe and takes effect immediately.
    WndProc::prepareForCrashDialog(/*async=*/false);
    return true;
  });
}
