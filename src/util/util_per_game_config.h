/*
* Copyright (c) 2026, NVIDIA CORPORATION. All rights reserved.
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

#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <string>
#include <windows.h>

#define XXH_INLINE_ALL
#include "xxHash/xxhash.h"

namespace dxvk {

  // Builds the per-game settings filename (<hash>.conf) from the game root path
  // (RtxFileSys::rootPath()). Stored under %LOCALAPPDATA%/NVIDIA/RTXRemix/user_settings/.
  inline std::string perGameConfigFileName(const std::filesystem::path& gameRootPath) {
    const auto canonicalRoot = std::filesystem::weakly_canonical(
      std::filesystem::absolute(gameRootPath));
    const std::wstring wide = canonicalRoot.wstring();
    if (wide.empty()) {
      return {};
    }
    const int len = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (len <= 0) {
      return {};
    }
    std::string rootUtf8(static_cast<size_t>(len), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, rootUtf8.data(), len, nullptr, nullptr);
    if (!rootUtf8.empty() && rootUtf8.back() == '\0') {
      rootUtf8.pop_back();
    }
    const XXH64_hash_t dirHash = XXH3_64bits(rootUtf8.data(), rootUtf8.size());
    std::stringstream ss;
    ss << std::hex << std::setfill('0') << std::setw(16) << dirHash << ".conf";
    return ss.str();
  }

} // namespace dxvk
