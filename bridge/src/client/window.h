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

#include <windows.h>
#include <stdint.h>

namespace WndProc {

inline static bool isInputMessage(uint32_t msg) {
  switch (msg) {
  case WM_KEYDOWN:
  case WM_KEYUP:
  case WM_SYSKEYDOWN:
  case WM_SYSKEYUP:
  case WM_SYSCHAR:
  case WM_LBUTTONDOWN:
  case WM_LBUTTONDBLCLK:
  case WM_LBUTTONUP:
  case WM_MBUTTONDOWN:
  case WM_MBUTTONDBLCLK:
  case WM_MBUTTONUP:
  case WM_RBUTTONDOWN:
  case WM_RBUTTONDBLCLK:
  case WM_RBUTTONUP:
  case WM_MOUSEWHEEL:
  case WM_MOUSEMOVE:
  case WM_CHAR:
  case WM_UNICHAR:
  case WM_MOUSELEAVE:
  case WM_MOUSEHOVER:
  case WM_INPUT:
    return true;
  }
  return false;
}


/////////////////////////
// Controlling WndProc //
/////////////////////////

// All below functions return bool success, which may or may not be useful for error checking.
//   e.g. since terminate() is likely called during DllDetach it doesn't matter too much if it fails
//        given that the game is probably closing

// Initialize the WndProc setting hooks. Should only be called once at beginning of time for the game
extern bool init();
// Undo the hooks established in init()
extern bool terminate();
// Set RemixWndProc for hwnd. Only one allowed at a time. Calling again implicitly undoes the previous
extern bool set(HWND hwnd);
// Undo previous set. 
extern bool unset();
// Directly invoke the RemixWndProc logic, really only useful for DirectInput forwarding
extern bool invokeRemixWndProc(UINT msg, WPARAM wParam, LPARAM lParam);

}
