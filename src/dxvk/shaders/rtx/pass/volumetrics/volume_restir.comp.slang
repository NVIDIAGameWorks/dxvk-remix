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

//!variant volume_restir_initial.comp
//!>       VOLUME_RESTIR_MODE=INITIAL
//!variant volume_restir_visibility.comp
//!>       VOLUME_RESTIR_MODE=VISIBILITY
//!variant volume_restir_temporal.comp
//!>       VOLUME_RESTIR_MODE=TEMPORAL
//!variant volume_restir_spatial_resampling.comp
//!>       VOLUME_RESTIR_MODE=SPATIAL_REUSE

//!end-variants

// Note: Required to enable volumetric radiance scale functionality on this pass to ensure any
// volumetric ReSTIR operations done with respect to light radiance take it into account (e.g.
// ensuring lights that are significantly boosted are sampled from more).
#define ENABLE_VOLUMETRIC_RADIANCE_SCALE 1

#define INITIAL       0
#define VISIBILITY    1
#define TEMPORAL      2
#define SPATIAL_REUSE 3

#include "rtx/pass/common_bindings.slangh"
#include "rtx/pass/volumetrics/volume_restir_bindings.slangh"

#include "rtx/pass/volumetrics/volume_restir.slangh"

#ifdef RAY_PIPELINE
[shader("raygeneration")]
void main()
{
  volume_integrate_pass(DispatchRaysIndex().xyz);
}
#else
[shader("compute")]
[numthreads(16, 8, 1)]
void main(uint3 ipos : SV_DispatchThreadID)
{ 
  volume_restir_pass(ipos);
}
#endif
