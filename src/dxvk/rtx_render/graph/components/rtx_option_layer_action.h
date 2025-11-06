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

namespace dxvk {
namespace components {

static constexpr uint32_t kMaxComponentRtxOptionLayerPriority = dxvk::RtxOptionLayer::s_runtimeOptionLayerPriority - 1;
static constexpr uint32_t kMinComponentRtxOptionLayerPriority = RtxOptionLayer::s_userOptionLayerOffset + 1;
static constexpr uint32_t kDefaultComponentRtxOptionLayerPriority = 10000;


#define LIST_INPUTS(X) \
  X(RtComponentPropertyType::AssetPath, "", configPath, "Config Path", "The config file for the RtxOptionLayer to control.") \
  X(RtComponentPropertyType::Bool, true, enabled, "Enabled", "If true, the option layer is enabled and its settings are applied. If false, the layer is disabled. If multiple components control the same layer, it will be enabled if ANY of them request it.", property.optional = true) \
  X(RtComponentPropertyType::Float, 1.0f, blendStrength, "Blend Strength", "The blend strength for the option layer (0.0 = no effect, 1.0 = full effect.)" \
      "\n\nLowest priority layer uses LERP to blend with default value, then each higher priority layer uses LERP to blend with the previous layer's result." \
      "\n\nIf multiple components control the same layer, the MAX blend strength will be used.", property.minValue = 0.0f, property.maxValue = 1.0f, property.optional = true) \
  X(RtComponentPropertyType::Float, 0.1f, blendThreshold, "Blend Threshold", "The blend threshold for non-float options (0.0 to 1.0). Non-float options are only applied when blend strength exceeds this threshold. If multiple components control the same layer, the MINIMUM blend threshold will be used.", property.minValue = 0.0f, property.maxValue = 1.0f, property.optional = true) \
  X(RtComponentPropertyType::Uint32, kDefaultComponentRtxOptionLayerPriority, priority, "Priority", "The priority for the option layer. Higher values are blended onto lower values. Must be unique across all layers.", property.minValue = RtxOptionLayer::s_userOptionLayerOffset + 1, property.maxValue = kMaxComponentRtxOptionLayerPriority, property.optional = true)

#define LIST_STATES(X) \
  X(RtComponentPropertyType::Uint64, 0, cachedLayerPtr, "", "Cached pointer to the RtxOptionLayer (internal use).")

#define LIST_OUTPUTS(X)

// Manually declaring the class to allow for cleanup method
class RtxOptionLayerAction : public RtRegisteredComponentBatch<RtxOptionLayerAction> {
private:
  REMIX_COMPONENT_WRITE_CLASS_MEMBERS(LIST_INPUTS, LIST_STATES, LIST_OUTPUTS)
public:
  REMIX_COMPONENT_WRITE_CTOR(RtxOptionLayerAction, LIST_INPUTS, LIST_STATES, LIST_OUTPUTS)
  static const RtComponentSpec* getStaticSpec() {
    REMIX_COMPONENT_WRITE_STATIC_SPEC(
      /* the Component name */ RtxOptionLayerAction,
      /* the UI name */        "Rtx Option Layer Action",
      /* the UI categories */  "Act",
      /* the doc string */     "Controls an RtxOptionLayer by name, allowing dynamic enable/disable, strength adjustment, and threshold control. "
        "This can be used to activate configuration layers at runtime based on game state or other conditions. "
        "The layer is created if it doesn't exist, and managed with reference counting. "
        "Each layer requires a unique priority value - if multiple components specify the same priority, an error will occur.",
      /* the version number */ 1,
      LIST_INPUTS, LIST_STATES, LIST_OUTPUTS,
      
      // Initialize callback to create or find option layers
      spec.initialize = [](const Rc<DxvkContext>& context, RtComponentBatch& batch, const size_t index) {
        static_cast<RtxOptionLayerAction&>(batch).initializeInstance(context, index);
      };
      
      // Cleanup callback to clear cached pointers when instances are destroyed
      spec.cleanup = [](RtComponentBatch& batch, const size_t index) {
        static_cast<RtxOptionLayerAction&>(batch).cleanupInstance(index);
      };
    )
  }
  
  void initializeInstance(const Rc<DxvkContext>& context, const size_t index);
  void updateRange(const Rc<DxvkContext>& context, const size_t start, const size_t end) final;
  void cleanupInstance(const size_t index);
};

#undef LIST_INPUTS
#undef LIST_STATES
#undef LIST_OUTPUTS

void RtxOptionLayerAction::initializeInstance(const Rc<DxvkContext>& context, const size_t index) {
  uint32_t priority = m_priority[index];
  clamp(priority, kMinComponentRtxOptionLayerPriority, kMaxComponentRtxOptionLayerPriority);

  // TODO if the priority is left unset, we need to automatically assign a priority.

  // Acquire layer through the manager
  const ::dxvk::RtxOptionLayer* layer = dxvk::RtxOptionLayerManager::acquireLayer(
    m_configPath[index],
    priority,
    1.0f,  // Default blend strength (will be updated in updateRange)
    0.1f   // Default blend threshold (will be updated in updateRange)
  );
  
  if (layer != nullptr) {
    m_cachedLayerPtr[index] = reinterpret_cast<uint64_t>(layer);
  } else {
    Logger::err(str::format("RtxOptionLayerAction: Failed to acquire layer '", m_configPath[index], "' with priority ", m_priority[index], "."));
    m_cachedLayerPtr[index] = 0;
  }
}

void RtxOptionLayerAction::cleanupInstance(const size_t index) {
  // Release the layer through the manager
  dxvk::RtxOptionLayer* layer = reinterpret_cast<dxvk::RtxOptionLayer*>(m_cachedLayerPtr[index]);
  if (m_cachedLayerPtr[index] != 0 && layer != nullptr) {
    dxvk::RtxOptionLayerManager::releaseLayer(layer);
  }
  m_cachedLayerPtr[index] = 0;
}

void RtxOptionLayerAction::updateRange(const Rc<DxvkContext>& context, const size_t start, const size_t end) {
  for (size_t i = start; i < end; i++) {
    // Get cached layer pointer
    dxvk::RtxOptionLayer* cachedLayer = reinterpret_cast<dxvk::RtxOptionLayer*>(m_cachedLayerPtr[i]);
    
    // Skip if no layer (empty config name or failed creation)
    if (m_cachedLayerPtr[i] == 0 || cachedLayer == nullptr) {
      continue;
    }
    
    // Request enabled state for this frame
    // If multiple components control this layer, it will be enabled if ANY of them request it
    cachedLayer->requestEnabled(m_enabled[i]);

    if (m_enabled[i]) {
      // Request blend strength for this frame
      // If multiple components control this layer, the MAX blend strength will be used
      const float targetStrength = std::clamp(m_blendStrength[i], 0.0f, 1.0f);
      cachedLayer->requestBlendStrength(targetStrength);
      
      // Request blend threshold for this frame
      // If multiple components control this layer, the MIN blend threshold will be used
      const float targetThreshold = std::clamp(m_blendThreshold[i], 0.0f, 1.0f);
      cachedLayer->requestBlendThreshold(targetThreshold);
    }
  }
}

}  // namespace components
}  // namespace dxvk
