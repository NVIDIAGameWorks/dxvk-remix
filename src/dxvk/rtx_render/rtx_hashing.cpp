/*
* Copyright (c) 2023, NVIDIA CORPORATION. All rights reserved.
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
#include <smmintrin.h>
#include <math.h>
#include <intrin.h>
#include <vector>
#include <string_view>

#include "../util/xxHash/xxhash.h"
#include "../util/util_fastops.h"
#include "../util/util_string.h"

#include "rtx_options.h"
#include "rtx_hashing.h"
#include "dxvk_scoped_annotation.h"
#include "rtx_geometry_utils.h"
#include "rtx_types.h"

namespace dxvk {
  const static char* HashComponentNames[] = {
    "positions",
    "legacypositions0",
    "legacypositions1",
    "texcoords",
    "indices",
    "legacyindices",
    "geometrydescriptor",
    "vertexlayout",
  };
  static_assert((sizeof(HashComponentNames) / sizeof(char*)) == (size_t) HashComponents::Count);

  const char* getHashComponentName(const HashComponents& component) {
    return HashComponentNames[(uint32_t) component]; 
  }

  HashRule createRule(const char* rulesetName, const std::string& ruleset) {
    HashRule ruleOutput;

    Logger::info(str::format(rulesetName, " hash rule:"));

    if (ruleset == "") {
      Logger::warn("\tNo active geometry hash rule.");
      return 0;
    }

    // Remove any empy spaces in case the tokens have spaces occuring after delimiters
    std::string rulesetNoSpaces = ruleset;
    rulesetNoSpaces.erase(std::remove(rulesetNoSpaces.begin(), rulesetNoSpaces.end(), ' '), rulesetNoSpaces.end());

    const std::vector<std::string> tokens = dxvk::str::split(rulesetNoSpaces);
    for (auto&& token : tokens) {
      for (uint32_t i = 0; i < (uint32_t) HashComponents::Count; i++) {
        if (token == HashComponentNames[(uint32_t) i]) {
          Logger::info(str::format("\t", HashComponentNames[(uint32_t) i]));
          ruleOutput.set((HashComponents) i);
        }
      }
    }

    return ruleOutput;
  }

  XXH64_hash_t hashGeometryDescriptor(const uint32_t indexCount, 
                                      const uint32_t vertexCount,
                                      const uint32_t indexType,
                                      const uint32_t topology) {
    // Note: Only information relating to how the geometry is structured should be included here.
    XXH64_hash_t h = XXH3_64bits_withSeed(&indexCount, sizeof(indexCount), 0);
    h = XXH3_64bits_withSeed(&vertexCount, sizeof(vertexCount), h);
    h = XXH3_64bits_withSeed(&topology, sizeof(topology), h);
    return XXH3_64bits_withSeed(&indexType, sizeof(indexType), h);
  }

  XXH64_hash_t hashVertexLayout(const RasterGeometry& input) {
    const size_t vertexStride = (input.isVertexDataInterleaved() && input.areFormatsGpuFriendly()) ? input.positionBuffer.stride() : RtxGeometryUtils::computeOptimalVertexStride(input);
    return XXH3_64bits(&vertexStride, sizeof(vertexStride));
  }

  XXH64_hash_t hashContiguousMemory(const void* pData, size_t byteSize) {
    ScopedCpuProfileZone();

    return XXH3_64bits(pData, byteSize);
  }

  template<typename T>
  XXH64_hash_t hashVertexRegionIndexed(const HashQuery& query, const std::vector<T>& uniqueIndices) {
    ScopedCpuProfileZone();

    XXH64_hash_t result = 0;

    constexpr bool hasIndices = std::is_same<T, uint16_t>::value || std::is_same<T, uint32_t>::value;

    if (hasIndices && uniqueIndices.size() > 0) {
      for (const T idx : uniqueIndices) {
        const uint8_t* pData = (query.pBase + idx * query.stride);
        result = XXH3_64bits_withSeed(pData, query.elementSize, result);
      }
    } else {
      for (uint32_t i = 0; i < query.size; i += query.stride) {
        const uint8_t* pData = (query.pBase + i);
        result = XXH3_64bits_withSeed(pData, query.elementSize, result);
      }
    }

    return result;
  }


  // TODO (REMIX-656): Remove this once we can transition content to new hash
  constexpr static uint32_t MaxGeomHashSize = 512; // 512b - this is a performance optimization

  // TODO (REMIX-656): Remove this once we can transition content to new hash
  inline __m128 discretize_SSE(const float* in, __m128 stepSize, __m128 invStepSize) {
    // Load the input data with mm_set_ps because it is likely not aligned to 16 bytes
    __m128 val = _mm_set_ps(0.f, in[2], in[1], in[0]);
    // Calculate: round(val * invStepSize) * stepSize
    val = _mm_mul_ps(val, invStepSize);
    // NOTE: MathLib is polluting our usage of mm here, undef and use actual intrinsic
#undef _mm_round_ps
    val = _mm_round_ps(val, _MM_FROUND_FLOOR);
    val = _mm_mul_ps(val, stepSize);
    return val;
  }

  // TODO (REMIX-656): Remove this once we can transition content to new hash
  inline void discretize(float* in, float stepSize) {
    in[0] = floor(in[0] / stepSize) * stepSize;
    in[1] = floor(in[1] / stepSize) * stepSize;
    in[2] = floor(in[2] / stepSize) * stepSize;
  }

  // TODO (REMIX-656): Remove this once we can transition content to new hash
  template<typename T>
  XXH64_hash_t hashIndicesLegacy(const void* pIndexData, const size_t indexCount) {
    ScopedCpuProfileZone();

    XXH64_hash_t indexHash = 0;

    if (indexCount * sizeof(T) <= MaxGeomHashSize * 2) {
      // Short buffer
      indexHash = XXH3_64bits(pIndexData, indexCount * sizeof(T));
    } else {
      // Long buffer, sample indices throughout:
      uint32_t step = indexCount * sizeof(T) / MaxGeomHashSize;
      for (uint32_t i = 0; i < indexCount; i += step) {
        indexHash = XXH3_64bits_withSeed((uint8_t*) pIndexData + i * sizeof(T), sizeof(T), indexHash);
      }
    }
    return indexHash;
  }

  // TODO (REMIX-656): Remove this once we can transition content to new hash
  void hashRegionLegacy(const HashQuery& query, XXH64_hash_t& h0, XXH64_hash_t& h1) {
    ScopedCpuProfileZone();

    // Need to round the vertex positions to prevent floating point error from changing the hash.  In practice positions were found
    // to have value errors in the order of 1 mm, so this step value is chosen to be within an order of magnitude of 1 cm.
    const float discreteStepSize = 0.01f * RtxOptions::Get()->getMeterToWorldUnitScale();

    const uint32_t dataToHash = query.size;
    const uint32_t kInitialHashVertexCount = 20;
    const uint32_t dataForLegacyHash = std::min(query.size, kInitialHashVertexCount * query.stride);

    // There are LSB differences in some key meshes, and discretizing the positions before
    // hashing makes the hash more stable. But the discretization function becomes
    // a significant bottleneck when it uses regular roundf(), so use an SSE-optimized
    // version of the discretize function below, if supported.
    const bool sse41supported = fast::getSimdSupportLevel() == fast::SIMD::SSE4_1;
    if (sse41supported) {
      // Prefetch the first vertex
      _mm_prefetch((char const*) query.pBase, 0);
      // Pre-calculate the scaling factors and place them into SSE regs
      __m128 stepSize = _mm_set1_ps(discreteStepSize);
      __m128 invStepSize = _mm_set1_ps(1.f / discreteStepSize);
      for (uint32_t i = 0; i < dataToHash; i += query.stride) {
        // Prefetch the next vertex
        _mm_prefetch((char const*) (query.pBase + i + query.stride), 0);
        // Save the legacy hash upon reaching 20 vertices (or less)
        if (i == dataForLegacyHash)
          h0 = h1;
        // Discretize
        __m128 vPos = discretize_SSE((const float*) (query.pBase + i), stepSize, invStepSize);
        // Hash the result
        h1 = XXH3_64bits_withSeed(&vPos, sizeof(float) * 3, h1);
      }
    } else {
      for (uint32_t i = 0; i < dataToHash; i += query.stride) {
        // Save the legacy hash upon reaching 20 vertices (or less)
        if (i == dataForLegacyHash)
          h0 = h1;
        Vector3 vPos = (*(Vector3*) (query.pBase + i));
        // NOTE: Discovered that there are LSB differences in some key meshes (Portal cube), this fixes those.
        discretize(&vPos.x, discreteStepSize);
        h1 = XXH3_64bits_withSeed(&vPos, query.elementSize, h1);
      }
    }
  }

  // Supported template params
  template XXH64_hash_t hashVertexRegionIndexed(const HashQuery& query, const std::vector<uint16_t>& uniqueIndices);
  template XXH64_hash_t hashVertexRegionIndexed(const HashQuery& query, const std::vector<uint32_t>& uniqueIndices);
  template XXH64_hash_t hashVertexRegionIndexed(const HashQuery& query, const std::vector<int>& uniqueIndices);

  template XXH64_hash_t hashIndicesLegacy<uint16_t>(const void* pIndexData, const size_t indexCount);
  template XXH64_hash_t hashIndicesLegacy<uint32_t>(const void* pIndexData, const size_t indexCount);
}
