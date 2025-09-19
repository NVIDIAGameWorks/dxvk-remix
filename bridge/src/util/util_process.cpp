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
#include <windows.h>
#include <tchar.h>
#include <stdio.h>

#include "log/log.h"
#include "util_process.h"

namespace bridge_util {

  HANDLE Process::createChildProcess(LPCTSTR szCmdline) {
    PROCESS_INFORMATION piProcInfo;
    STARTUPINFO siStartInfo;
    BOOL bFuncRetn = FALSE;

    // Set up members of the PROCESS_INFORMATION structure.
    ZeroMemory(&piProcInfo, sizeof(PROCESS_INFORMATION));

    // Set up members of the STARTUPINFO structure.
    ZeroMemory(&siStartInfo, sizeof(STARTUPINFO));
    siStartInfo.cb = sizeof(STARTUPINFO);

    // Make a copy of the string because the function modifies it
    LPTSTR lpCommandLine = strdup(szCmdline);

    // Create the child process.
    bFuncRetn = CreateProcess(
      NULL,          // application name
      lpCommandLine, // command line
      NULL,          // process security attributes
      NULL,          // primary thread security attributes
      TRUE,          // handles are inherited
      HIGH_PRIORITY_CLASS,             // creation flags
      NULL,          // use parent's environment
      NULL,          // use parent's current directory
      &siStartInfo,  // STARTUPINFO pointer
      &piProcInfo);  // receives PROCESS_INFORMATION
    if (bFuncRetn == 0) {
      printf("CreateProcess failed (%d)\n", GetLastError());
      return INVALID_HANDLE_VALUE;
    } else {
      processMainThreadId = GetThreadId(piProcInfo.hThread);
      CloseHandle(piProcInfo.hThread);
      return piProcInfo.hProcess;
    }
  }

  void Process::releaseChildProcess() {
    // Ensure this process is around until the child process terminates
    if (INVALID_HANDLE_VALUE != hProcess) {
      UnregisterExitCallback();

      // Give the child process 3 seconds to terminate on its own before we kill it
      DWORD result = WaitForSingleObject(hProcess, 3'000);
      if (result == WAIT_TIMEOUT) {
        TerminateProcess(hProcess, 1);
      }
      CloseHandle(hProcess);
    }
    // Also close the duplicate client process handle that we created for the server
    if (INVALID_HANDLE_VALUE != hDuplicate) {
      CloseHandle(hDuplicate);
    }
  }

  bool Process::RegisterExitCallback(ProcessExitCallback callback) {
    // If no callback was passed in or one is already registered then bail out
    if (!callback || exitCallback) {
      return false;
    }

    exitCallback = callback;

    BOOL result = RegisterWaitForSingleObject(&hWait, hProcess, OnExited, this, INFINITE, WT_EXECUTEONLYONCE);
    if (!result) {
      DWORD error = GetLastError();
      Logger::err(format_string("RegisterExitCallback() failed with error code %d", error));
    }

    return result;
  }

  void Process::UnregisterExitCallback() {
    // Unregister the exit callback
    if (hWait) {
      // According to MSDN docs INVALID_HANDLE_VALUE means the function
      // waits for all callback functions to complete before returning.
      UnregisterWaitEx(hWait, INVALID_HANDLE_VALUE);
      hWait = NULL;
    }
  }

  HANDLE Process::GetCurrentProcessHandle() {
    // To get a handle for the current client process we need to duplicate
    // the pseudohandle for the context of the server process, which will
    // give us a real handle we can send to the server for monitoring.
    BOOL result = DuplicateHandle(
      GetCurrentProcess(),    // Handle of the source process, i.e. the client
      GetCurrentProcess(),    // Handle we want duplicate, also the client process
      hProcess,               // Handle of the target process, i.e. the server
      &hDuplicate,            // Variable to store the duplicate handle in
      0,                      // Ignored
      false,                  // Handle is not inheritable
      DUPLICATE_SAME_ACCESS); // Same access as source handle

    if (result) {
      return hDuplicate;
    } else {
      DWORD error = GetLastError();
      Logger::err(format_string("DuplicateHandle() failed with error code %d", error));
      return INVALID_HANDLE_VALUE;
    }
  }

  bool Process::PostMessageToMainThread(UINT msg, WPARAM wParam, LPARAM lParam) const {
    if (processMainThreadId == 0)
      return false;

    return ::PostThreadMessage(processMainThreadId, msg, wParam, lParam);
  }
}
