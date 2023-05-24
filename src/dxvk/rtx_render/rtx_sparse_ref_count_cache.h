/*
* Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.
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
#include <queue>
#include <unordered_map>

namespace dxvk 
{
/*
*  Sparse Ref Counted (Object) Cache
* 
*  This is a high watermark unique object tracking container.  The idea is to 
*  efficiently store unique objects in a linear list, where each object owns 
*  a fixed index for it's tracking lifetime.
* 
*  For example:
*  { 0, 1, 2, 3, 4, ..., N }
* 
*  Remove any element, and a null element takes it's place:
*  { 0, 1, null, 3, 4, ..., N }
* 
*  All previous elements indices remain the same, the recently free'd  'null'
*  elements (2nd) index is added to a "free list", which implies this element 
*  should be repopulated next (FIFO) when a new tracking request comes in.
* 
*  This cache's storage high watermarks based on the total number of unique 
*  objects in the scene, and so is technically unbounded.
* 
*  This structure is particularly useful for tracking GPU objects, where persistent
*  indices for large, dynamic arrays are required.  e.g. bindless resources.
* 
*  NOTE: This object does ref counting, which is useful when multiple fields / objects need
*  to share the same resource.
*/
template<typename T, typename HashFn>
struct SparseRefCountCache
{
public:
  SparseRefCountCache(SparseRefCountCache const&) = delete;
  SparseRefCountCache& operator=(SparseRefCountCache const&) = delete;

  SparseRefCountCache() {}
  ~SparseRefCountCache() {}

  void clear() {
    std::queue<uint32_t> empty;
    std::swap(m_freeBuffers, empty);
    m_objects.clear();
    m_bufferMap.clear();
  }

  uint32_t addRef(const T& buf) {
    auto& iter = m_bufferMap.find(buf);
    uint32_t idx;
    if (iter == m_bufferMap.end()) {
      if (!m_freeBuffers.empty()) {
        idx = m_freeBuffers.front();
        m_freeBuffers.pop();
        m_objects.at(idx) = buf;
      } else {
        idx = m_objects.size();
        m_objects.push_back(buf);
      }
      m_bufferMap[buf] = std::make_pair(idx, 1);
    } else {
      idx = iter->second.first;
      ++iter->second.second;
    }
    return idx;
  }

  bool find(const T& buf, uint32_t& outIdx) const {
    auto& iter = m_bufferMap.find(buf);
    if (iter != m_bufferMap.end()) {
      outIdx = iter->second.first;
      return true;
    }
    return false;
  }

  void removeRef(const T& buf) {
    auto& iter = m_bufferMap.find(buf);
    if (iter != m_bufferMap.end()) {
      --iter->second.second;
      if (iter->second.second == 0) {
        m_objects.at(iter->second.first) = T();
        m_freeBuffers.push(iter->second.first);
        m_bufferMap.erase(iter);
      }
    }
  }

  uint32_t getActiveCount() const { return m_objects.size() - m_freeBuffers.size(); }
  uint32_t getTotalCount() const { return m_objects.size(); }

  const std::vector<T>& getObjectTable() const { return m_objects; }

private:
  std::queue<uint32_t> m_freeBuffers;
  std::vector<T> m_objects;
  // values of m_bufferMap are <index, refcount>
  std::unordered_map<T, std::pair<uint32_t, uint32_t>, HashFn> m_bufferMap;
};

}  // namespace dxvk

