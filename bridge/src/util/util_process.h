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
#ifndef UTIL_PROCESS_H_
#define UTIL_PROCESS_H_

#include <windows.h>
#include <tchar.h>
#include <stdio.h>
#include <sstream>
#include <tlhelp32.h>
#include <Psapi.h>
#include <Shlwapi.h>
#include <filesystem>

using fspath = std::filesystem::path;

namespace bridge_util {

  class Process {
  public:
    typedef void (*ProcessExitCallback)(Process const*);

    Process() = delete;
    Process(Process& p) = delete;

    Process(LPCTSTR cmd, ProcessExitCallback callback):
      hProcess(NULL),
      hWait(NULL),
      exitCallback(NULL) {
      hProcess = createChildProcess(cmd);
      RegisterExitCallback(callback);
    }

    ~Process() {
      releaseChildProcess();
    }

    bool PostMessageToMainThread(UINT msg, WPARAM wParam, LPARAM lParam) const;

    bool RegisterExitCallback(ProcessExitCallback callback);
    void UnregisterExitCallback();
    HANDLE GetCurrentProcessHandle();

  private:
    static void CALLBACK OnExited(void* context, BOOLEAN isTimeout) {
      ((Process*) context)->OnExited();
    }

    void OnExited() {
      if (exitCallback) {
        exitCallback(this);
      }
    }

    DWORD  processMainThreadId = 0;
    HANDLE hProcess;
    HANDLE hWait;
    HANDLE hDuplicate;
    ProcessExitCallback exitCallback;

    HANDLE createChildProcess(LPCTSTR szCmdline);

    void releaseChildProcess();
  };

  /**
   * \brief Helper for creating a std vector containing a windows path
   *
   * \return std vec of CHAR or WCHAR capable of storing MAX_PATH + \0
   */
  static inline auto createPathVec() {
#ifdef UNICODE
    std::vector<WCHAR> vecFilePath;
#else
    std::vector<char> vecFilePath;
#endif // !UNICODE
    vecFilePath.resize(MAX_PATH + 1);
    return vecFilePath;
  }

  /**
   * \brief Wrapper around Windows built-in GetModuleFileName
   *
   * \param [in] hModuleHandle the HMODULE in question. If NULL, defaults to parent executable.
   */
  static fspath getModuleFilePath(const HMODULE hModuleHandle = NULL) {
    auto vecModuleFilePath = createPathVec();
    vecModuleFilePath.resize(MAX_PATH + 1);
    GetModuleFileName(hModuleHandle, vecModuleFilePath.data(), MAX_PATH);
    fspath moduleFilePath(vecModuleFilePath.data());
    return moduleFilePath.string();
  }

  static DWORD getParentPID() {
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

  static std::string getProcessName(DWORD pid) {
    HANDLE h = ::OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (h) {
      char exePath[MAX_PATH + 1];

      DWORD len = ::GetModuleFileNameExA(h, NULL, &exePath[0], MAX_PATH);

      ::CloseHandle(h);

      return std::string(exePath);
    }

    return "";
  }

  static void killProcess() {
    const DWORD pid = GetCurrentProcessId();
    HANDLE hnd;
    hnd = OpenProcess(SYNCHRONIZE | PROCESS_TERMINATE, TRUE, pid);
    TerminateProcess(hnd, 0);
  }

  // https://gist.github.com/mattn/253013/d47b90159cf8ffa4d92448614b748aa1d235ebe4
  static DWORD getParentPid()
  {
    HANDLE hSnapshot;
    PROCESSENTRY32 pe32;
    DWORD ppid = 0, pid = GetCurrentProcessId();

    hSnapshot = CreateToolhelp32Snapshot( TH32CS_SNAPPROCESS, 0 );
    __try{
      if( hSnapshot == INVALID_HANDLE_VALUE ) __leave;

      ZeroMemory( &pe32, sizeof( pe32 ) );
      pe32.dwSize = sizeof( pe32 );
      if( !Process32First( hSnapshot, &pe32 ) ) __leave;

      do{
        if( pe32.th32ProcessID == pid ){
          ppid = pe32.th32ParentProcessID;
          break;
        }
      }while( Process32Next( hSnapshot, &pe32 ) );

    }
    __finally{
      if( hSnapshot != INVALID_HANDLE_VALUE ) CloseHandle( hSnapshot );
    }
    return ppid;
  }
  
}

#endif // UTIL_PROCESS_H_
