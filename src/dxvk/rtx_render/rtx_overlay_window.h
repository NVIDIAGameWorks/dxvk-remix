/*
* Copyright (c) 2021-2023, NVIDIA CORPORATION. All rights reserved.
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

#include "../../util/rc/util_rc.h"
#include "../../util/rc/util_rc_ptr.h"
#include "rtx_common_object.h"

namespace dxvk {
  class GameOverlay : public RcObject {
  public:
    GameOverlay() = delete;

    GameOverlay(const char* className, class ImGUI* pImgui);
    ~GameOverlay();

    HWND hwnd() const {  return m_hwnd; }

    void update(HWND gameHwnd);

    void gameWndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT overlayWndProc(HWND, UINT, WPARAM, LPARAM);

    void setDebugDraw(bool enable, BYTE alpha = 96) {
      m_debugDraw = enable;
      m_debugAlpha = alpha;
      if (m_hwnd) {
        // Make it visible if debugging; invisible if not.
        SetLayeredWindowAttributes(m_hwnd, 0, m_debugAlpha, ULW_ALPHA);
        InvalidateRect(m_hwnd, nullptr, TRUE);
      }
    }

  private:
    void windowThreadMain();

    void show();
    void hide();

    bool isOurForeground() const;

    HWND m_gameHwnd = nullptr;

    std::atomic<HWND> m_hwnd { 0 };
    std::atomic<bool> m_running { true };
    std::thread m_thread;
    const char* m_className;

    ImGUI* m_pImgui = nullptr;
    UINT m_w = 1, m_h = 1;

    bool  m_mouseInsideOverlay = false;

    bool  m_debugDraw = false;
    BYTE  m_debugAlpha = 96;
    RECT  m_lastRect { 0,0,0,0 };
  };
}