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
#pragma once

#include "log/log.h"

#include <string>
#include <filesystem>
#include <cassert>
#include <array>
#include <optional>
#include <fstream>

namespace version {
  static constexpr uint64_t fileSysV = 1;
}

namespace dxvk::util {

bool createDirectories(const std::filesystem::path& path);
std::optional<std::ofstream> createDirectoriesAndOpenFile(const std::filesystem::path& filePath);

class RtxFileSys {
public:
  enum Id {
    Mods,
    Captures,
    Logs,
    kNumIds
  };
private:
  using fspath = std::filesystem::path;
  template<typename... PathParts>
  static inline fspath join(PathParts... pathParts) {
    return std::filesystem::absolute((fspath(pathParts) / ...));
  }
  struct PathSpec {
    Id id;
    fspath defaultRelPath;
    std::string env;
  };
  // EDIT RTX FILESYSTEM PATHS HERE
  static inline const std::array<PathSpec,kNumIds> s_pathSpecs = {
    PathSpec{ Mods,     join(".", "rtx-remix", "mods"),     ""                  },
    PathSpec{ Captures, join(".", "rtx-remix", "captures"), "DXVK_CAPTURE_PATH" },
    PathSpec{ Logs,     join(".", "rtx-remix", "logs"),     "DXVK_LOG_PATH"     }
  };

  static bool s_bInit;
  using PathArray = std::array<fspath, kNumIds>;
  static PathArray s_paths;

public:
  static void init(const std::string rootPath);
  static inline const fspath path(const Id id) {
    assert(s_bInit && "[RtxFileSys] Not yet init.");
    return s_paths[id];
  }
  static void print();
};

}
