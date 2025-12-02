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
  X(RtComponentPropertyType::Float3, Vector3(0.0f, 0.0f, 0.0f), worldPosition, "World Position", "The world space position to use as the origin of the ray.") \
  X(RtComponentPropertyType::Float3, Vector3(0.0f, 0.0f, 1.0f), direction, "Direction", "The direction vector of the ray (does not need to be normalized).") \
  X(RtComponentPropertyType::Prim, kInvalidPrimTarget, target, "Target", "The mesh prim to get the centroid from. Must be a mesh prim.", \
    property.allowedPrimTypes = {PrimType::UsdGeomMesh})

#define LIST_STATES(X)

#define LIST_OUTPUTS(X) \
  X(RtComponentPropertyType::Float, 0.0f, angleDegrees, "Angle (Degrees)", "The angle in degrees between the ray direction and the direction to the mesh centroid.") \
  X(RtComponentPropertyType::Float, 0.0f, angleRadians, "Angle (Radians)", "The angle in radians between the ray direction and the direction to the mesh centroid.") \
  X(RtComponentPropertyType::Float3, Vector3(0.0f, 0.0f, 0.0f), directionToCentroid, "Direction to Centroid", "The normalized direction vector from the world position to the mesh centroid.")

REMIX_COMPONENT( \
  /* the Component name */ AngleToMesh, \
  /* the UI name */        "Angle to Mesh", \
  /* the UI categories */  "Sense", \
  /* the doc string */     "Measures the angle between a ray and a mesh's center point.  This can be used to determine if the camera is looking at a mesh.\n\n" \
    "Calculates the angle between a ray (from position + direction) and the direction to a mesh's transformed centroid.", \
  /* the version number */ 1, \
  LIST_INPUTS, LIST_STATES, LIST_OUTPUTS);

#undef LIST_INPUTS
#undef LIST_STATES
#undef LIST_OUTPUTS

void AngleToMesh::updateRange(const Rc<DxvkContext>& context, const size_t start, const size_t end) {
  const std::vector<GraphInstance*>& instances = m_batch.getInstances();
  
  for (size_t i = start; i < end; i++) {
    const Vector3& rayOrigin = m_worldPosition[i];
    const Vector3& rayDirection = m_direction[i];
    
    // Default outputs
    float angleDegrees = 0.0f;
    float angleRadians = 0.0f;
    Vector3 dirToCentroid(0.0f, 0.0f, 0.0f);

    const PrimInstance* meshPrim = m_batch.resolvePrimTarget(context, i, m_target[i]);

    if (meshPrim != nullptr && meshPrim->getType() == PrimInstance::Type::Instance) {
      // Get the RtInstance from the PrimInstance
      RtInstance* rtInstance = meshPrim->getInstance();
      
      if (rtInstance != nullptr) {
        // Get the BlasEntry from the RtInstance - this represents the mesh asset
        BlasEntry* blasEntry = rtInstance->getBlas();
        
        if (blasEntry != nullptr) {
          // Get the bounding box from the BlasEntry's input geometry data (object space)
          const AxisAlignedBoundingBox& objectSpaceBoundingBox = blasEntry->input.getGeometryData().boundingBox;
          
          if (objectSpaceBoundingBox.isValid()) {
            // Get the object-to-world transform from the RtInstance
            const Matrix4 objectToWorld = rtInstance->getTransform();
            
            // Get the transformed centroid in world space
            Vector3 worldSpaceCentroid = objectSpaceBoundingBox.getTransformedCentroid(objectToWorld);
            
            // Calculate direction to centroid
            Vector3 toCentroid = worldSpaceCentroid - rayOrigin;
            float distanceToCentroid = length(toCentroid);
            
            // Normalize the direction to centroid
            if (distanceToCentroid > 0.0f) {
              dirToCentroid = toCentroid / distanceToCentroid;
              
              // Normalize the ray direction
              Vector3 normalizedRayDir = rayDirection;
              float rayDirLength = length(rayDirection);
              if (rayDirLength > 0.0f) {
                normalizedRayDir = rayDirection / rayDirLength;
                
                // Calculate the angle between the two directions
                float dotProduct = dot(normalizedRayDir, dirToCentroid);
                
                // Clamp dot product to [-1, 1] to avoid numerical errors in acos
                dotProduct = clamp(dotProduct, -1.0f, 1.0f);
                
                // Calculate angle in radians
                angleRadians = std::acos(dotProduct);
                
                // Convert to degrees
                angleDegrees = angleRadians * (180.0f / kPi);
              } else {
                ONCE(Logger::warn(str::format("AngleToMesh: Direction vector has zero length.")));
              }
            } else {
              ONCE(Logger::warn(str::format("AngleToMesh: World position is at the centroid.")));
            }
          } else {
            ONCE(Logger::err(str::format("AngleToMesh: Bounding box is invalid.")));
          }
        } else {
          ONCE(Logger::err(str::format("AngleToMesh: BlasEntry is null.")));
        }
      } else {
        ONCE(Logger::err(str::format("AngleToMesh: RtInstance is null.")));
      }
    } else {
      ONCE(Logger::err(str::format("AngleToMesh: resolvePrimTarget failed is null.")));
    }
    
    // Set outputs
    m_angleDegrees[i] = angleDegrees;
    m_angleRadians[i] = angleRadians;
    m_directionToCentroid[i] = dirToCentroid;
  }
}

}  // namespace components
}  // namespace dxvk

