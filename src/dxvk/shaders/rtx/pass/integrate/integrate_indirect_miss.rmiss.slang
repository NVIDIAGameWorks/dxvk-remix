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

//!variant integrate_indirect_miss.rmiss
//!>       NEE_CACHE_ENABLE=0


//!variant integrate_indirect_miss_neeCache.rmiss
//!>       NEE_CACHE_ENABLE=1


// NRC

//!variant integrate_indirect_miss_nrc.rmiss
//!>       ENABLE_NRC=1
//!>       NEE_CACHE_ENABLE=0


//!variant integrate_indirect_miss_nrc_neeCache.rmiss
//!>       ENABLE_NRC=1
//!>       NEE_CACHE_ENABLE=1


 // WBOIT

//!variant integrate_indirect_miss_wboit.rmiss
//!>       NEE_CACHE_ENABLE=0
//!>       UNORDERED_RESOLVE_WBOIT

//!variant integrate_indirect_miss_neeCache_wboit.rmiss
//!>       NEE_CACHE_ENABLE=1
//!>       UNORDERED_RESOLVE_WBOIT

// NRC

//!variant integrate_indirect_miss_nrc_wboit.rmiss
//!>       ENABLE_NRC=1
//!>       NEE_CACHE_ENABLE=0
//!>       UNORDERED_RESOLVE_WBOIT

//!variant integrate_indirect_miss_nrc_neeCache_wboit.rmiss
//!>       ENABLE_NRC=1
//!>       NEE_CACHE_ENABLE=1
//!>       UNORDERED_RESOLVE_WBOIT

//!end-variants

#define RAY_PIPELINE
#define INTEGRATE_INDIRECT
#define RAB_HAS_PREVIOUS_GBUFFER 1
#define RAB_HAS_RTXDI_RESERVOIRS 1
#define RAB_HAS_RESTIR_GI_RESERVOIRS 1
// Note: Opacity lighting approximation used in the indirect integration pass due to particle lighting being somewhat
// important in reflections off shiny surfaces (as without this it can make areas with many particles look weird in reflections).
#define RESOLVE_OPACITY_LIGHTING_APPROXIMATION 1
#define RESOLVER_USE_VOLUMETRIC_ATTENUATION 1

#include "rtx/pass/common_binding_indices.h"
#include "rtx/pass/common_bindings.slangh"
#include "rtx/pass/integrate/integrate_indirect_binding_indices.h"
#include "rtx/pass/integrate/integrate_indirect_bindings.slangh"
// Note: Needed for declaration of integration ray payload structure
#include "rtx/algorithm/integrator.slangh"

#include "rtx/algorithm/integrator_indirect.slangh"

[shader("miss")]
void main(inout PathState rayPayload : SV_RayPayload)
{
  // Construct a Ray Hit Info for the miss

  RAY_HIT_INFO_CREATE_TRACE_RAY_MISS(rayHitInfo);

  // Invoke the Integrate Path Vertex function and handle the payload

  IndirectPathTextures indirectPathTextures;

  indirectPathTextures.PrimaryWorldPositionWorldTriangleNormal = PrimaryWorldPositionWorldTriangleNormal;
  indirectPathTextures.IndirectRadianceHitDistance = IndirectRadianceHitDistance;

  integratePathVertex(indirectPathTextures, rayHitInfo, rayPayload);
}
