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
#include "rtx_hash_collision_detection.h"

#include "rtx_opacity_micromap_manager.h"
#include "dxvk_device.h"

namespace dxvk {

  static constexpr char* HashSourceDataCategoryName[HashSourceDataCategory::Count] = {
    "OpacityMicromap",
  };


  HashSourceDataCache::~HashSourceDataCache() {
    release();
  }

  void HashSourceDataCache::release(HashSourceDataCategory category) {
    for (auto& cacheItem : m_hashSourceDataCaches[static_cast<uint8_t>(category)]) {
      free(cacheItem.second);
    }
  }

  void HashSourceDataCache::release() {
    for (uint8_t i = 0; i < static_cast<uint8_t>(HashSourceDataCategory::Count); i++) {
      release(static_cast<HashSourceDataCategory>(i));
    }
  }

  fast_unordered_cache<void*>& HashSourceDataCache::getCache(HashSourceDataCategory category) {
    return m_hashSourceDataCaches[static_cast<uint8_t>(category)];
  }

  uint32_t HashCollisionDetection::getHashSourceDataSize(HashSourceDataCategory category) {
    switch (category) {
    case HashSourceDataCategory::OpacityMicromap: return sizeof(OpacityMicromapHashSourceData);
    default:
      assert(!"Invalid category specified.");
      return 0;
    }
  }

  void HashCollisionDetection::registerHashedSourceData(XXH64_hash_t hash, void* hashSourceData, HashSourceDataCategory category) {
    if (!HashCollisionDetectionOptions::enable()) {
      return;
    }

    auto bitExactMatch = [](const void* data0, const void* data1, uint32_t dataSize) {
      return memcmp(data0, data1, dataSize) == 0;
    };

    std::lock_guard lock(s_cacheAccessMutex);
    
    fast_unordered_cache<void*>& cache = s_caches.getCache(category);
    auto cacheItemIter = cache.find(hash);

    const uint32_t hashSourceDataSize = getHashSourceDataSize(category);

    // Hash is in the cache
    if (cacheItemIter != cache.end()) {
      const void* cachedHashSourceData = cacheItemIter->second;

      // Validate the source data matches
      if (!bitExactMatch(hashSourceData, cachedHashSourceData, hashSourceDataSize)) {
        std::stringstream ssHash;
        ssHash << "0x" << std::uppercase << std::setfill('0') << std::hex << hash;

        Logger::err(str::format("[RTX Hash Collision Detection] Found a hash collision for hash ", ssHash.str(), " in category ", HashSourceDataCategoryName[static_cast<uint8_t>(category)]));
      }
    } 
    else { // Hash is not in the cache
      // Insert it
      void* hashSourceDataCopy = malloc(hashSourceDataSize);
      memcpy(hashSourceDataCopy, hashSourceData, hashSourceDataSize);
      cache.insert({ hash, hashSourceDataCopy });
    }
  }

}  // namespace dxvk
