/*
 * Copyright (c) 2022-2023, NVIDIA CORPORATION. All rights reserved.
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

 /*
  * This is a modified and reduced version of the config.h file in the DXVK repo
  * at https://github.com/doitsujin/dxvk/blob/master/src/util/config/config.h
  */

#pragma once

#include <string>
#include <unordered_map>
#include <vector>

namespace bridge_util {

  /**
   * \brief Tri-state
   *
   * Used to conditionally override
   * booleans if desired.
   */
  enum class Tristate: int32_t {
    Auto = -1,
    False = 0,
    True = 1,
  };

  /**
   * \brief Config option set
   *
   * Stores configuration options
   * as a set of key-value pairs.
   */
  class Config {
  public:
    enum class App {
      Client,
      Server
    };
    /**
     * \brief Initializes the static Config for this module
     *
     * \param [in] app [App::Client, App::Server], indicates which process in the bridge this is
     * \param [in] hModuleConfigOwner the HMODULE in question. If NULL, defaults to parent executable.
     * \param [in] clientExecutableName
     */
    static void init(const App app, void* hModuleConfigOwner = NULL);

    /**
     * \brief Sets an option
     *
     * \param [in] key Option name
     * \param [in] value Option value
     */
    static void setOption(
      const std::string& key,
      const std::string& value);

    /**
     * \brief Parses an option value
     *
     * Retrieves the option value as a string, and then
     * tries to convert that string to the given type.
     * If parsing the string fails because it is either
     * invalid or if the option is not defined, this
     * method will return a fallback value.
     *
     * Currently, this supports the types \c bool,
     * \c int32_t, \c uint32_t, \c float, and \c std::string.
     * \tparam T Return value type
     * \param [in] option Option name
     * \param [in] fallback Fallback value
     * \returns Parsed option value
     * \returns The parsed option value
     */
    template<typename T>
    static T getOption(const char* option, T fallback = T()) {
      if (!s_bIsInit) {
        Logger::err("ClientOptions accessed before Config initialized.");
        return T();
      }
      const std::string& value = get().getOptionValue(option);
      T result;
      if (parseOptionValue(value, result)) {
        return result;
      } else {
        return fallback;
      }
    }

    /**
     * \brief Checks if an option has been defined in config
     *
     * \param [in] option Option name
     * \returns true or false depending on if option was found
     */
    static bool isOptionDefined(const char* option);

  private:
    static bool s_bIsInit;
    using OptionMap = std::unordered_map<std::string, std::string>;
    OptionMap m_options;

    Config();
    ~Config();

    static Config& get() {
      static Config config;
      return config;
    };

    void merge(const Config& other);

    struct AppDefaultConfig {
      char* appName;
      char* regex;
      OptionMap options;
    };
    static std::vector<AppDefaultConfig> appDefaultConfigs;

    static Config getAppDefaultConfig(const char* exeFilePathIn = nullptr);

    static Config getUserConfig(const App app, void* hModuleConfigOwner = NULL);

    void logOptions() const;

    std::string getOptionValue(
      const char* option) const;

    static bool parseOptionValue(
      const std::string& value,
      std::string& result);

    static bool parseOptionValue(
      const std::string& value,
      std::vector<std::string>& result);

    static bool parseOptionValue(
      const std::string& value,
      std::vector<size_t>& result);

    static bool parseOptionValue(
      const std::string& value,
      bool& result);

    static bool parseOptionValue(
      const std::string& value,
      int32_t& result);

    static bool parseOptionValue(
      const std::string& value,
      uint32_t& result);

    static bool parseOptionValue(
      const std::string& value,
      uint16_t& result);

    static bool parseOptionValue(
      const std::string& value,
      uint8_t& result);

    static bool parseOptionValue(
      const std::string& value,
      float& result);

    static bool parseOptionValue(
      const std::string& value,
      Tristate& result);

  };


  /**
   * \brief Applies tristate option
   *
   * Overrides the given value if \c state is
   * \c True or \c False, and leaves it intact
   * otherwise.
   * \param [out] option The value to override
   * \param [in] state Tristate to apply
   */
  inline void applyTristate(bool& option, Tristate state) {
    option &= state != Tristate::False;
    option |= state == Tristate::True;
  }

}