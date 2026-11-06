/*
* Copyright (c) 2026, NVIDIA CORPORATION. All rights reserved.
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
#include "rtx/utility/render_target_motion.slangh"

RWStructuredBuffer<float4> results;

[shader("compute")]
[numthreads(1, 1, 1)]
void testRenderTargetMotion()
{
  // Left-handed perspective: NDC = (2*x/z, 3*y/z). Deliberately unequal FOV axes.
  const float4x4 projection = float4x4(
    2, 0, 0, 0,
    0, 3, 0, 0,
    0, 0, 1, -1,
    0, 0, 1, 0);
  const float4 anchor = float4(10, 20, 30, 1);
  // Oblique, non-uniform and skewed monitor basis, rather than unit tangents.
  const float4 du = float4(4, 0, 2, 0);
  const float4 dv = float4(1, -6, 3, 0);
  const float2 uv = float2(0.7f, 0.2f);
  float3 previous;
  float4x4 transform = renderTargetPreviousWorldTransform(anchor, du, dv, uv, projection);

  // Static remote point projects onto the current pixel: zero world motion.
  bool valid = renderTargetPreviousPosition(transform, float4(0.2f, 0.2f, 1, 1), previous);
  results[0] = float4(previous, valid);
  // Remote object translation. Expected UV delta = (+0.1, -0.3).
  valid = renderTargetPreviousPosition(transform, float4(0.3f, 0.4f, 1, 1), previous);
  results[1] = float4(previous, valid);
  // Same world displacement at twice the depth halves the UV displacement.
  valid = renderTargetPreviousPosition(transform, float4(0.5f, 0.6f, 2, 1), previous);
  results[2] = float4(previous, valid);

  // Previous camera translates +0.1 in x (world-to-view translates -0.1).
  float4x4 movedCamera = projection;
  movedCamera[0].w = -0.2f;
  transform = renderTargetPreviousWorldTransform(anchor, du, dv, uv, movedCamera);
  valid = renderTargetPreviousPosition(transform, float4(0.2f, 0.2f, 1, 1), previous);
  results[3] = float4(previous, valid);

  // A changed camera FOV contributes motion even for a static object.
  float4x4 changedFov = projection;
  changedFov[0].x = 4;
  changedFov[1].y = 6;
  transform = renderTargetPreviousWorldTransform(anchor, du, dv, uv, changedFov);
  valid = renderTargetPreviousPosition(transform, float4(0.2f, 0.2f, 1, 1), previous);
  results[4] = float4(previous, valid);

  // Previous screen translation is added after remote projection.
  transform = renderTargetPreviousWorldTransform(anchor + float4(2, 3, 4, 0), du, dv, uv, projection);
  valid = renderTargetPreviousPosition(transform, float4(0.2f, 0.2f, 1, 1), previous);
  results[5] = float4(previous, valid);

  // Texture tiling retains the current tile; previous UV outside the tile must not wrap.
  transform = renderTargetPreviousWorldTransform(anchor, du, dv, frac(uv + float2(2, -3)), projection);
  valid = renderTargetPreviousPosition(transform, float4(1.2f, 0.2f, 1, 1), previous);
  results[6] = float4(previous, valid);

  // Sky motion ignores remote camera translation, but includes its orientation/projection.
  transform = renderTargetPreviousWorldTransform(anchor, du, dv, uv, movedCamera);
  valid = renderTargetPreviousPosition(transform, float4(0.2f, 0.2f, 1, 0), previous);
  results[7] = float4(previous, valid);

  // Invalid projection must not produce NaN/Inf motion vectors.
  valid = renderTargetPreviousPosition(transform, float4(0, 0, 0, 1), previous);
  results[8] = float4(previous, valid);
  valid = renderTargetPreviousPosition(transform, float4(0, 0, -1, 1), previous);
  results[9] = float4(previous, valid);

  // Compose a second screen in homogeneous space, then divide only at the end.
  const float4x4 outer = renderTargetPreviousWorldTransform(anchor, du, dv, uv, projection);
  transform = renderTargetPreviousWorldTransform(
    mul(outer, float4(0.2f, 0.2f, 1, 1)),
    mul(outer, float4(2, 0, 0.5f, 0)), mul(outer, float4(0, -2, 0.25f, 0)),
    float2(0.5f), projection);
  valid = renderTargetPreviousPosition(transform, float4(0.1f, 0.1f, 1, 1), previous);
  results[10] = float4(previous, valid);

  // Right-handed projection and a camera looking along negative Z.
  float4x4 rightHanded = projection;
  rightHanded[3].z = -1;
  transform = renderTargetPreviousWorldTransform(anchor, du, dv, uv, rightHanded);
  valid = renderTargetPreviousPosition(transform, float4(0.2f, 0.2f, -1, 1), previous);
  results[11] = float4(previous, valid);

  // Previous camera yaw maps (x, y, z) to (z, y, -x).
  const float4x4 yawedCamera = float4x4(
    0, 0, 2, 0,
    0, 3, 0, 0,
    -1, 0, 0, -1,
    -1, 0, 0, 0);
  transform = renderTargetPreviousWorldTransform(anchor, du, dv, uv, yawedCamera);
  valid = renderTargetPreviousPosition(transform, float4(-1, 0.2f, 0.2f, 1), previous);
  results[12] = float4(previous, valid);

  // A rotating screen changes its previous UV basis independently of its anchor motion.
  transform = renderTargetPreviousWorldTransform(anchor,
    float4(0, 4, 2, 0), float4(6, 1, 3, 0), uv, projection);
  valid = renderTargetPreviousPosition(transform, float4(0.3f, 0.4f, 1, 1), previous);
  results[13] = float4(previous, valid);

  // The main camera then projects the previous screen point, including its own translation.
  transform = renderTargetPreviousWorldTransform(anchor, du, dv, uv, projection);
  valid = renderTargetPreviousPosition(transform, float4(0.2f, 0.2f, 1, 1), previous);
  float4x4 mainCamera = projection;
  mainCamera[0].w = -4;
  const float4 previousClip = mul(mainCamera, float4(previous, 1));
  const float2 previousNdc = previousClip.xy / previousClip.w;
  results[14] = float4(previousNdc, 0, valid);

  // Non-finite inputs and overflow cannot escape as motion vectors.
  valid = renderTargetPreviousPosition(transform, float4(asfloat(0x7f800000u), 0, 1, 1), previous);
  results[15] = float4(previous, valid);
  float4x4 overflow = transform;
  overflow[0].w = 3.0e38f;
  valid = renderTargetPreviousPosition(overflow, float4(0, 0, 0.0001f, 1), previous);
  results[16] = float4(previous, valid);

  // Previous skinned vertices and transformed UVs retain a skewed, non-unit basis.
  float3 vertices[3] = {float3(1,2,3), float3(5,2,5), float3(2,-4,6)};
  float2 texcoords[3] = {float2(0,0), float2(2,0), float2(0,-3)};
  float3 dpdu;
  float3 dpdv;
  valid = renderTargetTextureBasis(vertices, texcoords, dpdu, dpdv);
  results[17] = float4(dpdu, valid);
  results[18] = float4(dpdv, valid);
  texcoords[2] = texcoords[1];
  valid = renderTargetTextureBasis(vertices, texcoords, dpdu, dpdv);
  results[19] = float4(dpdu + dpdv, valid);
  texcoords[2] = float2(asfloat(0x7f800000), 0);
  valid = renderTargetTextureBasis(vertices, texcoords, dpdu, dpdv);
  results[20] = float4(dpdu + dpdv, valid);
}
