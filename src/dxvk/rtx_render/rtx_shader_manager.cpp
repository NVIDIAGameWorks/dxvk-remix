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
#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <stdexcept>
#include <cstdio>

#include "dxvk_device.h"

#include "../util/util_string.h"

#include "rtx_shader_manager.h"
#include "rtx_options.h"

namespace {
  // Note: Relative paths from the source root (which may vary at runtime) to various folders and tools involved in shader compilation.
  // These paths should be kept in sync with the project's structure if it changes.
  const std::filesystem::path g_shaderFolderRelativePath = "src/dxvk/shaders";
  const std::filesystem::path g_rtxShaderFolderRelativePath = "src/dxvk/shaders/rtx";
  const std::filesystem::path g_rtxdiIncludeFolderRelativePath = "submodules/rtxdi/rtxdi-sdk/include";
  const std::filesystem::path g_compileScriptRelativePath = "scripts-common/compile_shaders.py";
  const std::filesystem::path g_glslangRelativePath = "external/glslangValidator/glslangValidator.exe";
  const std::filesystem::path g_slangcRelativePath = "external/slang/slangc.exe";
}

namespace dxvk {
  ShaderManager* ShaderManager::s_instance = nullptr;

  ShaderManager::ShaderManager()
    : // Note: Override the source path defined at build-time with a runtime option.
    m_sourceRootPath{ !RtxOptions::Get()->sourceRootPath().empty() ? RtxOptions::Get()->sourceRootPath()
                                                                   : BUILD_SOURCE_ROOT }
    , m_tempFolderPath{ std::filesystem::temp_directory_path() }
    , m_tempFolder{ m_tempFolderPath.u8string() }
    , m_shaderFolder{ (m_sourceRootPath / g_shaderFolderRelativePath).u8string() }
    , m_rtxShaderFolder{ (m_sourceRootPath / g_rtxShaderFolderRelativePath).u8string() }
    , m_rtxdiIncludeFolder{ (m_sourceRootPath / g_rtxdiIncludeFolderRelativePath).u8string() }
    , m_compileScript{ (m_sourceRootPath / g_compileScriptRelativePath).u8string() }
    , m_glslang{ (m_sourceRootPath / g_glslangRelativePath).u8string() }
    , m_slangc{ (m_sourceRootPath / g_slangcRelativePath).u8string() }
    , m_recompileShadersOnLaunch{ RtxOptions::Get()->recompileShadersOnLaunch() }
    , m_device{ nullptr }
    , m_shaderChangeNotificationObject{ NULL } { }

  ShaderManager* ShaderManager::getInstance() {
    if (s_instance == nullptr) {
      s_instance = new ShaderManager();
    }
    return s_instance;
  }

  void ShaderManager::destroyInstance() {
    delete s_instance;
    s_instance = nullptr;
  }

  // "command" is a UTF-8 string of a command and all the relevant arguments to execute on the system. An exit code is written to if
  // the command is able to execute, and the output of the command is returned as a string.
  std::string executeCommand(const char* command, int& exitCode) {
    // Convert the command to the proper encoding
    // Note: This is done because while we work with UTF-8 up to this point Windows' _popen call only accepts either ASCII
    // or UTF-16 strings. We could use wstring and UTF-16 all throughout the shader manager to avoid this conversion at this
    // point but UTF-8 is nicer and would work better probably if other platforms were to be supported.

    const auto wideCommand = str::tows(command);

    // Execute the command

    std::vector<char> buffer(8 * 1024); // 8KB buffer
    std::string result{};
    FILE* pipe = _wpopen(wideCommand.c_str(), L"r");

    if (!pipe) {
      Logger::err("Shader compilation failed, unable to execute the compilation command.");

      throw std::runtime_error("_wpopen() failed!");
    }

    try {
      while (fgets(buffer.data(), buffer.size(), pipe) != NULL) {
        result += buffer.data();
      }
    } catch (...) {
      Logger::err("Shader compilation failed, exception thrown while reading from pipe.");
      _pclose(pipe);
      throw;
    }
    exitCode = _pclose(pipe);
    return result;
  }

  bool ShaderManager::compileShaders() {
    // Run the compile script
    // Note: python.exe must be on PATH. Additionally, this script should match what is specified in the Meson build files for
    // invoking the shader compilation script for consistency.

    // clang-format off
    const std::string command =
      "python.exe " + m_compileScript +
      " -input " + m_rtxShaderFolder +
      " -output " + m_tempFolder +
      " -include " + m_shaderFolder +
      " -include " + m_rtxdiIncludeFolder +
      " -glslang " + m_glslang +
      " -slangc " + m_slangc +
      " -parallel -binary"
#ifndef NDEBUG
      // Note: -debug flag only present when the build type in Meson starts with "debug", so Debug and DebugOptimized.
      " -debug"
#endif
      ;
    // clang-format on

    int exitCode = 0;

    Logger::info("======================== Compile Shaders =======================");
    Logger::info(command);
    std::string output = executeCommand(command.data(), exitCode); // Replace std::system()
    Logger::info(output);
    Logger::info("================================================================\n\n");

    return exitCode == 0;
  }

  void ShaderManager::checkForShaderChanges() {
    if (m_recompileShadersOnLaunch) {
      static bool isFirstFrame = true;

      // Skip shader reload at the start of a first frame as the render passes haven't initialized their shaders
      if (!isFirstFrame) {
        if (!reloadShaders()) {
          Logger::err("recompileShadersOnLaunch failed!");
        }
        m_recompileShadersOnLaunch = false;
      }
      isFirstFrame = false;
    }

    if (!RtxOptions::Get()->isLiveShaderEditModeEnabled()) {
      if (m_shaderChangeNotificationObject != NULL) {
        FindCloseChangeNotification(m_shaderChangeNotificationObject);
        m_shaderChangeNotificationObject = NULL;
      }

      return;
    }

    if (m_shaderChangeNotificationObject == NULL) {
      m_shaderChangeNotificationObject =
        FindFirstChangeNotificationA(m_shaderFolder.c_str(), TRUE, FILE_NOTIFY_CHANGE_LAST_WRITE);
    }

    if (WaitForSingleObject(m_shaderChangeNotificationObject, 0) == WAIT_OBJECT_0) {
      reloadShaders();
      FindNextChangeNotification(m_shaderChangeNotificationObject);
    }
  }

  bool ShaderManager::reloadShaders() {
    if (!compileShaders()) {
      return false;
    }

    bool allShadersLoaded = true;
    for (auto& [name, info] : m_shaderMap) {
      const std::string shaderSPIRVBinary = (m_tempFolderPath / (std::string{ info.m_name } + ".spv")).u8string();

      bool success = false;
      std::ifstream file(shaderSPIRVBinary, std::ios::binary);
      if (file) {
        SpirvCodeBuffer code(file);
        if (code.size()) {
          info.m_staticCode = code; // Update the code
          Rc<DxvkShader> shader = createShader(info);
          info.m_shader.emplace_back(shader);
          success = true;
        }
      }

      if (!success) {
        Logger::info("Failed to load SPIR-V binary: \"" + shaderSPIRVBinary + "\"");
        allShadersLoaded = false;
      }
    }

    return allShadersLoaded;
  }
} // namespace dxvk
