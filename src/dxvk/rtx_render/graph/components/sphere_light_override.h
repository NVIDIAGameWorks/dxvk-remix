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
#include "../../../util/util_globaltime.h"
#include "../rtx_graph_batch.h"
#include "../../rtx_types.h"
#include "../../rtx_lights.h"

namespace dxvk {
class SphereLightOverrideComponent;
static void sphereLightOverrideComponentApply(const Rc<DxvkContext>& context, SphereLightOverrideComponent& batch, const size_t start, const size_t end);

#define LIST_INPUTS(X) \
  X(RtComponentPropertyType::Bool, true, enabled, "If true, the overrides will be applied") \
  X(RtComponentPropertyType::Float, 0.0f, radius, "The radius of the sphere light.") \
  X(RtComponentPropertyType::LightInstance, ReplacementInstance::kInvalidReplacementIndex, target, "The sphere light to override.")

#define LIST_STATES(X) 
#define LIST_OUTPUTS(X) 

REMIX_COMPONENT( \
  /* the C++ class name */ SphereLightOverrideComponent, \
  /* the USD name */       "remix.override.light.sphere", \
  /* the doc string */     "Override the sphere light properties.", \
  /* the version number */ 1, \
  LIST_INPUTS, LIST_STATES, LIST_OUTPUTS, \
  /* optional arguments: */ \
  spec.applySceneOverrides = [](const Rc<DxvkContext>& context, RtComponentBatch& batch, const size_t start, const size_t end) { \
    sphereLightOverrideComponentApply(context, static_cast<SphereLightOverrideComponent&>(batch), start, end); \
  } \
);

#undef LIST_INPUTS
#undef LIST_STATES
#undef LIST_OUTPUTS

static void sphereLightOverrideComponentApply(const Rc<DxvkContext>& context, SphereLightOverrideComponent& batch, const size_t start, const size_t end) {
  std::vector<GraphInstance*> instances = batch.m_batch.getInstances();
  // simple example update function.
  for (size_t i = start; i < end; i++) {
    if (batch.m_enabled[i]) {
      ReplacementInstance* replacementInstance = instances[i]->getPrimInstanceOwner().getReplacementInstance();
      if (replacementInstance != nullptr && 
          replacementInstance->prims.size() > batch.m_target[i] &&
          replacementInstance->prims[batch.m_target[i]].getType() == PrimInstance::Type::Light) {
        // NOTE: light doesn't expose a non-const getter for sphere lights,
        // so we can't actually do this without a hack.
        // TODO: implement a better way for components to alter renderable objects.
        
        // RtLight* light = replacementInstance->prims[batch.m_target[i]].getLight();

        // if (light != nullptr && light->getType() == RtLightType::Sphere) {
        //   light->getSphereLight().setRadius(batch.m_radius[i]);
        // }
      }
    }
  }
}

void SphereLightOverrideComponent::updateRange(const Rc<DxvkContext>& context, const size_t start, const size_t end) {
  // no-op: any logic for this component should happen here, but right now this component only applies a value to a light.
}

}  // namespace dxvk
