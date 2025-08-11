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

#include "rtx_render/graph/rtx_graph_component_macros.h"

namespace dxvk {
namespace components {

enum class TestEnum : uint32_t {
  One = 1,
  Two = 2,
};

#define LIST_INPUTS(X) \
  X(RtComponentPropertyType::Bool, false, inputBool, "Input Bool", "test for Bool") \
  X(RtComponentPropertyType::Float, 1.f, inputFloat, "Input Float", "test for Float") \
  X(RtComponentPropertyType::Float2, Vector2(1.f, 2.f), inputFloat2, "Input Float2", "test for Float2") \
  X(RtComponentPropertyType::Float3, Vector3(1.f, 2.f, 3.f), inputFloat3, "Input Float3", "test for Float3") \
  X(RtComponentPropertyType::Color3, Vector3(1.f, 2.f, 3.f), inputColor3, "Input Color3", "test for Color3") \
  X(RtComponentPropertyType::Color4, Vector4(1.f, 2.f, 3.f, 4.f), inputColor4, "Input Color4", "test for Color4") \
  X(RtComponentPropertyType::Int32, 1, inputInt32, "Input Int32", "test for Int32") \
  X(RtComponentPropertyType::Uint32, 1, inputUint32, "Input Uint32", "test for Uint32") \
  X(RtComponentPropertyType::Uint64, 1, inputUint64, "Input Uint64", "test for Uint64") \
  X(RtComponentPropertyType::Prim, 101, inputPrim, "Input Prim", "test for Prim") \
  X(RtComponentPropertyType::Uint32, 1, inputUint32Enum, "Input Enum", "test for Uint32 as enum", \
    property.enumValues = { {"One", {TestEnum::One, "The first case"}}, {"Two", {TestEnum::Two, "The second case"}} }) \

#define LIST_STATES(X) \
  X(RtComponentPropertyType::Bool, false, stateBool, "", "test for Bool") \
  X(RtComponentPropertyType::Float, 2.f, stateFloat, "", "test for Float") \
  X(RtComponentPropertyType::Float2, Vector2(2.f, 3.f), stateFloat2, "", "test for Float2") \
  X(RtComponentPropertyType::Float3, Vector3(2.f, 3.f, 4.f), stateFloat3, "", "test for Float3") \
  X(RtComponentPropertyType::Color3, Vector3(2.f, 3.f, 4.f), stateColor3, "", "test for Color3") \
  X(RtComponentPropertyType::Color4, Vector4(2.f, 3.f, 4.f, 5.f), stateColor4, "", "test for Color4") \
  X(RtComponentPropertyType::Int32, 2, stateInt32, "", "test for Int32") \
  X(RtComponentPropertyType::Uint32, 2, stateUint32, "", "test for Uint32") \
  X(RtComponentPropertyType::Uint64, 2, stateUint64, "", "test for Uint64") \
  X(RtComponentPropertyType::Prim, 102, statePrim, "", "test for Prim") \
  X(RtComponentPropertyType::Uint32, 2, stateUint32Enum, "", "test for Uint32 as enum", \
    property.enumValues = { {"One", {TestEnum::One, "The first case"}}, {"Two", {TestEnum::Two, "The second case"}} })

#define LIST_OUTPUTS(X) \
  X(RtComponentPropertyType::Bool, false, outputBool, "Output Bool", "test for Bool") \
  X(RtComponentPropertyType::Float, 3.f, outputFloat, "Output Float", "test for Float") \
  X(RtComponentPropertyType::Float2, Vector2(3.f, 4.f), outputFloat2, "Output Float2", "test for Float2") \
  X(RtComponentPropertyType::Float3, Vector3(3.f, 4.f, 5.f), outputFloat3, "Output Float3", "test for Float3") \
  X(RtComponentPropertyType::Color3, Vector3(3.f, 4.f, 5.f), outputColor3, "Output Color3", "test for Color3") \
  X(RtComponentPropertyType::Color4, Vector4(3.f, 4.f, 5.f, 6.f), outputColor4, "Output Color4", "test for Color4") \
  X(RtComponentPropertyType::Int32, 3, outputInt32, "Output Int32", "test for Int32") \
  X(RtComponentPropertyType::Uint32, 3, outputUint32, "Output Uint32", "test for Uint32") \
  X(RtComponentPropertyType::Uint64, 3, outputUint64, "Output Uint64", "test for Uint64") \
  X(RtComponentPropertyType::Prim, 103, outputPrim, "Output Prim", "test for Prim") \
  X(RtComponentPropertyType::Uint32, 3, outputUint32Enum, "Output Enum", "test for Uint32 as enum", \
    property.enumValues = { {"One", {TestEnum::One, "The first case"}}, {"Two", {TestEnum::Two, "The second case"}} })

REMIX_COMPONENT( \
  /* the Component name */ TestComponent, \
  /* the UI name */        "Test Component", \
  /* the UI categories */  "test", \
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
      m_stateBool[i] = m_inputBool[i];
      m_stateFloat[i] = m_inputFloat[i];
      m_stateFloat2[i] = m_inputFloat2[i];
      m_stateFloat3[i] = m_inputFloat3[i];
      m_stateColor3[i] = m_inputColor3[i];
      m_stateColor4[i] = m_inputColor4[i];
      m_stateInt32[i] = m_inputInt32[i];
      m_stateUint32[i] = m_inputUint32[i];
      m_stateUint64[i] = m_inputUint64[i];
      m_statePrim[i] = m_inputPrim[i];
      m_stateUint32Enum[i] = m_inputUint32Enum[i];
    }
    m_outputBool[i] = m_stateBool[i];
    m_outputFloat[i] = m_stateFloat[i];
    m_outputFloat2[i] = m_stateFloat2[i];
    m_outputFloat3[i] = m_stateFloat3[i];
    m_outputColor3[i] = m_stateColor3[i];
    m_outputColor4[i] = m_stateColor4[i];
    m_outputInt32[i] = m_stateInt32[i];
    m_outputUint32[i] = m_stateUint32[i];
    m_outputUint64[i] = m_stateUint64[i];
    m_outputPrim[i] = m_statePrim[i];
    m_outputUint32Enum[i] = m_stateUint32Enum[i];
  }
}

}  // namespace components
}  // namespace dxvk
