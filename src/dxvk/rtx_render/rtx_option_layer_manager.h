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

#include <string>
#include <unordered_map>
#include <mutex>
#include "rtx_option.h"

namespace dxvk {

// Manages the lifecycle of RtxOptionLayers with reference counting.
// Users should acquire layers through this manager and release them when done.
// Layers are automatically removed when their reference count reaches zero.
//
// Key invariants:
// - Multiple layers can share the same priority value
// - Layers with the same priority are ordered alphabetically by config path
// - The combination of config path and priority uniquely identifies a layer
// - System layers use priorities 0-99, user layers use 100+ (clamped automatically)
// - priorities cannot be changed after a layer is created
class RtxOptionLayerManager {
public:
  // Acquire a layer by config path and priority. Creates the layer if it doesn't exist.
  // Returns a pointer to the layer, or nullptr on failure.
  // Each acquire must be matched with a release.
  // Multiple layers can share the same priority; they are ordered alphabetically.
  static const RtxOptionLayer* acquireLayer(const std::string& configPath, uint32_t priority, float blendStrength, float blendThreshold);

  // looks up a layer by config path and priority. Does not increment the reference count.
  static RtxOptionLayer* lookupLayer(const std::string& configPath, uint32_t priority);
  
  // Release a previously acquired layer. Decrements the reference count.
  // When the count reaches zero, the layer is removed from the system.
  static void releaseLayer(const std::string& configPath, uint32_t priority);
  
  // Get the reference count for a layer (primarily for debugging).
  static size_t getReferenceCount(const RtxOptionLayer* layer);

private:
  static std::mutex s_mutex;
};

}  // namespace dxvk

