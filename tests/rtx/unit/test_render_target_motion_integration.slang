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
// Execute production guide output and temporal gating with CPU resource adapters.
#include "render_target_motion_test_bindings.slangh"
#include "rtx/utility/virtual_hit_projection.slangh"
#include "rtx/algorithm/render_target_motion_output.slangh"
#include "rtx/algorithm/rtxdi/rtxdi_temporal_history.slangh"

[shader("compute")]
[numthreads(1, 1, 1)]
void testRenderTargetMotionIntegration()
{
  const float4x4 identity = float4x4(1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1);
  const float4x4 projection = float4x4(2,0,0,0, 0,3,0,0, 0,0,1,-1, 0,0,1,0);
  const float4x4 inverseProjection = float4x4(0.5,0,0,0, 0,1.0/3,0,0, 0,0,0,1, 0,0,-1,1);
  for (uint test = 0; test < 18; ++test)
  {
    resultBase = test * 16;
    for (uint slot = 0; slot < 16; ++slot) { results[resultBase + slot] = float4(-99); }
    cb = (TestConstants)0;
    cb.camera.viewToProjection = projection;
    cb.camera.worldToView = identity;
    cb.camera.viewToWorld = identity;
    cb.camera.projectionToView = inverseProjection;
    cb.camera.worldToProjectionJittered = projection;
    cb.camera.prevWorldToProjection = projection;
    cb.camera.prevProjectionToView = inverseProjection;
    cb.camera.prevViewToWorld = identity;
    cb.camera.resolution = uint2(64,32);
    cb.camera.projectionToPrevProjection = identity;
    cb.renderTargetCamera = cb.camera;
    cb.writePrimaryVirtualMotionVector = true;
    cb.writeSecondaryDenoisingGuides = true;
    cb.enableDLSSRR = test == 5 || test == 6 || test == 7 || test == 9 || test == 11;
    cb.setLogValueForDisocclusionMaskForDLSSRR = test == 7;
    cb.forceStaticSceneMotionVectors = test == 8;
    if (test == 16)
    {
      cb.camera.prevViewToWorld = float4x4(0,0,1,50, 0,1,0,20, -1,0,0,5, 0,0,0,1);
      const float4x4 previousWorldToView = float4x4(0,0,-1,5, 0,1,0,-20, 1,0,0,-50, 0,0,0,1);
      cb.camera.prevWorldToProjection = mul(projection, previousWorldToView);
    }
    if (test == 17)
    {
      const float4x4 rightProjection = float4x4(2,0,0,0, 0,3,0,0, 0,0,-1,-1, 0,0,-1,0);
      const float4x4 rightInverse = float4x4(0.5,0,0,0, 0,1.0/3,0,0, 0,0,0,-1, 0,0,-1,1);
      cb.camera.projectionToView = rightInverse;
      cb.camera.prevProjectionToView = rightInverse;
      cb.camera.worldToProjectionJittered = rightProjection;
      cb.camera.prevWorldToProjection = rightProjection;
      cb.camera.flags = rightHandedFlag;
    }
    if (test == 9 || test == 10) { cb.camera.resolution = uint2(63,31); }
    GeometryPSRResolverState state = (GeometryPSRResolverState)0;
    state.primarySurface = !(test == 3 || test == 4 || test == 6 || test == 15);
    state.pixelCoordinate = uint2(31,15);
    state.compactedCoordinate = test == 11 ? uint2(0xffff) : uint2(3,4);
    state.accumulatedHitDistance = 10;
    state.accumulatedRotation = float4(0,0,0,1);
    const Ray incomingRay = rayCreatePrimaryFromPixel(cb.camera, state.pixelCoordinate, false);
    const float3 currentPosition = rayEvaluate(incomingRay, 10);
    Surface screen = makeScreen(currentPosition);
    if (test == 13) { screen.uv[1] = 0; screen.uv[2] = 0; }
    SurfaceInteraction screenInteraction = makeScreenInteraction(screen);
    screenInteraction.textureCoordinates = float2(0.5);
    const Ray cameraRay = restoreRenderTargetCameraRay(cb.renderTargetCamera,
      sphereDirectionToSnorm2x16(float3(0,0,1)), incomingRay);
    beginRenderTargetMotion(state, screenInteraction, cameraRay);
    state.accumulatedHitDistance = 11;
    bool composedAccepts = true;
    float3 objectMotion = test == 0 || test == 4 || test == 14 ? float3(0) : float3(0,0,-2);
    if (test == 2) { objectMotion.z = -1; }
    if (test == 12) { objectMotion = float3(0.1,0,0); }
    if (test == 9 || test == 10)
    {
      // Execute the production nested entry using both the negative and positive chains.
      screen = makeScreen(float3(0,0,test == 9 ? -1 : 1));
      screenInteraction = makeScreenInteraction(screen);
      screenInteraction.motion = float3(0,0,test == 9 ? -2 : 0);
      beginRenderTargetMotion(state, screenInteraction, cameraRay);
      state.accumulatedHitDistance = 12;
      if (test == 10) { objectMotion = 0; }
      float3 uncheckedPrevious;
      composedAccepts = renderTargetPreviousPosition(state.renderTargetPreviousWorld,
        float4(0,0,test == 9 ? -1 : 1,1), uncheckedPrevious);
    }
    if (test == 13) { objectMotion = 0; }
    const bool miss = test == 14 || test == 15;
    if (test == 15) { state.renderTargetDirection.z = -1; }
    outputRenderTargetMotion(state, objectMotion, miss);

    const bool active = test != 11;
    const float4 motion = state.primarySurface ? PrimaryVirtualMotionVector[state.compactedCoordinate] : SecondaryVirtualMotionVector[state.compactedCoordinate];
    if (active)
    {
      // NRD's world-space reprojection with the actual quantized output, not pre-store math.
      const float4 previousClip = mul(cb.camera.prevWorldToProjection, float4(currentPosition + motion.xyz, 1));
      const float2 previousUV = float2(0.5 + 0.5 * previousClip.x / previousClip.w, 0.5 - 0.5 * previousClip.y / previousClip.w);
      results[resultBase + 13] = float4(previousUV, all(previousUV >= 0) && all(previousUV < 1), motion.w);
      ReprojectionConfidence[state.pixelCoordinate] = 0.5f;
      RTXDI_Reservoir reservoir; reservoir.sample = 7;
      RAB_RandomSamplerState rng = (RAB_RandomSamplerState)0;
      int2 previousPixel; bool similar;
      rtxdiTemporalResamplingWithHistoryValidity(motion.w >= 0, reservoir, state.pixelCoordinate,
        (RAB_Surface)0, rng, (RTXDI_TemporalResamplingParameters)0, previousPixel, similar);
      rtxdiStoreTemporalReprojection(motion.w >= 0, state.pixelCoordinate, previousPixel, similar, true);
      results[resultBase + 14] = float4(reservoir.sample, rng.calls, similar, state.renderTargetHistoryInvalid);
      results[resultBase + 15] = float4(float2(previousPixel), composedAccepts, 1);
    }
  }
  // Material-adjusted UVs must survive production octahedral packing and PSR f16 direction storage.
  resultBase = 18 * 16;
  const float2 rawUV = float2(0.2,0.3);
  const float2 pomUV = rawUV + float2(0.17,-0.11);
  const float3 sampledDirection = normalize(float3((pomUV.x*2-1)*0.5,(1-pomUV.y*2)/3,1));
  const uint packedDirection = sphereDirectionToSnorm2x16(sampledDirection);
  Ray viewRay = (Ray)0;
  const Ray restoredRay = restoreRenderTargetCameraRay(cb.renderTargetCamera, packedDirection, viewRay);
  const float3 restoredDirection = restoredRay.direction;
  const float2 restoredUV = renderTargetSampleUV(restoredDirection, identity, projection);
  cb.camera = cb.renderTargetCamera;
  cb.camera.resolution = uint2(1);
  GeometryPSRResolverState restoredState = (GeometryPSRResolverState)0;
  restoredState.accumulatedHitDistance = 10;
  restoredState.accumulatedRotation = float4(0,0,0,1);
  Surface restoredScreen = makeScreen(float3(0,0,10));
  SurfaceInteraction rawInteraction = makeScreenInteraction(restoredScreen);
  rawInteraction.textureCoordinates = rawUV;
  beginRenderTargetMotion(restoredState, rawInteraction, restoredRay);
  const float4x4 mapping = restoredState.renderTargetPreviousWorld;
  float3 restoredPosition;
  bool valid = renderTargetPreviousPosition(mapping,float4(restoredDirection,0),restoredPosition);
  results[resultBase] = float4(restoredUV, length(restoredDirection - sampledDirection), valid);
  results[resultBase + 1] = float4(restoredPosition,1);
  results[resultBase + 3] = float4(restoredRay.direction - quantizeHalf(snorm2x16ToSphereDirection(packedDirection)), 1);
  // A quantized edge sample must not wrap from one edge to the other.
  const float2 edgeUV = renderTargetSampleUV(float3(-0.50001,0,1),identity,projection);
  const float4x4 edgeMapping = renderTargetPreviousWorldTransform(float4(0,0,10,1),
    float4(2,0,0,0),float4(0,-2,0,0),edgeUV,projection);
  valid = renderTargetPreviousPosition(edgeMapping,float4(-0.50001,0,1,0),restoredPosition);
  results[resultBase + 2] = float4(restoredPosition,valid);
}
