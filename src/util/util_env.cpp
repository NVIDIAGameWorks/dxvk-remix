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
#include <numeric>

#include "util_env.h"
// NV-DXVK start: get environment variable with a fallback
#include "config/config.h"
// NV-DXVK end

#include "./com/com_include.h"

#include <Windows.h>
#include <stdio.h>
#include <tlhelp32.h>
#include <Psapi.h>
#include <Shlwapi.h>
// NV-DXVK start
#include <set>
#include <filesystem>
// NV-DXVK end
#include "../tracy/TracyC.h"

namespace dxvk::env {
  const char* kRenderingServerExeName = "NvRemixBridge.exe";

  std::string getEnvVar(const char* name) {
    std::vector<WCHAR> result;
    result.resize(MAX_PATH + 1);

    DWORD len = ::GetEnvironmentVariableW(str::tows(name).c_str(), result.data(), MAX_PATH);
    result.resize(len);

    return str::fromws(result.data());
  }

  // NV-DXVK start: get environment variable with a fallback
  template<>
  bool getEnvVar(const char* name, bool fallback) {
    const std::string& value = getEnvVar(name);

    bool result = fallback;
    Config::parseOptionValue(value, result);
    return result;
  }
  // NV-DXVK end

  bool setEnvVar(const char* name, const char* value) {
    const auto result = ::SetEnvironmentVariableW(str::tows(name).c_str(), str::tows(value).c_str());

    return result != 0;
  }
  
  DWORD getParentPID() {
    const DWORD pid = GetCurrentProcessId();
    HANDLE h = NULL;
    PROCESSENTRY32 pe = { 0 };
    DWORD ppid = 0;
    pe.dwSize = sizeof(PROCESSENTRY32);
    h = ::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (::Process32First(h, &pe)) {
      do {
        if (pe.th32ProcessID == pid) {
          ppid = pe.th32ParentProcessID;
          break;
        }
      } while (::Process32Next(h, &pe));
    }
    ::CloseHandle(h);
    return (ppid);
  }

  std::string getProcessName(DWORD pid) {
    HANDLE h = ::OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (h) {
      std::vector<WCHAR> exePath;
      exePath.resize(MAX_PATH + 1);

      DWORD len = ::GetModuleFileNameExW(h, NULL, exePath.data(), MAX_PATH);
      exePath.resize(len);

      ::CloseHandle(h);

      return str::fromws(exePath.data());
    }

    return "";
  }


  size_t matchFileExtension(const std::string& name, const char* ext) {
    auto pos = name.find_last_of('.');

    if (pos == std::string::npos)
      return pos;

    bool matches = std::accumulate(name.begin() + pos + 1, name.end(), true,
      [&ext] (bool current, char a) {
        if (a >= 'A' && a <= 'Z')
          a += 'a' - 'A';
        return current && *ext && a == *(ext++);
      });

    return matches ? pos : std::string::npos;
  }


  std::string getExeName() {
    std::string fullPath = getExePath();

    auto n = fullPath.find_last_of('\\');

    std::string currentProcessName = (n != std::string::npos) ? fullPath.substr(n + 1) : fullPath;

    return currentProcessName;
  }

  std::string getExeNameNoSuffix() {
    std::string exeName = env::getExeName();
    auto extp = exeName.find_last_of('.');

    if (extp != std::string::npos && exeName.substr(extp + 1) == "exe")
      exeName.erase(extp);
    return exeName;
  }

  // NV-DXVK start
  bool isRemixBridgeActive() {
    enum IsRemixBridgeActiveStatus {
      Unchecked = 0,
      Active,
      Inactive
    };
    
    static std::atomic<IsRemixBridgeActiveStatus> bridgeStatus = IsRemixBridgeActiveStatus::Unchecked;

    // Don't expect this to change in the process life-cycle, cache and read to avoid unnecessary string ops
    if(bridgeStatus == IsRemixBridgeActiveStatus::Unchecked) {
      std::vector<WCHAR> exePath;
      exePath.resize(MAX_PATH + 1);

      DWORD len = ::GetModuleFileNameW(NULL, exePath.data(), MAX_PATH);
      exePath.resize(len);

      std::string fullPath = str::fromws(exePath.data());
      
      if (std::strstr(fullPath.c_str(), kRenderingServerExeName) != nullptr) {
        bridgeStatus = IsRemixBridgeActiveStatus::Active;
      } else {
        bridgeStatus = IsRemixBridgeActiveStatus::Inactive;
      }
    }

    return (bridgeStatus == IsRemixBridgeActiveStatus::Active);
  }
  // NV-DXVK end

  std::string getExeBaseName() {
    auto exeName = getExeName();
    auto extp = matchFileExtension(exeName, "exe");

    if (extp != std::string::npos)
      exeName.erase(extp);

    return exeName;
  }


  std::string getExePath() {
    std::vector<WCHAR> exePath;
    exePath.resize(MAX_PATH + 1);

    DWORD len = ::GetModuleFileNameW(NULL, exePath.data(), MAX_PATH);
    exePath.resize(len);

    std::string fullPath = str::fromws(exePath.data());

    // If this process was launched from the bridge, we should look at our parent process name
    if (isRemixBridgeActive()) {
      fullPath = getProcessName(getParentPID());
    }

    return fullPath;
  }
  
  // NV-DXVK start
  std::string dedupeFilename(const std::string& originalFilePath) {
    const auto ogPath = std::filesystem::path(originalFilePath);
    const auto parentDirPath = ogPath.parent_path();
    auto filePathMinusExtension = parentDirPath;
    filePathMinusExtension /= ogPath.stem();
    
    // Enumerate all files that match the input file, even those with "__#" suffix
    std::set<std::filesystem::path> matchingFilePaths;
    for (const auto& dir_entry : std::filesystem::directory_iterator{parentDirPath}) {
      if (dir_entry.path().string().find(filePathMinusExtension.string()) != std::string::npos) {
        matchingFilePaths.insert(dir_entry.path());
      }
    }

    // Process matching file names
    const std::string suffixPrefix("__");
    if (matchingFilePaths.empty()) {
      // Quick path out
      return originalFilePath;
    } else {
      // Figure out what the highest dupe name created is, so we can +1 it
      long highestDupeNum = 0;
      const auto commonDeduped = filePathMinusExtension.string() + suffixPrefix;
      for (const auto& matchingFilePath : matchingFilePaths) {
        const auto dupeNumPos = matchingFilePath.string().find(commonDeduped);
        if (dupeNumPos == 0) {
          const auto potentialSuffix = matchingFilePath.string().substr(commonDeduped.length());
          // stol is convenient and throws if the string is not numerical
          try {
            const auto dupeNumFromName = std::stol(potentialSuffix);
            highestDupeNum = std::max(dupeNumFromName, highestDupeNum);
          } catch (...) {}
        }
      }

      // Form new file name
      const auto dupeNum = highestDupeNum + 1;
      const auto suffixStr = suffixPrefix + std::to_string(dupeNum);
      auto newFileNameStr = filePathMinusExtension.string();
      newFileNameStr.replace(newFileNameStr.length(), suffixStr.length(), suffixStr);
      newFileNameStr += ogPath.extension().string();
      auto newPath = ogPath;
      newPath.replace_filename(newFileNameStr);
      return newPath.string();
    }
  }
  
  std::string getModulePath(const char* module) {
    std::vector<WCHAR> modulePath;
    modulePath.resize(MAX_PATH + 1);

    HMODULE hModule = GetModuleHandle(module);
    GetModuleFileNameW(hModule, modulePath.data(), MAX_PATH);
    PathRemoveFileSpecW(modulePath.data());

    std::string path = str::fromws(modulePath.data());
    return path;
  }

  bool getAvailableSystemPhysicalMemory(uint64_t& availableSize) {
    MEMORYSTATUSEX memInfo;
    memInfo.dwLength = sizeof(MEMORYSTATUSEX);
    BOOL res = GlobalMemoryStatusEx(&memInfo);
    if (res) {
      availableSize = memInfo.ullAvailPhys;
    }
    return res != 0;
  }

  std::string getDllDirectory() {
    HMODULE hModule;
    GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR) &getDllDirectory, &hModule);

    wchar_t modulePath[MAX_PATH];
    GetModuleFileNameW(hModule, modulePath, MAX_PATH);
    PathRemoveFileSpecW(modulePath);

    std::filesystem::path path(modulePath);
    return path.string();
  }
  // NV-DXVK end

  void setThreadName(const std::string& name) {
    TracyCSetThreadName(name.c_str());

    using SetThreadDescriptionProc = HRESULT (WINAPI *) (HANDLE, PCWSTR);

    static auto proc = reinterpret_cast<SetThreadDescriptionProc>(
      ::GetProcAddress(::GetModuleHandleW(L"kernel32.dll"), "SetThreadDescription"));

    if (proc != nullptr) {
      auto wideName = std::vector<WCHAR>(name.length() + 1);
      str::tows(name.c_str(), wideName.data(), wideName.size());
      (*proc)(::GetCurrentThread(), wideName.data());
    }
  }

  bool createDirectory(const std::string& path) {
    WCHAR widePath[MAX_PATH];
    str::tows(path.c_str(), widePath);
    return !!CreateDirectoryW(widePath, nullptr);
  }

  void killProcess() {
    const DWORD pid = GetCurrentProcessId();
    HANDLE hnd;
    hnd = OpenProcess(SYNCHRONIZE | PROCESS_TERMINATE, TRUE, pid);
    TerminateProcess(hnd, 0);
  }
}
