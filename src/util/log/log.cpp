/*
* Copyright (c) 2021-2023, NVIDIA CORPORATION. All rights reserved.
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
#include "log.h"

#include <iostream>

#include "../util_env.h"
#include "../util_filesys.h"


// NV-DXVK start: Don't double print every line
namespace{
  bool getDoublePrintToStdErr() {
    const std::string str = dxvk::env::getEnvVar("DXVK_LOG_NO_DOUBLE_PRINT_STDERR");
    return str.empty();
  }

  template<int N>
  static inline void getLocalTimeString(char(&timeString)[N]) {
    // [HH:MM:SS.MS]
    static const char* format = "[%02d:%02d:%02d.%03d] ";

#ifdef _WIN32
    SYSTEMTIME lt;
    GetLocalTime(&lt);

    sprintf_s(timeString, format,
              lt.wHour, lt.wMinute, lt.wSecond, lt.wMilliseconds);
#else
    struct timeval tv;
    gettimeofday(&tv, NULL);

    struct tm* lt = localtime(&tv.tv_sec);

    sprintf_s(timeString, format,
              lt->tm_hour, lt->tm_min, lt->tm_sec, (tv.tv_usec / 1000) % 1000);
#endif
  }
}
// NV-DXVK end

namespace dxvk {

  Logger::Logger(const std::string& fileName, const LogLevel logLevel)
  : m_minLevel(logLevel)
  // NV-DXVK start: Don't double print every line
  , m_doublePrintToStdErr(getDoublePrintToStdErr())
  // NV-DXVK end
  {
    if (m_minLevel != LogLevel::None) {
      const auto path = getFilePath(fileName);

      if (!path.empty()) {
        m_fileStream = std::ofstream(str::tows(path.c_str()).c_str());
        assert(m_fileStream.is_open());
      }
    }
  }
  
  void Logger::initRtxLog() {
    s_instance = std::move(Logger("remix-dxvk.log"));
  }

  void Logger::trace(const std::string& message) {
    s_instance.emitMsg(LogLevel::Trace, message);
  }
  
  void Logger::debug(const std::string& message) {
    s_instance.emitMsg(LogLevel::Debug, message);
  }

  void Logger::info(const std::string& message) {
    s_instance.emitMsg(LogLevel::Info, message);
  }

  void Logger::warn(const std::string& message) {
    s_instance.emitMsg(LogLevel::Warn, message);
  }

  void Logger::err(const std::string& message) {
    s_instance.emitMsg(LogLevel::Error, message);
  }

  void Logger::log(LogLevel level, const std::string& message) {
    s_instance.emitMsg(level, message);
  }

  void Logger::emitMsg(LogLevel level, const std::string& message) {
    if (level >= m_minLevel) {
      OutputDebugString((message + '\n').c_str());

      std::lock_guard<dxvk::mutex> lock(m_mutex);
      
      constexpr std::array<const char*, 5> s_prefixes{
        "trace: ",
        "debug: ",
        "info:  ",
        "warn:  ",
        "err:   ",
      };
      const char* prefix = s_prefixes[static_cast<std::uint32_t>(level)];

      std::stringstream stream(message);
      std::string       line;

      char timeString[64];
      getLocalTimeString(timeString);

      while (std::getline(stream, line, '\n')) {
        // NV-DXVK start: Don't double print every line
        if (m_doublePrintToStdErr) {
          std::cerr << timeString << prefix << line << std::endl;
        }
        // NV-DXVK end

        if (m_fileStream) {
          m_fileStream << timeString << prefix << line << std::endl;
        }
      }
    }
  }
  
  
  LogLevel Logger::getMinLogLevel() {
    const std::array<std::pair<const char*, LogLevel>, 6> logLevels{ {
      { "trace", LogLevel::Trace },
      { "debug", LogLevel::Debug },
      { "info",  LogLevel::Info  },
      { "warn",  LogLevel::Warn  },
      { "error", LogLevel::Error },
      { "none",  LogLevel::None  },
    } };
    
    const std::string logLevelStr = env::getEnvVar("DXVK_LOG_LEVEL");
    
    for (const auto& pair : logLevels) {
      if (logLevelStr == pair.first)
        return pair.second;
    }
    
    return LogLevel::Info;
  }
  
  std::string Logger::getFilePath(const std::string& fileName) {
    // NV-DXVK start: Use std::filesystem::path helpers + RtxFileSys
    auto path = util::RtxFileSys::path(util::RtxFileSys::Logs);

    // Note: If no path is specified to store log files in, simply use the current directory by returning
    // the specified log file name directly.
    if (path.empty()) {
      return fileName;
    }

    // Append the specified log file name to the logging directory.
    path /= fileName;

    return path.string();
    // NV-DXVK end
  }
  
  Logger& Logger::operator=(Logger&& other) {
    m_minLevel = other.m_minLevel;
    m_doublePrintToStdErr = other.m_doublePrintToStdErr;
    std::swap(m_fileStream, other.m_fileStream);
    return *this;
  }
  
}
