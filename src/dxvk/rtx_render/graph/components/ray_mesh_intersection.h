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

enum class IntersectionType : uint32_t {
  BoundingBox = 0,
};

inline const auto kIntersectionTypeEnumValues = RtComponentPropertySpec::EnumPropertyMap{
  {"Bounding Box", {IntersectionType::BoundingBox, "Test intersection against the mesh's axis-aligned bounding box."}}
};

#define LIST_INPUTS(X) \
  X(RtComponentPropertyType::Float3, Vector3(0.0f, 0.0f, 0.0f), rayOrigin, "Ray Origin", "The origin point of the ray in world space.") \
  X(RtComponentPropertyType::Float3, Vector3(0.0f, 0.0f, 1.0f), rayDirection, "Ray Direction", "The direction of the ray in world space. Should be normalized.") \
  X(RtComponentPropertyType::Prim, kInvalidPrimTarget, target, "Target", "The mesh prim to test intersection against. Must be a mesh prim.", \
    property.allowedPrimTypes = {PrimType::UsdGeomMesh}) \
  X(RtComponentPropertyType::Enum, static_cast<uint32_t>(IntersectionType::BoundingBox), intersectionType, "Intersection Type", \
    "The type of intersection test to perform.", \
    property.optional = true, \
    property.enumValues = kIntersectionTypeEnumValues)

#define LIST_STATES(X)

#define LIST_OUTPUTS(X) \
  X(RtComponentPropertyType::Bool, false, intersects, "Intersects", "True if the ray intersects the mesh (based on the selected intersection type).")

REMIX_COMPONENT( \
  /* the Component name */ RayMeshIntersection, \
  /* the UI name */        "Ray Mesh Intersection", \
  /* the UI categories */  "Sense", \
  /* the doc string */     "Tests if a ray intersects with a mesh.\n\n" \
    "Performs a ray-mesh intersection test. Currently supports bounding box intersection tests. " \
    "Returns true if the ray intersects the mesh's bounding box.", \
  /* the version number */ 1, \
  LIST_INPUTS, LIST_STATES, LIST_OUTPUTS);

#undef LIST_INPUTS
#undef LIST_STATES
#undef LIST_OUTPUTS

// Helper function to test ray-AABB intersection
static bool rayIntersectsAABB(const Vector3& rayOrigin, const Vector3& rayDirection, const AxisAlignedBoundingBox& aabb) {
  if (!aabb.isValid()) {
    return false;
  }
  
  // Use the slab method for ray-AABB intersection
  float tmin = 0.0f;
  float tmax = FLT_MAX;
  
  for (int i = 0; i < 3; i++) {
    if (std::abs(rayDirection[i]) < 1e-8f) {
      // Ray is parallel to slab, check if origin is within slab
      if (rayOrigin[i] < aabb.minPos[i] || rayOrigin[i] > aabb.maxPos[i]) {
        return false;
      }
    } else {
      // Compute intersection t values for near and far plane
      float invD = 1.0f / rayDirection[i];
      float t1 = (aabb.minPos[i] - rayOrigin[i]) * invD;
      float t2 = (aabb.maxPos[i] - rayOrigin[i]) * invD;
      
      if (t1 > t2) {
        std::swap(t1, t2);
      }
      
      tmin = std::max(tmin, t1);
      tmax = std::min(tmax, t2);
      
      if (tmin > tmax) {
        return false;
      }
    }
  }
  
  return true;
}

void RayMeshIntersection::updateRange(const Rc<DxvkContext>& context, const size_t start, const size_t end) {
  for (size_t i = start; i < end; i++) {
    bool intersects = false;
    
    const PrimInstance* meshPrim = m_batch.resolvePrimTarget(context, i, m_target[i]);
    
    if (meshPrim != nullptr && meshPrim->getType() == PrimInstance::Type::Instance) {
      RtInstance* rtInstance = meshPrim->getInstance();
      
      if (rtInstance != nullptr) {
        BlasEntry* blasEntry = rtInstance->getBlas();
        
        if (blasEntry != nullptr) {
          const AxisAlignedBoundingBox& objectSpaceBoundingBox = blasEntry->input.getGeometryData().boundingBox;
          
          if (objectSpaceBoundingBox.isValid()) {
            // Get the object-to-world transform
            const Matrix4 objectToWorld = rtInstance->getTransform();
            Matrix4 worldToObject = inverse(objectToWorld);
            
            // Transform ray to object space
            Vector3 objectSpaceRayOrigin = (worldToObject * Vector4(m_rayOrigin[i], 1.0f)).xyz();
            Vector3 objectSpaceRayDirection = (worldToObject * Vector4(m_rayDirection[i], 0.0f)).xyz();
            
            // Normalize the direction after transformation
            objectSpaceRayDirection = normalize(objectSpaceRayDirection);
            
            // Perform intersection test based on type
            IntersectionType intersectionType = static_cast<IntersectionType>(m_intersectionType[i]);
            if (intersectionType == IntersectionType::BoundingBox) {
              intersects = rayIntersectsAABB(objectSpaceRayOrigin, objectSpaceRayDirection, objectSpaceBoundingBox);
            }
          }
        }
      }
    }
    
    m_intersects[i] = intersects;
  }
}

}  // namespace components
}  // namespace dxvk

