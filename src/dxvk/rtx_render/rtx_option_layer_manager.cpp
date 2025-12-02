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

const RtxOptionLayer* RtxOptionLayerManager::acquireLayer(const std::string& configPath, uint32_t priority, float blendStrength, float blendThreshold) {
  if (configPath.empty()) {
    return nullptr;
  }
  
  std::lock_guard<std::mutex> lock(s_mutex);
  
  // Check if a layer with this priority and config path already exists
  RtxOptionLayer* existingLayer = RtxOptionImpl::getRtxOptionLayer(priority, configPath);
  if (existingLayer != nullptr) {
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
  
  // Initialize reference count
  newLayer->incrementRefCount();
  
  return newLayer;
}

RtxOptionLayer* RtxOptionLayerManager::lookupLayer(const std::string& configPath, uint32_t priority) {
  std::lock_guard<std::mutex> lock(s_mutex);
  return RtxOptionImpl::getRtxOptionLayer(priority, configPath);
}

void RtxOptionLayerManager::releaseLayer(const std::string& configPath, uint32_t priority) {
  std::lock_guard<std::mutex> lock(s_mutex);
  
  RtxOptionLayer* layer = RtxOptionImpl::getRtxOptionLayer(priority, configPath);
  if (layer == nullptr) {
    Logger::warn(str::format("RtxOptionLayerManager: Attempted to release unknown layer '", configPath, "' with priority ", priority, "."));
    return;
  }
  
  if (layer->getRefCount() == 0) {
    Logger::warn(str::format("RtxOptionLayerManager: Layer '", layer->getName(), "' (priority: ", priority, ") already has zero references."));
    return;
  }
  
  layer->decrementRefCount();
  
  // If reference count reached zero, remove the layer
  if (layer->getRefCount() == 0) {
    // Remove from RtxOption system (this also removes from the global map)
    bool removed = RtxOptionImpl::removeRtxOptionLayer(layer);
    
    if (!removed) {
      Logger::warn(str::format("RtxOptionLayerManager: Failed to remove layer '", configPath, "' from RtxOption system."));
    }
  }
}

size_t RtxOptionLayerManager::getReferenceCount(const RtxOptionLayer* layer) {
  if (layer == nullptr) {
    return 0;
  }
  
  std::lock_guard<std::mutex> lock(s_mutex);
  
  return layer->getRefCount();
}

}  // namespace dxvk

