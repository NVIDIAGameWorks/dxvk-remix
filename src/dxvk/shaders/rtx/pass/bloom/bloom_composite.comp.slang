/*
* Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.
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
#include "rtx/pass/bloom/bloom.h"

layout(binding = BLOOM_COMPOSITE_COLOR_INPUT_OUTPUT)
RWTexture2D<float4> InOutColorBuffer; 

layout(binding = BLOOM_COMPOSITE_BLOOM)
Sampler2D InBloomBuffer;

layout(push_constant)
ConstantBuffer<BloomCompositeArgs> cb;

[shader("compute")]
[numthreads(16, 16, 1)]
void main(uint2 ipos : SV_DispatchThreadID)
{
    if (any(ipos >= cb.imageSize))
        return;
    
    float2 uv = saturate((float2(ipos) + float2(0.5, 0.5)) / float2(cb.imageSize));

    float4 src = InBloomBuffer.SampleLevel(uv, 0);
    float4 dst = InOutColorBuffer[ipos];

    if (any(isnan(dst)) || any(isinf(dst)))
        return;

    float4 result = src * cb.blendFactor + dst * (1.0 - cb.blendFactor);

    InOutColorBuffer[ipos] = result;
}
