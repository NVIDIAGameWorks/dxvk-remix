/*
* Copyright (c) 2025, NVIDIA CORPORATION. All rights reserved.
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
#include "rtx_system_info.h"

#include "dxvk_scoped_annotation.h"
#include "../../util/log/log.h"
#include "../../util/util_string.h"

#include <cassert>
#include <optional>
#include <array>
#include <string_view>

#include <windows.h>
#include <intrin.h>

namespace dxvk {

  namespace {

    extern "C" typedef LONG (WINAPI *RtlGetVersionProc)(PRTL_OSVERSIONINFOW);
    extern "C" typedef const char* (CDECL *wine_get_version_proc)(void);

    // Computes the length of a null-terminated string (minus the null terminator) up to a maximum
    // length. Useful when a buffer containing a potentially null terminated string has a known maximum
    // size but may omit the null terminator at its maximum size.
    unsigned int stringLengthOrMax(const char* data, unsigned int max) {
      for (unsigned int i = 0; i < max; ++i) {
        if (data[i] == '\0') {
          return i;
        }
      }

      return max;
    }

  }

  void RtxSystemInfo::logReport() {
    ScopedCpuProfileZoneN("System Info Log Report");

    // = Get CPU Information =

    char brandString[128] = "Unknown CPU";
    char manufacturerID[128] = "Unknown Vendor";

    HKEY hKey;
    if (ERROR_SUCCESS == RegOpenKeyExA(HKEY_LOCAL_MACHINE, "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", 0, KEY_READ, &hKey)) {
      DWORD type;
      DWORD size = sizeof(brandString);
      RegQueryValueExA(hKey, "ProcessorNameString", nullptr, &type, reinterpret_cast<PBYTE>(brandString), &size);
      size = sizeof(manufacturerID);
      RegQueryValueExA(hKey, "VendorIdentifier", nullptr, &type, reinterpret_cast<PBYTE>(manufacturerID), &size);
      RegCloseKey(hKey);
    }

    // = Get Memory Information =

    // Note: Only should be read from if hasMemoryInformation is not false.
    MEMORYSTATUSEX memoryStatus;
    bool hasMemoryInformation = false;

    ZeroMemory(&memoryStatus, sizeof(memoryStatus));
    memoryStatus.dwLength = sizeof(memoryStatus);

    const auto globalMemoryStatusExResult = GlobalMemoryStatusEx(&memoryStatus);

    if (globalMemoryStatusExResult == FALSE) {
      Logger::warn(str::format("Unable to get global memory information. Error code: ", GetLastError()));
    } else {
      hasMemoryInformation = true;
    }

    // = Get OS Information =

    // Note: Only should be read from if hasOsInformation is not false.
    RTL_OSVERSIONINFOW osVersionInfo;
    bool hasOsInformation = false;
    // Note: Only should be read from if hasWineInformation is not false.
    const char* wineVersion;
    bool hasWineInformation = false;

    const auto ntdllModule = GetModuleHandleA("ntdll.dll");

    if (ntdllModule != NULL) {
      // Get Windows Version Information

      const auto rtlGetVersion = reinterpret_cast<RtlGetVersionProc>(GetProcAddress(ntdllModule, "RtlGetVersion"));

      if (rtlGetVersion != NULL) {
        ZeroMemory(&osVersionInfo, sizeof(osVersionInfo));
        osVersionInfo.dwOSVersionInfoSize = sizeof(RTL_OSVERSIONINFOW);

        // Note: Using RtlGetVersion here instead of GetVersionEx because the latter is deprecated and only gets information
        // about the current process which often does not match the system's version information if the application is not
        // manifested for the current Windows version. See this for more information:
        // https://learn.microsoft.com/en-us/windows/win32/sysinfo/targeting-your-application-at-windows-8-1
        const auto rtlGetVersionResult = rtlGetVersion(&osVersionInfo);

        // Note: 0 is STATUS_SUCCESS in this case.
        if (rtlGetVersionResult != 0) {
          Logger::warn(str::format("Unable to get Windows version information. Error code: ", GetLastError()));
        } else {
          hasOsInformation = true;
        }
      } else {
        Logger::warn(str::format("Unable to get RtlGetVersion procedure address. Error code: ", GetLastError()));
      }

      // Detect if Wine is present

      const auto wine_get_version = reinterpret_cast<wine_get_version_proc>(GetProcAddress(ntdllModule, "wine_get_version"));

      if (wine_get_version != NULL) {
        // Note: The lifetime of the string returned by this function is unclear as this function is not documented. In Wine's source
        // however it is just a pointer to a static buffer so it should have a long enough lifetime to still be valid by the time it
        // is printed, plus an API that doesn't expect a buffer to be passed in or for the user to free a dynamically allocated string
        // the implication is that a returned string should have a long lifetime anyways.
        wineVersion = wine_get_version();

        assert(wineVersion != nullptr);

        hasWineInformation = true;
      }
    } else {
      Logger::warn(str::format("Unable to get ntdll.dll module handle. Error code: ", GetLastError()));
    }

    // = Log System Information Report =
    // Note: This report should contain information about relevant hardware on the system which may be useful for debugging when a log is provided. No identifiable information
    // (e.g. serial numbers, usernames, computer names, etc) should be included here to preserve privacy.

    Logger::info(str::format(
      "System Information Report:"
      "\n  CPU: (", manufacturerID, ") ", brandString
    ));

    if (hasMemoryInformation) {
      const auto usedPhysicalMemory = memoryStatus.ullTotalPhys >= memoryStatus.ullAvailPhys ? memoryStatus.ullTotalPhys - memoryStatus.ullAvailPhys : 0;
      const auto usedCommittedMemory = memoryStatus.ullTotalPageFile >= memoryStatus.ullAvailPageFile ? memoryStatus.ullTotalPageFile - memoryStatus.ullAvailPageFile : 0;
      const auto usedVirtualMemory = memoryStatus.ullTotalVirtual >= memoryStatus.ullAvailVirtual ? memoryStatus.ullTotalVirtual - memoryStatus.ullAvailVirtual : 0;

      Logger::info(str::format(
        "  Memory: ",
        str::formatBytes(usedPhysicalMemory), " / ", str::formatBytes(memoryStatus.ullTotalPhys), " physical, ",
        str::formatBytes(usedCommittedMemory), " / ", str::formatBytes(memoryStatus.ullTotalPageFile), " committed, ",
        str::formatBytes(usedVirtualMemory), " / ", str::formatBytes(memoryStatus.ullTotalVirtual), " virtual (current process)"
      ));
    }

    if (hasOsInformation) {
      auto osString = str::format("  OS: Windows ", osVersionInfo.dwMajorVersion, ".", osVersionInfo.dwMinorVersion, " Build ", osVersionInfo.dwBuildNumber);

      if (hasWineInformation) {
        osString += str::format(" (On Wine ", wineVersion, ")");
      }

      Logger::info(osString);
    }
  }

}
