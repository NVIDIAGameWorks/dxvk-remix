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

namespace dxvk {
namespace components {

#define LIST_INPUTS(X) \
  X(RtComponentPropertyType::Prim, 0, value, "Value", "The constant prim reference.", \
    property.isSettableOutput = true, \
    property.allowedPrimTypes = {PrimType::UsdGeomMesh, PrimType::UsdLuxSphereLight, PrimType::UsdLuxCylinderLight, PrimType::UsdLuxDiskLight, PrimType::UsdLuxDistantLight, PrimType::UsdLuxRectLight, PrimType::OmniGraph})

#define LIST_STATES(X)

#define LIST_OUTPUTS(X)


REMIX_COMPONENT( \
  /* the Component name */ ConstPrim, \
  /* the UI name */        "Constant Prim", \
  /* the UI categories */  "Constants", \
  /* the doc string */     "Provides a constant reference to a scene object (prim) that you can set.\n\n" \
    "Use this to provide fixed references to meshes, lights, cameras, or other scene elements.", \
  /* the version number */ 1, \
  LIST_INPUTS, LIST_STATES, LIST_OUTPUTS);

#undef LIST_INPUTS
#undef LIST_STATES
#undef LIST_OUTPUTS

void ConstPrim::updateRange(const Rc<DxvkContext>& context, const size_t start, const size_t end) {
  // No-op: constant value components don't need to update anything
}

}  // namespace components
}  // namespace dxvk

