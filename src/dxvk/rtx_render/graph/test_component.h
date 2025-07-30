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

#include "rtx_graph_component_macros.h"

namespace dxvk {

#define LIST_INPUTS(X) \
  X(RtComponentPropertyType::Float, 0.0f, inputFloat, "The input float.", property.minValue = 0.0f, property.maxValue = 1.0f) \
  X(RtComponentPropertyType::Bool, false, inputBool, "The input bool.")

#define LIST_STATES(X) \
  X(RtComponentPropertyType::Float, 0.0f, state1, "The state float.")

#define LIST_OUTPUTS(X) \
  X(RtComponentPropertyType::Float, 0.0f, outputFloat, "The output float.")

REMIX_COMPONENT( \
  /* the C++ class name */ TestComponent, \
  /* the USD name */       "remix.test.component", \
  /* the doc string */     "this is a test component, do not use.", \
  /* the version number */ 1, \
  LIST_INPUTS, LIST_STATES, LIST_OUTPUTS);

#undef LIST_INPUTS
#undef LIST_STATES
#undef LIST_OUTPUTS

void TestComponent::updateRange(const Rc<DxvkContext>& context, const size_t start, const size_t end) {
  // simple example update function.
  for (size_t i = start; i < end; i++) {
    if (m_inputBool[i]) {
      m_state1[i] = m_inputFloat[i];
    }
    m_outputFloat[i] = m_state1[i];
  }
}

}  // namespace dxvk
