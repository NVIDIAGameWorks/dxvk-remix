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

#include "dxvk_device.h"

#include "../util/util_string.h"

#include "rtx_shader_manager.h"
#include "rtx_options.h"

namespace dxvk {
  ShaderManager* ShaderManager::s_instance = nullptr;

  ShaderManager::ShaderManager() :
#ifdef REMIX_DEVELOPMENT
    // Note: Override the shader binary path defined at build-time with a runtime option if specified.
    m_shaderBinaryPath{ !RtxOptions::Shader::shaderBinaryPath().empty() ? RtxOptions::Shader::shaderBinaryPath()
                                                                : BUILD_SOURCE_ROOT },
    m_recompileShadersOnLaunch{ RtxOptions::Shader::recompileOnLaunch() },
    // m_shaderChangeNotificationObject{ NULL },
#endif
    m_device{ nullptr } { }

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

#ifdef REMIX_DEVELOPMENT
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

    // Todo: Re-implement live shader recompilation based on a touch to a "signal" file in the future.
    // See REMIX-3930.
    /*
    if (!RtxOptions::Shader::useLiveEditMode()) {
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
    */
  }

  bool ShaderManager::reloadShaders() {
    bool allShadersLoaded = true;
    for (auto& [name, info] : m_shaderMap) {
      const auto shaderSPIRVBinary = (m_shaderBinaryPath / (std::string{ info.m_name } + ".spv")).u8string();

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
#endif

} // namespace dxvk
