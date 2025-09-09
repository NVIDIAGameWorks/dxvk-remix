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

#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <filesystem>
#include <cstdlib>
#include <cstring>
#include <sstream>
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

  // Shared constants for directory paths
  const std::string kGoldenOgnDir = BUILD_SOURCE_ROOT "src/ogn/lightspeed.trex.components/";
  const std::string kGoldenMdDir = BUILD_SOURCE_ROOT "documentation/components/";
  
  const std::string kModifiedOgnDir = "rtx-remix/schemas/";
  const std::string kModifiedMdDir = "rtx-remix/docs/";
  const std::string kWebBaseDir = "rtx-remix";

  // ComparisonFailureError class for explicit error handling
  class ComparisonFailureError : public dxvk::DxvkError {
  public:
    ComparisonFailureError(std::string&& message) 
      : DxvkError(std::move(message)) { }
  };
} // namespace

namespace dxvk {
  // Note: Logger needed by some shared code used in this Unit Test.
  Logger Logger::s_instance("test_graph_documentation.log");

  using pfnWriteAllOGNSchemas = bool (*)(const char*);
  using pfnWriteAllMarkdownDocs = bool (*)(const char*);
}

namespace test_graph_documentation_app {

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
  bool compareFiles(const std::string& filePath1, const std::string& filePath2, 
                   const std::string& goldenDir = "", const std::string& modifiedDir = "") {
    bool differenceDetected = false;
    std::vector<std::string> file1Lines = readLinesFromFile(filePath1);
    std::vector<std::string> file2Lines = readLinesFromFile(filePath2);

    // Comparing sizes first
    if (file1Lines.size() != file2Lines.size()) {
      differenceDetected = true;
      dxvk::Logger::err("Files differ in number of lines.");
      dxvk::Logger::err("File 1 (" + filePath1 + "): " + std::to_string(file1Lines.size()) + " lines");
      dxvk::Logger::err("File 2 (" + filePath2 + "): " + std::to_string(file2Lines.size()) + " lines");
    }

    // Comparing each line
    size_t minLines = std::min(file1Lines.size(), file2Lines.size());
    for (size_t i = 0; i < minLines; ++i) {
      if (file1Lines[i] != file2Lines[i]) {
        differenceDetected = true;
        dxvk::Logger::err("Difference found at line " + std::to_string(i + 1) + ":");
        dxvk::Logger::err("File 1: " + file1Lines[i]);
        dxvk::Logger::err("File 2: " + file2Lines[i]);
        dxvk::Logger::err("");
      }
    }

    // If files are different and directories are provided, copy files for web interface
    if (differenceDetected && !goldenDir.empty() && !modifiedDir.empty()) {
      std::filesystem::path filePath1Path(filePath1);
      std::filesystem::path filePath2Path(filePath2);
      std::string fileName = filePath1Path.filename().string();
      
      // Calculate relative path from the source root to preserve folder structure
      // For OGN files: from src/ogn/lightspeed.trex.components/ to rtx-remix/golden/src/ogn/lightspeed.trex.components/
      // For MD files: from documentation/components/ to rtx-remix/golden/documentation/components/
      std::filesystem::path goldenSourceDir = std::filesystem::path(filePath1).parent_path();
      std::filesystem::path sourceRoot = BUILD_SOURCE_ROOT; // The repository root
      std::filesystem::path relativePath = std::filesystem::relative(goldenSourceDir, sourceRoot);
      
      // Build destination paths preserving folder structure
      std::string goldenDestPath;
      std::string modifiedDestPath;
      
      if (relativePath.empty() || relativePath == ".") {
        // File is in the root of the source structure
        goldenDestPath = (std::filesystem::path(goldenDir) / fileName).string();
        modifiedDestPath = (std::filesystem::path(modifiedDir) / fileName).string();
      } else {
        // File is in a subdirectory, preserve the structure
        goldenDestPath = (std::filesystem::path(goldenDir) / relativePath / fileName).string();
        modifiedDestPath = (std::filesystem::path(modifiedDir) / relativePath / fileName).string();
      }
      
      try {
        // Create destination directories if they don't exist
        std::filesystem::create_directories(std::filesystem::path(goldenDestPath).parent_path());
        std::filesystem::create_directories(std::filesystem::path(modifiedDestPath).parent_path());
        
        std::filesystem::copy_file(filePath1, goldenDestPath, std::filesystem::copy_options::overwrite_existing);
        std::filesystem::copy_file(filePath2, modifiedDestPath, std::filesystem::copy_options::overwrite_existing);
        dxvk::Logger::info("Copied files to golden and modified directories for web interface.");
        dxvk::Logger::info("goldenDestPath: " + goldenDestPath);
        dxvk::Logger::info("modifiedDestPath: " + modifiedDestPath);
      } catch (const std::exception& e) {
        dxvk::Logger::err("Warning: Failed to copy files for web interface: " + std::string(e.what()));
      }
    }

    return !differenceDetected;
  }



  // Function to compare all files in a directory
  bool compareDirectories(const std::string& goldenDir, const std::string& modifiedDir, 
                         const std::string& webGoldenDir = "", const std::string& webModifiedDir = "") {
    dxvk::Logger::info("Comparing directories:");
    dxvk::Logger::info("Golden: " + goldenDir);
    dxvk::Logger::info("Modified: " + modifiedDir);

    if (!std::filesystem::exists(goldenDir)) {
      throw dxvk::DxvkError("Golden directory does not exist: " + goldenDir);
    }

    if (!std::filesystem::exists(modifiedDir)) {
      throw dxvk::DxvkError("Modified directory does not exist: " + modifiedDir);
    }

    bool allFilesMatch = true;
    std::filesystem::path goldenPath(goldenDir);
    std::filesystem::path modifiedPath(modifiedDir);

    // Iterate through golden directory
    for (const auto& entry : std::filesystem::directory_iterator(goldenPath)) {
      if (entry.is_regular_file()) {
        std::string fileName = entry.path().filename().string();
        std::string goldenFilePath = (goldenPath / fileName).string();
        std::string modifiedFilePath = (modifiedPath / fileName).string();

        dxvk::Logger::info("Comparing file: " + fileName);

        if (!std::filesystem::exists(modifiedFilePath)) {
          dxvk::Logger::err("Modified file does not exist: " + modifiedFilePath);
          allFilesMatch = false;
          continue;
        }

        if (!compareFiles(goldenFilePath, modifiedFilePath, webGoldenDir, webModifiedDir)) {
          dxvk::Logger::err("Files do not match: " + fileName);
          allFilesMatch = false;
        }
      }
    }

    return allFilesMatch;
  }

  void run_test(const char* d3d9Path) {
    HMODULE hD3D9 = LoadLibraryA(d3d9Path);
    if (hD3D9 == NULL) {
      throw dxvk::DxvkError("Unable to load D3D9");
    }
    char path[MAX_PATH];
    GetModuleFileNameA(hD3D9, path, sizeof(path));
    dxvk::Logger::info("Loaded D3D9 at: " + std::string(path));

    // Load OGN schema writer function
    dxvk::pfnWriteAllOGNSchemas fnWriteAllOGNSchemas = (dxvk::pfnWriteAllOGNSchemas) GetProcAddress(hD3D9, "writeAllOGNSchemas");
    if (fnWriteAllOGNSchemas == NULL) {
      throw dxvk::DxvkError("Couldn't load writeAllOGNSchemas function");
    }

    // Load markdown writer function
    dxvk::pfnWriteAllMarkdownDocs fnWriteAllMarkdownDocs = (dxvk::pfnWriteAllMarkdownDocs) GetProcAddress(hD3D9, "writeAllMarkdownDocs");
    if (fnWriteAllMarkdownDocs == NULL) {
      throw dxvk::DxvkError("Couldn't load writeAllMarkdownDocs function");
    }

    // Create output directories
    std::filesystem::create_directories(kModifiedOgnDir);
    std::filesystem::create_directories(kModifiedMdDir);

    // Generate OGN schemas
    dxvk::Logger::info("Generating OGN schemas to: " + kModifiedOgnDir);
    if (!fnWriteAllOGNSchemas(kModifiedOgnDir.c_str())) {
      throw dxvk::DxvkError("Failed to write OGN schemas");
    }

    // Generate markdown documentation
    dxvk::Logger::info("Generating markdown documentation to: " + kModifiedMdDir);
    if (!fnWriteAllMarkdownDocs(kModifiedMdDir.c_str())) {
      throw dxvk::DxvkError("Failed to write markdown documentation");
    }

    // Create directories for web interface
    const std::string webGoldenDir = kWebBaseDir + "/golden";
    const std::string webModifiedDir = kWebBaseDir + "/modified";
    
    std::filesystem::create_directories(webGoldenDir);
    std::filesystem::create_directories(webModifiedDir);

    // Compare OGN schema files
    dxvk::Logger::info("=== Comparing OGN Schema Files ===");
    bool ognFilesMatch = compareDirectories(kGoldenOgnDir, kModifiedOgnDir, webGoldenDir, webModifiedDir);

    // Compare markdown documentation files
    dxvk::Logger::info("=== Comparing Markdown Documentation Files ===");
    bool mdFilesMatch = compareDirectories(kGoldenMdDir, kModifiedMdDir, webGoldenDir, webModifiedDir);

    // Check if any files don't match and throw error after processing all files
    if (!ognFilesMatch || !mdFilesMatch) {
      std::string errorMessage = "File differences detected:";
      if (!ognFilesMatch) errorMessage += " OGN schema files do not match.";
      if (!mdFilesMatch) errorMessage += " Markdown documentation files do not match.";
      throw ComparisonFailureError(std::move(errorMessage));
    }

    dxvk::Logger::info("All files match successfully!");
  }
}

int main(int n, const char* args[]) {
  try {
    if (n < 2) {
      throw dxvk::DxvkError("Expected D3D9 runtime path as argument.");
    }

    test_graph_documentation_app::run_test(args[1]);
  }
  catch (const ComparisonFailureError& error) {
    dxvk::Logger::err(error.message());
    
      dxvk::Logger::err("Please update the schema and documentation files by doing one of the following:");
      dxvk::Logger::err("  - In CI, promote the changes using the web interface linked at the end of the run.");
      dxvk::Logger::err("  - Copy the generated files using these commands:");
#ifdef _WIN32
      dxvk::Logger::err("  xcopy /E /Y " + kModifiedOgnDir + "* " + kGoldenOgnDir);
      dxvk::Logger::err("  xcopy /E /Y " + kModifiedMdDir + "* " + kGoldenMdDir);
#else
      dxvk::Logger::err("  cp -r " + kModifiedOgnDir + "* " + kGoldenOgnDir);
      dxvk::Logger::err("  cp -r " + kModifiedMdDir + "* " + kGoldenMdDir);
#endif
      dxvk::Logger::err("  - Run a Remix application with RTX_GRAPH_WRITE_OGN_SCHEMA=1, and copy the generated files to the source directories.");
    return 1;
  }
  catch (const dxvk::DxvkError& error) {
    dxvk::Logger::err(error.message());
    return 1;
  }

  return 0;
} 