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
#include <nmmintrin.h>
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

// Client-side copies of this size and larger come from VirtualAlloc when they may be freed while idle.
// Freeing a heap block returns no address space until its whole heap segment is empty.
constexpr size_t kMinIdleCopySize = 64 * 1024;

// Returns whether a client-side copy of this size may be freed while idle.
// The server sends a freed copy back in one response: up to 4 words of metadata, the blob's length word and the
// blob, which a wrap at the end of the ring can precede with up to the blob's size in skipped words. The server exits
// when one response does not fit its data ring (syncDataQueue), so the blob gets at most half of what is left of
// the ring after 8 words reserved for the metadata.
static bool isIdleCopySize(const size_t size) {
  constexpr size_t kWordSize = sizeof(bridge_util::DataQueue::BaseType);
  constexpr size_t kMetadataWords = 8;
  // The client's reader ring is the server's writer ring: the same shared memory, so the same size.
  const size_t ringWords = Bridge<BridgeId::Device>::getReaderChannel().data->get_total_size();
  const size_t blobWords = (size + kWordSize - 1) / kWordSize;
  return size >= kMinIdleCopySize && ringWords > kMetadataWords && blobWords <= (ringWords - kMetadataWords) / 2;
}

// Returns whether a device's idle pass is due, and if so sets lastPassMs to nowMs.
// The pass runs every idleMs / 2, but at most every 100 ms and at least once a second.
static bool isIdlePassDue(uint64_t& lastPassMs, const uint64_t nowMs, const uint32_t idleMs) {
  if (nowMs - lastPassMs < std::clamp<uint64_t>(idleMs / 2, 100, 1000)) {
    return false;
  }
  lastPassMs = nowMs;
  return true;
}

// Frees a client-side copy with the call that matches its allocation.
struct ClientCopyDeleter {
  bool virtualAlloc = false;

  void operator()(uint8_t* p) const {
    if (virtualAlloc) {
      VirtualFree(p, 0, MEM_RELEASE);
    } else {
      delete[] p;
    }
  }
};

// Returns the CRC32C of the bytes. Uses SSE4.2.
static uint32_t calcCrc32c(const uint8_t* pData, const size_t size) {
  uint32_t crc = 0xFFFFFFFFu;
  size_t i = 0;
  for (; i + 4 <= size; i += 4) {
    uint32_t value;
    memcpy(&value, pData + i, 4);
    crc = _mm_crc32_u32(crc, value);
  }
  for (; i < size; ++i) {
    crc = _mm_crc32_u8(crc, pData[i]);
  }
  return crc;
}

#endif // D3D9_UTIL_H_
