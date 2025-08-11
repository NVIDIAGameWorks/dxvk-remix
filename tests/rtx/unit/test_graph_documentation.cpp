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
#include <windows.h>
#include "../../test_utils.h"

#ifndef BUILD_SOURCE_ROOT
// Fallback for when BUILD_SOURCE_ROOT is not defined (e.g., during static analysis)
#define BUILD_SOURCE_ROOT "./"
#warning "BUILD_SOURCE_ROOT not defined, using current directory"
#endif

namespace {

  // Shared constants for directory paths
  const std::string kGoldenOgnDir = BUILD_SOURCE_ROOT "src/ogn/lightspeed.trex.components/";
  const std::string kGoldenMdDir = BUILD_SOURCE_ROOT "documentation/components/";
  const std::string kWebSourceDir = BUILD_SOURCE_ROOT "tests/rtx/unit/tools/docDiff";
  
  const std::string kModifiedOgnDir = "rtx-remix/schemas/";
  const std::string kModifiedMdDir = "rtx-remix/docs/";
  const std::string kWebOutputDir = "rtx-remix/web-interface";

  // CI URL generation constants
  const std::string kGitlabPagesUrl = "https://lightspeedrtx.gitlab-master-pages.nvidia.com/-/dxvk-remix-nv/-/jobs/";
  const std::string kRtxTestPath = "tests/rtx/unit/";

  // ComparisonFailureError class for explicit error handling
  class ComparisonFailureError : public dxvk::DxvkError {
  public:
    ComparisonFailureError(std::string&& message) 
      : DxvkError(std::move(message)) { }
  };

  // CI detection and URL generation
  class CI {
  public:
    static bool isCiRun() {
      // Check multiple CI environment variables to be more robust
      return std::getenv("CI") != nullptr || 
             std::getenv("GITLAB_CI") != nullptr || 
             std::getenv("CI_JOB_ID") != nullptr;
    }

    static std::string getJobId() {
      const char* jobId = std::getenv("CI_JOB_ID");
      return jobId ? std::string(jobId) : "";
    }

    static std::string getBranchName() {
      const char* branchName = std::getenv("CI_COMMIT_REF_NAME");
      return branchName ? std::string(branchName) : "";
    }

    static std::string getProjectId() {
      const char* projectId = std::getenv("CI_PROJECT_ID");
      return projectId ? std::string(projectId) : "";
    }

    static std::string localPathToArtifactUri(const std::string& localPathStr) {
      if (!isCiRun()) {
        throw dxvk::DxvkError("ERROR: localPathToArtifactUri should only be called in CI environment");
      }
      std::filesystem::path absPath = std::filesystem::absolute(localPathStr);
      std::string absPathStr = absPath.string();
      
      // Replace backslashes with forward slashes for consistency
      size_t pos = 0;
      while ((pos = absPathStr.find('\\', pos)) != std::string::npos) {
        absPathStr.replace(pos, 1, "/");
        pos += 1;
      }
      
      // Look for build directory patterns like _Comp64UnitTest, _Comp64Release, etc.
      size_t buildDirPos = absPathStr.find("_Comp64UnitTest");
      
      if (buildDirPos == std::string::npos) {
        throw dxvk::DxvkError("ERROR: Expected unit test build directory '_Comp64UnitTest' not found in path: " + absPathStr);
      }

      // Found a build directory, use the path from there
      std::string relativePath = absPathStr.substr(buildDirPos);
      std::string jobId = getJobId();
      if (jobId.empty()) {
        throw dxvk::DxvkError("CI ERROR: Missing required environment variable CI_JOB_ID");
      }
      return kGitlabPagesUrl + jobId + "/artifacts/" + relativePath;
    }

    static std::string resolvePathToPrint(const std::string& pathStr) {
      std::string result = pathStr;
      size_t pos = 0;
      while ((pos = result.find('/', pos)) != std::string::npos) {
        result.replace(pos, 1, "\\");
        pos += 1;
      }
      return result;
    }
  };

}
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

    dxvk::Logger::info("Reading file: " + CI::resolvePathToPrint(filePath));

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
      dxvk::Logger::err("Files differ in number of lines.");
      dxvk::Logger::err("File 1 (" + CI::resolvePathToPrint(filePath1) + "): " + std::to_string(file1Lines.size()) + " lines");
      dxvk::Logger::err("File 2 (" + CI::resolvePathToPrint(filePath2) + "): " + std::to_string(file2Lines.size()) + " lines");
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

    return !differenceDetected;
  }

  // Function to escape JSON strings
  std::string escapeJsonString(const std::string& input) {
    std::string output;
    output.reserve(input.length());
    for (char c : input) {
      switch (c) {
        case '"': output += "\\\""; break;
        case '\\': output += "\\\\"; break;
        case '\b': output += "\\b"; break;
        case '\f': output += "\\f"; break;
        case '\n': output += "\\n"; break;
        case '\r': output += "\\r"; break;
        case '\t': output += "\\t"; break;
        default: output += c; break;
      }
    }
    return output;
  }

  // Function to copy a file from source to destination
  void copyFile(const std::string& sourcePath, const std::string& destPath) {
    std::ifstream source(sourcePath, std::ios::binary);
    if (!source.is_open()) {
      throw dxvk::DxvkError("Could not open source file: " + sourcePath);
    }
    
    // Create destination directory if it doesn't exist
    std::filesystem::path destDir = std::filesystem::path(destPath).parent_path();
    std::filesystem::create_directories(destDir);
    
    std::ofstream dest(destPath, std::ios::binary);
    if (!dest.is_open()) {
      throw dxvk::DxvkError("Could not create destination file: " + destPath);
    }
    
    dest << source.rdbuf();
    source.close();
    dest.close();
  }

  // Function to copy all files from a directory recursively
  void copyDirectory(const std::string& sourceDir, const std::string& destDir) {
    if (!std::filesystem::exists(sourceDir)) {
      throw dxvk::DxvkError("Source directory does not exist: " + sourceDir);
    }
    
    std::filesystem::create_directories(destDir);
    
    for (const auto& entry : std::filesystem::recursive_directory_iterator(sourceDir)) {
      if (entry.is_regular_file()) {
        std::filesystem::path relativePath = std::filesystem::relative(entry.path(), sourceDir);
        std::filesystem::path destPath = std::filesystem::path(destDir) / relativePath;
        
        // Create destination directory if it doesn't exist
        std::filesystem::create_directories(destPath.parent_path());
        
        copyFile(entry.path().string(), destPath.string());
      }
    }
  }

  // Function to process files of a specific type (OGN or Markdown)
  void processFileType(const std::string& goldenDir, const std::string& modifiedDir, 
                      const std::string& outputDir, const std::string& fileType,
                      const std::string& goldenGitPath, const std::string& fileExtension,
                      std::vector<std::string>& fileList) {
    if (!std::filesystem::exists(goldenDir)) {
      return;
    }

    for (const auto& entry : std::filesystem::directory_iterator(goldenDir)) {
      if (!entry.is_regular_file()) {
        continue;
      }

      // Check file extension if specified
      if (!fileExtension.empty() && entry.path().extension() != fileExtension) {
        continue;
      }

      std::string fileName = entry.path().filename().string();
      std::string goldenPath = (std::filesystem::path(goldenDir) / fileName).string();
      std::string modifiedPath = (std::filesystem::path(modifiedDir) / fileName).string();
      std::string copiedGoldenPath = outputDir + "/golden/" + fileType + "/" + fileName;
      std::string copiedModifiedPath = outputDir + "/modified/" + fileType + "/" + fileName;
      
      // Copy golden file
      copyFile(goldenPath, copiedGoldenPath);
      
      // Copy modified file if it exists
      if (std::filesystem::exists(modifiedPath)) {
        copyFile(modifiedPath, copiedModifiedPath);
      }
      
      bool different = false;
      if (std::filesystem::exists(modifiedPath)) {
        different = !compareFiles(goldenPath, modifiedPath);
      }
      
      std::string goldenGitPathFull = goldenGitPath + fileName;
      
      // Generate URLs for web interface (CI only)
      std::string goldenUrl = CI::localPathToArtifactUri(copiedGoldenPath);
      std::string modifiedUrl = CI::localPathToArtifactUri(copiedModifiedPath);
      
      // Build JSON object using string builder
      std::ostringstream jsonBuilder;
      jsonBuilder << "    {\n"
                 << "      \"name\": \"" << escapeJsonString(fileName) << "\",\n"
                 << "      \"type\": \"" << fileType << "\",\n"
                 << "      \"different\": " << (different ? "true" : "false") << ",\n"
                 << "      \"goldenPath\": \"" << escapeJsonString(goldenUrl) << "\",\n"
                 << "      \"modifiedPath\": \"" << escapeJsonString(modifiedUrl) << "\",\n"
                 << "      \"goldenGitPath\": \"" << escapeJsonString(goldenGitPathFull) << "\"\n"
                 << "    }";
      
      fileList.push_back(jsonBuilder.str());
    }
  }

  // Function to generate HTML file with embedded JSON data for web interface
  void generateWebInterface(const std::string& goldenOgnDir, const std::string& modifiedOgnDir,
                           const std::string& goldenMdDir, const std::string& modifiedMdDir,
                           const std::string& outputDir) {
    std::vector<std::string> fileList;
    
    // Create output directories
    std::filesystem::create_directories(outputDir);
    std::filesystem::create_directories(outputDir + "/golden/ogn");
    std::filesystem::create_directories(outputDir + "/golden/md");
    std::filesystem::create_directories(outputDir + "/modified/ogn");
    std::filesystem::create_directories(outputDir + "/modified/md");
    std::filesystem::create_directories(outputDir + "/assets/css");
    std::filesystem::create_directories(outputDir + "/assets/js");
    
    // Process OGN files (all files, no extension filter)
    processFileType(goldenOgnDir, modifiedOgnDir, outputDir, "ogn", 
                   "src/ogn/lightspeed.trex.components/", "", fileList);
    
    // Process Markdown files (only .md files)
    processFileType(goldenMdDir, modifiedMdDir, outputDir, "markdown", 
                   "documentation/components/", ".md", fileList);
    
    // Build JSON string
    std::string jsonData = "[\n";
    for (size_t i = 0; i < fileList.size(); ++i) {
      jsonData += fileList[i];
      // Add comma only if not the last item
      if (i < fileList.size() - 1) {
        jsonData += ",";
      }
      jsonData += "\n";
    }
    jsonData += "]";
    
    // Read the original HTML template
    std::string htmlTemplatePath = kWebSourceDir + "/index.html";
    std::ifstream htmlTemplate(htmlTemplatePath);
    if (!htmlTemplate.is_open()) {
      throw dxvk::DxvkError("Could not open HTML template: " + htmlTemplatePath);
    }
    
    std::string htmlContent((std::istreambuf_iterator<char>(htmlTemplate)),
                           std::istreambuf_iterator<char>());
    htmlTemplate.close();
    
    // Replace the placeholder with embedded data
    std::string placeholder = "// EMBEDDED_DATA_PLACEHOLDER - This will be replaced by the C++ code";
    std::string embeddedData = "const embeddedFileData = " + jsonData + ";";
    
    // Add environment variables if available
    const char* branchName = std::getenv("CI_COMMIT_REF_NAME");
    const char* token = std::getenv("IMAGE_DIFF_TOKEN");
    const char* sourceProjectId = std::getenv("CI_MERGE_REQUEST_SOURCE_PROJECT_ID"); // Source project ID (developer fork)
    const char* ciServerHost = std::getenv("CI_SERVER_HOST");
    
    if (branchName) {
        embeddedData += "\n        branchName = \"" + std::string(branchName) + "\";";
    }
    if (token) {
        embeddedData += "\n        token = \"" + std::string(token) + "\";";
    }
    if (sourceProjectId) {
        embeddedData += "\n        sourceProjectId = \"" + std::string(sourceProjectId) + "\";";
    }
    if (ciServerHost) {
        embeddedData += "\n        ciServerHost = \"https://" + std::string(ciServerHost) + "\";";
    }
    
    size_t pos = htmlContent.find(placeholder);
    if (pos != std::string::npos) {
      htmlContent.replace(pos, placeholder.length(), embeddedData);
    }
    
    // Write the generated HTML file
    std::string htmlPath = outputDir + "/index.html";
    std::ofstream htmlFile(htmlPath);
    htmlFile << htmlContent;
    htmlFile.close();
  }

  // Function to compare all files in a directory
  bool compareDirectories(const std::string& goldenDir, const std::string& modifiedDir) {
    dxvk::Logger::info("Comparing directories:");
    dxvk::Logger::info("Golden: " + CI::resolvePathToPrint(goldenDir));
    dxvk::Logger::info("Modified: " + CI::resolvePathToPrint(modifiedDir));

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
          dxvk::Logger::err("Modified file does not exist: " + CI::resolvePathToPrint(modifiedFilePath));
          allFilesMatch = false;
          continue;
        }

        if (!compareFiles(goldenFilePath, modifiedFilePath)) {
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
    dxvk::Logger::info("Loaded D3D9 at: " + CI::resolvePathToPrint(path));

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
    dxvk::Logger::info("Generating OGN schemas to: " + CI::resolvePathToPrint(kModifiedOgnDir));
    if (!fnWriteAllOGNSchemas(kModifiedOgnDir.c_str())) {
      throw dxvk::DxvkError("Failed to write OGN schemas");
    }

    // Generate markdown documentation
    dxvk::Logger::info("Generating markdown documentation to: " + CI::resolvePathToPrint(kModifiedMdDir));
    if (!fnWriteAllMarkdownDocs(kModifiedMdDir.c_str())) {
      throw dxvk::DxvkError("Failed to write markdown documentation");
    }

    // Compare OGN schema files
    dxvk::Logger::info("=== Comparing OGN Schema Files ===");
    if (!compareDirectories(kGoldenOgnDir, kModifiedOgnDir)) {
      throw ComparisonFailureError("OGN schema files do not match.");
    }

    // Compare markdown documentation files
    dxvk::Logger::info("=== Comparing Markdown Documentation Files ===");
    if (!compareDirectories(kGoldenMdDir, kModifiedMdDir)) {
      throw ComparisonFailureError("Markdown documentation files do not match.");
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
    
    // Generate web interface for file comparison errors (CI only)
    if (CI::isCiRun()) {
      try {
        dxvk::Logger::info("=== Generating Web Interface for Review ===");
        
        // Use shared constants for directory paths
        const std::string& goldenOgnDir = kGoldenOgnDir;
        const std::string& modifiedOgnDir = kModifiedOgnDir;
        const std::string& goldenMdDir = kGoldenMdDir;
        const std::string& modifiedMdDir = kModifiedMdDir;
        
        // Create output directory for generated files
        std::filesystem::create_directories(kWebOutputDir);
        
        // Generate web interface with embedded JSON data
        test_graph_documentation_app::generateWebInterface(goldenOgnDir, modifiedOgnDir, goldenMdDir, modifiedMdDir, kWebOutputDir);
        
        // Print web interface URL referencing the generated HTML file
        std::string webInterfacePath = std::filesystem::absolute(kWebOutputDir + "/index.html").string();
        
        std::string ciUrl = CI::localPathToArtifactUri(webInterfacePath);
        dxvk::Logger::err("=== Diff View ===");
        dxvk::Logger::err(ciUrl);
      }
      catch (const std::exception& webError) {
        dxvk::Logger::err("Warning: Failed to generate web interface: " + std::string(webError.what()));
      }
    }
    
    if (CI::isCiRun()) {
      dxvk::Logger::err("Please update the schema and documentation files by doing one of the following:");
      dxvk::Logger::err("  - Download the artifacts from the unit_testing job in CI, and copy the generated files to the repo.");
      dxvk::Logger::err("  - Use the web interface URL above to review and promote the changes.");
    } else {
      dxvk::Logger::err("Please update the schema and documentation files by doing one of the following:");
      dxvk::Logger::err("  - Copy the generated files from " + kModifiedOgnDir + " to " + kGoldenOgnDir);
      dxvk::Logger::err("  - Copy the generated files from " + kModifiedMdDir + " to " + kGoldenMdDir);
      dxvk::Logger::err("  - Run a Remix application with RTX_GRAPH_WRITE_OGN_SCHEMA=1, and copy the generated files to the source directories.");
      dxvk::Logger::err("");
      dxvk::Logger::err("Or use these copy commands:");
#ifdef _WIN32
      dxvk::Logger::err("  xcopy /E /Y " + kModifiedOgnDir + "* " + kGoldenOgnDir);
      dxvk::Logger::err("  xcopy /E /Y " + kModifiedMdDir + "* " + kGoldenMdDir);
#else
      dxvk::Logger::err("  cp -r " + kModifiedOgnDir + "* " + kGoldenOgnDir);
      dxvk::Logger::err("  cp -r " + kModifiedMdDir + "* " + kGoldenMdDir);
#endif
    }
    return 1;
  }
  catch (const dxvk::DxvkError& error) {
    dxvk::Logger::err(error.message());
    return 1;
  }

  return 0;
} 