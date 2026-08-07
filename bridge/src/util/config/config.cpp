
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

 /*
  * This is a modified and reduced version of the config.cpp file in the DXVK repo
  * at https://github.com/doitsujin/dxvk/blob/master/src/util/config/config.cpp
  */

#include <bitset>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <regex>
#include <sstream>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "config.h"
#include "log/log.h"
#include "util_bytes.h"
#include "util_filesys.h"
#include "util_process.h"
#include "../../../../src/util/util_per_game_config.h"

// Guarantee internal linkage only
namespace {
  static bool isWhitespace(char ch) {
    return ch == ' ' || ch == '\x9' || ch == '\r';
  }


  static bool isValidKeyChar(char ch) {
    return (ch >= '0' && ch <= '9')
      || (ch >= 'A' && ch <= 'Z')
      || (ch >= 'a' && ch <= 'z')
      || (ch == '.' || ch == '_');
  }


  static size_t skipWhitespace(const std::string& line, size_t n) {
    while (n < line.size() && isWhitespace(line[n]))
      n += 1;
    return n;
  }


  static void parseUserConfigLine(bridge_util::Config& config, const std::string& line) {
    std::stringstream key;
    std::stringstream value;

    // Extract the key
    size_t n = skipWhitespace(line, 0);

    if (n < line.size() && line[n] == '[') {
      n += 1;

      size_t e = line.size() - 1;
      while (e > n && line[e] != ']')
        e -= 1;

      while (n < e)
        key << line[n++];
    } else {
      while (n < line.size() && isValidKeyChar(line[n]))
        key << line[n++];

      // Check whether the next char is a '='
      n = skipWhitespace(line, n);
      if (n >= line.size() || line[n] != '=') {
        return;
      }

      // Extract the value
      bool insideString = false;
      n = skipWhitespace(line, n + 1);

      while (n < line.size()) {
        if (!insideString && isWhitespace(line[n])) {
          break;
        }

        if (line[n] == '"') {
          insideString = !insideString;
          n++;
        } else
          value << line[n++];
      }

      config.setOption(key.str(), value.str());
    }
  }

  static std::filesystem::path getUserSettingsFilePath() {
    const char* localAppData = std::getenv("LOCALAPPDATA");
    if (localAppData == nullptr || localAppData[0] == '\0' || !dxvk::util::RtxFileSys::isInitialized()) {
      return {};
    }

    const std::string configFileName = dxvk::perGameConfigFileName(dxvk::util::RtxFileSys::rootPath());
    if (configFileName.empty()) {
      return {};
    }

    return std::filesystem::path(localAppData) / "NVIDIA" / "RTXRemix" / "user_settings" / configFileName;
  }
}

namespace bridge_util {

  bool Config::s_bIsInit = false;

  std::vector<Config::AppDefaultConfig> Config::appDefaultConfigs = {
    { "Source Engine", R"(\\hl2\.exe$)", {
      { "presentSemaphoreMaxFrames ", "1" }
    }}
  };

  void Config::init(const App app, void* hModuleLogOwner) {
    if (s_bIsInit) {
      Logger::err("Config already init.");
      return;
    }
    Config& config = get();
    if (app == Config::App::Server) {
      const auto parentPid = getParentPID();
      const std::string parentExeName = getProcessName(parentPid);
      config.merge(getAppDefaultConfig(parentExeName.c_str()));
    } else {
      config.merge(getAppDefaultConfig());
    }
    config.merge(getUserConfig(app, hModuleLogOwner));
    config.logOptions();
    s_bIsInit = true;
  }


  void Config::setOption(const std::string& key, const std::string& value) {
    m_options.insert_or_assign(key, value);
  }


  Config Config::loadFromFile(const std::filesystem::path& filePath) {
    Config config;

    std::ifstream stream(filePath);
    if (!stream.good()) {
      return config;
    }

    std::string line;
    while (std::getline(stream, line)) {
      parseUserConfigLine(config, line);
    }

    return config;
  }


  bool Config::saveToFile(const Config& config, const std::filesystem::path& filePath) {
    std::error_code ec;
    std::filesystem::create_directories(filePath.parent_path(), ec);
    if (ec) {
      return false;
    }

    std::ofstream stream(filePath, std::ios::trunc);
    if (!stream) {
      return false;
    }

    for (const auto& pair : config.m_options) {
      const bool needsQuotes = pair.second.find_first_of(" \t") != std::string::npos;
      if (needsQuotes) {
        stream << pair.first << " = \"" << pair.second << "\"\n";
      } else {
        stream << pair.first << " = " << pair.second << '\n';
      }
    }

    return true;
  }


  Config::Config() {
  }
  Config::~Config() {
  }


  void Config::merge(const Config& other) {
    for (auto& [key, value] : other.m_options) {
      m_options[key] = value;
    }
  }


  Config Config::getAppDefaultConfig(const char* exeFilePathIn) {
    Config config;

    auto exeFilePath = (exeFilePathIn == nullptr) ? getModuleFilePath() : exeFilePathIn; // No HMODULE parameter; We want the parent app, not dll

    auto pFoundAppDefaultConfig = std::find_if(appDefaultConfigs.begin(), appDefaultConfigs.end(),
      [&exeFilePath](const AppDefaultConfig& appDefaultConfig) {
        std::regex expr(appDefaultConfig.regex, std::regex::extended | std::regex::icase);
        return std::regex_search(exeFilePath.string(), expr);
      });

    if (pFoundAppDefaultConfig != appDefaultConfigs.end()) {
      // Inform the user that we loaded a default config
      Logger::info(std::string("Found default config for: ") + pFoundAppDefaultConfig->appName);
      Config appDefaultConfig;
      appDefaultConfig.m_options = pFoundAppDefaultConfig->options;
      return appDefaultConfig;
    }

    Logger::info(std::string("No default config found for: ") + exeFilePath.string());
    return Config();
  }


  Config Config::getUserConfig(const App app, void* hModuleConfigOwner) {
    Config config;

    const HMODULE _hModuleConfigOwner = reinterpret_cast<HMODULE>(hModuleConfigOwner);
    auto moduleFilePath = getModuleFilePath(_hModuleConfigOwner);
    const size_t finalDirPos = moduleFilePath.string().find_last_of('\\');
    if (finalDirPos == std::string::npos) {
      Logger::err("Error resolving module path for config setup.");
      return config;
    }
    const std::string moduleDir = moduleFilePath.string().substr(0, finalDirPos + 1);
    const std::string trexDirName = dxvk::util::RtxFileSys::runtimeDirName(getNativeArchString());
    const std::string trexDirPath = (app == App::Client) ? moduleDir + trexDirName + "\\" : moduleDir;
    const std::filesystem::path userConfPath = trexDirPath + "bridge.conf";

    Logger::info(std::string("Trying to open config file: ") + userConfPath.string());
    if (!std::filesystem::exists(userConfPath)) {
      return config;
    }

    Logger::info(std::string("Found user config file: ") + userConfPath.string());
    return loadFromFile(userConfPath);
  }


  bool Config::s_userSettingsLoaded = false;
  static std::mutex s_userSettingsMutex;

  Config& Config::getUserSettings() {
    static Config config;
    {
      std::lock_guard<std::mutex> lock(s_userSettingsMutex);
      if (!s_userSettingsLoaded) {
        s_userSettingsLoaded = true;
        config.m_options.clear();

        const std::filesystem::path filePath = getUserSettingsFilePath();
        if (!filePath.empty()) {
          config = loadFromFile(filePath);
        }
      }
    }
    return config;
  }

  bool Config::setUserSetting(const char* option, const std::string& value) {
    if (option == nullptr || option[0] == '\0') {
      return false;
    }

    const std::filesystem::path filePath = getUserSettingsFilePath();
    if (filePath.empty()) {
      return false;
    }

    std::lock_guard<std::mutex> lock(s_userSettingsMutex);
    Config config = loadFromFile(filePath);
    config.setOption(option, value);

    if (!saveToFile(config, filePath)) {
      return false;
    }

    s_userSettingsLoaded = false;
    return true;
  }

  void Config::reloadUserSettings() {
    std::lock_guard<std::mutex> lock(s_userSettingsMutex);
    s_userSettingsLoaded = false;
  }

  void Config::logOptions() const {
    if (!m_options.empty()) {
      Logger::info("Effective configuration:");

      for (auto& pair : m_options) {
        std::stringstream ss;
        ss << "  " << pair.first << " = " << pair.second;
        Logger::info(ss.str());
      }
    }
  }

  bool Config::isOptionDefined(const char* option) {
    return get().m_options.find(option) != get().m_options.end();
  }

  std::string Config::getOptionValue(const char* option) const {
    auto iter = m_options.find(option);
    return iter != m_options.end() ? iter->second : std::string();
  }


  bool Config::parseOptionValue(const std::string& value, std::string& result) {
    if (value.compare("") == 0) {
      return false;
    }
    result = value;
    return true;
  }

  bool Config::parseOptionValue(const std::string& value, std::vector<std::string>& result) {
    std::stringstream ss(value);
    std::string s;
    bool bFoundAtLeastOne = false;
    while (std::getline(ss, s, ',')) {
      bFoundAtLeastOne = true;
      result.push_back(s);
    }
    return bFoundAtLeastOne;
  }

  bool Config::parseOptionValue(const std::string& value, std::vector<std::size_t>& result) {
    std::stringstream ss(value);
    std::string s;
    bool bFoundAtLeastOne = false;
    while (std::getline(ss, s, ',')) {
      bFoundAtLeastOne = true;
      size_t val;
      if (s.find(kStrKiloByte) != std::string::npos) {
        const double numKBytes = std::stod(s, nullptr);
        val = static_cast<size_t>(numKBytes * static_cast<double>(kKByte));
      } else if (s.find(kStrMegaByte) != std::string::npos) {
        const double numMBytes = std::stod(s, nullptr);
        val = static_cast<size_t>(numMBytes * static_cast<double>(kMByte));
      } else if (s.find(kStrGigaByte) != std::string::npos) {
        const double numGBytes = std::stod(s, nullptr);
        val = static_cast<size_t>(numGBytes * static_cast<double>(kGByte));
      } else if (s.find("0b") != std::string::npos) {
        val = std::bitset<32>(s, 2).to_ulong();
      } else if (s.find("0x") != std::string::npos) {
        val = std::stoul(s, nullptr, 16);
      } else {
        val = std::stoul(s, nullptr, 10);
      }
      result.push_back(val);
    }
    return bFoundAtLeastOne;
  }

  bool Config::parseOptionValue(const std::string& value, bool& result) {
    if (value == "True") {
      result = true;
      return true;
    } else if (value == "False") {
      result = false;
      return true;
    } else {
      return false;
    }
  }


  bool Config::parseOptionValue(const std::string& value, int32_t& result) {
    if (value.size() == 0) {
      return false;
    }

    // Parse sign, don't allow '+'
    int32_t sign = 1;
    size_t start = 0;

    if (value[0] == '-') {
      sign = -1;
      start = 1;
    }

    // Parse absolute number
    int32_t intval = 0;

    for (size_t i = start; i < value.size(); i++) {
      if (value[i] < '0' || value[i] > '9') {
        return false;
      }

      intval *= 10;
      intval += value[i] - '0';
    }

    // Apply sign and return
    result = sign * intval;
    return true;
  }


  bool Config::parseOptionValue(const std::string& value, uint32_t& result) {
    if (value.size() == 0) {
      return false;
    }

    if (value.find("kB") != std::string::npos) {
      static constexpr double kKByte = 1 << 10;
      const double numKBytes = std::stod(value, nullptr);
      result = static_cast<size_t>(numKBytes * kKByte);
    } else if (value.find("MB") != std::string::npos) {
      static constexpr double kMByte = 1 << 20;
      const double numMBytes = std::stod(value, nullptr);
      result = static_cast<size_t>(numMBytes * kMByte);
    } else if (value.find("GB") != std::string::npos) {
      static constexpr double kGByte = 1 << 30;
      const double numGBytes = std::stod(value, nullptr);
      result = static_cast<size_t>(numGBytes * kGByte);
    } else if (value.find("0b") != std::string::npos) {
      result = std::bitset<32>(value, 2).to_ulong();
    } else if (value.find("0x") != std::string::npos) {
      result = std::stoul(value, nullptr, 16);
    } else {
      result = std::stoul(value, nullptr, 10);
    }
    return true;
  }


  bool Config::parseOptionValue(const std::string& value, uint16_t& result) {
    if (value.size() == 0) {
      return false;
    }

    // Parse absolute number
    uint16_t intval = 0;

    for (size_t i = 0; i < value.size(); i++) {
      if (value[i] < '0' || value[i] > '9') {
        return false;
      }

      intval *= 10;
      intval += value[i] - '0';
    }

    result = intval;
    return true;
  }


  bool Config::parseOptionValue(const std::string& value, uint8_t& result) {
    if (value.size() == 0) {
      return false;
    }

    // Parse absolute number
    uint8_t intval = 0;

    for (size_t i = 0; i < value.size(); i++) {
      if (value[i] < '0' || value[i] > '9') {
        return false;
      }

      intval *= 10;
      intval += value[i] - '0';
    }

    result = intval;
    return true;
  }


  bool Config::parseOptionValue(const std::string& value, float& result) {
    if (value.size() == 0) {
      return false;
    }

    result = std::stof(value);
    return true;
  }


  bool Config::parseOptionValue(const std::string& value, Tristate& result) {
    if (value == "True") {
      result = Tristate::True;
      return true;
    } else if (value == "False") {
      result = Tristate::False;
      return true;
    } else if (value == "Auto") {
      result = Tristate::Auto;
      return true;
    } else {
      return false;
    }
  }

}
