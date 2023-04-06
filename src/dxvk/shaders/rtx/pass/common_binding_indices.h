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

#ifndef COMMON_BINDING_INDICES_H
#define COMMON_BINDING_INDICES_H

// These are set indices - not bindings
#define BINDING_SET_BINDLESS_RAW_BUFFER          1
#define BINDING_SET_BINDLESS_TEXTURE2D           2


#define BINDING_ACCELERATION_STRUCTURE           0
#define BINDING_ACCELERATION_STRUCTURE_PREVIOUS  1
#define BINDING_ACCELERATION_STRUCTURE_UNORDERED 2
#define BINDING_SURFACE_DATA_BUFFER              3
#define BINDING_SURFACE_MAPPING_BUFFER           4
#define BINDING_SURFACE_MATERIAL_DATA_BUFFER     5
#define BINDING_VOLUME_MATERIAL_DATA_BUFFER      6
#define BINDING_LIGHT_DATA_BUFFER                7
#define BINDING_PREVIOUS_LIGHT_DATA_BUFFER       8
#define BINDING_LIGHT_MAPPING                    9
#define BINDING_BILLBOARDS_BUFFER                10
#define BINDING_BLUE_NOISE_TEXTURE               11
#define BINDING_BINDLESS_INDICES_BUFFER          12
#define BINDING_CONSTANTS                        13
#define BINDING_DEBUG_VIEW_TEXTURE               14

#define COMMON_MAX_BINDING                       14
#define COMMON_NUM_BINDINGS                      (COMMON_MAX_BINDING + 1)

// Note: Used to represent a non-existent buffer and material index in the Surface,
// as well as texture index in the Surface Material.
#define BINDING_INDEX_INVALID uint16_t(0xFFFF)

// Note: Light array may only be up to a size of 2^16-1, allowing the last index to be
// used for an invalid index similar to the max binding index for materials.
#define LIGHT_INDEX_INVALID (0xFFFF)

#ifdef __cplusplus

#define COMMON_RAYTRACING_BINDINGS \
  ACCELERATION_STRUCTURE(BINDING_ACCELERATION_STRUCTURE)            \
  ACCELERATION_STRUCTURE(BINDING_ACCELERATION_STRUCTURE_UNORDERED)  \
  ACCELERATION_STRUCTURE(BINDING_ACCELERATION_STRUCTURE_PREVIOUS)   \
  STRUCTURED_BUFFER(BINDING_SURFACE_DATA_BUFFER)                    \
  STRUCTURED_BUFFER(BINDING_SURFACE_MAPPING_BUFFER)                 \
  STRUCTURED_BUFFER(BINDING_SURFACE_MATERIAL_DATA_BUFFER)           \
  STRUCTURED_BUFFER(BINDING_VOLUME_MATERIAL_DATA_BUFFER)            \
  STRUCTURED_BUFFER(BINDING_LIGHT_DATA_BUFFER)                      \
  STRUCTURED_BUFFER(BINDING_PREVIOUS_LIGHT_DATA_BUFFER)             \
  STRUCTURED_BUFFER(BINDING_LIGHT_MAPPING)                          \
  STRUCTURED_BUFFER(BINDING_BILLBOARDS_BUFFER)                      \
  TEXTURE2DARRAY(BINDING_BLUE_NOISE_TEXTURE)                        \
  CONSTANT_BUFFER(BINDING_CONSTANTS)                                \
  RW_TEXTURE2D(BINDING_DEBUG_VIEW_TEXTURE)
  
#endif

#endif
