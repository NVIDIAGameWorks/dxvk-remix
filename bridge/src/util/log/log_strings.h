/*
 * Copyright (c) 2022-2024, NVIDIA CORPORATION. All rights reserved.
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

#include <string>
#include <map>

namespace logger_strings {
  constexpr char* OutOfBufferMemory = "The host application has tried to write data larger than one of the RTX Remix Bridge's buffers. Increase one of the buffer sizes in \".trex\\bridge.conf\".\n\n";
  constexpr char* OutOfBufferMemory1 = " Buffer Option: ";
  constexpr char* MultipleActiveCommands = "Multiple active Command instances detected!";
  constexpr char* RtxRemixRuntimeError = "RTX Remix Runtime Error!";
  constexpr char* BridgeClientClosing = "The RTX Remix Runtime has encountered an unexpected issue. The application will close.\n\n"
                                        "Please collect any: \n"
                                        "  *.log files in <application_directory>/rtx-remix/logs/\n"
                                        "  *.dmp files next to the application or in the .trex folder\n"
                                        "and report the error at https://github.com/NVIDIAGameWorks/rtx-remix/issues.";
  
  namespace WndProc {
    constexpr char* kStr_newSetWindowLong_settingHwnd = "[WndProc][NewSetWindowLong] Setting new HWND=0x%08x, OldHWND=0x%08x";
    constexpr char* kStr_newSetWindowLong_settingWndProc = "[WndProc][NewSetWindowLong] Setting NewWndProc=0x%08x, OldWndProc=0x%08x";
    constexpr char* kStr_newGetWindowLong_gettingWndProc = "[WndProc][NewGetWindowLong] Getting WndProc=0x%08x";
    constexpr char* kStr_init_attachErr = "[WndProc][init] Attach failed!";
    constexpr char* kStr_terminate_detachErr = "[WndProc][terminate] Detach failed!";
    constexpr char* kStr_set_implicitWarn = "[WndProc][set] Calling WndProc::set(...) without an intermediate unset(). Calling implicitly...";
    constexpr char* kStr_set_failedErr = "[WndProc][set] Failed!";
    constexpr char* kStr_set_settingWndProc = "[WndProc][set] Setting RemixWndProc=0x%08x, GameWndProc=0x%08x";
    constexpr char* kStr_unset_wndProcInvalidWarn = "[WndProc][unset] Previous WndProc is invalid.";
    constexpr char* kStr_unset_unsettingWndProc = "[WndProc][unset] Unsetting prevWndProc=0x%08x, GameWndProc=0x%08x";
  }

  inline static const std::map<const std::string, const std::string> bufferNameToOptionMap =
  {
    {"ModuleClient2ServerData", "moduleClientChannelMemSize"},
    {"ModuleServer2ClientData", "moduleServerChannelMemSize"},
    {"DeviceClient2ServerData", "clientChannelMemSize"},
    {"DeviceServer2ClientData", "serverChannelMemSize"}
  };

  static inline std::string bufferNameToOption(std::string name) {
    std::string optionName = "";
    auto it = logger_strings::bufferNameToOptionMap.find(name);
    if (it != logger_strings::bufferNameToOptionMap.end()) {
      optionName = it->second;
    }
    return optionName;
  }

}