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

#pragma once

#include "../rtx_graph_component_macros.h"
#include "../../rtx_option.h"
#include "../../../../util/xxHash/xxhash.h"

namespace dxvk {
namespace components {

#define LIST_INPUTS(X) \
  X(RtComponentPropertyType::String, "", optionName, "Option Name", "The full name of the RTX option to read (e.g., 'rtx.someOption').")

#define LIST_STATES(X)

#define LIST_OUTPUTS(X) \
  X(RtComponentPropertyType::Float3, Vector3(0.0f, 0.0f, 0.0f), value, "Value", "The current value of the RTX option as a Color3 (RGB). Returns black (0,0,0) if the option is not found or is not a Vector3 type.", property.treatAsColor = true)

REMIX_COMPONENT( \
  /* the Component name */ RtxOptionReadColor3, \
  /* the UI name */        "Rtx Option Read Color3", \
  /* the UI categories */  "Sense", \
  /* the doc string */     "Reads the current value of a Color3 (RGB) RTX option.\n\n" \
    "Outputs the current value of a given RTX option as a Color3. Internally, Color3 is stored as Vector3. " \
    "The option name should be the full name including category (e.g., 'rtx.fallbackLightRadiance').", \
  /* the version number */ 1, \
  LIST_INPUTS, LIST_STATES, LIST_OUTPUTS);

#undef LIST_INPUTS
#undef LIST_STATES
#undef LIST_OUTPUTS

void RtxOptionReadColor3::updateRange(const Rc<DxvkContext>& context, const size_t start, const size_t end) {
  auto& globalRtxOptions = RtxOptionImpl::getGlobalRtxOptionMap();
  
  for (size_t i = start; i < end; i++) {
    Vector3 value(0.0f, 0.0f, 0.0f);
    
    const std::string& optionName = m_optionName[i];
    if (!optionName.empty()) {
      const XXH64_hash_t optionHash = StringToXXH64(optionName, 0);
      
      auto optionIt = globalRtxOptions.find(optionHash);
      if (optionIt != globalRtxOptions.end()) {
        RtxOptionImpl* option = optionIt->second.get();
        
        // Get the value if it's a Vector3 type (Color3 is stored as Vector3)
        if (option->type == OptionType::Vector3 && option->resolvedValue.v3 != nullptr) {
          value = *option->resolvedValue.v3;
        } else {
          ONCE(Logger::warn(str::format("RtxOptionReadColor3: Option '", optionName, "' is not a Vector3/Color3 type.")));
        }
      } else {
        ONCE(Logger::warn(str::format("RtxOptionReadColor3: Option '", optionName, "' not found.")));
      }
    }
    
    m_value[i] = value;
  }
}

}  // namespace components
}  // namespace dxvk

