#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <algorithm>
#include "rtx_overlay_window.h"
#include "../imgui/dxvk_imgui.h"
#include "imgui/imgui_impl_win32.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace dxvk {
// Custom window events used to perform actions for showing/hiding the overlay window in the message pump thread.
#define WM_REMIX_HIDE_OVERLAY (WM_USER+0x7E1+1)
#define WM_REMIX_SHOW_OVERLAY (WM_USER+0x7E1+2)

static LRESULT CALLBACK sWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  if (msg == WM_NCCREATE) {
    auto cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
    SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
    return TRUE;
  }
  auto* self = reinterpret_cast<GameOverlay*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));
  return self ? self->overlayWndProc(msg, wParam, lParam) : DefWindowProcW(hWnd, msg, wParam, lParam);
}

static bool getWindowRect(HWND hwnd, RECT* out) {
  RECT rcClient {};
  if (!GetClientRect(hwnd, &rcClient)) {
    return false;
  }

  // top-left and bottom-right in client coordinates
  POINT tl { rcClient.left,  rcClient.top };
  POINT br { rcClient.right, rcClient.bottom };

  // convert to screen coordinates
  if (!ClientToScreen(hwnd, &tl)) {
    return false;
  }
  if (!ClientToScreen(hwnd, &br)) {
    return false;
  }

  out->left = tl.x;
  out->top = tl.y;
  out->right = br.x;
  out->bottom = br.y;
  return true;
}

GameOverlay::GameOverlay(const wchar_t* className, ImGUI* pImgui)
  : m_className(className), m_pImgui(pImgui) {
}

GameOverlay::~GameOverlay() {
  m_running.store(false);
  
  if (m_hwnd) {
    PostMessageW(m_hwnd, WM_CLOSE, 0, 0);
  }

  if (m_thread.joinable()) {
    m_thread.join();
  }
}

void GameOverlay::show() {
  if (!m_hwnd || !m_gameHwnd) {
    return;
  }

  RECT cr {};
  if (!getWindowRect(m_gameHwnd, &cr)) {
    return;
  }

  m_w = (UINT) std::max<int>(1, cr.right - cr.left);
  m_h = (UINT) std::max<int>(1, cr.bottom - cr.top);
  const int x = cr.left;
  const int y = cr.top;

  // Position/size overlay to match the game's *client* area.
  SetWindowPos(m_hwnd, HWND_TOPMOST, x, y, m_w, m_h, SWP_NOACTIVATE | SWP_NOOWNERZORDER);
}

void GameOverlay::hide() {
  SetWindowPos(hwnd(), HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
  ShowWindow(m_hwnd, SW_HIDE);
}

void GameOverlay::gameWndProcHandler(HWND gameHwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  if (gameHwnd != m_gameHwnd) {
    return;
  }

  // Handle resizing etc.. to match game window
  switch(msg) {
  case WM_ACTIVATE:
  case WM_ACTIVATEAPP:
  {
    const bool becameActive = (wParam != 0);

    // Foreground window guard to avoid hiding when the overlay takes focus
    HWND fg = GetForegroundWindow();
    DWORD fgPid = 0, gamePid = 0, ovlPid = 0;
    GetWindowThreadProcessId(fg, &fgPid);
    GetWindowThreadProcessId(m_gameHwnd, &gamePid);
    GetWindowThreadProcessId(m_hwnd, &ovlPid);

    const bool foregroundIsUs = (fg == m_hwnd) || (fgPid == ovlPid);
    if (becameActive) {
      // Only do work if were not already visible / in correct z-band
      PostMessage(hwnd(), WM_REMIX_SHOW_OVERLAY, 0, 0);
    } else if (!foregroundIsUs) {
      PostMessage(hwnd(), WM_REMIX_HIDE_OVERLAY, 0, 0);
    }
    return;
  }

  // hide while the user drags the window around.
  case WM_ENTERSIZEMOVE:
    // Final snap after interactive move/resize.
  case WM_EXITSIZEMOVE:
    // Borderless/fullscreen toggles often change styles; resnap just in case.
  case WM_STYLECHANGED:
    // Mode/refresh changes; your handler will resize swapchain if needed.
  case WM_DISPLAYCHANGE:
    // If you scale UI with DPI, rebuild fonts elsewhere; here we just snap.
  case WM_DPICHANGED:
  // covers move/size/z-order changes
  case WM_WINDOWPOSCHANGED:
  case WM_MOVE:
  case WM_SIZE:
  {
    // Hide while minimized; otherwise snap/resize swapchain via your handler
    if ((msg == WM_SIZE && wParam == SIZE_MINIMIZED) || (msg == WM_ENTERSIZEMOVE)) {
      PostMessage(hwnd(), WM_REMIX_HIDE_OVERLAY, 0, 0);
    } else {
      PostMessage(hwnd(), WM_REMIX_SHOW_OVERLAY, 0, 0);
    }
    return;
  }
  }
}

void GameOverlay::update(HWND gameHwnd) {
  if (m_gameHwnd == 0) {
    m_gameHwnd = gameHwnd;
    // Spawn UI thread
    m_thread = std::thread([this] { windowThreadMain(); });
  }
}

LRESULT GameOverlay::overlayWndProc(UINT msg, WPARAM wParam, LPARAM lParam) {
  switch (msg) {
  case WM_REMIX_SHOW_OVERLAY:
    show();
    return 0;
  case WM_REMIX_HIDE_OVERLAY:
    hide();
    return 0;
  case WM_DESTROY:
    PostQuitMessage(0);
    return 0;
  }

  if (ImGui_ImplWin32_WndProcHandler(m_hwnd, msg, wParam, lParam)) {
    return 0;
  }

  return DefWindowProcW(m_hwnd, msg, wParam, lParam);
}

void GameOverlay::windowThreadMain() {
  HINSTANCE hInst;
  GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>(&sWndProc), &hInst);

  // Register class
  WNDCLASSW wc {};
  wc.lpfnWndProc = &sWndProc;
  wc.hInstance = hInst;
  wc.lpszClassName = m_className;
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  RegisterClassW(&wc);

  // Size over the game
  RECT grc {}; 
  getWindowRect(m_gameHwnd, &grc);
  m_w = std::max<LONG>(1, grc.right - grc.left);
  m_h = std::max<LONG>(1, grc.bottom - grc.top);

  const DWORD ex = WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_LAYERED | WS_EX_TRANSPARENT;
  const DWORD st = WS_POPUP;
  m_hwnd = CreateWindowExW(ex, m_className, L"RTX Remix Overlay", st, grc.left, grc.top, m_w, m_h, nullptr, nullptr, hInst, this);

  if (!m_hwnd) {
    Logger::err(str::format("Failed to create ww: ", m_className));
    return;
  }

  // Fully transparent
  SetLayeredWindowAttributes(m_hwnd, 0, 0 /*alpha*/, LWA_ALPHA);

  show();

  RAWINPUTDEVICE rid[2] {};
  // Mouse
  rid[0].usUsagePage = 0x01;
  rid[0].usUsage = 0x02;
  rid[0].dwFlags = RIDEV_INPUTSINK;   // receive even when not focused
  rid[0].hwndTarget = m_hwnd;
  // Keyboard
  rid[1].usUsagePage = 0x01;
  rid[1].usUsage = 0x06;
  rid[1].dwFlags = RIDEV_INPUTSINK | RIDEV_NOLEGACY;
  rid[1].hwndTarget = m_hwnd;

  if (!RegisterRawInputDevices(rid, 2, sizeof(RAWINPUTDEVICE))) {
    Logger::err(str::format("Failed to register raw input for overlay window: ", m_className));
    return;
  }

  MSG msg {};
  while (m_running.load()) {
    // Pump pending messages for thread 
    while (GetMessage(&msg, NULL, 0, 0)) {
      if (msg.message == WM_QUIT) {
        m_running.store(false); 
        break;
      }
      TranslateMessage(&msg);
      DispatchMessage(&msg);
    }
  }

  if (m_hwnd) {
    DestroyWindow(m_hwnd); 
    m_hwnd = nullptr;
  }

  UnregisterClassW(m_className, hInst);
}
} // namespace dxvk