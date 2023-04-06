#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <stdexcept>
#include <stdio.h>

#include "dxvk_device.h"
#include "dxvk_shader_manager.h"

#include "rtx_render/rtx_options.h"

namespace dxvk {
  ShaderManager* ShaderManager::s_instance = nullptr;

  void ShaderManager::formatPath(std::string& path) {
    std::replace(path.begin(), path.end(), '\\', '/');
  }

  ShaderManager::ShaderManager() {
    std::string currentFilePath(__FILE__);
#if !BUILD_NINJA
    std::string dxvkFolder("src\\dxvk\\");
    int offset = currentFilePath.find(dxvkFolder);
    m_sourceRoot = currentFilePath.substr(0, offset);
#else
    m_sourceRoot = BUILD_NINJA_SOURCE_ROOT;
#endif
    if (RtxOptions::Get()->sourceRootPath() != "")
      m_sourceRoot = RtxOptions::Get()->sourceRootPath();

    m_shaderFolder = m_sourceRoot + "src/dxvk/shaders/";
    m_tempFolder = std::filesystem::temp_directory_path().u8string();
    formatPath(m_shaderFolder);
    formatPath(m_tempFolder);

    m_recompileShadersOnLaunch = RtxOptions::Get()->recompileShadersOnLaunch();
  }

  ShaderManager::~ShaderManager() {}

  ShaderManager* ShaderManager::getInstance() {
    if (s_instance == nullptr) {
      s_instance = new ShaderManager();
    }
    return s_instance;
  }

  std::string executeCommand(const char* cmd, int& exitCode) {
    std::vector<char> buffer(8 * 1024); // 8KB buffer
    std::string result = "";
    FILE* pipe = _popen(cmd, "r");
    if (!pipe)
      throw std::runtime_error("popen() failed!");

    try {
      while (fgets(buffer.data(), buffer.size(), pipe) != NULL) {
        result += buffer.data();
      }
    }
    catch (...) {
      Logger::err("Error occurs when invoking shader compiler.");
      _pclose(pipe);
      throw;
    }
    exitCode = _pclose(pipe);
    return result;
  }
  
  bool ShaderManager::compileShaders()
  {
    std::string compileScript = m_sourceRoot + "scripts-common/compile_shaders.py";
    std::string glslang = m_sourceRoot + "bin/glslangValidator.exe";
    std::string slangc = m_sourceRoot + "bin/slangc.exe";

    // Run the compile script - NOTE: python.exe must be on PATH

    std::string command = "python.exe " + compileScript +
      " -input " + m_shaderFolder + "/rtx" +
      " -output " + m_tempFolder +
      " -include " + m_shaderFolder +
      " -include " + m_sourceRoot + "external/rtxdi/rtxdi-sdk/include/" +
      " -glslang " + glslang +
      " -slangc " + slangc +
      " -parallel -binary";

    int exitCode = 0;

    Logger::info("======================== Compile shaders =======================");
    Logger::info(command);
    std::string output = executeCommand(command.data(), exitCode); // Replace std::system()
    Logger::info(output);
    Logger::info("================================================================\n\n");
    
    return exitCode == 0;
  }
  
  void ShaderManager::checkForShaderChanges()
  {
    if (m_recompileShadersOnLaunch) {
      static bool isFirstFrame = true;       

      // Skip shader reload at the start of a first frame as the render passes haven't initialized their shaders
      if (!isFirstFrame) {
        reloadShaders();
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
      m_shaderChangeNotificationObject = FindFirstChangeNotificationA(m_shaderFolder.c_str(), TRUE, FILE_NOTIFY_CHANGE_LAST_WRITE);
    }

    if (WaitForSingleObject(m_shaderChangeNotificationObject, 0) == WAIT_OBJECT_0) {
      reloadShaders();
      FindNextChangeNotification(m_shaderChangeNotificationObject);
    }
  }

  void ShaderManager::reloadShaders()
  {
    if (!compileShaders())
      return;

    for (auto& [name, info] : m_shaderMap) {
      std::string binaryFileName = m_tempFolder + "/" + info.m_name + ".spv";

      bool success = false;
      std::ifstream file(binaryFileName, std::ios::binary);
      if (file) {
        SpirvCodeBuffer code(file);
        if (code.size()) {
          info.m_staticCode = code; // Update the code
          Rc<DxvkShader> shader = createShader(info);
          info.m_shader.push_back(shader);
          success = true;
        }
      }

      if (!success) {
        Logger::info("Failed to load " + binaryFileName);
      }
    }
  }
}
