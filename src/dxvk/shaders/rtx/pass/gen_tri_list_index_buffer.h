/*
* Copyright (c) 2021-2023, NVIDIA CORPORATION. All rights reserved.
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

#ifdef __cplusplus
template<typename srcIndexType>
uint32_t genTriListLoadIndex(const srcIndexType* buf, uint32_t i, uint32_t) {
  return (uint32_t)buf[i];
}

// CPU path always writes uint16 indices; useUint32 is unused but matches the shared generateIndices body.
inline void genTriListStoreIndex(uint16_t* buf, uint32_t pos, uint32_t value, uint32_t /*useUint32*/) {
  buf[pos] = static_cast<uint16_t>(value);
}
#else
// GPU helpers for loading/storing indices from uint16_t structured buffers,
// supporting both 16-bit and packed 32-bit index formats.
uint32_t genTriListLoadIndex(StructuredBuffer<uint16_t> buf, uint32_t i, uint32_t useUint32) {
  if (useUint32 != 0)
    return uint32_t(buf[i * 2]) | (uint32_t(buf[i * 2 + 1]) << 16);
  return uint32_t(buf[i]);
}

void genTriListStoreIndex(RWStructuredBuffer<uint16_t> buf, uint32_t pos, uint32_t value, uint32_t useUint32) {
  if (useUint32 != 0) {
    buf[pos * 2]     = uint16_t(value);
    buf[pos * 2 + 1] = uint16_t(value >> 16);
  } else {
    buf[pos] = uint16_t(value);
  }
}
#endif

// This function can be executed on the CPU or GPU!!
#ifdef __cplusplus
template<typename srcIndexType>
void generateIndices(const uint32_t idx, uint16_t* dst, const srcIndexType* src, const GenTriListArgs& cb)
#else
void generateIndices(const uint32_t idx, RWStructuredBuffer<uint16_t> dst, StructuredBuffer<uint16_t> src, GenTriListArgs cb)
#endif
{
  uint32_t i0 = 0;
  uint32_t i1 = 0;
  uint32_t i2 = 0;

  switch (cb.topology)
  {
  case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN:
      i0 = 0;
      i1 = idx + 1;
      i2 = idx + 2;
      break;
  case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP:
      i0 = idx + 0;
      i1 = idx + 1 + (idx & 1);
      i2 = idx + 2 - (idx & 1);
      break;
  case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST:
      i0 = idx * 3 + 0;
      i1 = idx * 3 + 1;
      i2 = idx * 3 + 2;
      break;
  }

  // NOTE: firstIndex can be greater than 0xFFFF, be careful when applying to uint16 indices
  i0 += cb.firstIndex;
  i1 += cb.firstIndex;
  i2 += cb.firstIndex;

  if (cb.useIndexBuffer != 0)
  {
    uint32_t idx0 = genTriListLoadIndex(src, i0, cb.srcUseUint32);
    uint32_t idx1 = genTriListLoadIndex(src, i1, cb.srcUseUint32);
    uint32_t idx2 = genTriListLoadIndex(src, i2, cb.srcUseUint32);

    // degen, or invalid
    if (idx0 == idx1 || idx0 == idx2 || idx1 == idx2 ||
        idx0 > cb.maxVertex || idx1 > cb.maxVertex || idx2 > cb.maxVertex)
    {
      idx0 = cb.minVertex;
      idx1 = cb.minVertex;
      idx2 = cb.minVertex;
    }

    genTriListStoreIndex(dst, idx * 3 + 0, idx0 - cb.minVertex, cb.useUint32);
    genTriListStoreIndex(dst, idx * 3 + 1, idx1 - cb.minVertex, cb.useUint32);
    genTriListStoreIndex(dst, idx * 3 + 2, idx2 - cb.minVertex, cb.useUint32);
  }
  else
  {
    genTriListStoreIndex(dst, idx * 3 + 0, i0 - cb.minVertex, cb.useUint32);
    genTriListStoreIndex(dst, idx * 3 + 1, i1 - cb.minVertex, cb.useUint32);
    genTriListStoreIndex(dst, idx * 3 + 2, i2 - cb.minVertex, cb.useUint32);
  }
}
