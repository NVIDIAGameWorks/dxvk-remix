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
#include "util_filesys.h"

#include "log/log.h"

#include <windows.h>
#include <sstream>

#if defined(REMIX_BRIDGE_CLIENT) || defined(REMIX_BRIDGE_SERVER)
using namespace bridge_util;
#else
using namespace dxvk;
#endif

namespace dxvk::util {

namespace {
// Redefining anonymous version of dxvk util funcs to share with bridge w/o copying entire util_ files
std::string getEnvVar(const char* name) {
  std::vector<char> result;
  result.resize(MAX_PATH + 1);

  DWORD len = ::GetEnvironmentVariableA(name, result.data(), MAX_PATH);
  result.resize(len);

  return result.data();
}
inline void format1(std::stringstream&) { }
template<typename T, typename... Tx>
void format1(std::stringstream& str, const T& arg, const Tx&... args) {
  str << arg;
  format1(str, args...);
}
template<typename... Args>
std::string format(const Args&... args) {
  std::stringstream stream;
  format1(stream, args...);
  return stream.str();
}
} // namespace

bool createDirectories(const std::filesystem::path& path) {
  if (path.empty()) {
    return false;
  }
  if (!std::filesystem::exists(path)) {
    Logger::debug(format("Creating directory: ", path));
    try {
      if (!std::filesystem::create_directories(path)) {
        Logger::err(format("Failed to create directory: ", path));
        return false;
      }
    } catch (const std::filesystem::filesystem_error& e) {
      Logger::err(format("error when creating directory: `", path, "`: ", e.what()));
      return false;
    }
  }
  return true;
}

std::optional<std::ofstream> createDirectoriesAndOpenFile(const std::filesystem::path& filePath) {
  if (filePath.empty()) {
    return std::nullopt;
  }
  
  if (!util::createDirectories(filePath.parent_path())) {
    return std::nullopt;
  }

  // Open the output file for writing
  std::ofstream outputFile(filePath);
  if (!outputFile.is_open()) {
    Logger::err(format("Failed to open file ", filePath, " for writing"));
    return std::nullopt;
  }
  return outputFile;
}

bool RtxFileSys::s_bInit = false;
RtxFileSys::PathArray RtxFileSys::s_paths;

void RtxFileSys::init(const std::string rootPath) {
  assert(!s_bInit && "[RtxFileSys] Already init.");
  if(s_bInit) {
    Logger::err("[RtxFileSys] Already init.");
  }
  if(!std::filesystem::exists(rootPath)) {
    Logger::err(format("[RtxFileSys] Cannot resolve RTX filesystem, base path does exist: ", rootPath));
  }
  for(const auto& pathSpec : s_pathSpecs) {
    const std::string pathStr = (pathSpec.env.empty()) ? "" : getEnvVar(pathSpec.env.c_str());
    fspath& path = s_paths[pathSpec.id];
    if (path.compare("none") == 0) {
      path = "";
      continue;
    } else if (pathStr.empty()) {
      path = rootPath / pathSpec.defaultRelPath;
    } else {
      path = pathStr;
      path /= ""; // Add ending slash
    }
    createDirectories(absolute(path));
  }
  s_bInit = true;
}

void RtxFileSys::print() {
  Logger::debug(format("[RtxFileSys] Mods dir:    ", s_paths[Mods]));
  Logger::debug(format("[RtxFileSys] Capture dir: ", s_paths[Captures]));
  Logger::debug(format("[RtxFileSys] Logs dir:    ", s_paths[Logs]));
}

}
