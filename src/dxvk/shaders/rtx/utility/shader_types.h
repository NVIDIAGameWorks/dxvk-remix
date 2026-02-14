/*
* Copyright (c) 2021-2024, NVIDIA CORPORATION. All rights reserved.
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

#include <cstring>
#include <algorithm>
#include "../../../../util/util_matrix.h"
#include "MathLib/MathLib.h"

// Scalars

typedef uint32_t uint;

// Vectors

struct vec2 {
  float x;
  float y;

  vec2() = default;
  vec2(float _x, float _y) : x(_x), y(_y) {}
  vec2& operator=(const dxvk::Vector2& v)
  {
    x = v.x;
    y = v.y;
    return *this;
  }

  vec2 operator+(const vec2& other) const { return vec2(x + other.x, y + other.y); }
  vec2 operator-(const vec2& other) const { return vec2(x - other.x, y - other.y); }
  vec2 operator*(float s) const { return vec2(x * s, y * s); }
  friend vec2 operator*(float s, const vec2& v) { return vec2(s * v.x, s * v.y); }
};

struct vec3 {
  float x;
  float y;
  float z;

  vec3() = default;
  vec3(float _x, float _y, float _z) : x(_x), y(_y), z(_z) {}
  vec3(const dxvk::Vector3& v)
  {
    x = v.x;
    y = v.y;
    z = v.z;
  }
  vec3& operator=(const dxvk::Vector3& v)
  {
    x = v.x;
    y = v.y;
    z = v.z;
    return *this;
  }

  vec3 operator+(const vec3& other) const { return vec3(x + other.x, y + other.y, z + other.z); }
  vec3 operator-(const vec3& other) const { return vec3(x - other.x, y - other.y, z - other.z); }
  vec3 operator*(float s) const { return vec3(x * s, y * s, z * s); }
  friend vec3 operator*(float s, const vec3& v) { return vec3(s * v.x, s * v.y, s * v.z); }
};

struct alignas(16) vec4 {
  float x;
  float y;
  float z;
  float w;

  vec4() = default;
  explicit vec4(float _swiz) : x(_swiz), y(_swiz), z(_swiz), w(_swiz) {}
  vec4(vec2 _xy, float _z, float _w) : x(_xy.x), y(_xy.y), z(_z), w(_w) {}
  vec4(vec3 _xyz, float _w) : x(_xyz.x), y(_xyz.y), z(_xyz.z), w(_w) {}
  vec4(float _x, float _y, float _z, float _w) : x(_x), y(_y), z(_z), w(_w) {}
  vec4& operator=(const dxvk::Vector4& v)
  {
    x = v.x;
    y = v.y;
    z = v.z;
    w = v.w;
    return *this;
  }

  vec4 operator+(const vec4& other) const { return vec4(x + other.x, y + other.y, z + other.z, w + other.w); }
  vec4 operator-(const vec4& other) const { return vec4(x - other.x, y - other.y, z - other.z, w - other.w); }
  vec4 operator*(float s) const { return vec4(x * s, y * s, z * s, w * s); }
  friend vec4 operator*(float s, const vec4& v) { return vec4(s * v.x, s * v.y, s * v.z, s * v.w); }
};

typedef uint16_t half;

struct f16vec2 {
  uint16_t x;
  uint16_t y;
};

struct f16vec3 {
  uint16_t x;
  uint16_t y;
  uint16_t z;
};

struct f16vec4 {
  uint16_t x;
  uint16_t y;
  uint16_t z;
  uint16_t w;
};

struct ivec2 {
  int x;
  int y;
};

struct uvec2 {
  uint x;
  uint y;
};

struct uvec3 {
  uint x;
  uint y;
  uint z;
};

struct uvec4 {
  uint x;
  uint y;
  uint z;
  uint w;
};

struct u16vec2 {
  uint16_t x;
  uint16_t y;
};

struct u16vec3 {
  uint16_t x;
  uint16_t y;
  uint16_t z;
};

struct u16vec4 {
  uint16_t x;
  uint16_t y;
  uint16_t z;
  uint16_t w;
};

// Matrices

struct alignas(16) mat4 {
  vec4 m[4];

  mat4() = default;

  mat4(const dxvk::Matrix4& v)
  {
    std::memcpy(m, v.data, sizeof(m));
  }

  mat4& operator=(const dxvk::Matrix4& v)
  {
    std::memcpy(m, v.data, sizeof(m));
    return *this;
  }

  vec4& operator[](size_t index) {
    return m[index];
  }
};

struct alignas(16) mat4x3 {
  vec3 m[4];

  mat4x3() = default;

  mat4x3(const dxvk::Matrix4& v) {
    for (uint32_t i = 0; i < 4; i++) {
      std::memcpy(&m[i], &v[i], sizeof(float) * 3);
    }
  }

  mat4x3& operator=(dxvk::Matrix4& v) {
    for (uint32_t i = 0; i < 4; i++) {
      std::memcpy(&m[i], &v[i], sizeof(float) * 3);
    }
    return *this;
  }

  vec3& operator[](size_t index) {
    return m[index];
  }
};

// Functions

inline float dot(vec2 a, vec2 b) {
  return a.x * b.x + a.y * b.y;
}

inline float dot(vec4 a, vec4 b) {
  return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
}

inline float saturate(float x) {
  return std::clamp(x, 0.0f, 1.0f);
}

inline float uintBitsToFloat(uint32_t x) {
  float y;

  std::memcpy(&y, &x, sizeof(y));

  return y;
}

inline int32_t floatBitsToInt(float x) {
  int32_t y;

  std::memcpy(&y, &x, sizeof(y));

  return y;
}

#else // __cplusplus

#include "rtx/utility/shader_types.slangh"

#endif // __cplusplus
