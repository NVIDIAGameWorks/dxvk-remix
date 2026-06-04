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
// results.  The helpers in this file convert "no padding bytes" from an
// invariant the author has to remember into one the compiler proves on every
// build, by requiring the call site to enumerate every non-static data member
// of the struct.
//
// All checks in this header are driven by the *types* in the member-pointer
// pack (via sizeof(Ms)), never by the runtime pointer values.  The pointer
// values themselves are unused -- they exist solely so the call site reads
// naturally as a list of `&T::field` and the compiler can deduce `Ms...`.

namespace dxvk {

  // Returns true iff T has no implicit padding bytes, given that the supplied
  // member-pointer pack enumerates every non-static data member of T.
  //
  // Intended for use inside a static_assert at the point T is declared.
  // The pack does not need to be in declaration order; only the sum of member
  // sizes is compared against sizeof(T).
  //
  // Example:
  //   struct Foo { uint64_t a; uint32_t b; uint32_t c; };
  //   static_assert(hasNoImplicitPadding<Foo>(&Foo::a, &Foo::b, &Foo::c),
  //                 "Foo has padding bytes");
  template <typename T, typename... Ms>
  constexpr bool hasNoImplicitPadding(Ms T::*...) {
    return sizeof(T) == (size_t{0} + ... + sizeof(Ms));
  }

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
  template <typename T, typename... Ms>
  XXH64_hash_t hashStructByMemory(const T& data, Ms T::*...) {
    static_assert(std::is_standard_layout_v<T>,
                  "Type must be standard-layout to hash as a memory range.");
    static_assert(std::is_trivially_copyable_v<T>,
                  "Type must be trivially copyable to hash as a memory range.");
    static_assert(sizeof(T) == (size_t{0} + ... + sizeof(Ms)),
                  "sizeof(T) does not match the sum of the listed member "
                  "sizes. Either a non-static data member of T was omitted "
                  "from this call, or T contains implicit padding bytes "
                  "(reorder fields by descending alignment, or add explicit "
                  "padding members).");
    return XXH3_64bits(&data, sizeof(T));
  }

  // Hashes `count` contiguous T values as a single byte range via XXH3, with
  // the same compile-time safety checks as hashStructByMemory.  Useful when
  // accumulating trivially-copyable structs into a vector and hashing them
  // all at once.
  template <typename T, typename... Ms>
  XXH64_hash_t hashStructArrayByMemory(const T* data, size_t count, Ms T::*...) {
    static_assert(std::is_standard_layout_v<T>,
                  "Type must be standard-layout to hash as a memory range.");
    static_assert(std::is_trivially_copyable_v<T>,
                  "Type must be trivially copyable to hash as a memory range.");
    static_assert(sizeof(T) == (size_t{0} + ... + sizeof(Ms)),
                  "sizeof(T) does not match the sum of the listed member "
                  "sizes. Either a non-static data member of T was omitted "
                  "from this call, or T contains implicit padding bytes "
                  "(reorder fields by descending alignment, or add explicit "
                  "padding members).");
    return XXH3_64bits(data, count * sizeof(T));
  }

}
