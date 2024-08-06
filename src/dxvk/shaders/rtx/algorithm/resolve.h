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
#pragma once

#include "rtx/concept/ray_portal/ray_portal.slangh"

// Resolve Constants

// Todo: These should be part of the generic function non-type parameters and should control
// compile time paths.

// Default resolve mode in the absence of any other.
static const uint8_t resolveModeDefault = uint8_t(0);

// Indicates that the proper ray masking will be set up to ignore hits on surfaces using desired approximations such that a
// separate unordered pass can accumulate the results afterwards. Note this means that for example in Primary rays where hits
// on translucency are desired the ray masks and resolve approximation flags should reflect this, whereas for NEE rays where
// only approximations of translucency are needed the masks and flags should be set differently (but still match in intent).
static const uint8_t resolveModeSeparateUnorderedApproximations = uint8_t(1 << 0);
static const uint8_t resolveModeAlteredDirectionNotify = uint8_t(1 << 1);
static const uint8_t resolveModeRayPortalNotify = uint8_t(1 << 2);

// Indicates that all surfaces using opacity should be subject to emissive and attenuation approximations,
// meaning resolve hits are skipped but the effective emissive and attenuation approximation is still accumulated in.
// This is most useful for NEE rays where only visibility and attenuation needs to be considered.
static const uint8_t resolveModeOpacityTransmissionApprox = uint8_t(1 << 3);
// Indicates that all surfaces using emissive opacity modes should be subject to emissive and attenuation approximations.
// This is generally useful for both NEE and other rays as emissive tends to look fine even without lighting despite this
// being physically incorrect.
static const uint8_t resolveModeEmissiveOpacityTransmissionApprox = uint8_t(1 << 4);
// Indicates that all surfaces using opacity should be subject to lighting approximations. This provides a noise-free
// lighting contribution to particles useful for making non-emissive opacity particles look properly lit without the difficulty
// of denoising them. This flag should be used in unordered integration rays only. The macro RESOLVE_OPACITY_LIGHTING_APPROXIMATION
// should be used in addition to it (this is because the light approximation code references a texture not present in other passes).
static const uint8_t resolveModeForceOpacityLightingApprox = uint8_t(1 << 5);
// Todo: Currently unused with no functionality, in the future this will be used for indicating that transmission in translucency
// should be approximated (again useful for NEE for translucent shadows).
static const uint8_t resolveModeTranslucencyTransmissionApprox = uint8_t(1 << 6);
// Enables processing and blending of decals encountered along the ray, but only when cb.enableDecalMaterialBlending is true.
// Decal surfaces are stored into the DecalMaterial texture and continueResolving flag is enabled for them.
// Non-decal surfaces get the previously encountered decals applied on top of them.
static const uint8_t resolveModeDecalMaterialBlending = uint8_t(1 << 7);

// Resolve Helper Structures
// Todo: Potentially reduce duplication between these structures and the integrator path state as much is in common between them.

// Todo: Remove when Slang is added as this is only here to simplify implementation without generic functions.
struct HackGenericState
{
  vec3 origin;
  float16_t coneRadius;
  float16_t coneSpreadAngle;
  f16vec3 direction;
  bool directionAltered;
  uint8_t firstRayPortal;

  float segmentHitDistance;

  bool continueResolving;

  uint8_t rayMask;
  PortalSpace2BitsType portalSpace;

  u16vec2 pixelCoordinate;
  bool decalEncountered;

#ifdef RAY_TRACING_PRIMARY_RAY
  bool isStochasticAlphaBlend;
  f16vec4 accumulatedRotation;
#endif
};

struct EmptyExtraArgs {};
