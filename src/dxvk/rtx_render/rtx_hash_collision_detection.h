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

#include "rtx_utils.h"
#include "rtx_option.h"
#include "../util/xxHash/xxhash.h"

namespace dxvk {

  enum class HashSourceDataCategory : uint8_t {
    OpacityMicromap = 0,

    Count
  };

  struct HashCollisionDetectionOptions {
    friend class ImGUI;

    RTX_OPTION_ENV("rtx.hashCollisionDetection", bool, enable, false, "RTX_HASH_COLLISION_DETECTION", "Enables hash collision detection.");
  };

  class HashSourceDataCache {
  public:
    ~HashSourceDataCache();

    void release();
    void release(HashSourceDataCategory category);
    fast_unordered_cache<void* /*hashSourceData*/>& getCache(HashSourceDataCategory category);

  private:
    fast_unordered_cache<void* /*hashSourceData*/> m_hashSourceDataCaches[static_cast<uint8_t>(HashSourceDataCategory::Count)];
  };

  // Caches hash source data and validates that any future hash source data instances have a matching hash source data for a given hash and a category
  // Expects hash source data to be fully padded and initialized
  class HashCollisionDetection {
  public:
    ~HashCollisionDetection();

    static void registerHashedSourceData(XXH64_hash_t hash, void* hashSourceData, HashSourceDataCategory category);
  
  private:
    static uint32_t getHashSourceDataSize(HashSourceDataCategory category);

    inline static HashSourceDataCache s_caches;
    inline static dxvk::mutex s_cacheAccessMutex;
   };
}  // namespace dxvk

