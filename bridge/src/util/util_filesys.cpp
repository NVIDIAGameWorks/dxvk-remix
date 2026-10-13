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
}

bool RtxFileSys::s_bInit = false;
RtxFileSys::fspath RtxFileSys::s_rootPath;
RtxFileSys::fspath RtxFileSys::s_remixPath;
RtxFileSys::fspath RtxFileSys::s_runtimePath;
RtxFileSys::PathArray RtxFileSys::s_paths;

void RtxFileSys::init(const std::string rootPath, const std::string remixPath) {
  if (s_bInit) {
    Logger::err("[RtxFileSys] Already init.");
    return;
  }

  Logger::info(std::string("Native arch: ") + getNativeArchString());

  std::error_code ec;
  if(!std::filesystem::exists(rootPath, ec)) {
    Logger::err(format("[RtxFileSys] Cannot resolve RTX filesystem, base path does not exist: ", rootPath));
    return;
  }
  if(!std::filesystem::exists(remixPath, ec)) {
    Logger::err(format("[RtxFileSys] Cannot resolve Remix folder, path does not exist: ", remixPath));
    return;
  }
  s_rootPath = std::filesystem::weakly_canonical(std::filesystem::absolute(fspath(rootPath)));
  s_remixPath = std::filesystem::weakly_canonical(std::filesystem::absolute(fspath(remixPath)));
  s_runtimePath = s_remixPath / runtimeDirName(getNativeArchString());
  
  if (!std::filesystem::exists(s_runtimePath, ec)) {
    // Platform preferred runtime does not exist - fallback to x64 runtime
    Logger::warn(format("[RtxFileSys] Remix runtime for ", getNativeArchString(), " platform does not exist. Falling back to x64."));
    s_runtimePath = s_remixPath / runtimeDirName(bridge_util::RTX_ARCH_X64);
  }

  for(const auto& pathSpec : s_pathSpecs) {
    std::string pathStr = (pathSpec.env.empty()) ? "" : getEnvVar(pathSpec.env.c_str());
    // Reject paths containing " so they cannot be used for command injection when passed to CreateProcess.
    if (!pathStr.empty() && pathStr.find('"') != std::string::npos) {
      pathStr = "";
    }
    fspath& path = s_paths[pathSpec.id];
    if (path.compare("none") == 0) {
      path = "";
      continue;
    } else if (pathStr.empty()) {
      path = s_rootPath / pathSpec.defaultRelPath;
    } else {
      path = pathStr;
      path /= ""; // Add ending slash
    }
    mkDirs(path);
  }
  s_bInit = true;
}

void RtxFileSys::print() {
  Logger::debug(format("[RtxFileSys] Mods dir:    ", s_paths[Mods]));
  Logger::debug(format("[RtxFileSys] Capture dir: ", s_paths[Captures]));
  Logger::debug(format("[RtxFileSys] Logs dir:    ", s_paths[Logs]));
}

void RtxFileSys::mkDirs(const fspath& path) {
  fspath ctorPath("");
  for (const auto& part : path) {
    ctorPath /= part;
    ctorPath = absolute(ctorPath);
    if(!std::filesystem::is_directory(ctorPath)) {
      Logger::debug(format("Creating dir: ", ctorPath));
      CreateDirectoryA(ctorPath.string().c_str(), nullptr);
    }
  }
}

}
