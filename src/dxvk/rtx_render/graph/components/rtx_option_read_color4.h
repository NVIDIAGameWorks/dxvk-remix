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
  X(RtComponentPropertyType::Float4, Vector4(0.0f, 0.0f, 0.0f, 1.0f), value, "Value", "The current value of the RTX option as a Color4 (RGBA). Returns black with full alpha (0,0,0,1) if the option is not found or is not a Vector4 type.", property.treatAsColor = true)

REMIX_COMPONENT( \
  /* the Component name */ RtxOptionReadColor4, \
  /* the UI name */        "Rtx Option Read Color4", \
  /* the UI categories */  "Sense", \
  /* the doc string */     "Reads the current value of a Color4 (RGBA) RTX option.\n\n" \
    "Outputs the current value of a given RTX option as a Color4. Internally, Color4 is stored as Vector4. " \
    "The option name should be the full name including category.", \
  /* the version number */ 1, \
  LIST_INPUTS, LIST_STATES, LIST_OUTPUTS);

#undef LIST_INPUTS
#undef LIST_STATES
#undef LIST_OUTPUTS

void RtxOptionReadColor4::updateRange(const Rc<DxvkContext>& context, const size_t start, const size_t end) {
  auto& globalRtxOptions = RtxOptionImpl::getGlobalRtxOptionMap();
  
  for (size_t i = start; i < end; i++) {
    Vector4 value(0.0f, 0.0f, 0.0f, 1.0f);
    
    const std::string& optionName = m_optionName[i];
    if (!optionName.empty()) {
      const XXH64_hash_t optionHash = XXH3_64bits(optionName.c_str(), optionName.length());
      
      auto optionIt = globalRtxOptions.find(optionHash);
      if (optionIt != globalRtxOptions.end()) {
        RtxOptionImpl* option = optionIt->second.get();
        
        // Get the value if it's a Vector4 type (Color4 is stored as Vector4)
        if (option->type == OptionType::Vector4 && option->resolvedValue.v4 != nullptr) {
          value = *option->resolvedValue.v4;
        } else {
          ONCE(Logger::warn(str::format("RtxOptionReadColor4: Option '", optionName, "' is not a Vector4/Color4 type.")));
        }
      } else {
        ONCE(Logger::warn(str::format("RtxOptionReadColor4: Option '", optionName, "' not found.")));
      }
    }
    
    m_value[i] = value;
  }
}

}  // namespace components
}  // namespace dxvk

