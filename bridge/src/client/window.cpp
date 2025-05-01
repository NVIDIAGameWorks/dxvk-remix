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

#include "window.h"

#include "detours_common.h"
#include "di_hook.h"
#include "message_channels.h"
#include "remix_state.h"
#include "swapchain_map.h"

#include "log/log.h"

#include "util_monitor.h"

#include <unordered_map>
#include <mutex>
#include <d3d9.h>

using namespace bridge_util;
using namespace logger_strings::WndProc;

extern std::mutex gSwapChainMapMutex;
extern SwapChainMap gSwapChainMap;

namespace WndProc {
namespace {

HWND g_hwnd = nullptr;
WNDPROC g_gameWndProc = nullptr;
bool g_bActivateProcessed = false;

// reinterpret_cast wrappers
template<typename T>
static inline LONG asLong(T p) {
  return reinterpret_cast<LONG>(p);
}
template<typename T>
static inline WNDPROC asWndProcP(T p) {
  return reinterpret_cast<WNDPROC>(p);
}
static inline bool isSet() {
  return g_hwnd && g_gameWndProc;
}


//////////////////////////////////
// New Set-/GetWindowLong impls //
//////////////////////////////////

template<bool bUnicode>
static LONG WINAPI NewSetWindowLong(_In_ HWND hWnd, _In_ int nIndex, _In_ LONG dwNewLong) {
  if(nIndex == GWLP_WNDPROC) {
    // If we haven't yet set the RemixWndProc, as is evident by g_gameWndProc being invalid, then just
    // call OrigSetWindowLong as usual
    if(isSet()) {
      // We only handle cases wherein the window's handle matches the window's handle 
      // used in D3DDEVICE_CREATION_PARAMETERS or D3DPRESENT_PARAMETERS
      if(hWnd != g_hwnd) {
        Logger::debug(format_string(kStr_newSetWindowLong_settingHwnd, hWnd, g_hwnd));
      }
      else {
        auto oldGameWndProc = asLong(g_gameWndProc);
        g_gameWndProc = asWndProcP(dwNewLong);
        Logger::debug(format_string(kStr_newSetWindowLong_settingWndProc, g_gameWndProc, oldGameWndProc));
        return oldGameWndProc;
      }
    }
  }
  if constexpr (bUnicode) {
    return OrigSetWindowLongW(hWnd, nIndex, dwNewLong);
  } else {
    return OrigSetWindowLongA(hWnd, nIndex, dwNewLong);
  }
}
DETOURS_FUNC__UNICODE(SetWindowLong, NewSetWindowLong<true>, NewSetWindowLong<false>);

template<bool bUnicode>
static LONG WINAPI NewGetWindowLong(_In_ HWND hWnd, _In_ int nIndex) {
  if(nIndex == GWLP_WNDPROC) {
    // If we haven't yet set the RemixWndProc, as is evident by g_gameWndProc being invalid, then just
    // call OrigGetWindowLong as usual
    if(isSet()) {
      Logger::debug(format_string(kStr_newGetWindowLong_gettingWndProc, g_gameWndProc));
      // We only handle cases wherein the window's handle matches the window's handle 
      // used in D3DDEVICE_CREATION_PARAMETERS or D3DPRESENT_PARAMETERS
      if (hWnd == g_hwnd)
        return asLong(g_gameWndProc);
    }
  }
  if constexpr (bUnicode) {
    return OrigGetWindowLongW(hWnd, nIndex);
  } else {
    return OrigGetWindowLongA(hWnd, nIndex);
  }
}
DETOURS_FUNC__UNICODE(GetWindowLong, NewGetWindowLong<true>, NewGetWindowLong<false>);


/////////////////////////////////
// Detours attaching/detaching //
/////////////////////////////////
static bool attach() {
  bool bSuccess = true;
  DetourTransactionBegin();
  DetourUpdateThread(GetCurrentThread());
  bSuccess &= DETOURS_ATTACH__UNICODE(SetWindowLong);
  bSuccess &= DETOURS_ATTACH__UNICODE(GetWindowLong);
  DetourTransactionCommit();
  return bSuccess;
}

static bool detach() {
  bool bSuccess = true;
  DetourTransactionBegin();
  DetourUpdateThread(GetCurrentThread());
  bSuccess &= DETOURS_DETACH__UNICODE(SetWindowLong);
  bSuccess &= DETOURS_DETACH__UNICODE(GetWindowLong);
  DetourTransactionCommit();
  return bSuccess;
}

static bool reattach() {
  detach();
  attach();
}


/////////////////////////
// Remix WndProc funcs //
/////////////////////////

// Logic for handling some window messages that often cause problems
void windowMsg(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  std::scoped_lock lock(gSwapChainMapMutex);
  if (gSwapChainMap.find(hWnd) != gSwapChainMap.end()) {
    /*
    * The following lines are adapted from source in the DXVK repo
    * at https://github.com/doitsujin/dxvk/blob/master/src/d3d9/d3d9_window.cpp
    */
    if (msg == WM_DESTROY) {
      gSwapChainMap.erase(hWnd);
    } else {
      struct WindowDisplayData data = gSwapChainMap[hWnd];
      D3DPRESENT_PARAMETERS presParams = data.presParam;
      if (msg == WM_ACTIVATEAPP && !presParams.Windowed && !(msg == WM_NCCALCSIZE && wParam == TRUE)) {
        D3DDEVICE_CREATION_PARAMETERS create_parms = data.createParam;
        if (!(create_parms.BehaviorFlags & D3DCREATE_NOWINDOWCHANGES)) {
          if (wParam && !g_bActivateProcessed) {
            RECT rect;
            GetMonitorRect(GetDefaultMonitor(), &rect);
            SetWindowPos(hWnd, HWND_TOP, rect.left, rect.top, presParams.BackBufferWidth, presParams.BackBufferHeight,
              SWP_NOACTIVATE | SWP_NOZORDER | SWP_ASYNCWINDOWPOS);
            Logger::info(format_string("Window's position is reset. Left: %d, Top: %d, Width: %d, Height: %d", rect.left, rect.top, presParams.BackBufferWidth, presParams.BackBufferHeight));
            g_bActivateProcessed = true;
          } else if (!wParam) {
            if (IsWindowVisible(hWnd))
              ShowWindowAsync(hWnd, SW_MINIMIZE);
            g_bActivateProcessed = false;
          }
        }
      } else if (msg == WM_SIZE) {
        D3DDEVICE_CREATION_PARAMETERS create_parms = data.createParam;

        if (!(create_parms.BehaviorFlags & D3DCREATE_NOWINDOWCHANGES) && !IsIconic(hWnd)) {
          PostMessageW(hWnd, WM_ACTIVATEAPP, 1, GetCurrentThreadId());
        }
      }
    }
  }
}

// The actual logic for processing a Windows message for Remix purposes.
// Returns true when message was consumed by Remix and needs to be swallowed
// and so removed from the client application message pump (e.g. you don't want
// Remix UI clicks to accidentally click "Exit" in game menu)
bool remixMsg(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
#ifdef _DEBUG
  Logger::info(format_string("msg: %d, %d, %d, %d", msg, hWnd, wParam, lParam));
#endif

  const bool uiWasActive = RemixState::isUIActive();

  // Process remix renderer-related messages
  if (gpRemixMessageChannel->onMessage(msg, wParam, lParam)) {
    if (!uiWasActive && RemixState::isUIActive()) {
      // Remix UI has been activated - unstick modifier keys at application side
      auto unstick = [hWnd](uint32_t vk) {
        CallWindowProc(g_gameWndProc, hWnd, WM_KEYUP, vk,
          ((KF_REPEAT + KF_UP + MapVirtualKeyA(vk, MAPVK_VK_TO_VSC)) << 16) + 1);
      };

      unstick(VK_CONTROL);
      unstick(VK_SHIFT);
      unstick(VK_INSERT);

      // To be able to ignore target app WinHooks,
      // bridge WinHooks must be on a top of the hook chain.
      // So reattach bridge WinHooks each time, as the app might
      // set and reset its own hooks at any moment
      InputWinHooksReattach();
    }

    // Message was handled - bail out
    return true;
  }

  // Process server-related messages
  gpServerMessageChannel->onMessage(msg, wParam, lParam);

  if (RemixState::isUIActive()) {
    // ImGUI attempts to track when mouse leaves the window area using Windows API.
    // Some games with DirectInput in windowed mode may receive a WM_MOUSELEAVE message
    // after every WM_MOUSEMOVE message and this will result in ImGUI mouse cursor
    // toggling between -FLT_MAX and current mouse position.
    // To WAR it just swallow the WM_MOUSELEAVE messages when Remix UI is active.
    if (msg == WM_MOUSELEAVE) {
      // Swallow message 
      return true;
    }

    // Game overlay style message swallowing section
    {
      if (msg == WM_SYSCOMMAND) {
        // Swallow window move and size messages when UI is active.
        if (wParam == SC_MOVE || wParam == SC_SIZE || wParam == 0xF012 ||
            wParam == SC_MINIMIZE || wParam == SC_MAXIMIZE) {
          return true;
        }
      }

      // Process WM_NCMOUSEMOVE to WM_NCXBUTTONDBLCLK
      if ((msg & 0xFFA0) == 0x00A0) {
        // Swallow all non-client window messages when UI is active.
        switch (wParam) {
        case HTCLOSE:
          // Only Close button is allowed
          break;
        default:
          return true;
        }
      }
    }
  }

  // WAR: on Win11 preview build 25236 the WM_INPUT message sent to a thread proc
  // causes a WIN32K_CRITICAL_FAILURE bug check. This could creep into winnext
  // release so let's just block it here since we do not need this message anyway
  // on Remix renderer side.
  const bool doForward = msg != WM_INPUT;

  // Forward to remix renderer
  if (doForward) {
    gpRemixMessageChannel->send(msg, wParam, lParam);
  }

  // Block the input message when Remix UI is active
  if (RemixState::isUIActive() && isInputMessage(msg)) {
    // Block all input except ALT key up event.
    // ALT is a very special key, we must pass the key up event for ALT or risking
    // to stop receiving mouse events.
    if (msg != WM_KEYUP || wParam != VK_MENU) {
      return true;
    }
  }
  return false;
}

LRESULT WINAPI RemixWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  const bool isUnicode = IsWindowUnicode(hWnd);
  LRESULT lresult = 0;

  if (msg == WM_ACTIVATEAPP || msg == WM_SIZE || msg == WM_DESTROY) {
    windowMsg(hWnd, msg, wParam, lParam);
  }
  const bool bSwallowMsg = remixMsg(hWnd, msg, wParam, lParam);
  if (bSwallowMsg) {
    lresult = !isUnicode ? DefWindowProcA(hWnd, msg, wParam, lParam) :
                           DefWindowProcW(hWnd, msg, wParam, lParam);
  } else {
    lresult = !isUnicode ? CallWindowProcA(g_gameWndProc, hWnd, msg, wParam, lParam) :
                           CallWindowProcW(g_gameWndProc, hWnd, msg, wParam, lParam);
  }
  return lresult;
}

}

////////////////////
// External funcs //
////////////////////

bool init() {
  if(!attach()) {
    Logger::err(kStr_init_attachErr);
    return false;
  }
  // Don't set WndProc here, as we don't know which window the game wants to use. Wait until a device is created.
  return true;
}

bool terminate() {
  if(!detach()) {
    Logger::err(kStr_terminate_detachErr);
    return false;
  }
  if(g_gameWndProc) {
    if(IsWindow(g_hwnd)) {
      if(!unset()) {
        return false;
      }
      assert(g_gameWndProc == nullptr);
    } else {
      g_gameWndProc = nullptr;
    }
  }
  return true;
}

bool set(HWND hwnd) {
  assert(hwnd);
  assert(IsWindow(hwnd));

  // If set is called a subsequent time without an unset in between, we need to null out our
  // current handles, so we implicitly call unset
  const bool bHwndReset = g_hwnd && (hwnd != g_hwnd); // In case hWnd changes
  if(g_gameWndProc || bHwndReset) {
    Logger::warn(kStr_set_implicitWarn);
    unset();
  }

  // Whether by explicit or implicit unset, assert these values have been undone
  assert(!g_hwnd);
  assert(!g_gameWndProc);
  g_hwnd = hwnd;
  g_gameWndProc = asWndProcP(OrigSetWindowLongA(hwnd, GWLP_WNDPROC, asLong(RemixWndProc)));

  // If the original SetWindowLong fails, then something is going on
  if(!g_gameWndProc) {
    Logger::err(kStr_set_failedErr);
    return false;
  }

  // Fix up DirectInput forwarding if setting has succeeded
  DInputSetDefaultWindow(hwnd);

  Logger::debug(format_string(kStr_set_settingWndProc, RemixWndProc, g_gameWndProc));

  return true;
}

bool unset() {
  assert(g_hwnd);

  // Put the game's intended WndProc back on top of the WndProc stack
  auto prevWndProc = asWndProcP(OrigSetWindowLongA(g_hwnd, GWLP_WNDPROC, asLong(g_gameWndProc)));
  assert(RemixWndProc == prevWndProc);
  
  if(!prevWndProc) {
    // It would be weird to have gotten here, but that's why we throw a warning
    Logger::warn(kStr_unset_wndProcInvalidWarn);
  }

  // Clean out the globals
  g_hwnd = nullptr;
  g_gameWndProc = nullptr;

  Logger::debug(format_string(kStr_unset_unsettingWndProc, prevWndProc, g_gameWndProc));

  return true;
}

bool invokeRemixWndProc(UINT msg, WPARAM wParam, LPARAM lParam) {
  return remixMsg(g_hwnd, msg, wParam, lParam);
}

}
