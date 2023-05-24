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

#include <vector>
#include <limits>
#include <unordered_map>

#include "../util/util_vector.h"
#include "dxvk_scoped_annotation.h"

#include "rtx_types.h"
#include <d3d9types.h>

namespace dxvk 
{
class DxvkDevice;

// A cache of the BlasEntries across frames.  This maintains stable BlasEntry pointers until that BlasEntry
// is erased by sceneManager's garbage collection.
class DrawCallCache {
public:
  enum class CacheState
  {
    kNew = 0,
    kExisted = 1,
  };

  DrawCallCache(DrawCallCache const&) = delete;
  DrawCallCache& operator=(DrawCallCache const&) = delete;

  DrawCallCache(Rc<DxvkDevice> device);
  ~DrawCallCache();

  CacheState get(const DrawCallState& drawCall, BlasEntry** out);

  std::unordered_multimap<XXH64_hash_t, BlasEntry>& getEntries() {return m_entries;}

  void clear() {
    m_entries.clear();
  }

private:
  std::unordered_multimap<XXH64_hash_t, BlasEntry> m_entries;

  Rc<DxvkDevice> m_device;

  BlasEntry* allocateEntry(XXH64_hash_t hash, const DrawCallState& drawCall);
};

}  // namespace nvvk
