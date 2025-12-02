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

#pragma once
#include <variant>
#include <vector>
#include <string>
#include <string_view>

#include "dxvk_context.h"
#include "../util/util_fast_cache.h"
#include "../util/util_vector.h"
#include "../util/xxHash/xxhash.h"

#include "../rtx_types.h"

namespace dxvk {

enum class RtComponentPropertyIOType {
  Input,
  State,
  Output

  // NOTE: Places to change when adding a new case:
  //   RtComponentPropertyIOType's operator << function in rtx_graph_types.cpp,
  //   Macros in rtx_graph_node_macros.h,
};
std::ostream& operator << (std::ostream& os, RtComponentPropertyIOType type);


// Struct to allow for passing and storing references to a specific RtInstance or RtLight.
// In USD, these are represented as relationships to a prim within the same mesh replacement.
// TODO figure out rules for referencing lights from outside that light replacement.
struct PrimTarget {
  // NOTE: don't use this directly.  Pass it to `m_batch.resolvePrimTarget(context, i, m_target[i])` instead.
  uint32_t replacementIndex = ReplacementInstance::kInvalidReplacementIndex; // The index of the prim within the replacement instance.
  uint64_t instanceId = kInvalidInstanceId; // The ID of the instance in the replacement manager

  bool operator==(const PrimTarget& other) const {
    return replacementIndex == other.replacementIndex && instanceId == other.instanceId;
  }

  bool operator!=(const PrimTarget& other) const {
    return !(*this == other);
  }

  // Note: The ordering operators below don't represent a semantic ordering of prim targets.
  // They exist solely to satisfy std::variant comparison requirements, allowing PrimTarget
  // to be stored in RtComponentPropertyValue. The ordering is arbitrary but consistent.
  bool operator<(const PrimTarget& other) const {
    if (instanceId != other.instanceId) {
      return instanceId < other.instanceId;
    }
    return replacementIndex < other.replacementIndex;
  }

  bool operator<=(const PrimTarget& other) const {
    return *this < other || *this == other;
  }

  bool operator>(const PrimTarget& other) const {
    return !(*this <= other);
  }

  bool operator>=(const PrimTarget& other) const {
    return !(*this < other);
  }
};
static constexpr PrimTarget kInvalidPrimTarget = { ReplacementInstance::kInvalidReplacementIndex, kInvalidInstanceId };

enum class RtComponentPropertyType {
  Bool,
  Float,
  Float2,
  Float3,
  Float4,
  Enum,
  String,
  AssetPath,
  Hash,

  // Default Value is ignored for relationships. It's safe to just use 0.
  Prim,

  // Flexible types
  Any, // Can be any of the above types
  NumberOrVector,

  // TODO should we support lists of any of the above types.

  // NOTE: Places to change when adding a new case:
  //   RtComponentPropertyType's operator << function in rtx_graph_types.cpp,
  //   `propertyValueFromString` in rtx_graph_types.cpp,
  //   `RtComponentPropertyTypeToCppType` in rtx_graph_types.h.
  //   `RtComponentPropertyValue` variant in rtx_graph_types.h,
  //   `RtComponentPropertyVector` variant in rtx_graph_types.h,
  //   `GraphUsdParser::getPropertyValue` in rtx_graph_usd_parser.cpp
  //   `TestComponent` in test_component.h, and the unit tests it is used in.
};
std::ostream& operator << (std::ostream& os, RtComponentPropertyType e);


// Specify what types are allowed for the NumberOrVector flexible type.
using RtComponentPropertyNumberOrVector = std::variant<
  float,
  Vector2,
  Vector3,
  Vector4
>;

using RtComponentPropertyValue = std::variant<
  // NOTE: std::vector<bool> has a special implementation to use 1 bit per element.
  // unfortunately, when placing such a vector inside a std::Variant, this makes it
  // unsafe to get a stable reference to the vector (it returns a temporary object instead).
  // To work around this, we store the bool as a uint32_t instead.
  // TODO: Potential optimization: should test out memory footprint vs number of types in this variant.
  // Once there are heavy use cases for graphs, we could test removing uint8_t and / or uint32_t as types.
  // Higher memory footprint vs fewer branches when adding / removing components.
  float,
  Vector2,
  Vector3,
  Vector4,
  uint32_t, // For Bool and Enums
  uint64_t, // For Hashes
  PrimTarget,
  std::string
>;

// Specific what types are allowed for the Any flexible type.
// Just reuse the RtComponentPropertyValue variant, since Any can be any of them.
using RtComponentPropertyAny = RtComponentPropertyValue;

// Use a default constructed PrimTarget as an invalid property.
inline const RtComponentPropertyValue kInvalidRtComponentPropertyValue{std::in_place_type<PrimTarget>, PrimTarget()};

// convenience constants for hardcoding bool values properly.
inline const RtComponentPropertyValue kFalsePropertyValue{std::in_place_type<uint32_t>, 0};
inline const RtComponentPropertyValue kTruePropertyValue{std::in_place_type<uint32_t>, 1};

using RtComponentPropertyVector = std::variant<
  std::vector<float>,
  std::vector<Vector2>,
  std::vector<Vector3>,
  std::vector<Vector4>,
  std::vector<uint32_t>,  // Bools and Enums
  std::vector<uint64_t>,  // Hashes
  std::vector<PrimTarget>,
  std::vector<std::string>
>;


// Templates to resolve RtComponentPropertyType enum to its corresponding C++ type
// This is used to map from the RtComponentPropertyType enum to the corresponding C++ type at compile time.
template<RtComponentPropertyType T>
struct RtComponentPropertyTypeToCppTypeImpl;
template<> struct RtComponentPropertyTypeToCppTypeImpl<RtComponentPropertyType::Bool> { using Type = uint32_t; }; // NOTE: see comment on RtComponentPropertyValue for why bool is stored as uint32_t.
template<> struct RtComponentPropertyTypeToCppTypeImpl<RtComponentPropertyType::Float> { using Type = float; };
template<> struct RtComponentPropertyTypeToCppTypeImpl<RtComponentPropertyType::Float2> { using Type = Vector2; };
template<> struct RtComponentPropertyTypeToCppTypeImpl<RtComponentPropertyType::Float3> { using Type = Vector3; };
template<> struct RtComponentPropertyTypeToCppTypeImpl<RtComponentPropertyType::Float4> { using Type = Vector4; };
template<> struct RtComponentPropertyTypeToCppTypeImpl<RtComponentPropertyType::Enum> { using Type = uint32_t; };
template<> struct RtComponentPropertyTypeToCppTypeImpl<RtComponentPropertyType::String> { using Type = std::string; };
template<> struct RtComponentPropertyTypeToCppTypeImpl<RtComponentPropertyType::AssetPath> { using Type = std::string; };
template<> struct RtComponentPropertyTypeToCppTypeImpl<RtComponentPropertyType::Hash> { using Type = uint64_t; };
template<> struct RtComponentPropertyTypeToCppTypeImpl<RtComponentPropertyType::Prim> { using Type = PrimTarget; };

template< RtComponentPropertyType propertyType >
using RtComponentPropertyTypeToCppType = typename RtComponentPropertyTypeToCppTypeImpl<propertyType>::Type;

// Reverse mapping: C++ type to RtComponentPropertyType enum
// This is used for automatic type deduction in templated code
// Note: uint32_t could be Bool or Enum, but as this is just for instantiating templated classes, we don't actually need to distinguish.
template<typename T> struct CppTypeToPropertyType;
template<> struct CppTypeToPropertyType<float> { static constexpr RtComponentPropertyType value = RtComponentPropertyType::Float; };
template<> struct CppTypeToPropertyType<Vector2> { static constexpr RtComponentPropertyType value = RtComponentPropertyType::Float2; };
template<> struct CppTypeToPropertyType<Vector3> { static constexpr RtComponentPropertyType value = RtComponentPropertyType::Float3; };
template<> struct CppTypeToPropertyType<Vector4> { static constexpr RtComponentPropertyType value = RtComponentPropertyType::Float4; };
template<> struct CppTypeToPropertyType<uint32_t> { static constexpr RtComponentPropertyType value = RtComponentPropertyType::Enum; };
template<> struct CppTypeToPropertyType<PrimTarget> { static constexpr RtComponentPropertyType value = RtComponentPropertyType::Prim; };
template<> struct CppTypeToPropertyType<std::string> { static constexpr RtComponentPropertyType value = RtComponentPropertyType::String; };
template<> struct CppTypeToPropertyType<uint64_t> { static constexpr RtComponentPropertyType value = RtComponentPropertyType::Hash; };

RtComponentPropertyValue propertyValueFromString(const std::string& str, const RtComponentPropertyType type);
RtComponentPropertyVector propertyVectorFromType(const RtComponentPropertyType type);

// Helper function to correctly create a RtComponentPropertyValue from an ambiguous
// type like `int`.  Without this, passing in a uint32_t will often create a 
// RtComponentPropertyValue<int> instead.
template<typename T, typename E>
RtComponentPropertyValue propertyValueForceType(const E& value) {
  // Check if T can be constructed from E
  // If so, convert the value. Otherwise, use default construction.
  if constexpr (std::is_constructible_v<T, E>) {
    // We want to allow reasonable conversions here, like converting `0` to a float.
    // To compile those, we need to disable the `narrowing conversion` warning.
    #pragma warning(push)
    #pragma warning(disable: 4244)
    return RtComponentPropertyValue(std::in_place_type<T>, T(value));
    #pragma warning(pop)
  } else {
    // T cannot be constructed from E (e.g., std::string from int, PrimTarget from int)
    // Use default construction instead
    return RtComponentPropertyValue(std::in_place_type<T>, T());
  }
}

// Helper to convert a RtComponentPropertyValue to the correct type for a property.
// This is used to ensure minValue/maxValue have the correct type even if the user
// writes `property.minValue = 0` (which defaults to int32_t).
// Only converts between numeric types; non-numeric types are left as-is.
template<typename TargetType>
RtComponentPropertyValue convertPropertyValueToType(const RtComponentPropertyValue& value) {
  // Only convert if target type is arithmetic (numeric)
  if constexpr (std::is_arithmetic_v<TargetType>) {
    return std::visit([](auto&& val) -> RtComponentPropertyValue {
      using SourceType = std::decay_t<decltype(val)>;
      // Only convert if source is also arithmetic
      if constexpr (std::is_arithmetic_v<SourceType>) {
        // Explicitly convert source value to target type
        return RtComponentPropertyValue(std::in_place_type<TargetType>, static_cast<TargetType>(val));
      } else {
        // Source is not numeric (e.g. string), return as-is
        return RtComponentPropertyValue(val);
      }
    }, value);
  } else {
    // Target type is not numeric (e.g. Vector3, string), return as-is
    return value;
  }
}

// Helper to determine the appropriate type for propertyValueForceType
// For enums, use the underlying type; for non-enums, use the original type
template<typename T>
using PropertyValueType = std::conditional_t<
  std::is_enum_v<T>,
  std::underlying_type_t<T>,
  T
>;

using RtComponentType = XXH64_hash_t;
static const RtComponentType kInvalidComponentType = kEmptyHash;

// Enum for USD prim types that can be targeted by Prim properties
enum class PrimType : uint32_t {
  UsdGeomMesh = 0,
  UsdLuxSphereLight = 1,
  UsdLuxCylinderLight = 2,
  UsdLuxDiskLight = 3,
  UsdLuxDistantLight = 4,
  UsdLuxRectLight = 5,
  OmniGraph = 6,
};

// Convert PrimType enum to USD type name string
inline std::string primTypeToString(PrimType type) {
  switch (type) {
    case PrimType::UsdGeomMesh: return "UsdGeomMesh";
    case PrimType::UsdLuxSphereLight: return "UsdLuxSphereLight";
    case PrimType::UsdLuxCylinderLight: return "UsdLuxCylinderLight";
    case PrimType::UsdLuxDiskLight: return "UsdLuxDiskLight";
    case PrimType::UsdLuxDistantLight: return "UsdLuxDistantLight";
    case PrimType::UsdLuxRectLight: return "UsdLuxRectLight";
    case PrimType::OmniGraph: return "OmniGraph";
    default: return "";
  }
}

struct RtComponentPropertySpec {
  static inline const std::string kUsdNamePrefix = "lightspeed.trex.logic.";
  
  RtComponentPropertyType type;  // For flexible types, this is the resolved concrete type (e.g., Float, Float2)
  RtComponentPropertyValue defaultValue;
  RtComponentPropertyIOType ioType;

  std::string name;
  std::string usdPropertyName;
  std::string_view uiName;
  std::string_view docString;
  
  // For flexible types (Any, NumberOrVector), stores the original declared type
  // For non-flexible types, this is the same as `type`
  RtComponentPropertyType declaredType;

  ///////////////////////////////////////////////////////////////////////////////////////////////////////////
  // BEGINNING OF OPTIONAL VALUES FOR PROPERTY SPECS
  ///////////////////////////////////////////////////////////////////////////////////////////////////////////

  // To set optional values when using the macros, write them as a comma separated list after the docString. 
  // `property.<name> = <value>`, i.e. `property.minValue = 0.0f, property.maxValue = 1.0f`
  // Note: minValue and maxValue are automatically converted to match the property's declared type

  // If this property has been renamed, list the old `usdPropertyName`s here for backwards compatibility.
  // If multiple definitions for the same property exist, the property on the strongest USD layer will be used.
  // If multiple definitions for the same property exist on a single layer, `name` will be used first,
  // followed by the earliest name in `oldUsdNames`.  So the ideal order should be:
  // property.oldUsdNames = { "thirdName", "secondName", "originalName" }
  std::vector<std::string> oldUsdNames;

  // NOTE: These are currently unenforced on the c++ side, but should be used for OGN generation.
  // TODO: consider enforcing these on the c++ side (between component batch updates?)
  // Using kFalsePropertyValue to represent false due to the bool problem mentioned in RtComponentPropertyValue.
  RtComponentPropertyValue minValue = kFalsePropertyValue;
  RtComponentPropertyValue maxValue = kFalsePropertyValue;


  // Whether the component will function without this property being set.
  // Runtime side all properties have a default value, so this is mostly a UI hint.
  bool optional = false;

  // Whether this input property can be both set by the user and read by other components as an output.
  // This is useful for constant value components where the input value itself acts as an output.
  // When true, the OGN writer will add "outputOnly": "1" metadata.
  // Note that properties with this set to true cannot accept inputs from other components.
  bool isSettableOutput = false;

  // Optional property to display as an enum in the USD.
  // specify as `property.enumValues = { {"DisplayName1", {enumClass::Value1, "DocString1"}}, {"DisplayName2", {enumClass::Value2, "DocString2"}}, ... }`
  struct EnumProperty {
    template<typename T, typename = std::enable_if_t<std::is_enum_v<T>>>
    EnumProperty(const T& value, const std::string& docString) : 
      value(static_cast<std::underlying_type_t<T>>(value)), docString(docString) {
      static_assert(std::is_same_v<std::underlying_type_t<T>, uint32_t>, 
                    "Enum underlying type must be uint32_t to match RtComponentPropertyType::Enum");
    }
    RtComponentPropertyValue value;
    std::string docString;
  };
  using EnumPropertyMap = std::map<std::string,EnumProperty>;
  EnumPropertyMap enumValues;

  // Whether to treat Float3/Float4 types as colors in UI/OGN generation (adds color metadata)
  bool treatAsColor = false;

  // For Prim properties, specify the allowed prim types as a vector of PrimType enum values.
  // Example: property.allowedPrimTypes = {PrimType::UsdGeomMesh}
  // or: property.allowedPrimTypes = {PrimType::UsdGeomMesh, PrimType::OmniGraph}
  // When set, the OGN writer will add "filterPrimTypes" metadata for target prim validation.
  std::vector<PrimType> allowedPrimTypes;

  ///////////////////////////////////////////////////////////////////////////////////////////////////////////
  // END OF OPTIONAL VALUES FOR PROPERTY SPECS
  ///////////////////////////////////////////////////////////////////////////////////////////////////////////

  // Validation methods
  bool isValid() const {
    return !name.empty() && !usdPropertyName.empty();
  }

};

// forward declare RtGraphBatch and RtComponentBatch so that they can be used in the createComponentBatch function.
class RtComponentBatch;
class RtGraphBatch;

// Function pointer types for component spec callbacks (using pointers instead of std::function to reduce binary size)
using CreateComponentBatchFunc = std::unique_ptr<RtComponentBatch>(*)(const RtGraphBatch& batch, std::vector<RtComponentPropertyVector>& values, const std::vector<size_t>& indices);
using ApplySceneOverridesFunc = void(*)(const Rc<DxvkContext>& context, RtComponentBatch& batch, const size_t start, const size_t end);
using InitializeFunc = void(*)(const Rc<DxvkContext>& context, RtComponentBatch& batch, const size_t index);
using CleanupFunc = void(*)(RtComponentBatch& batch, const size_t index);

struct RtComponentSpec {
  std::vector<RtComponentPropertySpec> properties;
  RtComponentType componentType = kInvalidComponentType;
  int version = 0;

  std::string name;
  std::string_view uiName;
  std::string_view categories;
  std::string_view docString;
  
  // For templated components: maps property name to its resolved concrete type
  // Empty for non-templated components
  std::unordered_map<std::string, RtComponentPropertyType> resolvedTypes;

  // Function to construct a batch of components from a graph topology and initial graph state.
  CreateComponentBatchFunc createComponentBatch = nullptr;

  ///////////////////////////////////////////////////////////////////////////////////////////////////////////
  // BEGINNING OF OPTIONAL VALUES FOR COMPONENT SPECS
  ///////////////////////////////////////////////////////////////////////////////////////////////////////////
  // Optional arguments for component batches.  Set these by adding a comma separated list at the end of
  // the component definition macro, i.e.: `REMIX_COMPONENT(..., spec.applySceneOverrides = [](...) { ... })`

  // If this component has been renamed, list the old `name`s here for backwards compatibility.
  std::vector<std::string> oldNames;

  // Optional function intended for applying values in the graph to renderable objects.  This is called near the top of SceneManager::prepareSceneData.
  ApplySceneOverridesFunc applySceneOverrides = nullptr;

  // Optional function called when component instances are created.
  // Called after earlier components have been initialized and updated, but before the first time
  // this component is updated.
  InitializeFunc initialize = nullptr;

  // Optional function called when component instances are about to be destroyed.
  // Called before the instance is removed from the batch. No context is available during cleanup.
  CleanupFunc cleanup = nullptr;

  ///////////////////////////////////////////////////////////////////////////////////////////////////////////
  // END OF OPTIONAL VALUES FOR COMPONENT SPECS
  ///////////////////////////////////////////////////////////////////////////////////////////////////////////

  // Validation methods
  bool isValid() const {
    if (componentType == kInvalidComponentType) {
      return false;
    }
    if (name.empty()) {
      return false;
    }

    if (!createComponentBatch) {
      return false;
    }

    // Validate all properties
    for (const auto& prop : properties) {
      if (!prop.isValid()) {
        return false;
      }
    }

    return true;
  }
  
  // `name` stores the full omniverse class name, including the namespace.
  // This function returns just the class name.
  std::string getClassName() const {
    size_t lastPeriod = name.find_last_of('.');
    if (lastPeriod == std::string::npos) {
      return name; // Return the full name if no period found
    }
    return name.substr(lastPeriod + 1);
  }
};

void registerComponentSpec(const RtComponentSpec* spec);
const RtComponentSpec* getComponentSpec(const RtComponentType& componentType);

// Returns a vector of ComponentSpec* for all registered variants of a component
// Returns empty vector if component type not found
using ComponentSpecVariantMap = std::vector<const RtComponentSpec*>;
const ComponentSpecVariantMap& getAllComponentSpecVariants(const RtComponentType& componentType);

// Returns any variant of a component for inspection purposes (to determine declared types, etc.)
// Returns nullptr if component type not found
const RtComponentSpec* getAnyComponentSpecVariant(const RtComponentType& componentType);

bool writeAllOGNSchemas(const char* outputFolderPath);
bool writeAllMarkdownDocs(const char* outputFolderPath);

// Stores all of the information about what components the graph contains and how they are related.
struct RtGraphTopology {
  std::vector<RtComponentPropertyType> propertyTypes;
  std::unordered_map<std::string, size_t> propertyPathHashToIndexMap;
  // for each component, list of the index (in `propertyTypes`) of the component's properties.
  std::vector<std::vector<size_t>> propertyIndices;
  std::vector<const RtComponentSpec*> componentSpecs;

  // Note: This hash is dependent on the order of the prims in the USD.
  // Graphs with the same hash will always have the same topology, but the graphs
  // with the same topology may have different hashes.
  XXH64_hash_t graphHash = 0;
};

// Stores the initial values used to when creating an instance of a graph.
struct RtGraphState {
  const RtGraphTopology& topology;
  std::vector<RtComponentPropertyValue> values;
  std::string primPath;
};

class RtComponentBatch {
public:
  // Update the range of instances for this batch of components, going from start to end-1.
  // This should iterate over the range of instances, updating each one individually.
  // `for (size_t i = start; i < end; i++) { property[i] = ...}`
  virtual void updateRange(const Rc<DxvkContext>& context, const size_t start, const size_t end) = 0;

  // Returns the specification for this component.  This should always be defined by `RtRegisteredComponentBatch<T>`.
  // Implementation classes should use 'static RtComponentSpec getStaticSpec()`
  virtual const RtComponentSpec* getSpec() const = 0;
};

// Base class that handles registration
template<typename Derived>
class RtRegisteredComponentBatch : public RtComponentBatch {
private:
  static bool registerType() {
    registerComponentSpec(Derived::getStaticSpec());
    return true;
  }
  static inline bool registered = registerType();
public:
  const RtComponentSpec* getSpec() const final {
    return Derived::getStaticSpec();
  }
};

}  // namespace dxvk

// Export functions for unit testing
#ifdef _WIN32
extern "C" __declspec(dllexport) bool writeAllOGNSchemas(const char* outputFolderPath);
extern "C" __declspec(dllexport) bool writeAllMarkdownDocs(const char* outputFolderPath);
#else
extern "C" __attribute__((visibility("default"))) bool writeAllOGNSchemas(const char* outputFolderPath);
extern "C" __attribute__((visibility("default"))) bool writeAllMarkdownDocs(const char* outputFolderPath);
#endif
