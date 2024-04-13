/*
* Copyright (c) 2023, NVIDIA CORPORATION. All rights reserved.
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

#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include "../../test_utils.h"

namespace dxvk {
  // Note: Logger needed by some shared code used in this Unit Test.
  Logger Logger::s_instance("test_documentation.log");

  using pfnWriteMarkdownDocumentation = bool (*)(const char*);
}

namespace test_documentation_app {
  // Function to read all lines from a file into a vector
  std::vector<std::string> readLinesFromFile(const std::string& filePath) {
    std::vector<std::string> lines;
    std::ifstream file(filePath);
    std::string line;

    std::cout << "Reading file: " << filePath << std::endl;

    if (!file.is_open()) {
      throw dxvk::DxvkError("Could not open file: " + filePath);
    }

    while (getline(file, line)) {
      lines.push_back(line);
    }

    file.close();
    return lines;
  }

  // Function to compare the files and print differences
  // Returns true if files are the same, false if difference detected
  bool compareFiles(const std::string& filePath1, const std::string& filePath2) {
    bool differenceDetected = false;
    std::vector<std::string> file1Lines = readLinesFromFile(filePath1);
    std::vector<std::string> file2Lines = readLinesFromFile(filePath2);

    // Comparing sizes first
    if (file1Lines.size() != file2Lines.size()) {
      differenceDetected = true;
      std::cout << "Files differ in number of lines." << std::endl;
    }

    // Comparing each line
    size_t minLines = std::min(file1Lines.size(), file2Lines.size());
    for (size_t i = 0; i < minLines; i++) {
      if (file1Lines[i] != file2Lines[i]) {
        differenceDetected = true;
        std::cout << "Difference found at line " << (i + 1) << ":" << std::endl;
        std::cout << "File 1: " << file1Lines[i] << std::endl;
        std::cout << "File 2: " << file2Lines[i] << std::endl;
        std::cout << std::endl;
      }
    }

    return !differenceDetected;
  }

  void run_test(const char* d3d9Path) {
    const std::string& srcRtxOptionsMarkdownPath = BUILD_SOURCE_ROOT "RtxOptions.md";
    const std::string& dstRtxOptionsMarkdownPath = "RtxOptions.md";

    HMODULE hD3D9 = LoadLibrary(d3d9Path);
    if (hD3D9 == NULL) {
      throw dxvk::DxvkError("Unable to load D3D9");
    }
    char path[MAX_PATH];
    GetModuleFileName(hD3D9, path, sizeof(path));
    std::cout << "Loaded D3D9 at: " << path << std::endl;

    dxvk::pfnWriteMarkdownDocumentation fnWriteMarkdownDocumentation = (dxvk::pfnWriteMarkdownDocumentation) GetProcAddress(hD3D9, "writeMarkdownDocumentation");
    if (fnWriteMarkdownDocumentation == NULL) {
      throw dxvk::DxvkError("Couldn't load markdown func");
    }
    std::cout << "Writing documentation to: " << dstRtxOptionsMarkdownPath << std::endl;
    fnWriteMarkdownDocumentation(dstRtxOptionsMarkdownPath.c_str());

    if (!compareFiles(srcRtxOptionsMarkdownPath, dstRtxOptionsMarkdownPath)) {
      throw dxvk::DxvkError("File difference detected.");
    }
  }
}

int main(int n, const char* args[]) {
  try {
    if (n < 2) {
      throw dxvk::DxvkError("Expected D3D9 runtime path as argument.");
    }

    test_documentation_app::run_test(args[1]);
  }
  catch (const dxvk::DxvkError& error) {
    std::cerr << error.message() << std::endl;
    std::cerr << "Please update the RtxOptions.md file by doing one of the following:\n"
                 "\t- Download the artifacts from the unit_testing job in CI, and copy the RtxOptions.md to the repo root locally.\n" 
                 "\t- Run this test application from your local system (e.g. from _Comp64Release, run 'meson test test_documentation') and copy the resulting RtxOptions.md file from _Comp64Release to the repo root.\n" 
                 "\t- Running a Remix application with the following environment variable set (DXVK_DOCUMENTATION_WRITE_RTX_OPTIONS_MD=1), and copying the RtxOptions.md file from the application root to the source root of dxvk-remix." 
      << std::endl;
    throw;
  }

  return 0;
}