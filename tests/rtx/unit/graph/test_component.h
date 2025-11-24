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
  X(RtComponentPropertyType::Bool, false, inputBool, "Input Bool", "test for Bool", \
    property.oldUsdNames = {"oldInputBool2", "oldInputBool1"}) \
  X(RtComponentPropertyType::Float, 1.f, inputFloat, "Input Float", "test for Float") \
  X(RtComponentPropertyType::Float2, Vector2(1.f, 2.f), inputFloat2, "Input Float2", "test for Float2") \
  X(RtComponentPropertyType::Float3, Vector3(1.f, 2.f, 3.f), inputFloat3, "Input Float3", "test for Float3", property.treatAsColor = true) \
  X(RtComponentPropertyType::Float4, Vector4(1.f, 2.f, 3.f, 4.f), inputFloat4, "Input Float4", "test for Float4", property.treatAsColor = true) \
  X(RtComponentPropertyType::String, std::string("test_string"), inputString, "Input String", "test for String") \
  X(RtComponentPropertyType::AssetPath, std::string("/path/to/asset.usd"), inputAssetPath, "Input AssetPath", "test for AssetPath") \
  X(RtComponentPropertyType::Hash, 0x123456789ABCDEF0, inputHash, "Input Hash", "test for Hash") \
  X(RtComponentPropertyType::Prim, kInvalidPrimTarget, inputPrim, "Input Prim", "test for Prim") \
  X(RtComponentPropertyType::Enum, 1, inputUint32Enum, "Input Enum", "test for Uint32 as enum", \
    property.enumValues = { {"One", {TestEnum::One, "The first case"}}, {"Two", {TestEnum::Two, "The second case"}} })

#define LIST_STATES(X) \
  X(RtComponentPropertyType::Bool, false, stateBool, "", "test for Bool") \
  X(RtComponentPropertyType::Float, 2.f, stateFloat, "", "test for Float") \
  X(RtComponentPropertyType::Float2, Vector2(2.f, 3.f), stateFloat2, "", "test for Float2") \
  X(RtComponentPropertyType::Float3, Vector3(2.f, 3.f, 4.f), stateFloat3, "", "test for Float3", property.treatAsColor = true) \
  X(RtComponentPropertyType::Float4, Vector4(2.f, 3.f, 4.f, 5.f), stateFloat4, "", "test for Float4", property.treatAsColor = true) \
  X(RtComponentPropertyType::String, std::string("state_string"), stateString, "", "test for String") \
  X(RtComponentPropertyType::AssetPath, std::string("/path/to/state/asset.usd"), stateAssetPath, "", "test for AssetPath") \
  X(RtComponentPropertyType::Hash, 0xFEDCBA9876543210, stateHash, "", "test for Hash") \
  X(RtComponentPropertyType::Prim, kInvalidPrimTarget, statePrim, "", "test for Prim") \
  X(RtComponentPropertyType::Enum, 2, stateUint32Enum, "", "test for Uint32 as enum", \
    property.enumValues = { {"One", {TestEnum::One, "The first case"}}, {"Two", {TestEnum::Two, "The second case"}} })

#define LIST_OUTPUTS(X) \
  X(RtComponentPropertyType::Bool, false, outputBool, "Output Bool", "test for Bool", \
    property.oldUsdNames = {"oldOutputBool2", "oldOutputBool1"}) \
  X(RtComponentPropertyType::Float, 3.f, outputFloat, "Output Float", "test for Float") \
  X(RtComponentPropertyType::Float2, Vector2(3.f, 4.f), outputFloat2, "Output Float2", "test for Float2") \
  X(RtComponentPropertyType::Float3, Vector3(3.f, 4.f, 5.f), outputFloat3, "Output Float3", "test for Float3", property.treatAsColor = true) \
  X(RtComponentPropertyType::Float4, Vector4(3.f, 4.f, 5.f, 6.f), outputFloat4, "Output Float4", "test for Float4", property.treatAsColor = true) \
  X(RtComponentPropertyType::String, std::string("output_string"), outputString, "Output String", "test for String") \
  X(RtComponentPropertyType::AssetPath, std::string("/path/to/output/asset.usd"), outputAssetPath, "Output AssetPath", "test for AssetPath") \
  X(RtComponentPropertyType::Hash, 0xABCDEF0123456789, outputHash, "Output Hash", "test for Hash") \
  X(RtComponentPropertyType::Prim, kInvalidPrimTarget, outputPrim, "Output Prim", "test for Prim") \
  X(RtComponentPropertyType::Enum, 3, outputUint32Enum, "Output Enum", "test for Uint32 as enum", \
    property.enumValues = { {"One", {TestEnum::One, "The first case"}}, {"Two", {TestEnum::Two, "The second case"}} })

REMIX_COMPONENT( \
  /* the Component name */ TestComponent, \
  /* the UI name */        "Test Component", \
  /* the UI categories */  "test", \
  /* the doc string */     "this is a test component, do not use.", \
  /* the version number */ 1, \
  LIST_INPUTS, LIST_STATES, LIST_OUTPUTS, spec.oldNames = {"OriginalTestComponent"});

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
      m_stateFloat4[i] = m_inputFloat4[i];
      m_stateString[i] = m_inputString[i];
      m_stateAssetPath[i] = m_inputAssetPath[i];
      m_stateHash[i] = m_inputHash[i];
      m_statePrim[i] = m_inputPrim[i];
      m_stateUint32Enum[i] = m_inputUint32Enum[i];
    }
    m_outputBool[i] = m_stateBool[i];
    m_outputFloat[i] = m_stateFloat[i];
    m_outputFloat2[i] = m_stateFloat2[i];
    m_outputFloat3[i] = m_stateFloat3[i];
    m_outputFloat4[i] = m_stateFloat4[i];
    m_outputString[i] = m_stateString[i];
    m_outputAssetPath[i] = m_stateAssetPath[i];
    m_outputHash[i] = m_stateHash[i];
    m_outputPrim[i] = m_statePrim[i];
    m_outputUint32Enum[i] = m_stateUint32Enum[i];
  }
}

}  // namespace components
}  // namespace dxvk
