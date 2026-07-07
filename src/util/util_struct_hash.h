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

#include <cstddef>
#include <type_traits>

#include "xxHash/xxhash.h"

// Utilities for safely hashing a struct as a contiguous byte range.
//
// Hashing `XXH3_64bits(&value, sizeof(value))` is only well-defined when the
// struct has no uninitialized bytes -- otherwise implicit padding between
// members poisons the hash with stack garbage and produces non-deterministic
// results.  hashStructByMemory() converts "no padding bytes" from an invariant
// the author has to remember into one the compiler proves on every build, by
// requiring the call site to enumerate every non-static data member of the
// struct as non-type template parameters.

namespace dxvk {

namespace hashStructHelpers {

  template <auto MemberPtr>
  struct memberDataSize;

  template <typename T, typename M, M T::* Ptr>
  struct memberDataSize<Ptr> {
    static constexpr size_t value = sizeof(M);
  };

  // Compile-time checks only. Instantiating this type triggers static_asserts
  // but emits no runtime code when referenced from static_assert.
  template <typename T, auto... MemberPtrs>
  struct hashStructByMemoryChecks {
    static_assert(std::is_standard_layout_v<T>,
                  "Type must be standard-layout to hash as a memory range.");
    static_assert(std::is_trivially_copyable_v<T>,
                  "Type must be trivially copyable to hash as a memory range.");
    static_assert(sizeof(T) == (size_t{0} + ... + memberDataSize<MemberPtrs>::value),
                  "sizeof(T) does not match the sum of the listed member "
                  "sizes. Either a non-static data member of T was omitted "
                  "from this call, or T contains implicit padding bytes "
                  "(reorder fields by descending alignment, or add explicit "
                  "padding members).");
    static constexpr bool value = true;
  };

} // namespace hashStructHelpers

// Hashes a single T as a contiguous byte range via XXH3, with compile-time
// safety checks:
//   * T is standard-layout              (well-defined memory layout)
//   * T is trivially copyable           (memcpy semantics)
//   * sizeof(T) equals the sum of the member sizes (no implicit padding,
//     given the supplied member-pointer pack enumerates every non-static
//     data member of T)
//
// Any violation produces a localized compile error pointing at the call.
//
// The caller is still responsible for value-initializing the object
// (e.g. `Foo data{};`) so that any field forgotten in the data-fill code
// is zero rather than stack garbage.
//
// Example:
//   return hashStructByMemory<Foo, &Foo::a, &Foo::b, &Foo::c>(data);
template <typename T, auto... MemberPtrs>
inline XXH64_hash_t hashStructByMemory(const T& data) {
  static_assert(hashStructHelpers::hashStructByMemoryChecks<T, MemberPtrs...>::value);
  return XXH3_64bits(&data, sizeof(T));
}

// Hashes `count` contiguous T values as a single byte range via XXH3, with
// the same compile-time safety checks as hashStructByMemory().  Useful when
// accumulating trivially-copyable structs into a vector and hashing them
// all at once.
//
// Example:
//   return hashStructArrayByMemory<Foo, &Foo::a, &Foo::b, &Foo::c>(data, count);
template <typename T, auto... MemberPtrs>
inline XXH64_hash_t hashStructArrayByMemory(const T* data, size_t count) {
  static_assert(hashStructHelpers::hashStructByMemoryChecks<T, MemberPtrs...>::value);
  return XXH3_64bits(data, count * sizeof(T));
}

} // namespace dxvk
