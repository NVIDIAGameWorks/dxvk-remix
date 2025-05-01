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

#include "util_bridgecommand.h"
#include "util_common.h"

#include <d3d9.h>
#include <algorithm>
#include <functional>

 // Driver Hacks / Unofficial Formats
#define D3DFMT_ATI1 MAKEFOURCC('A', 'T', 'I', '1')
#define D3DFMT_ATI2 MAKEFOURCC('A', 'T', 'I', '2')
#define D3DFMT_INST MAKEFOURCC('I', 'N', 'S', 'T')
#define D3DFMT_DF24 MAKEFOURCC('D', 'F', '2', '4')
#define D3DFMT_DF16 MAKEFOURCC('D', 'F', '1', '6')
#define D3DFMT_NULL_FORMAT MAKEFOURCC('N', 'U', 'L', 'L')
#define D3DFMT_GET4 MAKEFOURCC('G', 'E', 'T', '4')
#define D3DFMT_GET1 MAKEFOURCC('G', 'E', 'T', '1')
#define D3DFMT_NVDB MAKEFOURCC('N', 'V', 'D', 'B')
#define D3DFMT_A2M1 MAKEFOURCC('A', '2', 'M', '1')
#define D3DFMT_A2M0 MAKEFOURCC('A', '2', 'M', '0')
#define D3DFMT_ATOC MAKEFOURCC('A', 'T', 'O', 'C')
#define D3DFMT_INTZ MAKEFOURCC('I', 'N', 'T', 'Z')
#define D3DFMT_RAWZ MAKEFOURCC('R', 'A', 'W', 'Z')
#define D3DFMT_RESZ MAKEFOURCC('R', 'E', 'S', 'Z')
#define D3DFMT_NV11 MAKEFOURCC('N', 'V', '1', '1')
#define D3DFMT_NV12 MAKEFOURCC('N', 'V', '1', '2')
#define D3DFMT_P010 MAKEFOURCC('P', '0', '1', '0') // Same as NV12 but 10 bit
#define D3DFMT_P016 MAKEFOURCC('P', '0', '1', '6') // Same as NV12 but 16 bit
#define D3DFMT_Y210 MAKEFOURCC('Y', '2', '1', '0')
#define D3DFMT_Y216 MAKEFOURCC('Y', '2', '1', '6')
#define D3DFMT_Y410 MAKEFOURCC('Y', '4', '1', '0')
#define D3DFMT_AYUV MAKEFOURCC('A', 'Y', 'U', 'V')
#define D3DFMT_YV12 MAKEFOURCC('Y', 'V', '1', '2')
#define D3DFMT_OPAQUE_420 MAKEFOURCC('4', '2', '0', 'O')

// Not supported but exist
#define D3DFMT_AI44 MAKEFOURCC('A', 'I', '4', '4')
#define D3DFMT_IA44 MAKEFOURCC('I', 'A', '4', '4')
#define D3DFMT_R2VB MAKEFOURCC('R', '2', 'V', 'B')
#define D3DFMT_COPM MAKEFOURCC('C', 'O', 'P', 'M')
#define D3DFMT_SSAA MAKEFOURCC('S', 'S', 'A', 'A')
#define D3DFMT_AL16 MAKEFOURCC('A', 'L', '1', '6')
#define D3DFMT_R16  MAKEFOURCC(' ', 'R', '1', '6')
#define D3DFMT_EXT1 MAKEFOURCC('E', 'X', 'T', '1')
#define D3DFMT_FXT1 MAKEFOURCC('F', 'X', 'T', '1')
#define D3DFMT_GXT1 MAKEFOURCC('G', 'X', 'T', '1')
#define D3DFMT_HXT1 MAKEFOURCC('H', 'X', 'T', '1')


namespace bridge_util {
  static uint32_t getBlockSize(const D3DFORMAT& format) {
    switch (format) {
    case D3DFMT_DXT1:
    case D3DFMT_DXT2:
    case D3DFMT_DXT3:
    case D3DFMT_DXT4:
    case D3DFMT_DXT5:
      return 4;
    // Dummy value to align IncomingPitch and SlicePitch with DXVK for D3DFMT_ATI2 format
    case D3DFMT_ATI1:
    case D3DFMT_ATI2:
      return 1;
    default:
      return 1;
    }
  }

  // Determines the bytes per pixel for the given color format
  static uint32_t getBytesFromFormat(const D3DFORMAT& format) {
    switch (format) {
    case D3DFMT_DXT1:
      return 8;
    case D3DFMT_DXT2:
    case D3DFMT_DXT3:
    case D3DFMT_DXT4:
    case D3DFMT_DXT5:
      return 16;
    // Dummy value to align IncomingPitch and SlicePitch with DXVK for D3DFMT_ATI2 format
    case D3DFMT_ATI1:
    case D3DFMT_ATI2:
      return 1;
    // Explicitly unsupported
    case D3DFMT_RESZ:
    case D3DFMT_INST:
    case D3DFMT_NVDB:
    case D3DFMT_GET4:
    case D3DFMT_GET1:
    case D3DFMT_NULL_FORMAT:
    case D3DFMT_A2M1:
    case D3DFMT_A2M0:
    case D3DFMT_ATOC:
    case D3DFMT_RAWZ:
    case D3DFMT_NV11:
    case D3DFMT_P010:
    case D3DFMT_Y210:
    case D3DFMT_Y216:
    case D3DFMT_Y410:
    case D3DFMT_AYUV:
    case D3DFMT_OPAQUE_420:
    case D3DFMT_AI44:
    case D3DFMT_IA44:
    case D3DFMT_R2VB:
    case D3DFMT_COPM:
    case D3DFMT_SSAA:
    case D3DFMT_AL16:
    case D3DFMT_R16:
    case D3DFMT_EXT1:
    case D3DFMT_FXT1:
    case D3DFMT_GXT1:
    case D3DFMT_HXT1:
      return 0;

    case D3DFMT_A32B32G32R32F:
      return 16;

    case D3DFMT_A16B16G16R16:
    case D3DFMT_Q16W16V16U16:
    case D3DFMT_A16B16G16R16F:
    case D3DFMT_G32R32F:
    case D3DFMT_MULTI2_ARGB8:
      return 8;

    case D3DFMT_A8R8G8B8:
    case D3DFMT_X8R8G8B8:
    case D3DFMT_D32:
    case D3DFMT_D24S8:
    //case D3DFMT_S8D24:
    case D3DFMT_X8L8V8U8:
    //case D3DFMT_X4S4D24:
    case D3DFMT_D24X4S4:
    case D3DFMT_Q8W8V8U8:
    case D3DFMT_V16U16:
    case D3DFMT_A2W10V10U10:
    case D3DFMT_A2B10G10R10:
    case D3DFMT_A8B8G8R8:
    case D3DFMT_X8B8G8R8:
    case D3DFMT_G16R16:
    case D3DFMT_D24X8:
    //case D3DFMT_X8D24:
    //case D3DFMT_W11V11U10:
    case D3DFMT_A2R10G10B10:
    case D3DFMT_G16R16F:
    case D3DFMT_R32F:
    case D3DFMT_D32F_LOCKABLE:
    case D3DFMT_D24FS8:
    case D3DFMT_D32_LOCKABLE:
    case D3DFMT_DF24:
    case D3DFMT_INTZ:
      return 4;

    case D3DFMT_R8G8B8:
    case D3DFMT_YV12:
      return 3;

    case D3DFMT_R5G6B5:
    case D3DFMT_X1R5G5B5:
    case D3DFMT_A1R5G5B5:
    case D3DFMT_A4R4G4B4:
    case D3DFMT_A8L8:
    case D3DFMT_V8U8:
    case D3DFMT_L6V5U5:
    case D3DFMT_D16:
    case D3DFMT_D16_LOCKABLE:
    case D3DFMT_D15S1:
    //case D3DFMT_S1D15:
    case D3DFMT_A8P8:
    case D3DFMT_A8R3G3B2:
    case D3DFMT_UYVY:
    case D3DFMT_YUY2:
    case D3DFMT_X4R4G4B4:
    case D3DFMT_CxV8U8:
    case D3DFMT_L16:
    case D3DFMT_R16F:
    case D3DFMT_R8G8_B8G8:
    case D3DFMT_G8R8_G8B8:
    case D3DFMT_DF16:
    case D3DFMT_P016:
      return 2;

    case D3DFMT_P8:
    case D3DFMT_L8:
    case D3DFMT_R3G3B2:
    case D3DFMT_A4L4:
    case D3DFMT_A8:
    case D3DFMT_A1:
    case D3DFMT_S8_LOCKABLE:
    case D3DFMT_NV12:
      return 1;

    default:
      Logger::err("Unknown D3DFORMAT passed for conversion: "+ std::to_string(static_cast<int>(format)));
      throw;
      return 0;
    }
  }

  // Num pixels OR num compressed pixels
  static inline uint32_t calcStride(const uint32_t numPixels, const D3DFORMAT format) {
    const uint32_t effectivePixelsPerUnit = getBlockSize(format);
    // If effectivePixelsPerUnit == 4, numPixels is compressed
    return ((numPixels + effectivePixelsPerUnit - 1) / effectivePixelsPerUnit);
  }

  static inline uint32_t calcRowSize(const uint32_t width, const D3DFORMAT format) {
    const uint32_t numPixelsInRow = calcStride(width, format);
    const uint32_t bytesPerPixel = getBytesFromFormat(format);
    return std::max(caps::MinSurfacePitch, numPixelsInRow * bytesPerPixel);
  }

  static inline uint32_t calcTotalSizeOfRect(const uint32_t width, const uint32_t height, const D3DFORMAT format) {
    const uint32_t numRows = calcStride(height, format);
    const uint32_t rowSize = calcRowSize(width, format);
    return numRows * rowSize;
  }

  static inline size_t calcImageByteOffset(const int32_t pitch, const RECT& rect, const D3DFORMAT format) {
    const uint32_t y = calcStride(rect.top, format); // unit: unitless ('y' is a row ID); pitch unit: bytes
    const uint32_t x = calcStride(rect.left, format); // unit: pixels
    const uint32_t bytesPerPixel = getBytesFromFormat(format);
    return  y * pitch + bytesPerPixel * x;
  }

  struct RectDecompInfo {
    size_t baseX, baseY, width, height;
  };
  static RectDecompInfo getDecomposedRectInfo(const D3DSURFACE_DESC& desc, const RECT* pRect) {
    RectDecompInfo decomp;
    decomp.baseX  = (pRect) ? pRect->left : 0;
    decomp.baseY  = (pRect) ? pRect->top : 0;
    decomp.width  = (pRect) ? pRect->right - pRect->left : desc.Width;
    decomp.height = (pRect) ? pRect->bottom - pRect->top : desc.Height;
    return decomp;
  }
}

#define FOR_EACH_RECT_ROW(LOCKED_RECT, HEIGHT, FORMAT, DO_THIS_TO_ptr)   \
{                                                                        \
  const uint32_t columnStride = bridge_util::calcStride(HEIGHT, FORMAT); \
  for (uint32_t y = 0; y < columnStride; y++) {                          \
    auto ptr = (PBYTE) LOCKED_RECT.pBits + y * LOCKED_RECT.Pitch;        \
    {                                                                    \
      DO_THIS_TO_ptr                                                     \
    }                                                                    \
  }                                                                      \
}
