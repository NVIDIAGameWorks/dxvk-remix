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
#include "rtx_option_layer_manager.h"
#include "../util/log/log.h"
#include "../util/util_string.h"

namespace dxvk {

std::mutex RtxOptionLayerManager::s_mutex;
std::unordered_map<uint32_t, const RtxOptionLayer*> RtxOptionLayerManager::s_priorityToLayer;

const RtxOptionLayer* RtxOptionLayerManager::acquireLayer(const std::string& configPath, uint32_t priority, float blendStrength, float blendThreshold) {
  if (configPath.empty()) {
    return nullptr;
  }
  
  std::lock_guard<std::mutex> lock(s_mutex);
  
  
  // Check if a layer with this priority already exists
  auto priorityIt = s_priorityToLayer.find(priority);
  if (priorityIt != s_priorityToLayer.end()) {
    const RtxOptionLayer* existingLayer = priorityIt->second;
    
    // Verify it's for the same config path
    if (existingLayer->getName() != configPath) {
      Logger::err(str::format("RtxOptionLayerManager: Priority ", priority, " is already in use by '", 
                              existingLayer->getName(), "'. Cannot create layer for '", configPath, "'."));
      return nullptr;
    }
    
    // Same config and priority - increment reference count
    existingLayer->incrementRefCount();
    return existingLayer;
  }
  
  // Create new layer
  const RtxOptionLayer* newLayer = RtxOptionImpl::addRtxOptionLayer(configPath, priority, false, blendStrength, blendThreshold);
  
  if (newLayer == nullptr) {
    Logger::err(str::format("RtxOptionLayerManager: Failed to create layer for '", configPath, "' with priority ", priority, "."));
    return nullptr;
  }
  
  // Initialize reference count and store in priority map
  newLayer->incrementRefCount();
  s_priorityToLayer[newLayer->getPriority()] = newLayer;
  
  return newLayer;
}

void RtxOptionLayerManager::releaseLayer(const RtxOptionLayer* layer) {
  if (layer == nullptr) {
    return;
  }
  
  std::lock_guard<std::mutex> lock(s_mutex);
  
  // Verify this layer is in our map
  auto priorityIt = s_priorityToLayer.find(layer->getPriority());
  if (priorityIt == s_priorityToLayer.end() || priorityIt->second != layer) {
    Logger::warn("RtxOptionLayerManager: Attempted to release unknown layer.");
    return;
  }
  
  if (layer->getRefCount() == 0) {
    Logger::warn(str::format("RtxOptionLayerManager: Layer '", layer->getName(), "' (priority: ", layer->getPriority(), ") already has zero references."));
    return;
  }
  
  layer->decrementRefCount();
  
  // If reference count reached zero, remove the layer
  if (layer->getRefCount() == 0) {
    const uint32_t layerPriority = layer->getPriority();
    const std::string configPath = layer->getName();
    
    // Remove from priority map first
    s_priorityToLayer.erase(layerPriority);
    
    // Remove from RtxOption system
    bool removed = RtxOptionImpl::removeRtxOptionLayer(layer);
    
    if (!removed) {
      Logger::warn(str::format("RtxOptionLayerManager: Failed to remove layer '", configPath, "' from RtxOption system."));
    }
  }
}

const RtxOptionLayer* RtxOptionLayerManager::findLayerByPriority(uint32_t priority) {
  std::lock_guard<std::mutex> lock(s_mutex);
  
  auto it = s_priorityToLayer.find(priority);
  
  if (it != s_priorityToLayer.end()) {
    return it->second;
  }
  
  return nullptr;
}

size_t RtxOptionLayerManager::getReferenceCount(const RtxOptionLayer* layer) {
  if (layer == nullptr) {
    return 0;
  }
  
  std::lock_guard<std::mutex> lock(s_mutex);
  
  return layer->getRefCount();
}

}  // namespace dxvk

