/*
* Copyright (c) 2023, NVIDIA CORPORATION. All rights reserved.
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
#include "util_string.h"
#include <iomanip>

namespace dxvk::str {
  std::string fromws(const WCHAR *ws) {
    size_t len = ::WideCharToMultiByte(CP_UTF8,
      0, ws, -1, nullptr, 0, nullptr, nullptr);

    if (len <= 1)
      return "";

    len -= 1;

    std::string result;
    result.resize(len);
    ::WideCharToMultiByte(CP_UTF8, 0, ws, -1,
      &result.at(0), len, nullptr, nullptr);
    return result;
  }


  void tows(const char* mbs, WCHAR* wcs, size_t wcsLen) {
    ::MultiByteToWideChar(
      CP_UTF8, 0, mbs, -1,
      wcs, wcsLen);
  }

  std::wstring tows(const char* mbs) {
    size_t len = ::MultiByteToWideChar(CP_UTF8,
      0, mbs, -1, nullptr, 0);
    
    if (len <= 1)
      return L"";

    len -= 1;

    std::wstring result;
    result.resize(len);
    ::MultiByteToWideChar(CP_UTF8, 0, mbs, -1,
      &result.at(0), len);
    return result;
  }

  std::vector<std::string> split(std::string value, const char delimiter) {
    std::vector<std::string> result;
    std::stringstream ss(value);
    std::string s;
    while (std::getline(ss, s, delimiter)) {
      result.push_back(s);
    }
    return result;
  }

  bool isInvalidAscii(const unsigned char ch) {
    // Single-byte character (ASCII)
    return !(ch <= 0x7F);
  }

  std::string stripNonAscii(const std::string& input) {
    std::string result = input;
    result.erase(std::remove_if(result.begin(), result.end(), isInvalidAscii), result.end());
    return result;
  }

  std::string formatBytes(std::size_t bytes) {
    // Note: 2^64 is 16 EiB, so no need to go beyond this for binary metric prefixes.
    constexpr std::size_t KiB = 1024;
    constexpr std::size_t MiB = KiB * 1024;
    constexpr std::size_t GiB = MiB * 1024;
    constexpr std::size_t TiB = GiB * 1024;
    constexpr std::size_t PiB = TiB * 1024;
    constexpr std::size_t EiB = PiB * 1024;

    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2);

    if (bytes < KiB) {
      oss << bytes << " B";
    } else if (bytes < MiB) {
      oss << (static_cast<double>(bytes) / static_cast<double>(KiB)) << " KiB";
    } else if (bytes < GiB) {
      oss << (static_cast<double>(bytes) / static_cast<double>(MiB)) << " MiB";
    } else if (bytes < TiB) {
      oss << (static_cast<double>(bytes) / static_cast<double>(GiB)) << " GiB";
    } else if (bytes < PiB) {
      oss << (static_cast<double>(bytes) / static_cast<double>(TiB)) << " TiB";
    } else if (bytes < EiB) {
      oss << (static_cast<double>(bytes) / static_cast<double>(PiB)) << " PiB";
    } else {
      oss << (static_cast<double>(bytes) / static_cast<double>(EiB)) << " EiB";
    }

    return oss.str();
  }
}
