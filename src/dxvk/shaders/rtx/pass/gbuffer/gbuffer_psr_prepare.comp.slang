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

#define RAY_TRACING_PRIMARY_RAY
#define RESOLVE_OPACITY_LIGHTING_APPROXIMATION 1
#define OPAQUE_MATERIAL_USE_THIN_FILM 1
#define OPAQUE_MATERIAL_USE_POM 1

#include "rtx/pass/common_binding_indices.h"
#include "rtx/pass/common_bindings.slangh"
#include "rtx/pass/gbuffer/gbuffer_binding_indices.h"
#include "rtx/pass/gbuffer/gbuffer_bindings.slangh"
#include "rtx/algorithm/geometry_resolver_state.slangh"

#include "rtx/concept/camera/camera.slangh"
#include "rtx/algorithm/geometry_resolver.slangh"

[shader("compute")]
[numthreads(16, 8, 1)]
void main(uint2 ipos : SV_DispatchThreadID)
{
  if (any(ipos >= cb.camera.resolution))
  {
    return;
  }

  geometryPSRPreparePass(ipos);
}
