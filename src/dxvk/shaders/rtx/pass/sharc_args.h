/*
* Copyright (c) 2025-2026, NVIDIA CORPORATION. All rights reserved.
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

// The update query reservoir stores the selected pixel's offset within its update cell in this many bits per axis,
// which limits how many query pixels an update cell may span.
#define SHARC_UPDATE_QUERY_OFFSET_BITS_PER_AXIS 7
#define SHARC_MAX_QUERY_PIXELS_PER_UPDATE_PIXEL_PER_AXIS (1u << SHARC_UPDATE_QUERY_OFFSET_BITS_PER_AXIS)

// Indices into the SHaRC update counters buffer.
#define SHARC_UPDATE_COUNTER_RECORDS 0
#define SHARC_UPDATE_COUNTER_PATHS 1
#define SHARC_UPDATE_COUNTER_COUNT 2

// Note: Ensure 16B alignment
struct SharcArgs
{
  vec3 cameraPosition;
  // Number of rows at the start of a combined update and query dispatch that trace update paths.
  uint numRowsForUpdate;
  vec3 cameraPositionPrev;
  uint pad1;

  int accumulationFrameNum;
  int staleFrameNum;
  float radianceScale;
  int entriesNum;

  float sceneScale;
  float isotropicRoughnessThreshold;
  float fireflyClampMultiplier;
  uint updateAllowRussianRoulette;

  vec2 updatePixelJitter;
  vec2 activeUpdateDimensions;

  vec2 queryToUpdateCoordinateSpace;
  vec2 updateToQueryCoordinateSpace;
};
