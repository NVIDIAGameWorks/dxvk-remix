/*
* Copyright (c) 2022-2024, NVIDIA CORPORATION. All rights reserved.
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

//!variant gbuffer_miss.rmiss


// NRC 

//!variant gbuffer_nrc_miss.rmiss
//!>       ENABLE_NRC=1


// WBOIT

//!variant gbuffer_miss_wboit.rmiss
//!>       UNORDERED_RESOLVE_WBOIT

// NRC 

//!variant gbuffer_nrc_miss_wboit.rmiss
//!>       ENABLE_NRC=1
//!>       UNORDERED_RESOLVE_WBOIT

//!end-variants

#define RAY_TRACING_PRIMARY_RAY
#define RAY_PIPELINE
// Note: Opacity lighting approximation used in the GBuffer pass due to it being most visually
// prominent on particles in primary rays and important in PSR.
#define RESOLVE_OPACITY_LIGHTING_APPROXIMATION 1

// Note: No RESOLVER_USE_VOLUMETRIC_ATTENUATION here - we will calculate attenuation for the primary vertex
//       during composition.  This is so we can factor in heterogenous attenuation via the global volumetrics system.

#include "rtx/pass/common_binding_indices.h"
#include "rtx/pass/common_bindings.slangh"
#include "rtx/pass/gbuffer/gbuffer_binding_indices.h"
#include "rtx/pass/gbuffer/gbuffer_bindings.slangh"
// Note: Needed for declaration of g-buffer ray payload structure
#include "rtx/algorithm/geometry_resolver_state.slangh"
// Note: Needed for empty extra args structure.
#include "rtx/algorithm/resolve.h"

#include "rtx/algorithm/geometry_resolver.slangh"

[shader("miss")]
void main(inout GeometryResolverState rayPayload : SV_RayPayload)
{
  // Construct a Ray Hit Info for the miss

  RAY_HIT_INFO_CREATE_TRACE_RAY_MISS(rayHitInfo);

  // Invoke the Geometry Resolver Vertex function and handle the payload

  EmptyExtraArgs extraArgs;

  geometryResolverVertex(extraArgs, rayHitInfo, rayPayload);
}
