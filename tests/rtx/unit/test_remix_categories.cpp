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

#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <filesystem>
#include <algorithm>
#include <windows.h>
#include "../../test_utils.h"
#include "../../../src/util/util_filesys.h"

#ifndef BUILD_SOURCE_ROOT
// Fallback for when BUILD_SOURCE_ROOT is not defined (e.g., during static analysis)
#define BUILD_SOURCE_ROOT "./"
#warning "BUILD_SOURCE_ROOT not defined, using current directory"
#endif

namespace {

  // The checked-in RemixCategories USD schema the Remix Toolkit consumes. It is
  // the golden copy; the DLL regenerates it from the InstanceCategories enum.
  const std::string kGoldenCategoriesFile = BUILD_SOURCE_ROOT "src/usd-plugins/RemixCategories/schema.usda";
  const std::string kModifiedCategoriesFile = "remix_categories_schema.usda";

  // Directories the CI view-diff page reads to let a developer promote the
  // regenerated file over the stale golden copy.
  const std::string kWebGoldenDir = "rtx-remix/golden";
  const std::string kWebModifiedDir = "rtx-remix/modified";

  // ComparisonFailureError class for explicit error handling
  class ComparisonFailureError : public dxvk::DxvkError {
  public:
    ComparisonFailureError(std::string&& message)
      : DxvkError(std::move(message)) { }
  };
} // namespace

namespace dxvk {
  // Note: Logger needed by some shared code used in this Unit Test.
  Logger Logger::s_instance("test_remix_categories.log");

  using pfnWriteRemixCategoriesSchemaUsda = bool (*)(const char*);
}

namespace test_remix_categories_app {

  // Function to read all lines from a file into a vector
  std::vector<std::string> readLinesFromFile(const std::string& filePath) {
    std::vector<std::string> lines;
    std::ifstream file(filePath);
    std::string line;

    dxvk::Logger::info("Reading file: " + filePath);

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
  bool compareFiles(const std::string& goldenFilePath, const std::string& modifiedFilePath,
                   const std::string& webGoldenDir = "", const std::string& webModifiedDir = "") {
    bool differenceDetected = false;
    std::vector<std::string> goldenLines = readLinesFromFile(goldenFilePath);
    std::vector<std::string> modifiedLines = readLinesFromFile(modifiedFilePath);

    // Comparing sizes first
    if (goldenLines.size() != modifiedLines.size()) {
      differenceDetected = true;
      dxvk::Logger::err("Files differ in number of lines.");
      dxvk::Logger::err("Golden (" + goldenFilePath + "): " + std::to_string(goldenLines.size()) + " lines");
      dxvk::Logger::err("Modified (" + modifiedFilePath + "): " + std::to_string(modifiedLines.size()) + " lines");
    }

    // Comparing each line
    size_t minLines = std::min(goldenLines.size(), modifiedLines.size());
    for (size_t i = 0; i < minLines; ++i) {
      if (goldenLines[i] != modifiedLines[i]) {
        differenceDetected = true;
        dxvk::Logger::err("Difference found at line " + std::to_string(i + 1) + ":");
        dxvk::Logger::err("Golden:   " + goldenLines[i]);
        dxvk::Logger::err("Modified: " + modifiedLines[i]);
        dxvk::Logger::err("");
      }
    }

    // If files are different and directories are provided, copy files for the
    // CI view-diff page, preserving the folder structure so the diff shows the
    // canonical src/usd-plugins/RemixCategories/schema.usda path.
    if (differenceDetected && !webGoldenDir.empty() && !webModifiedDir.empty()) {
      std::string fileName = std::filesystem::path(goldenFilePath).filename().string();

      std::filesystem::path goldenSourceDir = std::filesystem::path(goldenFilePath).parent_path();
      std::filesystem::path sourceRoot = BUILD_SOURCE_ROOT;
      std::filesystem::path relativePath = std::filesystem::relative(goldenSourceDir, sourceRoot);

      std::string goldenDestPath;
      std::string modifiedDestPath;
      if (relativePath.empty() || relativePath == ".") {
        goldenDestPath = (std::filesystem::path(webGoldenDir) / fileName).string();
        modifiedDestPath = (std::filesystem::path(webModifiedDir) / fileName).string();
      } else {
        goldenDestPath = (std::filesystem::path(webGoldenDir) / relativePath / fileName).string();
        modifiedDestPath = (std::filesystem::path(webModifiedDir) / relativePath / fileName).string();
      }

      try {
        std::filesystem::create_directories(std::filesystem::path(goldenDestPath).parent_path());
        std::filesystem::create_directories(std::filesystem::path(modifiedDestPath).parent_path());

        std::filesystem::copy_file(goldenFilePath, goldenDestPath, std::filesystem::copy_options::overwrite_existing);
        std::filesystem::copy_file(modifiedFilePath, modifiedDestPath, std::filesystem::copy_options::overwrite_existing);
        dxvk::Logger::info("Copied files to golden and modified directories for web interface.");
        dxvk::Logger::info("goldenDestPath: " + goldenDestPath);
        dxvk::Logger::info("modifiedDestPath: " + modifiedDestPath);
      } catch (const std::exception& e) {
        dxvk::Logger::err("Warning: Failed to copy files for web interface: " + std::string(e.what()));
      }
    }

    return !differenceDetected;
  }

  void run_test(const char* d3d9Path) {
    HMODULE hD3D9 = LoadLibraryA(d3d9Path);
    if (hD3D9 == NULL) {
      throw dxvk::DxvkError("Unable to load D3D9");
    }
    char path[MAX_PATH];
    GetModuleFileNameA(hD3D9, path, sizeof(path));
    dxvk::Logger::info("Loaded D3D9 at: " + std::string(path));

    // Load the categories export function generated from the C++ enum
    dxvk::pfnWriteRemixCategoriesSchemaUsda fnWriteRemixCategoriesSchemaUsda =
      (dxvk::pfnWriteRemixCategoriesSchemaUsda) GetProcAddress(hD3D9, "writeRemixCategoriesSchemaUsda");
    if (fnWriteRemixCategoriesSchemaUsda == NULL) {
      throw dxvk::DxvkError("Couldn't load writeRemixCategoriesSchemaUsda function");
    }

    // Create directories for web interface
    std::filesystem::create_directories(kWebGoldenDir);
    std::filesystem::create_directories(kWebModifiedDir);

    // Regenerate the categories schema from the C++ source
    dxvk::Logger::info("Generating remix categories schema to: " + kModifiedCategoriesFile);
    if (!fnWriteRemixCategoriesSchemaUsda(kModifiedCategoriesFile.c_str())) {
      throw dxvk::DxvkError("Failed to write remix categories schema.usda");
    }

    // Compare the regenerated file against the checked-in golden copy
    dxvk::Logger::info("=== Comparing Remix Categories Schema ===");
    if (!compareFiles(kGoldenCategoriesFile, kModifiedCategoriesFile, kWebGoldenDir, kWebModifiedDir)) {
      throw ComparisonFailureError("RemixCategories schema.usda does not match the InstanceCategories enum.");
    }

    dxvk::Logger::info("Remix categories schema matches successfully!");
  }
}

int main(int n, const char* args[]) {
  try {
    if (n < 2) {
      throw dxvk::DxvkError("Expected D3D9 runtime path as argument.");
    }

    test_remix_categories_app::run_test(args[1]);
  }
  catch (const ComparisonFailureError& error) {
    dxvk::Logger::err(error.message());

    dxvk::Logger::err("Please update src/usd-plugins/RemixCategories/schema.usda by doing one of the following:");
    dxvk::Logger::err("  - In CI, promote the change using the web interface linked at the end of the run.");
    dxvk::Logger::err("  - Copy the generated file over the golden copy:");
#ifdef _WIN32
    dxvk::Logger::err("  copy /Y " + kModifiedCategoriesFile + " " + kGoldenCategoriesFile);
#else
    dxvk::Logger::err("  cp " + kModifiedCategoriesFile + " " + kGoldenCategoriesFile);
#endif
    return 1;
  }
  catch (const dxvk::DxvkError& error) {
    dxvk::Logger::err(error.message());
    return 1;
  }

  return 0;
}
