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

/*
 * This is a modified and reduced version of the config.cpp file in the DXVK repo
 * at https://github.com/doitsujin/dxvk/blob/master/src/wsi/wsi_monitor.cpp
*/

#include "util_monitor.h"
#include "./log/log.h"

namespace bridge_util {

  HMONITOR GetDefaultMonitor() {
    return ::MonitorFromPoint({ 0, 0 }, MONITOR_DEFAULTTOPRIMARY);
  }

  BOOL SetMonitorDisplayMode(
          HMONITOR hMonitor,
          DEVMODEW* pMode) {
    ::MONITORINFOEXW monInfo;
    monInfo.cbSize = sizeof(monInfo);
    if (!::GetMonitorInfoW(hMonitor, reinterpret_cast<MONITORINFO*>(&monInfo))) {
      Logger::err("Failed to query monitor info");
      return E_FAIL;
    }
    Logger::info(format_string("Setting display mode: %dx%d@%d", pMode->dmPelsWidth, pMode->dmPelsHeight, pMode->dmDisplayFrequency));

    DEVMODEW curMode = { };
    curMode.dmSize = sizeof(curMode);

    if (GetMonitorDisplayMode(hMonitor, ENUM_CURRENT_SETTINGS, &curMode)) {
      bool eq = curMode.dmPelsWidth == pMode->dmPelsWidth
        && curMode.dmPelsHeight == pMode->dmPelsHeight
        && curMode.dmBitsPerPel == pMode->dmBitsPerPel;

      if (pMode->dmFields & DM_DISPLAYFREQUENCY)
        eq &= curMode.dmDisplayFrequency == pMode->dmDisplayFrequency;
      if (eq)
        return true;
    }

    LONG status = ::ChangeDisplaySettingsExW(monInfo.szDevice,
      pMode, nullptr, CDS_FULLSCREEN, nullptr);

    if (status != DISP_CHANGE_SUCCESSFUL) {
      pMode->dmFields &= ~DM_DISPLAYFREQUENCY;
      status = ::ChangeDisplaySettingsExW(monInfo.szDevice,
        pMode, nullptr, CDS_FULLSCREEN, nullptr);
    }
    return status == DISP_CHANGE_SUCCESSFUL;
  }

  BOOL GetMonitorDisplayMode(
          HMONITOR hMonitor,
          DWORD modeNum,
          DEVMODEW* pMode) {
    ::MONITORINFOEXW monInfo;
    monInfo.cbSize = sizeof(monInfo);
    if (!::GetMonitorInfoW(hMonitor, reinterpret_cast<MONITORINFO*>(&monInfo))) {
      Logger::err("Failed to query monitor info");
      return false;
    }
    return ::EnumDisplaySettingsW(monInfo.szDevice, modeNum, pMode);
  }

  BOOL CALLBACK RestoreMonitorDisplayModeCallback(
          HMONITOR hMonitor,
          HDC hDC,
          LPRECT pRect,
          LPARAM pUserdata) {
    auto success = reinterpret_cast<bool*>(pUserdata);
    DEVMODEW devMode = { };
    devMode.dmSize = sizeof(devMode);
    if (!GetMonitorDisplayMode(hMonitor, ENUM_REGISTRY_SETTINGS, &devMode)) {
      *success = false;
      return false;
    }
    Logger::info(format_string("Restoring display mode: %dx%d@%d", devMode.dmPelsWidth, devMode.dmPelsHeight, devMode.dmDisplayFrequency));
      if (!SetMonitorDisplayMode(hMonitor, &devMode)) {
        *success = false;
        return false;
      }
    return true;
  }

  BOOL RestoreMonitorDisplayMode() {
    bool success = true;
    bool result = ::EnumDisplayMonitors(nullptr, nullptr,
      &RestoreMonitorDisplayModeCallback,
      reinterpret_cast<LPARAM>(&success));
    return result && success;
  }

  void GetWindowClientSize(
          HWND  hWnd,
          UINT* pWidth,
          UINT* pHeight) {
    RECT rect = { };
    ::GetClientRect(hWnd, &rect);
    if (pWidth)
      *pWidth = rect.right - rect.left;
    if (pHeight)
      *pHeight = rect.bottom - rect.top;
  }

  void GetMonitorClientSize(
          HMONITOR hMonitor,
          UINT* pWidth,
          UINT* pHeight) {
    ::MONITORINFOEXW monInfo;
    monInfo.cbSize = sizeof(monInfo);
    if (!::GetMonitorInfoW(hMonitor, reinterpret_cast<MONITORINFO*>(&monInfo))) {
      Logger::err("Failed to query monitor info");
      return;
    }
    auto rect = monInfo.rcMonitor;
    if (pWidth)
      *pWidth = rect.right - rect.left;
    if (pHeight)
      *pHeight = rect.bottom - rect.top;
  }

  void GetMonitorRect(
          HMONITOR hMonitor,
          RECT* pRect) {
    ::MONITORINFOEXW monInfo;
    monInfo.cbSize = sizeof(monInfo);
    if (!::GetMonitorInfoW(hMonitor, reinterpret_cast<MONITORINFO*>(&monInfo))) {
      Logger::err("Failed to query monitor info");
      return;
    }
    *pRect = monInfo.rcMonitor;
  }
}