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

namespace bridge_util {

  static char* kStrByte("B");
  static char* kStrKiloByte("kB");
  static char* kStrMegaByte("MB");
  static char* kStrGigaByte("GB");

  static constexpr size_t kKByte = 1 << 10;
  static constexpr size_t kMByte = 1 << 20;
  static constexpr size_t kGByte = 1 << 30;

  enum class ByteUnit {
    B = 0,
    kB = 10,
    MB = 20,
    GB = 30
  };

  static ByteUnit findLargestByteUnit(const size_t val) {
    if (val >= kGByte) {
      return ByteUnit::GB;
    } else if (val >= kMByte) {
      return ByteUnit::MB;
    } else if (val >= kKByte) {
      return ByteUnit::kB;
    } else {
      return ByteUnit::B;
    }
  }

  static size_t convertToByteUnit(const size_t val, const ByteUnit unit) {
    return (val >> (size_t) unit);
  }

  static std::string toByteUnitString(const size_t val) {
    const auto unit = findLargestByteUnit(val);
    const auto convertedVal = convertToByteUnit(val, unit);
    std::string str;
    str += std::to_string(convertedVal);
    switch (unit) {
    case ByteUnit::GB: str += kStrGigaByte; break;
    case ByteUnit::MB: str += kStrMegaByte; break;
    case ByteUnit::kB: str += kStrKiloByte; break;
    case ByteUnit::B:  str += kStrByte; break;
    }
    return str;
  }
}
