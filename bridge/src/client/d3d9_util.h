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
#ifndef D3D9_UTIL_H_
#define D3D9_UTIL_H_

#ifndef DXPIXELVER
#define DXPIXELVER 100
#endif

#include "client_options.h"
#include "util_bridge_assert.h"
#include "util_bridgecommand.h"
#include "util_sharedheap.h"
#include "util_once.h"
#include "util_texture_and_volume.h"

#include <array>
#include <optional>
#include <assert.h>
#include <windowsx.h>
#include <d3d9types.h>
#include <stack>

using namespace bridge_util;

#if defined(_DEBUG) || defined(DEBUGOPT)

class FunctionEntryExitLogger {
public:
  FunctionEntryExitLogger(const std::string functionName, void* thiz);
  ~FunctionEntryExitLogger();
private:
  std::string m_functionName;
  void* m_thiz;
  static std::map<std::thread::id, std::atomic<size_t>> s_counters;
};

#define LogMissingFunctionCall() ONCE(_LogMissingFunctionCall(__FUNCTION__))
#define LogMissingReadFunctionCall() ONCE(_LogMissingFunctionCall(__FUNCTION__, false))

#define LogFunctionCall() FunctionEntryExitLogger _feeLogger(__FUNCTION__, this);
#define LogStaticFunctionCall() FunctionEntryExitLogger _feeLogger(__FUNCTION__, nullptr);
#else
#define LogMissingFunctionCall()
#define LogMissingReadFunctionCall()
#define LogFunctionCall()
#define LogStaticFunctionCall()
#endif

static void _LogMissingFunctionCall(const std::string& functionName, bool errorlogLevel = true) {
  std::string functionNameNoLambda = functionName;
  // Check if the function name contains an anonymous function
  const size_t lambdaNamePos = functionName.find("lambda_");
  if (lambdaNamePos != std::string::npos && lambdaNamePos + 32 < functionName.length()) {
    // Remove the 32 character unique identifier for lambdas to shorten the type name a bit
    functionNameNoLambda.erase(lambdaNamePos + 6, 33);
  }
  if (errorlogLevel) {
    Logger::err(std::string("Missing function call intercepted: ") + functionNameNoLambda);
  } else {
    Logger::warn(std::string("Missing function call intercepted: ") + functionNameNoLambda);
  }
}

static void _LogFunctionCall(const std::string& functionName, void* thiz) {
  std::stringstream tid;
  tid << "[" << std::this_thread::get_id() << "]";

  Logger::info(format_string("%s[%p] %s", tid.str().c_str(), thiz, functionName.c_str()));
}

static uint32_t GetIndexCount(D3DPRIMITIVETYPE type, UINT count) {
  switch (type) {
  default:
  case D3DPT_TRIANGLELIST:  return count * 3;
  case D3DPT_POINTLIST:     return count;
  case D3DPT_LINELIST:      return count * 2;
  case D3DPT_LINESTRIP:     return count + 1;
  case D3DPT_TRIANGLESTRIP: return count + 2;
  case D3DPT_TRIANGLEFAN:   return count + 2;
  }
}

static void SetWindowMode(HWND hwnd, const bool windowed, const LONG width, const LONG height) {
  if (hwnd == 0) {
    hwnd = ::GetForegroundWindow(); // Get current window
  }

  DWORD dwWndStyle = GetWindowStyle(hwnd);
  if (!windowed) {
    // Determine the new window style
    dwWndStyle = WS_POPUP | WS_VISIBLE;

    // Change the window style
    ::SetWindowLong(hwnd, GWL_STYLE, dwWndStyle);
    ::SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, width, height, SWP_NOREDRAW | SWP_NOACTIVATE);
  } else {
    // Determine the new window style
    dwWndStyle &= ~WS_POPUP;
    dwWndStyle |= WS_VISIBLE | WS_CAPTION | WS_MINIMIZEBOX | WS_SYSMENU;

    // Change the window style
    ::SetWindowLong(hwnd, GWL_STYLE, dwWndStyle);

    // Resize the window so that the client area is width x height
    RECT rect = { 0, 0, width, height };
    AdjustWindowRectEx(&rect, GetWindowStyle(hwnd), GetMenu(hwnd) != NULL, GetWindowExStyle(hwnd));

    // Adjust the size of the window
    SetWindowPos(hwnd, NULL, 0, 0, rect.right - rect.left, rect.bottom - rect.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
  }
}

extern void cleanLssGarbage();

static uint32_t CalculateNumMipLevels(uint32_t width, uint32_t height = 1, uint32_t depth = 1) {
  const uint32_t maxDimension = std::max(depth, std::max(width, height));
  return static_cast<uint32_t>(std::ceil(std::log2(maxDimension))) + 1;
}

#endif // D3D9_UTIL_H_
