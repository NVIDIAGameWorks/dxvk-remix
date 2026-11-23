/*
* Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.
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
#ifndef LIGHT_TYPES_H
#define LIGHT_TYPES_H

static const uint lightTypeSphere = 0;
static const uint lightTypeRect = 1;
static const uint lightTypeDisk = 2;
static const uint lightTypeCylinder = 3;
static const uint lightTypeDistant = 4;
static const uint lightTypeMask = 0x7;
static const uint lightTypeInvalid = lightTypeMask;

static const uint lightTypeCount = 5;

// Marks a sphere light that projects a gobo texture. Stored in the flag word of the packed light, data3.w.
static const uint lightFlagGobo = 1u << 3;

// The gobo's options in the flag word of the packed light.
static const uint lightGoboWrapShift = 4;
static const uint lightGoboWrapMask = 0x3;
static const uint lightFlagGoboInvert = 1u << 6;
static const uint lightFlagGoboUseAlpha = 1u << 7;
static const uint lightFlagGoboGrayscale = 1u << 8;
static const uint lightFlagGoboDecodeSrgb = 1u << 9;

static const uint lightGoboWrapClamp = 0;
static const uint lightGoboWrapRepeat = 1;
static const uint lightGoboWrapMirror = 2;
static const uint lightGoboWrapExtend = 3;

// The number of animated gobo lights that can shorten the volumetric lighting history in their cones.
static const uint lightGoboAnimatedLightCapacity = 8;

// Decodes the low 16 bits of a value as the two's complement snorm that glm::packSnorm1x16 writes for the gobo tangent.
// Note: The CPU compiles this too, so the unit tests decode the tangent with the shaders' own code.
#ifdef __cplusplus
inline
#endif
float lightGoboDecodeSnorm16(uint value)
{
  const int signedValue = int(value << 16) >> 16;
  const float decoded = float(signedValue) / 32767.0f;

  return decoded < -1.0f ? -1.0f : decoded;
}

#endif // LIGHT_TYPES_H