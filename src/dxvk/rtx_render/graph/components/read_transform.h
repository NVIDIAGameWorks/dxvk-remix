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
#include "../rtx_graph_batch.h"
#include "../../rtx_scene_manager.h"
#include "../../rtx_types.h"
#include "../../../../util/util_math.h"
#include "../../../../util/util_vector.h"
#include "../../../../util/util_matrix.h"

namespace dxvk {
namespace components {

#define LIST_INPUTS(X) \
  X(RtComponentPropertyType::Prim, kInvalidPrimTarget, target, "Target", "The mesh or light prim to read the transform from.", \
    property.allowedPrimTypes = {PrimType::UsdGeomMesh, PrimType::UsdLuxSphereLight, PrimType::UsdLuxCylinderLight, PrimType::UsdLuxDiskLight, PrimType::UsdLuxDistantLight, PrimType::UsdLuxRectLight})

#define LIST_STATES(X)

#define LIST_OUTPUTS(X) \
  X(RtComponentPropertyType::Float3, Vector3(0.0f, 0.0f, 0.0f), position, "Position", "The world space position of the target.") \
  X(RtComponentPropertyType::Float4, Vector4(0.0f, 0.0f, 0.0f, 1.0f), rotation, "Rotation", "The world space rotation of the target as a quaternion (x, y, z, w).") \
  X(RtComponentPropertyType::Float3, Vector3(1.0f, 1.0f, 1.0f), scale, "Scale", "The world space scale of the target.")

REMIX_COMPONENT( \
  /* the Component name */ ReadTransform, \
  /* the UI name */        "Read Transform", \
  /* the UI categories */  "Sense", \
  /* the doc string */     "Reads the transform (position, rotation, scale) of a mesh or light in world space.\n\n" \
    "Extracts the transform information from a given mesh or light prim. " \
    "Outputs position, rotation (as quaternion), and scale in world space.", \
  /* the version number */ 1, \
  LIST_INPUTS, LIST_STATES, LIST_OUTPUTS);

#undef LIST_INPUTS
#undef LIST_STATES
#undef LIST_OUTPUTS

void ReadTransform::updateRange(const Rc<DxvkContext>& context, const size_t start, const size_t end) {
  for (size_t i = start; i < end; i++) {
    Vector3 position(0.0f, 0.0f, 0.0f);
    Vector4 rotation(0.0f, 0.0f, 0.0f, 1.0f);
    Vector3 scale(1.0f, 1.0f, 1.0f);
    
    const PrimInstance* prim = m_batch.resolvePrimTarget(context, i, m_target[i]);
    
    if (prim != nullptr) {
      if (prim->getType() == PrimInstance::Type::Instance) {
        // Mesh instance
        RtInstance* rtInstance = prim->getInstance();
        if (rtInstance != nullptr) {
          const Matrix4 transform = rtInstance->getTransform();
          decomposeMatrix(transform, position, rotation, scale);
        }
      } else if (prim->getType() == PrimInstance::Type::Light) {
        // Light instance
        const RtLight* light = prim->getLight();
        if (light != nullptr) {
          position = light->getPosition();
          // For lights, we can extract orientation from direction/axes if available
          // For simplicity, we'll just return identity rotation and unit scale
          rotation = Vector4(0.0f, 0.0f, 0.0f, 1.0f);
          scale = Vector3(1.0f, 1.0f, 1.0f);
        }
      }
    }
    
    m_position[i] = position;
    m_rotation[i] = rotation;
    m_scale[i] = scale;
  }
}

}  // namespace components
}  // namespace dxvk

