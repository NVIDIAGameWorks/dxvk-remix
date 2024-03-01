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
#include <unordered_map>
#include <unordered_set>
#include "xxHash/xxhash.h"


namespace dxvk {
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
      for (auto it = this->begin(); it != this->end();) {
        if (!p(it)) {
          ++it;
        } else {
          it = this->erase(it);
        }
      }
    }
  };

  // A fast set for use ONLY with already hashed keys.
  struct fast_unordered_set : public std::unordered_set<XXH64_hash_t, XXH64_hash_passthrough> { };

  static bool lookupHash(const fast_unordered_set& hashList, const XXH64_hash_t& h) {
    return hashList.find(h) != hashList.end();
  }
}