/*
* Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.
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

#include "rtx/utility/shader_types.h"

// Note: Indicates if the camera's view space is in a left or right handed coordinate space.
static const uint rightHandedFlag = 1 << 0;

struct Camera
{
  mat4 worldToView;
  mat4 viewToWorld;
  mat4 viewToProjection;
  mat4 projectionToView;
  mat4 viewToProjectionJittered;
  mat4 projectionToViewJittered;
  mat4 worldToProjectionJittered;
  mat4 projectionToWorldJittered;
  mat4 translatedWorldToView;
  mat4 translatedWorldToProjectionJittered;
  mat4 projectionToTranslatedWorld;

  mat4 prevWorldToView;
  mat4 prevViewToWorld;
  mat4 prevWorldToProjection;
  mat4 prevWorldToProjectionJittered;
  mat4 prevProjectionToView;
  mat4 prevProjectionToViewJittered;
  mat4 prevTranslatedWorldToView;
  mat4 prevTranslatedWorldToProjection;

  mat4 projectionToPrevProjectionJittered;

  uvec2 resolution;
  // Note: Near plane is not expressed with the same sign as the view space Z coordinates, so this
  // should be considered if ever mixing them.
  float nearPlane;
  uint flags;
};

struct VolumeDefinitionCamera
{
  mat4 viewToProjection;
  mat4 translatedWorldToView;
  mat4 translatedWorldToProjectionJittered;
  mat4 projectionToTranslatedWorld;
  mat4 prevTranslatedWorldToView;
  mat4 prevTranslatedWorldToProjection;

  vec3 translatedWorldOffset;
  float nearPlane;

  vec3 previousTranslatedWorldOffset;
  uint flags;
};
