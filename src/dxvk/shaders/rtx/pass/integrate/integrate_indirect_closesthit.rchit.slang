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

//!variant integrate_indirect_material_opaque_translucent_closestHit.rchit
//!>       SURFACE_MATERIAL_RESOLVE_TYPE_ACTIVE_MASK=SURFACE_MATERIAL_RESOLVE_TYPE_OPAQUE_TRANSLUCENT
//!>       NEE_CACHE_ENABLE=0


//!variant integrate_indirect_neeCache_material_opaque_translucent_closestHit.rchit
//!>       SURFACE_MATERIAL_RESOLVE_TYPE_ACTIVE_MASK=SURFACE_MATERIAL_RESOLVE_TYPE_OPAQUE_TRANSLUCENT
//!>       NEE_CACHE_ENABLE=1


//!variant integrate_indirect_material_rayportal_closestHit.rchit
//!>       SURFACE_MATERIAL_RESOLVE_TYPE_ACTIVE_MASK=SURFACE_MATERIAL_RESOLVE_TYPE_ALL
//!>       NEE_CACHE_ENABLE=0


//!variant integrate_indirect_neeCache_material_rayportal_closestHit.rchit
//!>       SURFACE_MATERIAL_RESOLVE_TYPE_ACTIVE_MASK=SURFACE_MATERIAL_RESOLVE_TYPE_ALL
//!>       NEE_CACHE_ENABLE=1



//!variant integrate_indirect_pom_material_opaque_translucent_closestHit.rchit
//!>       SURFACE_MATERIAL_RESOLVE_TYPE_ACTIVE_MASK=SURFACE_MATERIAL_RESOLVE_TYPE_OPAQUE_TRANSLUCENT
//!>       NEE_CACHE_ENABLE=0
//!>       OPAQUE_MATERIAL_USE_POM=1


//!variant integrate_indirect_neeCache_pom_material_opaque_translucent_closestHit.rchit
//!>       SURFACE_MATERIAL_RESOLVE_TYPE_ACTIVE_MASK=SURFACE_MATERIAL_RESOLVE_TYPE_OPAQUE_TRANSLUCENT
//!>       NEE_CACHE_ENABLE=1
//!>       OPAQUE_MATERIAL_USE_POM=1


//!variant integrate_indirect_pom_material_rayportal_closestHit.rchit
//!>       SURFACE_MATERIAL_RESOLVE_TYPE_ACTIVE_MASK=SURFACE_MATERIAL_RESOLVE_TYPE_ALL
//!>       NEE_CACHE_ENABLE=0
//!>       OPAQUE_MATERIAL_USE_POM=1


//!variant integrate_indirect_neeCache_pom_material_rayportal_closestHit.rchit
//!>       SURFACE_MATERIAL_RESOLVE_TYPE_ACTIVE_MASK=SURFACE_MATERIAL_RESOLVE_TYPE_ALL
//!>       NEE_CACHE_ENABLE=1
//!>       OPAQUE_MATERIAL_USE_POM=1



/* NRC */

//!variant integrate_indirect_nrc_material_opaque_translucent_closestHit.rchit
//!>       SURFACE_MATERIAL_RESOLVE_TYPE_ACTIVE_MASK=SURFACE_MATERIAL_RESOLVE_TYPE_OPAQUE_TRANSLUCENT
//!>       ENABLE_NRC=1
//!>       NEE_CACHE_ENABLE=0


//!variant integrate_indirect_nrc_neeCache_material_opaque_translucent_closestHit.rchit
//!>       SURFACE_MATERIAL_RESOLVE_TYPE_ACTIVE_MASK=SURFACE_MATERIAL_RESOLVE_TYPE_OPAQUE_TRANSLUCENT
//!>       ENABLE_NRC=1
//!>       NEE_CACHE_ENABLE=1


//!variant integrate_indirect_nrc_material_rayportal_closestHit.rchit
//!>       SURFACE_MATERIAL_RESOLVE_TYPE_ACTIVE_MASK=SURFACE_MATERIAL_RESOLVE_TYPE_ALL
//!>       ENABLE_NRC=1
//!>       NEE_CACHE_ENABLE=0


//!variant integrate_indirect_nrc_neeCache_material_rayportal_closestHit.rchit
//!>       SURFACE_MATERIAL_RESOLVE_TYPE_ACTIVE_MASK=SURFACE_MATERIAL_RESOLVE_TYPE_ALL
//!>       ENABLE_NRC=1
//!>       NEE_CACHE_ENABLE=1


//!variant integrate_indirect_nrc_pom_material_opaque_translucent_closestHit.rchit
//!>       SURFACE_MATERIAL_RESOLVE_TYPE_ACTIVE_MASK=SURFACE_MATERIAL_RESOLVE_TYPE_OPAQUE_TRANSLUCENT
//!>       ENABLE_NRC=1
//!>       NEE_CACHE_ENABLE=0
//!>       OPAQUE_MATERIAL_USE_POM=1


//!variant integrate_indirect_nrc_neeCache_pom_material_opaque_translucent_closestHit.rchit
//!>       SURFACE_MATERIAL_RESOLVE_TYPE_ACTIVE_MASK=SURFACE_MATERIAL_RESOLVE_TYPE_OPAQUE_TRANSLUCENT
//!>       ENABLE_NRC=1
//!>       NEE_CACHE_ENABLE=1
//!>       OPAQUE_MATERIAL_USE_POM=1


//!variant integrate_indirect_nrc_pom_material_rayportal_closestHit.rchit
//!>       SURFACE_MATERIAL_RESOLVE_TYPE_ACTIVE_MASK=SURFACE_MATERIAL_RESOLVE_TYPE_ALL
//!>       ENABLE_NRC=1
//!>       NEE_CACHE_ENABLE=0
//!>       OPAQUE_MATERIAL_USE_POM=1


//!variant integrate_indirect_nrc_neeCache_pom_material_rayportal_closestHit.rchit
//!>       SURFACE_MATERIAL_RESOLVE_TYPE_ACTIVE_MASK=SURFACE_MATERIAL_RESOLVE_TYPE_ALL
//!>       ENABLE_NRC=1
//!>       NEE_CACHE_ENABLE=1
//!>       OPAQUE_MATERIAL_USE_POM=1



// WBOIT

//!variant integrate_indirect_material_opaque_translucent_closestHit_wboit.rchit
//!>       SURFACE_MATERIAL_RESOLVE_TYPE_ACTIVE_MASK=SURFACE_MATERIAL_RESOLVE_TYPE_OPAQUE_TRANSLUCENT
//!>       NEE_CACHE_ENABLE=0
//!>       UNORDERED_RESOLVE_WBOIT

//!variant integrate_indirect_neeCache_material_opaque_translucent_closestHit_wboit.rchit
//!>       SURFACE_MATERIAL_RESOLVE_TYPE_ACTIVE_MASK=SURFACE_MATERIAL_RESOLVE_TYPE_OPAQUE_TRANSLUCENT
//!>       NEE_CACHE_ENABLE=1
//!>       UNORDERED_RESOLVE_WBOIT

//!variant integrate_indirect_material_rayportal_closestHit_wboit.rchit
//!>       SURFACE_MATERIAL_RESOLVE_TYPE_ACTIVE_MASK=SURFACE_MATERIAL_RESOLVE_TYPE_ALL
//!>       NEE_CACHE_ENABLE=0
//!>       UNORDERED_RESOLVE_WBOIT

//!variant integrate_indirect_neeCache_material_rayportal_closestHit_wboit.rchit
//!>       SURFACE_MATERIAL_RESOLVE_TYPE_ACTIVE_MASK=SURFACE_MATERIAL_RESOLVE_TYPE_ALL
//!>       NEE_CACHE_ENABLE=1
//!>       UNORDERED_RESOLVE_WBOIT


//!variant integrate_indirect_pom_material_opaque_translucent_closestHit_wboit.rchit
//!>       SURFACE_MATERIAL_RESOLVE_TYPE_ACTIVE_MASK=SURFACE_MATERIAL_RESOLVE_TYPE_OPAQUE_TRANSLUCENT
//!>       NEE_CACHE_ENABLE=0
//!>       OPAQUE_MATERIAL_USE_POM=1
//!>       UNORDERED_RESOLVE_WBOIT

//!variant integrate_indirect_neeCache_pom_material_opaque_translucent_closestHit_wboit.rchit
//!>       SURFACE_MATERIAL_RESOLVE_TYPE_ACTIVE_MASK=SURFACE_MATERIAL_RESOLVE_TYPE_OPAQUE_TRANSLUCENT
//!>       NEE_CACHE_ENABLE=1
//!>       OPAQUE_MATERIAL_USE_POM=1
//!>       UNORDERED_RESOLVE_WBOIT

//!variant integrate_indirect_pom_material_rayportal_closestHit_wboit.rchit
//!>       SURFACE_MATERIAL_RESOLVE_TYPE_ACTIVE_MASK=SURFACE_MATERIAL_RESOLVE_TYPE_ALL
//!>       NEE_CACHE_ENABLE=0
//!>       OPAQUE_MATERIAL_USE_POM=1
//!>       UNORDERED_RESOLVE_WBOIT

//!variant integrate_indirect_neeCache_pom_material_rayportal_closestHit_wboit.rchit
//!>       SURFACE_MATERIAL_RESOLVE_TYPE_ACTIVE_MASK=SURFACE_MATERIAL_RESOLVE_TYPE_ALL
//!>       NEE_CACHE_ENABLE=1
//!>       OPAQUE_MATERIAL_USE_POM=1
//!>       UNORDERED_RESOLVE_WBOIT

/* NRC */

//!variant integrate_indirect_nrc_material_opaque_translucent_closestHit_wboit.rchit
//!>       SURFACE_MATERIAL_RESOLVE_TYPE_ACTIVE_MASK=SURFACE_MATERIAL_RESOLVE_TYPE_OPAQUE_TRANSLUCENT
//!>       ENABLE_NRC=1
//!>       NEE_CACHE_ENABLE=0
//!>       UNORDERED_RESOLVE_WBOIT

//!variant integrate_indirect_nrc_neeCache_material_opaque_translucent_closestHit_wboit.rchit
//!>       SURFACE_MATERIAL_RESOLVE_TYPE_ACTIVE_MASK=SURFACE_MATERIAL_RESOLVE_TYPE_OPAQUE_TRANSLUCENT
//!>       ENABLE_NRC=1
//!>       NEE_CACHE_ENABLE=1
//!>       UNORDERED_RESOLVE_WBOIT

//!variant integrate_indirect_nrc_material_rayportal_closestHit_wboit.rchit
//!>       SURFACE_MATERIAL_RESOLVE_TYPE_ACTIVE_MASK=SURFACE_MATERIAL_RESOLVE_TYPE_ALL
//!>       ENABLE_NRC=1
//!>       NEE_CACHE_ENABLE=0
//!>       UNORDERED_RESOLVE_WBOIT

//!variant integrate_indirect_nrc_neeCache_material_rayportal_closestHit_wboit.rchit
//!>       SURFACE_MATERIAL_RESOLVE_TYPE_ACTIVE_MASK=SURFACE_MATERIAL_RESOLVE_TYPE_ALL
//!>       ENABLE_NRC=1
//!>       NEE_CACHE_ENABLE=1
//!>       UNORDERED_RESOLVE_WBOIT


//!variant integrate_indirect_nrc_pom_material_opaque_translucent_closestHit_wboit.rchit
//!>       SURFACE_MATERIAL_RESOLVE_TYPE_ACTIVE_MASK=SURFACE_MATERIAL_RESOLVE_TYPE_OPAQUE_TRANSLUCENT
//!>       ENABLE_NRC=1
//!>       NEE_CACHE_ENABLE=0
//!>       OPAQUE_MATERIAL_USE_POM=1
//!>       UNORDERED_RESOLVE_WBOIT

//!variant integrate_indirect_nrc_neeCache_pom_material_opaque_translucent_closestHit_wboit.rchit
//!>       SURFACE_MATERIAL_RESOLVE_TYPE_ACTIVE_MASK=SURFACE_MATERIAL_RESOLVE_TYPE_OPAQUE_TRANSLUCENT
//!>       ENABLE_NRC=1
//!>       NEE_CACHE_ENABLE=1
//!>       OPAQUE_MATERIAL_USE_POM=1
//!>       UNORDERED_RESOLVE_WBOIT

//!variant integrate_indirect_nrc_pom_material_rayportal_closestHit_wboit.rchit
//!>       SURFACE_MATERIAL_RESOLVE_TYPE_ACTIVE_MASK=SURFACE_MATERIAL_RESOLVE_TYPE_ALL
//!>       ENABLE_NRC=1
//!>       NEE_CACHE_ENABLE=0
//!>       OPAQUE_MATERIAL_USE_POM=1
//!>       UNORDERED_RESOLVE_WBOIT

//!variant integrate_indirect_nrc_neeCache_pom_material_rayportal_closestHit_wboit.rchit
//!>       SURFACE_MATERIAL_RESOLVE_TYPE_ACTIVE_MASK=SURFACE_MATERIAL_RESOLVE_TYPE_ALL
//!>       ENABLE_NRC=1
//!>       NEE_CACHE_ENABLE=1
//!>       OPAQUE_MATERIAL_USE_POM=1
//!>       UNORDERED_RESOLVE_WBOIT

//!end-variants

#define RAY_PIPELINE
#define INTEGRATE_INDIRECT
#define RAB_HAS_PREVIOUS_GBUFFER 1
#define RAB_HAS_RTXDI_RESERVOIRS 1

// Note: ReSTIR GI Reservoirs are not available with NRC enabled, but 
// the usage of the reservoirs is gated on ReSTIR GI being enabled
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

[shader("closesthit")]
void main(inout PathState rayPayload : SV_RayPayload, in float2 barycentricCoordinates : SV_IntersectionAttributes)
{
  // Construct a Ray Hit Info from the hit

  RAY_HIT_INFO_CREATE_TRACE_RAY_HIT(rayHitInfo);

  // Invoke the Integrate Path Vertex function and handle the payload

  IndirectPathTextures indirectPathTextures;

  indirectPathTextures.PrimaryWorldPositionWorldTriangleNormal = PrimaryWorldPositionWorldTriangleNormal;
  indirectPathTextures.IndirectRadianceHitDistance = IndirectRadianceHitDistance;

  integratePathVertex(indirectPathTextures, rayHitInfo, rayPayload);
}
