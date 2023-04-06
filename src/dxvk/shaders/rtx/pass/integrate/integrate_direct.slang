/*
* Copyright (c) 2022-2023, NVIDIA CORPORATION. All rights reserved.
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

//!variant integrate_direct_rayquery.comp

//!variant integrate_direct_rayquery_raygen.rgen
//!>       RAY_PIPELINE

//!end-variants

#define USE_32BIT_RAY_DIRECTION 1
// Note: Use thin film in the direct integration pass for its usage in direct lighting (as this is where
// the effect is most prominent outside of PSR).
#define OPAQUE_MATERIAL_USE_THIN_FILM 1
#define RAB_HAS_RTXDI_RESERVOIRS 1

#include "rtx/pass/common_binding_indices.h"
#include "rtx/pass/common_bindings.slangh"
#include "rtx/pass/integrate/integrate_direct_binding_indices.h"
#include "rtx/pass/integrate/integrate_direct_bindings.slangh"
// Note: Needed for declaration of integration ray payload structure
#include "rtx/algorithm/integrator.slangh"

#include "rtx/pass/integrate/integrate_direct.slangh"

#ifdef RAY_PIPELINE
[shader("raygeneration")]
void main()
{
  integrate_direct_pass(DispatchRaysIndex().xy);
}
#else
[shader("compute")]
[numthreads(16, 8, 1)]
void main(uint2 ipos : SV_DispatchThreadID)
{ 
  integrate_direct_pass(ipos);
}
#endif