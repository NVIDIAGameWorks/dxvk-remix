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
#include "remix_state.h"
#include "log/log.h"
#include "util_messagechannel.h"
#include "di_hook.h"

using namespace bridge_util;

uint64_t RemixState::m_state = 0;

void RemixState::init(MessageChannelBase& msgChannel) {
  msgChannel.registerHandler(kUIActiveMsgName,
    [](uint32_t wParam, uint32_t lParam) {
      if (wParam & 1) {
        m_state |= RemixStateBits::UIActive;
        Logger::info("Remix UI activated.");
        DI::unsetCooperativeLevel();
      } else {
        m_state &= ~RemixStateBits::UIActive;
        Logger::info("Remix UI deactivated.");
        DI::resetCooperativeLevel();
      }
      return true;
    });
}
