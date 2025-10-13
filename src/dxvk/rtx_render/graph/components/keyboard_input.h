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
#include "../../imgui/dxvk_imgui.h"
#include "../../../util/util_keybind.h"
#include "../../../util/config/config.h"

namespace dxvk {
namespace components {

#define LIST_INPUTS(X) \
  X(RtComponentPropertyType::String, std::string("A"), keyString, "Key String", "The key combination string (e.g., 'A', 'CTRL,A', 'SHIFT,SPACE'). Supports key names and combinations like RTX options.")

#define LIST_STATES(X) \
  X(RtComponentPropertyType::Bool, false, wasPressedLastFrame, "", "Internal state to track if the key was pressed in the previous frame.")

#define LIST_OUTPUTS(X) \
  X(RtComponentPropertyType::Bool, false, isPressed, "Is Pressed", "True if the key combination is currently being pressed.") \
  X(RtComponentPropertyType::Bool, false, wasJustPressed, "Was Just Pressed", "True if the key combination was just pressed this frame.") \
  X(RtComponentPropertyType::Bool, false, wasClicked, "Was Clicked", "True for one frame after the key combination is released (press then release cycle).")

REMIX_COMPONENT( \
  /* the Component name */ KeyboardInput, \
  /* the UI name */        "Keyboard Input", \
  /* the UI categories */  "Sense", \
  /* the doc string */     "Checks the state of a keyboard key or key combination using the same format as RTX options.", \
  /* the version number */ 1, \
  LIST_INPUTS, LIST_STATES, LIST_OUTPUTS);

#undef LIST_INPUTS
#undef LIST_STATES
#undef LIST_OUTPUTS

void KeyboardInput::updateRange(const Rc<DxvkContext>& context, const size_t start, const size_t end) {
  for (size_t i = start; i < end; i++) {
    // Parse the key string into VirtualKeys
    VirtualKeys virtualKeys;
    const bool parseSuccess = Config::parseOptionValue(m_keyString[i], virtualKeys);
    
    if (parseSuccess && !virtualKeys.empty()) {
      // Check if the key combination is currently pressed (continuous press)
      const bool currentlyPressed = ImGUI::checkHotkeyState(virtualKeys, true);
      
      // Check if the key combination was just pressed this frame (single press)
      const bool justPressed = ImGUI::checkHotkeyState(virtualKeys, false);
      
      // Check if the key was clicked (pressed last frame, released this frame)
      const bool wasClicked = m_wasPressedLastFrame[i] && !currentlyPressed;
      
      // Update outputs
      m_isPressed[i] = currentlyPressed;
      m_wasJustPressed[i] = justPressed;
      m_wasClicked[i] = wasClicked;
      
      // Update state for next frame
      m_wasPressedLastFrame[i] = currentlyPressed;
    } else {
      ONCE(Logger::err(str::format("Failed to parse key string: '", m_keyString[i], "'")));
      // Invalid key string - default to false
      m_isPressed[i] = false;
      m_wasJustPressed[i] = false;
      m_wasClicked[i] = false;
      m_wasPressedLastFrame[i] = false;
    }
  }
}

}  // namespace components
}  // namespace dxvk
