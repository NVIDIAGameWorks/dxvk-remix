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

#include <cmath>
#include <iostream>
#include <algorithm>
#include <type_traits>

#include "xxHash/xxhash.h"
#include "util_bit.h"
#include "util_math.h"

namespace dxvk {
  template <typename T>
  struct Vector2Base;

  template <typename T>
  struct Vector3Base;

  // Vector 4

  template <typename T>
  struct Vector4Base {
    constexpr Vector4Base()
      : x{ }, y{ }, z{ }, w{ } { }

    constexpr explicit Vector4Base(T splat)
      : x(splat), y(splat), z(splat), w(splat) { }

    constexpr explicit Vector4Base(T x, T y, T z, T w)
      : x(x), y(y), z(z), w(w) { }

    constexpr explicit Vector4Base(const T xyzw[4])
      : x(xyzw[0]), y(xyzw[1]), z(xyzw[2]), w(xyzw[3]) { }

    Vector4Base(const Vector4Base<T>& other) = default;

    template<typename TOther>
    explicit Vector4Base(const Vector4Base<TOther>& other) :
      x(static_cast<T>(other.x)),
      y(static_cast<T>(other.y)),
      z(static_cast<T>(other.z)),
      w(static_cast<T>(other.w)) { }

    explicit Vector4Base(const Vector3Base<T>& other, T w);
    
    inline       T& operator[](size_t index)       { return data[index]; }
    inline const T& operator[](size_t index) const { return data[index]; }

    bool operator==(const Vector4Base<T>& other) const {
      for (uint32_t i = 0; i < 4; i++) {
        if (data[i] != other.data[i])
        return false;
      }

      return true;
    }

    bool operator!=(const Vector4Base<T>& other) const {
      return !operator==(other);
    }

    bool operator<(const Vector4Base<T>& other) const {
      for (uint32_t i = 0; i < 4; i++) {
        if (data[i] >= other.data[i])
        return false;
      }

      return true;
    }

    bool operator<=(const Vector4Base<T>& other) const {
      for (uint32_t i = 0; i < 4; i++) {
        if (data[i] > other.data[i])
        return false;
      }

      return true;
    }

    bool operator>(const Vector4Base<T>& other) const {
      return other < *this;
    }

    bool operator>=(const Vector4Base<T>& other) const {
      return other <= *this;
    }

    Vector4Base operator-() const { return {-x, -y, -z, -w}; }

    Vector4Base operator+(const Vector4Base<T>& other) const {
      return Vector4Base{x + other.x, y + other.y, z + other.z, w + other.w};
    }

    Vector4Base operator-(const Vector4Base<T>& other) const {
      return Vector4Base{x - other.x, y - other.y, z - other.z, w - other.w};
    }

    Vector4Base operator*(T scalar) const {
      return Vector4Base{scalar * x, scalar * y, scalar * z, scalar * w};
    }

    Vector4Base operator*(const Vector4Base<T>& other) const {
      Vector4Base result;
      for (uint32_t i = 0; i < 4; i++)
        result[i] = data[i] * other.data[i];
      return result;
    }

    Vector4Base operator/(const Vector4Base<T>& other) const {
      Vector4Base result;
      for (uint32_t i = 0; i < 4; i++)
        result[i] = data[i] / other.data[i];
      return result;
    }

    Vector4Base operator/(T scalar) const {
      return Vector4Base{x / scalar, y / scalar, z / scalar, w / scalar};
    }

    Vector4Base& operator+=(const Vector4Base<T>& other) {
      x += other.x;
      y += other.y;
      z += other.z;
      w += other.w;

      return *this;
    }

    Vector4Base& operator-=(const Vector4Base<T>& other) {
      x -= other.x;
      y -= other.y;
      z -= other.z;
      w -= other.w;

      return *this;
    }

    Vector4Base& operator*=(T scalar) {
      x *= scalar;
      y *= scalar;
      z *= scalar;
      w *= scalar;

      return *this;
    }

    Vector4Base& operator/=(T scalar) {
      x /= scalar;
      y /= scalar;
      z /= scalar;
      w /= scalar;

      return *this;
    }

    Vector3Base<T>& xyz();
    const Vector3Base<T>& xyz() const;
    Vector2Base<T>& xy();
    const Vector2Base<T>& xy() const;

    union {
      T data[4];
      struct {
        T x, y, z, w;
      };
      struct {
        T r, g, b, a;
      };
    };

  };

  template <typename T>
  inline Vector4Base<T> operator*(T scalar, const Vector4Base<T>& vector) {
    return vector * scalar;
  }

  template <typename T>
  T dot(const Vector4Base<T>& a, const Vector4Base<T>& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
  }

  template <typename T>
  Vector4Base<T> clamp(const Vector4Base<T>& a, const Vector4Base<T>& lo, const Vector4Base<T>& hi) {
    Vector4Base<T> out;

    out.x = std::clamp(a.x, lo.x, hi.x);
    out.y = std::clamp(a.y, lo.y, hi.y);
    out.z = std::clamp(a.z, lo.z, hi.z);
    out.w = std::clamp(a.w, lo.w, hi.w);

    return out;
  }

  template <typename T>
  Vector4Base<T> abs(const Vector4Base<T>& a) {
    Vector4Base<T> out;

    out.x = std::abs(a.x);
    out.y = std::abs(a.y);
    out.z = std::abs(a.z);
    out.w = std::abs(a.w);

    return out;
  }

  template <typename T>
  std::ostream& operator<<(std::ostream& os, const Vector4Base<T>& v) {
    return os << "Vector4(" << v[0] << ", " << v[1] << ", " << v[2] << ", " << v[3] << ")";
  }

  using Vector4d = Vector4Base<double>;
  using Vector4  = Vector4Base<float>;
  using Vector4i = Vector4Base<int>;

  static_assert(sizeof(Vector4)  == sizeof(float) * 4);
  static_assert(sizeof(Vector4i) == sizeof(int)   * 4);

  // Replaces NaNs in the vector with zero.
  inline Vector4 replaceNaN(Vector4 a) {
    Vector4 result;
    __m128 value = _mm_load_ps(a.data);
    __m128 mask  = _mm_cmpeq_ps(value, value);
           value = _mm_and_ps(value, mask);
    _mm_store_ps(result.data, value);
    return result;
  }

  // Gets a mask where NaN/Inf values are represented with 0 and other values are represented with filled bits.
  inline __m128 nanInfMask(__m128 value) {
    __m128 infValue = _mm_set1_ps(std::numeric_limits<float>::infinity());
    // Note: Generates a mask with filled bits for non-NaN values and 0 for NaN values
    __m128 nanMask = _mm_cmpeq_ps(value, value);
    // Note: Generates a mask with filled bits for non-infinity values and 0 for infinity values
    __m128 infMask = _mm_cmpneq_ps(value, infValue);

    // Note: Combines the two masks to get the desired result (must be NaN-free and Inf-free to be a valid value).
    return _mm_and_ps(nanMask, infMask);
  }

  // Replaces NaNs or Infs in the vector with zero.
  inline Vector4 replaceNaNInf(Vector4 a) {
    Vector4 result;

    __m128 value = _mm_load_ps(a.data);
    __m128 combinedMask = nanInfMask(value);

    // Note: Zero out the values corresponding to the zero bits in the NaN/Inf mask.
     value = _mm_and_ps(value, combinedMask);

    _mm_store_ps(result.data, value);

    return result;
  }

  // Returns true if the vector has any NaNs/Infs, false otherwise.
  inline bool hasNaNInf(Vector4 a) {
    __m128 value = _mm_load_ps(a.data);
    __m128 combinedMask = nanInfMask(value);

    // Note: Return true if any of the elements in the mask are zero (indicating NaN/Inf).
    return _mm_movemask_ps(combinedMask) != 0xF;
  }

  // Vector 3

  template <typename T>
  struct Vector3Base {
    constexpr Vector3Base()
      : x{ }, y{ }, z{ } { }

    constexpr explicit Vector3Base(T splat)
      : x(splat), y(splat), z(splat) { }

    constexpr explicit Vector3Base(T x, T y, T z)
      : x(x), y(y), z(z) { }

    constexpr explicit Vector3Base(const T xyz[3])
      : x(xyz[0]), y(xyz[1]), z(xyz[2]) { }

    Vector3Base& operator=(Vector3Base&&) = default;
    Vector3Base& operator=(const Vector3Base&) = default;

    Vector3Base(const Vector3Base<T>& other) = default;

    template<typename TOther>
    explicit Vector3Base(const Vector3Base<TOther>& other) :
      x(static_cast<T>(other.x)),
      y(static_cast<T>(other.y)),
      z(static_cast<T>(other.z)) { }

    explicit Vector3Base(const Vector2Base<T>&other, T z);

    inline       T& operator[](size_t index) { return data[index]; }
    inline const T& operator[](size_t index) const { return data[index]; }

    bool operator==(const Vector3Base<T>& other) const {
      for (uint32_t i = 0; i < 3; i++) {
        if (data[i] != other.data[i])
          return false;
      }

      return true;
    }

    bool operator!=(const Vector3Base<T>& other) const {
      return !operator==(other);
    }

    bool operator<(const Vector3Base<T>& other) const {
      for (uint32_t i = 0; i < 3; i++) {
        if (data[i] >= other.data[i])
          return false;
      }

      return true;
    }

    bool operator<=(const Vector3Base<T>& other) const {
      for (uint32_t i = 0; i < 3; i++) {
        if (data[i] > other.data[i])
          return false;
      }

      return true;
    }

    Vector3Base operator-() const { return Vector3Base{ -x, -y, -z }; }

    Vector3Base operator+(const Vector3Base<T>& other) const {
      return Vector3Base{ x + other.x, y + other.y, z + other.z };
    }

    Vector3Base operator-(const Vector3Base<T>& other) const {
      return Vector3Base{ x - other.x, y - other.y, z - other.z };
    }

    Vector3Base operator*(T scalar) const {
      return Vector3Base{ scalar * x, scalar * y, scalar * z };
    }

    Vector3Base operator*(const Vector3Base<T>& other) const {
      Vector3Base result;
      for (uint32_t i = 0; i < 3; i++)
        result[i] = data[i] * other.data[i];
      return result;
    }

    Vector3Base operator/(const Vector3Base<T>& other) const {
      Vector3Base result;
      for (uint32_t i = 0; i < 3; i++)
        result[i] = data[i] / other.data[i];
      return result;
    }

    Vector3Base operator/(T scalar) const {
      return Vector3Base{ x / scalar, y / scalar, z / scalar };
    }

    Vector3Base& operator+=(const Vector3Base<T>& other) {
      x += other.x;
      y += other.y;
      z += other.z;

      return *this;
    }

    Vector3Base& operator-=(const Vector3Base<T>& other) {
      x -= other.x;
      y -= other.y;
      z -= other.z;

      return *this;
    }

    Vector3Base& operator*=(T scalar) {
      x *= scalar;
      y *= scalar;
      z *= scalar;

      return *this;
    }

    Vector3Base& operator/=(T scalar) {
      x /= scalar;
      y /= scalar;
      z /= scalar;

      return *this;
    }

    Vector2Base<T>& xy();
    const Vector2Base<T>& xy() const;

    union {
      T data[3];
      struct {
        T x, y, z;
      };
      struct {
        T r, g, b;
      };
    };

  };

  template <typename T>
  inline Vector3Base<T> operator*(T scalar, const Vector3Base<T>& vector) {
    return vector * scalar;
  }

  template <typename T>
  T dot(const Vector3Base<T>& a, const Vector3Base<T>& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
  }

  template <typename T>
  Vector3Base<T> project(const Vector3Base<T>& p, const Vector3Base<T>& o, const Vector3Base<T>& n) {
    return p - dot(p - o, n) * n;
  }

  template <typename T>
  // Wrapping the function name in () to fix linter errors from min/max macros in windows headers.
  Vector3Base<T> (min)(const Vector3Base<T>& a, const Vector3Base<T>& b) {
    return Vector3Base<T>(
      std::min(a.x, b.x),
      std::min(a.y, b.y),
      std::min(a.z, b.z)
    );
  }

  template <typename T>
  // Wrapping the function name in () to fix linter errors from min/max macros in windows headers.
  Vector3Base<T> (max)(const Vector3Base<T>& a, const Vector3Base<T>& b) {
    return Vector3Base<T>(
      std::max(a.x, b.x),
      std::max(a.y, b.y),
      std::max(a.z, b.z)
    );
  }

  template <typename T>
  Vector3Base<T> cross(const Vector3Base<T>& a, const Vector3Base<T>& b) {
    return Vector3Base<T>(
      a.y * b.z - b.y * a.z,
      a.z * b.x - b.z * a.x,
      a.x * b.y - b.x * a.y);
  }

  template <typename T>
  Vector3Base<T> clamp(const Vector3Base<T>& a, const Vector3Base<T>& lo, const Vector3Base<T>& hi) {
    Vector3Base<T> out;

    out.x = std::clamp(a.x, lo.x, hi.x);
    out.y = std::clamp(a.y, lo.y, hi.y);
    out.z = std::clamp(a.z, lo.z, hi.z);

    return out;
  }

  template <typename T>
  Vector3Base<T> abs(const Vector3Base<T>& a) {
    Vector3Base<T> out;

    out.x = std::abs(a.x);
    out.y = std::abs(a.y);
    out.z = std::abs(a.z);

    return out;
  }

  template <typename T>
  std::ostream& operator<<(std::ostream& os, const Vector3Base<T>& v) {
    return os << "Vector3(" << v[0] << ", " << v[1] << ", " << v[2] << ")";
  }

  using Vector3  = Vector3Base<float>;
  using Vector3d = Vector3Base<double>;
  using Vector3i = Vector3Base<int>;

  static_assert(sizeof(Vector3)  == sizeof(float) * 3);
  static_assert(sizeof(Vector3i) == sizeof(int)   * 3);

  // Vector 2

  template <typename T>
  struct Vector2Base {
    constexpr Vector2Base()
      : x{ }, y{ } { }

    constexpr explicit Vector2Base(T splat)
      : x(splat), y(splat) { }

    constexpr explicit Vector2Base(T x, T y)
      : x(x), y(y) { }

    constexpr explicit Vector2Base(const T xy[2])
      : x(xy[0]), y(xy[1]) { }

    Vector2Base(const Vector2Base<T>& other) = default;

    inline       T& operator[](size_t index) { return data[index]; }
    inline const T& operator[](size_t index) const { return data[index]; }

    bool operator==(const Vector2Base<T>& other) const {
      for (uint32_t i = 0; i < 2; i++) {
        if (data[i] != other.data[i])
          return false;
      }

      return true;
    }

    bool operator!=(const Vector2Base<T>& other) const {
      return !operator==(other);
    }

    bool operator<(const Vector2Base<T>& other) const {
      for (uint32_t i = 0; i < 2; i++) {
        if (data[i] >= other.data[i])
          return false;
      }

      return true;
    }

    bool operator<=(const Vector2Base<T>& other) const {
      for (uint32_t i = 0; i < 2; i++) {
        if (data[i] > other.data[i])
          return false;
      }

      return true;
    }

    Vector2Base operator-() const { return { -x, -y }; }

    Vector2Base operator+(const Vector2Base<T>& other) const {
      return Vector2Base{ x + other.x, y + other.y };
    }

    Vector2Base operator-(const Vector2Base<T>& other) const {
      return Vector2Base{ x - other.x, y - other.y };
    }

    Vector2Base operator*(T scalar) const {
      return Vector2Base{ scalar * x, scalar * y };
    }

    Vector2Base operator*(const Vector2Base<T>& other) const {
      Vector2Base result;
      for (uint32_t i = 0; i < 2; i++)
        result[i] = data[i] * other.data[i];
      return result;
    }

    Vector2Base operator/(const Vector2Base<T>& other) const {
      Vector2Base result;
      for (uint32_t i = 0; i < 2; i++)
        result[i] = data[i] / other.data[i];
      return result;
    }

    Vector2Base operator/(T scalar) const {
      return { x / scalar, y / scalar };
    }

    Vector2Base& operator+=(const Vector2Base<T>& other) {
      x += other.x;
      y += other.y;

      return *this;
    }

    Vector2Base& operator-=(const Vector2Base<T>& other) {
      x -= other.x;
      y -= other.y;

      return *this;
    }

    Vector2Base& operator*=(T scalar) {
      x *= scalar;
      y *= scalar;

      return *this;
    }

    Vector2Base& operator/=(T scalar) {
      x /= scalar;
      y /= scalar;

      return *this;
    }

    union {
      T data[2];
      struct {
        T x, y;
      };
      struct {
        T r, g;
      };
    };

  };

  template <typename T>
  inline Vector2Base<T> operator*(T scalar, const Vector2Base<T>& vector) {
    return vector * scalar;
  }

  template <typename T>
  T dot(const Vector2Base<T>& a, const Vector2Base<T>& b) {
    return a.x * b.x + a.y * b.y;
  }

  template <typename T>
  // Wrapping the function name in () to fix linter errors from min/max macros in windows headers.
  Vector2Base<T> (min)(const Vector2Base<T>& a, const Vector2Base<T>& b) {
    return Vector2Base<T>(
      std::min(a.x, b.x),
      std::min(a.y, b.y)
    );
  }

  template <typename T>
  // Wrapping the function name in () to fix linter errors from min/max macros in windows headers.
  Vector2Base<T> (max)(const Vector2Base<T>& a, const Vector2Base<T>& b) {
    return Vector2Base<T>(
      std::max(a.x, b.x),
      std::max(a.y, b.y)
    );
  }

  template <typename T>
  Vector2Base<T> doFloor(const Vector2Base<T>& a) {
    return Vector2Base<T>(
      floor(a.x),
      floor(a.y)
    );
  }

  template <typename T>
  std::ostream& operator<<(std::ostream& os, const Vector2Base<T>& v) {
    return os << "Vector2(" << v[0] << ", " << v[1] << ")";
  }

  using Vector2  = Vector2Base<float>;
  using Vector2i = Vector2Base<int>;

  static_assert(sizeof(Vector2)  == sizeof(float) * 2);
  static_assert(sizeof(Vector2i) == sizeof(int)   * 2);

  // Vector-generic Functions

  template <template<typename> typename TVector, typename T>
  T lengthSqr(const TVector<T>& a) {
    return dot(a, a);
  }

  template <template<typename> typename TVector, typename T>
  std::enable_if_t<std::is_floating_point_v<T>, T> length(const TVector<T>& a) {
    return std::sqrt(lengthSqr(a));
  }

  template <template<typename> typename TVector, typename T>
  std::enable_if_t<std::is_floating_point_v<T>, TVector<T>> normalizeGetLength(const TVector<T>& a, T& aLength) {
    aLength = length(a);

    // Note: Ensure the vector can be normalized (non-zero length).
    mathValidationAssert(aLength != static_cast<T>(0.0), "Attempted to normalize a zero-length vector.");

    return a * (static_cast<T>(1.0) / aLength);
  }

  template <template<typename> typename TVector, typename T>
  std::enable_if_t<std::is_floating_point_v<T>, TVector<T>> normalize(const TVector<T>& a) {
    T dummyLength;

    return normalizeGetLength(a, dummyLength);
  }

  // Sanitizes away the singularity case in some vector calculations when the vector is 0, 0, 0 as this often poses issues for normalization
  // calculations for instance due to having a length of 0. Do note this behavior is already encapsulated in safeNormalize, this function is
  // only for use when a vector is already expected to be normalized (e.g. from an external source) but still needs to be sanitized.
  template <template<typename> typename TVector, typename T>
  std::enable_if_t<std::is_floating_point_v<T>, TVector<T>> sanitizeSingularity(const TVector<T>& a, const TVector<T>& fallback) {
    // Note: This splats a new vector of the proper size with all 0s to compare to.
    if (a == TVector<T>(0.0)) {
      return fallback;
    }

    return a;
  }

  template <template<typename> typename TVector, typename T>
  std::enable_if_t<std::is_floating_point_v<T>, TVector<T>> safeNormalizeGetLength(const TVector<T>& a, const TVector<T>& fallback, T& aLength) {
    // Note: The fallback vector is expected to be pretty much exactly normalized by the intent of this function.
    assert(isApproxNormalized(fallback, static_cast<T>(0.0001)));

    aLength = length(a);

    if (aLength == static_cast<T>(0.0)) {
      // Note: Length should be 1 for a normalized fallback vector.
      aLength = static_cast<T>(1.0);

      return fallback;
    }

    return a * (static_cast<T>(1.0) / aLength);
  }

  template <template<typename> typename TVector, typename T>
  std::enable_if_t<std::is_floating_point_v<T>, TVector<T>> safeNormalize(const TVector<T>& a, const TVector<T>& fallback) {
    T dummyLength;

    return safeNormalizeGetLength(a, fallback, dummyLength);
  }

  template <template<typename> typename TVector, typename T>
  std::enable_if_t<std::is_floating_point_v<T>, bool> isApproxNormalized(const TVector<T>& a, const T& threshold) {
    const auto aLength = length(a);

    return
      (aLength >= static_cast<T>(1.0) - threshold) &&
      (aLength <= static_cast<T>(1.0) + threshold);
  }

  // Class inter-dependent definitions

  template <typename T>
  Vector2Base<T>& Vector3Base<T>::xy() {
    return reinterpret_cast<Vector2Base<T>&>(x);
  }

  template <typename T>
  const Vector2Base<T>& Vector3Base<T>::xy() const {
    return reinterpret_cast<const Vector2Base<T>&>(x);
  }

  template <typename T>
  Vector3Base<T>::Vector3Base(const Vector2Base<T>& other, T z)
    : x(other.x)
    , y(other.y)
    , z(z) { 
  }

  template <typename T>
  Vector3Base<T>& Vector4Base<T>::xyz() {
    return reinterpret_cast<Vector3Base<T>&>(x);
  }

  template <typename T>
  const Vector3Base<T>& Vector4Base<T>::xyz() const {
    return reinterpret_cast<const Vector3Base<T>&>(x);
  }

  template <typename T>
  Vector2Base<T>& Vector4Base<T>::xy() {
    return reinterpret_cast<Vector2Base<T>&>(x);
  }

  template <typename T>
  const Vector2Base<T>& Vector4Base<T>::xy() const {
    return reinterpret_cast<const Vector2Base<T>&>(x);
  }

  template <typename T>
  Vector4Base<T>::Vector4Base(const Vector3Base<T>&other, T w)
    : x(other.x)
    , y(other.y)
    , z(other.z)
    , w(w) {
  }

}