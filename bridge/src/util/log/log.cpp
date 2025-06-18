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
#include "log.h"
#include "log_strings.h"

#include "../config/global_options.h"
#include "util_filesys.h"
#include "util_process.h"

#include <array>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <cassert>

#ifndef _WIN32
#include <sys/time.h>
#endif

using namespace dxvk::util;

namespace bridge_util {
  template<int N>
  static inline void getLocalTimeString(char (&timeString)[N]) {
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

  Logger* Logger::logger = nullptr;
  Logger::PreInitMessageArr Logger::s_preInitMsgs;
  std::mutex Logger::s_mutex;

  void Logger::init() {
    assert(logger == nullptr && "Logger already init!");
    if (logger == nullptr) {
      logger = new Logger(GlobalOptions::getLogLevel());
      emitPreInitMsgs();
    }
  }

  Logger& Logger::get() {
    return *logger;
  }

  Logger::Logger(const LogLevel log_level) : m_level(log_level) {
    if (m_level != LogLevel::None) {

#ifdef REMIX_BRIDGE_CLIENT
      std::string logName = "bridge32.log";
#else
      std::string logName = "bridge64.log";
#endif
      auto logPath = RtxFileSys::path(RtxFileSys::Logs);
      logPath /= logName;
#ifdef REMIX_BRIDGE_CLIENT
      uint32_t attempt = 0;
      while (attempt < 4) {
        m_hFile = CreateFileA(logPath.string().c_str(), GENERIC_WRITE, FILE_SHARE_READ, NULL,
                            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL |
                            FILE_FLAG_WRITE_THROUGH, NULL);
        if (m_hFile != INVALID_HANDLE_VALUE) {
          break;
        }
        // Dump Win32 errors to debug output in DEBUG mode
        emitMsg(LogLevel::Error, format_string("Log CreateFile() failed with %d", GetLastError()));

        attempt++;
      }
#else
      m_fileStream = std::ofstream(logPath);
#endif
    }
  }

  Logger::~Logger() {
#ifdef REMIX_BRIDGE_CLIENT
    CloseHandle(m_hFile);
#else
    if (m_fileStream.is_open()) {
      m_fileStream.close();
    }
#endif
  }

  void Logger::emitPreInitMsgs() {
    debug("[Pre-Init Message] Emitting...");
    for(size_t logLevel = (size_t)LogLevel::Trace;
               logLevel < (size_t)LogLevel::None; logLevel++) {
      auto& preInitSS = s_preInitMsgs[logLevel];
      emitMsg((LogLevel)logLevel, preInitSS.str());
      preInitSS.clear();
    }
    debug("[Pre-Init Message] Done!");
  }

  void Logger::trace(const std::string& message) {
    emitMsg(LogLevel::Trace, message);
  }

  void Logger::debug(const std::string& message) {
    emitMsg(LogLevel::Debug, message);
  }

  void Logger::info(const std::string& message) {
    emitMsg(LogLevel::Info, message);
  }

  void Logger::warn(const std::string& message) {
    emitMsg(LogLevel::Warn, message);
  }

  void Logger::err(const std::string& message) {
    emitMsg(LogLevel::Error, message);
  }

  void Logger::errLogMessageBoxAndExit(const std::string& message) {
    Logger::err(message);
    MessageBox(nullptr, message.c_str(), logger_strings::RtxRemixRuntimeError, MB_OK | MB_TOPMOST | MB_TASKMODAL);
    std::exit(-1 );
  }

  void Logger::log(const LogLevel level, const std::string& message) {
    emitMsg(level, message);
  }

  void Logger::logLine(const LogLevel level, const char* line) {
    get().emitLine(level, line);
  }

  void Logger::emitMsg(const LogLevel level, const std::string& message) {
    if(!logger) {
      auto ss = formatMessage(level, message);
      std::scoped_lock lock(s_mutex);
      s_preInitMsgs[(size_t)level] << ss.str();
    } else {
      auto ss = formatMessage(level, message);
      std::scoped_lock lock(s_mutex);
      std::string line;
      while (std::getline(ss, line, '\n')) {
        logger->emitLine(level, line);
      }
    }
  }

  void Logger::emitLine(const LogLevel level, const std::string& line) {
    if (level >= m_level) {
#ifdef REMIX_BRIDGE_CLIENT
      static char c_line[4096];
      int len = sprintf_s(c_line, "%s\n", line.c_str());
#ifdef _DEBUG
      OutputDebugStringA(c_line);
#endif
      if (m_hFile != INVALID_HANDLE_VALUE) {
        WriteFile(m_hFile, c_line, len, NULL, NULL);
      }
#else
      if (m_fileStream.is_open()) {
        m_fileStream << line << std::endl;
      }
#endif
    }
  }
  
  std::stringstream Logger::formatMessage(const LogLevel level, const std::string& message) {
    std::stringstream unformattedStream(message);
    std::string       line;
    constexpr std::array<const char*, 5> s_prefixes = {
      "trace: ",
      "debug: ",
      "info:  ",
      "warn:  ",
      "err:   ",
    };
    const char* prefix = s_prefixes[static_cast<uint32_t>(level)];
    char timeString[64];
    getLocalTimeString(timeString);

    std::stringstream formattedStream;
    while (std::getline(unformattedStream, line, '\n')) {
      formattedStream << timeString << prefix << line << '\n';
    }
    return std::move(formattedStream);
  }

  void Logger::set_loglevel(const LogLevel level) {
    get().m_level = level;
  }

}
