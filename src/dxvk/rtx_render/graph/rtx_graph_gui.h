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

#include "rtx_graph_types.h"
#include <unordered_map>
#include <vector>
#include <string>
#include <array>
#include <limits>

namespace dxvk {

// Forward declarations
class DxvkContext;
class SceneManager;
class GraphManager;

class RtxGraphGUI {
public:
  RtxGraphGUI() = default;
  ~RtxGraphGUI() = default;

  // Main function to show the graph visualization section
  void showGraphVisualization(const Rc<DxvkContext>& ctx);

private:
  struct PropertyInfo {
    std::string name;
    std::string currentValue;
    size_t topologyIndex;
    std::string docString;
    std::vector<std::string> propertyPaths;
  };

  struct ComponentInfo {
    std::string name;
    std::string typeName;
    std::string docString;
    std::vector<PropertyInfo> properties;
  };

  // Graph visualization state
  static constexpr uint64_t kInvalidInstanceId = std::numeric_limits<uint64_t>::max();
  static constexpr float kDefaultComponentListHeight = 400.0f;
  uint64_t m_selectedInstanceId = kInvalidInstanceId;
  std::vector<ComponentInfo> m_components;
  std::array<char, 256> m_instanceFilter = {};
  float m_componentListHeight = kDefaultComponentListHeight;

  // Helper functions
  void showGraphSelector(const SceneManager& sceneManager);
  void showComponentList();
  void updateGraphData(const SceneManager& sceneManager);
  std::string formatPropertyValue(const RtComponentPropertyValue& value, const RtComponentPropertySpec* propSpec = nullptr) const;
  std::string extractGraphInstanceName(const GraphManager& graphManager, const GraphInstance& graphInstance) const;
};

} // namespace dxvk
