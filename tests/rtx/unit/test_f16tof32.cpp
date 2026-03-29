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
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>

// Test the actual CPU-side f16tof32 implementation
#include "rtx/utility/f16_conversion.h"

namespace {

// Helper: build a half-float bit pattern from components
uint32_t makeHalf(uint32_t sign, uint32_t exponent, uint32_t mantissa) {
  return (sign << 15u) | (exponent << 10u) | mantissa;
}

// Helper: compare floats with tolerance
void expectNear(float actual, float expected, float tolerance, const char* label) {
  if (std::abs(actual - expected) > tolerance) {
    std::cerr << label << ": expected " << expected << ", got " << actual << std::endl;
    throw std::runtime_error(label);
  }
}

void expectExact(float actual, float expected, const char* label) {
  uint32_t a, b;
  std::memcpy(&a, &actual, sizeof(actual));
  std::memcpy(&b, &expected, sizeof(expected));
  if (a != b) {
    std::cerr << label << ": expected bits 0x" << std::hex << b
              << ", got 0x" << a << std::dec << std::endl;
    throw std::runtime_error(label);
  }
}

class F16ToF32TestApp {
public:
  static void run() {
    std::cout << std::endl << "Begin f16tof32 tests" << std::endl;
    test_zeros();
    test_normals();
    test_denormals();
    test_inf_nan();
    test_roundtrip_known_values();
    std::cout << "All f16tof32 tests passed" << std::endl;
  }

private:
  static void test_zeros() {
    std::cout << "  test_zeros" << std::endl;
    // +0
    expectExact(f16tof32(makeHalf(0, 0, 0)), 0.0f, "+zero");
    // -0
    expectExact(f16tof32(makeHalf(1, 0, 0)), -0.0f, "-zero");
  }

  static void test_normals() {
    std::cout << "  test_normals" << std::endl;
    // 1.0 = sign=0, exp=15, mant=0 -> 0x3C00
    expectExact(f16tof32(0x3C00u), 1.0f, "1.0");
    // -1.0 = 0xBC00
    expectExact(f16tof32(0xBC00u), -1.0f, "-1.0");
    // 0.5 = sign=0, exp=14, mant=0 -> 0x3800
    expectExact(f16tof32(0x3800u), 0.5f, "0.5");
    // 2.0 = sign=0, exp=16, mant=0 -> 0x4000
    expectExact(f16tof32(0x4000u), 2.0f, "2.0");
    // 65504.0 (max normal half) = sign=0, exp=30, mant=0x3FF -> 0x7BFF
    expectExact(f16tof32(0x7BFFu), 65504.0f, "65504.0 (max normal)");
    // -2.5 = sign=1, exp=16, mant=0x100 -> 0xC100
    expectNear(f16tof32(0xC100u), -2.5f, 1e-6f, "-2.5");
  }

  static void test_denormals() {
    std::cout << "  test_denormals" << std::endl;
    // Smallest positive denorm: sign=0, exp=0, mant=1
    // Expected: 2^-24 ~= 5.96046e-8
    float smallest = f16tof32(makeHalf(0, 0, 1));
    expectNear(smallest, 5.96046448e-8f, 1e-14f, "smallest denorm");
    // Largest denorm: sign=0, exp=0, mant=0x3FF
    // Expected: 0x3FF * 2^-24 = 1023 * 2^-24 ~= 6.09756e-5
    float largest = f16tof32(makeHalf(0, 0, 0x3FF));
    expectNear(largest, 6.09755516e-5f, 1e-10f, "largest denorm");
  }

  static void test_inf_nan() {
    std::cout << "  test_inf_nan" << std::endl;
    // +Inf: exp=31, mant=0 -> 0x7C00
    float posInf = f16tof32(0x7C00u);
    if (!std::isinf(posInf) || posInf < 0) {
      throw std::runtime_error("+Inf failed");
    }
    // -Inf: 0xFC00
    float negInf = f16tof32(0xFC00u);
    if (!std::isinf(negInf) || negInf > 0) {
      throw std::runtime_error("-Inf failed");
    }
    // NaN: exp=31, mant!=0 -> 0x7C01
    float nan = f16tof32(0x7C01u);
    if (!std::isnan(nan)) {
      throw std::runtime_error("NaN failed");
    }
  }

  static void test_roundtrip_known_values() {
    std::cout << "  test_roundtrip_known_values" << std::endl;
    // Test a set of known half-float bit patterns and their expected float values
    struct TestCase {
      uint32_t half;
      float expected;
    };
    TestCase cases[] = {
      { 0x3555u, 0.333251953125f }, // ~1/3
      { 0x3C00u, 1.0f },
      { 0x4200u, 3.0f },
      { 0x4900u, 10.0f },
      { 0x5640u, 100.0f },
      { 0x6E10u, 6208.0f },
    };
    for (const auto& tc : cases) {
      expectNear(f16tof32(tc.half), tc.expected, 1e-4f, "roundtrip known value");
    }
  }
};

} // anonymous namespace

int main() {
  try {
    F16ToF32TestApp::run();
  }
  catch (const std::exception& e) {
    std::cerr << e.what() << std::endl;
    throw;
  }

  return 0;
}
