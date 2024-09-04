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

#include "util_vector.h"
#include "util_fast_cache.h"
#include "./log/log.h"

namespace dxvk {
  // A structure to allow for quickly returning data close to a specific position.
  template<class T>
  class SpatialMap {
  public:
    SpatialMap(float cellSize) : m_cellSize(cellSize) {
      if (m_cellSize <= 0) {
        ONCE(Logger::err("Invalid cell size in SpatialMap. cellSize must be greater than 0."));
        m_cellSize = 1.f;
      }
    }

    SpatialMap& operator=(SpatialMap&& other) {
      m_cellSize = other.m_cellSize;
      m_cache = std::move(other.m_cache);
      return *this;
    }

    // returns the 8 cells closest to `position`
    const std::vector<const std::vector<T>*> getDataNearPos(const Vector3& position) const {
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
      std::vector<const std::vector<T>*> result;
      result.reserve(8);

      const Vector3 cellPosition = position / m_cellSize - Vector3(0.5f, 0.5f, 0.5f);
      const Vector3i floorPos(int(std::floor(cellPosition.x)), int(std::floor(cellPosition.y)), int(std::floor(cellPosition.z)));

      for (const Vector3i& offset : kOffsets) {
        auto iter = m_cache.find(floorPos + offset);
        if (iter != m_cache.end()) {
          const std::vector<T>* value = &iter->second;
          result.push_back(value);
        }
      }
      
      return result;
    };
    
    void insert(const Vector3& position, T data) {
      insert(getCellPos(position), data);
    }

    void erase(const Vector3& position, T data) {
      erase(getCellPos(position), data);
    }

    void move(const Vector3& oldPosition, const Vector3& newPosition, T data) {
      Vector3i oldPos = getCellPos(oldPosition);
      Vector3i newPos = getCellPos(newPosition);
      if (oldPos != newPos) {
        erase(oldPosition, data);
        insert(newPos, data);
      }
    }

    const fast_spatial_cache<std::vector<T>>& getAll() {
      return m_cache;
    }

  private:

    Vector3i getCellPos(const Vector3& position) const {
      const Vector3 scaledPos = position / m_cellSize;
      return Vector3i(int(std::floor(scaledPos.x)), int(std::floor(scaledPos.y)), int(std::floor(scaledPos.z))); 
    }

    void insert(const Vector3i& pos, T data) {
      m_cache[pos].push_back(data);
    }

    void erase(const Vector3i& pos, T data) {
      auto cellIter = m_cache.find(pos);
      if (cellIter == m_cache.end()) {
        Logger::err("Specified cell was already empty in SpatialMap::erase().");
        return;
      }

      std::vector<T>& cell = cellIter->second;
      auto iter = std::find(cell.begin(), cell.end(), data);
      if (iter != cell.end()) {
        if (cell.size() > 1) {
          // Swap & pop - faster than "erase", but doesn't preserve order, which is fine here.
          std::swap(*iter, cell.back());
          cell.pop_back();
        } else {
          m_cache.erase(cellIter);
        }
      } else {
        Logger::err("Couldn't find matching data in SpatialMap::erase().");
      }
    }

    float m_cellSize;
    fast_spatial_cache<std::vector<T>> m_cache;
  };
}
