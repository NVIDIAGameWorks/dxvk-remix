/*
* Copyright (c) 2025, NVIDIA CORPORATION. All rights reserved.
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

// Flexible Type System for Graph Components
//
// This file provides utilities for creating graph components with flexible types
// (e.g., NumberOrVector) that automatically resolve to concrete types at runtime.
//
// USAGE:
//
// For binary operations that compute a result type (Add, Multiply, etc.):
//
//   DEFINE_BINARY_OP_COMPONENT_CPP(Multiply, std::declval<A>() * std::declval<B>(), RtComponentPropertyNumberOrVector)
//
// For comparison operations that always return bool (LessThan, EqualTo, etc.):
//
//   DEFINE_COMPARISON_OP_COMPONENT_CPP(LessThan, std::declval<A>() < std::declval<B>(), RtComponentPropertyNumber)
//
// These macros automatically:
// - Define type checking logic to verify the operation is syntactically valid
// - Instantiate all valid type combinations from the specified variant
// - Compute result types for binary operations
// - Create a registration function (createTypeVariantsFor<ComponentName>) to trigger instantiation
//
// NOTE: This will accept all syntactically valid operations, including those that may
// cause narrowing conversions (e.g., Vector3 * float, uint64_t * float).

#pragma once

#include <type_traits>
#include "../../util/util_vector.h"
#include "rtx_graph_types.h"

namespace dxvk {

// Use SFINAE (https://en.cppreference.com/w/cpp/language/sfinae.html) to check if A op B is syntactically valid
// OpCheck should be a template like: template<typename A, typename B> using OpCheck = decltype(std::declval<A>() + std::declval<B>());
template<typename A, typename B, template<typename, typename> typename OpCheck, typename = void>
struct IsBinaryOpValid : std::false_type {};

template<typename A, typename B, template<typename, typename> typename OpCheck>
struct IsBinaryOpValid<A, B, OpCheck, std::void_t<OpCheck<A, B>>> : std::true_type {};

// Generic helper to instantiate a binary operation component (with result type computation)
// ComponentTemplate should be a template like: template<RtComponentPropertyType A, RtComponentPropertyType B, RtComponentPropertyType R>
template<
  template<RtComponentPropertyType, RtComponentPropertyType, RtComponentPropertyType> typename ComponentTemplate,
  template<typename, typename> typename OpCheck,
  template<typename, typename> typename ValidCheck,
  typename AElemType, 
  typename BElemType
>
constexpr void instantiateBinaryOpIfValid() {
  if constexpr (ValidCheck<AElemType, BElemType>::value) {
    constexpr auto AType = CppTypeToPropertyType<AElemType>::value;
    constexpr auto BType = CppTypeToPropertyType<BElemType>::value;
    using ResultType = OpCheck<AElemType, BElemType>;
    constexpr auto ResultPropertyType = CppTypeToPropertyType<ResultType>::value;
    (void)sizeof(ComponentTemplate<AType, BType, ResultPropertyType>);
  }
}

// Generic helper to instantiate a comparison component (no result type, always returns bool)
// ComponentTemplate should be a template like: template<RtComponentPropertyType A, RtComponentPropertyType B>
template<
  template<RtComponentPropertyType, RtComponentPropertyType> typename ComponentTemplate,
  template<typename, typename> typename ValidCheck,
  typename AElemType, 
  typename BElemType
>
constexpr void instantiateComparisonOpIfValid() {
  if constexpr (ValidCheck<AElemType, BElemType>::value) {
    constexpr auto AType = CppTypeToPropertyType<AElemType>::value;
    constexpr auto BType = CppTypeToPropertyType<BElemType>::value;
    (void)sizeof(ComponentTemplate<AType, BType>);
  }
}

// Binary operation component macro (for operations that return computed types like Add, Multiply)
//
// This macro automates the following steps:
// 1. Defines an operation check: ComponentName##Check = decltype(OperationExpr)
// 2. Creates a validation alias: Is##ComponentName##Valid using IsBinaryOpValid
// 3. Instantiates valid combinations using instantiateBinaryOpIfValid (which computes result types)
// 4. Iterates through all type combinations from VariantType
// 5. Creates a registration function: createTypeVariantsFor##ComponentName()
//
// Parameters:
//   ComponentName - The name of the component class (e.g., Add, Multiply)
//   OperationExpr - The operation expression using A and B (e.g., std::declval<A>() + std::declval<B>())
//   VariantType   - The variant type containing all allowed types (e.g., RtComponentPropertyNumberOrVector)
//
// Usage: DEFINE_BINARY_OP_COMPONENT_CPP(Add, std::declval<A>() + std::declval<B>(), RtComponentPropertyNumberOrVector)
#define DEFINE_BINARY_OP_COMPONENT_CPP(ComponentName, OperationExpr, VariantType) \
  namespace { \
    template<typename A, typename B> \
    using ComponentName##Check = decltype(OperationExpr); \
    \
    template<typename A, typename B> \
    using Is##ComponentName##Valid = IsBinaryOpValid<A, B, ComponentName##Check>; \
    \
    template<typename AElemType, typename BElemType> \
    constexpr void instantiate##ComponentName##IfValid() { \
      instantiateBinaryOpIfValid<ComponentName, ComponentName##Check, Is##ComponentName##Valid, AElemType, BElemType>(); \
    } \
    \
    template<typename AElemType, typename... BTypes> \
    constexpr void instantiate##ComponentName##ForAllB() { \
      (instantiate##ComponentName##IfValid<AElemType, BTypes>(), ...); \
    } \
    \
    template<typename... AllTypes> \
    constexpr void instantiateAll##ComponentName(std::variant<AllTypes...>*) { \
      (instantiate##ComponentName##ForAllB<AllTypes, AllTypes...>(), ...); \
    } \
    \
    static const auto ComponentName##Instantiations = (instantiateAll##ComponentName((VariantType*)nullptr), 0); \
  } \
  \
  void createTypeVariantsFor##ComponentName() { \
    (void)ComponentName##Instantiations; \
  }

// Comparison operation component macro (for operations that return bool like LessThan, EqualTo)
//
// Similar to DEFINE_BINARY_OP_COMPONENT_CPP, but for operations that always return bool.
// This macro automates the following steps:
// 1. Defines an operation check: ComponentName##Check = decltype(OperationExpr)
// 2. Creates a validation alias: Is##ComponentName##Valid using IsBinaryOpValid
// 3. Instantiates valid combinations using instantiateComparisonOpIfValid (no result type computation)
// 4. Iterates through all type combinations from VariantType
// 5. Creates a registration function: createTypeVariantsFor##ComponentName()
//
// Parameters:
//   ComponentName - The name of the component class (e.g., LessThan, EqualTo)
//   OperationExpr - The operation expression using A and B (e.g., std::declval<A>() < std::declval<B>())
//   VariantType   - The variant type containing all allowed types (e.g., RtComponentPropertyNumber)
//
// Usage: DEFINE_COMPARISON_OP_COMPONENT_CPP(LessThan, std::declval<A>() < std::declval<B>(), RtComponentPropertyNumber)
#define DEFINE_COMPARISON_OP_COMPONENT_CPP(ComponentName, OperationExpr, VariantType) \
  namespace { \
    template<typename A, typename B> \
    using ComponentName##Check = decltype(OperationExpr); \
    \
    template<typename A, typename B> \
    using Is##ComponentName##Valid = IsBinaryOpValid<A, B, ComponentName##Check>; \
    \
    template<typename AElemType, typename BElemType> \
    constexpr void instantiate##ComponentName##IfValid() { \
      instantiateComparisonOpIfValid<ComponentName, Is##ComponentName##Valid, AElemType, BElemType>(); \
    } \
    \
    template<typename AElemType, typename... BTypes> \
    constexpr void instantiate##ComponentName##ForAllB() { \
      (instantiate##ComponentName##IfValid<AElemType, BTypes>(), ...); \
    } \
    \
    template<typename... AllTypes> \
    constexpr void instantiateAll##ComponentName(std::variant<AllTypes...>*) { \
      (instantiate##ComponentName##ForAllB<AllTypes, AllTypes...>(), ...); \
    } \
    \
    static const auto ComponentName##Instantiations = (instantiateAll##ComponentName((VariantType*)nullptr), 0); \
  } \
  \
  void createTypeVariantsFor##ComponentName() { \
    (void)ComponentName##Instantiations; \
  }

}  // namespace dxvk
