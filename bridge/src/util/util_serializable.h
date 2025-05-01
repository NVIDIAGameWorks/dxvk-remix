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

#include <assert.h>
#include <stdint.h>
#include <array>

namespace bridge_util {
  
template<typename T, bool HasStaticSize>
class Serializable : public T {
public:
  using BaseT = T;
  static inline constexpr bool bHasStaticSize = HasStaticSize;

  Serializable() {} // Allow non-functional placeholder Serializables
  Serializable(const Serializable& other) = delete; // Deep copy would be necessary, probably overkill
  Serializable(Serializable&& other) {
    *this = std::move(other);
  }
    
  // Serializing ctor
  Serializable(const BaseT& serializeMe)
    : kType(Serialize)
    , BaseT(serializeMe)
    , m_kSize(initSize()) {}
  // Deserializing ctor
  Serializable(void* pDeserializeMe)
    : kType(Deserialize)
    , m_pDeserializeMe(pDeserializeMe)
    , m_kSize(initSize(pDeserializeMe)) {}

  ~Serializable() {
    // Static sized struct implies POD with no variable length pointers.
    // Trivial implicit dtor sufficient
    if constexpr (!bHasStaticSize) {
      // User code that serializes a given struct is in charge of freeing relevant memory
      if(kType == Deserialize) {
        _dtor();
      }
    }
  }
  
  Serializable& operator=(Serializable&& other) {
    Serializable::BaseT::operator=(other);
    kType = other.kType;
    m_kSize = other.m_kSize;
    m_pDeserializeMe = other.m_pDeserializeMe;
    other.kType = Invalid;
    other.m_kSize = 0;
    other.m_pDeserializeMe = nullptr;
    memset(static_cast<BaseT*>(&other), 0x0, sizeof(BaseT));
    return *this;
  }

  inline uint32_t size() const {
    if constexpr (bHasStaticSize) {
      return s_kSize;
    } else {
      return m_kSize;
    }
  };
  uint32_t calcSize() const {
    return _calcSize() + sizeof(uint32_t); // calculated size of type + size storage
  }
  void serialize(void* const pSerializeBegin) const {
    assert(kType == Serialize && "[serialize] Coding error: This serializable type was constructed for deserializing!");
    const auto startPos = reinterpret_cast<uintptr_t>(pSerializeBegin);
    void* pSerialize = pSerializeBegin;
    bridge_util::serialize(size(), pSerialize);
    _serialize(pSerialize);
    const auto endPos = reinterpret_cast<uintptr_t>(pSerialize);
    assert((endPos - startPos) == size());
  }
  void deserialize() {
    assert(kType == Deserialize && "[deserialize] Coding error: This serializable type was constructed for serializing!");
    auto startPos = reinterpret_cast<uintptr_t>(m_pDeserializeMe);
    void* pDeserialize = m_pDeserializeMe;
    uint32_t deserializedSize = 0;
    bridge_util::deserialize(pDeserialize, deserializedSize);
    assert(deserializedSize == size());
    _deserialize(pDeserialize);
    const auto endPos = reinterpret_cast<uintptr_t>(pDeserialize);
    assert((endPos - startPos) == size());
  }

private:
  uint32_t _calcSize() const; // Implement me
  void _serialize(void*& pSerialize) const; // Implement me
  void _deserialize(void*& pDeserialize); // Implement me
  void _dtor(); // Implement me

  uint32_t initSize(const void* deserializeMe = nullptr) const {
    if constexpr (bHasStaticSize) {
      return s_kSize;
    } else {
      if(deserializeMe) {
        return *(const uint32_t*)deserializeMe;
      } else {
        return calcSize();
      }
    }
  }
  static inline uint32_t initStaticSize() {
    if constexpr (bHasStaticSize) {
      return calcStaticSize();
    } else {
      return 0;
    }
  }
  static uint32_t calcStaticSize() {
    static_assert(bHasStaticSize);
    BaseT dummyA;
    Serializable<BaseT,true> dummyB(dummyA);
    return dummyB.calcSize();
  }

  enum Type {
    Invalid,
    Serialize,
    Deserialize
  } kType = Invalid;
  void* m_pDeserializeMe = nullptr;
  uint32_t m_kSize = 0;
public:
  static inline const uint32_t s_kSize = initStaticSize();
};

template< typename T >
using underlying = std::remove_cv_t< std::remove_pointer_t< std::remove_reference_t< std::remove_cv_t< std::remove_all_extents_t< T > > > > >;
// Double std::remove_cv_t necessary because prior to removing the pointer,
// std::remove_cv_t only affects ptr constness

// Would just use is_arithmetic, but need to include enums.
// Would use is_scalar, but don't want to include pointers.
template<class T>
struct is_default_sizeOf_allowed : std::integral_constant<bool,
  // Cover basic scalars of valid types
  std::is_arithmetic_v<underlying<T>> || std::is_enum_v<underlying<T>> >{};
template<class T>
inline constexpr bool is_default_sizeOf_allowed_v = is_default_sizeOf_allowed<T>::value;


// sizeOf(...) templates
// Convenience implementations for simple types
// e.g. integral, floats, enums, arrays thereof
template<typename T>
static inline constexpr uint32_t sizeOf() {
  static_assert(bridge_util::is_default_sizeOf_allowed_v<T>, "Default constexpr variant of \"sizeOf()\" can only be used for integral, floating point, and enum types.");
  return sizeof(T);
}
template<typename T>
static inline uint32_t sizeOf(const T& obj) {
  return sizeOf<T>();
}

// serialize(...)
// The core serializing function. memcpys from one ptr to another, and increments the
//   pointer *being copied to* by size copied.
static void serialize(const void* const serializeFrom, void*& serializeTo, const uint32_t size) {
  std::memcpy(serializeTo, serializeFrom, size);
  (reinterpret_cast<uintptr_t&>(serializeTo)) += size;
}

// deserialize(...)
// The core deserializing function. memcpys from one ptr to another, and increments the
//   pointer *being copied from* by size copied.
static void deserialize(void*& deserializeFrom, void* const deserializeTo, const uint32_t size) {
  std::memcpy(deserializeTo, deserializeFrom, size);
  (reinterpret_cast<uintptr_t&>(deserializeFrom)) += size;
}

// Convenience templated function for serializing simply laid-out types
// with defined sizeOf()
template<typename T>
void serialize(const T& serializeFrom, void*& serializeTo) {
  static_assert(!std::is_pointer_v<T>, "Implement a specialization for the pointer type in question OR pass in the type pointed to.");
  serialize(&serializeFrom, serializeTo, sizeOf<T>(serializeFrom));
}
// Convenience templated function for deserializing simply laid-out types
// with defined sizeOf()
template<typename T>
void deserialize(void*& deserializeFrom, T& deserializeTo) {
  static_assert(!std::is_const_v<T>, "Cannot deserialize to a const type. Implement a specialization or correct the struct being deserialized.");
  static_assert(!std::is_pointer_v<T>, "Implement a specialization for the pointer type in question OR pass in the type pointed to.");
  deserialize(deserializeFrom, &deserializeTo, sizeOf<T>(deserializeTo));
}
// NOTE: Ensure that non-integral/-float types are not casually de-/serialized 
// sans an explicit size parameter UNLESS a user-defined sizeOf implementation
// exists. `sizeOf()` implementation should fire a static_assert accordingly.


// Size of bool is compiler implementation specific, so we should
// ensure that it is locked to a given size across architectures
enum class Bool : uint8_t {
  False = 0,
  True = 0xff
};
template<>
static inline constexpr uint32_t sizeOf<bool>() {
  return sizeof(Bool);
}
template<>
static inline void serialize(const bool& serializeFrom, void*& serializeTo) {
  const Bool b = (serializeFrom) ? Bool::True : Bool::False;
  serialize(&b, serializeTo, sizeOf<Bool>());
}
template<>
static inline void deserialize(void*& deserializeFrom, bool& deserializeTo) {
  Bool b = Bool::True;
  deserialize(deserializeFrom, &b, sizeOf<Bool>());
  deserializeTo = (b == Bool::True);
}


// https://stackoverflow.com/a/31763111
template<typename T>
struct is_serializable // Default case, no pattern match
    : std::false_type {};
template<typename SpecializationT, bool Bool>
struct is_serializable< bridge_util::Serializable<SpecializationT, Bool> > // For types matching the pattern A<T>
    : std::true_type {};
template<typename T>
inline constexpr bool is_serializable_v = is_serializable<T>::value;

// Use below helpers to quickly define Serializables that only have
//   members whose sizes are determined trivially. For example, if
//   a struct's member points to a variable-sized blob of memory, then
//   you should not use these and must define calcSize(), serialize(),
//   and deserialize() explicitly.
//   See util_remixapi.h/.cpp for examples
namespace fold_helper {

template<typename... Types>
static uint32_t calcSize(const Types&... args) {
  return (bridge_util::sizeOf(args) + ...);
}

template<typename... Types>
static void serialize(void*& pSerialize, const Types&... args) {
  (bridge_util::serialize(args, pSerialize) , ...);
}

template<typename... Types>
static void deserialize(void*& pDeserialize, Types&... args) {
  (bridge_util::deserialize(pDeserialize, args), ...);
}

}

}
