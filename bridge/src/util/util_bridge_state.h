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

#include <mutex>

class BridgeState {
public:
  enum class ProcessState: uint32_t {
    NotInit,
    Init,
    Handshaking,
    Running,
    DoneProcessing,
    Exited,
  };

  static void setClientState(const ProcessState clientState) {
    get().m_clientState = clientState;
  }
  static ProcessState getClientState() {
    return get().m_clientState;
  }

  static void setServerState(const ProcessState serverState) {
    get().m_serverState = serverState;
  }

  static ProcessState getServerState() {
    return get().m_serverState;
  }

  static ProcessState getServerState_NoLock() {
    return get().m_serverState;
  }

private:
  inline static BridgeState* inst = nullptr;
  static BridgeState& get() {
    if (inst == nullptr) {
      inst = new BridgeState;
    }
    return *inst;
  }

  BridgeState() {
  }

  BridgeState(const BridgeState& rhs) = delete;
  BridgeState(const BridgeState&& rhs) = delete;

  ~BridgeState() {
  }

  ProcessState m_clientState = ProcessState::NotInit;
  ProcessState m_serverState = ProcessState::NotInit;
};
