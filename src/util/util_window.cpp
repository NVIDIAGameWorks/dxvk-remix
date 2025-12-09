/*
* Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.
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
#include "util_env.h"
// NV-DXVK start: 
#include "util_string.h"
// NV-DXVK end
#include "util_window.h"
#include "log/log.h"

#include <windows.h>
#include <fstream>
#include <iostream>
#include <string>
#include <shellapi.h>
#include <commctrl.h>
#include <commoncontrols.h>


namespace dxvk::window {
  // Create a .bmp file, from a single icon!
  bool SaveIcon(const std::string& filename, HICON hIcon) {
    if (hIcon == 0)
      return false;

    std::ofstream iconFile = std::ofstream(str::tows(filename.c_str()).c_str(), std::ios_base::binary);

    if (!iconFile.is_open()) {
      Logger::err(str::format("Create file failed while writing icon"));
      return false;
    }

    ICONINFO iconInfo;
    if (!GetIconInfo(hIcon, &iconInfo)) {
      Logger::err(str::format("Failed to get icon info"));
      return false;
    }

    BITMAP bmpColor;
    if (!GetObject(iconInfo.hbmColor, sizeof(BITMAP), &bmpColor)) {
      Logger::err(str::format("Failed to get bitmap info"));
      return false;
    }

    HDC hDC = CreateCompatibleDC(NULL);

    BITMAPINFO biInfo;
    memset(&biInfo, 0, sizeof(biInfo));
    biInfo.bmiHeader.biSize = sizeof(BITMAPINFO);

    // First call fills out some fields
    GetDIBits(hDC, iconInfo.hbmColor, 0, 0, nullptr, &biInfo, DIB_RGB_COLORS);

    // Request specific settings to make our lives easier
    biInfo.bmiHeader.biCompression = BI_RGB;
    biInfo.bmiHeader.biBitCount = 32;

    uint8_t* pIconData = new uint8_t[biInfo.bmiHeader.biSizeImage];
    if (pIconData == nullptr) {
      Logger::err(str::format("Failed to allocate bitmap data buffer"));
      return false;
    }

    GetDIBits(hDC, iconInfo.hbmColor, 0, biInfo.bmiHeader.biHeight, pIconData, &biInfo, DIB_RGB_COLORS);

    BITMAPFILEHEADER hdr;
    hdr.bfType = 0x4d42; // 0x42 = "B" 0x4d = "M"  
    hdr.bfSize = (DWORD)(sizeof(BITMAPFILEHEADER) + biInfo.bmiHeader.biSize + biInfo.bmiHeader.biClrUsed * sizeof(RGBQUAD) + biInfo.bmiHeader.biSizeImage);
    hdr.bfReserved1 = 0;
    hdr.bfReserved2 = 0;

    // Compute the offset to the array of color indices.  
    hdr.bfOffBits = (DWORD)sizeof(BITMAPFILEHEADER) + biInfo.bmiHeader.biSize + biInfo.bmiHeader.biClrUsed * sizeof(RGBQUAD);

    // Copy the BITMAPFILEHEADER into the .BMP file.  
    iconFile.write((char*)&hdr, sizeof(BITMAPFILEHEADER));

    // Write the modified header
    iconFile.write((char*)&biInfo.bmiHeader, sizeof(BITMAPINFOHEADER));

    // Write image bits
    iconFile.write((char*)pIconData, biInfo.bmiHeader.biSizeImage);

    delete[] pIconData;

    DeleteObject(iconInfo.hbmColor);
    DeleteDC(hDC);

    return true;
  }

  HICON getIcon(HWND hwnd) {
    // Get the window icon - robust, will use all tricks available
    HICON icon = 0;

    // Get from the window class
    icon = reinterpret_cast<HICON>(GetClassLongPtr(hwnd, GCLP_HICON));
    if (icon != 0)
      return icon;    
    
    // Try smaller
    icon = reinterpret_cast<HICON>(GetClassLongPtr(hwnd, GCLP_HICONSM));
    if (icon != 0)
      return icon;

    // Get title-bar icon from winproc
    icon = reinterpret_cast<HICON>(SendMessage(hwnd, WM_GETICON, ICON_BIG, 0));
    if (icon != 0)
      return icon;    
    
    // Smaller
    icon = reinterpret_cast<HICON>(SendMessage(hwnd, WM_GETICON, ICON_SMALL, 0));
    if (icon != 0)
      return icon;

    // Below here - we fall back to "whatever icon we can get"
  
    // The windows shell method - this will likely only get us the exe icon, which isnt always accurate
    {
      // Get the icon index using SHGetFileInfo 
      SHFILEINFO* lpSfi = new SHFILEINFO;
      ZeroMemory(lpSfi, sizeof(SHFILEINFO));
      SHGetFileInfo(env::getExePath().c_str(), 0, lpSfi, sizeof(SHFILEINFO), SHGFI_SYSICONINDEX);

      // Retrieve the system image list.  In this order!
      const int iconIDs[] = { SHIL_EXTRALARGE, SHIL_LARGE, SHIL_SMALL, SHIL_SYSSMALL };
      for (int i = 0; i < sizeof(iconIDs) / sizeof(iconIDs[0]); i++) {
        HIMAGELIST* imageList;
        HRESULT hResult = SHGetImageList(iconIDs[i], IID_IImageList, (void**)&imageList);
        if (hResult == S_OK) {
          ((IImageList*)imageList)->GetIcon(lpSfi->iIcon, ILD_TRANSPARENT, &icon);

          if (icon != 0)
            return icon;
        }
      }
    }

    // Alternative method: get the first icon from the main module (executable image of the process)
    icon = LoadIcon(GetModuleHandle(0), MAKEINTRESOURCE(0));
    if (icon != 0)
      return icon;

    // Alternative method. Use OS default icon
    icon = LoadIcon(0, IDI_APPLICATION);
    if (icon != 0)
      return icon;

    return 0;
  }

  void saveWindowIconToFile(const std::string& filename, HWND hwnd /*= (HWND) 0*/) {
    if (hwnd == (HWND) 0 || !IsWindow(hwnd)) {
      // Assume the window in focus is the one we care about...
      hwnd = GetForegroundWindow();
    }

    const HICON hIcon = getIcon(hwnd);

    if (hIcon == 0) {
      Logger::warn("Failed to find icon");
      return;
    }

    if (!SaveIcon(filename, hIcon)) {
      Logger::err("Failed to generate icon file on request");
    }
  }

  const std::string getWindowTitle(HWND hwnd/* = (HWND)0*/) {
    if (hwnd == (HWND)0 || !IsWindow(hwnd)) {
      // Assume the window in focus is the one we care about...
      hwnd = GetForegroundWindow();
    }

    const bool isUnicode = IsWindowUnicode(hwnd);
    if (isUnicode) {
      wchar_t title[256];
      GetWindowTextW(hwnd, title, sizeof(title));
      return str::fromws(title);
    } else {
      char title[256];
      GetWindowTextA(hwnd, title, sizeof(title));
      return std::string(title);
    }
  }
}
