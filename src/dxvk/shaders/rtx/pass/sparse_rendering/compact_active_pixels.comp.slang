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
#include "rtx/pass/common_bindings.slangh"
#include "rtx/pass/sparse_rendering/compact_active_pixels_binding_indices.h"
#include "rtx/pass/sparse_rendering/compact_active_pixels_internal.slangh"
#include "rtx/pass/sparse_rendering/sparse_rendering.slangh"

// Inputs

layout(binding = COMPACT_ACTIVE_PIXELS_BINDING_DIRECT_ACTIVE_PIXEL_MASK_INPUT)
Texture2D<uint> DirectActivePixelMask;

layout(binding = COMPACT_ACTIVE_PIXELS_BINDING_INDIRECT_ACTIVE_PIXEL_MASK_INPUT)
Texture2D<uint> IndirectActivePixelMask;

// Outputs

layout(binding = COMPACT_ACTIVE_PIXELS_BINDING_DIRECT_ACTIVE_LOCAL_PIXEL_COORDS_OUTPUT)
RWTexture2D<uint16_t> DirectActiveLocalPixelCoords;

layout(binding = COMPACT_ACTIVE_PIXELS_BINDING_INDIRECT_ACTIVE_LOCAL_PIXEL_COORDS_OUTPUT)
RWTexture2D<uint16_t> IndirectActiveLocalPixelCoords;

layout(binding = COMPACT_ACTIVE_PIXELS_BINDING_UNION_ACTIVE_LOCAL_PIXEL_COORDS_OUTPUT)
RWTexture2D<uint16_t> UnionActiveLocalPixelCoords;

// Per-signal output texture selector for the scatter helpers below. Direct + Union are
// produced together by scatterForDirectAndUnionOutputs, so this enum only addresses the
// remaining two-region scatter (Indirect).
static const uint kCompactOutputDirect   = 0u;
static const uint kCompactOutputIndirect = 1u;

// Stored-bitmap slot indices. Three slots are stored per uintIdx in the interleaved
// s_bitmaps layout below; union is derived as `direct | indirect`.
static const uint kBitmapDirect   = 0u;
static const uint kBitmapIndirect = 1u;
static const uint kBitmapBounds   = 2u;
static const uint kNumStoredBitmaps = 3u;

// Wave-accelerated inclusive prefix sum over the workgroup.
// Uses WavePrefixSum within each wave, then a single-barrier cross-wave fixup via groupshared.
// Max waves per group: 1024 / 32 = 32.
groupshared uint s_waveTotals[COMPACT_ACTIVE_PIXELS_GROUP_SIZE / 32]; // assumes min wave size 32

// Interleaved groupshared bitmaps: address(uintIdx, bitmapSlot) = uintIdx * kNumStoredBitmaps + bitmapSlot.
// Phase 1 distributes the three stores across lanes 0..2 (each lane writes one slot)
// so they hit three distinct banks for the same uintIdx instead of serializing through lane 0.
// Phase 2 (scatter) reads one slot at a time across the wave; same-address replicates
// broadcast and distinct uintIdx values at stride kNumStoredBitmaps fall in distinct banks.
groupshared uint s_bitmaps[(COMPACT_ACTIVE_PIXELS_TILE_SIZE / 32) * kNumStoredBitmaps];

uint bitmapLoad(uint uintIdx, uint bitmapSlot)
{
  return s_bitmaps[uintIdx * kNumStoredBitmaps + bitmapSlot];
}

void bitmapStore(uint uintIdx, uint bitmapSlot, uint value)
{
  s_bitmaps[uintIdx * kNumStoredBitmaps + bitmapSlot] = value;
}

// Bitmap readback constraints:
// BLOCK_SIZE_X must fit in a single uint and align to uint boundaries.
#if COMPACT_ACTIVE_PIXELS_BLOCK_SIZE_X > 32
  #error "BLOCK_SIZE_X must be <= 32 to fit in a single bitmap word bitmask."
#endif
#if (32 % COMPACT_ACTIVE_PIXELS_BLOCK_SIZE_X) != 0
  #error "BLOCK_SIZE_X must evenly divide 32 so block bits don't span uint boundaries."
#endif

// Perform an inclusive prefix sum of `value` across the workgroup using wave intrinsics.
// After return, s_waveTotals[numWaves-1] holds the group total (read it before the next barrier).
uint inclusivePrefixSum(uint value, uint groupIndex)
{
  // Step 1: Intra-wave inclusive prefix sum (no barrier needed).
  uint wavePrefix = WavePrefixSum(value) + value;

  // Step 2: Last lane of each wave writes its wave total.
  uint laneCount = WaveGetLaneCount();
  uint waveIndex = groupIndex / laneCount;
  if (WaveGetLaneIndex() == laneCount - 1)
  {
    s_waveTotals[waveIndex] = wavePrefix;
  }

  GroupMemoryBarrierWithGroupSync();

  // Step 3: First wave computes inclusive prefix over wave totals.
  uint numWaves = COMPACT_ACTIVE_PIXELS_GROUP_SIZE / laneCount;
  if (groupIndex < numWaves)
  {
    uint t = s_waveTotals[groupIndex];
    t = WavePrefixSum(t) + t;
    s_waveTotals[groupIndex] = t;
  }

  GroupMemoryBarrierWithGroupSync();

  // Step 4: Add preceding waves' total to get final result.
  uint result = wavePrefix;
  if (waveIndex > 0)
  {
    result += s_waveTotals[waveIndex - 1];
  }

  return result;
}

// Run the prefix-sum + scatter pipeline for one output's predicate.
void scatterForOutput<let outputIdx : uint>(
  uint3 groupThreadId,
  uint3 groupId,
  uint groupIndex,
  uint2 tileBasePixel,
  uint tileWidth,
  uint tileHeight,
  bool useMorton)
{
  static const uint kBlockMask = (1u << COMPACT_ACTIVE_PIXELS_BLOCK_SIZE_X) - 1u;
  static const uint kUintsPerRow = COMPACT_ACTIVE_PIXELS_TILE_SIZE_X / 32;

  bool pixelIsActive[COMPACT_ACTIVE_PIXELS_PIXELS_PER_THREAD];
  bool pixelInBounds[COMPACT_ACTIVE_PIXELS_PIXELS_PER_THREAD];
  uint myActiveCount = 0;
  uint myInactiveCount = 0;

  uint laneIdx = WaveGetLaneIndex();

  for (uint iter = 0; iter < COMPACT_ACTIVE_PIXELS_BLOCK_SIZE_Y; iter++)
  {
    // Stagger row: adjacent lanes start at different rows → different banks.
    uint dy = (laneIdx + iter) % COMPACT_ACTIVE_PIXELS_BLOCK_SIZE_Y;
    uint row = groupThreadId.y * COMPACT_ACTIVE_PIXELS_BLOCK_SIZE_Y + dy;
    uint colStart = groupThreadId.x * COMPACT_ACTIVE_PIXELS_BLOCK_SIZE_X;
    uint uintIdx = row * kUintsPerRow + colStart / 32;
    uint bitOff = colStart % 32;

    uint activeWord;
    if (outputIdx == kCompactOutputDirect)
    {
      activeWord = bitmapLoad(uintIdx, kBitmapDirect);
    }
    else
    {
      activeWord = bitmapLoad(uintIdx, kBitmapIndirect);
    }
    uint activeBits = (activeWord >> bitOff) & kBlockMask;
    uint boundsBits = (bitmapLoad(uintIdx, kBitmapBounds) >> bitOff) & kBlockMask;

    myActiveCount += countbits(activeBits);
    myInactiveCount += countbits(boundsBits & ~activeBits);

    // Store results indexed by dy (not iter) so natural-order scatter works.
    uint basePixelIdx = dy * COMPACT_ACTIVE_PIXELS_BLOCK_SIZE_X;
    for (uint dx = 0; dx < COMPACT_ACTIVE_PIXELS_BLOCK_SIZE_X; dx++)
    {
      pixelIsActive[basePixelIdx + dx] = (activeBits >> dx) & 1u;
      pixelInBounds[basePixelIdx + dx] = (boundsBits >> dx) & 1u;
    }
  }

  // Wave-accelerated inclusive prefix sums.
  uint inclusiveActivePrefix = inclusivePrefixSum(myActiveCount, groupIndex);
  uint numWaves = COMPACT_ACTIVE_PIXELS_GROUP_SIZE / WaveGetLaneCount();
  uint groupActiveCount = s_waveTotals[numWaves - 1];

  GroupMemoryBarrierWithGroupSync(); // protect s_waveTotals before reuse

  uint inclusiveInactivePrefix = inclusivePrefixSum(myInactiveCount, groupIndex);

  // Scatter in natural block order (spatially coherent for downstream passes).
  uint activeSlot = inclusiveActivePrefix - myActiveCount;
  uint inactiveSlot = groupActiveCount + inclusiveInactivePrefix - myInactiveCount;

  uint pixelIdx = 0;
  for (uint dy2 = 0; dy2 < COMPACT_ACTIVE_PIXELS_BLOCK_SIZE_Y; dy2++)
  {
    for (uint dx2 = 0; dx2 < COMPACT_ACTIVE_PIXELS_BLOCK_SIZE_X; dx2++)
    {
      if (!pixelInBounds[pixelIdx])
      {
        pixelIdx++;
        continue;
      }

      const uint2 localPixel = uint2(
        groupThreadId.x * COMPACT_ACTIVE_PIXELS_BLOCK_SIZE_X + dx2,
        groupThreadId.y * COMPACT_ACTIVE_PIXELS_BLOCK_SIZE_Y + dy2);

      uint compactedLocalIndex;
      if (pixelIsActive[pixelIdx])
      {
        compactedLocalIndex = activeSlot++;
      }
      else
      {
        compactedLocalIndex = inactiveSlot++;
      }

      uint2 compactedLocal;
      if (useMorton)
      {
        compactedLocal = mortonIndexToTileCoord(compactedLocalIndex);
      }
      else
      {
        compactedLocal = uint2(compactedLocalIndex % tileWidth, compactedLocalIndex / tileWidth);
      }
      const uint2 compactedPixelCoordinate = tileBasePixel + compactedLocal;

      const uint16_t packedCoordinate = packLocalPixelCoordinate(localPixel, pixelIsActive[pixelIdx]);
      if (outputIdx == kCompactOutputDirect)
      {
        DirectActiveLocalPixelCoords[compactedPixelCoordinate] = packedCoordinate;
      }
      else
      {
        IndirectActiveLocalPixelCoords[compactedPixelCoordinate] = packedCoordinate;
      }
      pixelIdx++;
    }
  }
}

// Three-region scatter that produces BOTH the Direct and Union outputs in one pass.
// Splits each tile into:
//   [ direct-active | (indirect-active AND NOT direct-active) | inactive ]
// so the integrate-direct dispatch sees wave-aligned class boundaries (full work,
// indirect-prep only, early-out). The Union output reuses the same slot allocation
// (regions A+B form the union-active span) — only the packed `isActive` flag differs:
// Direct entries flag direct-active; Union entries flag (direct OR indirect-only).
void scatterForDirectAndUnionOutputs(
  uint3 groupThreadId,
  uint3 groupId,
  uint groupIndex,
  uint2 tileBasePixel,
  uint tileWidth,
  uint tileHeight,
  bool useMorton)
{
  static const uint kBlockMask = (1u << COMPACT_ACTIVE_PIXELS_BLOCK_SIZE_X) - 1u;
  static const uint kUintsPerRow = COMPACT_ACTIVE_PIXELS_TILE_SIZE_X / 32;

  bool pixelIsDirect[COMPACT_ACTIVE_PIXELS_PIXELS_PER_THREAD];
  bool pixelIsIndirectOnly[COMPACT_ACTIVE_PIXELS_PIXELS_PER_THREAD];
  bool pixelInBounds[COMPACT_ACTIVE_PIXELS_PIXELS_PER_THREAD];
  uint myDirectCount = 0;
  uint myIndirectOnlyCount = 0;
  uint myInactiveCount = 0;

  uint laneIdx = WaveGetLaneIndex();

  for (uint iter = 0; iter < COMPACT_ACTIVE_PIXELS_BLOCK_SIZE_Y; iter++)
  {
    // Stagger row: adjacent lanes start at different rows → different banks.
    uint dy = (laneIdx + iter) % COMPACT_ACTIVE_PIXELS_BLOCK_SIZE_Y;
    uint row = groupThreadId.y * COMPACT_ACTIVE_PIXELS_BLOCK_SIZE_Y + dy;
    uint colStart = groupThreadId.x * COMPACT_ACTIVE_PIXELS_BLOCK_SIZE_X;
    uint uintIdx = row * kUintsPerRow + colStart / 32;
    uint bitOff = colStart % 32;

    const uint directWord   = bitmapLoad(uintIdx, kBitmapDirect);
    const uint indirectWord = bitmapLoad(uintIdx, kBitmapIndirect);
    uint directBits = (directWord >> bitOff) & kBlockMask;
    uint unionBits  = ((directWord | indirectWord) >> bitOff) & kBlockMask;
    uint boundsBits = (bitmapLoad(uintIdx, kBitmapBounds) >> bitOff) & kBlockMask;

    uint directInBoundsBits     = boundsBits & directBits;
    uint indirectOnlyBits       = boundsBits & unionBits & ~directBits;
    uint inactiveInBoundsBits   = boundsBits & ~unionBits;

    myDirectCount        += countbits(directInBoundsBits);
    myIndirectOnlyCount  += countbits(indirectOnlyBits);
    myInactiveCount      += countbits(inactiveInBoundsBits);

    uint basePixelIdx = dy * COMPACT_ACTIVE_PIXELS_BLOCK_SIZE_X;
    for (uint dx = 0; dx < COMPACT_ACTIVE_PIXELS_BLOCK_SIZE_X; dx++)
    {
      pixelIsDirect[basePixelIdx + dx]       = (directInBoundsBits >> dx) & 1u;
      pixelIsIndirectOnly[basePixelIdx + dx] = (indirectOnlyBits   >> dx) & 1u;
      pixelInBounds[basePixelIdx + dx]       = (boundsBits         >> dx) & 1u;
    }
  }

  uint numWaves = COMPACT_ACTIVE_PIXELS_GROUP_SIZE / WaveGetLaneCount();

  // Region A: direct-active.
  uint inclusiveDirectPrefix = inclusivePrefixSum(myDirectCount, groupIndex);
  uint groupDirectCount = s_waveTotals[numWaves - 1];

  GroupMemoryBarrierWithGroupSync(); // protect s_waveTotals before reuse

  // Region B: indirect-active but not direct-active.
  uint inclusiveIndirectOnlyPrefix = inclusivePrefixSum(myIndirectOnlyCount, groupIndex);
  uint groupIndirectOnlyCount = s_waveTotals[numWaves - 1];

  GroupMemoryBarrierWithGroupSync(); // protect s_waveTotals before reuse

  // Region C: inactive (still in bounds).
  uint inclusiveInactivePrefix = inclusivePrefixSum(myInactiveCount, groupIndex);

  uint directSlot       = inclusiveDirectPrefix - myDirectCount;
  uint indirectOnlySlot = groupDirectCount + inclusiveIndirectOnlyPrefix - myIndirectOnlyCount;
  uint inactiveSlot     = groupDirectCount + groupIndirectOnlyCount + inclusiveInactivePrefix - myInactiveCount;

  uint pixelIdx = 0;
  for (uint dy2 = 0; dy2 < COMPACT_ACTIVE_PIXELS_BLOCK_SIZE_Y; dy2++)
  {
    for (uint dx2 = 0; dx2 < COMPACT_ACTIVE_PIXELS_BLOCK_SIZE_X; dx2++)
    {
      if (!pixelInBounds[pixelIdx])
      {
        pixelIdx++;
        continue;
      }

      const uint2 localPixel = uint2(
        groupThreadId.x * COMPACT_ACTIVE_PIXELS_BLOCK_SIZE_X + dx2,
        groupThreadId.y * COMPACT_ACTIVE_PIXELS_BLOCK_SIZE_Y + dy2);

      const bool isDirect       = pixelIsDirect[pixelIdx];
      const bool isIndirectOnly = pixelIsIndirectOnly[pixelIdx];
      const bool isUnionActive  = isDirect || isIndirectOnly;

      uint compactedLocalIndex;
      if (isDirect)
      {
        compactedLocalIndex = directSlot++;
      }
      else if (isIndirectOnly)
      {
        compactedLocalIndex = indirectOnlySlot++;
      }
      else
      {
        compactedLocalIndex = inactiveSlot++;
      }

      uint2 compactedLocal;
      if (useMorton)
      {
        compactedLocal = mortonIndexToTileCoord(compactedLocalIndex);
      }
      else
      {
        compactedLocal = uint2(compactedLocalIndex % tileWidth, compactedLocalIndex / tileWidth);
      }
      const uint2 compactedPixelCoordinate = tileBasePixel + compactedLocal;

      // Direct's isActive flag retains its existing meaning ("direct-active"); the direct
      // integrate pass re-checks the indirect mask after resolving the gbuffer coord.
      // Union shares the same compacted slot (regions A+B form union-active) but flags
      // any union-active pixel — order within active is irrelevant to union consumers.
      DirectActiveLocalPixelCoords[compactedPixelCoordinate] = packLocalPixelCoordinate(localPixel, isDirect);
      UnionActiveLocalPixelCoords[compactedPixelCoordinate]  = packLocalPixelCoordinate(localPixel, isUnionActive);
      pixelIdx++;
    }
  }
}

void computeActiveLocalPixelCoordinates(
  uint3 threadIndex,
  uint3 groupThreadId,
  uint3 groupId,
  uint  groupIndex)
{
  const uint2 tileBasePixel = uint2(
    groupId.x * COMPACT_ACTIVE_PIXELS_TILE_SIZE_X,
    groupId.y * COMPACT_ACTIVE_PIXELS_TILE_SIZE_Y);

  // Phase 1: Coalesced strided reads → groupshared bitmaps via WaveActiveBallot.
  // Each pixel evaluates direct/indirect predicates (union is reconstructed at read time).
  for (uint j = 0; j < COMPACT_ACTIVE_PIXELS_PIXELS_PER_THREAD; j++)
  {
    uint linearPixel = groupIndex + j * COMPACT_ACTIVE_PIXELS_GROUP_SIZE;
    uint2 localPixel = uint2(linearPixel % COMPACT_ACTIVE_PIXELS_TILE_SIZE_X,
                             linearPixel / COMPACT_ACTIVE_PIXELS_TILE_SIZE_X);
    uint2 pixelCoordinate = tileBasePixel + localPixel;
    const bool inBounds = all(pixelCoordinate < cb.camera.resolution);

    bool directActive = false;
    bool indirectActive = false;
    if (inBounds)
    {
      directActive = isActivePixelFromMask(DirectActivePixelMask, pixelCoordinate);
      indirectActive = isActivePixelFromMask(IndirectActivePixelMask, pixelCoordinate);
    }

    // WaveActiveBallot must be called by ALL lanes — hoisted out of the conditional.
    // Results are wave-uniform, so any lane can store its corresponding word.
    uint4 directBallot = WaveActiveBallot(directActive);
    uint4 indirectBallot = WaveActiveBallot(indirectActive);
    uint4 boundsBallot = WaveActiveBallot(inBounds);

    // All lanes in the wave map to the same bitmap word (linearPixel/32 is constant across the wave).
    // Distribute the three stores across lanes 0/1/2 — the interleaved s_bitmaps layout puts
    // them in adjacent banks so they issue in parallel without conflict.
    const uint bitmapIdx = linearPixel / 32;
    const uint laneIdx = WaveGetLaneIndex();
    if (laneIdx < kNumStoredBitmaps)
    {
      uint value;
      if (laneIdx == kBitmapDirect)
      {
        value = directBallot.x;
      }
      else if (laneIdx == kBitmapIndirect)
      {
        value = indirectBallot.x;
      }
      else
      {
        value = boundsBallot.x;
      }
      bitmapStore(bitmapIdx, laneIdx, value);
    }
  }

  GroupMemoryBarrierWithGroupSync();

  const uint tileWidth = min(uint(COMPACT_ACTIVE_PIXELS_TILE_SIZE_X), cb.camera.resolution.x - tileBasePixel.x);
  const uint tileHeight = min(uint(COMPACT_ACTIVE_PIXELS_TILE_SIZE_Y), cb.camera.resolution.y - tileBasePixel.y);

  // Morton ordering improves 2D spatial locality for downstream texture accesses.
  // Only used for full-size interior tiles; edge tiles fall back to row-major to avoid out-of-bounds writes.
  const bool useMorton = tileWidth == COMPACT_ACTIVE_PIXELS_TILE_SIZE_X
                      && tileHeight == COMPACT_ACTIVE_PIXELS_TILE_SIZE_Y;

  // Direct + Union are produced together in a three-region scatter — [direct-active |
  // indirect-only-active | inactive] — so the integrate-direct pass gets wave-coherent
  // regions, and the Union output reuses the same slot allocation (regions A+B = union-active).
  // Indirect keeps its own [active | inactive] scatter because its ordering is independent.
  scatterForDirectAndUnionOutputs(groupThreadId, groupId, groupIndex,
                                  tileBasePixel, tileWidth, tileHeight, useMorton);
  GroupMemoryBarrierWithGroupSync();

  scatterForOutput<kCompactOutputIndirect>(groupThreadId, groupId, groupIndex,
                   tileBasePixel, tileWidth, tileHeight, useMorton);
  GroupMemoryBarrierWithGroupSync();
}

[shader("compute")]
[numthreads(COMPACT_ACTIVE_PIXELS_THREADGROUP_SIZE_X, COMPACT_ACTIVE_PIXELS_THREADGROUP_SIZE_Y, 1)]
void main(
  uint3 threadIndex : SV_DispatchThreadID,
  uint3 groupThreadId    : SV_GroupThreadID,
  uint3 groupId          : SV_GroupID,
  uint  groupIndex       : SV_GroupIndex)
{
  computeActiveLocalPixelCoordinates(threadIndex, groupThreadId, groupId, groupIndex);
}
