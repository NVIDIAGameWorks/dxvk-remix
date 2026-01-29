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
#include "../../rtx_option_layer_manager.h"
#include "rtx_option_layer_constants.h"

namespace dxvk {
namespace components {


#define LIST_INPUTS(X) \
  X(RtComponentPropertyType::AssetPath, "", configPath, "Config Path", "The config file for the RtxOptionLayer to control.") \
  X(RtComponentPropertyType::Bool, true, enabled, "Enabled", "If true, the option layer is enabled and its settings are applied. If false, the layer is disabled. If multiple components control the same layer, it will be enabled if ANY of them request it.", property.optional = true) \
  X(RtComponentPropertyType::Float, 1.0f, blendStrength, "Blend Strength", "The blend strength for the option layer (0.0 = no effect, 1.0 = full effect.)" \
      "\n\nLowest priority layer uses LERP to blend with default value, then each higher priority layer uses LERP to blend with the previous layer's result." \
      "\n\nIf multiple components control the same layer, the MAX blend strength will be used.", property.minValue = 0.0f, property.maxValue = 1.0f, property.optional = true) \
  X(RtComponentPropertyType::Float, 0.1f, blendThreshold, "Blend Threshold", \
      "The blend threshold for non-float options (0.0 to 1.0). Non-float options are only applied when blend strength exceeds this threshold." \
      " If multiple components control the same layer, the MINIMUM blend threshold will be used.", property.minValue = 0.0f, property.maxValue = 1.0f, property.optional = true) \
  X(RtComponentPropertyType::Float, kDefaultComponentRtxOptionLayerPriority, priority, "Priority", \
      "The priority for the option layer. Numbers are rounded to the nearest positive integer. Higher values are blended on top of lower values." \
      " If two components specify the same priority but different config paths, the layers will be prioritized alphabetically (a.conf will override values from z.conf).", \
      property.minValue = RtxOptionLayer::s_userOptionLayerOffset + 1, property.maxValue = kMaxComponentRtxOptionLayerPriority, property.optional = true)

#define LIST_STATES(X) \
  X(RtComponentPropertyType::Bool, false, holdsReference, "", "True if the component is holding a reference to the RtxOptionLayer.") \
  X(RtComponentPropertyType::AssetPath, "", cachedConfigPath, "", "Cached config path from when the layer was acquired.") \
  X(RtComponentPropertyType::Float, 0.0f, cachedPriority, "", "Cached priority from when the layer was acquired.")

#define LIST_OUTPUTS(X)

// Manually declaring the class to allow for custom initialize and cleanup methods
class RtxOptionLayerAction : public RtRegisteredComponentBatch<RtxOptionLayerAction> {
private:
  REMIX_COMPONENT_GENERATE_PROP_TYPES(LIST_INPUTS, LIST_STATES, LIST_OUTPUTS)
  REMIX_COMPONENT_BODY(
    /* the Component class name */ RtxOptionLayerAction,
    /* the UI name */        "Rtx Option Layer Action",
    /* the UI categories */  "Act",
    /* the doc string */     "Activates and controls configuration layers at runtime based on game conditions.\n\n"
      "Controls an RtxOptionLayer by name, allowing dynamic enable/disable, strength adjustment, and threshold control. "
      "This can be used to activate configuration layers at runtime based on game state or other conditions.\n\n"
      "The layer is created if it doesn't exist, and managed with reference counting.\n"
      "If two components specify the same priority and config path, they will both control the same layer (for enabled components, uses the MAX of the blend strengths and the MIN of the blend thresholds).\n"
      "If two components specify the same priority but different config paths, the layers will be prioritized alphabetically (a.conf will override values from z.conf).",
    /* the version number */ 1,
    LIST_INPUTS, LIST_STATES, LIST_OUTPUTS,
    /* optional arguments: */
    spec.initialize = initialize; // Initialize callback to create or find option layers
    spec.cleanup = cleanup; // Cleanup callback to clear cached pointers when instances are destroyed
  )
  void updateRange(const Rc<DxvkContext>& context, const size_t start, const size_t end) final;
  
  // Need to wrap the optional functions in static methods to cast the batch to the correct type.
  static void initialize(const Rc<DxvkContext>& context, RtComponentBatch& batch, const size_t index) {
    static_cast<RtxOptionLayerAction&>(batch).initializeInstance(context, index);
  }
  static void cleanup(RtComponentBatch& batch, const size_t index) {
    static_cast<RtxOptionLayerAction&>(batch).cleanupInstance(index);
  }
  void initializeInstance(const Rc<DxvkContext>& context, const size_t index);
  void cleanupInstance(const size_t index);

  uint32_t getPriority(const float originalPriority) const {
    // TODO if the priority is left unset, we need to automatically assign a priority.
    return getRtxOptionLayerComponentClampedPriority(originalPriority);
  }
};

#undef LIST_INPUTS
#undef LIST_STATES
#undef LIST_OUTPUTS

void RtxOptionLayerAction::initializeInstance(const Rc<DxvkContext>& context, const size_t index) {
  if (m_configPath[index].empty()) {
    m_holdsReference[index] = false;
    return;
  }
  
  // Acquire layer through the manager
  const RtxOptionLayer* layer = RtxOptionLayerManager::acquireLayer(
    m_configPath[index],
    getPriority(m_priority[index]),
    1.0f,  // Default blend strength (will be updated in updateRange)
    0.1f   // Default blend threshold (will be updated in updateRange)
  );
  if (layer != nullptr) {
    m_holdsReference[index] = true;
    // Cache the values used to acquire this layer
    m_cachedConfigPath[index] = m_configPath[index];
    m_cachedPriority[index] = m_priority[index];
  } else {
    Logger::err(str::format("RtxOptionLayerAction: Failed to acquire layer '", m_configPath[index], "' with priority ", m_priority[index], "."));
    m_holdsReference[index] = false;
  }
}

void RtxOptionLayerAction::cleanupInstance(const size_t index) {
  if (!m_holdsReference[index]) {
    return;
  }
  // Release the layer through the manager
  RtxOptionLayerManager::releaseLayer(m_cachedConfigPath[index], getPriority(m_cachedPriority[index]));
  m_holdsReference[index] = false;
}

void RtxOptionLayerAction::updateRange(const Rc<DxvkContext>& context, const size_t start, const size_t end) {
  for (size_t i = start; i < end; i++) {
    // Check if configPath or priority has changed
    if (m_holdsReference[i]) {
      const bool configPathChanged = (m_configPath[i] != m_cachedConfigPath[i]);
      const bool priorityChanged = (m_priority[i] != m_cachedPriority[i]);
      
      if (configPathChanged || priorityChanged) {
        cleanupInstance(i);
        initializeInstance(context, i);
      }
    }
    
    if (!m_holdsReference[i]) {
      continue;
    }

    // Get cached layer pointer
    RtxOptionLayer* layer = RtxOptionLayerManager::lookupLayer(m_cachedConfigPath[i], getPriority(m_cachedPriority[i]));
    
    // Skip if no layer (empty config name or failed creation)
    if (layer == nullptr) {
      continue;
    }
    
    // Request enabled state for this frame
    // If multiple components control this layer, it will be enabled if ANY of them request it
    layer->requestEnabled(m_enabled[i]);

    if (m_enabled[i]) {
      // Request blend strength for this frame
      // If multiple components control this layer, the MAX blend strength will be used
      const float targetStrength = std::clamp(m_blendStrength[i], 0.0f, 1.0f);
      layer->requestBlendStrength(targetStrength);
      
      // Request blend threshold for this frame
      // If multiple components control this layer, the MIN blend threshold will be used
      const float targetThreshold = std::clamp(m_blendThreshold[i], 0.0f, 1.0f);
      layer->requestBlendThreshold(targetThreshold);
    }
  }
}

}  // namespace components
}  // namespace dxvk
