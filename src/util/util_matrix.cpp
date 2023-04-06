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
#include "util_matrix.h"

namespace dxvk {

        Vector4& Matrix4::operator[](size_t index)       { return data[index]; }
  const Vector4& Matrix4::operator[](size_t index) const { return data[index]; }

  bool Matrix4::operator==(const Matrix4& m2) const {
    const Matrix4& m1 = *this;
    for (uint32_t i = 0; i < 4; i++) {
      if (m1[i] != m2[i])
        return false;
    }
    return true;
  }

  bool Matrix4::operator!=(const Matrix4& m2) const { return !operator==(m2); }

  Matrix4 Matrix4::operator+(const Matrix4& other) const {
    Matrix4 mat;
    for (uint32_t i = 0; i < 4; i++)
      mat[i] = data[i] + other.data[i];
    return mat;
  }

  Matrix4 Matrix4::operator-(const Matrix4& other) const {
    Matrix4 mat;
    for (uint32_t i = 0; i < 4; i++)
      mat[i] = data[i] - other.data[i];
    return mat;
  }

  Matrix4 Matrix4::operator*(const Matrix4& m2) const {
    const Matrix4& m1 = *this;

    const Vector4 srcA0 = { m1[0] };
    const Vector4 srcA1 = { m1[1] };
    const Vector4 srcA2 = { m1[2] };
    const Vector4 srcA3 = { m1[3] };

    const Vector4 srcB0 = { m2[0] };
    const Vector4 srcB1 = { m2[1] };
    const Vector4 srcB2 = { m2[2] };
    const Vector4 srcB3 = { m2[3] };

    Matrix4 result;
    result[0] = srcA0 * srcB0[0] + srcA1 * srcB0[1] + srcA2 * srcB0[2] + srcA3 * srcB0[3];
    result[1] = srcA0 * srcB1[0] + srcA1 * srcB1[1] + srcA2 * srcB1[2] + srcA3 * srcB1[3];
    result[2] = srcA0 * srcB2[0] + srcA1 * srcB2[1] + srcA2 * srcB2[2] + srcA3 * srcB2[3];
    result[3] = srcA0 * srcB3[0] + srcA1 * srcB3[1] + srcA2 * srcB3[2] + srcA3 * srcB3[3];
    return result;
  }

  Vector4 Matrix4::operator*(const Vector4& v) const {
    const Matrix4& m = *this;

    const Vector4 mul0 = { m[0] * v[0] };
    const Vector4 mul1 = { m[1] * v[1] };
    const Vector4 mul2 = { m[2] * v[2] };
    const Vector4 mul3 = { m[3] * v[3] };

    const Vector4 add0 = { mul0 + mul1 };
    const Vector4 add1 = { mul2 + mul3 };

    return add0 + add1;
  }

  Matrix4 Matrix4::operator*(float scalar) const {
    Matrix4 mat;
    for (uint32_t i = 0; i < 4; i++)
      mat[i] = data[i] * scalar;
    return mat;
  }

  Matrix4 Matrix4::operator/(float scalar) const {
    Matrix4 mat;
    for (uint32_t i = 0; i < 4; i++)
      mat[i] = data[i] / scalar;
    return mat;
  }

  Matrix4& Matrix4::operator+=(const Matrix4& other) {
    for (uint32_t i = 0; i < 4; i++)
      data[i] += other.data[i];
    return *this;
  }

  Matrix4& Matrix4::operator-=(const Matrix4& other) {
    for (uint32_t i = 0; i < 4; i++)
      data[i] -= other.data[i];
    return *this;
  }

  Matrix4& Matrix4::operator*=(const Matrix4& other) {
    return (*this = (*this) * other);
  }

  Matrix4 transpose(const Matrix4& m) {
    Matrix4 result;

    for (uint32_t i = 0; i < 4; i++) {
      for (uint32_t j = 0; j < 4; j++)
        result[i][j] = m.data[j][i];
    }
    return result;
  }

  // Note: From GLM
  double determinant(const Matrix4& m) {
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

    Vector4d fac0 = { coef00, coef00, coef02, coef03 };
    Vector4d fac1 = { coef04, coef04, coef06, coef07 };
    Vector4d fac2 = { coef08, coef08, coef10, coef11 };
    Vector4d fac3 = { coef12, coef12, coef14, coef15 };
    Vector4d fac4 = { coef16, coef16, coef18, coef19 };
    Vector4d fac5 = { coef20, coef20, coef22, coef23 };

    Vector4d vec0 = { m[1][0], m[0][0], m[0][0], m[0][0] };
    Vector4d vec1 = { m[1][1], m[0][1], m[0][1], m[0][1] };
    Vector4d vec2 = { m[1][2], m[0][2], m[0][2], m[0][2] };
    Vector4d vec3 = { m[1][3], m[0][3], m[0][3], m[0][3] };

    Vector4d inv0 = { vec1 * fac0 - vec2 * fac1 + vec3 * fac2 };
    Vector4d inv1 = { vec0 * fac0 - vec2 * fac3 + vec3 * fac4 };
    Vector4d inv2 = { vec0 * fac1 - vec1 * fac3 + vec3 * fac5 };
    Vector4d inv3 = { vec0 * fac2 - vec1 * fac4 + vec2 * fac5 };

    Vector4d signA = { +1, -1, +1, -1 };
    Vector4d signB = { -1, +1, -1, +1 };

    Vector4d inverse[4];
    inverse[0] = inv0 * signA;
    inverse[1] = inv1 * signB;
    inverse[2] = inv2 * signA;
    inverse[3] = inv3 * signB;

    Vector4d row0 = { inverse[0][0], inverse[1][0], inverse[2][0], inverse[3][0] };

    Vector4d dot0 = { Vector4d(m[0].x,m[0].y,m[0].z,m[0].w) * row0 };

    return (dot0.x + dot0.y) + (dot0.z + dot0.w);
  }

  // Note: From GLM
  Matrix4 inverse(const Matrix4& m) {
    double coef00 = (double)m[2][2] * m[3][3] - (double)m[3][2] * m[2][3];
    double coef02 = (double)m[1][2] * m[3][3] - (double)m[3][2] * m[1][3];
    double coef03 = (double)m[1][2] * m[2][3] - (double)m[2][2] * m[1][3];
    double coef04 = (double)m[2][1] * m[3][3] - (double)m[3][1] * m[2][3];
    double coef06 = (double)m[1][1] * m[3][3] - (double)m[3][1] * m[1][3];
    double coef07 = (double)m[1][1] * m[2][3] - (double)m[2][1] * m[1][3];
    double coef08 = (double)m[2][1] * m[3][2] - (double)m[3][1] * m[2][2];
    double coef10 = (double)m[1][1] * m[3][2] - (double)m[3][1] * m[1][2];
    double coef11 = (double)m[1][1] * m[2][2] - (double)m[2][1] * m[1][2];
    double coef12 = (double)m[2][0] * m[3][3] - (double)m[3][0] * m[2][3];
    double coef14 = (double)m[1][0] * m[3][3] - (double)m[3][0] * m[1][3];
    double coef15 = (double)m[1][0] * m[2][3] - (double)m[2][0] * m[1][3];
    double coef16 = (double)m[2][0] * m[3][2] - (double)m[3][0] * m[2][2];
    double coef18 = (double)m[1][0] * m[3][2] - (double)m[3][0] * m[1][2];
    double coef19 = (double)m[1][0] * m[2][2] - (double)m[2][0] * m[1][2];
    double coef20 = (double)m[2][0] * m[3][1] - (double)m[3][0] * m[2][1];
    double coef22 = (double)m[1][0] * m[3][1] - (double)m[3][0] * m[1][1];
    double coef23 = (double)m[1][0] * m[2][1] - (double)m[2][0] * m[1][1];
  
    Vector4d fac0 = { coef00, coef00, coef02, coef03 };
    Vector4d fac1 = { coef04, coef04, coef06, coef07 };
    Vector4d fac2 = { coef08, coef08, coef10, coef11 };
    Vector4d fac3 = { coef12, coef12, coef14, coef15 };
    Vector4d fac4 = { coef16, coef16, coef18, coef19 };
    Vector4d fac5 = { coef20, coef20, coef22, coef23 };
  
    Vector4d vec0 = { m[1][0], m[0][0], m[0][0], m[0][0] };
    Vector4d vec1 = { m[1][1], m[0][1], m[0][1], m[0][1] };
    Vector4d vec2 = { m[1][2], m[0][2], m[0][2], m[0][2] };
    Vector4d vec3 = { m[1][3], m[0][3], m[0][3], m[0][3] };
  
    Vector4d inv0 = { vec1 * fac0 - vec2 * fac1 + vec3 * fac2 };
    Vector4d inv1 = { vec0 * fac0 - vec2 * fac3 + vec3 * fac4 };
    Vector4d inv2 = { vec0 * fac1 - vec1 * fac3 + vec3 * fac5 };
    Vector4d inv3 = { vec0 * fac2 - vec1 * fac4 + vec2 * fac5 };
  
    Vector4d signA = { +1, -1, +1, -1 };
    Vector4d signB = { -1, +1, -1, +1 };

    Vector4d inverse[4];
    inverse[0] = inv0 * signA;
    inverse[1] = inv1 * signB;
    inverse[2] = inv2 * signA;
    inverse[3] = inv3 * signB;

    Vector4d row0    = { inverse[0][0], inverse[1][0], inverse[2][0], inverse[3][0] };

    Vector4d dot0    = { Vector4d(m[0].x,m[0].y,m[0].z,m[0].w) * row0 };
    double dot1      = (dot0.x + dot0.y) + (dot0.z + dot0.w);

    Matrix4 output;
    for (uint32_t i = 0; i < 16; i++)
      output[i / 4][i % 4] = (float)(inverse[i/4][i%4] / dot1);
    return output;
  }

  Matrix4 hadamardProduct(const Matrix4& a, const Matrix4& b) {
    Matrix4 result;

    for (uint32_t i = 0; i < 4; i++)
      result[i] = a[i] * b[i];

    return result;
  }

  Matrix4 translationMatrix(const Vector3& v) {
    return Matrix4 {
         1.0f, 0.0f, 0.0f, 0.0f,
         0.0f, 1.0f, 0.0f, 0.0f,
         0.0f, 0.0f, 1.0f, 0.0f,
         v.x,  v.y,  v.z,  1.0f
    };
  }

  std::ostream& operator<<(std::ostream& os, const Matrix4& m) {
    os << "Matrix4(";
    for (uint32_t i = 0; i < 4; i++) {
      os << "\n\t" << m[i];
      if (i < 3)
        os << ", ";
    }
    os << "\n)";

    return os;
  }

  Vector3& Matrix3::operator[](size_t index) { return data[index]; }
  const Vector3& Matrix3::operator[](size_t index) const { return data[index]; }

  bool Matrix3::operator==(const Matrix3& m2) const {
    const Matrix3& m1 = *this;
    for (uint32_t i = 0; i < 3; i++) {
      if (m1[i] != m2[i])
        return false;
    }
    return true;
  }

  bool Matrix3::operator!=(const Matrix3& m2) const { return !operator==(m2); }

  Matrix3 Matrix3::operator+(const Matrix3& other) const {
    Matrix3 mat;
    for (uint32_t i = 0; i < 3; i++)
      mat[i] = data[i] + other.data[i];
    return mat;
  }

  Matrix3 Matrix3::operator-(const Matrix3& other) const {
    Matrix3 mat;
    for (uint32_t i = 0; i < 3; i++)
      mat[i] = data[i] - other.data[i];
    return mat;
  }

  Matrix3 Matrix3::operator*(const Matrix3& m2) const {
    const Matrix3& m1 = *this;

    const Vector3 srcA0 = { m1[0] };
    const Vector3 srcA1 = { m1[1] };
    const Vector3 srcA2 = { m1[2] };

    const Vector3 srcB0 = { m2[0] };
    const Vector3 srcB1 = { m2[1] };
    const Vector3 srcB2 = { m2[2] };

    Matrix3 result;
    result[0] = srcA0 * srcB0[0] + srcA1 * srcB0[1] + srcA2 * srcB0[2];
    result[1] = srcA0 * srcB1[0] + srcA1 * srcB1[1] + srcA2 * srcB1[2];
    result[2] = srcA0 * srcB2[0] + srcA1 * srcB2[1] + srcA2 * srcB2[2];
    return result;
  }

  Vector3 Matrix3::operator*(const Vector3& v) const {
    const Matrix3& m = *this;

    const Vector3 mul0 = { m[0] * v[0] };
    const Vector3 mul1 = { m[1] * v[1] };
    const Vector3 mul2 = { m[2] * v[2] };

    return mul0 + mul1 + mul2;
  }

  Matrix3 Matrix3::operator*(float scalar) const {
    Matrix3 mat;
    for (uint32_t i = 0; i < 3; i++)
      mat[i] = data[i] * scalar;
    return mat;
  }

  Matrix3 Matrix3::operator/(float scalar) const {
    Matrix3 mat;
    for (uint32_t i = 0; i < 3; i++)
      mat[i] = data[i] / scalar;
    return mat;
  }

  Matrix3& Matrix3::operator+=(const Matrix3& other) {
    for (uint32_t i = 0; i < 3; i++)
      data[i] += other.data[i];
    return *this;
  }

  Matrix3& Matrix3::operator-=(const Matrix3& other) {
    for (uint32_t i = 0; i < 3; i++)
      data[i] -= other.data[i];
    return *this;
  }

  Matrix3& Matrix3::operator*=(const Matrix3& other) {
    return (*this = (*this) * other);
  }

  Matrix3 transpose(const Matrix3& m) {
    Matrix3 result;

    for (uint32_t i = 0; i < 3; i++) {
      for (uint32_t j = 0; j < 3; j++)
        result[i][j] = m.data[j][i];
    }
    return result;
  }

  // Note: From GLM
  Matrix3 inverse(const Matrix3& m) {
    const double oneOverDeterminant = 1.0 / (
      + (double)m[0][0] * ((double)m[1][1] * m[2][2] - (double)m[2][1] * m[1][2])
      - (double)m[1][0] * ((double)m[0][1] * m[2][2] - (double)m[2][1] * m[0][2])
      + (double)m[2][0] * ((double)m[0][1] * m[1][2] - (double)m[1][1] * m[0][2]));

    Matrix3 inverse;

    inverse[0][0] = (float)(+((double)m[1][1] * m[2][2] - (double)m[2][1] * m[1][2]) * oneOverDeterminant);
    inverse[1][0] = (float)(-((double)m[1][0] * m[2][2] - (double)m[2][0] * m[1][2]) * oneOverDeterminant);
    inverse[2][0] = (float)(+((double)m[1][0] * m[2][1] - (double)m[2][0] * m[1][1]) * oneOverDeterminant);
    inverse[0][1] = (float)(-((double)m[0][1] * m[2][2] - (double)m[2][1] * m[0][2]) * oneOverDeterminant);
    inverse[1][1] = (float)(+((double)m[0][0] * m[2][2] - (double)m[2][0] * m[0][2]) * oneOverDeterminant);
    inverse[2][1] = (float)(-((double)m[0][0] * m[2][1] - (double)m[2][0] * m[0][1]) * oneOverDeterminant);
    inverse[0][2] = (float)(+((double)m[0][1] * m[1][2] - (double)m[1][1] * m[0][2]) * oneOverDeterminant);
    inverse[1][2] = (float)(-((double)m[0][0] * m[1][2] - (double)m[1][0] * m[0][2]) * oneOverDeterminant);
    inverse[2][2] = (float)(+((double)m[0][0] * m[1][1] - (double)m[1][0] * m[0][1]) * oneOverDeterminant);

    return inverse;
  }

  std::ostream& operator<<(std::ostream& os, const Matrix3& m) {
    os << "Matrix3(";
    for (uint32_t i = 0; i < 3; i++) {
      os << "\n\t" << m[i];
      if (i < 2)
        os << ", ";
    }
    os << "\n)";

    return os;
  }

}