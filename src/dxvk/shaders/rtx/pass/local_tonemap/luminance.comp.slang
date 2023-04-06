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
#include "rtx/pass/local_tonemap/local_tonemapping.slangh"
#include "rtx/pass/tonemap/tonemapping.slangh"

layout(binding = LUMINANCE_ORIGINAL)
Texture2D<vec4> OriginalTexture;

layout(binding = LUMINANCE_OUTPUT)
RWTexture2D<vec4> OutLuminanceTexture;

layout(binding = LUMINANCE_DEBUG_VIEW_OUTPUT)
RWTexture2D<float4> DebugView;

layout(binding = LUMINANCE_EXPOSURE)
RWTexture1D<float> AutoExposureTexture;

#include "rtx/utility/debug_view_helpers.slangh"

layout(push_constant)
ConstantBuffer<LuminanceArgs> cb;

[shader("compute")]
[numthreads(16, 16, 1)]
void main(uint2 threadId : SV_DispatchThreadID)
{
  float avgExposure = getExposure(AutoExposureTexture, cb.enableAutoExposure, cb.exposure);

  // Tonemap three syntetic exposures and produce their luminances.
  vec3 inpColor = OriginalTexture[threadId].xyz * avgExposure;
  float highlights = calcBt709Luminance(linearToGamma(ACESFilm(inpColor * cb.highlights)));
  float midtones   = calcBt709Luminance(linearToGamma(ACESFilm(inpColor)));
  float shadows    = calcBt709Luminance(linearToGamma(ACESFilm(inpColor * cb.shadows)));
  vec3 cols = vec3(highlights, midtones, shadows);

  OutLuminanceTexture[threadId] = vec4(cols, 1.0);
}
