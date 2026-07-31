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
#include "util_local_data.h"

#include <filesystem>

#include "log/log.h"
#include "util_env.h"
#include "util_filesys.h"
#include "util_per_game_config.h"
#include "util_string.h"

namespace dxvk {

  static std::string getBaseDir() {
    const std::string localAppData = env::getEnvVar("LOCALAPPDATA");
    if (localAppData.empty()) {
      Logger::warn("[LocalData] LOCALAPPDATA environment variable not set.");
      return {};
    }
    return (std::filesystem::path(localAppData) / "NVIDIA" / "RTXRemix" / "user_settings").string();
  }

  static std::string getAppFilePath() {
    const std::string baseDir = getBaseDir();
    if (baseDir.empty()) {
      return {};
    }

    const std::filesystem::path gameRoot = util::RtxFileSys::isInitialized()
      ? util::RtxFileSys::rootPath()
      : std::filesystem::path(env::getExePath()).parent_path();

    const std::string configFileName = perGameConfigFileName(gameRoot);
    if (configFileName.empty()) {
      return {};
    }

    return (std::filesystem::path(baseDir) / configFileName).string();
  }

  static std::string getSharedFilePath() {
    const std::string baseDir = getBaseDir();
    if (baseDir.empty()) {
      return {};
    }
    return (std::filesystem::path(baseDir) / "shared.conf").string();
  }

  // --- Scope ---

  void LocalData::Scope::ensureLoaded() {
    if (!m_loaded) {
      load();
    }
  }

  void LocalData::Scope::load() {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_loaded) {
      return;
    }
    m_loaded = true;

    const auto filePath = m_pathFn();
    if (filePath.empty()) {
      return;
    }

    m_config = Config::getOptionLayerConfig(filePath);
  }

  void LocalData::Scope::save() {
    std::lock_guard<std::mutex> lock(m_mutex);

    const auto filePath = m_pathFn();
    if (filePath.empty()) {
      return;
    }

    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(filePath).parent_path(), ec);
    if (ec) {
      Logger::warn(str::format("[LocalData] Failed to create settings directory: ", ec.message()));
      return;
    }

    Config::serializeCustomConfig(m_config, filePath);
  }

  // --- Static accessors ---

  LocalData::Scope& LocalData::app() {
    static Scope s_app(getAppFilePath);
    return s_app;
  }

  LocalData::Scope& LocalData::shared() {
    static Scope s_shared(getSharedFilePath);
    return s_shared;
  }

}
