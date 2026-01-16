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
  return self ? self->overlayWndProc(hWnd, msg, wParam, lParam) : DefWindowProcW(hWnd, msg, wParam, lParam);
}

struct DpiCtxGuard {
  DPI_AWARENESS_CONTEXT prev {};
  DpiCtxGuard() {
    auto p = reinterpret_cast<DPI_AWARENESS_CONTEXT(WINAPI*)(DPI_AWARENESS_CONTEXT)>(GetProcAddress(GetModuleHandleA("user32.dll"), "SetThreadDpiAwarenessContext"));
    prev = p ? p(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2) : nullptr;
  }
  ~DpiCtxGuard() {
    auto p = reinterpret_cast<DPI_AWARENESS_CONTEXT(WINAPI*)(DPI_AWARENESS_CONTEXT)>(GetProcAddress(GetModuleHandleA("user32.dll"), "SetThreadDpiAwarenessContext"));
    if (p && prev) {
      p(prev);
    }
  }
};

static inline RECT vec4ToRectMinMax(const Vector4& v) {
  RECT rc { (LONG) std::floor(v.x), (LONG) std::floor(v.y), (LONG) std::ceil(v.z), (LONG) std::ceil(v.w) };
  if (rc.right < rc.left) std::swap(rc.right, rc.left);
  if (rc.bottom < rc.top) std::swap(rc.bottom, rc.top);
  return rc;
}

static inline bool isEmptyRect(const RECT& rc) {
  return rc.right <= rc.left || rc.bottom <= rc.top;
}

static inline int rectWidth(const RECT& rc) {
  return rc.right - rc.left;
}
static inline int rectHeight(const RECT& rc) {
  return rc.bottom - rc.top;
}

static inline RECT clientToScreenRect(HWND hwnd, RECT rcClient) {
  POINT pts[2] = { {rcClient.left, rcClient.top}, {rcClient.right, rcClient.bottom} };
  MapWindowPoints(hwnd, nullptr, pts, 2);
  return RECT { pts[0].x, pts[0].y, pts[1].x, pts[1].y };
}

static inline RECT clientRectScreen(HWND hwnd) {
  RECT c {}; GetClientRect(hwnd, &c);
  return clientToScreenRect(hwnd, c);
}

static inline RECT intersect(const RECT& a, const RECT& b) {
  RECT r { std::max(a.left,b.left), std::max(a.top,b.top),
          std::min(a.right,b.right), std::min(a.bottom,b.bottom) };
  if (isEmptyRect(r)) return RECT { 0,0,0,0 };
  return r;
}
static inline int area(const RECT& r) {
  return std::max(0, rectWidth(r)) * std::max(0, rectHeight(r));
}

GameOverlay::GameOverlay(const char* className, ImGUI* pImgui)
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

  DpiCtxGuard dpiGuard;

  const Vector4 sr(0.f);
  RECT raw = vec4ToRectMinMax(sr);

  RECT screenRc {};
  if (isEmptyRect(raw)) {
    // Fallback - full client area
    screenRc = clientRectScreen(m_gameHwnd);
  } else {
    // Build both candidates in screen space and choose the one that best overlaps the client rect
    RECT candScreen = raw;
    RECT candFromClient = clientToScreenRect(m_gameHwnd, raw); 
    RECT gameClientS = clientRectScreen(m_gameHwnd);

    int overlapA = area(intersect(candScreen, gameClientS));
    int overlapB = area(intersect(candFromClient, gameClientS));
    screenRc = (overlapB > overlapA) ? candFromClient : candScreen;
  }

  if (isEmptyRect(screenRc)) {
    hide();
    return;
  }

  if (m_debugDraw) {
    // Make it visible if debugging; invisible if not.
    SetLayeredWindowAttributes(m_hwnd, 0, m_debugAlpha, ULW_ALPHA);
    InvalidateRect(m_hwnd, nullptr, TRUE);
  }

  const int w = std::max(1, rectWidth(screenRc));
  const int h = std::max(1, rectHeight(screenRc));

  m_lastRect = screenRc;
  m_w = (UINT) w; m_h = (UINT) h;
  SetWindowPos(m_hwnd, HWND_TOPMOST, screenRc.left, screenRc.top, m_w, m_h, SWP_NOACTIVATE | SWP_NOOWNERZORDER);
  ShowWindow(m_hwnd, SW_SHOWNOACTIVATE);
}

void GameOverlay::hide() {
  if (!m_hwnd) {
    return;
  }

  if (m_mouseInsideOverlay) {
    m_mouseInsideOverlay = false;
    ImGui_ImplWin32_WndProcHandler(m_hwnd, WM_MOUSELEAVE, 0, 0);
  }

  SetWindowPos(m_hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
  ShowWindow(m_hwnd, SW_HIDE);
}

void GameOverlay::gameWndProcHandler(HWND gameHwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  if (gameHwnd != m_gameHwnd) {
    return;
  }

  auto postShowMsg = [this] { PostMessage(hwnd(), WM_REMIX_SHOW_OVERLAY, 0, 0); };
  auto postHideMsg = [this] { PostMessage(hwnd(), WM_REMIX_HIDE_OVERLAY, 0, 0); };

  switch (msg) {
  case WM_ACTIVATE:
  case WM_ACTIVATEAPP:
  {
    const bool becameActive = (wParam != 0);
    HWND fg = GetForegroundWindow();
    DWORD fgPid = 0, gamePid = 0, ovlPid = 0;
    GetWindowThreadProcessId(fg, &fgPid);
    GetWindowThreadProcessId(m_gameHwnd, &gamePid);
    GetWindowThreadProcessId(m_hwnd, &ovlPid);
    const bool foregroundIsUs = (fg == m_hwnd) || (fgPid == ovlPid);

    if (becameActive) {
      postShowMsg();
    } else if (!foregroundIsUs) {
      postHideMsg();
    }
    return;
  }

  // System-size commands (maximize/restore/move/size via menu/keyboard)
  case WM_SYSCOMMAND:
  {
    const UINT cmd = (UINT) (wParam & 0xFFF0);
    if (cmd == SC_MAXIMIZE || cmd == SC_RESTORE || cmd == SC_SIZE || cmd == SC_MOVE) {
      postShowMsg();   // re-read subRect and resnap
    }
    return;
  }

  // Interactive begin/end sizing/move
  case WM_ENTERSIZEMOVE:
    postHideMsg();
    return;

  // Z-order and position updates
  case WM_WINDOWPOSCHANGED:
  // Canonical move/size
  case WM_MOVE:
  // Style/frame changes
  case WM_STYLECHANGED:
  case WM_NCCALCSIZE:
  case WM_EXITSIZEMOVE:
  // DPI/monitor/refresh/theme changes
  case WM_DPICHANGED:
  case WM_DISPLAYCHANGE:
  case WM_DWMCOMPOSITIONCHANGED:
  case WM_THEMECHANGED:
    postShowMsg();
    return;

  // Show/hide triggered by shell or parent
  case WM_SHOWWINDOW:
    if (wParam) {
      postShowMsg(); 
    } else {
      postHideMsg();
    }
    return;

  case WM_SIZE:
  {
    if (wParam == SIZE_MINIMIZED) {
      postHideMsg();
    } else {
      // SIZE_MAXIMIZED and SIZE_RESTORED 
      postShowMsg();
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
    while(!m_hwnd && m_running) {
      Sleep(0);
    }
    // update returns with a valid overlay window HWND
  }
}

bool GameOverlay::isOurForeground() const {
  if (!m_gameHwnd) {
    return false;
  }

  HWND fg = GetForegroundWindow();
  if (!fg) {
    return false;
  }

  DWORD fgPid = 0;
  DWORD gamePid = 0;
  GetWindowThreadProcessId(fg, &fgPid);
  GetWindowThreadProcessId(m_gameHwnd, &gamePid);

  // Treat our app as the game process being foreground.
  return fgPid == gamePid;
}

LRESULT GameOverlay::overlayWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  switch (msg) {
  case WM_PAINT:
  {
    if (!m_debugDraw) {
      break; // nothing to draw if transparent
    }
    PAINTSTRUCT ps {};
    HDC hdc = BeginPaint(m_hwnd, &ps);
    if (hdc) {
      RECT rc; GetClientRect(m_hwnd, &rc);

      HBRUSH fill = CreateSolidBrush(RGB(255, 0, 0));
      BLENDFUNCTION bf { AC_SRC_OVER, 0, 20, 0 };

      // Border
      HPEN pen = CreatePen(PS_SOLID, 2, RGB(255, 0, 0));
      HGDIOBJ oldPen = SelectObject(hdc, pen);
      HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(HOLLOW_BRUSH));
      Rectangle(hdc, rc.left, rc.top, rc.right, rc.bottom);
      SelectObject(hdc, oldBrush);
      SelectObject(hdc, oldPen);
      DeleteObject(pen);
      DeleteObject(fill);
    }
    EndPaint(m_hwnd, &ps);
    return 0;
  }

  case WM_REMIX_SHOW_OVERLAY: show(); return 0;
  case WM_REMIX_HIDE_OVERLAY: hide(); return 0;
  case WM_DESTROY: PostQuitMessage(0); return 0;

  // Important, we are taking over the implementation of LEAVE to handle mouse interactions with the overlay
  case WM_MOUSELEAVE: return 0;

  case WM_NCHITTEST:
  {
    // Default hit-test
    LRESULT hit = DefWindowProcW(hWnd, msg, wParam, lParam);

    // If our game is NOT the foreground process, pretend we don't exist.
    // This lets the actual foreground window under us own the cursor.
    if (!isOurForeground()) {
      return HTTRANSPARENT;
    }

    return hit;
  }

  case WM_INPUT:
  {
    if (!isOurForeground()) {
      if (m_mouseInsideOverlay) { 
        m_mouseInsideOverlay = false;
        ImGui_ImplWin32_WndProcHandler(m_hwnd, WM_MOUSELEAVE, 0, 0);
      }
      return 0;
    }

    // Stable scale 
    float sx = 1.0f, sy = 1.0f;
    if (m_w > 0 && m_h > 0) {
      const ImVec2 disp = ImGui::GetIO().DisplaySize;
      if (disp.x > 0.0f && disp.y > 0.0f) {
        sx = disp.x / (float) m_w;
        sy = disp.y / (float) m_h;
      }
    }

    UINT size = 0;
    if (GetRawInputData((HRAWINPUT) lParam, RID_INPUT, nullptr, &size, sizeof(RAWINPUTHEADER)) != 0 || size == 0) {
      return 0;
    }

    BYTE stack_buf[256];
    std::unique_ptr<BYTE[]> heap_buf;
    BYTE* buf = size <= sizeof(stack_buf) ? stack_buf : (heap_buf.reset(new BYTE[size]), heap_buf.get());
    if (GetRawInputData((HRAWINPUT) lParam, RID_INPUT, buf, &size, sizeof(RAWINPUTHEADER)) != size) {
      return 0;
    }

    // Capture mouse events so we can modify for scaling.
    RAWINPUT* ri = reinterpret_cast<RAWINPUT*>(buf);
    if (ri->header.dwType == RIM_TYPEMOUSE) {
      const RAWMOUSE& m = ri->data.mouse;

      POINT p; 
      GetCursorPos(&p);
      ScreenToClient(m_hwnd, &p);

      RECT cr {}; 
      GetClientRect(m_hwnd, &cr);
      const int cw = cr.right - cr.left, ch = cr.bottom - cr.top;
      const bool outside = (p.x < 0 || p.y < 0 || p.x >= cw || p.y >= ch);
      if (!outside) {
        // Transition: outside -> inside
        if (!m_mouseInsideOverlay) {
          m_mouseInsideOverlay = true;
        }

        int x = std::clamp((int) std::lround(p.x * sx), -32768, 32768);
        int y = std::clamp((int) std::lround(p.y * sy), -32768, 32768);
       
        // Button mask for wParam
        WPARAM wp = 0;
        if (GetKeyState(VK_LBUTTON) & 0x8000) wp |= MK_LBUTTON;
        if (GetKeyState(VK_RBUTTON) & 0x8000) wp |= MK_RBUTTON;
        if (GetKeyState(VK_MBUTTON) & 0x8000) wp |= MK_MBUTTON;
        if (GetKeyState(VK_XBUTTON1) & 0x8000) wp |= MK_XBUTTON1;
        if (GetKeyState(VK_XBUTTON2) & 0x8000) wp |= MK_XBUTTON2;
        if (GetKeyState(VK_CONTROL) & 0x8000) wp |= MK_CONTROL;
        if (GetKeyState(VK_SHIFT) & 0x8000) wp |= MK_SHIFT;

        LPARAM lp = MAKELPARAM((WORD) (SHORT) x, (WORD) (SHORT) y);
        ImGui_ImplWin32_WndProcHandler(m_hwnd, WM_MOUSEMOVE, wp, lp);

        if (m.usButtonFlags) {
          auto send_btn = [&](UINT msg, WPARAM w) {
            LPARAM lp = MAKELPARAM((WORD) (SHORT) x, (WORD) (SHORT) y);
            ImGui_ImplWin32_WndProcHandler(m_hwnd, msg, w, lp);
          };
          if (m.usButtonFlags & RI_MOUSE_LEFT_BUTTON_DOWN)  send_btn(WM_LBUTTONDOWN, wp | MK_LBUTTON);
          if (m.usButtonFlags & RI_MOUSE_LEFT_BUTTON_UP)    send_btn(WM_LBUTTONUP, wp & ~MK_LBUTTON);
          if (m.usButtonFlags & RI_MOUSE_RIGHT_BUTTON_DOWN) send_btn(WM_RBUTTONDOWN, wp | MK_RBUTTON);
          if (m.usButtonFlags & RI_MOUSE_RIGHT_BUTTON_UP)   send_btn(WM_RBUTTONUP, wp & ~MK_RBUTTON);
          if (m.usButtonFlags & RI_MOUSE_MIDDLE_BUTTON_DOWN)send_btn(WM_MBUTTONDOWN, wp | MK_MBUTTON);
          if (m.usButtonFlags & RI_MOUSE_MIDDLE_BUTTON_UP)  send_btn(WM_MBUTTONUP, wp & ~MK_MBUTTON);
          if (m.usButtonFlags & RI_MOUSE_BUTTON_4_DOWN)     send_btn(WM_XBUTTONDOWN, wp | MK_XBUTTON1);
          if (m.usButtonFlags & RI_MOUSE_BUTTON_4_UP)       send_btn(WM_XBUTTONUP, wp & ~MK_XBUTTON1);
          if (m.usButtonFlags & RI_MOUSE_BUTTON_5_DOWN)     send_btn(WM_XBUTTONDOWN, wp | MK_XBUTTON2);
          if (m.usButtonFlags & RI_MOUSE_BUTTON_5_UP)       send_btn(WM_XBUTTONUP, wp & ~MK_XBUTTON2);
        }

        if (m.usButtonFlags & RI_MOUSE_WHEEL) {
          SHORT d = (SHORT) m.usButtonData;
          WPARAM w = MAKEWPARAM(wp & 0xFFFF, (UINT16) d);
          LPARAM l = MAKELPARAM((WORD) (SHORT) x, (WORD) (SHORT) y);
          ImGui_ImplWin32_WndProcHandler(m_hwnd, WM_MOUSEWHEEL, w, l);
        }
        if (m.usButtonFlags & RI_MOUSE_HWHEEL) {
          SHORT d = (SHORT) m.usButtonData;
          WPARAM w = MAKEWPARAM(wp & 0xFFFF, (UINT16) d);
          LPARAM l = MAKELPARAM((WORD) (SHORT) x, (WORD) (SHORT) y);
          ImGui_ImplWin32_WndProcHandler(m_hwnd, WM_MOUSEHWHEEL, w, l);
        }
      } else {
        if (m_mouseInsideOverlay) {
          m_mouseInsideOverlay = false;
          ImGui_ImplWin32_WndProcHandler(m_hwnd, WM_MOUSELEAVE, 0, 0);
        }
      }

      return 0;
    }

    // Still handle keyboard using ImGui
    break;
  }
  }

  // Let ImGui Win32 backend handle everything else (keyboard, etc.)
  if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
    return 0;

  return DefWindowProcW(m_hwnd, msg, wParam, lParam);
}

void GameOverlay::windowThreadMain() {
  DpiCtxGuard dpiGuard; 

  HINSTANCE hInst;
  GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCSTR>(&sWndProc), &hInst);

  WNDCLASS wc {};
  wc.lpfnWndProc = &sWndProc;
  wc.hInstance = hInst;
  wc.lpszClassName = m_className;
  wc.hbrBackground = nullptr;
  RegisterClass(&wc);

  // initial rect (prefer subrect, else fallback)
  RECT screenRc = vec4ToRectMinMax(Vector4(0.f));
  if (isEmptyRect(screenRc)) {
    screenRc = clientRectScreen(m_gameHwnd);
  }
  screenRc = vec4ToRectMinMax(Vector4 { (float) screenRc.left,(float) screenRc.top,(float) screenRc.right,(float) screenRc.bottom });

  m_w = std::max<LONG>(1, rectWidth(screenRc));
  m_h = std::max<LONG>(1, rectHeight(screenRc));
  m_lastRect = screenRc;

  const DWORD ex = WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_LAYERED | WS_EX_TRANSPARENT;
  const DWORD st = WS_POPUP;

  m_hwnd = CreateWindowEx(ex, m_className, "RTX Remix Overlay", st, screenRc.left, screenRc.top, m_w, m_h, nullptr, nullptr, hInst, this);
  if (!m_hwnd) {
    Logger::err(str::format("Failed to create overlay window: ", m_className));
    m_running.store(false);
    return;
  }

  // transparent by default
  SetLayeredWindowAttributes(m_hwnd, 0, 0, LWA_ALPHA);

  // Uncomment to render window as a dark box.
  //setDebugDraw(true);

  show();

  RAWINPUTDEVICE rid[2] {};
  // Mouse
  rid[0].usUsagePage = 0x01;
  rid[0].usUsage = 0x02;
  rid[0].dwFlags = RIDEV_INPUTSINK;
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
      show();
      TranslateMessage(&msg);
      DispatchMessage(&msg);
    }
  }

  if (m_hwnd) {
    DestroyWindow(m_hwnd); 
    m_hwnd = nullptr;
  }

  UnregisterClass(m_className, hInst);
}
} // namespace dxvk