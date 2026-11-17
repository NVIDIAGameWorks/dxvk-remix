/*
* Copyright (c) 2026, NVIDIA CORPORATION. All rights reserved.
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

#include <cstdint>
#include <limits>
#include <type_traits>
#include <utility>
#include <vector>

namespace dxvk {

  template<typename Tag>
  struct GenerationArrayId {
    uint32_t m_arrayIndex = std::numeric_limits<uint32_t>::max();
    uint32_t m_generation = 0;

    explicit operator bool() const {
      return m_generation != 0;
    }


    bool operator==(const GenerationArrayId& other) const {
      return m_arrayIndex == other.m_arrayIndex
          && m_generation == other.m_generation;
    }


    bool operator!=(const GenerationArrayId& other) const {
      return !(*this == other);
    }
  };


  // Fixed-capacity generational storage that rejects stale IDs.
  // A slot moves from Available to Live on allocId, to FreeQueued on freeId,
  // to GpuPending on processFree, and back to Available on completeFree.
  // Freed values remain intact until GPU-side use has completed.
  // Slots become reusable only after completeFree increments their generation.
  // Callers must provide external synchronization.
  template<typename Id, typename T>
  struct GenerationArray {
    explicit GenerationArray(uint32_t capacity)
      : m_generations(capacity, 1)
      , m_values(capacity)
      , m_states(capacity, State::Available)
      , m_availableIndices(capacity) {
      for (uint32_t i = 0; i < capacity; i++) {
        m_availableIndices[i] = capacity - i - 1;
      }
    }
    ~GenerationArray() = default;

    GenerationArray(const GenerationArray&) = delete;
    GenerationArray& operator=(const GenerationArray&) = delete;
    GenerationArray(GenerationArray&&) = delete;
    GenerationArray& operator=(GenerationArray&&) = delete;


    Id allocId() {
      if (m_availableIndices.empty()) {
        return {};
      }
      const uint32_t index = m_availableIndices.back();
      m_availableIndices.pop_back();
      m_values[index] = T {};
      m_states[index] = State::Live;
      return Id { index, m_generations[index] };
    }


    T* lset(Id id, T value = {}) {
      if (!matches(id, State::Live)) {
        return nullptr;
      }
      m_values[id.m_arrayIndex] = std::move(value);
      return &m_values[id.m_arrayIndex];
    }


    T* lget(Id id) {
      return matches(id, State::Live) ? &m_values[id.m_arrayIndex] : nullptr;
    }
    const T* lget(Id id) const {
      return matches(id, State::Live) ? &m_values[id.m_arrayIndex] : nullptr;
    }


    uint32_t capacity() const {
      return uint32_t(m_states.size());
    }


    Id liveIdAt(uint32_t index) const {
      return index < m_states.size() && m_states[index] == State::Live
        ? Id { index, m_generations[index] }
        : Id {};
    }

    // Calls fn(id, value) for every slot that is not Available.
    // The ID is empty for slots whose free is still pending.
    template<typename Fn>
    void forEachAllocated(Fn&& fn) {
      for (uint32_t index = 0; index < m_states.size(); index++) {
        if (m_states[index] == State::Available) {
          continue;
        }
        const Id liveId = m_states[index] == State::Live
          ? Id { index, m_generations[index] }
          : Id {};
        fn(liveId, m_values[index]);
      }
    }


    bool empty() const {
      return m_availableIndices.size() == m_states.size();
    }


    bool freeId(Id id) {
      if (!matches(id, State::Live)) {
        return false;
      }
      m_states[id.m_arrayIndex] = State::FreeQueued;
      return true;
    }


    T* processFree(Id id) {
      if (!matches(id, State::FreeQueued)) {
        return nullptr;
      }
      m_states[id.m_arrayIndex] = State::GpuPending;
      return &m_values[id.m_arrayIndex];
    }


    T completeFree(Id id) {
      if (!matches(id, State::GpuPending)) {
        return {};
      }
      T value = std::move(m_values[id.m_arrayIndex]);
      m_values[id.m_arrayIndex] = T {};
      if (++m_generations[id.m_arrayIndex] == 0) {
        m_generations[id.m_arrayIndex] = 1;
      }
      m_states[id.m_arrayIndex] = State::Available;
      m_availableIndices.push_back(id.m_arrayIndex);
      return value;
    }


    void clear() {
      const uint32_t capacity = uint32_t(m_states.size());
      m_availableIndices.resize(capacity);
      for (uint32_t i = 0; i < capacity; i++) {
        m_values[i] = T {};
        if (m_states[i] != State::Available
            && ++m_generations[i] == 0) {
          m_generations[i] = 1;
        }
        m_states[i] = State::Available;
        m_availableIndices[i] = capacity - i - 1;
      }
    }


  private:
    enum class State : uint8_t {
      Available,
      Live,
      FreeQueued,
      GpuPending,
    };


    bool matches(Id id, State state) const {
      return id.m_arrayIndex < m_states.size()
          && m_generations[id.m_arrayIndex] == id.m_generation
          && m_states[id.m_arrayIndex] == state;
    }


    std::vector<uint32_t> m_generations;
    std::vector<T>        m_values;
    std::vector<State>    m_states;
    std::vector<uint32_t> m_availableIndices;
  };


  using BlasId = GenerationArrayId<struct BlasIdTag>;
  using MaterialId = GenerationArrayId<struct MaterialIdTag>;
  using SurfaceId = GenerationArrayId<struct SurfaceIdTag>;


  // Packs an ID into a 64-bit API handle as generation << 32 | (index + 1).
  // A zero handle never names a valid ID.
  template<typename Handle, typename Id>
  Handle packGenerationArrayId(Id id) {
    static_assert(std::is_pointer_v<Handle>
      && sizeof(Handle) == sizeof(uint64_t));
    if (!id) {
      return {};
    }
    const uint64_t value =
      uint64_t(id.m_generation) << 32 | uint64_t(id.m_arrayIndex + 1);
    return reinterpret_cast<Handle>(static_cast<uintptr_t>(value));
  }


  template<typename Id, typename Handle>
  Id unpackGenerationArrayId(Handle handle) {
    static_assert(std::is_pointer_v<Handle>
      && sizeof(Handle) == sizeof(uint64_t));
    const uint64_t value = reinterpret_cast<uintptr_t>(handle);
    if (value == 0 || uint32_t(value) == 0) {
      return {};
    }
    return Id { uint32_t(value) - 1, uint32_t(value >> 32) };
  }

}  // namespace dxvk
