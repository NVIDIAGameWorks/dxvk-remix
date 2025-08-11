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

enum class RtComponentPropertyType {
  Bool,
  Float,
  Float2,
  Float3,
  Color3,
  Color4,
  Int32,
  Uint32,
  Uint64,

  // Default Value is ignored for relationships. It's safe to just use 0.
  Prim,

  // TODO should we support lists of any of the above types.
  // TODO should Hash be a separate type? it's just uint64_t under the hood, but could be displayed differently.
  // TODO should we support strings? asset paths?
  // TODO support generic types (i.e. number, or numbersAndVectors)

  // NOTE: Places to change when adding a new case:
  //   RtComponentPropertyType's operator << function in rtx_graph_types.cpp,
  //   `propertyValueFromString` in rtx_graph_types.cpp,
  //   `RtComponentPropertyTypeToCppType` in rtx_graph_types.h.
  //   `RtComponentPropertyValue` variant in rtx_graph_types.h,
  //   `RtComponentPropertyVector` variant in rtx_graph_types.h,
};
std::ostream& operator << (std::ostream& os, RtComponentPropertyType e);

// Templates to resolve RtComponentPropertyType enum to its corresponding C++ type
// This is used to map from the RtComponentPropertyType enum to the corresponding C++ type at compile time.
template<RtComponentPropertyType T>
struct RtComponentPropertyTypeToCppTypeImpl;
template<> struct RtComponentPropertyTypeToCppTypeImpl<RtComponentPropertyType::Bool> { using Type = uint8_t; }; // NOTE: see comment on RtComponentPropertyValue for why bool is stored as uint8_t.
template<> struct RtComponentPropertyTypeToCppTypeImpl<RtComponentPropertyType::Float> { using Type = float; };
template<> struct RtComponentPropertyTypeToCppTypeImpl<RtComponentPropertyType::Float2> { using Type = Vector2; };
template<> struct RtComponentPropertyTypeToCppTypeImpl<RtComponentPropertyType::Float3> { using Type = Vector3; };
template<> struct RtComponentPropertyTypeToCppTypeImpl<RtComponentPropertyType::Color3> { using Type = Vector3; };
template<> struct RtComponentPropertyTypeToCppTypeImpl<RtComponentPropertyType::Color4> { using Type = Vector4; };
template<> struct RtComponentPropertyTypeToCppTypeImpl<RtComponentPropertyType::Int32> { using Type = int32_t; };
template<> struct RtComponentPropertyTypeToCppTypeImpl<RtComponentPropertyType::Uint32> { using Type = uint32_t; };
template<> struct RtComponentPropertyTypeToCppTypeImpl<RtComponentPropertyType::Uint64> { using Type = uint64_t; };
template<> struct RtComponentPropertyTypeToCppTypeImpl<RtComponentPropertyType::Prim> { using Type = uint32_t; };

template< RtComponentPropertyType propertyType >
using RtComponentPropertyTypeToCppType = typename RtComponentPropertyTypeToCppTypeImpl<propertyType>::Type;

using RtComponentPropertyValue = std::variant<
  // NOTE: std::vector<bool> has a special implementation to use 1 bit per element.
  // unfortunately, when placing such a vector inside a std::Variant, this makes it
  // unsafe to get a stable reference to the vector (it returns a temporary object instead).
  // To work around this, we store the bool as a uint8_t instead.
  uint8_t,
  // TODO: Potential optimization: should test out memory footprint vs number of types in this variant.
  // Once there are heavy use cases for graphs, we could test removing uint8_t and / or uint32_t as types.
  // Higher memory footprint vs fewer branches when adding / removing components.
  float,
  Vector2,
  Vector3,
  Vector4,
  int32_t,
  uint32_t,
  uint64_t
>;

using RtComponentPropertyVector = std::variant<
  // NOTE: see comment on RtComponentPropertyValue for why bool is stored as uint8_t.
  std::vector<uint8_t>,
  std::vector<float>,
  std::vector<Vector2>,
  std::vector<Vector3>,
  std::vector<Vector4>,
  std::vector<int32_t>,
  std::vector<uint32_t>,
  std::vector<uint64_t>
>;

RtComponentPropertyValue propertyValueFromString(const std::string& str, const RtComponentPropertyType type);
RtComponentPropertyVector propertyVectorFromType(const RtComponentPropertyType type);

// Helper function to correctly create a RtComponentPropertyValue from an ambiguous
// type like `int`.  Without this, passing in a uint32_t will often create a 
// RtComponentPropertyValue<int> instead.
template<typename T, typename E>
RtComponentPropertyValue propertyValueForceType(const E& value) {
  return RtComponentPropertyValue(std::in_place_type<T>, static_cast<T>(value));
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

struct RtComponentPropertySpec {
  RtComponentPropertyType type;
  RtComponentPropertyValue defaultValue;
  RtComponentPropertyIOType ioType;

  std::string name;
  std::string usdPropertyName;
  std::string uiName;
  std::string docString;

  // Optional Values
  // To set optional values when using the macros, write them as a comma separated list after the docString. 
  // `property.<name> = <value>`, i.e. `property.minValue = 0.0f, property.maxValue = 1.0f`

  // NOTE: These are currently unenforced on the c++ side, but should be used for OGN generation.
  // TODO: consider enforcing these on the c++ side (between component batch updates?)
  RtComponentPropertyValue minValue = false;
  RtComponentPropertyValue maxValue = false;

  // Whether the component will function without this property being set.
  // Runtime side all properties have a default value, so this is mostly a UI hint.
  bool optional = false;

  // Optional property to display as an enum in the USD.
  // specify as `property.enumValues = { {"DisplayName1", {enumClass::Value1, "DocString1"}}, {"DisplayName2", {enumClass::Value2, "DocString2"}}, ... }`
  struct EnumProperty {
    template<typename T, typename = std::enable_if_t<std::is_enum_v<T>>>
    EnumProperty(const T& value, const std::string& docString) : 
      value(propertyValueForceType<std::underlying_type_t<T>>(value)), docString(docString) {}
    RtComponentPropertyValue value;
    std::string docString;
  };
  std::map<std::string,EnumProperty> enumValues;

  // Validation methods
  bool isValid() const {
    return !name.empty() && !usdPropertyName.empty();
  }

};

// forward declare RtGraphBatch and RtComponentBatch so that they can be used in the createComponentBatch function.
class RtComponentBatch;
class RtGraphBatch;

struct RtComponentSpec {
  std::vector<RtComponentPropertySpec> properties;
  RtComponentType componentType = kInvalidComponentType;
  int version = 0;

  std::string name;
  std::string uiName;
  std::string categories;
  std::string docString;

  // Function to construct a batch of components from a graph topology and initial graph state.
  std::function<std::unique_ptr<RtComponentBatch>(const RtGraphBatch& batch, std::vector<RtComponentPropertyVector>& values, const std::vector<size_t>& indices)> createComponentBatch;

  // Optional functions for component batches.  Set these by adding a lambda to the end of the component definition macro:
  // `spec.applySceneOverrides = [](...) { ... }`


  // Optional function intended for applying values in the graph to renderable objects.  This is called near the top of SceneManager::prepareSceneData.
  std::function<void(const Rc<DxvkContext>& context, RtComponentBatch& batch, const size_t start, const size_t end)> applySceneOverrides;

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
