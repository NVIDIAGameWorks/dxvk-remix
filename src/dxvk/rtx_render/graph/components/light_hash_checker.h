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
#include "../../rtx_scene_manager.h"
#include "../../rtx_light_manager.h"

namespace dxvk {
namespace components {

#define LIST_INPUTS(X) \
  X(RtComponentPropertyType::Hash, 0x0, lightHash, "Light Hash", "The light hash to check for usage in the current frame.")

#define LIST_STATES(X)

#define LIST_OUTPUTS(X) \
  X(RtComponentPropertyType::Bool, false, isUsed, "Is Used", "True if the light hash was used in the current frame.")

REMIX_COMPONENT( \
  /* the Component name */ LightHashChecker, \
  /* the UI name */        "Light Hash Checker", \
  /* the UI categories */  "Sense", \
  /* the doc string */     "Detects if a specific light is currently active in the scene.\n\n" \
    "Checks if a specific light hash is present in the current frame's light table.", \
  /* the version number */ 1, \
  LIST_INPUTS, LIST_STATES, LIST_OUTPUTS);

#undef LIST_INPUTS
#undef LIST_STATES
#undef LIST_OUTPUTS

void LightHashChecker::updateRange(const Rc<DxvkContext>& context, const size_t start, const size_t end) {
  // Get the scene manager from context
  RtxContext* rtxContext = dynamic_cast<RtxContext*>(context.ptr());
  assert(rtxContext != nullptr && "Components must be run within a valid RtxContext.");
  const SceneManager& sceneManager = rtxContext->getSceneManager();
  const LightManager& lightManager = sceneManager.getLightManager();
  
  // Get the light table
  const std::unordered_map<XXH64_hash_t, RtLight>& lightTable = lightManager.getLightTable();
  
  for (size_t i = start; i < end; i++) {
    const uint64_t targetHash = m_lightHash[i];
    
    // Check if the light hash exists in the light table
    bool isUsed = lightTable.find(targetHash) != lightTable.end();
    
    m_isUsed[i] = isUsed;
  }
}

}  // namespace components
}  // namespace dxvk

