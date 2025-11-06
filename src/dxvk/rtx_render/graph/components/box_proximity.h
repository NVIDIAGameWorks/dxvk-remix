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
#include "animation_utils.h"
#include <algorithm>

namespace dxvk {
namespace components {

#define LIST_INPUTS(X) \
  X(RtComponentPropertyType::Prim, 0, target, "Target", "The mesh prim to get bounding box from. Must be a mesh prim.") \
  X(RtComponentPropertyType::Float3, Vector3(0.0f, 0.0f, 0.0f), worldPosition, "World Position", "The world space position to test against the mesh bounding box.") \
  X(RtComponentPropertyType::Float, 0.0f, inactiveDistance, "Inactive Distance", "The distance inside the bounding box that corresponds to a normalized value of 0.0.  Negative numbers represent values outside the AABB. ", property.optional = true) \
  X(RtComponentPropertyType::Float, 1.0f, fullActivationDistance, "Full Activation Distance", "The distance inside the bounding box that corresponds to a normalized value of 1.0.  Negative numbers represent values outside the AABB. ", property.optional = true) \
  X(RtComponentPropertyType::Uint32, static_cast<uint32_t>(InterpolationType::Linear), easingType, "Easing Type", \
    "The type of easing to apply to the normalized output.", \
    property.optional = true, \
    property.enumValues = kInterpolationTypeEnumValues)

#define LIST_STATES(X)

#define LIST_OUTPUTS(X) \
  X(RtComponentPropertyType::Float, 0.0f, signedDistance, "Signed Distance", "Distance in object space to the nearest bounding box plane. Positive when outside, negative when inside.  Outputs FLT_MAX when no valid bounding box is found.") \
  X(RtComponentPropertyType::Float, 0.0f, activationStrength, "Activation Strength", "Normalized 0-1 value: 0 when on bounding box surface, 1 when at max distance inside (with easing applied).")

REMIX_COMPONENT( \
  /* the Component name */ BoxProximity, \
  /* the UI name */        "Box Proximity", \
  /* the UI categories */  "Sense", \
  /* the doc string */     "Calculates the signed distance from a world position to a mesh's bounding box. Positive values indicate the point is outside the bounding box.  Note that the output is in object space.", \
  /* the version number */ 1, \
  LIST_INPUTS, LIST_STATES, LIST_OUTPUTS);

#undef LIST_INPUTS
#undef LIST_STATES
#undef LIST_OUTPUTS

// Calculate signed distance from point to axis-aligned bounding box
// Returns positive distance when outside the box, negative when inside
static float calculateSignedDistanceToAABB(const Vector3& point, const AxisAlignedBoundingBox& aabb) {
  // bounding box already validated in updateRange function
  
  // Calculate distance to each face of the bounding box
  Vector3 minDist = aabb.minPos - point; // Distance to min faces (negative when inside)
  Vector3 maxDist = point - aabb.maxPos; // Distance to max faces (negative when inside)
  
  // Take the maximum distance for each axis (closest face)
  Vector3 distToFaces = max(minDist, maxDist);
  
  // If the point is inside the box, all distances are negative
  // The signed distance is the maximum (least negative) distance to any face
  if (distToFaces.x <= 0.0f && distToFaces.y <= 0.0f && distToFaces.z <= 0.0f) {
    // Inside the box - return the distance to the nearest face (negative)
    return std::max(distToFaces.x, std::max(distToFaces.y, distToFaces.z));
  } else {
    // Outside the box - return the distance to the nearest corner/edge/face (positive)
    // For points outside, we need the Euclidean distance to the nearest point on the box
    Vector3 clampedPoint = clamp(point, aabb.minPos, aabb.maxPos);
    Vector3 diff = point - clampedPoint;
    return length(diff);
  }
}

void BoxProximity::updateRange(const Rc<DxvkContext>& context, const size_t start, const size_t end) {
  const std::vector<GraphInstance*>& instances = m_batch.getInstances();
  
  for (size_t i = start; i < end; i++) {
    const Vector3& testPoint = m_worldPosition[i];
    
    // Default to "very far outside" if no valid mesh found
    float signedDistance = FLT_MAX;
    
    // Check if we have a valid graph instance
    if (instances[i] != nullptr) {
      ReplacementInstance* replacementInstance = instances[i]->getPrimInstanceOwner().getReplacementInstance();
      
      // Check if we have a valid replacement instance and target prim
      if (replacementInstance != nullptr &&
          replacementInstance->prims.size() > m_target[i] &&
          replacementInstance->prims[m_target[i]].getType() == PrimInstance::Type::Instance) {
        
        // Get the mesh prim instance
        const PrimInstance& meshPrim = replacementInstance->prims[m_target[i]];
        
        // Get the RtInstance from the PrimInstance
        RtInstance* rtInstance = meshPrim.getInstance();
        
        if (rtInstance != nullptr) {
          // Get the BlasEntry from the RtInstance - this represents the mesh asset
          BlasEntry* blasEntry = rtInstance->getBlas();
          
          if (blasEntry != nullptr) {
            // Get the bounding box from the BlasEntry's input geometry data (object space)
            const AxisAlignedBoundingBox& objectSpaceBoundingBox = blasEntry->input.getGeometryData().boundingBox;
            
            if (objectSpaceBoundingBox.isValid()) {
              // Get the object-to-world transform from the RtInstance
              const Matrix4 objectToWorld = rtInstance->getTransform();
              
              // Transform the world space point to object space
              // We need the inverse transform (world-to-object)
              Matrix4 worldToObject = inverse(objectToWorld);
              
              // Transform the world space point to object space
              Vector3 objectSpacePoint = (worldToObject * Vector4(testPoint, 1.0f)).xyz();
              
              // Calculate the signed distance in object space
              signedDistance = calculateSignedDistanceToAABB(objectSpacePoint, objectSpaceBoundingBox);
            }
          }
        }
      }
    }

    if (unlikely(signedDistance == FLT_MAX)) {
      ONCE(Logger::err(str::format("BoxProximity: No valid bounding box found.")));
      m_signedDistance[i] = FLT_MAX;
      m_activationStrength[i] = 0.0f;
      continue;
    }
    
    // Set the signed distance output
    m_signedDistance[i] = signedDistance;
    
    // Calculate activation strength based on distance range
    float activationStrength = 0.0f;
    
    // Map the signed distance to the activation range
    const float inactiveDistance = m_inactiveDistance[i];
    const float fullActivationDistance = m_fullActivationDistance[i];
    
    if (fullActivationDistance != inactiveDistance) {
      // Calculate normalized value based on the distance range
      // 0.0 at inactiveDistance, 1.0 at fullActivationDistance
      float normalizedValue = (signedDistance - inactiveDistance) / (fullActivationDistance - inactiveDistance);
      
      // Clamp to 0-1 range
      normalizedValue = clamp(normalizedValue, 0.0f, 1.0f);
      
      // Apply easing
      InterpolationType interpolation = static_cast<InterpolationType>(m_easingType[i]);
      activationStrength = applyInterpolation(interpolation, normalizedValue);
    } else {
      // Avoid division by zero - if distances are equal, use step function
      activationStrength = (signedDistance >= inactiveDistance) ? 1.0f : 0.0f;
    }
    
    m_activationStrength[i] = activationStrength;
  }
}

}  // namespace components
}  // namespace dxvk
