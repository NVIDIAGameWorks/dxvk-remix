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
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <variant>

#include "../util/util_vector.h"
#include "rtx_option_constants.h"

namespace dxvk {

  union GenericValue;

  // A family of runtime-created RtxOptions that share one type and default.
  // See documentation/RemixConfig.md, "Dynamic Option Namespaces".
  struct DynamicOptionNamespace {
    std::string prefix;        // Begins with "rtx." and ends with ".", e.g. "rtx.example."
    OptionType type = OptionType::Bool;
    std::string defaultValue;  // Parsed with parseDynamicValue; String namespaces may use "".
    uint32_t flags = 0;        // RtxOptionFlags::UserSetting and RtxOptionFlags::NoSave only.
    std::string description;

    bool operator==(const DynamicOptionNamespace& other) const {
      return prefix == other.prefix && type == other.type && defaultValue == other.defaultValue &&
             flags == other.flags && description == other.description;
    }
  };

  using DynamicOptionValue = std::variant<bool, int32_t, float, std::string, Vector2, Vector3, Vector4, Vector2i>;

  struct DynamicOptionEntry {
    std::string suffix;  // Key without the namespace prefix
    DynamicOptionValue value;
  };

  enum class DynamicOptionResult {
    Success,
    InvalidArgument,  // Malformed key or value for the owning namespace
    NotOwned,         // No registered namespace owns the key
    Conflict,         // Key hashes to an existing option with a different name
    NoTargetLayer     // The layer the change routes to is missing or disabled
  };

  // One or more [0-9A-Za-z_] segments separated by single dots.
  bool isValidDynamicSuffix(std::string_view suffix);

  // Strict parse of a dynamic option value. `value` must be allocated for `type`
  // (see createGenericValue); it may be partially written when this returns false.
  bool parseDynamicValue(OptionType type, std::string_view text, GenericValue& value);

}  // namespace dxvk
