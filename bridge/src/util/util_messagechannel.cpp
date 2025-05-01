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
#include "util_messagechannel.h"

#if defined(REMIX_BRIDGE_CLIENT) || defined(REMIX_BRIDGE_SERVER)
#include "config/global_options.h"
#else
#include "util_env.h"
#include "log/log.h"
#endif

using namespace UTIL_NS;

#if !defined(REMIX_BRIDGE_CLIENT) && !defined(REMIX_BRIDGE_SERVER)

template<typename... Args>
static std::string format_string(const std::string& format, Args... args) {
  int size_s = std::snprintf(nullptr, 0, format.c_str(), args...) + 1; // Extra space for '\0'
  if (size_s <= 0) {
    throw std::runtime_error("Error during formatting!");
  }
  auto size = static_cast<size_t>(size_s);
  auto buf = std::make_unique<char[]>(size);
  std::snprintf(buf.get(), size, format.c_str(), args...);
  return std::string(buf.get(), buf.get() + size - 1); // We don't want the '\0' inside
}

#endif

MessageChannelBase::MessageChannelBase(const char* handshakeMsgName)
  : m_handshakeMsgName(handshakeMsgName) {
  m_handshakeMsgId = getMessageId(handshakeMsgName);

  if (0 == m_handshakeMsgId) {
    Logger::err(format_string("Message channel for %s was not registered. "
                              "Short message exchange will not be available!",
                              handshakeMsgName));
  }
}

bool MessageChannelBase::registerHandler(uint32_t msg, HandlerType&& handler) {
  if (msg) {
    std::lock_guard<std::recursive_mutex> _(m_accessMutex);

    m_handlers[msg] = std::move(handler);
    return true;
  }

  return false;
}

bool MessageChannelBase::registerHandler(const char* msgName, HandlerType&& handler) {
  if (uint32_t msg = getMessageId(msgName)) {
    std::lock_guard<std::recursive_mutex> _(m_accessMutex);
    m_handlers[msg] = std::move(handler);
    return true;
  }

  Logger::err(format_string("Message handler %s was not registered!", msgName));

  return false;
}

void MessageChannelBase::removeHandler(const char* msgName) {
  std::lock_guard<std::recursive_mutex> _(m_accessMutex);
  m_handlers.erase(m_msgs[msgName]);
  m_msgs.erase(msgName);
}

void MessageChannelBase::removeHandler(uint32_t msg) {
  std::lock_guard<std::recursive_mutex> _(m_accessMutex);
  m_handlers.erase(msg);
}

uint32_t MessageChannelBase::getMessageId(const char* msgName) {
  uint32_t msg = 0;
  {
    std::lock_guard<std::recursive_mutex> _(m_accessMutex);

    auto it = m_msgs.find(msgName);

    if (it != m_msgs.end()) {
      msg = it->second;
    } else if (msg = ::RegisterWindowMessageA(msgName)) {
      m_msgs[msgName] = msg;
    }
  }

  if (msg == 0) {
    Logger::err(format_string("Message %s was not registered (%d)!",
                              msgName, GetLastError()));
  }

  return msg;
}

bool MessageChannelBase::onMessage(uint32_t msgId, uint32_t wParam,
                                   uint32_t lParam) {
  std::lock_guard<std::recursive_mutex> _(m_accessMutex);

  auto it = m_handlers.find(msgId);
  if (it != m_handlers.end()) {
    return it->second(wParam, lParam);
  }

  return false;
}

MessageChannelClient::MessageChannelClient(const char* handshakeMsgName)
  : MessageChannelBase(handshakeMsgName) {
  registerHandler(handshakeMsgName, [this](uint32_t wParam, uint32_t lParam) {
    m_serverThreadId = wParam;
    Logger::info(format_string("Message channel %s handshake complete.",
                               m_handshakeMsgName));
    return true;
  });
}

bool MessageChannelClient::send(uint32_t msg, uint32_t wParam, uint32_t lParam) {
  return ::PostThreadMessage(m_serverThreadId, msg, wParam, lParam) != FALSE;
}

bool MessageChannelClient::send(const char* msgName, uint32_t wParam, uint32_t lParam) {
  const uint32_t msg = getMessageId(msgName);

  if (msg) {
    return send(msg, wParam, lParam);
  }

  return false;
}

MessageChannelServer::~MessageChannelServer() {
  if (m_worker.joinable()) {
    ::PostThreadMessage(m_workerThreadId, WM_QUIT, 0, 0);
    m_worker.join();
  }
}

bool MessageChannelServer::init(HWND clientWindow,
                                WindowMessageHandlerType&& windowHandler) {
  if (clientWindow) {
    if (m_clientWindow == clientWindow) {
      return true;
    }

    if (m_handshakeMsgId == 0) {
      return false;
    }
  }

  m_clientWindow = clientWindow;
  m_windowHandler = std::move(windowHandler);
  m_worker = ThreadType([this] { workerJob(); });
  m_workerThreadId = ::GetThreadId((HANDLE) m_worker.native_handle());

  // It would be better to do handshake here, however because init is
  // usually called from SwapChain which is created at CreateDevice,
  // there's no guarantee that in bridge environment the client's window
  // thread is not blocked and handshake message can be delivered.
  //
  // Handshake will be performed from the worker thread instead.

  return true;
}

bool MessageChannelServer::handshake() {
  if (canSend()) {
    // Do handshake with a timeout
    LRESULT result = ::SendMessageTimeout(m_clientWindow, m_handshakeMsgId,
      m_workerThreadId, 0, SMTO_BLOCK, kHandshakeTimeoutMs, nullptr);

    if (result == 0) {
      Logger::err(format_string("Message channel %s handshake failed with %d.",
                                m_handshakeMsgName, GetLastError()));
      return false;
    }
  }

  // Handshake complete or running one-way communications
  return true;
}

void MessageChannelServer::workerJob() {
  if (!handshake()) {
    return;
  }

  Logger::info(format_string("Message channel %s established.",
                             m_handshakeMsgName));

  const HWND kCurrentThreadId = (HWND) (-1);

  MSG msg;
  while (GetMessage(&msg, kCurrentThreadId, 0, 0)) {
    TranslateMessage(&msg);

    if (onMessage(msg.message, msg.wParam, msg.lParam)) {
      continue;
    }

    if (m_windowHandler) {
      m_windowHandler(m_clientWindow, msg.message, msg.wParam, msg.lParam);
    }
  }
}

bool MessageChannelServer::send(const char* msgName,
                                uint32_t wParam, uint32_t lParam) {
  if (!canSend()) {
    return false;
  }

  const uint32_t msg = getMessageId(msgName);

  if (msg) {
    return send(msg, wParam, lParam);
  }

  return false;
}

bool MessageChannelServer::send(uint32_t msg,
                                uint32_t wParam, uint32_t lParam) {
  if (!canSend()) {
    return false;
  }
  
  LRESULT result = ::PostMessage(m_clientWindow, msg, wParam, lParam);

  if (result != 0) {
    return true;
  }

  Logger::err(format_string("Message %d was not sent (%d)!",
                            msg, GetLastError()));

  return false;
}
