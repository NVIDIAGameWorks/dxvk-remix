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
#include "rtx_options.h"

namespace dxvk {
  DrawCallState::DrawCallState(const DrawCallState& _input)
    : geometryData(_input.geometryData)
    , materialData(_input.materialData)
    , transformData(_input.transformData)
    , skinningData(_input.skinningData)
    , fogState(_input.fogState)
    , isSky(_input.isSky)
    , futureSkinningData(_input.futureSkinningData)
    , usesVertexShader(_input.usesVertexShader)
    , usesPixelShader(_input.usesPixelShader)
    , drawCallID(_input.drawCallID) { }

  DrawCallState& DrawCallState::operator=(const DrawCallState& drawCallState) {
    if (this != &drawCallState) {
      geometryData = drawCallState.geometryData;
      materialData = drawCallState.materialData;
      transformData = drawCallState.transformData;
      skinningData = drawCallState.skinningData;
      fogState = drawCallState.fogState;
      stencilEnabled = drawCallState.stencilEnabled;
      isSky = drawCallState.isSky;
      futureSkinningData = drawCallState.futureSkinningData;
      usesPixelShader = drawCallState.usesPixelShader;
      futureSkinningData = drawCallState.futureSkinningData;
      drawCallID = drawCallState.drawCallID;
    }

    return *this;
  }

  bool DrawCallState::finalizePendingFutures(const RtCamera* pLastCamera) {
    // Geometry hashes are vital, and cannot be disabled, so its important we get valid data (hence the return type)
    const bool valid = finalizeGeometryHashes();
    if (valid) {
      // Bounding boxes (if enabled) will be finalized here, default is FLT_MAX bounds
      finalizeGeometryBoundingBox();

      // Skinning processing will be finalized here, if object requires skinning
      finalizeSkinningData(pLastCamera);

      return true;
    }

    return false;
  }

  bool DrawCallState::finalizeGeometryHashes() {
    if (!geometryData.futureGeometryHashes.valid())
      return false;

    geometryData.hashes = geometryData.futureGeometryHashes.get();

    if (geometryData.hashes[HashComponents::VertexPosition] == kEmptyHash)
      throw DxvkError("Position hash should never be empty");

    return true;
  }

  void DrawCallState::finalizeGeometryBoundingBox() {
    if (geometryData.futureBoundingBox.valid())
      geometryData.boundingBox = geometryData.futureBoundingBox.get();
  }

  void DrawCallState::finalizeSkinningData(const RtCamera* pLastCamera) {
    if (futureSkinningData.valid()) {
      skinningData = futureSkinningData.get();

      assert(geometryData.blendWeightBuffer.defined());
      assert(skinningData.numBonesPerVertex <= 4);

      if (pLastCamera != nullptr) {
        const auto fusedMode = RtxOptions::Get()->fusedWorldViewMode();
        if (likely(fusedMode == FusedWorldViewMode::None)) {
          transformData.objectToView = transformData.worldToView;
          // Do not bother when transform is fused. Camera matrices are identity and so is worldToView.
        }
        transformData.objectToWorld = pLastCamera->getViewToWorld(false) * transformData.objectToView;
        transformData.worldToView = pLastCamera->getWorldToView(false);
      } else {
        ONCE(Logger::warn("[RTX-Compatibility-Warn] Cannot decompose the matrices for a skinned mesh because the camera is not set."));
      }

      // In rare cases when the mesh is skinned but has only one active bone, skip the skinning pass
      // and bake that single bone into the objectToWorld/View matrices.
      if (skinningData.minBoneIndex + 1 == skinningData.numBones) {
        const Matrix4& skinningMatrix = skinningData.pBoneMatrices[skinningData.minBoneIndex];

        transformData.objectToWorld = transformData.objectToWorld * skinningMatrix;
        transformData.objectToView = transformData.objectToView * skinningMatrix;

        skinningData.boneHash = 0;
        skinningData.numBones = 0;
        skinningData.numBonesPerVertex = 0;
      }

      // Store the numBonesPerVertex in the RasterGeometry as well to allow it to be overridden
      geometryData.numBonesPerVertex = skinningData.numBonesPerVertex;
    }
  }
} // namespace dxvk
