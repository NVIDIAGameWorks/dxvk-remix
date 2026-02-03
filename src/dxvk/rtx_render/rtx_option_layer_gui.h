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

#include <string>
#include <vector>

namespace dxvk {

  class RtxOptionLayer;
  enum class OptionType;

  // Utilities for querying and displaying option layer state
  class OptionLayerUI {
  public:
    // Options for rendering state to ImGui
    struct RenderOptions {
      bool showUnchanged = false; // Show unchanged options (normal color)
      const char* uniqueId = nullptr; // Unique ID for the child window (required if called multiple times per frame)
      std::string filter;         // Optional filter string (case-insensitive)
    };
    
    // Render layer's changes to a plain string (for tooltips, logging)
    // Only renders changed options (added, modified, removed)
    static std::string renderToString(const RtxOptionLayer* layer, const char* layerName);
    
    // Render layer's state to ImGui with colored text
    static void renderToImGui(const RtxOptionLayer* layer, const RenderOptions& options = {});
    
    // Convenience: display all options in a layer with optional filtering
    // Shows unsaved status for layers with config files
    static void displayContents(const RtxOptionLayer& optionLayer, const std::string& filterLower);
    
    // Render action buttons for a saveable layer: Save, Reload, Reset, Clean
    // idSuffix is used to make button IDs unique (e.g., "RtxConf", "User")
    // Returns true if any button was clicked
    static bool renderLayerButtons(RtxOptionLayer* layer, const char* idSuffix, float buttonWidth = 0);
  };

} // namespace dxvk

