/*
* Copyright (c) 2026, NVIDIA CORPORATION. All rights reserved.
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

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>
#include <set>
#include <windows.h>

#include "../../test_utils.h"

// Matches the arch DLL suffix used by the shim's own runtime resolution logic.
static constexpr wchar_t* kX64Dll = L"_x64.dll";

namespace test_remix_shim_app {

  // Maps the DLL as a data file instead of executing it: no DllMain, no import resolution.
  // LOAD_LIBRARY_AS_IMAGE_RESOURCE keeps the section layout image-relative, so RVAs in the
  // export directory can still be resolved by simple pointer arithmetic off the module base.
  HMODULE loadDll(const char* path) {
    HMODULE hModule = LoadLibraryExA(path, NULL, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
    if (hModule == NULL) {
      throw dxvk::DxvkError(std::string("Unable to load DLL: ") + path);
    }
    return hModule;
  }

  // Walks the PE export directory of a loaded module and returns the exported symbol names.
  std::set<std::string> getExportNames(HMODULE hModule) {
    // The low bits of the handle encode the LOAD_LIBRARY_AS_DATAFILE/AS_IMAGE_RESOURCE flags
    // and must be masked off to get the actual mapped base address.
    auto base = reinterpret_cast<const BYTE*>(
      reinterpret_cast<uintptr_t>(hModule) & ~static_cast<uintptr_t>(3));
    auto dosHeader = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    auto ntHeaders = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dosHeader->e_lfanew);

    const IMAGE_DATA_DIRECTORY& exportDirEntry =
      ntHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    if (exportDirEntry.VirtualAddress == 0) {
      return {};
    }

    auto exportDir = reinterpret_cast<const IMAGE_EXPORT_DIRECTORY*>(base + exportDirEntry.VirtualAddress);
    auto nameRvas = reinterpret_cast<const DWORD*>(base + exportDir->AddressOfNames);

    std::set<std::string> names;
    for (DWORD i = 0; i < exportDir->NumberOfNames; i++) {
      names.emplace(reinterpret_cast<const char*>(base + nameRvas[i]));
    }
    return names;
  }

  void run_test(const char* shimDllPath) {
    const std::string implDllPath =
      std::filesystem::path(shimDllPath).replace_extension().concat(kX64Dll).string();

    HMODULE hShimDll = loadDll(shimDllPath);
    HMODULE hImplDll = loadDll(implDllPath.c_str());

    auto shimExports = getExportNames(hShimDll);
    auto implExports = getExportNames(hImplDll);

    std::vector<std::string> missingFromShim;
    std::set_difference(implExports.begin(), implExports.end(), shimExports.begin(), shimExports.end(),
      std::back_inserter(missingFromShim));

    std::vector<std::string> missingFromImpl;
    std::set_difference(shimExports.begin(), shimExports.end(), implExports.begin(), implExports.end(),
      std::back_inserter(missingFromImpl));

    for (const std::string& name : missingFromShim) {
      std::cout << "Export present in implementation DLL but missing from shim: " << name << std::endl;
    }
    for (const std::string& name : missingFromImpl) {
      std::cout << "Export present in shim but missing from implementation DLL: " << name << std::endl;
    }

    if (!missingFromShim.empty() || !missingFromImpl.empty()) {
      throw dxvk::DxvkError("Export name mismatch between shim and implementation DLLs!");
    }
  }

}

int main(int n, const char* args[]) {
  try {
    if (n < 2) {
      throw dxvk::DxvkError("Expected shim d3d9.dll path as argument. The d3d9_x64.dll implementation "
                            "must be located in the same folder.");
    }

    test_remix_shim_app::run_test(args[1]);
  }
  catch (const dxvk::DxvkError& error) {
    std::cerr << error.message() << std::endl;
    throw;
  }

  return 0;
}
