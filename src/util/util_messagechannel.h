/*
* Copyright (c) 2022-2024, NVIDIA CORPORATION. All rights reserved.
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

#include "util_version.h"

#include <stdint.h>
#include <string>
#include <unordered_map>
#include <functional>
#include <mutex>

// The code is shared between Remix Bridge and Remix Renderer
#if defined(REMIX_BRIDGE_CLIENT) || defined(REMIX_BRIDGE_SERVER)
#include <windows.h>
#include <thread>
typedef std::thread ThreadType;
#define UTIL_NS bridge_util
#else
#include "thread.h"
typedef dxvk::thread ThreadType;
#define UTIL_NS dxvk
#endif

namespace version {
  static constexpr uint64_t messageChannelV = 1;
}

namespace UTIL_NS {
  // Bidirectional communication channel based on Windows GetMessage/PostThreadMessage.
  // Should only be used for low-frequency messaging like input and window messages.
  // 
  // Bidirectional communication:
  //    Client and server instances are created with the same handshake message name.
  //    Client must have a window and call the onMessage() handler from its window proc.
  //    Server must know client's window handle. The protocol is initiated from server side
  //    using the handshake message which delivers server thread id as a parameter to client.
  //    Client receives the handshake message with the server thread id and may use
  //    for sending messages to the server side.
  //    Quirk: client window proc MUST start onMessage() processing BEFORE server
  //    handshake initiation.
  //
  // One-way client -> server communication:
  //    No handshake message is necessary. Client may be only given the server thread id.
  //    Server will not be able to send messages to the client.
  //
  class MessageChannelBase {
  public:
    using HandlerType = std::function<bool(uint32_t, uint32_t)>;

    bool onMessage(uint32_t msg, uint32_t wParam, uint32_t lParam);

    bool registerHandler(uint32_t msg, HandlerType&& handler);
    bool registerHandler(const char* msg, HandlerType&& handler);
    void removeHandler(uint32_t msg);
    void removeHandler(const char* msg);

  protected:
    MessageChannelBase() = default;
    explicit MessageChannelBase(const char* handshakeMsgName);

    uint32_t getMessageId(const char* msgName);
    
    const char* m_handshakeMsgName = nullptr;
    uint32_t m_handshakeMsgId = 0;

    mutable std::recursive_mutex m_accessMutex;

    std::unordered_map<std::string, uint32_t> m_msgs;
    std::unordered_map<uint32_t, HandlerType> m_handlers;
  };

  class MessageChannelServer : public MessageChannelBase {
    static constexpr uint32_t kHandshakeTimeoutMs = 5000;
  public:
    using WindowMessageHandlerType =
      std::function<LRESULT(HWND, UINT, WPARAM, LPARAM)>;

    MessageChannelServer() = default;
    explicit MessageChannelServer(const char* handshakeMsgName)
    : MessageChannelBase(handshakeMsgName)
    {}
    ~MessageChannelServer();

    bool init(HWND window, WindowMessageHandlerType&& windowHandler);

    bool send(const char* msgName, uint32_t wParam, uint32_t lParam);
    bool send(uint32_t msg, uint32_t wParam, uint32_t lParam);

    bool canSend() const {
      return m_clientWindow != nullptr;
    }

    uint32_t getWorkerThreadId() const {
      return m_workerThreadId;
    }

  private:
    bool handshake();
    void workerJob();

    HWND m_clientWindow = nullptr;
    WindowMessageHandlerType m_windowHandler;

    ThreadType m_worker;
    uint32_t m_workerThreadId = 0;

    bool m_isDestroying = false;
  };

  class MessageChannelClient : public MessageChannelBase {
  public:
    MessageChannelClient() = delete;
    explicit MessageChannelClient(const char* handshakeMsgName);
    explicit MessageChannelClient(uint32_t serverThreadId)
    : m_serverThreadId(serverThreadId)
    {}

    bool send(uint32_t msg, uint32_t wParam, uint32_t lParam);
    bool send(const char* msgName, uint32_t wParam, uint32_t lParam);

    bool canSend() const {
      return m_serverThreadId != 0;
    }

  private:
    uint32_t m_serverThreadId = 0;
  };
}
