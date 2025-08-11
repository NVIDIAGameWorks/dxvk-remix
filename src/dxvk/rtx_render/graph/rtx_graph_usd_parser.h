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
#include "../../../lssusd/usd_include_begin.h"
#include <pxr/usd/usd/attribute.h>
#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usdGeom/mesh.h>
#include <pxr/usd/usdLux/lightAPI.h>
#include "../../../lssusd/usd_include_end.h"

#include <algorithm>
#include "rtx_graph_types.h"
#include "../rtx_asset_replacer.h"
#include "../util/log/log.h"
#include "../util/util_string.h"

namespace dxvk {
class GraphUsdParser {
public:
  using PathToOffsetMap = fast_unordered_cache<uint32_t>;

  // Make a RtGraphTopology object from a USD graph prim.
  static RtGraphState parseGraph(AssetReplacements& replacements, const pxr::UsdPrim& graphPrim, PathToOffsetMap& pathToOffsetMap);

  // Friend class for testing
  friend class GraphUsdParserTestApp;

private:

  struct DAGNode {
    pxr::SdfPath path;
    const RtComponentSpec* spec;
    size_t dependencyCount = 0; // NOTE: represents unfilled dependencies during DAG sorting, should be 0 for the returned results.
    std::unordered_set<size_t> dependents;
  };

  // Function to sort the components in the graph into a DAG, with components that have no dependencies first.
  // Also loads and caches the RtComponentSpec, to avoid re-loading it later.
  static std::vector<DAGNode> getDAGSortedNodes(const pxr::UsdPrim& graphPrim);

  static const RtComponentSpec* getComponentSpecForPrim(const pxr::UsdPrim& nodePrim);

  // If the `propertyPath` has been encountered before, return the original index.
  // Otherwise, create a new index for the property and return tht.
  static size_t getPropertyIndex(
      RtGraphTopology& topology,
      const pxr::SdfPath& propertyPath,
      const RtComponentPropertySpec& property);
  static bool versionCheck(const pxr::UsdPrim& nodePrim, const RtComponentSpec& componentSpec);


  static RtComponentPropertyValue getPropertyValue(const pxr::UsdRelationship& rel, const RtComponentPropertySpec& spec, PathToOffsetMap& pathToOffsetMap);
  static RtComponentPropertyValue getPropertyValue(const pxr::UsdAttribute& attr, const RtComponentPropertySpec& spec, PathToOffsetMap& pathToOffsetMap);
  template<typename T>
  static RtComponentPropertyValue getPropertyValue(const pxr::VtValue& value, const RtComponentPropertySpec& spec) {
    // Value may be declared but have no contents - common for output values.
    if (value.IsEmpty()) {
      return spec.defaultValue;
    }
    // Omnigraph input properties can be tokens, rather than the actual type.
    // This function will handle either the original type, or a token with a string value.
    // If we need additional custom behavior for specific spec.types, that can be implemented in
    // the non-templated getPropertyValue function.
    if (value.IsHolding<T>()) {
      if constexpr (std::is_same_v<T, bool>) {
        // NOTE: see comment on RtComponentPropertyValue for why bool is stored as uint8_t.
        return propertyValueForceType<uint8_t>(value.Get<T>());
      }
      return value.Get<T>();
    } else if (value.IsHolding<pxr::TfToken>()) {
      if constexpr (!std::is_same_v<T, bool>) {
        if ( spec.enumValues.size() > 0) {
          // If the property has enum values, we need to look up the value in the enum values map.
          auto iter = spec.enumValues.find(value.Get<pxr::TfToken>().GetString());
          if (iter != spec.enumValues.end()) {
            assert(std::holds_alternative<T>(iter->second.value) && "enumValue values must match the property type.");
            return iter->second.value;
          }
        }
      }
      return propertyValueFromString(value.Get<pxr::TfToken>().GetString(), spec.type);
    } else if (constexpr (std::is_same_v<T, Vector2>) && value.IsHolding<pxr::GfVec2f>()) {
      return Vector2(value.Get<pxr::GfVec2f>().data());
    } else if (constexpr (std::is_same_v<T, Vector3>) && value.IsHolding<pxr::GfVec3f>()) {
      return Vector3(value.Get<pxr::GfVec3f>().data());
    } else if (constexpr (std::is_same_v<T, Vector4>) && value.IsHolding<pxr::GfVec4f>()) { 
      return Vector4(value.Get<pxr::GfVec4f>().data());
    }
    Logger::err(str::format("type mismatch in getPropertyValue. property: ", spec.name,
                            " expects type: ", spec.type, " but got type: ", value.GetTypeName()));
    assert(false && "type mismatch in getPropertyValue");
    return spec.defaultValue;
  }

};
}  // namespace dxvk
