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
#pragma once

#include "rtx/utility/shader_types.h"

// Arguments of one step of the water simulation, see water_sim.comp.slang. The height field is a torus of gridSize x gridSize
// cells anchored to the world: a texel holds the world cell that is congruent to it modulo gridSize inside the current window.
struct WaterSimStepArgs {
  int originX;
  int originY;
  int oldOriginX;
  int oldOriginY;

  uint gridSize;
  uint disturbanceCount;
  uint writeDisplay;
  // Is 1 when the step moves the surface in time, and 0 when it only applies disturbances and clears cells.
  float advance;

  float waveCoefficient;
  float velocityKeep;
  float maxHeight;
  float cellSize;

  // Fraction of the height kept per step, 1 when not advancing.
  float heightKeep;
  // Scale of the long wind waves in the displayed height, 0 without them.
  float waveHeightScale;
  // The long wind waves, cascade 0 of the wave generator, see WaterSimArgs.
  vec2 wave0Rotation;
  float wave0InvTileSize;
  float gustInvSize;
  float gustTime;
  float gustMin;
  // Fraction of the landed spray kept per step, 1 when not advancing.
  float sprayKeep;
  // Fraction of the spray in the air that lands per step, 0 when not advancing.
  float sprayLanding;
  // Fraction of the clearing in the sludge kept per step, 1 when not advancing, 0 while sludge is off.
  float clearKeep;
};

// Arguments of one pass of the wave generator, see water_sim_waves.comp.slang.
struct WaterSimWaveArgs {
  uint pass;
  // Size of the mip that the mip pass writes.
  uint mipSize;
  // World size of the cascade's tile.
  float tileSize;
  // Seconds, wrapped to the loop period.
  float time;

  // For the wave speeds, in world units per second squared.
  float gravity;
  // Is 2 pi / loop period: the wave frequencies are multiples of it, so the waves loop without a seam.
  float omega0;
  uint pad0;
  uint pad1;
};

// Passes of the wave generator.
#define WATER_SIM_WAVES_PASS_SPECTRUM 0
#define WATER_SIM_WAVES_PASS_ROWS     1
#define WATER_SIM_WAVES_PASS_COLUMNS  2
#define WATER_SIM_WAVES_PASS_MIP      3
// Texels along each side of a cascade, a power of 2.
#define WATER_SIM_WAVES_SIZE      256
#define WATER_SIM_WAVES_LOG2_SIZE 8
#define WATER_SIM_WAVES_MIPS      6

// What the ray tracing shaders need to read the simulated surface, see water_sim_surface.slangh.
struct WaterSimArgs {
  uint enable;
  float cellSize;
  float gridSize;
  float maxHeight;

  vec2 centerCell;
  float normalStrength;
  uint pomSteps;

  uint pomMaxSteps;
  // Height above and below the rest level that the parallax walk searches: the highest wave there can be now.
  float walkBound;
  // Index into the surface materials of the water that sludge opens into, SURFACE_INDEX_INVALID while sludge is off.
  uint sludgeWaterMaterialIndex;
  float sludgeEdgeNoise;

  // Holds WATER_SIM_DETAIL_FLAG_* bits.
  uint detailFlags;
  // The detail replaces the material's normal maps: wind waves in two cascades of the wave generator (texel: height, slope x,
  // slope y). Texture and sampler indices are into the bindless tables.
  uint detailSamplerIndex;
  uint wave0TextureIndex;
  uint wave1TextureIndex;

  // Rotation of each cascade's tile as (cos, sin), so that the two tiles never line up.
  vec2 wave0Rotation;
  vec2 wave1Rotation;

  float wave0InvTileSize;
  float wave1InvTileSize;
  float gustInvSize;
  float gustTime;

  float gustMin;
  // The droplet rings, a texture of drops in the bindless table.
  uint ringTextureIndex;
  float ringInvTileSize;
  float ringSlope;

  // Phase of the rings in [0, 1).
  float ringPhase;
  // Rings everywhere, 0 to 1, for rain.
  float ringRain;
  // Holds WATER_SIM_SILHOUETTE_FLAG_* bits.
  uint silhouetteFlags;
  // How far below the mesh the simulated surface rests, 0 without silhouettes.
  float restDepth;

  // rayOffset: view-ray distance from the water mesh hit to the simulated water surface.
  // shoreHit: view-ray distance from the water mesh hit to the shore hit behind it.
  // The silhouette ray is traced only when rayOffset > silhouetteMinOffset.
  float silhouetteMinOffset;
  // Extends the silhouette ray by silhouettePomMargin below the simulated surface (vertically), to reach POM meshes under the water whose bumps rise above it.
  float silhouettePomMargin;
  // The refraction starts at least shoreRefractionGap before the shore hit.
  float shoreRefractionGap;
  // If the shore hit is under water, the ray origin moves shoreStepBack (max half the camera distance) before the water mesh hit, to hit the water again.
  float shoreStepBack;
};

// Bits of WaterSimArgs::detailFlags.
// The wind waves replace the material's normal maps.
#define WATER_SIM_DETAIL_FLAG_ACTIVE (1 << 0)
// The ring texture is loaded.
#define WATER_SIM_DETAIL_FLAG_RINGS  (1 << 1)

// Bits of WaterSimArgs::silhouetteFlags, see the water silhouettes in resolve.slangh.
// The parallax hit of a wave trough is hidden by what stands between it and the flat water.
#define WATER_SIM_SILHOUETTE_FLAG_WAVES                   (1 << 0)
// The parallax hit of an opaque POM surface seen through translucency is hidden by what stands between it and the low poly hit.
#define WATER_SIM_SILHOUETTE_FLAG_POM_BEHIND_TRANSLUCENCY (1 << 1)

#ifdef __cplusplus
// Packed into the ray tracing constant buffer (see raytrace_args.h), so it must stay aligned.
static_assert((sizeof(WaterSimArgs) & 15) == 0);
#endif
