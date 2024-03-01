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
#pragma once

#include <vector>

#include "rtx_constants.h"

#include "../util/xxHash/xxhash.h"
#include "../util/rc/util_rc_ptr.h"
#include "../util/util_flags.h"

namespace dxvk {
  enum class HashComponents : uint32_t {
    VertexPosition = 0,
    LegacyPositions0,
    LegacyPositions1,
    VertexTexcoord,
    Indices,
    LegacyIndices,
    GeometryDescriptor,
    VertexLayout,
    VertexShader,
    Count
  };

  using HashRule = Flags<HashComponents>;

  // Set of predefined, useful hash rules
  namespace rules {
    const uint32_t TopologicalHash = (1 << (uint32_t)HashComponents::Indices)
                                   | (1 << (uint32_t)HashComponents::GeometryDescriptor);    
    
    const uint32_t VertexDataHash  = (1 << (uint32_t)HashComponents::VertexPosition)
                                   | (1 << (uint32_t)HashComponents::VertexTexcoord)
                                   | (1 << (uint32_t)HashComponents::VertexLayout)
                                   | (1 << (uint32_t)HashComponents::VertexShader);

    const uint32_t FullGeometryHash = VertexDataHash | TopologicalHash;

    const uint32_t LegacyAssetHash0 = (1 << (uint32_t)HashComponents::LegacyPositions0)
                                    | (1 << (uint32_t)HashComponents::LegacyIndices);

    const uint32_t LegacyAssetHash1 = (1 << (uint32_t)HashComponents::LegacyPositions1)
                                    | (1 << (uint32_t)HashComponents::LegacyIndices);
    const uint32_t Total = 5;
  }

  // Structure contains data required to perform a hash operation on specific data
  struct HashQuery {
    uint8_t* pBase;           // base pointer of the memory region to hash
    size_t size;              // length of the memory in bytes
    size_t stride;            // byte stride elements within buffer
    size_t elementSize;       // byte stride of the specific elements to hash
    class DxvkBuffer* ref;    // a pointer to the buffer for ref counting purposes
                              // Note: buffer object is refcounted manually using incRef/decRef.
  };

  // Structure containing the various hashes used for geometry
  struct GeometryHashes {
    GeometryHashes() {
      memset(&fields[0], kEmptyHash, sizeof(fields));
    }

    // Simple getters for hash components
    const XXH64_hash_t& operator[](const HashComponents& field) const { return fields[(uint32_t) field]; }
          XXH64_hash_t& operator[](const HashComponents& field)       { return fields[(uint32_t) field]; }

    void precombine() {
      precombined[0] = getHashForRuleImpl<rules::TopologicalHash>();
      precombined[1] = getHashForRuleImpl<rules::VertexDataHash>();
      precombined[2] = getHashForRuleImpl<rules::FullGeometryHash>();
      if (operator[](HashComponents::LegacyPositions0) != kEmptyHash) {
        precombined[3] = getHashForRuleImpl<rules::LegacyAssetHash0>();
      }
      if (operator[](HashComponents::LegacyPositions1) != kEmptyHash) {
        precombined[4] = getHashForRuleImpl<rules::LegacyAssetHash1>();
      }
    }

    template<uint32_t rule>
    XXH64_hash_t getHashForRule() const {
      switch (rule) {
      case rules::TopologicalHash:
        return precombined[0];
      case rules::VertexDataHash:
        return precombined[1];
      case rules::FullGeometryHash:
        return precombined[2];
      case rules::LegacyAssetHash0:
        return precombined[3];
      case rules::LegacyAssetHash1:
        return precombined[4];
      }
      return getHashForRuleImpl<rule>();
    }

    XXH64_hash_t getHashForRule(const HashRule& rule) const {
      switch (rule.raw()) {
      case rules::TopologicalHash:
        return getHashForRule<rules::TopologicalHash>();
      case rules::VertexDataHash:
        return getHashForRule<rules::VertexDataHash>();
      case rules::FullGeometryHash:
        return getHashForRule<rules::FullGeometryHash>();
      case rules::LegacyAssetHash0:
        return getHashForRule<rules::LegacyAssetHash0>();
      case rules::LegacyAssetHash1:
        return getHashForRule<rules::LegacyAssetHash1>();
      }
      return getHashForRuleImpl(rule);
    }

  private:
    // Legacy hash combiner
    XXH64_hash_t getHashForRuleImpl(const HashRule& rule) const {
      XXH64_hash_t hashResult = kEmptyHash;
      for (uint32_t i = 0; i < (uint32_t) HashComponents::Count; i++) {
        const HashComponents component = (HashComponents) i;

        if (rule.test(component)) {
          if (hashResult == kEmptyHash)
            // For the first entry, we use the hash directly
            hashResult = fields[i];
          else
            // For all other entries, we combine the hash via seeding
            hashResult = XXH64(&(fields[i]), sizeof(XXH64_hash_t), hashResult);
        }
      }
      assert(hashResult != kEmptyHash);
      return hashResult;
    }

    template<uint32_t rule>
    XXH64_hash_t getHashForRuleImpl() const {
      return getHashForRuleImpl(rule);
    }

    // Array of hashes, indexed by HashComponent
    XXH64_hash_t fields[static_cast<uint32_t>(HashComponents::Count)];
    XXH64_hash_t precombined[rules::Total];
  };

  /**
    * \brief Get the name (string) of a particular hash component
    *
    *   component [in]: hash component to get name for
    *   returns: name of the requested component as string
    */
  const char* getHashComponentName(const HashComponents& component);

  /**
    * \brief Converts a string of mod rules (comma-separated values) to a HashRule bitfield
    *
    *   ruleset [in]: list of hash component names (CSV format)
    */
  HashRule createRule(const char* rulesetName, const std::string& ruleset);

  /**
    * \brief Generate a hash from geometry description
    *
    *   indexCount [in]: number of indices
    *   vertexCount [in]: number of vertices
    *   indexType [in]: value uniquely describing the index format of mesh
    *   topology [in]: value uniquely describing the topology of mesh
    */
  XXH64_hash_t hashGeometryDescriptor(const uint32_t indexCount,
                                      const uint32_t vertexCount,
                                      const uint32_t indexType,
                                      const uint32_t topology);

  /**
    * \brief Generate a hash from vertex layout
    *
    *   geometry [in]: object defining the layout of a drawcalls geometry
    */
  XXH64_hash_t hashVertexLayout(const struct RasterGeometry& geometry);

  /**
    * \brief Hashes a region of contiguous memory
    *
    *   pData [in]: pointer of contiguous memory
    *   byteSize [in]: size of memory region in bytes
    */
  XXH64_hash_t hashContiguousMemory(const void* pData, size_t byteSize);

  /**
    * \brief Hashes a region of sparse memory
    *
    *   query [in]: structure containing information about the region
    *   uniqueIndices [in]: indices (byte offsets as multiples of query.stride) to hash
    */
  template<typename T>
  XXH64_hash_t hashVertexRegionIndexed(const HashQuery& query, const std::vector<T>& uniqueIndices);

  template<typename T>
  [[deprecated("(REMIX-656): Remove this once we can transition content to new hash)")]]
  XXH64_hash_t hashIndicesLegacy(const void* pIndexData, const size_t indexCount);

  [[deprecated("(REMIX-656): Remove this once we can transition content to new hash)")]]
  void hashRegionLegacy(const HashQuery& query, XXH64_hash_t& h0, XXH64_hash_t& h1);
}
