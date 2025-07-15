/*
* Copyright (c) 2024, NVIDIA CORPORATION. All rights reserved.
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

#include "util_matrix.h"
#include "util_vector.h"
#include "util_fast_cache.h"
#include "./log/log.h"

namespace dxvk {
  // A structure to allow for quickly returning data close to a specific position.
  template<class T>
  class SpatialMap {
  private:
    struct Entry {
      const T* data;
      Vector3 centroid;
      XXH64_hash_t transformHash;
      Entry() : data(nullptr), centroid(0.f), transformHash(0) { }
      Entry(const T* data, const Vector3& centroid, XXH64_hash_t transformHash) : data(data), centroid(centroid), transformHash(transformHash) { }
      Entry(const Entry& other) : data(other.data), centroid(other.centroid), transformHash(other.transformHash) { }
    };
  public:
    SpatialMap(float cellSize) : m_cellSize(cellSize) {
      if (m_cellSize <= 0) {
        ONCE(Logger::err("Invalid cell size in SpatialMap. cellSize must be greater than 0."));
        m_cellSize = 1.f;
      }
    }

    SpatialMap& operator=(SpatialMap&& other) {
      m_cellSize = other.m_cellSize;
      m_cells = std::move(other.m_cells);
      m_cache = std::move(other.m_cache);
      return *this;
    }

    // returns the data with an identical transform
    const T* getDataAtTransform(const Matrix4& transform) const {
      XXH64_hash_t transformHash = XXH64(&transform, sizeof(transform), 0);
      auto pair = m_cache.find(transformHash);
      if ( pair != m_cache.end()) {
        return pair->second.data;
      }
      return nullptr;
    }

    // returns the entry cosest to `centroid` that passes the `filter` and is less than `sqrt(maxDistSqr)` units from `centroid`.
    // `filter` should return true if the entry is a valid result.
    const T* getNearestData(const Vector3& centroid, float maxDistSqr, float& nearestDistSqr, std::function<bool(const T*)> filter) const {
      static const std::array kOffsets{
        Vector3i{0, 0, 0},
        Vector3i{0, 0, 1},
        Vector3i{0, 1, 0},
        Vector3i{0, 1, 1},
        Vector3i{1, 0, 0},
        Vector3i{1, 0, 1},
        Vector3i{1, 1, 0},
        Vector3i{1, 1, 1}
      };
      const Vector3 cellPosition = centroid / m_cellSize - Vector3(0.5f, 0.5f, 0.5f);
      const Vector3i floorPos(int(std::floor(cellPosition.x)), int(std::floor(cellPosition.y)), int(std::floor(cellPosition.z)));

      const T* nearestData = nullptr;
      nearestDistSqr = FLT_MAX;
      for (const Vector3i& offset : kOffsets) {
        auto cell = m_cells.find(floorPos + offset);
        if (cell == m_cells.end()) {
          continue;
        }
        for (const Entry& entry : cell->second) {
          if (!filter(entry.data)) {
            continue;
          }
          const float distSqr = lengthSqr(entry.centroid - centroid);
          if (distSqr <= maxDistSqr && distSqr < nearestDistSqr) {
              nearestDistSqr = distSqr;
            if (nearestDistSqr == 0.0f) {
              // Not going to find anything closer, so stop the iteration
              return entry.data;
            }
            nearestData = entry.data;
          }
        }
      }
      return nearestData;
    }
    
    XXH64_hash_t insert(const Vector3& centroid, const Matrix4& transform, const T* data) {
      XXH64_hash_t transformHash = XXH64(&transform, sizeof(transform), 0);
      while(m_cache.find(transformHash) != m_cache.end()) {
        // Note: This can happen if an instance is moved to the same position as another existing instance.
        // It can cause a single frame of NaN, but shouldn't cause any crashes.
        // TODO(REMIX-4134): Once spatial map is used on draw calls and not rtInstances, it should be safe to restore the assert() below.
        ONCE(Logger::warn("Specified hash was already present in SpatialMap::insert(). May indicate a duplicated overlapping object."));
        // assert(false);
        transformHash++;
      }
      auto [iter, success] = m_cache.emplace(std::piecewise_construct,
          std::forward_as_tuple(transformHash),
          std::forward_as_tuple(data, centroid, transformHash));
      if (!success) {
        ONCE(Logger::err("Failed to add entry in SpatialMap::insert()."));
        assert(false);
        return transformHash;
      }
      m_cells[getCellPos(centroid)].emplace_back(data, centroid, transformHash);
      return transformHash;
    }

    void erase(const XXH64_hash_t& transformHash) {
      auto pair = m_cache.find(transformHash);
      if (pair != m_cache.end()) {
        eraseFromCell(pair->second.centroid, transformHash);
        m_cache.erase(pair);
      } else {
        // Note: This can happen if a duplicate hash is encountered in the insert() call.
        // TODO(REMIX-4134): Once spatial map is used on draw calls and not rtInstances, it should be safe to restore the assert() below.
        ONCE(Logger::warn("Specified hash was missing in SpatialMap::erase()."));
        // assert(false);
      }
    }

    XXH64_hash_t move(const XXH64_hash_t& oldTransformHash, const Vector3& centroid, const Matrix4& newTransform, const T* data) {
      XXH64_hash_t transformHash = XXH64(&newTransform, sizeof(newTransform), 0);

      if (oldTransformHash != transformHash) {
        erase(oldTransformHash);
        insert(centroid, newTransform, data);
      }
      return transformHash;
    }

    void rebuild(float cellSize) {
      m_cells.clear();
      for (auto pair : m_cache) {
        m_cells[getCellPos(pair.second.centroid)].emplace_back(pair.second);
      }
    }

    size_t size() const {
      return m_cache.size();
    }

  private:

    Vector3i getCellPos(const Vector3& position) const {
      const Vector3 scaledPos = position / m_cellSize;
      return Vector3i(int(std::floor(scaledPos.x)), int(std::floor(scaledPos.y)), int(std::floor(scaledPos.z))); 
    }

    void eraseFromCell(const Vector3& pos, XXH64_hash_t hash) {
      auto cellIter = m_cells.find(getCellPos(pos));
      if (cellIter == m_cells.end()) {
        ONCE(Logger::err("Specified cell was already empty in SpatialMap::erase()."));
        assert(false);
        return;
      }

      std::vector<Entry>& cell = cellIter->second;
      for (auto iter = cell.begin(); iter != cell.end(); ++iter) {
        if (iter->transformHash == hash) {
          if (cell.size() > 1) {
            // Swap & pop - faster than "erase", but doesn't preserve order, which is fine here.
            std::swap(*iter, cell.back());
            cell.pop_back();
          } else {
            m_cells.erase(cellIter);
          }
          return;
        }
      }

      Logger::err("Couldn't find matching data in SpatialMap::erase().");
    }

    float m_cellSize;
    fast_spatial_cache<std::vector<Entry>> m_cells;
    fast_unordered_cache<Entry> m_cache;
  };
}
