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
#include "../../rtx_scene_manager.h"

namespace dxvk {
namespace components {
// 60 degrees is a reasonable default vertical fov for 16:9 displays. 
static constexpr float kDefaultFovRadians = (float)M_PI / 3.0f;  // 60 degrees

#define LIST_INPUTS(X)

#define LIST_STATES(X)

#define LIST_OUTPUTS(X) \
  X(RtComponentPropertyType::Float3, Vector3(0.0f, 0.0f, 0.0f), position, "Position", "The current camera position in world space.") \
  X(RtComponentPropertyType::Float3, Vector3(0.0f, 0.0f, -1.0f), forward, "Forward", "The camera's normalized forward direction vector in world space.") \
  X(RtComponentPropertyType::Float3, Vector3(1.0f, 0.0f, 0.0f), right, "Right", "The camera's normalized right direction vector in world space.") \
  X(RtComponentPropertyType::Float3, Vector3(0.0f, 1.0f, 0.0f), up, "Up", "The camera's normalized up direction vector in world space.") \
  X(RtComponentPropertyType::Float, kDefaultFovRadians, fovRadians, "FOV (radians)", "The Y axis (vertical) Field of View of the camera in radians. Note this value will always be positive.") \
  X(RtComponentPropertyType::Float, 60.0f, fovDegrees, "FOV (degrees)", "The Y axis (vertical) Field of View of the camera in degrees. Note this value will always be positive.") \
  X(RtComponentPropertyType::Float, 1.0f, aspectRatio, "Aspect Ratio", "The camera's aspect ratio (width/height).") \
  X(RtComponentPropertyType::Float, 0.1f, nearPlane, "Near Plane", "The camera's near clipping plane distance.") \
  X(RtComponentPropertyType::Float, 1000.0f, farPlane, "Far Plane", "The camera's far clipping plane distance.")

REMIX_COMPONENT( \
  /* the Component name */ Camera, \
  /* the UI name */        "Camera", \
  /* the UI categories */  "Sense", \
  /* the doc string */     "Outputs current camera properties including position, orientation vectors, and projection parameters. Uses free camera when both 'rtx.camera.useFreeCameraForComponents' and free camera are enabled.", \
  /* the version number */ 1, \
  LIST_INPUTS, LIST_STATES, LIST_OUTPUTS);

#undef LIST_INPUTS
#undef LIST_STATES
#undef LIST_OUTPUTS

void Camera::updateRange(const Rc<DxvkContext>& context, const size_t start, const size_t end) {
  // Get the scene manager from the context
  RtxContext* rtxContext = static_cast<RtxContext*>(context.ptr());
  const RtCamera& camera = rtxContext->getSceneManager().getCamera();
  
  // Determine which camera to use globally
  const bool useFreeCam = RtCamera::useFreeCameraForComponents() && RtCamera::isFreeCameraEnabled();
  const bool isCameraValid = camera.isValid(rtxContext->getDevice()->getCurrentFrameId());
  
  // Default values for invalid camera
  Vector3 cameraPos = Vector3(0.0f, 0.0f, 0.0f);
  Vector3 cameraForward = Vector3(0.0f, 0.0f, -1.0f);
  Vector3 cameraRight = Vector3(1.0f, 0.0f, 0.0f);
  Vector3 cameraUp = Vector3(0.0f, 1.0f, 0.0f);
  float cameraFovRadians = kDefaultFovRadians;
  float cameraAspectRatio = 1.0f;
  float cameraNearPlane = 0.1f;
  float cameraFarPlane = 1000.0f;
  
  // Extract camera properties once if valid
  if (isCameraValid) {
    const Matrix4 viewToWorld = camera.getViewToWorld(useFreeCam);
    cameraPos = viewToWorld.data[3].xyz();
    cameraForward = camera.getDirection(useFreeCam);
    cameraRight = camera.getRight(useFreeCam);
    cameraUp = camera.getUp(useFreeCam);
    cameraFovRadians = camera.getFov();
    cameraAspectRatio = camera.getAspectRatio();
    cameraNearPlane = camera.getNearPlane();
    cameraFarPlane = camera.getFarPlane();
  }
  
  // Apply to all instances (same values for all)
  for (size_t i = start; i < end; i++) {
    m_position[i] = cameraPos;
    m_forward[i] = cameraForward;
    m_right[i] = cameraRight;
    m_up[i] = cameraUp;
    m_fovRadians[i] = cameraFovRadians;
    m_fovDegrees[i] = RadToDeg<float>(cameraFovRadians);
    m_aspectRatio[i] = cameraAspectRatio;
    m_nearPlane[i] = cameraNearPlane;
    m_farPlane[i] = cameraFarPlane;
  }
}

}  // namespace components
}  // namespace dxvk
