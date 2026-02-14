/*
* Copyright (c) 2026, NVIDIA CORPORATION. All rights reserved.
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

// This header provides vec2/vec3/vec4 specializations of AssignFromPrimvar.
// Include this AFTER both particle_system_helpers.h and shader_types.h are included.
// Do NOT include this in HdRemix code as it requires MathLib.

#include "particle_system_helpers.h"
#include "../dxvk/shaders/rtx/utility/shader_types.h"

namespace lss {

  template<>
  inline vec4 AssignFromPrimvar<vec4, pxr::GfVec4f>(const pxr::GfVec4f& src) {
    return vec4(src[0], src[1], src[2], src[3]);
  }

  template<>
  inline vec3 AssignFromPrimvar<vec3, pxr::GfVec3f>(const pxr::GfVec3f& src) {
    return vec3(src[0], src[1], src[2]);
  }

  template<>
  inline vec2 AssignFromPrimvar<vec2, pxr::GfVec2f>(const pxr::GfVec2f& src) {
    return vec2(src[0], src[1]);
  }

}
