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

enum class BlendType : uint8_t {
  kAlpha = 0,
  kAlphaEmissive = 1,
  kReverseAlphaEmissive = 2,
  kColor = 3,
  kColorEmissive = 4,
  kReverseColorEmissive = 5,
  kEmissive = 6,
  kMultiplicative = 7,
  kDoubleMultiplicative = 8,
  kReverseAlpha = 9,
  kReverseColor = 10,

  kMinValue = 0, // kAlpha
  kMaxValue = 10, // kReverseColor
};

static const uint32_t surfaceBlendTypeMask = 0xfu;

// Note: Use caution when changing this enum, must match the values defined on the MDL side of things
// as well as matching Vulkan's alpha compare operations due to conversions from these values at some points
// in the code.
enum class AlphaTestType : uint8_t {
  kNever = 0,
  kLess = 1,
  kEqual = 2,
  kLessOrEqual = 3,
  kGreater = 4,
  kNotEqual = 5,
  kGreaterOrEqual = 6,
  kAlways = 7,

  kMinValue = 0, // kNever
  kMaxValue = 7, // kAlways
};

static const uint32_t alphaTestTypeMask = 0x7u;

enum class RtTextureArgSource : uint8_t {
  None = 0,
  Texture,
  VertexColor0,
  TFactor
};

// Texture stage operation, correspond to D3DTEXTUREOP
enum class DxvkRtTextureOperation : uint8_t {
  Disable = 0,
  SelectArg1,
  SelectArg2,
  Modulate,
  Modulate2x,
  Modulate4x,
  Add,
  Force_Modulate2x,
};

enum class TexGenMode : uint8_t {
  None = 0,
  ViewPositions,
  CascadedViewPositions,
  ViewNormals,

  Count
};

enum class DisplacementMode : uint32_t {
  Off = 0,
  RaymarchPOM = 1,
  QuadtreePOM = 2
};

enum class RussianRouletteMode : uint32_t {
  ThroughputBased = 0,
  SpecularBased = 1
};

