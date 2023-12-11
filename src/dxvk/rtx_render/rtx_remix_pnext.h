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

#include <remix/remix_c.h>

#include <cassert>
#include <type_traits>
#include <vector>

namespace pnext{
  namespace detail {
    template< typename... Types >
    struct TypeList { };

    struct AnyInfoPrototype {
      remixapi_StructType sType;
      void* pNext;
    };

    template< typename T >
    auto getStructType(const T* info) noexcept -> remixapi_StructType {
      if (info) {
        return reinterpret_cast< const AnyInfoPrototype* >(info)->sType;
      }
      return REMIXAPI_STRUCT_TYPE_NONE;
    }

    // If SourcePtr is const, then add const to TargetPtr
    template< typename TargetPtr, typename Source > // requires(std::is_pointer_v< TargetPtr > && !std::is_pointer_v< Source >)
    using TryConst = std::conditional_t< std::is_const_v< Source >,
                                         std::add_pointer_t< std::add_const_t<    std::remove_pointer_t< TargetPtr > > >,
                                         std::add_pointer_t< std::remove_const_t< std::remove_pointer_t< TargetPtr > > > >;

    template< typename T >
    TryConst< void*, T > getPNext(T* info) noexcept {
      if (info) {
        return reinterpret_cast< TryConst< AnyInfoPrototype*, T > >(info)->pNext;
      }
      return nullptr;
    }

    template< typename T >
    T* non_const(const T* v) {
      return const_cast< T* >(v);
    }
  }
}

// User-defined specializations for remixapi types
#include "rtx_remix_specialization.inl"

namespace pnext {
  namespace detail {
    template< typename T >
    using Underlying = std::remove_pointer_t< std::remove_reference_t< std::remove_cv_t< T > > >;

    template< typename T >
    using RootOf = typename Root< T >::Type;

    template< typename EXT, typename Base >
    constexpr bool CanBeLinkedTo = std::is_same_v< RootOf< Underlying< EXT > >,
                                                   Underlying< Base > >;

#ifdef RTX_REMIX_PNEXT_CHECK_STRUCTS
    namespace helper {
      template< typename T, typename = void, typename = void >
      struct checkMembers_t {
        static constexpr bool hasMembers = false;
      };

      template< typename T >
      struct checkMembers_t< T, std::void_t< decltype( T::sType ) >,
                                std::void_t< decltype( T::pNext ) > > {
        static constexpr bool hasMembers =
          std::is_same_v< decltype( T::sType ), remixapi_StructType > &&
          std::is_same_v< decltype( T::pNext),  void* > &&
          offsetof(AnyInfoPrototype, sType) == offsetof(T, sType) && sizeof(T::sType) == sizeof(AnyInfoPrototype::sType) &&
          offsetof(AnyInfoPrototype, pNext) == offsetof(T, pNext) && sizeof(T::pNext) == sizeof(AnyInfoPrototype::pNext);
      };

      template< typename T >
      constexpr bool HasSTypePNext = checkMembers_t< T >::hasMembers;


      template< typename T, typename... Types >
      struct hasUniqueId_t : std::true_type { };

      template< typename T, typename U, typename... Rest >
      struct hasUniqueId_t< T, TypeList< U, Rest... > >
        // check that StructType enum is unique for types
        : std::conditional_t< !std::is_same_v< T, U > && ToEnum< T > == ToEnum< U >,
                              std::false_type,                          // found a duplicate
                              hasUniqueId_t< T, TypeList< Rest... > > > // continue
      { };

      template< typename T >
      constexpr bool HasUniqueId = hasUniqueId_t< T, AllTypes >::value;


      template< typename T >
      constexpr void checkStruct() {
        static_assert(ToEnum< T > != REMIXAPI_STRUCT_TYPE_NONE, "ToEnum must be specialized for this type");
        static_assert(std::is_same_v< decltype(ToEnum< T >), const remixapi_StructType >, "ToEnum must be remixapi_StructType");
        static_assert(HasSTypePNext< T >, "Struct must contain sType (remixapi_StructType) and pNext (void*)");
        static_assert(HasUniqueId< T >, "Please, recheck StructType enum for duplicates");
      }

      template < typename... Types >
      constexpr bool checkAllTypes(TypeList< Types... >)
      {
        (checkStruct< Types >(), ...);
        return true;
      }

      static_assert(checkAllTypes(AllTypes {}));
    }
#endif
  }

  template< typename T,
            typename Root,
            std::enable_if_t<
              detail::ToEnum< T > != REMIXAPI_STRUCT_TYPE_NONE && detail::CanBeLinkedTo< T, Root >
              , int > = 0 >
  detail::TryConst< T*, Root > find(Root* listStart) noexcept {
    // NOTE: if compilation fails here, please ensure the structure is defined in rules: rtx_remix_specialization.inl
    auto next = static_cast< detail::TryConst< void*, Root > >(listStart);
    while (next) {
      remixapi_StructType sType = detail::getStructType(next);
      if (sType == detail::ToEnum< T >) {
        return static_cast< detail::TryConst< T*, Root > >(next);
      }
      if (sType == REMIXAPI_STRUCT_TYPE_NONE) {
        // debug::Error( "Found sType=REMIXAPI_STRUCT_TYPE_NONE on {:#x}", uint64_t( next ) );
        assert(0);
        break;
      }
      next = detail::getPNext(next);
    }

    return nullptr;
  }
} // namespace pnext
