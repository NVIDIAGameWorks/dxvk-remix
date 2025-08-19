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

#include "util_math.h"
#include "util_vector.h"
#include "vulkan/vulkan_core.h"
#include "log/log.h"

namespace dxvk {

  template<typename T>
  class Matrix4Base {

    public:

    // Identity
    inline Matrix4Base() {
      data[0] = Vector4Base<T>{ 1, 0, 0, 0 };
      data[1] = Vector4Base<T>{ 0, 1, 0, 0 };
      data[2] = Vector4Base<T>{ 0, 0, 1, 0 };
      data[3] = Vector4Base<T>{ 0, 0, 0, 1 };
    }

    // Produces a scalar matrix, x * Identity
    inline explicit Matrix4Base(T x) {
      data[0] = Vector4Base<T>{ x, 0, 0, 0 };
      data[1] = Vector4Base<T>{ 0, x, 0, 0 };
      data[2] = Vector4Base<T>{ 0, 0, x, 0 };
      data[3] = Vector4Base<T>{ 0, 0, 0, x };
    }

    inline Matrix4Base(
      const Vector4Base<T>& v0,
      const Vector4Base<T>& v1,
      const Vector4Base<T>& v2,
      const Vector4Base<T>& v3) {
      data[0] = v0;
      data[1] = v1;
      data[2] = v2;
      data[3] = v3;
    }

    inline Matrix4Base(const T matrix[4][4]) {
      data[0] = Vector4Base<T>(matrix[0]);
      data[1] = Vector4Base<T>(matrix[1]);
      data[2] = Vector4Base<T>(matrix[2]);
      data[3] = Vector4Base<T>(matrix[3]);
    }

    inline Matrix4Base(const VkTransformMatrixKHR matrix) {
      data[0] = Vector4Base<T>(matrix.matrix[0]);
      data[1] = Vector4Base<T>(matrix.matrix[1]);
      data[2] = Vector4Base<T>(matrix.matrix[2]);
      data[3] = Vector4Base<T>(0, 0, 0, 1);
    }

    inline Matrix4Base(const Vector4Base<T> quaternion, const Vector3Base<T> translation) {
      data[0][0] = 1.0 - 2.0 * (quaternion.y * quaternion.y + quaternion.z * quaternion.z);
      data[0][1] = 2.0 * (quaternion.x * quaternion.y + quaternion.z * quaternion.w);
      data[0][2] = 2.0 * (quaternion.z * quaternion.x - quaternion.y * quaternion.w);

      data[1][0] = 2.0 * (quaternion.x * quaternion.y - quaternion.z * quaternion.w);
      data[1][1] = 1.0 - 2.0 * (quaternion.z * quaternion.z + quaternion.x * quaternion.x);
      data[1][2] = 2.0 * (quaternion.y * quaternion.z + quaternion.x * quaternion.w);

      data[2][0] = 2.0 * (quaternion.z * quaternion.x + quaternion.y * quaternion.w);
      data[2][1] = 2.0 * (quaternion.y * quaternion.z - quaternion.x * quaternion.w);
      data[2][2] = 1.0 - 2.0 * (quaternion.y * quaternion.y + quaternion.x * quaternion.x);

      data[3] = Vector4Base<T>(translation.x, translation.y, translation.z, 1.f);
    }

    explicit inline Matrix4Base(const Vector3Base<T> translation) {
      data[0] = Vector4Base<T>{ 1, 0, 0, 0 };
      data[1] = Vector4Base<T>{ 0, 1, 0, 0 };
      data[2] = Vector4Base<T>{ 0, 0, 1, 0 };
      data[3] = Vector4Base<T>(translation.x, translation.y, translation.z, 1.f);
    }

    inline Matrix4Base(const T m00, const T m01, const T m02, const T m03,
                   const T m10, const T m11, const T m12, const T m13,
                   const T m20, const T m21, const T m22, const T m23,
                   const T m30, const T m31, const T m32, const T m33) {
      data[0] = Vector4Base<T>(m00, m01, m02, m03);
      data[1] = Vector4Base<T>(m10, m11, m12, m13);
      data[2] = Vector4Base<T>(m20, m21, m22, m23);
      data[3] = Vector4Base<T>(m30, m31, m32, m33);
    }

    Matrix4Base(const Matrix4Base<T>& other) = default;

    template<typename TOther>
    Matrix4Base(const Matrix4Base<TOther>& other) {
      data[0] = Vector4Base<T>(other.data[0]);
      data[1] = Vector4Base<T>(other.data[1]);
      data[2] = Vector4Base<T>(other.data[2]);
      data[3] = Vector4Base<T>(other.data[3]);
    }

    Vector4Base<T>& operator[](size_t index) {
      return data[index];
    }

    const Vector4Base<T>& operator[](size_t index) const {
      return data[index];
    }

    bool operator==(const Matrix4Base<T>& m2) const {
      const auto& m1 = *this;
      for (uint32_t i = 0; i < 4; i++) {
        if (m1[i] != m2[i])
          return false;
      }
      return true;
    }

    bool operator!=(const Matrix4Base<T>& m2) const {
      return !operator==(m2);
    }

    Matrix4Base<T> operator+(const Matrix4Base<T>& other) const {
      Matrix4Base<T> mat;
      for (uint32_t i = 0; i < 4; i++)
        mat[i] = data[i] + other.data[i];
      return mat;
    }

    Matrix4Base<T> operator-(const Matrix4Base<T>& other) const {
      Matrix4Base<T> mat;
      for (uint32_t i = 0; i < 4; i++)
        mat[i] = data[i] - other.data[i];
      return mat;
    }

    Matrix4Base<T> operator*(const Matrix4Base<T>& m2) const {
      const auto& m1 = *this;

      const auto srcA0 = m1[0];
      const auto srcA1 = m1[1];
      const auto srcA2 = m1[2];
      const auto srcA3 = m1[3];

      const auto srcB0 = m2[0];
      const auto srcB1 = m2[1];
      const auto srcB2 = m2[2];
      const auto srcB3 = m2[3];

      Matrix4Base<T> result;
      result[0] = srcA0 * srcB0[0] + srcA1 * srcB0[1] + srcA2 * srcB0[2] + srcA3 * srcB0[3];
      result[1] = srcA0 * srcB1[0] + srcA1 * srcB1[1] + srcA2 * srcB1[2] + srcA3 * srcB1[3];
      result[2] = srcA0 * srcB2[0] + srcA1 * srcB2[1] + srcA2 * srcB2[2] + srcA3 * srcB2[3];
      result[3] = srcA0 * srcB3[0] + srcA1 * srcB3[1] + srcA2 * srcB3[2] + srcA3 * srcB3[3];
      return result;
    }

    Vector4Base<T> operator*(const Vector4Base<T>& v) const {
      const auto& m = *this;

      const auto mul0 = m[0] * v[0];
      const auto mul1 = m[1] * v[1];
      const auto mul2 = m[2] * v[2];
      const auto mul3 = m[3] * v[3];

      const auto add0 = mul0 + mul1;
      const auto add1 = mul2 + mul3;

      return add0 + add1;
    }

    Matrix4Base<T> operator*(T scalar) const {
      Matrix4Base<T> mat;
      for (uint32_t i = 0; i < 4; i++)
        mat[i] = data[i] * scalar;
      return mat;
    }

    Matrix4Base<T> operator/(T scalar) const {
      Matrix4Base<T> mat;
      for (uint32_t i = 0; i < 4; i++)
        mat[i] = data[i] / scalar;
      return mat;
    }

    Matrix4Base<T>& operator+=(const Matrix4Base<T>& other) {
      for (uint32_t i = 0; i < 4; i++)
        data[i] += other.data[i];
      return *this;
    }

    Matrix4Base<T>& operator-=(const Matrix4Base<T>& other) {
      for (uint32_t i = 0; i < 4; i++)
        data[i] -= other.data[i];
      return *this;
    }

    Matrix4Base<T>& operator*=(const Matrix4Base<T>& other) {
      return (*this = (*this) * other);
    }

    Vector4Base<T> data[4];

  };
  

  template<typename T>
  inline Matrix4Base<T> operator*(T scalar, const Matrix4Base<T>& m) { return m * scalar; }

  template<typename T>
  Matrix4Base<T> transpose(const Matrix4Base<T>& m) {
    Matrix4Base<T> result;

    for (uint32_t i = 0; i < 4; i++) {
      for (uint32_t j = 0; j < 4; j++)
        result[i][j] = m.data[j][i];
    }
    return result;
  }

  // Note: From GLM
  template<typename T>
  double determinant(const Matrix4Base<T>& m) {
    // Note: Double precision used here always for better precision.
    double coef00 = m[2][2] * m[3][3] - m[3][2] * m[2][3];
    double coef02 = m[1][2] * m[3][3] - m[3][2] * m[1][3];
    double coef03 = m[1][2] * m[2][3] - m[2][2] * m[1][3];
    double coef04 = m[2][1] * m[3][3] - m[3][1] * m[2][3];
    double coef06 = m[1][1] * m[3][3] - m[3][1] * m[1][3];
    double coef07 = m[1][1] * m[2][3] - m[2][1] * m[1][3];
    double coef08 = m[2][1] * m[3][2] - m[3][1] * m[2][2];
    double coef10 = m[1][1] * m[3][2] - m[3][1] * m[1][2];
    double coef11 = m[1][1] * m[2][2] - m[2][1] * m[1][2];
    double coef12 = m[2][0] * m[3][3] - m[3][0] * m[2][3];
    double coef14 = m[1][0] * m[3][3] - m[3][0] * m[1][3];
    double coef15 = m[1][0] * m[2][3] - m[2][0] * m[1][3];
    double coef16 = m[2][0] * m[3][2] - m[3][0] * m[2][2];
    double coef18 = m[1][0] * m[3][2] - m[3][0] * m[1][2];
    double coef19 = m[1][0] * m[2][2] - m[2][0] * m[1][2];
    double coef20 = m[2][0] * m[3][1] - m[3][0] * m[2][1];
    double coef22 = m[1][0] * m[3][1] - m[3][0] * m[1][1];
    double coef23 = m[1][0] * m[2][1] - m[2][0] * m[1][1];

    Vector4d fac0{ coef00, coef00, coef02, coef03 };
    Vector4d fac1{ coef04, coef04, coef06, coef07 };
    Vector4d fac2{ coef08, coef08, coef10, coef11 };
    Vector4d fac3{ coef12, coef12, coef14, coef15 };
    Vector4d fac4{ coef16, coef16, coef18, coef19 };
    Vector4d fac5{ coef20, coef20, coef22, coef23 };

    Vector4d vec0{ m[1][0], m[0][0], m[0][0], m[0][0] };
    Vector4d vec1{ m[1][1], m[0][1], m[0][1], m[0][1] };
    Vector4d vec2{ m[1][2], m[0][2], m[0][2], m[0][2] };
    Vector4d vec3{ m[1][3], m[0][3], m[0][3], m[0][3] };

    Vector4d inv0{ vec1 * fac0 - vec2 * fac1 + vec3 * fac2 };
    Vector4d inv1{ vec0 * fac0 - vec2 * fac3 + vec3 * fac4 };
    Vector4d inv2{ vec0 * fac1 - vec1 * fac3 + vec3 * fac5 };
    Vector4d inv3{ vec0 * fac2 - vec1 * fac4 + vec2 * fac5 };

    Vector4d signA{ +1, -1, +1, -1 };
    Vector4d signB{ -1, +1, -1, +1 };

    Vector4d inverse[4];
    inverse[0] = inv0 * signA;
    inverse[1] = inv1 * signB;
    inverse[2] = inv2 * signA;
    inverse[3] = inv3 * signB;

    Vector4d row0{ inverse[0][0], inverse[1][0], inverse[2][0], inverse[3][0] };

    Vector4d dot0{ Vector4d(m[0].x,m[0].y,m[0].z,m[0].w) * row0 };

    return (dot0.x + dot0.y) + (dot0.z + dot0.w);
  }

  template<typename T>
  inline Matrix4Base<T> inverseAffine(const Matrix4Base<T>& m) {
    // If uniform/non-uniform scale present, we still do a robust 3x3 inverse in double.
    double r00 = m[0][0], r01 = m[0][1], r02 = m[0][2];
    double r10 = m[1][0], r11 = m[1][1], r12 = m[1][2];
    double r20 = m[2][0], r21 = m[2][1], r22 = m[2][2];

    double det = r00 * (r11 * r22 - r12 * r21) - r01 * (r10 * r22 - r12 * r20) + r02 * (r10 * r21 - r11 * r20);
    // If det ~ 0, fall back to general inverse
    if (fabs(det) < 1e-24)  {
      return inverse(m);
    }

    double invDet = 1.0 / det;
    Matrix4Base<T> inv;

    // 3x3 inverse
    inv[0][0] = T((r11 * r22 - r12 * r21) * invDet);
    inv[0][1] = T((r02 * r21 - r01 * r22) * invDet);
    inv[0][2] = T((r01 * r12 - r02 * r11) * invDet);
    inv[1][0] = T((r12 * r20 - r10 * r22) * invDet);
    inv[1][1] = T((r00 * r22 - r02 * r20) * invDet);
    inv[1][2] = T((r02 * r10 - r00 * r12) * invDet);
    inv[2][0] = T((r10 * r21 - r11 * r20) * invDet);
    inv[2][1] = T((r01 * r20 - r00 * r21) * invDet);
    inv[2][2] = T((r00 * r11 - r01 * r10) * invDet);

    // translation
    T tx = m[3][0], ty = m[3][1], tz = m[3][2];
    inv[3][0] = -(inv[0][0] * tx + inv[1][0] * ty + inv[2][0] * tz);
    inv[3][1] = -(inv[0][1] * tx + inv[1][1] * ty + inv[2][1] * tz);
    inv[3][2] = -(inv[0][2] * tx + inv[1][2] * ty + inv[2][2] * tz);

    // last row/col
    inv[0][3] = inv[1][3] = inv[2][3] = 0.0f;
    inv[3][3] = 1.0f;
    return inv;
  }

  // Note: From GLM
  template<typename T>
  Matrix4Base<T> inverse(const Matrix4Base<T>& m) {
    // Note: Double precision used here always for better precision.
    double coef00 = (double) m[2][2] * m[3][3] - (double) m[3][2] * m[2][3];
    double coef02 = (double) m[1][2] * m[3][3] - (double) m[3][2] * m[1][3];
    double coef03 = (double) m[1][2] * m[2][3] - (double) m[2][2] * m[1][3];
    double coef04 = (double) m[2][1] * m[3][3] - (double) m[3][1] * m[2][3];
    double coef06 = (double) m[1][1] * m[3][3] - (double) m[3][1] * m[1][3];
    double coef07 = (double) m[1][1] * m[2][3] - (double) m[2][1] * m[1][3];
    double coef08 = (double) m[2][1] * m[3][2] - (double) m[3][1] * m[2][2];
    double coef10 = (double) m[1][1] * m[3][2] - (double) m[3][1] * m[1][2];
    double coef11 = (double) m[1][1] * m[2][2] - (double) m[2][1] * m[1][2];
    double coef12 = (double) m[2][0] * m[3][3] - (double) m[3][0] * m[2][3];
    double coef14 = (double) m[1][0] * m[3][3] - (double) m[3][0] * m[1][3];
    double coef15 = (double) m[1][0] * m[2][3] - (double) m[2][0] * m[1][3];
    double coef16 = (double) m[2][0] * m[3][2] - (double) m[3][0] * m[2][2];
    double coef18 = (double) m[1][0] * m[3][2] - (double) m[3][0] * m[1][2];
    double coef19 = (double) m[1][0] * m[2][2] - (double) m[2][0] * m[1][2];
    double coef20 = (double) m[2][0] * m[3][1] - (double) m[3][0] * m[2][1];
    double coef22 = (double) m[1][0] * m[3][1] - (double) m[3][0] * m[1][1];
    double coef23 = (double) m[1][0] * m[2][1] - (double) m[2][0] * m[1][1];

    Vector4d fac0{ coef00, coef00, coef02, coef03 };
    Vector4d fac1{ coef04, coef04, coef06, coef07 };
    Vector4d fac2{ coef08, coef08, coef10, coef11 };
    Vector4d fac3{ coef12, coef12, coef14, coef15 };
    Vector4d fac4{ coef16, coef16, coef18, coef19 };
    Vector4d fac5{ coef20, coef20, coef22, coef23 };

    Vector4d vec0{ m[1][0], m[0][0], m[0][0], m[0][0] };
    Vector4d vec1{ m[1][1], m[0][1], m[0][1], m[0][1] };
    Vector4d vec2{ m[1][2], m[0][2], m[0][2], m[0][2] };
    Vector4d vec3{ m[1][3], m[0][3], m[0][3], m[0][3] };

    Vector4d inv0{ vec1 * fac0 - vec2 * fac1 + vec3 * fac2 };
    Vector4d inv1{ vec0 * fac0 - vec2 * fac3 + vec3 * fac4 };
    Vector4d inv2{ vec0 * fac1 - vec1 * fac3 + vec3 * fac5 };
    Vector4d inv3{ vec0 * fac2 - vec1 * fac4 + vec2 * fac5 };

    Vector4d signA{ +1, -1, +1, -1 };
    Vector4d signB{ -1, +1, -1, +1 };

    Vector4d inverse[4];
    inverse[0] = inv0 * signA;
    inverse[1] = inv1 * signB;
    inverse[2] = inv2 * signA;
    inverse[3] = inv3 * signB;

    Vector4d row0{ inverse[0][0], inverse[1][0], inverse[2][0], inverse[3][0] };

    Vector4d dot0{ Vector4d(m[0].x,m[0].y,m[0].z,m[0].w) * row0 };
    double dot1 = (dot0.x + dot0.y) + (dot0.z + dot0.w);

    // Note: Ensure the matrix is invertable.
    mathValidationAssert(dot1 != 0.0, "Attempted invert a non-invertible matrix.");

    Matrix4Base<T> output;

    for (uint32_t i = 0; i < 16; i++) {
      output[i / 4][i % 4] = inverse[i / 4][i % 4] / dot1;
    }

    return output;
  }

  template<typename T>
  Matrix4Base<T> hadamardProduct(const Matrix4Base<T>& a, const Matrix4Base<T>& b) {
    Matrix4Base<T> result;

    for (uint32_t i = 0; i < 4; i++)
      result[i] = a[i] * b[i];

    return result;
  }

  template<typename T>
  Matrix4Base<T> translationMatrix(const Vector3Base<T>& v) {
    return Matrix4Base<T>{
      1.0, 0.0, 0.0, 0.0,
      0.0, 1.0, 0.0, 0.0,
      0.0, 0.0, 1.0, 0.0,
      v.x, v.y, v.z, 1.0
    };
  }

  template<typename T>
  std::ostream& operator<<(std::ostream& os, const Matrix4Base<T>& m) {
    os << "Matrix4(";
    for (uint32_t i = 0; i < 4; i++) {
      os << "\n\t" << m[i];
      if (i < 3)
        os << ", ";
    }
    os << "\n)";

    return os;
  }

  using Matrix4 = Matrix4Base<float>;
  using Matrix4d = Matrix4Base<double>;

  static_assert(sizeof(Matrix4) == sizeof(Vector4) * 4);
  static_assert(sizeof(Matrix4d) == sizeof(Vector4d) * 4);

  // NV-DXVK start
  // Fast check if Matrix4 is an exact identity matrix (specifically for floats only right now, no double
  // version implemented yet).
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

  template<typename T>
  static inline bool isMirrorTransform(const Matrix4Base<T>& m) {
    // Note: Identify if the winding is inverted by checking if the z axis is ever flipped relative to what it's expected to be for clockwise vertices in a lefthanded space
    // (x cross y) through the series of transformations
    Vector3d x(m[0].data), y(m[1].data), z(m[2].data);
    return dot(cross(x, y), z) < 0.0;
  }
  // NV-DXVK end

  class Matrix3 {

    public:

    // Identity
    inline Matrix3() {
      data[0] = Vector3{ 1, 0, 0 };
      data[1] = Vector3{ 0, 1, 0 };
      data[2] = Vector3{ 0, 0, 1 };
    }

    // Produces a scalar matrix, x * Identity
    inline explicit Matrix3(float x) {
      data[0] = Vector3{ x, 0, 0 };
      data[1] = Vector3{ 0, x, 0 };
      data[2] = Vector3{ 0, 0, x };
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

  std::ostream& operator<<(std::ostream& os, const Matrix3& m);

}