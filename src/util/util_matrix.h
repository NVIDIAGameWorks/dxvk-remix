/*
* Copyright (c) 2021-2022, NVIDIA CORPORATION. All rights reserved.
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

#include "util_vector.h"
#include "vulkan/vulkan_core.h"

namespace dxvk {

  class Matrix4 {

    public:

    // Identity
    inline Matrix4() {
      data[0] = { 1, 0, 0, 0 };
      data[1] = { 0, 1, 0, 0 };
      data[2] = { 0, 0, 1, 0 };
      data[3] = { 0, 0, 0, 1 };
    }

    // Produces a scalar matrix, x * Identity
    inline explicit Matrix4(float x) {
      data[0] = { x, 0, 0, 0 };
      data[1] = { 0, x, 0, 0 };
      data[2] = { 0, 0, x, 0 };
      data[3] = { 0, 0, 0, x };
    }

    inline Matrix4(
      const Vector4& v0,
      const Vector4& v1,
      const Vector4& v2,
      const Vector4& v3) {
      data[0] = v0;
      data[1] = v1;
      data[2] = v2;
      data[3] = v3;
    }

    inline Matrix4(const float matrix[4][4]) {
      data[0] = Vector4(matrix[0]);
      data[1] = Vector4(matrix[1]);
      data[2] = Vector4(matrix[2]);
      data[3] = Vector4(matrix[3]);
    }

    inline Matrix4(const VkTransformMatrixKHR matrix) {
      data[0] = Vector4(matrix.matrix[0]);
      data[1] = Vector4(matrix.matrix[1]);
      data[2] = Vector4(matrix.matrix[2]);
      data[3] = Vector4(0, 0, 0, 1);
    }
    
    inline Matrix4(const float m00, const float m01, const float m02, const float m03,
                   const float m10, const float m11, const float m12, const float m13,
                   const float m20, const float m21, const float m22, const float m23,
                   const float m30, const float m31, const float m32, const float m33) {
      data[0] = Vector4(m00, m01, m02, m03);
      data[1] = Vector4(m10, m11, m12, m13);
      data[2] = Vector4(m20, m21, m22, m23);
      data[3] = Vector4(m30, m31, m32, m33);
    }

    Matrix4(const Matrix4& other) = default;

    Vector4& operator[](size_t index);
    const Vector4& operator[](size_t index) const;

    bool operator==(const Matrix4& m2) const;
    bool operator!=(const Matrix4& m2) const;

    Matrix4 operator+(const Matrix4& other) const;
    Matrix4 operator-(const Matrix4& other) const;

    Matrix4 operator*(const Matrix4& m2) const;
    Vector4 operator*(const Vector4& v) const;
    Matrix4 operator*(float scalar) const;

    Matrix4 operator/(float scalar) const;

    Matrix4& operator+=(const Matrix4& other);
    Matrix4& operator-=(const Matrix4& other);

    Matrix4& operator*=(const Matrix4& other);

    Vector4 data[4];

  };
  
  static_assert(sizeof(Matrix4) == sizeof(Vector4) * 4);

  inline Matrix4 operator*(float scalar, const Matrix4& m) { return m * scalar; }

  Matrix4 transpose(const Matrix4& m);

  double determinant(const Matrix4& m);

  Matrix4 inverse(const Matrix4& m);

  Matrix4 hadamardProduct(const Matrix4& a, const Matrix4& b);

  std::ostream& operator<<(std::ostream& os, const Matrix4& m);

  Matrix4 translationMatrix(const Vector3& v);

  // NV-DXVK start
  // Fast check if Matrix4 is an exact identity matrix
  static inline bool isIdentityExact(const Matrix4& m) {
#ifdef _M_X64
    // Identity vector
    __m128 iv = _mm_set_ss(1.f);

    for (const Vector4& unalignedVec : m.data) {
      const __m128 vec = _mm_loadu_ps(unalignedVec.data);

      // Check if current vector is not equal to identity vector
      const __m128 eq = _mm_cmpeq_ps(vec, iv);

      if (_mm_movemask_epi8(_mm_castps_si128(eq)) != 0xffff) {
        return false;
      }

      // Shift identity vector left
      iv = _mm_shuffle_ps(iv, iv, _MM_SHUFFLE(2, 1, 0, 3));
    }

    return true;
#else
    return m == Matrix4();
#endif
  }
  // NV-DXVK end

  class Matrix3 {

    public:

    // Identity
    inline Matrix3() {
      data[0] = { 1, 0, 0 };
      data[1] = { 0, 1, 0 };
      data[2] = { 0, 0, 1 };
    }

    // Produces a scalar matrix, x * Identity
    inline explicit Matrix3(float x) {
      data[0] = { x, 0, 0 };
      data[1] = { 0, x, 0 };
      data[2] = { 0, 0, x };
    }

    inline Matrix3(
      const Vector3& v0,
      const Vector3& v1,
      const Vector3& v2) {
      data[0] = v0;
      data[1] = v1;
      data[2] = v2;
    }

    inline Matrix3(const float matrix[3][3]) {
      data[0] = Vector3(matrix[0]);
      data[1] = Vector3(matrix[1]);
      data[2] = Vector3(matrix[2]);
    }

    inline Matrix3(const Matrix4& other) {
      data[0] = Vector3(other[0].x, other[0].y, other[0].z);
      data[1] = Vector3(other[1].x, other[1].y, other[1].z);
      data[2] = Vector3(other[2].x, other[2].y, other[2].z);
    }

    Matrix3(const Matrix3& other) = default;

    Vector3& operator[](size_t index);
    const Vector3& operator[](size_t index) const;

    bool operator==(const Matrix3& m2) const;
    bool operator!=(const Matrix3& m2) const;

    Matrix3 operator+(const Matrix3& other) const;
    Matrix3 operator-(const Matrix3& other) const;

    Matrix3 operator*(const Matrix3& m2) const;
    Vector3 operator*(const Vector3& v) const;
    Matrix3 operator*(float scalar) const;

    Matrix3 operator/(float scalar) const;

    Matrix3& operator+=(const Matrix3& other);
    Matrix3& operator-=(const Matrix3& other);

    Matrix3& operator*=(const Matrix3& other);

    Vector3 data[3];

  };

  static_assert(sizeof(Matrix3) == sizeof(Vector3) * 3);

  Matrix3 transpose(const Matrix3& m);

  Matrix3 inverse(const Matrix3& m);

  bool isLeftHanded(const Matrix3& m);

  std::ostream& operator<<(std::ostream& os, const Matrix3& m);

}