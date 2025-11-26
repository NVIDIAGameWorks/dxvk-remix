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
  X(RtComponentPropertyType::Float3, Vector3(0.0f), value, "Value", "The constant RGB color (Red, Green, Blue).", property.isSettableOutput = true/*, property.treatAsColor = true */)

#define LIST_STATES(X)

#define LIST_OUTPUTS(X)


REMIX_COMPONENT( \
  /* the Component name */ ConstColor3, \
  /* the UI name */        "Constant Color3", \
  /* the UI categories */  "Constants", \
  /* the doc string */     "Provides a constant RGB color (Red, Green, Blue) that you can set.\n\n" \
    "Use this to provide fixed colors to materials, lights, or other components. Each channel ranges from 0.0 to 1.0.", \
  /* the version number */ 1, \
  LIST_INPUTS, LIST_STATES, LIST_OUTPUTS);

#undef LIST_INPUTS
#undef LIST_STATES
#undef LIST_OUTPUTS

void ConstColor3::updateRange(const Rc<DxvkContext>& context, const size_t start, const size_t end) {
  // No-op: constant value components don't need to update anything
}

}  // namespace components
}  // namespace dxvk

