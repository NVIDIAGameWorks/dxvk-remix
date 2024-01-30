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

#include "MathLib/MathLib.h"
#include "../util/util_matrix.h"

#include "../../lssusd/usd_include_begin.h"
#include <pxr/base/arch/math.h>
#include "../../lssusd/usd_include_end.h"

static inline void copyDxvkMatrix4ToDouble4x4(const dxvk::Matrix4& src, double dest[][4]) {
  for (int i = 0; i < 4; i++) {
    for (int j = 0; j < 4; j++) {
      //Convert from floats to doubles for USD
      dest[i][j] = src[i][j];
    }
  }
};

static inline void copyDxvkMatrix4ToFloat4x4(const dxvk::Matrix4& src, float dest[][4]) {
  memcpy(&dest[0][0], &src, sizeof(float)*16);
};

static inline void decomposeProjection(const dxvk::Matrix4& matrix, float& aspectRatio, float& fov, float& nearPlane, float& farPlane, float& shearX, float& shearY, bool& isLHS, bool& isReverseZ, bool log = false) {
  float cameraParams[PROJ_NUM];
  float4x4 cameraMatrix;
  // Check size since struct padding can impacy this memcpy
  assert(sizeof(matrix) == sizeof(cameraMatrix));
  memcpy(&cameraMatrix, &matrix, sizeof(matrix));
  uint32_t flags;
  DecomposeProjection(NDC_D3D, NDC_D3D, float4x4(cameraMatrix), &flags, cameraParams, nullptr, nullptr, nullptr, nullptr);
  // Extract the FOV and aspect ratio from the projection matrix
  aspectRatio = matrix[0][0] * matrix[1][1] > 0.0f ? cameraParams[PROJ_ASPECT] : -cameraParams[PROJ_ASPECT];
  // Note: FoV represents the vertical FoV in radians (as opposed to PROJ_FOVX which is the horizontal FoV).
  fov = cameraParams[PROJ_FOVY];
  nearPlane = cameraParams[PROJ_ZNEAR];
  farPlane = cameraParams[PROJ_ZFAR];
  shearX = cameraParams[PROJ_DIRX];
  shearY = cameraParams[PROJ_DIRY];
  isLHS = (flags & PROJ_LEFT_HANDED) ? 1 : 0;
  isReverseZ = (flags & PROJ_REVERSED_Z) ? 1 : 0;

#ifdef _DEBUG
  if (log) {
    dxvk::Logger::info(dxvk::str::format("Projection Info: \n\tFlags: ", flags,
                                         "\n\tPROJ_ZNEAR: ", cameraParams[PROJ_ZNEAR],
                                         "\n\tPROJ_ZFAR: ", cameraParams[PROJ_ZFAR],
                                         "\n\tPROJ_ASPECT: ", cameraParams[PROJ_ASPECT],
                                         "\n\tPROJ_FOVX: ", cameraParams[PROJ_FOVX],
                                         "\n\tPROJ_FOVY: ", cameraParams[PROJ_FOVY],
                                         "\n\tPROJ_MINX: ", cameraParams[PROJ_MINX],
                                         "\n\tPROJ_MAXX: ", cameraParams[PROJ_MAXX],
                                         "\n\tPROJ_MINY: ", cameraParams[PROJ_MINY],
                                         "\n\tPROJ_MAXY: ", cameraParams[PROJ_MAXY],
                                         "\n\tPROJ_DIRX: ", cameraParams[PROJ_DIRX],
                                         "\n\tPROJ_DIRY: ", cameraParams[PROJ_DIRY],
                                         "\n\tPROJ_ANGLEMINX: ", cameraParams[PROJ_ANGLEMINX],
                                         "\n\tPROJ_ANGLEMAXX: ", cameraParams[PROJ_ANGLEMAXX],
                                         "\n\tPROJ_ANGLEMINY: ", cameraParams[PROJ_ANGLEMINY],
                                         "\n\tPROJ_ANGLEMAXY: ", cameraParams[PROJ_ANGLEMAXY]));
  }
#endif
}