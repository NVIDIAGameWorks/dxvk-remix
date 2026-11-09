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

// Wind waves of the water simulation. A wave spectrum, made once on the CPU, is advanced in time with deep water
// dispersion and turned into heights and slopes by an inverse FFT, every frame. Every wavelength travels at its own speed,
// so the waves change shape as they go instead of scrolling like a texture. A cascade is one periodic tile of
// WATER_SIM_WAVES_SIZE^2 texels; a group transforms one row or column in shared memory.

// Element: h0(k) (xy) and the conjugate of h0(-k) (zw), in texel order (frequencies 0..N/2-1, then -N/2..-1).
layout(binding = WATER_SIM_WAVES_BINDING_H0)
StructuredBuffer<float4> InitialSpectrum;

// Two complex spectra per texel: height + i slope x (xy), slope y (zw). One inverse FFT gives all three real fields.
layout(binding = WATER_SIM_WAVES_BINDING_SPECTRUM)
RWTexture2D<float4> Spectrum;

layout(binding = WATER_SIM_WAVES_BINDING_TEMP)
RWTexture2D<float4> Temp;

// Texel: height, slope along x, slope along y (world units), 0. Mip 0, or the mip that the mip pass writes.
layout(binding = WATER_SIM_WAVES_BINDING_OUTPUT)
RWTexture2D<float4> Output;

// The mip above the one that the mip pass writes.
layout(binding = WATER_SIM_WAVES_BINDING_MIP_INPUT)
RWTexture2D<float4> MipInput;

layout(push_constant)
ConstantBuffer<WaterSimWaveArgs> cb;

static const float kPi = 3.14159265358979;
static const uint kSize = WATER_SIM_WAVES_SIZE;

groupshared float4 s_line[WATER_SIM_WAVES_SIZE];

float2 complexMul(float2 a, float2 b)
{
  return float2(a.x * b.x - a.y * b.y, a.x * b.y + a.y * b.x);
}

// Signed frequency of a texel index.
int frequencyIndex(uint i)
{
  return i < kSize / 2 ? int(i) : int(i) - int(kSize);
}

uint reverseIndex(uint i)
{
  return reversebits(i) >> (32 - WATER_SIM_WAVES_LOG2_SIZE);
}

// Inverse FFT (no scaling) of the line in s_line, in place: input in bit reversed order, output in natural order. Every
// thread does one butterfly per stage, on two complex numbers at once.
void inverseFft(uint thread)
{
  for (uint half = 1; half < kSize; half *= 2)
  {
    GroupMemoryBarrierWithGroupSync();
    const uint j = thread & (half - 1);
    const uint i0 = (thread - j) * 2 + j;
    const uint i1 = i0 + half;
    const float angle = kPi * float(j) / float(half);
    const float2 w = float2(cos(angle), sin(angle));
    const float4 a = s_line[i0];
    const float4 b = s_line[i1];
    const float4 bw = float4(complexMul(b.xy, w), complexMul(b.zw, w));
    s_line[i0] = a + bw;
    s_line[i1] = a - bw;
  }
  GroupMemoryBarrierWithGroupSync();
}

[shader("compute")]
[numthreads(WATER_SIM_WAVES_SIZE / 2, 1, 1)]
void main(uint3 groupId : SV_GroupID, uint3 threadId : SV_GroupThreadID)
{
  const uint thread = threadId.x;

  if (cb.pass == WATER_SIM_WAVES_PASS_SPECTRUM)
  {
    // A group per row, two texels per thread.
    for (uint e = 0; e < 2; ++e)
    {
      const uint2 texel = uint2(thread + e * kSize / 2, groupId.x);
      const float2 k = float2(frequencyIndex(texel.x), frequencyIndex(texel.y)) * (2.0 * kPi / cb.tileSize);
      // Deep water: omega^2 = g k.
      const float omega = floor(sqrt(cb.gravity * length(k)) / cb.omega0) * cb.omega0;
      const float phase = omega * cb.time;
      const float2 rotation = float2(cos(phase), sin(phase));
      const float4 h0 = InitialSpectrum[texel.y * kSize + texel.x];
      // h(k, t) = h0(k) e^(i omega t) + conj(h0(-k)) e^(-i omega t): h(-k) = conj(h(k)), so the heights are real.
      const float2 h = complexMul(h0.xy, rotation) + complexMul(h0.zw, float2(rotation.x, -rotation.y));
      // The slopes are i k h, also real fields. Height + i * (i kx h) = (1 - kx) h.
      Spectrum[texel] = float4((1.0 - k.x) * h, -k.y * h.y, k.y * h.x);
    }
    return;
  }

  if (cb.pass == WATER_SIM_WAVES_PASS_ROWS || cb.pass == WATER_SIM_WAVES_PASS_COLUMNS)
  {
    const bool rows = cb.pass == WATER_SIM_WAVES_PASS_ROWS;
    const uint line = groupId.x;
    for (uint e = 0; e < 2; ++e)
    {
      const uint i = thread + e * kSize / 2;
      s_line[reverseIndex(i)] = rows ? Spectrum[uint2(i, line)] : Temp[uint2(line, i)];
    }

    inverseFft(thread);

    for (uint e = 0; e < 2; ++e)
    {
      const uint i = thread + e * kSize / 2;
      if (rows)
      {
        Temp[uint2(i, line)] = s_line[i];
      }
      else
      {
        const float4 v = s_line[i];
        Output[uint2(line, i)] = float4(v.x, v.y, v.z, 0.0);
      }
    }
    return;
  }

  // Mip pass: a texel per thread, the mean of 2x2 texels of the mip above.
  const uint index = groupId.x * (kSize / 2) + thread;
  if (index >= cb.mipSize * cb.mipSize)
  {
    return;
  }
  const uint2 texel = uint2(index % cb.mipSize, index / cb.mipSize);
  const float4 sum = MipInput[texel * 2] + MipInput[texel * 2 + uint2(1, 0)] + MipInput[texel * 2 + uint2(0, 1)] + MipInput[texel * 2 + uint2(1, 1)];
  Output[texel] = 0.25 * sum;
}
