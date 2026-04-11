#include "d3d9_cursor.h"
#include "d3d9_util.h"

#include <utility>

namespace dxvk {

  void D3D9Cursor::UpdateCursor(int X, int Y) {
    POINT currentPos = { };
    if (::GetCursorPos(&currentPos) && currentPos == POINT{ X, Y })
        return;

    ::SetCursorPos(X, Y);
  }


  BOOL D3D9Cursor::ShowCursor(BOOL bShow) {
    if (bShow) {
      // Only change the cursor if a hardware cursor was configured via
      // SetCursorProperties.  When m_hCursor is NULL (game never set one),
      // leave the OS cursor alone so it remains visible — the game is
      // expected to draw its own software cursor, which may not render
      // under path-traced backends like RTX Remix.
      if (m_hCursor != nullptr)
        ::SetCursor(m_hCursor);
    } else {
      ::SetCursor(nullptr);
    }
    return std::exchange(m_visible, bShow);
  }


  HRESULT D3D9Cursor::SetHardwareCursor(UINT XHotSpot, UINT YHotSpot, const CursorBitmap& bitmap) {
    DWORD mask[32];
    std::memset(mask, ~0, sizeof(mask));

    ICONINFO info;
    info.fIcon    = FALSE;
    info.xHotspot = XHotSpot;
    info.yHotspot = YHotSpot;
    info.hbmMask  = ::CreateBitmap(HardwareCursorWidth, HardwareCursorHeight, 1, 1,  mask);
    info.hbmColor = ::CreateBitmap(HardwareCursorWidth, HardwareCursorHeight, 1, 32, &bitmap[0]);

    if (m_hCursor != nullptr)
      ::DestroyCursor(m_hCursor);

    m_hCursor = ::CreateIconIndirect(&info);

    ::DeleteObject(info.hbmMask);
    ::DeleteObject(info.hbmColor);

    ShowCursor(m_visible);

    return D3D_OK;
  }

}