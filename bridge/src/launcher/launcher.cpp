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

//
//  Based on withdll.cpp from Detours.
//
#include <stdio.h>
#include <windows.h>
#include <shlwapi.h>

#include "detours.cpp"
#include "creatwth.cpp"
#include "disasm.cpp"
#include "image.cpp"
#include "modules.cpp"

#pragma warning(push)
#if _MSC_VER > 1400
#pragma warning(disable:6102 6103) // /analyze warnings
#endif
#include <strsafe.h>
#pragma warning(pop)

#if DETOURS_64BIT
#define TARGET_SUFFIX "64"
#else
#define TARGET_SUFFIX "32"
#endif

#define LAUNCHER_NAME "NvRemixLauncher" TARGET_SUFFIX ".exe"
#define INJECTION_NAME "d3d9.dll"

static void PrintUsage(void) {
  printf("Usage:\n"
         "    " LAUNCHER_NAME " [-w work folder] [-i] <command line>\n\n");
  printf("The <command line> MUST contain full path to the executable file and the command line options if any.\n\n");
  printf("Options:\n");
  printf("    -w : set working folder if different from executable path in command line.\n");
  printf("    -i : attempt DLL injection instead of changing the search path.\n");
}

#ifdef _DEBUG
//  This code verifies that the named DLL has been configured correctly
//  to be imported into the target process.  DLLs must export a function with
//  ordinal #1 so that the import table touch-up magic works.
//
struct ExportContext {
  BOOL    fHasOrdinal1;
  ULONG   nExports;
};

static BOOL CALLBACK ExportCallback(_In_opt_ PVOID pContext,
                                    _In_ ULONG nOrdinal,
                                    _In_opt_ LPCSTR pszSymbol,
                                    _In_opt_ PVOID pbTarget) {
  (void) pContext;
  (void) pbTarget;
  (void) pszSymbol;

  ExportContext* pec = (ExportContext*) pContext;

  if (nOrdinal == 1) {
    pec->fHasOrdinal1 = TRUE;
  }
  pec->nExports++;

  return TRUE;
}
#endif

int CDECL main(int argc, char** argv) {
  if (argc < 2) {
    PrintUsage();
    return -1;
  }

  CHAR szChildCWD[1024] = { 0 };
  bool doInject = false;

  int arg = 1;

  while (true) {
    if (arg >= argc) {
      printf(LAUNCHER_NAME ": Error: Unable to parse command line. Please check the quotation marks in filepaths.\n");
      return -1;
    }

    if (strstr(argv[arg], "-w") == argv[arg]) {
      ++arg;
      // Copy CWD from parameter
      StringCchCopyA(szChildCWD, sizeof(szChildCWD), argv[arg]);
      PathUnquoteSpacesA(szChildCWD);
    } else if (strstr(argv[arg], "-i") == argv[arg]) {
      doInject = true;
    } else {
      break;
    }
    ++arg;
  }

  if (szChildCWD[0] == '\0') {
    // Try to strip CWD from the command line
    StringCchCopyA(szChildCWD, sizeof(szChildCWD), argv[arg]);
    PathRemoveFileSpecA(szChildCWD);
  }

  if (PathIsDirectoryA(szChildCWD) == FALSE) {
    printf(LAUNCHER_NAME ": Error: %s is not a valid working directory.\n",
           szChildCWD);
    return 9001;
  }

  CHAR szCWD[1024];
  GetCurrentDirectoryA(sizeof(szCWD), szCWD);
  StringCchCatA(szCWD, sizeof(szCWD), "\\");

  if (PathIsRelativeA(szChildCWD)) {
    CHAR szTMP[1024];
    StringCchCopyA(szTMP, sizeof(szTMP), szCWD);
    StringCchCatA(szTMP, sizeof(szTMP), szChildCWD);
    StringCchCopyA(szChildCWD, sizeof(szChildCWD), szTMP);
  }

  CHAR szDllInjectionPath[1024];
  if (doInject) {
    // Validate Remix dll is injectable just in case
    PCHAR pszFilePart = NULL;
    if (!GetFullPathNameA(INJECTION_NAME, ARRAYSIZE(szDllInjectionPath), szDllInjectionPath, &pszFilePart)) {
      printf(LAUNCHER_NAME ": Error: %s is not a valid path name.\n", INJECTION_NAME);
      return 9002;
    }
  }

#ifdef _DEBUG
  if (doInject) {
    // Validate Remix dll is injectable just in case
    HMODULE hDll = LoadLibraryExA(szDllInjectionPath, NULL, DONT_RESOLVE_DLL_REFERENCES);
    if (hDll == NULL) {
      printf(LAUNCHER_NAME ": Error: %s failed to load (error %ld).\n",
             szDllInjectionPath, GetLastError());
      return 9003;
    }

    ExportContext ec;
    ec.fHasOrdinal1 = FALSE;
    ec.nExports = 0;
    DetourEnumerateExports(hDll, &ec, ExportCallback);
    FreeLibrary(hDll);

    if (!ec.fHasOrdinal1) {
      printf(LAUNCHER_NAME ": Error: %s does not export ordinal #1.\n", szDllInjectionPath);
      printf("             See help entry DetourCreateProcessWithDllEx in Detours.chm.\n");
      return 9004;
    }
  }
#endif

  STARTUPINFOA si;
  PROCESS_INFORMATION pi;
  CHAR szCommand[2048];
  CHAR szExe[1024];
  CHAR szFullExe[1024] = "\0";
  PCHAR pszFileExe = NULL;

  ZeroMemory(&si, sizeof(si));
  ZeroMemory(&pi, sizeof(pi));
  si.cb = sizeof(si);

  szCommand[0] = L'\0';

  StringCchCopyA(szExe, sizeof(szExe), argv[arg]);
  for (; arg < argc; arg++) {
    if (strchr(argv[arg], ' ') != NULL || strchr(argv[arg], '\t') != NULL) {
      if (szCommand[0] == '\0' && PathIsRelativeA(szExe)) {
        StringCchCatA(szCommand, sizeof(szCommand), "\"");
        StringCchCatA(szCommand, sizeof(szCommand), szCWD);
        StringCchCatA(szCommand, sizeof(szCommand), szExe);
        StringCchCatA(szCommand, sizeof(szCommand), "\"");
      } else {
        StringCchCatA(szCommand, sizeof(szCommand), "\"");
        StringCchCatA(szCommand, sizeof(szCommand), argv[arg]);
        StringCchCatA(szCommand, sizeof(szCommand), "\"");
      }
    } else {
      if (szCommand[0] == '\0' && PathIsRelativeA(szExe)) {
        StringCchCatA(szCommand, sizeof(szCommand), szCWD);
        StringCchCatA(szCommand, sizeof(szCommand), szExe);
      } else {
        StringCchCatA(szCommand, sizeof(szCommand), argv[arg]);
      }
    }

    if (arg + 1 < argc) {
      StringCchCatA(szCommand, sizeof(szCommand), " ");
    }
  }

  DWORD dwFlags = CREATE_DEFAULT_ERROR_MODE | CREATE_SUSPENDED;

  SetLastError(0);
  SearchPathA(NULL, szExe, ".exe", ARRAYSIZE(szFullExe), szFullExe, &pszFileExe);

  if (!doInject) {
    SetDllDirectoryA(szCWD);
  }

#ifdef _DEBUG
  printf(LAUNCHER_NAME ": Starting: `%s', CWD: '%s'", szCommand, szChildCWD);

  if (doInject) {
    printf(", with injected '%s'", szDllInjectionPath);
  }

  printf(".\n");
  fflush(stdout);
#endif

  BOOL result;
  if (doInject) {
    LPCSTR pszDllToInject = szDllInjectionPath;
    result = DetourCreateProcessWithDllsA(szFullExe[0] ? szFullExe : NULL, szCommand,
                                          NULL, NULL, TRUE, dwFlags, NULL, szChildCWD,
                                          &si, &pi, 1, &pszDllToInject, NULL);
  } else {
    result = CreateProcessA(szFullExe[0] ? szFullExe : NULL, szCommand,
                            NULL, NULL, TRUE, dwFlags, NULL, szChildCWD,
                            &si, &pi);
  }

  if (result == FALSE) {
    DWORD dwError = GetLastError();
    printf(LAUNCHER_NAME ": failed: %ld\n", dwError);
    if (doInject) {
      if (dwError == ERROR_INVALID_HANDLE) {
#if DETOURS_64BIT
        printf(LAUNCHER_NAME ": Can't detour a 32-bit target process from a 64-bit parent process.\n");
#else
        printf(LAUNCHER_NAME ": Can't detour a 64-bit target process from a 32-bit parent process.\n");
#endif
      }
    }
    return 9009;
  }

  ResumeThread(pi.hThread);

  WaitForSingleObject(pi.hProcess, INFINITE);

  DWORD dwResult = 0;
  if (!GetExitCodeProcess(pi.hProcess, &dwResult)) {
    printf(LAUNCHER_NAME ": GetExitCodeProcess failed: %ld\n", GetLastError());
    return 9010;
  }

  return dwResult;
}
