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
 * at https://github.com/doitsujin/dxvk/blob/master/src/wsi/wsi_monitor.h
*/

#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace bridge_util {

  /**
   * \brief Retrieves primary monitor
   * \returns The primary monitor
   */
  HMONITOR GetDefaultMonitor();

  /**
   * \brief Sets monitor display mode
   *
   * Note that \c pMode may be altered by this function.
   * \param [in] hMonitor The monitor to change
   * \param [in] pMode The desired display mode
   * \returns \c true on success
   */
  BOOL SetMonitorDisplayMode(
          HMONITOR hMonitor,
          DEVMODEW* pMode);

  /**
   * \brief Enumerates monitor display modes
   *
   * \param [in] hMonitor The monitor to query
   * \param [in] modeNum Mode number or enum
   * \param [in] pMode The display mode
   * \returns \c true on success
   */
  BOOL GetMonitorDisplayMode(
          HMONITOR hMonitor,
          DWORD modeNum,
          DEVMODEW* pMode);

  /**
   * \brief Change display modes to registry settings
   * \returns \c true on success
   */
  BOOL RestoreMonitorDisplayMode();

  /**
   * \brief Queries window client size
   * 
   * \param [in] hWnd Window to query
   * \param [out] pWidth Client width
   * \param [out] pHeight Client height
   */
  void GetWindowClientSize(
          HWND hWnd,
          UINT* pWidth,
          UINT* pHeight);
  
  /**
   * \brief Queries monitor size
   * 
   * \param [in] hMonitor Monitor to query
   * \param [out] pWidth Client width
   * \param [out] pHeight Client height
   */
  void GetMonitorClientSize(
          HMONITOR hMonitor,
          UINT* pWidth,
          UINT* pHeight);

  /**
   * \brief Queries monitor rect
   * 
   * \param [in] hMonitor Monitor to query
   * \param [out] pRect The rect to return
   */
  void GetMonitorRect(
          HMONITOR hMonitor,
          RECT* pRect);

}
