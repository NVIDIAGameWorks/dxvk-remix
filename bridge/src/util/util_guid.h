/*
 * Copyright (c) 2022-2023, NVIDIA CORPORATION. All rights reserved.
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
#ifndef UTIL_GUID_H_
#define UTIL_GUID_H_

#include "log/log.h"

#include <assert.h>
#include <iomanip>
#include <sstream>
#include <windows.h>
#include <objbase.h>

#define GUID_LENGTH 36

namespace bridge_util {

  class Guid {

  public:
    Guid() {
      const auto hresult = CoCreateGuid(&m_guid);
      if (!SUCCEEDED(hresult)) {
        Logger::err("GUID creation failed!");
        throw;
      }
    }

    ~Guid() = default;

    bool setGuid(LPWSTR* szGuid) {
      if (wcslen(*szGuid) != GUID_LENGTH) {
        return false;
      }

      auto result = swscanf_s(*szGuid,
        L"%8x-%4hx-%4hx-%2hhx%2hhx-%2hhx%2hhx%2hhx%2hhx%2hhx%2hhx",
        &m_guid.Data1, &m_guid.Data2, &m_guid.Data3,
        &m_guid.Data4[0], &m_guid.Data4[1], &m_guid.Data4[2], &m_guid.Data4[3],
        &m_guid.Data4[4], &m_guid.Data4[5], &m_guid.Data4[6], &m_guid.Data4[7]);

      return result != EOF;
    }

    std::string toString(const std::string& baseName) {
      char guid_cstr[39];
      _snprintf_s(guid_cstr, sizeof(guid_cstr),
        "%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x",
        m_guid.Data1, m_guid.Data2, m_guid.Data3,
        m_guid.Data4[0], m_guid.Data4[1], m_guid.Data4[2], m_guid.Data4[3],
        m_guid.Data4[4], m_guid.Data4[5], m_guid.Data4[6], m_guid.Data4[7]);

      std::string output = (baseName.length() > 0 ? baseName + "_" : "") + std::string(guid_cstr);
      return output;
    }

    std::string toString() {
      return toString("");
    }

  private:
    GUID m_guid;
  };
}

#endif // UTIL_GUID_H_
