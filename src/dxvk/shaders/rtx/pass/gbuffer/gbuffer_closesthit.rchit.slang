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
#include "rtx/concept/surface_material/surface_material_hitgroup.h"

//!variant gbuffer_material_opaque_translucent_closestHit.rchit
//!>       SURFACE_MATERIAL_RESOLVE_TYPE_ACTIVE_MASK=SURFACE_MATERIAL_RESOLVE_TYPE_OPAQUE_TRANSLUCENT


//!variant gbuffer_material_rayportal_closestHit.rchit
//!>       SURFACE_MATERIAL_RESOLVE_TYPE_ACTIVE_MASK=SURFACE_MATERIAL_RESOLVE_TYPE_ALL


// NRC

//!variant gbuffer_nrc_material_opaque_translucent_closestHit.rchit
//!>       ENABLE_NRC=1
//!>       SURFACE_MATERIAL_RESOLVE_TYPE_ACTIVE_MASK=SURFACE_MATERIAL_RESOLVE_TYPE_OPAQUE_TRANSLUCENT


//!variant gbuffer_nrc_material_rayportal_closestHit.rchit
//!>       ENABLE_NRC=1
//!>       SURFACE_MATERIAL_RESOLVE_TYPE_ACTIVE_MASK=SURFACE_MATERIAL_RESOLVE_TYPE_ALL



// WBOIT

//!variant gbuffer_material_opaque_translucent_closestHit_wboit.rchit
//!>       SURFACE_MATERIAL_RESOLVE_TYPE_ACTIVE_MASK=SURFACE_MATERIAL_RESOLVE_TYPE_OPAQUE_TRANSLUCENT
//!>       UNORDERED_RESOLVE_WBOIT

//!variant gbuffer_material_rayportal_closestHit_wboit.rchit
//!>       SURFACE_MATERIAL_RESOLVE_TYPE_ACTIVE_MASK=SURFACE_MATERIAL_RESOLVE_TYPE_ALL
//!>       UNORDERED_RESOLVE_WBOIT

// NRC

//!variant gbuffer_nrc_material_opaque_translucent_closestHit_wboit.rchit
//!>       ENABLE_NRC=1
//!>       SURFACE_MATERIAL_RESOLVE_TYPE_ACTIVE_MASK=SURFACE_MATERIAL_RESOLVE_TYPE_OPAQUE_TRANSLUCENT
//!>       UNORDERED_RESOLVE_WBOIT

//!variant gbuffer_nrc_material_rayportal_closestHit_wboit.rchit
//!>       ENABLE_NRC=1
//!>       SURFACE_MATERIAL_RESOLVE_TYPE_ACTIVE_MASK=SURFACE_MATERIAL_RESOLVE_TYPE_ALL
//!>       UNORDERED_RESOLVE_WBOIT

//!end-variants

#define RAY_TRACING_PRIMARY_RAY
#define RAY_PIPELINE
// Note: Opacity lighting approximation used in the GBuffer pass due to it being most visually
// prominent on particles in primary rays and important in PSR.
#define RESOLVE_OPACITY_LIGHTING_APPROXIMATION 1
// Note: Use thin film in the GBuffer pass for its usage in PSR on mirrorlike surfaces.
#define OPAQUE_MATERIAL_USE_THIN_FILM 1
#define OPAQUE_MATERIAL_USE_POM 1

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

[shader("closesthit")]
void main(inout GeometryResolverState rayPayload : SV_RayPayload, in float2 barycentricCoordinates : SV_IntersectionAttributes)
{
  // Construct a Ray Hit Info from the hit

  RAY_HIT_INFO_CREATE_TRACE_RAY_HIT(rayHitInfo);

  // Invoke the Geometry Resolver Vertex function and handle the payload

  EmptyExtraArgs extraArgs;

  geometryResolverVertex(extraArgs, rayHitInfo, rayPayload);
}
