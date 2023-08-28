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