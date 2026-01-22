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

#include "imgui/imgui.h"
#include "rtx_graph_batch.h"
#include "rtx_graph_gui.h"
#include "rtx_graph_instance.h"
#include "rtx_render/rtx_context.h"
#include "rtx_render/rtx_imgui.h"
#include "rtx_render/rtx_scene_manager.h"
#include "../util/util_string.h"
#include <algorithm>
#include <iomanip>
#include <sstream>

namespace dxvk {

void RtxGraphGUI::showGraphVisualization(const Rc<DxvkContext>& ctx) {
  RtxContext* rtxContext = static_cast<RtxContext*>(ctx.ptr());
  const SceneManager& sceneManager = rtxContext->getSceneManager();
  RemixGui::Separator();
  RemixGui::Checkbox("Enable", &GraphManager::enableObject());
  ImGui::SameLine(0.0f, 20.f);
  RemixGui::Checkbox("Pause", &GraphManager::pauseGraphUpdatesObject());
  ImGui::SameLine(0.0f, 20.f);
  if (IMGUI_ADD_TOOLTIP(ImGui::Button("Reset Graph State"), "Destroys then recreates all graphs, clearing any stored state.")) {
    const GraphManager& graphManager = sceneManager.getGraphManager();
    graphManager.resetGraphState();
  }

  // Graph selector
  showGraphSelector(sceneManager);

  if (m_selectedInstanceId != kInvalidInstanceId) {
    // Update graph data
    updateGraphData(sceneManager);
    
    // Component list
    showComponentList();
  }
}

void RtxGraphGUI::showGraphSelector(const SceneManager& sceneManager) {
  if (RemixGui::CollapsingHeader("Select Graph Instance:", ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::Indent();
    // Get graph manager and available instances
    const GraphManager& graphManager = sceneManager.getGraphManager();
    const auto& graphInstances = graphManager.getGraphInstances();
    
    if (graphInstances.empty()) {
      ImGui::Text("No graph instances available");
      ImGui::Unindent();

      return;
    }
    
    // Filter input
    ImGui::Text("Filter:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1);
    ImGui::InputText("##InstanceFilter", m_instanceFilter.data(), m_instanceFilter.size());
    
    // Build list of instances with names
    std::vector<std::pair<std::string, uint64_t>> instanceList;
    for (const auto& [instanceId, graphInstance] : graphInstances) {
      std::string instanceName = extractGraphInstanceName(graphManager, graphInstance);
      instanceList.push_back({instanceName, instanceId});
    }
    
    // Filter instances based on filter string (case-insensitive)
    std::vector<std::pair<std::string, uint64_t>> filteredInstances;
    std::string filterLower(m_instanceFilter.data());
    std::transform(filterLower.begin(), filterLower.end(), filterLower.begin(), 
        [](unsigned char c) { return std::tolower(c); });
    
    for (const auto& [name, id] : instanceList) {
      if (filterLower.empty()) {
        // No filter - show all
        filteredInstances.push_back({name, id});
      } else {
        // Check if name contains filter string (case-insensitive)
        std::string nameLower = name;
        std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(), 
            [](unsigned char c) { return std::tolower(c); });
        if (nameLower.find(filterLower) != std::string::npos) {
          filteredInstances.push_back({name, id});
        }
      }
    }
    
    // Display filtered instances in a list box
    ImGui::BeginChild("InstanceList", ImVec2(0, 200), true, ImGuiWindowFlags_AlwaysVerticalScrollbar);
    
    if (filteredInstances.empty()) {
      ImGui::Text("No matching instances");
    } else {
      for (const auto& [name, id] : filteredInstances) {
        bool isSelected = (m_selectedInstanceId == id);
        if (ImGui::Selectable(name.c_str(), isSelected)) {
          m_selectedInstanceId = id;
        }
      }
    }
    
    ImGui::EndChild();
    
    // Show current selection
    if (m_selectedInstanceId == kInvalidInstanceId) {
      ImGui::Text("No graph instance selected");
    } else {
      // Find and display the selected instance name
      for (const auto& [name, id] : instanceList) {
        if (id == m_selectedInstanceId) {
          ImGui::Text("Selected: %s", name.c_str());
          if (IMGUI_ADD_TOOLTIP(ImGui::Button("Reset Instance"), "Destroys then recreates this graph instance, clearing any stored state.")) {
            graphManager.queueInstanceReset(m_selectedInstanceId);
            m_selectedInstanceId = kInvalidInstanceId; // Clear selection since instance will be removed
          }
          break;
        }
      }
    }
    ImGui::Unindent();
  }
}

void RtxGraphGUI::showComponentList() {
  if (m_components.empty()) {
    if (m_selectedGraphIsEmpty) {
      ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.3f, 1.0f), "Empty graph - no valid components found");
      ImGui::TextWrapped("This graph has no components. This can happen when all components "
                         "failed to load, are unsupported, or were filtered out.");
    }
    ImGui::Text("No graph selected.");
    return;
  }
  if (ImGui::CollapsingHeader("Components:", ImGuiTreeNodeFlags_DefaultOpen)) {
    // Show components in a scrollable list with resizable height
    ImGui::BeginChild("ComponentList", ImVec2(0, m_componentListHeight), true, ImGuiWindowFlags_AlwaysVerticalScrollbar);
    
    for (size_t i = 0; i < m_components.size(); ++i) {
      const auto& component = m_components[i];
      
      // Component header with collapsible tree node.  Included the index so each section has a unique name.
      std::string headerText = component.typeName + " (" + std::to_string(i) + ")";
      
      if (ImGui::CollapsingHeader(headerText.c_str())) {
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

  }
  
  ImGui::EndChild();
  
  // Add a draggable splitter to resize the component list
  ImGui::Button("##splitter", ImVec2(-1, 8.0f));
  if (ImGui::IsItemActive()) {
    m_componentListHeight += ImGui::GetIO().MouseDelta.y;
    // Clamp to reasonable values
    m_componentListHeight = std::clamp(m_componentListHeight, 100.0f, 1000.0f);
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
  }
}

void RtxGraphGUI::updateGraphData(const SceneManager& sceneManager) {
  // Clear existing data
  m_components.clear();
  m_selectedGraphIsEmpty = false;
  
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
  
  // Handle empty graphs gracefully - they have no components to display
  if (batch.isEmpty()) {
    m_selectedGraphIsEmpty = true;
    return;
  }
  
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
    component.typeName = spec->uiName.empty() ? spec->getClassName() : spec->uiName;
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
    
    // Append instance ID
    std::string instanceSuffix = " (" + std::to_string(graphInstance.getId()) + ")";
    
    // Truncate if too long for ImGui tooltip (keep first 14 chars and end)
    const size_t maxLength = 60; // Adjust this value as needed
    if (fullName.length() > maxLength) {
      const size_t prefixLength = 14;
      const size_t suffixLength = maxLength - prefixLength - 3; // 3 for "..."
      
      if (fullName.length() > prefixLength + suffixLength + 3) {
        std::string prefix = fullName.substr(0, prefixLength);
        std::string suffix = fullName.substr(fullName.length() - suffixLength);
        return prefix + "..." + suffix + instanceSuffix;
      }
    }
    
    return fullName + instanceSuffix;
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
  
  return std::visit([propSpec](const auto& v) -> std::string {
    using T = std::decay_t<decltype(v)>;
    if constexpr (std::is_same_v<T, uint32_t>) {
      // uint32_t is used for both Bool and Enum types - check propSpec to differentiate
      if (propSpec && propSpec->type == RtComponentPropertyType::Bool) {
        return v ? "true" : "false";
      } else {
        return std::to_string(v);
      }
    } else if constexpr (std::is_same_v<T, float>) {
      return std::to_string(v);
    } else if constexpr (std::is_same_v<T, Vector2>) {
      return "(" + std::to_string(v.x) + ", " + std::to_string(v.y) + ")";
    } else if constexpr (std::is_same_v<T, Vector3>) {
      return "(" + std::to_string(v.x) + ", " + std::to_string(v.y) + ", " + std::to_string(v.z) + ")";
    } else if constexpr (std::is_same_v<T, Vector4>) {
      return "(" + std::to_string(v.x) + ", " + std::to_string(v.y) + ", " + std::to_string(v.z) + ", " + std::to_string(v.w) + ")";
    } else if constexpr (std::is_same_v<T, uint64_t>) {
      std::stringstream ss;
      ss << "0x" << std::hex << std::uppercase << std::setw(16) << std::setfill('0') << v;
      return ss.str();
    } else if constexpr (std::is_same_v<T, std::string>) {
      return "\"" + v + "\"";
    } else if constexpr (std::is_same_v<T, PrimTarget>) {
      if (v.instanceId == kInvalidInstanceId) {
        return "Invalid instance";
      } else if (v.replacementIndex == ReplacementInstance::kInvalidReplacementIndex) {
        return "Invalid replacement index";
      } else {
        return "instance: " + std::to_string(v.instanceId) + ", index: " + std::to_string(v.replacementIndex);
      }
    } else {
      return "Unknown";
    }
  }, value);
}

} // namespace dxvk
