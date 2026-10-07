/*
* Copyright (c) 2025, NVIDIA CORPORATION. All rights reserved.
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

#include "sharc_binding_indices.h"
#include "rtx/pass/sharc/sharc_integrator_utilities.slangh"
#include "rtx/pass/raytrace_args.h"

// Inputs

layout(binding = SHARC_BINDING_CONSTANTS_INPUT)
ConstantBuffer<RaytraceArgs> cb;

// Inputs / Outputs

layout(binding = SHARC_BINDING_HASH_ENTRIES_INPUT_OUTPUT)
RWStructuredBuffer<uint64_t> SharcHashEntriesBuffer;

layout(binding = SHARC_BINDING_ACCUMULATION_OUTPUT)
RWStructuredBuffer<SharcAccumulationData> SharcAccumulationBuffer;

layout(binding = SHARC_BINDING_RESOLVED_INPUT_OUTPUT)
RWStructuredBuffer<SharcPackedData> SharcResolvedBuffer;

[shader("compute")]
[numthreads(SHARC_COMPUTE_LINEAR_BLOCK_SIZE, 1, 1)]
void main(uint2 threadId : SV_DispatchThreadID)
{
  SharcParameters sharcParameters = Sharc::createSharcParameters(cb.sharcArgs);

  SharcResolveParameters resolveParameters;
  resolveParameters.accumulationFrameNum = cb.sharcArgs.accumulationFrameNum;
  resolveParameters.responsiveFrameNum = cb.sharcArgs.accumulationFrameNum;
  resolveParameters.staleFrameNumMax = cb.sharcArgs.staleFrameNum;
  resolveParameters.cameraPositionPrev = cb.sharcArgs.cameraPositionPrev;
  resolveParameters.frameIndex = cb.frameIdx;

  SharcResolveEntry(threadId.x, sharcParameters, resolveParameters);
}
