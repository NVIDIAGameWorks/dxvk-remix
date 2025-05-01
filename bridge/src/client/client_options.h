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

#include <assert.h>
#include <sstream>

#include "config/config.h"
#include "log/log.h"
#include "di_hook.h"

#include <d3d9.h>

namespace ClientOptions {
  inline bool getUseVanillaDxvk() {
    return bridge_util::Config::getOption<bool>("client.useVanillaDxvk", false);
  }

  inline bool getSetExceptionHandler() {
    return bridge_util::Config::getOption<bool>("client.setExceptionHandler", false);
  }

  inline bool getHookMessagePump() {
    return bridge_util::Config::getOption<bool>("client.hookMessagePump", false);
  }

  inline bool getOverrideCustomWinHooks() {
    return bridge_util::Config::getOption<bool>("client.overrideCustomWinHooks", false);
  }

  inline bool getDisableExclusiveInput() {
    return bridge_util::Config::getOption<bool>("client.DirectInput.disableExclusiveInput", false);
  }

  inline bool getEnableBackbufferCapture() {
    return bridge_util::Config::getOption<bool>("client.enableBackbufferCapture", false);
  }

  inline DI::ForwardPolicy getForwardDirectInputMousePolicy() {
    return (DI::ForwardPolicy)bridge_util::Config::getOption<int>("client.DirectInput.forward.mousePolicy", DI::RemixUIActive);
  }
  
  inline DI::ForwardPolicy getForwardDirectInputKeyboardPolicy() {
    return (DI::ForwardPolicy)bridge_util::Config::getOption<int>("client.DirectInput.forward.keyboardPolicy", DI::RemixUIActive);
  }

  inline bool getForceWindowed() {
    return bridge_util::Config::getOption<bool>("client.forceWindowed", false);
  }

  inline bool getEnableDpiAwareness() {
    return bridge_util::Config::getOption<bool>("client.enableDpiAwareness", true);
  }

  // If set, the space for data for dynamic buffer updates will be preallocated on data channel
  // and redundant copy will be avioded. However, because D3D applications are not obliged
  // to write the entire locked region this optimization is NOT considered safe and may
  // not always work.
  inline bool getOptimizedDynamicLock() {
    return bridge_util::Config::getOption<bool>("client.optimizedDynamicLock", false);
  }
}