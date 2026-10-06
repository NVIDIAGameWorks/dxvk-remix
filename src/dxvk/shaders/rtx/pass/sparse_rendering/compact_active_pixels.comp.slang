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

layout(binding = COMPACT_ACTIVE_PIXELS_BINDING_ACTIVE_PIXEL_MASK_INPUT)
Texture2D<uint> ActivePixelMask;

// Inputs/Outputs

layout(binding = COMPACT_ACTIVE_PIXELS_BINDING_ACTIVE_PIXEL_COUNT_INPUT_OUTPUT)
RWStructuredBuffer<uint> ActivePixelCount;

// Outputs

layout(binding = COMPACT_ACTIVE_PIXELS_BINDING_ACTIVE_LOCAL_PIXEL_COORDS_OUTPUT)
RWTexture2D<uint16_t> ActiveLocalPixelCoords;

// Inverts the table above by mapping each pixel coordinate to the compacted index that owns the pixel,
// counted from the tile's base. Holds kInvalidCompactedIndex for a pixel with no slot this frame.
layout(binding = COMPACT_ACTIVE_PIXELS_BINDING_COMPACTED_PIXEL_INDICES_OUTPUT)
RWTexture2D<uint16_t> CompactedPixelIndices;

layout(binding = COMPACT_ACTIVE_PIXELS_BINDING_TILE_ACTIVE_COUNTS_OUTPUT)
RWTexture2D<uint2> TileActiveCounts;

layout(binding = COMPACT_ACTIVE_PIXELS_BINDING_ACTIVE_PIXEL_COORDS_OUTPUT)
RWStructuredBuffer<uint> ActivePixelCoords;

layout(binding = COMPACT_ACTIVE_PIXELS_BINDING_TRACE_RAYS_ARGS_OUTPUT)
RWStructuredBuffer<uint> TraceRaysIndirectArgs;

layout(binding = COMPACT_ACTIVE_PIXELS_BINDING_DISPATCH_ARGS_OUTPUT)
RWStructuredBuffer<uint> DispatchIndirectArgs;

// The two APIs count different things: VkTraceRaysIndirectCommandKHR takes invocations
// while VkDispatchIndirectCommand takes workgroups, so they cannot share one buffer.
void writeIndirectArgs(uint activePixelCount)
{
  TraceRaysIndirectArgs[0] = activePixelCount;
  TraceRaysIndirectArgs[1] = 1;
  TraceRaysIndirectArgs[2] = 1;

  const uint groupCount =
    (activePixelCount + COMPACT_ACTIVE_PIXELS_CONSUMER_GROUP_SIZE - 1) / COMPACT_ACTIVE_PIXELS_CONSUMER_GROUP_SIZE;

  DispatchIndirectArgs[0] = groupCount;
  DispatchIndirectArgs[1] = 1;
  DispatchIndirectArgs[2] = 1;
}

// Wave-accelerated inclusive prefix sum over the workgroup.
// Uses WavePrefixSum within each wave, then a single-barrier cross-wave fixup via groupshared.
// Max waves per group: 1024 / 32 = 32.
groupshared uint s_waveTotals[COMPACT_ACTIVE_PIXELS_GROUP_SIZE / 32]; // assumes min wave size 32

// Holds one bit per tile pixel that records whether the pixel was active.
// Phase 2 recomputes whether a pixel is in bounds from its coordinate.
groupshared uint s_isActiveBitmap[COMPACT_ACTIVE_PIXELS_TILE_SIZE / 32];

// Holds the start of this tile's run in the global list.
// Tiles claim runs in arrival order, so the list is not ordered by tile.
// A tile's entries still stay contiguous, which is what the compacted launch relies on.
groupshared uint s_globalBaseOffset;

// Bitmap readback constraints:
// BLOCK_SIZE_X must fit in a single uint and align to uint boundaries.
#if COMPACT_ACTIVE_PIXELS_BLOCK_SIZE_X > 32
  #error "BLOCK_SIZE_X must be <= 32 to fit in a single bitmap word bitmask."
#endif
#if (32 % COMPACT_ACTIVE_PIXELS_BLOCK_SIZE_X) != 0
  #error "BLOCK_SIZE_X must evenly divide 32 so block bits don't span uint boundaries."
#endif
#if COMPACT_ACTIVE_PIXELS_PIXELS_PER_THREAD > 32
  #error "PIXELS_PER_THREAD must be <= 32 to fit a thread's block in one uint of active bits."
#endif

// These arrays hold one entry per thread block, indexed by the groupIndex of the thread that owns the block.
// The mask array holds the block's active bits in block order.
// The prefix arrays hold the numbers of active and in-bounds inactive tile pixels that precede the block.
groupshared uint s_blockActiveMask[COMPACT_ACTIVE_PIXELS_GROUP_SIZE];
groupshared uint s_blockActivePrefix[COMPACT_ACTIVE_PIXELS_GROUP_SIZE];
groupshared uint s_blockInactivePrefix[COMPACT_ACTIVE_PIXELS_GROUP_SIZE];

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

// Run the prefix-sum + scatter pipeline, splitting each tile into [active | inactive].
void scatterActivePixels(
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

  // Holds this thread's active pixels as a bitmask, with pixel (dx, dy) at bit (dy * BLOCK_SIZE_X + dx).
  uint activeMask = 0;
  uint myActiveCount = 0;
  uint myInactiveCount = 0;

  uint laneIdx = WaveGetLaneIndex();
  const uint colStart = groupThreadId.x * COMPACT_ACTIVE_PIXELS_BLOCK_SIZE_X;
  // Out-of-bounds pixels never ballot active, so activeBits is already a subset of boundsBits.
  const uint validX = colStart < tileWidth
    ? min(tileWidth - colStart, uint(COMPACT_ACTIVE_PIXELS_BLOCK_SIZE_X))
    : 0u;
  const uint rowBoundsBits = (1u << validX) - 1u;

  [unroll]
  for (uint iter = 0; iter < COMPACT_ACTIVE_PIXELS_BLOCK_SIZE_Y; iter++)
  {
    // Stagger row: adjacent lanes start at different rows → different banks.
    uint dy = (laneIdx + iter) % COMPACT_ACTIVE_PIXELS_BLOCK_SIZE_Y;
    uint row = groupThreadId.y * COMPACT_ACTIVE_PIXELS_BLOCK_SIZE_Y + dy;
    uint uintIdx = row * kUintsPerRow + colStart / 32;
    uint bitOff = colStart % 32;

    uint activeBits = (s_isActiveBitmap[uintIdx] >> bitOff) & kBlockMask;
    uint boundsBits = row < tileHeight ? rowBoundsBits : 0u;

    myActiveCount += countbits(activeBits);
    myInactiveCount += countbits(boundsBits & ~activeBits);

    // Indexed by dy rather than iter, so the bits are in block order.
    activeMask |= activeBits << (dy * COMPACT_ACTIVE_PIXELS_BLOCK_SIZE_X);
  }

  s_blockActiveMask[groupIndex] = activeMask;

  // Wave-accelerated inclusive prefix sums.
  uint inclusiveActivePrefix = inclusivePrefixSum(myActiveCount, groupIndex);
  uint numWaves = COMPACT_ACTIVE_PIXELS_GROUP_SIZE / WaveGetLaneCount();
  uint groupActiveCount = s_waveTotals[numWaves - 1];

  const uint capacity = cb.sparseRenderingArgs.compactedStorageCapacity;

  // The compacted storage lays tiles out in the order they claim their runs.
  if (groupIndex == 0)
  {
    uint baseOffset = 0;
    InterlockedAdd(ActivePixelCount[COMPACT_ACTIVE_PIXELS_COUNT_SLOT_ACCUMULATOR], groupActiveCount, baseOffset);
    s_globalBaseOffset = baseOffset;

    const uint2 tileCount = (cb.camera.resolution +
      uint2(COMPACT_ACTIVE_PIXELS_TILE_SIZE_X - 1, COMPACT_ACTIVE_PIXELS_TILE_SIZE_Y - 1)) /
      uint2(COMPACT_ACTIVE_PIXELS_TILE_SIZE_X, COMPACT_ACTIVE_PIXELS_TILE_SIZE_Y);

    // This is the writer-side barrier.
    // The last tile takes the tiles-done count as proof that every accumulator add has landed,
    // but Interlocked* orders each slot on its own and not one slot against the other.
    // This barrier keeps the accumulator add ahead of the tiles-done increment. If the barrier came after both atomics,
    // another tile could see the increment first, and no reader-side barrier could undo that.
    DeviceMemoryBarrier();
    uint previousTilesDone = 0;
    InterlockedAdd(ActivePixelCount[COMPACT_ACTIVE_PIXELS_COUNT_SLOT_TILES_DONE], 1, previousTilesDone);

    // Runs once, in the last tile to finish. Other tiles may have added to the accumulator after this one did,
    // so its own base offset is not the total and the accumulator is read back.
    if (previousTilesDone + 1 == tileCount.x * tileCount.y)
    {
      // This is the reader-side barrier. It keeps the accumulator read-back below after the tiles-done read,
      // so together with the writer-side barrier, the read-back includes every tile's accumulator add.
      DeviceMemoryBarrier();

      // The read uses an atomic rather than a plain load because every other group has written this slot.
      // Adding zero is the cheapest way to get a coherent value for it.
      uint totalActiveCount = 0;
      InterlockedAdd(ActivePixelCount[COMPACT_ACTIVE_PIXELS_COUNT_SLOT_ACCUMULATOR], 0, totalActiveCount);

      // Pixels past the capacity are clamped to inactive below.
      // They are exactly those with the highest global indices, so the surviving set is the contiguous run [0, capacity).
      const uint activePixelCount = min(totalActiveCount, capacity);

      writeIndirectArgs(activePixelCount);

      // Consumers read the published slot, so the accumulators reset here without a host clear before the next frame.
      ActivePixelCount[COMPACT_ACTIVE_PIXELS_COUNT_SLOT_ACTIVE_PIXELS] = activePixelCount;
      ActivePixelCount[COMPACT_ACTIVE_PIXELS_COUNT_SLOT_ACCUMULATOR] = 0;
      ActivePixelCount[COMPACT_ACTIVE_PIXELS_COUNT_SLOT_TILES_DONE] = 0;
    }
  }

  // This barrier also orders every s_waveTotals read above before the edge-tile prefix sum below rewrites it.
  GroupMemoryBarrierWithGroupSync();

  const uint globalBaseOffset = s_globalBaseOffset;

  // Everything past the capacity is reported inactive,
  // so a grid-launched thread testing against this count never resolves a storage coordinate that was not written.
  const uint storedActiveCount =
    min(groupActiveCount, globalBaseOffset < capacity ? capacity - globalBaseOffset : 0u);

  // Consumers use this to test activity from their dispatch index alone, without reading the per-pixel table,
  // and to reach their storage coordinate from the same read.
  if (groupIndex == 0)
  {
    TileActiveCounts[groupId.xy] = uint2(storedActiveCount, globalBaseOffset);
  }

  // useMorton is set exactly for full tiles. Every thread of a full tile owns exactly PIXELS_PER_THREAD in-bounds pixels,
  // so its inclusive in-bounds prefix is analytic and the inactive prefix is that minus the active one.
  // Only partial edge tiles pay for a second reduction.
  // The branch is tile-uniform, so the barriers inside stay in uniform control flow.
  uint inclusiveInactivePrefix;
  if (useMorton)
  {
    inclusiveInactivePrefix =
      (groupIndex + 1) * COMPACT_ACTIVE_PIXELS_PIXELS_PER_THREAD - inclusiveActivePrefix;
  }
  else
  {
    inclusiveInactivePrefix = inclusivePrefixSum(myInactiveCount, groupIndex);
  }

  // Slots follow natural block order, which is spatially coherent for downstream passes.
  // Thread blocks come in group order, each block is row-major, and active pixels come first.
  s_blockActivePrefix[groupIndex] = inclusiveActivePrefix - myActiveCount;
  s_blockInactivePrefix[groupIndex] = inclusiveInactivePrefix - myInactiveCount;

  GroupMemoryBarrierWithGroupSync();

  // Lanes take consecutive pixels of a row rather than their own block, so the per-pixel stores coalesce.
  [unroll]
  for (uint j = 0; j < COMPACT_ACTIVE_PIXELS_PIXELS_PER_THREAD; j++)
  {
    const uint linearPixel = groupIndex + j * COMPACT_ACTIVE_PIXELS_GROUP_SIZE;
    const uint2 localPixel = uint2(linearPixel % COMPACT_ACTIVE_PIXELS_TILE_SIZE_X,
                                   linearPixel / COMPACT_ACTIVE_PIXELS_TILE_SIZE_X);

    if (localPixel.x >= tileWidth || localPixel.y >= tileHeight)
    {
      continue;
    }

    const uint2 blockCoord = localPixel / uint2(COMPACT_ACTIVE_PIXELS_BLOCK_SIZE_X, COMPACT_ACTIVE_PIXELS_BLOCK_SIZE_Y);
    const uint2 blockLocalPixel = localPixel % uint2(COMPACT_ACTIVE_PIXELS_BLOCK_SIZE_X, COMPACT_ACTIVE_PIXELS_BLOCK_SIZE_Y);
    const uint blockIndex = blockCoord.y * COMPACT_ACTIVE_PIXELS_THREADGROUP_SIZE_X + blockCoord.x;
    const uint blockBit = blockLocalPixel.y * COMPACT_ACTIVE_PIXELS_BLOCK_SIZE_X + blockLocalPixel.x;

    const uint blockActiveMask = s_blockActiveMask[blockIndex];
    const bool isActive = ((blockActiveMask >> blockBit) & 1u) != 0u;
    const uint activeBefore = countbits(blockActiveMask & ((1u << blockBit) - 1u));

    // A tile over its allowance keeps an evenly spread subset rather than its leading run,
    // so the loss is spread across the tile.
    // Dropped pixels take the slots directly above the survivors, which keeps the compacted ordering a permutation.
    // The dense passes need every slot to own exactly one pixel.
    uint compactedLocalIndex;
    bool hasSlot = false;
    if (isActive)
    {
      const uint activeRank = s_blockActivePrefix[blockIndex] + activeBefore;

      // This branch is tile-uniform.
      // Clamping is rare, and skipping the clamp math for a tile that fits also keeps the products below 2^32 at a 256x256 tile.
      if (storedActiveCount == groupActiveCount)
      {
        hasSlot = true;
        compactedLocalIndex = activeRank;
      }
      else
      {
        const uint keptRank = (activeRank * storedActiveCount) / groupActiveCount;

        hasSlot = ((activeRank + 1u) * storedActiveCount) / groupActiveCount > keptRank;
        compactedLocalIndex = hasSlot
          ? keptRank
          : storedActiveCount + (activeRank - keptRank);
      }
    }
    else
    {
      // The block's rows above this one are in bounds too, each holding as many pixels as this row.
      const uint blockWidth = min(tileWidth - blockCoord.x * COMPACT_ACTIVE_PIXELS_BLOCK_SIZE_X,
                                  uint(COMPACT_ACTIVE_PIXELS_BLOCK_SIZE_X));
      const uint inBoundsBefore = blockLocalPixel.y * blockWidth + blockLocalPixel.x;

      compactedLocalIndex = groupActiveCount + s_blockInactivePrefix[blockIndex] + (inBoundsBefore - activeBefore);
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
    const uint2 pixelCoordinate = tileBasePixel + localPixel;

    // Active pixels the tile could not fit get no storage slot and are reported inactive everywhere.
    const uint globalIndex = globalBaseOffset + compactedLocalIndex;

    if (hasSlot)
    {
      // The list position is the global compacted index, so the storage coordinate follows from it by arithmetic
      // and the entry only has to carry the pixel.
      ActivePixelCoords[globalIndex] = packActivePixelCoordinate(pixelCoordinate);
    }

    // Every slot of the tile owns a pixel, including the inactive region,
    // so the dense passes still cover each pixel exactly once.
    ActiveLocalPixelCoords[compactedPixelCoordinate] = packLocalPixelCoordinate(localPixel);
    // This table maps the other direction, for threads that hold only a pixel coordinate.
    // The index is tile-local so that it fits in 16 bits, and the sentinel doubles as the activity answer.
    CompactedPixelIndices[pixelCoordinate] = uint16_t(hasSlot ? compactedLocalIndex : kInvalidCompactedIndex);
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
  [unroll]
  for (uint j = 0; j < COMPACT_ACTIVE_PIXELS_PIXELS_PER_THREAD; j++)
  {
    uint linearPixel = groupIndex + j * COMPACT_ACTIVE_PIXELS_GROUP_SIZE;
    uint2 localPixel = uint2(linearPixel % COMPACT_ACTIVE_PIXELS_TILE_SIZE_X,
                             linearPixel / COMPACT_ACTIVE_PIXELS_TILE_SIZE_X);
    uint2 pixelCoordinate = tileBasePixel + localPixel;
    const bool inBounds = all(pixelCoordinate < cb.camera.resolution);

    bool active = false;
    if (inBounds)
    {
      active = isActivePixelFromMask(ActivePixelMask, pixelCoordinate);
    }

    // WaveActiveBallot must be called by ALL lanes — hoisted out of the conditional.
    // The result is wave-uniform, and all lanes map to the same word because linearPixel/32 is constant across the wave.
    const uint4 activeBallot = WaveActiveBallot(active);
    if (WaveGetLaneIndex() == 0)
    {
      s_isActiveBitmap[linearPixel / 32] = activeBallot.x;
    }
  }

  GroupMemoryBarrierWithGroupSync();

  const uint tileWidth = min(uint(COMPACT_ACTIVE_PIXELS_TILE_SIZE_X), cb.camera.resolution.x - tileBasePixel.x);
  const uint tileHeight = min(uint(COMPACT_ACTIVE_PIXELS_TILE_SIZE_Y), cb.camera.resolution.y - tileBasePixel.y);

  // Morton ordering improves 2D spatial locality for downstream texture accesses.
  // Only used for full-size interior tiles; edge tiles fall back to row-major to avoid out-of-bounds writes.
  const bool useMorton = tileWidth == COMPACT_ACTIVE_PIXELS_TILE_SIZE_X
                      && tileHeight == COMPACT_ACTIVE_PIXELS_TILE_SIZE_Y;

  // Phase 2: Inclusive prefix sum + scatter to compacted output.
  scatterActivePixels(groupThreadId, groupId, groupIndex,
                      tileBasePixel, tileWidth, tileHeight, useMorton);
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
