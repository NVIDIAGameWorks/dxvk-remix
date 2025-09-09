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

//!variant gbuffer_raygen.rgen
//!>       RAY_TRACING_PRIMARY_RAY
//!>       RT_TRACE_RAY
//!>       RAY_PIPELINE


//!variant gbuffer_raygen_ser.rgen
//!>       RAY_TRACING_PRIMARY_RAY
//!>       RT_TRACE_RAY
//!>       RAY_PIPELINE
//!>       RT_SHADER_EXECUTION_REORDERING


//!variant gbuffer_rayquery.comp
//!>       RAY_TRACING_PRIMARY_RAY


//!variant gbuffer_rayquery_raygen.rgen
//!>       RAY_TRACING_PRIMARY_RAY
//!>       RAY_PIPELINE


//!variant gbuffer_psr_raygen.rgen
//!>       RAY_TRACING_PRIMARY_RAY
//!>       RT_TRACE_RAY
//!>       RAY_PIPELINE
//!>       GBUFFER_PSR


//!variant gbuffer_psr_raygen_ser.rgen
//!>       RAY_TRACING_PRIMARY_RAY
//!>       RT_TRACE_RAY
//!>       RAY_PIPELINE
//!>       RT_SHADER_EXECUTION_REORDERING
//!>       GBUFFER_PSR


//!variant gbuffer_psr_rayquery.comp
//!>       RAY_TRACING_PRIMARY_RAY
//!>       GBUFFER_PSR


//!variant gbuffer_psr_rayquery_raygen.rgen
//!>       RAY_TRACING_PRIMARY_RAY
//!>       RAY_PIPELINE
//!>       GBUFFER_PSR


/* NRC */

//!variant gbuffer_raygen_nrc.rgen
//!>       RAY_TRACING_PRIMARY_RAY
//!>       RT_TRACE_RAY
//!>       RAY_PIPELINE
//!>       ENABLE_NRC=1


//!variant gbuffer_raygen_ser_nrc.rgen
//!>       RAY_TRACING_PRIMARY_RAY
//!>       RT_TRACE_RAY
//!>       RAY_PIPELINE
//!>       RT_SHADER_EXECUTION_REORDERING
//!>       ENABLE_NRC=1


//!variant gbuffer_rayquery_nrc.comp
//!>       RAY_TRACING_PRIMARY_RAY
//!>       ENABLE_NRC=1


//!variant gbuffer_rayquery_raygen_nrc.rgen
//!>       RAY_TRACING_PRIMARY_RAY
//!>       RAY_PIPELINE
//!>       ENABLE_NRC=1


//!variant gbuffer_psr_raygen_nrc.rgen
//!>       RAY_TRACING_PRIMARY_RAY
//!>       RT_TRACE_RAY
//!>       RAY_PIPELINE
//!>       GBUFFER_PSR
//!>       ENABLE_NRC=1


//!variant gbuffer_psr_raygen_ser_nrc.rgen
//!>       RAY_TRACING_PRIMARY_RAY
//!>       RT_TRACE_RAY
//!>       RAY_PIPELINE
//!>       RT_SHADER_EXECUTION_REORDERING
//!>       GBUFFER_PSR
//!>       ENABLE_NRC=1


//!variant gbuffer_psr_rayquery_nrc.comp
//!>       RAY_TRACING_PRIMARY_RAY
//!>       GBUFFER_PSR
//!>       ENABLE_NRC=1


//!variant gbuffer_psr_rayquery_raygen_nrc.rgen
//!>       RAY_TRACING_PRIMARY_RAY
//!>       RAY_PIPELINE
//!>       GBUFFER_PSR
//!>       ENABLE_NRC=1



// WBOIT

//!variant gbuffer_raygen_wboit.rgen
//!>       RAY_TRACING_PRIMARY_RAY
//!>       RT_TRACE_RAY
//!>       RAY_PIPELINE
//!>       UNORDERED_RESOLVE_WBOIT

//!variant gbuffer_raygen_ser_wboit.rgen
//!>       RAY_TRACING_PRIMARY_RAY
//!>       RT_TRACE_RAY
//!>       RAY_PIPELINE
//!>       RT_SHADER_EXECUTION_REORDERING
//!>       UNORDERED_RESOLVE_WBOIT

//!variant gbuffer_rayquery_wboit.comp
//!>       RAY_TRACING_PRIMARY_RAY
//!>       UNORDERED_RESOLVE_WBOIT

//!variant gbuffer_rayquery_raygen_wboit.rgen
//!>       RAY_TRACING_PRIMARY_RAY
//!>       RAY_PIPELINE
//!>       UNORDERED_RESOLVE_WBOIT

//!variant gbuffer_psr_raygen_wboit.rgen
//!>       RAY_TRACING_PRIMARY_RAY
//!>       RT_TRACE_RAY
//!>       RAY_PIPELINE
//!>       GBUFFER_PSR
//!>       UNORDERED_RESOLVE_WBOIT

//!variant gbuffer_psr_raygen_ser_wboit.rgen
//!>       RAY_TRACING_PRIMARY_RAY
//!>       RT_TRACE_RAY
//!>       RAY_PIPELINE
//!>       RT_SHADER_EXECUTION_REORDERING
//!>       GBUFFER_PSR
//!>       UNORDERED_RESOLVE_WBOIT

//!variant gbuffer_psr_rayquery_wboit.comp
//!>       RAY_TRACING_PRIMARY_RAY 
//!>       GBUFFER_PSR
//!>       UNORDERED_RESOLVE_WBOIT

//!variant gbuffer_psr_rayquery_raygen_wboit.rgen
//!>       RAY_TRACING_PRIMARY_RAY
//!>       RAY_PIPELINE
//!>       GBUFFER_PSR
//!>       UNORDERED_RESOLVE_WBOIT

/* NRC */

//!variant gbuffer_raygen_nrc_wboit.rgen
//!>       RAY_TRACING_PRIMARY_RAY
//!>       RT_TRACE_RAY
//!>       RAY_PIPELINE
//!>       ENABLE_NRC=1
//!>       UNORDERED_RESOLVE_WBOIT 

//!variant gbuffer_raygen_ser_nrc_wboit.rgen
//!>       RAY_TRACING_PRIMARY_RAY
//!>       RT_TRACE_RAY
//!>       RAY_PIPELINE
//!>       RT_SHADER_EXECUTION_REORDERING
//!>       ENABLE_NRC=1
//!>       UNORDERED_RESOLVE_WBOIT

//!variant gbuffer_rayquery_nrc_wboit.comp
//!>       RAY_TRACING_PRIMARY_RAY
//!>       ENABLE_NRC=1
//!>       UNORDERED_RESOLVE_WBOIT

//!variant gbuffer_rayquery_raygen_nrc_wboit.rgen
//!>       RAY_TRACING_PRIMARY_RAY
//!>       RAY_PIPELINE
//!>       ENABLE_NRC=1
//!>       UNORDERED_RESOLVE_WBOIT

//!variant gbuffer_psr_raygen_nrc_wboit.rgen
//!>       RAY_TRACING_PRIMARY_RAY
//!>       RT_TRACE_RAY
//!>       RAY_PIPELINE
//!>       GBUFFER_PSR
//!>       ENABLE_NRC=1
//!>       UNORDERED_RESOLVE_WBOIT

//!variant gbuffer_psr_raygen_ser_nrc_wboit.rgen
//!>       RAY_TRACING_PRIMARY_RAY
//!>       RT_TRACE_RAY
//!>       RAY_PIPELINE
//!>       RT_SHADER_EXECUTION_REORDERING
//!>       GBUFFER_PSR
//!>       ENABLE_NRC=1
//!>       UNORDERED_RESOLVE_WBOIT

//!variant gbuffer_psr_rayquery_nrc_wboit.comp
//!>       RAY_TRACING_PRIMARY_RAY
//!>       GBUFFER_PSR
//!>       ENABLE_NRC=1
//!>       UNORDERED_RESOLVE_WBOIT

//!variant gbuffer_psr_rayquery_raygen_nrc_wboit.rgen
//!>       RAY_TRACING_PRIMARY_RAY
//!>       RAY_PIPELINE
//!>       GBUFFER_PSR
//!>       ENABLE_NRC=1
//!>       UNORDERED_RESOLVE_WBOIT


//!end-variants

// Note: Opacity lighting approximation used in the GBuffer pass due to it being most visually
// prominent on particles in primary rays and important in PSR.
#define RESOLVE_OPACITY_LIGHTING_APPROXIMATION 1
// Note: Use thin film in the GBuffer pass for its usage in PSR on mirrorlike surfaces.
#define OPAQUE_MATERIAL_USE_THIN_FILM 1
#define OPAQUE_MATERIAL_USE_POM 1

#ifdef GBUFFER_PSR
// Note: This is done in primary rays only currently so that attenuation from volumetrics can be factored in
// similar to how it was done previously when it was simply applied in compositing via the total virtual hit
// distance, and ensuring that it does not attenuate secondary rays (since they do not receive inscattering and
// attenuating them would cause large amounts of energy loss as a result).
#define RESOLVER_USE_VOLUMETRIC_ATTENUATION 1
#endif

#include "rtx/pass/common_binding_indices.h"
#include "rtx/pass/common_bindings.slangh"
#include "rtx/pass/gbuffer/gbuffer_binding_indices.h"
#include "rtx/pass/gbuffer/gbuffer_bindings.slangh"
// Note: Needed for declaration of g-buffer ray payload structure
#include "rtx/algorithm/geometry_resolver_state.slangh"

#include "rtx/concept/camera/camera.slangh"
#include "rtx/algorithm/geometry_resolver.slangh"

#ifdef RAY_PIPELINE
[shader("raygeneration")]
void main()
{
  #ifdef GBUFFER_PSR
  geometryPSRResolverPass(DispatchRaysIndex().xy);
  #else
  geometryResolver(DispatchRaysIndex().xy);
  #endif
}
#else
[shader("compute")]
[numthreads(16, 8, 1)]
void main(uint2 ipos : SV_DispatchThreadID)
{
  if (any(ipos >= cb.camera.resolution))
  {
    return;
  }

  #ifdef GBUFFER_PSR
  geometryPSRResolverPass(ipos);
  #else
  geometryResolver(ipos);
  #endif
}
#endif