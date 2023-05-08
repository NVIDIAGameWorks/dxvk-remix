/*
* Copyright (c) 2023, NVIDIA CORPORATION. All rights reserved.
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

#include "rtx_types.h"

namespace dxvk 
{
  DrawCallState::DrawCallState(
    const RasterGeometry& geometryData, const LegacyMaterialData& materialData, const DrawCallTransforms& transformData,
    const SkinningData& skinningData, const FogState& fogState, bool stencilEnabled) :
    m_geometryData { geometryData },
    m_materialData { materialData },
    m_transformData { transformData },
    m_skinningData { skinningData },
    m_fogState { fogState },
    m_stencilEnabled { stencilEnabled }
  {}

  DrawCallState::DrawCallState(const DrawCallState& _input)
    : m_geometryData(_input.m_geometryData)
    , m_materialData(_input.m_materialData)
    , m_transformData(_input.m_transformData)
    , m_skinningData(_input.m_skinningData)
    , m_fogState(_input.m_fogState)
    , m_isSky(_input.m_isSky) { }

  DrawCallState& DrawCallState::operator=(const DrawCallState& drawCallState) {
    if (this != &drawCallState) {
      m_geometryData = drawCallState.m_geometryData;
      m_materialData = drawCallState.m_materialData;
      m_transformData = drawCallState.m_transformData;
      m_skinningData = drawCallState.m_skinningData;
      m_fogState = drawCallState.m_fogState;
      m_stencilEnabled = drawCallState.m_stencilEnabled;
    }

    return *this;
  }

  bool DrawCallState::finalizePendingFutures() {
    // Bounding boxes (if enabled) will be finalized here, default is FLT_MAX bounds
    finalizeGeometryBoundingBox();

    // Geometry hashes are vital, and cannot be disabled, so its important we get valid data (hence the return type)
    return finalizeGeometryHashes();
  }

  bool DrawCallState::finalizeGeometryHashes() {
    if (!m_geometryData.futureGeometryHashes.valid())
      return false;

    m_geometryData.hashes = m_geometryData.futureGeometryHashes.get();

    if (m_geometryData.hashes[HashComponents::VertexPosition] == kEmptyHash)
      throw DxvkError("Position hash should never be empty");

    return true;
  }

  void DrawCallState::finalizeGeometryBoundingBox() {
    if (m_geometryData.futureBoundingBox.valid())
      m_geometryData.boundingBox = m_geometryData.futureBoundingBox.get();
  }
} // namespace dxvk
