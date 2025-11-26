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
  X(RtComponentPropertyType::Float, 0.0f, x, "X", "The X component of the vector (Red channel).") \
  X(RtComponentPropertyType::Float, 0.0f, y, "Y", "The Y component of the vector (Green channel).") \
  X(RtComponentPropertyType::Float, 0.0f, z, "Z", "The Z component of the vector (Blue channel).")

#define LIST_STATES(X)

#define LIST_OUTPUTS(X) \
  X(RtComponentPropertyType::Float3, Vector3(0.0f), vector, "Vector", "The composed vector.")

REMIX_COMPONENT( \
  /* the Component name */ ComposeVector3, \
  /* the UI name */        "Compose Vector3", \
  /* the UI categories */  "Transform", \
  /* the doc string */     "Combines three separate numbers into a single Vector3.", \
  /* the version number */ 1, \
  LIST_INPUTS, LIST_STATES, LIST_OUTPUTS)

#undef LIST_INPUTS
#undef LIST_STATES
#undef LIST_OUTPUTS

void ComposeVector3::updateRange(const Rc<DxvkContext>& context, const size_t start, const size_t end) {
  for (size_t i = start; i < end; i++) {
    m_vector[i] = Vector3(m_x[i], m_y[i], m_z[i]);
  }
}

}  // namespace components
}  // namespace dxvk

