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
namespace components {

#define LIST_INPUTS(X) \
  X(RtComponentPropertyType::Bool, true, enabled, "Enabled", "If true, the overrides will be applied", property.optional = true) \
  X(RtComponentPropertyType::Float, 0.0f, radius, "Radius", "The radius of the sphere light.", property.optional = true) \
  X(RtComponentPropertyType::Prim, 0, target, "Target", "The sphere light to override.")

#define LIST_STATES(X)
#define LIST_OUTPUTS(X)

// Manually declaring the class here to allow for extra member functions.
class SphereLightOverride : public RtRegisteredComponentBatch<SphereLightOverride> {
private:
  REMIX_COMPONENT_WRITE_CLASS_MEMBERS(LIST_INPUTS, LIST_STATES, LIST_OUTPUTS)
public:
  REMIX_COMPONENT_WRITE_CTOR(SphereLightOverride, LIST_INPUTS, LIST_STATES, LIST_OUTPUTS)
  static const RtComponentSpec* getStaticSpec() {
    REMIX_COMPONENT_WRITE_STATIC_SPEC( \
      /* the Component name */ SphereLightOverride, \
      /* the UI name */        "Sphere Light", \
      /* the UI categories */  "light", \
      /* the doc string */     "Override the sphere light properties.", \
      /* the version number */ 1, \
      LIST_INPUTS, LIST_STATES, LIST_OUTPUTS, \
      /* optional arguments: */ \
      spec.applySceneOverrides = [](const Rc<DxvkContext>& context, RtComponentBatch& batch, const size_t start, const size_t end) { \
        static_cast<SphereLightOverride&>(batch).applySceneOverrides(context, start, end); \
      } \
    )
  }

  // no-op: If this component contained any logical updates, they would go here.
  void updateRange(const Rc<DxvkContext>& context, const size_t start, const size_t end) final {};

  // Apply changes to render state here.
  void applySceneOverrides(const Rc<DxvkContext>& context, const size_t start, const size_t end) {
    const std::vector<GraphInstance*>& instances = m_batch.getInstances();
    for (size_t i = start; i < end; i++) {
      if (!m_enabled[i] || instances[i] == nullptr) {
        continue;
      }
      ReplacementInstance* replacementInstance = instances[i]->getPrimInstanceOwner().getReplacementInstance();
      if (replacementInstance != nullptr && 
          replacementInstance->prims.size() > m_target[i] &&
          replacementInstance->prims[m_target[i]].getType() == PrimInstance::Type::Light) {
        // NOTE: light doesn't expose a non-const getter for sphere lights,
        // so we can't actually do this without a hack.
        // TODO: implement a better way for components to alter renderable objects.
        
        // RtLight* light = replacementInstance->prims[m_target[i]].getLight();
  
        // if (light != nullptr && light->getType() == RtLightType::Sphere) {
        //   light->getSphereLight().setRadius(m_radius[i]);
        // }
      } else {
        ONCE(Logger::err(str::format("SphereLightOverride: target prim was invalid (not a sphere light, or not part of the same replacement hierarchy.)")));
      }
    }
  }
};

#undef LIST_INPUTS
#undef LIST_STATES
#undef LIST_OUTPUTS

}  // namespace components
}  // namespace dxvk
