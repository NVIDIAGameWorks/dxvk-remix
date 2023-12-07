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

#include <string>
#include <unordered_map>
#include <vector>

#include "../util/util_vector.h"
#include "../util/util_env.h"
#include "../util/util_keybind.h"

namespace dxvk {

  /**
   * \brief Tri-state
   * 
   * Used to conditionally override
   * booleans if desired.
   */
  enum class Tristate : int32_t {
    Auto  = -1,
    False =  0,
    True  =  1,
  };

  /**
   * \brief Config option set
   * 
   * Stores configuration options
   * as a set of key-value pairs.
   */
  class Config {
    using OptionMap = std::unordered_map<std::string, std::string>;
  public:

    Config();
    Config(OptionMap&& options);
    ~Config();

    /**
     * \brief Merges two configuration sets
     * 
     * Options specified in this config object will
     * not be overridden if they are specified in
     * the second config object.
     * \param [in] other Config set to merge.
     */
    void merge(const Config& other);

    // NV-DXVK start: new methods
    /**
     * \brief Generates a string for a value
     *
     * \param [in] value Option value
     */
    static std::string generateOptionString(
      const std::string& value);

    /**
     * \brief Generates a string for a value
     *
     * \param [in] value Option value
     */
    static std::string generateOptionString(
      const bool& value);

    /**
     * \brief Generates a string for a value
     *
     * \param [in] value Option value
     */
    static std::string generateOptionString(
      const int32_t& value);

    /**
     * \brief Generates a string for a value
     *
     * \param [in] value Option value
     */
    static std::string generateOptionString(
      const uint32_t& value);

    /**
     * \brief Generates a string for a value
     *
     * \param [in] value Option value
     */
    static std::string generateOptionString(
      const float& value);

    /**
     * \brief Generates a string for a value
     *
     * \param [in] value Option value
     */
    static std::string generateOptionString(
      const Vector2i& value);

    // NV-DXVK start: added a variant
    /**
     * \brief Generates a string for a value
     *
     * \param [in] value Option value
     */
    static std::string generateOptionString(
      const Vector2& value);
    // NV-DXVK end

    /**
     * \brief Generates a string for a value
     *
     * \param [in] value Option value
     */
    static std::string generateOptionString(
      const Vector3& value);

    // NV-DXVK start: added a variant
    /**
     * \brief Generates a string for a value
     *
     * \param [in] value Option value
     */
    static std::string generateOptionString(
      const Vector4& value);
    // NV-DXVK end

    /**
     * \brief Generates a string for a value
     *
     * \param [in] value Option value
     */
    static std::string generateOptionString(
      const Tristate& value);
    // NV-DXVK end

    // NV-DXVK start: setOption type safety
    template< typename T >
    void setOption(
      const std::string& key,
      const T& value) = delete;
    // NV-DXVK end

    /**
     * \brief Sets an option
     * 
     * \param [in] key Option name
     * \param [in] value Option value
     */
    void setOption(
      const std::string& key,
      const std::string& value);

    /**
     * \brief Sets an option
     *
     * \param [in] key Option name
     * \param [in] value Option value
     */
    void setOption(
      const std::string& key,
      const bool& value);

    /**
     * \brief Sets an option
     *
     * \param [in] key Option name
     * \param [in] value Option value
     */
    void setOption(
      const std::string& key,
      const int32_t& value);

    /**
     * \brief Sets an option
     *
     * \param [in] key Option name
     * \param [in] value Option value
     */
    void setOption(
      const std::string& key,
      const uint32_t& value);

    /**
     * \brief Sets an option
     *
     * \param [in] key Option name
     * \param [in] value Option value
     */
    void setOption(
      const std::string& key,
      const float& value);

    /**
     * \brief Sets an option
     *
     * \param [in] key Option name
     * \param [in] value Option value
     */
    void setOption(
      const std::string& key,
      const Vector2i& value);

    // NV-DXVK start: added a variant
    /**
     * \brief Sets an option
     *
     * \param [in] key Option name
     * \param [in] value Option value
     */
    void setOption(
      const std::string& key,
      const Vector2& value);
    // NV-DXVK end

    /**
     * \brief Sets an option
     *
     * \param [in] key Option name
     * \param [in] value Option value
     */
    void setOption(
      const std::string& key,
      const Vector3& value);

    /**
     * \brief Sets an option
     *
     * \param [in] key Option name
     * \param [in] value Option value
     */
    void setOption(
      const std::string& key,
      const Tristate& value);

    /**
     * \brief Parses an option value
     *
     * Retrieves the option value as a string, and then
     * tries to convert that string to the given type.
     * If envVarName is specified and the envVar is set, 
     * it retrieves the option value from the envVar instead. 
     * If parsing the string fails because it is either
     * invalid or if the option is not defined, this
     * method will return a fallback value. 
     * 
     * Currently, this supports the types \c bool,
     * \c int32_t, \c uint32_t, \c float, and \c std::string.
     * \tparam T Return value type
     * \param [in] option Option name
     * \param [in] fallback Fallback value
     * \param [in] envVarName Environment variable name
     * \returns Parsed option value
     * \returns The parsed option value
     */
    template<typename T>
    T getOption(const char* option, T fallback = T(), const char* envVarName = nullptr) const {
      const std::string& value = getOptionValue(option);

      T result = fallback;
      parseOptionValue(value, result);
      if (envVarName) {
        const std::string& envVarValue = env::getEnvVar(envVarName);
        if (envVarValue != "")
          parseOptionValue(envVarValue, result);
      }

      return result;
    }

    // NV-DXVK start: Extend logOptions function
    /**
     * \brief Logs option values
     * 
     * Prints the effective configuration
     * to the log for debugging purposes. 
     */
    void logOptions(const char* configName) const;
    // NV-DXVK end
    
    // NV-DXVK start: Generic config parsing, reduce duped code
    enum Type {
      Type_User,
      Type_App,
      Type_RtxUser,
      Type_RtxMod,
      Type_kSize
    };
    struct Desc{
      std::string name;
      std::string env;
      std::string confName;
    };
    static const Desc& getDesc(const Type& type) { return m_descs[type]; }
    template<Type type>
    static Config getConfig(const std::string& configPath = "");
    // NV-DXVK end

    /**
     * \brief Retrieves default options for an app
     * 
     * \param [in] appName Name of the application
     * \returns Default options for the application
     */
    static Config getAppConfig(const std::string& appName);

    static std::string toLower(std::string str);

    /**
     * \brief Writes custom configuration to file
     */
    static void serializeCustomConfig(const Config& config, std::string filePath, std::string filterStr = std::string());
    // END

    // START
    /**
     * \brief Retrieves custom configuration
     * 
     * Reads options from the configuration file,
     * if it can be found, or an empty option set.
     * \returns User-defined configuration options
     */
    static Config getCustomConfig(std::string filePath, std::string filterStr = std::string());

    static bool parseOptionValue(
      const std::string&  value,
            std::string&  result);

    static bool parseOptionValue(
      const std::string& value,
      std::vector<std::string>& result);

    static bool parseOptionValue(
      const std::string&  value,
            bool&         result);

    static bool parseOptionValue(
      const std::string&  value,
            int32_t&      result);

    static bool parseOptionValue(
      const std::string&  value,
            uint32_t&     result);
    
    static bool parseOptionValue(
      const std::string&  value,
            float&      result);

    static bool parseOptionValue(
      const std::string& value,
      Vector2i& result);

    // NV-DXVK start: Add option parsing variants
    static bool parseOptionValue(
      const std::string& value,
      Vector2& result);

    static bool parseOptionValue(
      const std::string& value,
      Vector3& result);
    // NV-DXVK end

    static bool parseOptionValue(
      const std::string& value,
      VirtualKeys& result);

    static bool parseOptionValue(
      const std::string&  value,
            Tristate&     result);

    template<typename I, typename V>
    static bool parseStringOption(
            std::string   str,
            I             begin,
            I             end,
            V&            value);

  private:

    OptionMap m_options;

    std::string getOptionValue(
      const char*         option) const;
      
    // NV-DXVK start: Generic config parsing, reduce duped code
    static const inline std::array<Desc,Type_kSize> m_descs {
      Desc{"User","DXVK_CONFIG_FILE","dxvk.conf"},
      Desc{"App","",""},
      Desc{"RtxUser","DXVK_RTX_CONFIG_FILE","rtx.conf"},
      Desc{"RtxMod","","rtx.conf"}
    };
    // NV-DXVK end

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
