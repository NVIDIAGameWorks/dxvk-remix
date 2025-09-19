/*
* Copyright (c) 2021-2023, NVIDIA CORPORATION. All rights reserved.
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
#pragma once

#include "util_string.h"

namespace dxvk::env {
  
  /**
   * \brief Checks whether the host platform is 32-bit
   */
  constexpr bool is32BitHostPlatform() {
    return sizeof(void*) == 4;
  }

  /**
   * \brief Gets environment variable
   * 
   * If the variable is not defined, this will return
   * an empty string. Note that environment variables
   * may be defined with an empty value.
   * \param [in] name Name of the variable
   * \returns Value of the variable
   */
  std::string getEnvVar(const char* name);

  // NV-DXVK start: get environment variable with a fallback
  /**
   * \brief Gets environment variable as a specified type
   *
   * NOTE: Generalization is deleted, and specific implementations
   * are moved to .cpp, to prevent include of 'config/config.h',
   * which in turn uses this header, 'util_env.h'.
   *
   * If parsing the string fails because it is either
     * invalid or if the option is not defined, this
     * method will return a fallback value. 
   * \tparam T Return value type
   * \param [in] name Name of the variable
   * \param [in] fallback Fallback value
   * \returns Parsed environment variable value
   */
  template<typename T>
  T getEnvVar(const char* name, T fallback) = delete;
  template<>
  bool getEnvVar(const char* name, bool fallback);
  // NV-DXVK end

  /**
   * \brief Sets environment variable
   * 
   * If setting the variable was successful true is returned,
   * false otherwise.
   * \param [in] name Name of the variable
   * \param [in] value String to set the variable to
   * \returns A boolean indicating if setting the environment variable succeeded.
   */
  bool setEnvVar(const char* name, const char* value);

  /**
   * \brief Checks whether a file name has a given extension
   *
   * \param [in] name File name
   * \param [in] ext Extension to match, in lowercase letters
   * \returns Position of the extension within the file name, or
   *    \c std::string::npos if the file has a different extension
   */
  size_t matchFileExtension(const std::string& name, const char* ext);

  /**
   * \brief Gets the executable name
   * 
   * Returns the base name (not the full path) of the
   * program executable, including the file extension.
   * This function should be used to identify programs.
   * \returns Executable name
   */
  std::string getExeName();


  /**
   * \brief Gets the executable name without the .exe
   * 
   * Returns the base name (not the full path) of the
   * program executable, including the file extension.
   * This function should be used to identify programs.
   * \returns Executable name
   */
  std::string getExeNameNoSuffix();
  
  /**
   * \brief Gets the executable name without extension
   *
   * Same as \ref getExeName but without the file extension.
   * \returns Executable name
   */
  std::string getExeBaseName();

  /**
   * \brief Gets full path to executable
   * \returns Path to executable
   */
  std::string getExePath();

  // NV-DXVK start

  /**
   * \brief Appends "__X" numbered suffix to filename in path if file already exists
   * \returns De-duped filename IF path already exists, else return original
   */
  std::string dedupeFilename(const std::string& originalFilePath);

  /**
   * \brief Query whether we're running under the remix bridge IPC mechanism
   * \returns True if running under Remix bridge
   */
  bool isRemixBridgeActive();
  
  /**
   * \brief Gets full path to a given module
   * \param module The name of the module to search for
   * \returns Path to module
   */
  std::string getModulePath(const char* module);

  /**
   * \brief Gets available system physical memory (i.e. system physical memory not used by any process)
   * \param availableSize Available system physical memory in bytes
   * \returns true if the function succeeds
   */
  bool getAvailableSystemPhysicalMemory(uint64_t& availableSize);

  /**
   * \brief Gets full directory path to the current module
   */
  std::string getDllDirectory();
  // NV-DXVK end

  /**
   * \brief Sets name of the calling thread
   * \param [in] name Thread name
   */
  void setThreadName(const std::string& name);

  /**
   * \brief Creates a directory
   * 
   * \param [in] path Path to directory
   * \returns \c true on success
   */
  bool createDirectory(const std::string& path);
  
  /**
 * \brief Kills the current process via system
 */
  void killProcess();
}
