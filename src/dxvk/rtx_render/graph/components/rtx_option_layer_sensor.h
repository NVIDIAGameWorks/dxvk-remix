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
  X(RtComponentPropertyType::AssetPath, "", configPath, "Config Path", "The config file for the RtxOptionLayer to read.") \
  X(RtComponentPropertyType::Float, kDefaultComponentRtxOptionLayerPriority, priority, "Priority", "The priority for the option layer. Numbers are rounded to the nearest positive integer. Higher values are blended on top of lower values. If multiple layers share the same priority, they are ordered alphabetically by config path.", property.minValue = RtxOptionLayer::s_userOptionLayerOffset + 1, property.maxValue = kMaxComponentRtxOptionLayerPriority, property.optional = true)

#define LIST_STATES(X)

#define LIST_OUTPUTS(X) \
  X(RtComponentPropertyType::Bool, false, isEnabled, "Is Enabled", "True if the option layer is currently enabled.") \
  X(RtComponentPropertyType::Float, 0.0f, blendStrength, "Blend Strength", "The current blend strength of the option layer (0.0 = no effect, 1.0 = full effect).") \
  X(RtComponentPropertyType::Float, 0.0f, blendThreshold, "Blend Threshold", "The current blend threshold for non-float options (0.0 to 1.0).")

REMIX_COMPONENT( \
  /* the Component name */ RtxOptionLayerSensor, \
  /* the UI name */        "Rtx Option Layer Sensor", \
  /* the UI categories */  "Sense", \
  /* the doc string */     "Reads the state of a configuration layer.\n\n" \
    "Outputs whether a given RtxOptionLayer is enabled, along with its blend strength and threshold values. " \
    "This can be used to create logic that responds to the state of configuration layers.", \
  /* the version number */ 1, \
  LIST_INPUTS, LIST_STATES, LIST_OUTPUTS);

#undef LIST_INPUTS
#undef LIST_STATES
#undef LIST_OUTPUTS

void RtxOptionLayerSensor::updateRange(const Rc<DxvkContext>& context, const size_t start, const size_t end) {
  for (size_t i = start; i < end; i++) {
    // Default values
    bool isEnabled = false;
    float blendStrength = 0.0f;
    float blendThreshold = 0.0f;
    
    // Look up the layer
    if (!m_configPath[i].empty()) {
      uint32_t priority = getRtxOptionLayerComponentClampedPriority(m_priority[i]);
      
      const RtxOptionLayer* layer = RtxOptionLayerManager::lookupLayer(m_configPath[i], priority);
      
      if (layer != nullptr) {
        isEnabled = layer->isEnabled();
        blendStrength = layer->getPendingBlendStrength();
        blendThreshold = layer->getPendingBlendThreshold();
      }
    }
    
    m_isEnabled[i] = isEnabled;
    m_blendStrength[i] = blendStrength;
    m_blendThreshold[i] = blendThreshold;
  }
}

}  // namespace components
}  // namespace dxvk

