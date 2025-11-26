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
  X(RtComponentPropertyType::Prim, kInvalidPrimTarget, target, "Target", "The mesh prim to read the bone transform from. Must be a skinned mesh.", \
    property.allowedPrimTypes = {PrimType::UsdGeomMesh}) \
  X(RtComponentPropertyType::Float, 0.0f, boneIndex, "Bone Index", "The index of the bone to read. Will be rounded to the nearest integer.", property.minValue = 0.0f)

#define LIST_STATES(X)

#define LIST_OUTPUTS(X) \
  X(RtComponentPropertyType::Float3, Vector3(0.0f, 0.0f, 0.0f), position, "Position", "The world space position of the bone.") \
  X(RtComponentPropertyType::Float4, Vector4(0.0f, 0.0f, 0.0f, 1.0f), rotation, "Rotation", "The world space rotation of the bone as a quaternion (x, y, z, w).") \
  X(RtComponentPropertyType::Float3, Vector3(1.0f, 1.0f, 1.0f), scale, "Scale", "The world space scale of the bone.")

REMIX_COMPONENT( \
  /* the Component name */ ReadBoneTransform, \
  /* the UI name */        "Read Bone Transform", \
  /* the UI categories */  "Sense", \
  /* the doc string */     "Reads the transform (position, rotation, scale) of a bone from a skinned mesh.\n\n" \
    "Extracts the transform information for a specific bone from a skinned mesh prim. " \
    "Outputs position, rotation (as quaternion), and scale in world space. " \
    "Returns identity transform if the target is not a skinned mesh or the bone index is invalid.", \
  /* the version number */ 1, \
  LIST_INPUTS, LIST_STATES, LIST_OUTPUTS);

#undef LIST_INPUTS
#undef LIST_STATES
#undef LIST_OUTPUTS

void ReadBoneTransform::updateRange(const Rc<DxvkContext>& context, const size_t start, const size_t end) {
  for (size_t i = start; i < end; i++) {
    Vector3 position(0.0f, 0.0f, 0.0f);
    Vector4 rotation(0.0f, 0.0f, 0.0f, 1.0f);
    Vector3 scale(1.0f, 1.0f, 1.0f);
    
    const PrimInstance* prim = m_batch.resolvePrimTarget(context, i, m_target[i]);
    
    if (prim != nullptr && prim->getType() == PrimInstance::Type::Instance) {
      RtInstance* rtInstance = prim->getInstance();
      
      if (rtInstance != nullptr) {
        // Get the bone index
        uint32_t boneIdx = static_cast<uint32_t>(std::round(m_boneIndex[i]));
        
        // Access skinning data from the drawCall via the BLAS input
        BlasEntry* blasEntry = rtInstance->getBlas();
        if (blasEntry != nullptr) {
          const DrawCallState& drawCall = blasEntry->input;
          const SkinningData& skinningData = drawCall.getSkinningState();
          
          // Check if this drawCall has skinning data and the bone index is valid
          if (skinningData.numBones > 0 && boneIdx < skinningData.numBones) {
            // Get the bone transform from the skinning data
            const Matrix4& boneTransform = skinningData.pBoneMatrices[boneIdx];
            
            // Get the instance's object-to-world transform
            const Matrix4& objectToWorld = rtInstance->getTransform();
            
            // Combine bone transform with object-to-world transform
            Matrix4 worldBoneTransform = objectToWorld * boneTransform;
            
            decomposeMatrix(worldBoneTransform, position, rotation, scale);
          } else {
            if (boneIdx >= skinningData.numBones) {
              ONCE(Logger::warn(str::format("ReadBoneTransform: Bone index ", boneIdx, " is out of range. Mesh has ", skinningData.numBones, " bones.")));
            } else {
              ONCE(Logger::warn(str::format("ReadBoneTransform: Target mesh is not a skinned mesh.")));
            }
          }
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

