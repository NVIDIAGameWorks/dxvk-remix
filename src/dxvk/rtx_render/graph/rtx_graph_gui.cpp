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

#include "rtx_graph_gui.h"
#include "../../imgui/imgui.h"
#include "../rtx_scene_manager.h"
#include "../rtx_context.h"
#include "rtx_graph_instance.h"
#include "rtx_graph_batch.h"
#include "../../util/util_string.h"
#include <algorithm>
#include <sstream>
#include <iomanip>

namespace dxvk {

void RtxGraphGUI::showGraphVisualization(const Rc<DxvkContext>& ctx) {
  RtxContext* rtxContext = static_cast<RtxContext*>(ctx.ptr());
  const SceneManager& sceneManager = rtxContext->getSceneManager();

  ImGui::Text("RTX Graph Visualization");
  ImGui::Separator();

  // Graph selector
  showGraphSelector(sceneManager);

  if (m_selectedInstanceId != kInvalidInstanceId) {
    ImGui::Separator();
    
    // Update graph data
    updateGraphData(sceneManager);
    
    // Component list
    showComponentList();
  }
}

void RtxGraphGUI::showGraphSelector(const SceneManager& sceneManager) {
  ImGui::Text("Select Graph Instance:");
  
  // Get graph manager and available instances
  const GraphManager& graphManager = sceneManager.getGraphManager();
  const auto& graphInstances = graphManager.getGraphInstances();
  
  if (graphInstances.empty()) {
    ImGui::Text("No graph instances available");
    return;
  }
  
  // Create dropdown with actual graph instances
  static int selectedIndex = -1;
  std::vector<std::string> instanceNames;
  std::vector<uint64_t> instanceIds;
  
  for (const auto& [instanceId, graphInstance] : graphInstances) {
    std::string instanceName = extractGraphInstanceName(graphManager, graphInstance);
    instanceNames.push_back(instanceName);
    instanceIds.push_back(instanceId);
  }
  
  // Convert to const char* array for ImGui
  std::vector<const char*> instanceNamesCStr;
  for (const auto& name : instanceNames) {
    instanceNamesCStr.push_back(name.c_str());
  }
  
  // Clamp selectedIndex to prevent undefined behavior if instances were removed
  selectedIndex = std::clamp(selectedIndex, -1, static_cast<int>(instanceNamesCStr.size()) - 1);
  
  if (ImGui::Combo("##GraphInstance", &selectedIndex, instanceNamesCStr.data(), static_cast<int>(instanceNamesCStr.size()))) {
    if (selectedIndex >= 0 && selectedIndex < static_cast<int>(instanceIds.size())) {
      m_selectedInstanceId = instanceIds[selectedIndex];
      m_components.clear();
    }
  }
  
  if (selectedIndex < 0) {
    ImGui::Text("No graph instance selected");
  }
}

void RtxGraphGUI::showComponentList() {
  ImGui::Text("Components:");
  ImGui::Separator();
  
  if (m_components.empty()) {
    ImGui::Text("No components in selected graph instance");
    return;
  }
  
  // Show components in a scrollable list
  ImGui::BeginChild("ComponentList", ImVec2(0, 400), true);
  
  for (size_t i = 0; i < m_components.size(); ++i) {
    const auto& component = m_components[i];
    
    // Component header with collapsible tree node
    std::string headerText = component.typeName;
    if (!component.uiName.empty() && component.uiName != component.typeName) {
      headerText += " (" + component.uiName + ")";
    }
    
    if (ImGui::CollapsingHeader(headerText.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
      // Show component description as tooltip on header hover
      if (ImGui::IsItemHovered() && !component.docString.empty()) {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f); // Set wrap width
        ImGui::TextWrapped("%s", component.docString.c_str());
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
      }
      
      ImGui::Indent();
      
      // Show properties
      ImGui::Text("Properties:");
      for (const auto& prop : component.properties) {
        // Display property with name, value, and index
        std::string propText = " [" + std::to_string(prop.topologyIndex) + "] " + prop.name + ": " + prop.currentValue;
        ImGui::Text("%s", propText.c_str());
        
        // Show tooltip with doc string and property paths on hover
        if (ImGui::IsItemHovered() && (!prop.docString.empty() || !prop.propertyPaths.empty())) {
          ImGui::BeginTooltip();
          ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f); // Set wrap width
          
          // Show all property paths
          if (!prop.propertyPaths.empty()) {
            if (prop.propertyPaths.size() == 1) {
              ImGui::Text("Path: %s", prop.propertyPaths[0].c_str());
            } else {
              ImGui::Text("Paths:");
              for (const auto& path : prop.propertyPaths) {
                ImGui::Text("  %s", path.c_str());
              }
            }
            if (!prop.docString.empty()) {
              ImGui::Separator();
            }
          }
          
          // Show description
          if (!prop.docString.empty()) {
            ImGui::TextWrapped("%s", prop.docString.c_str());
          }
          
          ImGui::PopTextWrapPos();
          ImGui::EndTooltip();
        }
      }
      
      ImGui::Unindent();
      ImGui::Spacing();
    }
  }
  
  ImGui::EndChild();
}

void RtxGraphGUI::updateGraphData(const SceneManager& sceneManager) {
  // Clear existing data
  m_components.clear();
  
  if (m_selectedInstanceId == kInvalidInstanceId) {
    return;
  }
  
  // Get graph manager and find the selected instance
  const GraphManager& graphManager = sceneManager.getGraphManager();
  const auto& graphInstances = graphManager.getGraphInstances();
  
  auto instanceIt = graphInstances.find(m_selectedInstanceId);
  if (instanceIt == graphInstances.end()) {
    return;
  }
  
  const GraphInstance& graphInstance = instanceIt->second;
  
  // Get the batch for this graph instance
  const auto& batches = graphManager.getBatches();
  auto batchIt = batches.find(graphInstance.getGraphHash());
  if (batchIt == batches.end()) {
    return;
  }
  
  const RtGraphBatch& batch = batchIt->second;
  const auto& componentBatches = batch.getComponentBatches();
  const auto& topology = batch.getTopology();
  const auto& properties = batch.getProperties();
  
  // Create inverted map from topology index to all property paths that map to it
  std::unordered_map<size_t, std::vector<std::string>> indexToPathsMap;
  for (const auto& [path, index] : topology.propertyPathHashToIndexMap) {
    indexToPathsMap[index].push_back(path);
  }
  
  // Find the instance index for the selected graph instance
  size_t instanceIndex = 0;
  const auto& instances = batch.getInstances();
  for (size_t i = 0; i < instances.size(); ++i) {
    if (instances[i] && instances[i]->getId() == m_selectedInstanceId) {
      instanceIndex = i;
      break;
    }
  }
  
  // Extract component information from the component batches
  for (size_t componentIdx = 0; componentIdx < componentBatches.size(); ++componentIdx) {
    const auto& componentBatch = componentBatches[componentIdx];
    if (!componentBatch) continue;
    
    const RtComponentSpec* spec = componentBatch->getSpec();
    if (!spec) continue;
    
    ComponentInfo component;
    component.name = spec->name;
    component.typeName = spec->getClassName();
    component.uiName = spec->uiName;
    component.docString = spec->docString;
    
    // Get property indices for this component from topology
    if (componentIdx < topology.propertyIndices.size()) {
      const auto& propIndices = topology.propertyIndices[componentIdx];
      
      // Match property specs with their topology indices
      for (size_t propSpecIdx = 0; propSpecIdx < spec->properties.size() && propSpecIdx < propIndices.size(); ++propSpecIdx) {
        const auto& propSpec = spec->properties[propSpecIdx];
        size_t topologyIndex = propIndices[propSpecIdx];
        
        PropertyInfo propInfo;
        propInfo.name = propSpec.name;
        propInfo.topologyIndex = topologyIndex;
        propInfo.docString = propSpec.docString;
        
        // Get all property paths from inverted map
        auto pathsIt = indexToPathsMap.find(topologyIndex);
        if (pathsIt != indexToPathsMap.end()) {
          propInfo.propertyPaths = pathsIt->second;
        } else {
          propInfo.propertyPaths = {"Unknown path"};
        }
        
        // Get current value from the property vector
        if (topologyIndex < properties.size()) {
          const auto& propertyVector = properties[topologyIndex];
          
          // Extract the value for this instance
          std::visit([&](const auto& vec) {
            if (instanceIndex < vec.size()) {
              RtComponentPropertyValue value = vec[instanceIndex];
              propInfo.currentValue = formatPropertyValue(value, &propSpec);
            } else {
              propInfo.currentValue = "N/A";
            }
          }, propertyVector);
        } else {
          propInfo.currentValue = "N/A";
        }
        
        component.properties.push_back(propInfo);
      }
    }
    
    m_components.push_back(component);
  }
}


std::string RtxGraphGUI::extractGraphInstanceName(const GraphManager& graphManager, const GraphInstance& graphInstance) const {
  // Get the batch for this graph instance
  const auto& batches = graphManager.getBatches();
  auto batchIt = batches.find(graphInstance.getGraphHash());
  if (batchIt == batches.end()) {
    // Fallback to old naming scheme
    std::stringstream hashStream;
    hashStream << "0x" << std::hex << std::uppercase << graphInstance.getGraphHash();
    return "Instance " + std::to_string(graphInstance.getId()) + " (Hash: " + hashStream.str() + ")";
  }
  
  const RtGraphBatch& batch = batchIt->second;
  const auto& initialGraphState = graphInstance.getInitialGraphState();
  
  // Use the stored prim path from the topology
  if (initialGraphState.primPath.empty()) {
    // Fallback to old naming scheme
    std::stringstream hashStream;
    hashStream << "0x" << std::hex << std::uppercase << graphInstance.getGraphHash();
    return "Instance " + std::to_string(graphInstance.getId()) + " (Hash: " + hashStream.str() + ")";
  }
  
  // Extract the name from the prim path (between 3rd '/' and end)
  const std::string& primPath = initialGraphState.primPath;
  
  // Find the 3rd '/'
  size_t thirdSlash = 0;
  int slashCount = 0;
  for (size_t i = 0; i < primPath.length(); ++i) {
    if (primPath[i] == '/') {
      slashCount++;
      if (slashCount == 3) {
        thirdSlash = i + 1; // Position after the 3rd slash
        break;
      }
    }
  }
  
  // Extract the substring from after the 3rd slash to the end
  if (thirdSlash > 0 && thirdSlash < primPath.length()) {
    std::string fullName = primPath.substr(thirdSlash);
    
    // Truncate if too long for ImGui tooltip (keep first 14 chars and end)
    const size_t maxLength = 60; // Adjust this value as needed
    if (fullName.length() > maxLength) {
      const size_t prefixLength = 14;
      const size_t suffixLength = maxLength - prefixLength - 3; // 3 for "..."
      
      if (fullName.length() > prefixLength + suffixLength + 3) {
        std::string prefix = fullName.substr(0, prefixLength);
        std::string suffix = fullName.substr(fullName.length() - suffixLength);
        return prefix + "..." + suffix;
      }
    }
    
    return fullName;
  }
  
  // Fallback to old naming scheme
  std::stringstream hashStream;
  hashStream << "0x" << std::hex << std::uppercase << graphInstance.getGraphHash();
  return "Instance " + std::to_string(graphInstance.getId()) + " (Hash: " + hashStream.str() + ")";
}

std::string RtxGraphGUI::formatPropertyValue(const RtComponentPropertyValue& value, const RtComponentPropertySpec* propSpec) const {
  // First check if this is an enum property and we have a property spec
  if (propSpec && !propSpec->enumValues.empty()) {
    // For enum properties, try to find the display name for the current value
    for (const auto& [displayName, enumProp] : propSpec->enumValues) {
      if (enumProp.value == value) {
        return displayName;
      }
    }
    // If no match found, fall through to default formatting
  }
  
  return std::visit([](const auto& v) -> std::string {
    using T = std::decay_t<decltype(v)>;
    if constexpr (std::is_same_v<T, uint8_t>) {
      return v ? "true" : "false"; // bool stored as uint8_t
    } else if constexpr (std::is_same_v<T, float>) {
      return std::to_string(v);
    } else if constexpr (std::is_same_v<T, Vector2>) {
      return "(" + std::to_string(v.x) + ", " + std::to_string(v.y) + ")";
    } else if constexpr (std::is_same_v<T, Vector3>) {
      return "(" + std::to_string(v.x) + ", " + std::to_string(v.y) + ", " + std::to_string(v.z) + ")";
    } else if constexpr (std::is_same_v<T, Vector4>) {
      return "(" + std::to_string(v.x) + ", " + std::to_string(v.y) + ", " + std::to_string(v.z) + ", " + std::to_string(v.w) + ")";
    } else if constexpr (std::is_same_v<T, int32_t>) {
      return std::to_string(v);
    } else if constexpr (std::is_same_v<T, uint32_t>) {
      return std::to_string(v);
    } else if constexpr (std::is_same_v<T, uint64_t>) {
      std::stringstream ss;
      ss << "0x" << std::hex << std::uppercase << std::setw(16) << std::setfill('0') << v;
      return ss.str();
    } else if constexpr (std::is_same_v<T, std::string>) {
      return "\"" + v + "\"";
    } else {
      return "Unknown";
    }
  }, value);
}

} // namespace dxvk
