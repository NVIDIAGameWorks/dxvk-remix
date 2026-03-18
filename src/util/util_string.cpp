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

  std::string sanitizeUtf8(const std::string& input) {
    // Validate UTF-8 by round-tripping through wide string conversion.
    // This preserves valid non-ASCII UTF-8 characters (e.g., Cyrillic, CJK)
    // while stripping truly invalid sequences.
    if (input.empty()) {
      return input;
    }
    int wideLen = ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
      input.c_str(), -1, nullptr, 0);
    if (wideLen > 0) {
      // Input is valid UTF-8, return as-is
      return input;
    }
    // Input contains invalid UTF-8 sequences; strip only the invalid bytes
    std::string result;
    result.reserve(input.size());
    for (size_t i = 0; i < input.size(); ) {
      unsigned char ch = static_cast<unsigned char>(input[i]);
      if (ch <= 0x7F) {
        result.push_back(ch);
        i++;
      } else if ((ch & 0xE0) == 0xC0 && i + 1 < input.size() &&
                 (static_cast<unsigned char>(input[i+1]) & 0xC0) == 0x80) {
        result.push_back(input[i]);
        result.push_back(input[i+1]);
        i += 2;
      } else if ((ch & 0xF0) == 0xE0 && i + 2 < input.size() &&
                 (static_cast<unsigned char>(input[i+1]) & 0xC0) == 0x80 &&
                 (static_cast<unsigned char>(input[i+2]) & 0xC0) == 0x80) {
        result.push_back(input[i]);
        result.push_back(input[i+1]);
        result.push_back(input[i+2]);
        i += 3;
      } else if ((ch & 0xF8) == 0xF0 && i + 3 < input.size() &&
                 (static_cast<unsigned char>(input[i+1]) & 0xC0) == 0x80 &&
                 (static_cast<unsigned char>(input[i+2]) & 0xC0) == 0x80 &&
                 (static_cast<unsigned char>(input[i+3]) & 0xC0) == 0x80) {
        result.push_back(input[i]);
        result.push_back(input[i+1]);
        result.push_back(input[i+2]);
        result.push_back(input[i+3]);
        i += 4;
      } else {
        // Skip invalid byte
        i++;
      }
    }
    // If all bytes were invalid and stripped, return a descriptive placeholder
    // to avoid producing an empty string that could cause invalid USD syntax
    if (result.empty()) {
      result = "<Invalid UTF-8 encoding>";
    }
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
