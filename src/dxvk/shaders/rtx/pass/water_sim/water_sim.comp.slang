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
#include "rtx/pass/water_sim/water_sim_args.h"
#include "rtx/pass/water_sim/water_sim_binding_indices.h"
#include "rtx/pass/water_sim/water_sim_detail.slangh"

// One step of the water simulation. See documentation/WaterSimulation.md.

// Texel: height and velocity in world units, the spray that has landed (0 to 1), and the spray still in the air.
layout(binding = WATER_SIM_BINDING_STATE_INPUT)
Texture2D<float4> StateInput;

layout(binding = WATER_SIM_BINDING_STATE_OUTPUT)
RWTexture2D<float4> StateOutput;

// Texel: height with the long wind waves, height of the simulated waves only, landed spray, sludge clearing.
layout(binding = WATER_SIM_BINDING_DISPLAY_OUTPUT)
RWTexture2D<float4> DisplayOutput;

// Texel: how much the sludge is cleared, 0 to 1.
layout(binding = WATER_SIM_BINDING_LAYER_INPUT)
Texture2D<float> LayerInput;

layout(binding = WATER_SIM_BINDING_LAYER_OUTPUT)
RWTexture2D<float> LayerOutput;

// Two elements per disturbance. The first holds the plane position and radius in cells and the push.
// The second holds the spray it throws and its spread in cells, then the sludge clearing and its spread in cells.
layout(binding = WATER_SIM_BINDING_DISTURBANCES)
StructuredBuffer<float4> Disturbances;

// The long wind waves: height and slopes in world units.
layout(binding = WATER_SIM_BINDING_WAVES)
Sampler2D<float4> Waves;

layout(push_constant)
ConstantBuffer<WaterSimStepArgs> cb;

int wrap(int value, int size)
{
  return ((value % size) + size) % size;
}

// Cells outside the current window have no data, and cells outside the previous window still hold the cells that the window left.
bool isInsideWindow(int2 cell, int2 origin, int size)
{
  const int2 offset = cell - origin;
  return all(offset >= 0) && all(offset < size);
}

// Returns the height and landed spray of a neighbour cell.
float2 loadNeighbour(int2 texel, int2 cell, int2 origin, int2 oldOrigin, int size)
{
  if (!isInsideWindow(cell, origin, size) || !isInsideWindow(cell, oldOrigin, size))
  {
    return float2(0.0);
  }
  return StateInput.Load(int3(wrap(texel.x, size), wrap(texel.y, size), 0)).xz;
}

float loadNeighbourCleared(int2 texel, int2 cell, int2 origin, int2 oldOrigin, int size)
{
  if (!isInsideWindow(cell, origin, size) || !isInsideWindow(cell, oldOrigin, size))
  {
    return 0.0;
  }
  return LayerInput.Load(int3(wrap(texel.x, size), wrap(texel.y, size), 0));
}

[shader("compute")]
[numthreads(16, 16, 1)]
void main(uint2 ipos : SV_DispatchThreadID)
{
  const int size = int(cb.gridSize);
  if (any(ipos >= uint2(size, size)))
  {
    return;
  }

  const int2 texel = int2(ipos);
  const int2 origin = int2(cb.originX, cb.originY);
  const int2 oldOrigin = int2(cb.oldOriginX, cb.oldOriginY);

  // The world cell that this texel holds in the current window. Cells that were outside the previous window start flat.
  const int2 cell = origin + int2(wrap(texel.x - origin.x, size), wrap(texel.y - origin.y, size));
  const bool isNewCell = !isInsideWindow(cell, oldOrigin, size);

  const float4 state = isNewCell ? float4(0.0) : StateInput.Load(int3(texel, 0));
  const float2 left = loadNeighbour(texel + int2(-1, 0), cell + int2(-1, 0), origin, oldOrigin, size);
  const float2 right = loadNeighbour(texel + int2(1, 0), cell + int2(1, 0), origin, oldOrigin, size);
  const float2 down = loadNeighbour(texel + int2(0, -1), cell + int2(0, -1), origin, oldOrigin, size);
  const float2 up = loadNeighbour(texel + int2(0, 1), cell + int2(0, 1), origin, oldOrigin, size);
  const bool sludge = cb.clearKeep > 0.0;
  const float clearedState = sludge && !isNewCell ? LayerInput.Load(int3(texel, 0)) : 0.0;

  if (cb.writeDisplay)
  {
    // The long wind waves are only added to the displayed height, so that the parallax walk sees them.
    // Their slopes come from the material (waterSimEvalDetail).
    float waveHeight = 0.0;
    if (cb.waveHeightScale > 0.0)
    {
      const float2 planePosition = (float2(cell) + 0.5) * cb.cellSize;
      const float2 uv = waterSimDetailUv(planePosition, cb.wave0InvTileSize, cb.wave0Rotation);
      waveHeight = cb.waveHeightScale * waterSimGust(planePosition, cb.gustInvSize, cb.gustTime, cb.gustMin) * Waves.SampleLevel(uv, 0.0).x;
    }
    DisplayOutput[texel] = float4(clamp(state.x + waveHeight, -cb.maxHeight, cb.maxHeight), clamp(state.x, -cb.maxHeight, cb.maxHeight),
                                  state.z, clearedState);
  }

  const float laplacian = left.x + right.x + down.x + up.x - 4.0 * state.x;
  float velocity = (state.y + cb.waveCoefficient * laplacian) * cb.velocityKeep;
  // The wave equation keeps the mean height, so the surface is pulled back to rest separately.
  float height = (state.x + cb.advance * velocity) * cb.heightKeep;

  // Waves are absorbed near the window border, so they do not wrap around the torus.
  const int2 cellFromEdge = min(cell - origin, origin + size - 1 - cell);
  const float absorb = cb.advance > 0.0 ? lerp(0.85, 1.0, smoothstep(0.0, float(size) / 16.0, float(min(cellFromEdge.x, cellFromEdge.y)))) : 1.0;
  velocity *= absorb;
  height *= absorb;

  float spray = 0.0;
  float clearing = 0.0;
  for (uint i = 0; i < cb.disturbanceCount; ++i)
  {
    const float4 disturbance = Disturbances[2 * i];
    const float4 disturbanceSpray = Disturbances[2 * i + 1];
    const float2 offset = float2(cell) + 0.5 - disturbance.xy;
    const float radius = max(disturbance.z, 1.5);

    // A dent with a rim of the same volume around it, a difference of Gaussians, keeps the mean height of the water.
    const float distanceSquared = dot(offset, offset) / (radius * radius);
    if (distanceSquared < 9.0)
    {
      velocity -= disturbance.w * (exp(-2.0 * distanceSquared) - 0.25 * exp(-0.5 * distanceSquared));
    }

    // The spray of a steep dent lands in a Gaussian around it, wider than the dent.
    const float spreadSquared = dot(offset, offset) / (disturbanceSpray.y * disturbanceSpray.y);
    if (disturbanceSpray.x > 0.0 && spreadSquared < 9.0)
    {
      spray += disturbanceSpray.x * exp(-0.5 * spreadSquared);
    }

    // Any disturbance pushes the sludge aside, in a Gaussian about as wide as the dent.
    const float clearSquared = dot(offset, offset) / (disturbanceSpray.w * disturbanceSpray.w);
    if (disturbanceSpray.z > 0.0 && clearSquared < 9.0)
    {
      clearing = max(clearing, disturbanceSpray.z * exp(-0.5 * clearSquared));
    }
  }

  // A cell held at the clamp stops there, otherwise its velocity would keep it pinned.
  const float clampedHeight = clamp(height, -cb.maxHeight, cb.maxHeight);
  if (clampedHeight != height)
  {
    velocity = 0.0;
  }

  // Spray in the air lands over the fall time, spreads a little to the neighbours, and fades.
  float landed = state.z;
  float inAir = state.w + spray;
  if (cb.advance > 0.0)
  {
    landed = lerp(landed, 0.25 * (left.y + right.y + down.y + up.y), 0.02) * cb.sprayKeep;
    const float landing = inAir * cb.sprayLanding;
    landed += landing;
    inAir -= landing;
  }

  StateOutput[texel] = float4(clampedHeight, velocity, saturate(landed), inAir);

  // The sludge stays open as far as the strongest recent disturbance cleared it, and closes from the edges of the holes.
  float cleared = 0.0;
  if (sludge)
  {
    cleared = clearedState;
    if (cb.advance > 0.0)
    {
      const float neighbours = 0.25 * (loadNeighbourCleared(texel + int2(-1, 0), cell + int2(-1, 0), origin, oldOrigin, size) +
                                       loadNeighbourCleared(texel + int2(1, 0), cell + int2(1, 0), origin, oldOrigin, size) +
                                       loadNeighbourCleared(texel + int2(0, -1), cell + int2(0, -1), origin, oldOrigin, size) +
                                       loadNeighbourCleared(texel + int2(0, 1), cell + int2(0, 1), origin, oldOrigin, size));
      cleared = lerp(cleared, neighbours, 0.02) * cb.clearKeep;
    }
    cleared = max(cleared, clearing);
  }
  LayerOutput[texel] = cleared;
}
