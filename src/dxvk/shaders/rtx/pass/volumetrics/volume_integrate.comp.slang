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

//!variant volume_integrate_rayquery.comp

//!end-variants

#include "rtx/pass/common_bindings.slangh"
#include "rtx/pass/volumetrics/volume_integrate_bindings.slangh"

#include "rtx/pass/volumetrics/volume_integrate.slangh"

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
  volume_integrate_pass(ipos);
}
#endif
