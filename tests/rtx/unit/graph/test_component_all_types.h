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

#define LIST_COMPONENT_INPUTS(X) \
  X(RtComponentPropertyType::Bool, false, inputBool, "test for Bool") \
  X(RtComponentPropertyType::Float, 1.f, inputFloat, "test for Float") \
  X(RtComponentPropertyType::Float2, Vector2(1.f, 2.f), inputFloat2, "test for Float2") \
  X(RtComponentPropertyType::Float3, Vector3(1.f, 2.f, 3.f), inputFloat3, "test for Float3") \
  X(RtComponentPropertyType::Color3, Vector3(1.f, 2.f, 3.f), inputColor3, "test for Color3") \
  X(RtComponentPropertyType::Color4, Vector4(1.f, 2.f, 3.f, 4.f), inputColor4, "test for Color4") \
  X(RtComponentPropertyType::Int32, 1, inputInt32, "test for Int32") \
  X(RtComponentPropertyType::Uint32, 1, inputUint32, "test for Uint32") \
  X(RtComponentPropertyType::Uint64, 1, inputUint64, "test for Uint64") \
  X(RtComponentPropertyType::MeshInstance, 101, inputMeshInstance, "test for MeshInstance") \
  X(RtComponentPropertyType::LightInstance, 101, inputLightInstance, "test for LightInstance") \
  X(RtComponentPropertyType::GraphInstance, 101, inputGraphInstance, "test for GraphInstance") \
  X(RtComponentPropertyType::Uint32, 1, inputUint32Enum, "test for Uint32 as enum", property.enumValues = { {"One", uint32_t(1)}, {"Two", uint32_t(2)} }) \

#define LIST_COMPONENT_STATES(X) \
  X(RtComponentPropertyType::Bool, false, stateBool, "test for Bool") \
  X(RtComponentPropertyType::Float, 2.f, stateFloat, "test for Float") \
  X(RtComponentPropertyType::Float2, Vector2(2.f, 3.f), stateFloat2, "test for Float2") \
  X(RtComponentPropertyType::Float3, Vector3(2.f, 3.f, 4.f), stateFloat3, "test for Float3") \
  X(RtComponentPropertyType::Color3, Vector3(2.f, 3.f, 4.f), stateColor3, "test for Color3") \
  X(RtComponentPropertyType::Color4, Vector4(2.f, 3.f, 4.f, 5.f), stateColor4, "test for Color4") \
  X(RtComponentPropertyType::Int32, 2, stateInt32, "test for Int32") \
  X(RtComponentPropertyType::Uint32, 2, stateUint32, "test for Uint32") \
  X(RtComponentPropertyType::Uint64, 2, stateUint64, "test for Uint64") \
  X(RtComponentPropertyType::MeshInstance, 102, stateMeshInstance, "test for MeshInstance") \
  X(RtComponentPropertyType::LightInstance, 102, stateLightInstance, "test for LightInstance") \
  X(RtComponentPropertyType::GraphInstance, 102, stateGraphInstance, "test for GraphInstance") \
  X(RtComponentPropertyType::Uint32, 2, stateUint32Enum, "test for Uint32 as enum", property.enumValues = { {"One", uint32_t(1)}, {"Two", uint32_t(2)} })

#define LIST_COMPONENT_OUTPUTS(X) \
  X(RtComponentPropertyType::Bool, false, outputBool, "test for Bool") \
  X(RtComponentPropertyType::Float, 3.f, outputFloat, "test for Float") \
  X(RtComponentPropertyType::Float2, Vector2(3.f, 4.f), outputFloat2, "test for Float2") \
  X(RtComponentPropertyType::Float3, Vector3(3.f, 4.f, 5.f), outputFloat3, "test for Float3") \
  X(RtComponentPropertyType::Color3, Vector3(3.f, 4.f, 5.f), outputColor3, "test for Color3") \
  X(RtComponentPropertyType::Color4, Vector4(3.f, 4.f, 5.f, 6.f), outputColor4, "test for Color4") \
  X(RtComponentPropertyType::Int32, 3, outputInt32, "test for Int32") \
  X(RtComponentPropertyType::Uint32, 3, outputUint32, "test for Uint32") \
  X(RtComponentPropertyType::Uint64, 3, outputUint64, "test for Uint64") \
  X(RtComponentPropertyType::MeshInstance, 103, outputMeshInstance, "test for MeshInstance") \
  X(RtComponentPropertyType::LightInstance, 103, outputLightInstance, "test for LightInstance") \
  X(RtComponentPropertyType::GraphInstance, 103, outputGraphInstance, "test for GraphInstance") \
  X(RtComponentPropertyType::Uint32, 3, outputUint32Enum, "test for Uint32 as enum", property.enumValues = { {"One", uint32_t(1)}, {"Two", uint32_t(2)} })

REMIX_COMPONENT(TestAllTypes, "remix.test.all_types", "this is a test component, do not use.", 1, \
  LIST_COMPONENT_INPUTS, LIST_COMPONENT_STATES, LIST_COMPONENT_OUTPUTS);

#undef LIST_COMPONENT_INPUTS
#undef LIST_COMPONENT_STATES
#undef LIST_COMPONENT_OUTPUTS

void TestAllTypes::updateRange(const Rc<DxvkContext>& context, const size_t start, const size_t end) {
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
      m_stateMeshInstance[i] = m_inputMeshInstance[i];
      m_stateLightInstance[i] = m_inputLightInstance[i];
      m_stateGraphInstance[i] = m_inputGraphInstance[i];
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
    m_outputMeshInstance[i] = m_stateMeshInstance[i];
    m_outputLightInstance[i] = m_stateLightInstance[i];
    m_outputGraphInstance[i] = m_stateGraphInstance[i];
    m_outputUint32Enum[i] = m_stateUint32Enum[i];
  }
}

}  // namespace dxvk
