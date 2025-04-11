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
#include <cassert>
#include <algorithm>
#include <array>
#include <filesystem>

#include "dxvk_device.h"

#include "../util/util_string.h"

#include "rtx_shader_manager.h"
#include "rtx_options.h"

namespace dxvk {
  namespace {
#ifdef REMIX_DEVELOPMENT
    // Warning: These executable-related strings and paths are intentionally not configurable. By being constant expressions they may be stored in
    // a read only section of the executable's memory, protecting them from corruption or manipulation that could lead to security issues. This has
    // the downside of reducing how flexible the runtime shader recompilation system is as build-specific paths for instance will not be valid on other
    // machinies, but this should not be a huge issue as it is intended to be a local development-only feature to begin with, rather than something
    // a user would need to be able to configure. For any sort of exceptional cases, this information can be modified manually in the source code for
    // a custom build (e.g. needing to do shader debugging remotely on a machine Remix was not built on, requiring the paths be hardcoded to something else).

    constexpr auto recompileCommandPythonExecutableName{ L"python.exe" };
    constexpr auto recompileCommandArguments{ str::string_viewz(
      // Note: python.exe put here as a bit of a hack to provide Python with the argv[0] it expects from the shell. Without this Python will not interpret
      // the arguments properly (as it starts parsing arguments at argv[1] not [0]). This argv[0] is not the full path of the executable that is found,
      // but programs generally don't rely on this value being the actual path of the program so this should be fine. If it does need to be the full path
      // though, then another temporary buffer will have to be created to memcpy the found path of the Python executable and the arguments together (this
      // complexity has just been avoided unless it actually becomes a problem).
      L"python.exe " WIDEN_MACRO_LITERAL(RUNTIME_SHADER_RECOMPILATION_PYTHON_ARGUMENTS)
    ) };
    constexpr auto shaderSourcePath{
      WIDEN_MACRO_LITERAL(RUNTIME_SHADER_RECOMPILATION_SHADER_SOURCE_PATH)
    };
    constexpr auto spirvBinaryOutputPath{
      // Note: No widening needed as this is expected to be a UTF-8 string (may need the u8 prefix if the compiler does not default to UTF-8).
      RUNTIME_SHADER_RECOMPILATION_SPIRV_BINARY_OUTPUT_PATH
    };
#endif
  }

  ShaderManager* ShaderManager::s_instance = nullptr;

  ShaderManager::ShaderManager() :
#ifdef REMIX_DEVELOPMENT
    m_recompileShadersOnLaunch{ RtxOptions::Shader::recompileOnLaunch() },
#endif
    m_device{ nullptr } { }

  ShaderManager::~ShaderManager() {
#ifdef REMIX_DEVELOPMENT
    // Free resources for runtime shader recompilation

    freeShaderChangeNotification();

    if (getShaderReloadPhase() == ShaderReloadPhase::SPIRVRecompilation) {
      terminateSpirVRecompilation();
    }
#endif
  }

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
  void ShaderManager::update() {
    // Reload shaders on launch if requested

    if (m_recompileShadersOnLaunch) {
      static bool isFirstFrame = true;

      // Skip shader reload at the start of a first frame as the render passes haven't initialized their shaders,
      // and do not request a reload if there is already a reload active
      if (!isFirstFrame && getShaderReloadPhase() == ShaderReloadPhase::Idle) {
        requestReloadShaders();

        m_recompileShadersOnLaunch = false;
      }

      isFirstFrame = false;
    }

    // Handle live edit mode

    if (RtxOptions::Shader::useLiveEditMode()) {
      // Allocate a change notification to watch the shader directory for any changes

      if (!allocateShaderChangeNotification()) {
        // Disable live edit mode if any allocation error occurs

        RtxOptions::Shader::useLiveEditModeRef() = false;
      }

      // Reload shaders if any shaders have changed on disk and if there is not a reload currently in progress

      bool shadersChanged;

      if (!checkShaderChangeNotification(shadersChanged)) {
        // Free and disable live edit mode if any error occurs during checking the status

        freeShaderChangeNotification();

        RtxOptions::Shader::useLiveEditModeRef() = false;
      } else if (shadersChanged && getShaderReloadPhase() == ShaderReloadPhase::Idle) {
        requestReloadShaders();
      }
    } else {
      // Free the change notification as it's no longer needed

      freeShaderChangeNotification();
    }

    // Attempt to finalize reloading shaders asynchronously if SPIR-V recompilation is active
    // Note: This is done as the only reason this recompilation would be active during the update call like this
    // is if it was specified to be non-blocking in the request, as otherwise it would've finished entierly within
    // that call.

    if (getShaderReloadPhase() == ShaderReloadPhase::SPIRVRecompilation) {
      tryFinalizeReloadShaders(false);
    }
  }

  void ShaderManager::requestReloadShaders() {
    // Ensure shader reloading is not currently active
    // Note: This requires the external caller to check this condition which is why there is an assertion here,
    // but returning early is done as well just to avoid resource leaks from improper usage.

    assert(getShaderReloadPhase() == ShaderReloadPhase::Idle);

    if (getShaderReloadPhase() != ShaderReloadPhase::Idle) {
      return;
    }

    m_lastShaderReloadStatus = ShaderReloadStatus::Unknown;

    Logger::info("Runtime shader recompilation initiated.");

    // Attempt to dispatch a SPIR-V recompilation process

    if (!dispatchSpirVRecompilation()) {
      return;
    }

    // Attempt to finalize reloading shaders synchronously if async SPIR-V recompilation is disabled

    if (!RtxOptions::Shader::asyncSpirVRecompilation()) {
      tryFinalizeReloadShaders(true);
    }
  }

  ShaderManager::ShaderReloadPhase ShaderManager::getShaderReloadPhase() const {
    return m_shaderReloadPhase;
  }

  ShaderManager::ShaderReloadStatus ShaderManager::getLastShaderReloadStatus() const {
    return m_lastShaderReloadStatus;
  }

  bool ShaderManager::allocateShaderChangeNotification() {
    // Skip allocation if the change notification is already allocated

    if (m_shaderChangeNotificationObject != NULL) {
      return true;
    }

    // Allocate the change notification

    const auto findFirstChangeNotificationResult{
      FindFirstChangeNotificationW(shaderSourcePath, TRUE, FILE_NOTIFY_CHANGE_LAST_WRITE)
    };

    if (findFirstChangeNotificationResult == INVALID_HANDLE_VALUE) {
      const auto findFirstChangeNotificationErrorCode{ GetLastError() };

      Logger::err(str::format("Unable to create the live shader edit file change notification. Error code: ", findFirstChangeNotificationErrorCode));

      return false;
    }

    m_shaderChangeNotificationObject = findFirstChangeNotificationResult;

    return true;
  }

  void ShaderManager::freeShaderChangeNotification() {
    // Skip freeing if the change notification is already freed

    if (m_shaderChangeNotificationObject == NULL) {
      return;
    }

    // Free the change notification

    const auto findCloseChangeNotificationResult{
      FindCloseChangeNotification(m_shaderChangeNotificationObject)
    };

    if (findCloseChangeNotificationResult == 0) {
      const auto findCloseChangeNotficationErrorCode{ GetLastError() };

      Logger::warn(str::format("Unable to close the live shader edit file change notification. Error code: ", findCloseChangeNotficationErrorCode));

      // Note: Fallthrough to the usual path from here, nothing much else we can do at this point other than retrying potentially, but a small resource leak is fine for now.
    }

    m_shaderChangeNotificationObject = NULL;
  }

  bool ShaderManager::checkShaderChangeNotification(bool& shadersChanged) {
    // Check the status of the change notification

    if (WaitForSingleObject(m_shaderChangeNotificationObject, 0) == WAIT_OBJECT_0) {
      // Continue monitorying by scheduling the next change notification

      if (FindNextChangeNotification(m_shaderChangeNotificationObject) == 0) {
        const auto findNextChangeNotificationErrorCode{ GetLastError() };

        Logger::err(str::format("Unable to re-create the live shader edit file change notification. Error code: ", findNextChangeNotificationErrorCode));

        return false;
      }

      shadersChanged = true;

      return true;
    }

    shadersChanged = false;

    return true;
  }

  bool ShaderManager::dispatchSpirVRecompilation() {
    // Ensure shader reloading is not currently active

    assert(getShaderReloadPhase() == ShaderReloadPhase::Idle);

    // Locate the Python install
    // Note: This assumes pythom.exe is in the user's PATH or otherwise accessible via the typical Windows system search paths.

    // Note: Limited to MAX_PATH, technically paths on modern Windows can be longer but this should be fine for development purposes.
    std::array<wchar_t, MAX_PATH> pythonExecutablePath;

    // Note: This function may return 0 or a value greater than the array size passed in on failure. Both failure cases need to be taken into account to avoid reading
    // from a buffer that is not properly filled with the desired path. Will output the properly null-terminated string otherwise.
    const auto searchPathResult{
      SearchPathW(NULL, recompileCommandPythonExecutableName, NULL, pythonExecutablePath.size(), pythonExecutablePath.data(), NULL)
    };

    if (searchPathResult == 0) {
      const auto searchPathErrorCode{ GetLastError() };

      Logger::err(str::format("Unable to find Python executable in system search paths for shader recompilation. Error code: ", searchPathErrorCode));

      return false;
    } else if (searchPathResult > pythonExecutablePath.size()) {
      Logger::err(str::format("Python executable path length for shader recompilation exceeds allocated limit. Required: ", searchPathResult, ", Limit: ", pythonExecutablePath.size()));

      return false;
    }

    // Note: On success, the returned value is the length of the string not including the null terminator.
    assert(pythonExecutablePath[searchPathResult] == L'\0');

    // Make a copy of the recompile command arguments
    // Note: This must be done due to unusual design with Windows' CreateProcessW API where the argument string must be non-const as it can apparently be
    // modified by the function for some reason. We want to maintain a constexpr argument string though to prevent the possibility of corruption or modification
    // of that memory, so making a fresh copy of it every time like this is probably the best way of satisfying both of those requirements.

    std::array<wchar_t, recompileCommandArguments.size()> recompileCommandArgumentsCopy{};

    // Note: String view range in this case includes the null terminator due to using the str::string_viewz helper. Important that the null terminator
    // is copied over for proper behavior.
    std::copy(recompileCommandArguments.cbegin(), recompileCommandArguments.cend(), recompileCommandArgumentsCopy.begin());

    assert(recompileCommandArgumentsCopy.back() == L'\0');

    // Invoke Python to run the compile script

    // Warning: Obviously, invoking Python can introduce potential security holes into Remix, so the data passed to CreateProcess needs to be
    // considered carefully. Generally:
    // - Do not allow user or runtime-specified executable names or arguments as these may allow for manipulation of the intended behavior (searching
    //   for a constant defined name should be fine though even if it may resolve to different locations at runtime as this behavior will be more controlled
    //   by the user). Also, do not attempt to allow such functionality by sanitizing arguments/etc as this is difficult to do especially with complex
    //   argument parsing and bound to be done incorrectly.
    // - Do not use the shell to invoke this process (rather use SearchPath+CreateProcess) as doing so may allow for potential shell injection if
    //   user-specified information is allowed to be passed into the shell.
    // - Do not allow this code to exist for non-development builds. This is a development feature and any risk associated with invoking Python in
    //   Remix directly should be limited in scope to a small subset of users.
    // If more flexible behavior is desired in the future, then this code should probably be moved out of Remix and into an external script which is
    // fully executed manually or via the build system somehow (slightly annoying though as builds cannot be invoked while debugging).

    // Note: Development-only code sanity check, see above.
#ifndef REMIX_DEVELOPMENT
#error REMIX_DEVELOPMENT must be defined for runtime shader recompilation code.
#endif

    STARTUPINFOW startupInformation;

    ZeroMemory(&startupInformation, sizeof(startupInformation));
    ZeroMemory(&m_spirVRecompilationDispatchInformation, sizeof(m_spirVRecompilationDispatchInformation));

    startupInformation.cb = sizeof(startupInformation);

    // Note: No stdout redirection needed, default settings like this for CreateProcess allow for stdout to share the terminal of the parent process which allows for the compilation
    // progress messages to be written to the console as desired. They will not be written to the Remix log however, so if this functionality is desired in the future then stdout will
    // have to be redirected into something this process can read and then output via the logging system.
    const auto createProcessResult{
      CreateProcessW(pythonExecutablePath.data(), recompileCommandArgumentsCopy.data(), NULL, NULL, FALSE, 0, NULL, NULL, &startupInformation, &m_spirVRecompilationDispatchInformation)
    };

    if (createProcessResult == 0) {
      const auto createProcessErrorCode{ GetLastError() };

      Logger::err(str::format("Unable create a Python shader recompilation process. Error code: ", createProcessErrorCode));

      return false;
    }

    // Set the reload phase to SPIR-V Recompilation now that the process has been started successfully

    m_shaderReloadPhase = ShaderReloadPhase::SPIRVRecompilation;

    return true;
  }

  bool ShaderManager::tryGetSpirVRecompilationResults(bool blockUntilCompletion, bool& completed, bool& result) {
    // Ensure the shader reloading is in the SPIR-V recompilation phase as otherwise there is no process to get results from

    assert(getShaderReloadPhase() == ShaderReloadPhase::SPIRVRecompilation);

    // Block until the process has completed if requested, or poll the status

    const auto waitForSingleObjectResult{
      WaitForSingleObject(m_spirVRecompilationDispatchInformation.hProcess, blockUntilCompletion ? INFINITE : 0)
    };

    if (blockUntilCompletion || waitForSingleObjectResult == WAIT_OBJECT_0) {
      // Get the exit code from the process now that it has exited

      DWORD exitCode;

      const auto getExitCodeResult{
        GetExitCodeProcess(m_spirVRecompilationDispatchInformation.hProcess, &exitCode)
      };

      if (!getExitCodeResult) {
        const auto getExitCodeErrorCode{ GetLastError() };

        Logger::err(str::format("Unable get exit code from Python shader recompilation process. Error code: ", getExitCodeErrorCode));

        return false;
      }

      completed = true;
      result = exitCode == 0;

      return true;
    } else {
      completed = false;
    }

    return true;
  }

  void ShaderManager::freeSpirVRecompilationHandles() {
    // Ensure the shader reloading is in the SPIR-V recompilation phase, indicating the handles have not yet been freed

    assert(getShaderReloadPhase() == ShaderReloadPhase::SPIRVRecompilation);

    // Free the handles retreived from the created process

    CloseHandle(m_spirVRecompilationDispatchInformation.hProcess);
    CloseHandle(m_spirVRecompilationDispatchInformation.hThread);
  }

  void ShaderManager::terminateSpirVRecompilation() {
    // Ensure shader reloading is in the SPIR-V recompilation phase

    assert(getShaderReloadPhase() == ShaderReloadPhase::SPIRVRecompilation);

    // Note: Shader manager is destructing, no sense in trying to continue compiling shaders, so the process
    // should be stopped to avoid a lingering process.
    TerminateProcess(m_spirVRecompilationDispatchInformation.hProcess, 1);

    freeSpirVRecompilationHandles();

    // Re-set the reload phase to Idle now that the process has been terminated

    m_shaderReloadPhase = ShaderReloadPhase::Idle;
  }

  void ShaderManager::recreateShaders() {
    // Ensure shader reloading was previously the SPIR-V recompilation phase

    assert(getShaderReloadPhase() == ShaderReloadPhase::SPIRVRecompilation);

    // Set the reload phase to Shader Recreation

    m_shaderReloadPhase = ShaderReloadPhase::ShaderRecreation;

    // Recreate all shaders within the shader map from the recompiled SPIR-V binaries

    bool allShaderRecreationSuccessful = true;

    for (auto& [name, info] : m_shaderMap) {
      const auto shaderSPIRVBinaryPath = (std::filesystem::path{ spirvBinaryOutputPath } / (std::string { info.m_name } + ".spv")).u8string();

      bool recreationSuccessful = false;
      std::ifstream file(shaderSPIRVBinaryPath, std::ios::binary);

      if (file) {
        SpirvCodeBuffer code(file);

        if (code.size()) {
          info.m_staticCode = code; // Update the code
          info.m_shader.emplace_back(createShader(info));

          recreationSuccessful = true;
        }
      }

      if (!recreationSuccessful) {
        Logger::err(str::format("Failed to recreate a shader from a SPIR-V binary: \"", shaderSPIRVBinaryPath, "\""));

        allShaderRecreationSuccessful = false;
      }
    }

    // Set the reload phase to Idle now that shader recreation is complete, and set the reload status based on if shaders were reloaded successfully

    m_lastShaderReloadStatus = allShaderRecreationSuccessful ? ShaderReloadStatus::Success : ShaderReloadStatus::Failure;
    m_shaderReloadPhase = ShaderReloadPhase::Idle;

    Logger::info(str::format("Runtime shader recompilation completed ", allShaderRecreationSuccessful ? "successfully." : "unsuccessfully."));
  }

  void ShaderManager::tryFinalizeReloadShaders(bool blockUntilComplete) {
    // Ensure the shader reloading is in the SPIR-V recompilation phase
    // Note: This is the phase the finalization process is expected to work off of, either by waiting for the SPIR-V recompilation process
    // to complete or by checking if the results are available yet and then recreating shaders afterwards.

    assert(getShaderReloadPhase() == ShaderReloadPhase::SPIRVRecompilation);

    // Attempt to get results from the SPIR-V compilation process

    bool completed;
    bool result;

    if (tryGetSpirVRecompilationResults(blockUntilComplete, completed, result)) {
      // Note: If blocking until completion is requested and the result function returns without error, then the completed flag should always be true.
      if (blockUntilComplete) {
        assert(completed);
      }

      if (completed) {
        // Free recompilation handles now that the process has completed

        freeSpirVRecompilationHandles();

        if (result) {
          // Recreate the shaders from the SPIR-V binaries which should now be available

          recreateShaders();
        } else {
          // Indicate that shader recompilation has failed

          m_lastShaderReloadStatus = ShaderReloadStatus::Failure;
          m_shaderReloadPhase = ShaderReloadPhase::Idle;

          Logger::err("Unable to compile shader sources to SPIR-V for runtime shader recompilation.");
        }

        // Ensure the shader reload phase has been re-set to Idle after either recreating shaders or handling
        // the SPIR-V recompilation failure

        assert(getShaderReloadPhase() == ShaderReloadPhase::Idle);
      }
    } else {
      // Terminate the SPIR-V recompilation in the case of an error while trying to read results

      terminateSpirVRecompilation();

      // Ensure the shader reload phase has been re-set to Idle after terminating SPIR-V recompilation

      assert(getShaderReloadPhase() == ShaderReloadPhase::Idle);
    }
  }
#endif

} // namespace dxvk
