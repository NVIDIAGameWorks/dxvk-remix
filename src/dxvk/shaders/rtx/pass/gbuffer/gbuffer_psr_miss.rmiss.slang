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

//!variant gbuffer_psr_miss.rmiss


// NRC 

//!variant gbuffer_psr_nrc_miss.rmiss
//!>       ENABLE_NRC=1



//!variant gbuffer_psr_miss_wboit.rmiss
//!>       UNORDERED_RESOLVE_WBOIT

// NRC 

//!variant gbuffer_psr_nrc_miss_wboit.rmiss
//!>       ENABLE_NRC=1
//!>       UNORDERED_RESOLVE_WBOIT

//!end-variants

#define RAY_TRACING_PRIMARY_RAY
#define RT_TRACE_RAY
#define RAY_PIPELINE
// Note: Opacity lighting approximation used in the GBuffer pass due to it being most visually
// prominent on particles in primary rays and important in PSR.
#define RESOLVE_OPACITY_LIGHTING_APPROXIMATION 1

// Note: OPAQUE_MATERIAL_USE_THIN_FILM not used on miss as there is nothing to hit and thin film is usually not used on something
// like particles. If this is desired behavior though then it should be defined here just like it is in the closest hit shader.
// Note: Resolver volume attenuation still needed in miss shaders to account for infinite volumetric attenuation.
// See respective closest hit shader for more details.

// Note: This is done in primary rays only currently so that attenuation from volumetrics can be factored in
// similar to how it was done previously when it was simply applied in compositing via the total virtual hit
// distance, and ensuring that it does not attenuate secondary rays (since they do not receive inscattering and
// attenuating them would cause large amounts of energy loss as a result).
#define RESOLVER_USE_VOLUMETRIC_ATTENUATION 1

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
void main(inout GeometryPSRResolverState rayPayload : SV_RayPayload)
{
  // Construct a Ray Hit Info for the miss

  RAY_HIT_INFO_CREATE_TRACE_RAY_MISS(rayHitInfo);

  // Invoke the Geometry PSR Resolver Vertex function and handle the payload

  EmptyExtraArgs extraArgs;

  geometryPSRResolverVertex(extraArgs, rayHitInfo, rayPayload);
}
