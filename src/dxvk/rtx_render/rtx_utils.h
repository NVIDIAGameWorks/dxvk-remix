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

#include <sstream>
#include <iomanip>
#include <cassert>
#include <optional>
#include <vulkan/vulkan.h>
#include <glm/gtc/packing.hpp>
#include "dxvk_buffer.h"
#include "dxvk_image.h"
#include "dxvk_sampler.h"
#include "../util/util_vector.h"
#include "../util/util_matrix.h"
#include "../util/util_quat.h"
#include "../util/util_pack.h"
#include "dxvk_bind_mask.h"
#include <d3d9types.h>
#include <type_traits>

namespace dxvk 
{
// 64kb is the size of a physical GPU memory page, aligning buffers to this size will eliminate redundant allocs
static constexpr size_t kBufferAlignment = 64 * 1024;
static constexpr float kPi = 3.141592653589793f;
static constexpr float kDegreesToRadians = kPi / 180.0f;
static constexpr float kRadiansToDegrees = 180.0f / kPi;
static constexpr uint32_t kMaxFramesInFlight = 4;   // ToDo: use actual swap chain image size

template<typename T>
void writeGPUHelper(unsigned char* data, std::size_t& offset, const T& value) {
  std::memcpy(data + offset, &value, sizeof(value));

  offset += sizeof(value);
}

// Note: This variant is used for writing an explicit type by cutting off the end of integers without needing to perform a static cast
template<uint32_t Bytes, typename T>
void writeGPUHelperExplicit(unsigned char* data, std::size_t& offset, const T& value) {
  static_assert(Bytes <= sizeof(T), "Explicit size must be less than or equal to the size of the original value");

  // Note: Ensure the value can fit in the requested explicit size
  assert(value < (static_cast<T>(1) << (Bytes * 8)));

  std::memcpy(data + offset, &value, Bytes);

  offset += Bytes;
}

template<uint32_t Bytes>
void writeGPUPadding(unsigned char* data, std::size_t& offset) {
#ifndef NDEBUG
  // Note: Debug pattern for catching incorrect reads from padding regions
  std::memset(data + offset, 0xFF, Bytes);
#endif

  offset += Bytes;
}

inline const std::string hashToString(XXH64_hash_t hash) {
  //Two Hex Digits per byte
  constexpr uint8_t kNumHexits = sizeof(hash) * 2;
  std::stringstream ss;
  ss << std::uppercase << std::setfill('0') << std::setw(kNumHexits) << std::hex << hash;
  return ss.str();
}

enum BufferType {
  Raster = 0,
  Raytrace
};

// Raster and Raytrace buffers are very similar, but not the same. 
// Template enforces that inequality at compile time to avoid mistakes.
template<BufferType T>
class GeometryBuffer : public DxvkBufferSlice {
 public:
   GeometryBuffer() { }

   GeometryBuffer(const DxvkBufferSlice slice, uint32_t offsetFromSlice, uint32_t stride, VkIndexType type)
    : DxvkBufferSlice(slice)
    , m_offsetFromSlice(offsetFromSlice)
    , m_stride(stride) {
    m_format.index = type;
  }

  GeometryBuffer(const DxvkBufferSlice slice, uint32_t offsetFromSlice, uint32_t stride, VkFormat vertexFormat)
    : DxvkBufferSlice(slice)
    , m_offsetFromSlice(offsetFromSlice)
    , m_stride(stride) {
    m_format.vertex = vertexFormat;
  }

  uint32_t offsetFromSlice() const { return m_offsetFromSlice;}
  uint32_t stride() const { return m_stride; }
  VkFormat vertexFormat() const {return m_format.vertex;}
  VkIndexType indexType() const {return m_format.index;}

  bool operator==(GeometryBuffer const& rhs) const {
    return defined() && rhs.defined() && matches(rhs)
       && m_stride == rhs.m_stride
       && m_format.vertex == rhs.m_format.vertex;
  }
  
  bool operator!=(GeometryBuffer const& rhs) const {
    return !(*this == rhs);
  }

  bool isPendingGpuWrite() const {
    return buffer()->isInUse(DxvkAccess::Write);
  }

  inline void* mapPtr(VkDeviceSize offset = 0) const {
    return DxvkBufferSlice::mapPtr(offset);
  }

 private:
  uint32_t m_offsetFromSlice = 0;
  uint32_t m_stride = 0;
  union Format {
    VkFormat vertex;
    VkIndexType index;
  };
  // access as m_format.vertex for vertex types, m_format.index for index types.
  Format m_format = {VkFormat(0)};
};

inline uint32_t setBit(uint32_t target, bool value, uint32_t oneBitMask) {
  return (target & ~oneBitMask) | (value ? oneBitMask : 0);
}

inline uint32_t setBits(uint32_t target, uint32_t value, uint32_t bitmask) {
  return (target & ~bitmask) | (value & bitmask);
}

inline uint32_t setBits(uint32_t& target, uint32_t value, uint32_t bitmask, uint32_t lshift) {
  return setBits(target, value << lshift, bitmask << lshift);
}

// A passthrough hash class compatible with std c++ containers.
struct XXH64_hash_passthrough {
  [[nodiscard]] size_t operator()(const XXH64_hash_t keyval) const noexcept {
    static_assert(sizeof(size_t) == sizeof(XXH64_hash_t), "Hash value size != size_t size.");
    return keyval;
  }
};

// A hash class compatible with std c++ containers.
template<typename T>
struct XXH64_std_hash {
  [[nodiscard]] size_t operator()(const T keyval) const noexcept {
    static_assert(sizeof(size_t) == sizeof(XXH64_hash_t), "Hash value size != size_t size.");
    static_assert((std::is_enum_v<T> || std::is_integral_v<T> || std::is_pointer_v<T>), "Uncompatible key type.");

    return XXH3_64bits(&keyval, sizeof(keyval));
  }
};

template<>
struct XXH64_std_hash<std::string> {
  [[nodiscard]] size_t operator()(const std::string& keyval) const noexcept {
    return XXH3_64bits(keyval.c_str(), keyval.length());
  }
};

// A fast caching structure for use ONLY with already hashed keys.
template<class T>
struct fast_unordered_cache : public std::unordered_map<XXH64_hash_t, T, XXH64_hash_passthrough> {
  template<typename P>
  void erase_if(P&& p) {
    for (auto it = begin(); it != end();) {
      if (!p(it)) {
        ++it;
      } else {
        it = erase(it);
      }
    }
  }
};

} // namespace dxvk
