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
#include "util_seh.h"
#include "util_filesys.h"
#include "log/log.h"

#ifdef _WIN32

#include <assert.h>
#include <Windows.h>
#include <dbghelp.h>

using namespace bridge_util;

#pragma comment(lib, "dbghelp")

static void SafeLog(LogLevel level, const char* format, ...) {
  static char tmpSpace[4096];

  va_list args;
  va_start(args, format);
  int len = vsprintf_s(tmpSpace, format, args);
  va_end(args);

  if (len > 0) {
    Logger::logLine(level, tmpSpace);
  }
}

static LONG WINAPI BridgeExceptionHandler(PEXCEPTION_POINTERS pExceptionPointers) {
  SYSTEMTIME lt;
  GetLocalTime(&lt);

  char time[64];
  sprintf_s(time, "%04d%02d%02d_%02d%02d%02d", lt.wYear, lt.wMonth, lt.wDay,
            lt.wHour, lt.wMinute, lt.wSecond);

  char moduleFilename[MAX_PATH];
  GetModuleFileNameA(NULL, moduleFilename, MAX_PATH);

  char dumpFilename[MAX_PATH];
  sprintf_s(dumpFilename, "%s_%s.dmp", moduleFilename, time);

  HANDLE hFile = CreateFileA(dumpFilename, GENERIC_ALL, 0, NULL,
                             CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

  SafeLog(LogLevel::Info, "Exception 0x%x at %p! Saving minidump to '%s'",
          pExceptionPointers->ExceptionRecord->ExceptionCode,
          pExceptionPointers->ExceptionRecord->ExceptionAddress,
          dumpFilename);

  if (hFile != INVALID_HANDLE_VALUE) {
    MINIDUMP_EXCEPTION_INFORMATION ei;

    ei.ThreadId = GetCurrentThreadId();
    ei.ExceptionPointers = pExceptionPointers;
    ei.ClientPointers = TRUE;

    const DWORD flags = MiniDumpNormal | MiniDumpWithThreadInfo;

    bool result = MiniDumpWriteDump(GetCurrentProcess(),
                                    GetCurrentProcessId(), hFile,
                                    (MINIDUMP_TYPE) flags, &ei, NULL, NULL);

    if (result == FALSE) {
      SafeLog(LogLevel::Error, "Minidump write failed with %d", GetLastError());
    }

    CloseHandle(hFile);
  } else {
    SafeLog(LogLevel::Error, "CreateFile() failed with %d", GetLastError());
  }

  // Trap it in debug
  assert(0 && "Unhandled exception thrown!");

  return EXCEPTION_EXECUTE_HANDLER;
}

void ExceptionHandler::init() {
  ::SetUnhandledExceptionFilter(BridgeExceptionHandler);
}

#else

void ExceptionHandler::init() {
}

#endif
