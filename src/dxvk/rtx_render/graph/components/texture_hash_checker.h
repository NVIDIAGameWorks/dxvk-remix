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

namespace dxvk {
namespace components {

#define LIST_INPUTS(X) \
  X(RtComponentPropertyType::Hash, 0x0, textureHash, "Texture Hash", "The texture hash to check for usage in the current frame.")

#define LIST_STATES(X)

#define LIST_OUTPUTS(X) \
  X(RtComponentPropertyType::Bool, false, isUsed, "Is Used", "True if the texture hash was used in the current frame.") \
  X(RtComponentPropertyType::Uint32, 0, usageCount, "Usage Count", "Number of times the texture hash was used in the current frame.")

REMIX_COMPONENT( \
  /* the Component name */ TextureHashChecker, \
  /* the UI name */        "Texture Hash Checker", \
  /* the UI categories */  "Sense", \
  /* the doc string */     "Checks if a specific texture hash was used for material replacement in the current frame.  This includes textures in all categories, including ignored textures.", \
  /* the version number */ 1, \
  LIST_INPUTS, LIST_STATES, LIST_OUTPUTS);

#undef LIST_INPUTS
#undef LIST_STATES
#undef LIST_OUTPUTS

void TextureHashChecker::updateRange(const Rc<DxvkContext>& context, const size_t start, const size_t end) {
  // Get the scene manager from context
  RtxContext* rtxContext = static_cast<RtxContext*>(context.ptr());
  const SceneManager& sceneManager = rtxContext->getSceneManager();
  
  for (size_t i = start; i < end; i++) {
    const uint64_t targetHash = m_textureHash[i];
    
    // Check if the texture hash was used for material replacement this frame
    uint32_t count = sceneManager.getReplacementMaterialHashUsageCount(targetHash);
    bool isUsed = count > 0;
    
    m_isUsed[i] = isUsed;
    m_usageCount[i] = count;
  }
}

}  // namespace components
}  // namespace dxvk
