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
#pragma once

#include <cstdint>
#include <array>
#include <fstream>
#include <string>

#include "../thread.h"

namespace dxvk {
  
  enum class LogLevel : std::uint32_t {
    Trace = 0,
    Debug = 1,
    Info  = 2,
    Warn  = 3,
    Error = 4,
    None  = 5,
  };

  /**
   * \brief Logger
   * 
   * Logger for one DLL. Creates a text file and
   * writes all log messages to that file.
   */
  class Logger {
    
  public:

    // NV-DXVK start: pass log level as param
    Logger(const std::string& fileName, const LogLevel logLevel = getMinLogLevel());
    // NV-DXVK end
    ~Logger() = default;
    
    // NV-DXVK start: special init pathway for remix logs
    static void initRtxLog();
    // NV-DXVK end
    
    static void trace(const std::string& message);
    static void debug(const std::string& message);
    static void info (const std::string& message);
    static void warn (const std::string& message);
    static void err  (const std::string& message);
    static void log  (LogLevel level, const std::string& message);
    
    static LogLevel logLevel() {
      return s_instance.m_minLevel;
    }
    
  private:
    
    static Logger s_instance;
    
    LogLevel m_minLevel;
    // NV-DXVK start: Don't double print every line
    bool m_doublePrintToStdErr;
    // NV-DXVK end
    
    dxvk::mutex   m_mutex;
    std::ofstream m_fileStream;
    
    void emitMsg(LogLevel level, const std::string& message);
    
    static LogLevel getMinLogLevel();
    static std::string getFilePath(const std::string& fileName);

    Logger& operator=(Logger&& other);

  };
  
}
