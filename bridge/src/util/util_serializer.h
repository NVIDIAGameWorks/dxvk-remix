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
#include <vector>

enum class DataType: uint8_t {
  uint8,
  int8,
  uint16,
  int16,
  uint32,
  int32,
  uint64,
  int64
};

// Serializer
class Serializer {
  std::vector<DataType> types;
  std::vector<uint8_t> sizes;
  std::vector<void*> data;

public:
  Serializer() = default;

  ~Serializer() = default;

  void add_int(const DataType& type, void* integer) {
    types.push_back(type);
    data.push_back(integer);

    switch (type) {
    case DataType::uint8: sizes.push_back(sizeof(uint8_t)); break;
    case DataType::int8: sizes.push_back(sizeof(uint8_t)); break;
    case DataType::uint16: sizes.push_back(sizeof(uint16_t)); break;
    case DataType::int16: sizes.push_back(sizeof(uint16_t)); break;
    case DataType::uint32: sizes.push_back(sizeof(uint32_t)); break;
    case DataType::int32: sizes.push_back(sizeof(uint32_t)); break;
    case DataType::uint64: sizes.push_back(sizeof(uint64_t)); break;
    case DataType::int64: sizes.push_back(sizeof(uint64_t)); break;
    }
  }
};
