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

#include <iostream>
#include <cmath>
#include "../../test_utils.h"
#include "../../../src/util/util_vector.h"
#include "../../../src/util/log/log.h"
#include "../../../src/util/util_globaltime.h"
#include "../../../src/dxvk/rtx_render/graph/rtx_graph_types.h"
#include "../../../src/dxvk/rtx_render/graph/rtx_graph_batch.h"

namespace dxvk {
Logger Logger::s_instance("test_transform_components.log");

namespace test {

// Mock RtGraphBatch for testing
struct MockGraphBatch : public RtGraphBatch {
  MockGraphBatch() {}
};

// Helper to compare floats with tolerance
inline bool floatEquals(float a, float b, float epsilon = 1e-5f) {
  return std::fabs(a - b) < epsilon;
}

// Helper to compare vectors with tolerance
inline bool vectorEquals(const Vector2& a, const Vector2& b, float epsilon = 1e-5f) {
  return floatEquals(a.x, b.x, epsilon) && floatEquals(a.y, b.y, epsilon);
}

inline bool vectorEquals(const Vector3& a, const Vector3& b, float epsilon = 1e-5f) {
  return floatEquals(a.x, b.x, epsilon) && floatEquals(a.y, b.y, epsilon) && floatEquals(a.z, b.z, epsilon);
}

inline bool vectorEquals(const Vector4& a, const Vector4& b, float epsilon = 1e-5f) {
  return floatEquals(a.x, b.x, epsilon) && floatEquals(a.y, b.y, epsilon) && 
         floatEquals(a.z, b.z, epsilon) && floatEquals(a.w, b.w, epsilon);
}

// Helper to get component variant matching specific types
const RtComponentSpec* getComponentVariant(const std::string& baseName, 
                                           const std::unordered_map<std::string, RtComponentPropertyType>& desiredTypes) {
  XXH64_hash_t baseHash = XXH3_64bits(baseName.c_str(), baseName.size());
  const ComponentSpecVariantMap& variants = getAllComponentSpecVariants(baseHash);
  
  for (const auto* variantSpec : variants) {
    bool allMatch = true;
    for (const auto& [propName, desiredType] : desiredTypes) {
      auto variantIt = variantSpec->resolvedTypes.find(propName);
      if (variantIt == variantSpec->resolvedTypes.end() || variantIt->second != desiredType) {
        allMatch = false;
        break;
      }
    }
    
    if (allMatch) {
      return variantSpec;
    }
  }
  
  return nullptr;
}

// Helper to get any variant of a non-templated component
const RtComponentSpec* getComponentSpec(const std::string& baseName) {
  XXH64_hash_t baseHash = XXH3_64bits(baseName.c_str(), baseName.size());
  return getAnyComponentSpecVariant(baseHash);
}

// Generic helper to test a component variant with specific types
// Returns the property vector for further validation by caller
template<typename ResultType>
std::vector<ResultType>& testComponentVariant(
    const char* componentName,
    const std::unordered_map<std::string, RtComponentPropertyType>& desiredTypes,
    std::vector<RtComponentPropertyVector>& props,
    size_t resultPropIndex,
    size_t startIdx = 0,
    size_t count = 1) {
  
  const RtComponentSpec* spec = getComponentVariant(componentName, desiredTypes);
  if (!spec) {
    // Build error message with type info
    std::string typeInfo;
    for (const auto& [name, type] : desiredTypes) {
      if (!typeInfo.empty()) { typeInfo += ", "; }
      typeInfo += name;
    }
    throw DxvkError(str::format("Failed to find ", componentName, " with types: ", typeInfo));
  }
  
  std::vector<size_t> indices;
  for (size_t i = 0; i < props.size(); ++i) {
    indices.push_back(i);
  }
  
  MockGraphBatch batch;
  auto comp = spec->createComponentBatch(batch, props, indices);
  comp->updateRange(nullptr, startIdx, startIdx + count);
  
  return std::get<std::vector<ResultType>>(props[resultPropIndex]);
}

//=============================================================================
// ARITHMETIC COMPONENTS
//=============================================================================

void testAdd() {
  
  XXH64_hash_t baseHash = XXH3_64bits("lightspeed.trex.logic.Add", strlen("lightspeed.trex.logic.Add"));
  const ComponentSpecVariantMap& variants = getAllComponentSpecVariants(baseHash);
  
  // Test Float variant
  {
    std::unordered_map<std::string, RtComponentPropertyType> desiredTypes = {
      {"a", RtComponentPropertyType::Float},
      {"b", RtComponentPropertyType::Float},
      {"sum", RtComponentPropertyType::Float}
    };
    std::vector<RtComponentPropertyVector> props;
    props.push_back(std::vector<float>{2.5f, 10.0f, -5.0f});  // a
    props.push_back(std::vector<float>{1.5f, -3.0f, 5.0f});   // b
    props.push_back(std::vector<float>{0.0f, 0.0f, 0.0f});    // sum
    
    auto& result = testComponentVariant<float>("lightspeed.trex.logic.Add", desiredTypes, props, 2, 0, 3);
    if (!floatEquals(result[0], 4.0f) || !floatEquals(result[1], 7.0f) || !floatEquals(result[2], 0.0f)) {
      throw DxvkError("Add<Float> failed");
    }
  }
  
  // Test Float2 variant
  {
    std::unordered_map<std::string, RtComponentPropertyType> desiredTypes = {
      {"a", RtComponentPropertyType::Float2},
      {"b", RtComponentPropertyType::Float2},
      {"sum", RtComponentPropertyType::Float2}
    };
    std::vector<RtComponentPropertyVector> props;
    props.push_back(std::vector<Vector2>{Vector2(1.0f, 2.0f), Vector2(-1.0f, -2.0f)});  // a
    props.push_back(std::vector<Vector2>{Vector2(3.0f, 4.0f), Vector2(1.0f, 2.0f)});    // b
    props.push_back(std::vector<Vector2>{Vector2(), Vector2()});                         // sum
    
    auto& result = testComponentVariant<Vector2>("lightspeed.trex.logic.Add", desiredTypes, props, 2, 0, 2);
    if (!vectorEquals(result[0], Vector2(4.0f, 6.0f)) || 
        !vectorEquals(result[1], Vector2(0.0f, 0.0f))) {
      throw DxvkError("Add<Float2> failed");
    }
  }
  
  // Test Float3 variant
  {
    std::unordered_map<std::string, RtComponentPropertyType> desiredTypes = {
      {"a", RtComponentPropertyType::Float3},
      {"b", RtComponentPropertyType::Float3},
      {"sum", RtComponentPropertyType::Float3}
    };
    std::vector<RtComponentPropertyVector> props;
    props.push_back(std::vector<Vector3>{Vector3(1.0f, 2.0f, 3.0f), Vector3(-1.0f, -2.0f, -3.0f)});  // a
    props.push_back(std::vector<Vector3>{Vector3(4.0f, 5.0f, 6.0f), Vector3(1.0f, 2.0f, 3.0f)});     // b
    props.push_back(std::vector<Vector3>{Vector3(), Vector3()});                                      // sum
    
    auto& result = testComponentVariant<Vector3>("lightspeed.trex.logic.Add", desiredTypes, props, 2, 0, 2);
    if (!vectorEquals(result[0], Vector3(5.0f, 7.0f, 9.0f)) || 
        !vectorEquals(result[1], Vector3(0.0f, 0.0f, 0.0f))) {
      throw DxvkError("Add<Float3> failed");
    }
  }
  
  // Test Float4 variant
  {
    std::unordered_map<std::string, RtComponentPropertyType> desiredTypes = {
      {"a", RtComponentPropertyType::Float4},
      {"b", RtComponentPropertyType::Float4},
      {"sum", RtComponentPropertyType::Float4}
    };
    std::vector<RtComponentPropertyVector> props;
    props.push_back(std::vector<Vector4>{Vector4(1.0f, 2.0f, 3.0f, 4.0f)});  // a
    props.push_back(std::vector<Vector4>{Vector4(5.0f, 6.0f, 7.0f, 8.0f)});  // b
    props.push_back(std::vector<Vector4>{Vector4()});                         // sum
    
    auto& result = testComponentVariant<Vector4>("lightspeed.trex.logic.Add", desiredTypes, props, 2, 0, 1);
    if (!vectorEquals(result[0], Vector4(6.0f, 8.0f, 10.0f, 12.0f))) {
      throw DxvkError("Add<Float4> failed");
    }
  }
  
  if (variants.size() != 4) {
    throw DxvkError(str::format("Add variant count mismatch: expected 4, tested 4, found ", variants.size()));
  }
  
  Logger::info("Add component passed (Float, Float2, Float3, Float4)");
}

void testSubtract() {
  
  XXH64_hash_t baseHash = XXH3_64bits("lightspeed.trex.logic.Subtract", strlen("lightspeed.trex.logic.Subtract"));
  const ComponentSpecVariantMap& variants = getAllComponentSpecVariants(baseHash);
  
  // Test Float variant
  {
    std::unordered_map<std::string, RtComponentPropertyType> desiredTypes = {
      {"a", RtComponentPropertyType::Float},
      {"b", RtComponentPropertyType::Float},
      {"difference", RtComponentPropertyType::Float}
    };
    std::vector<RtComponentPropertyVector> props;
    props.push_back(std::vector<float>{10.0f, 5.0f, -3.0f});  // a
    props.push_back(std::vector<float>{3.0f, 2.0f, -5.0f});   // b
    props.push_back(std::vector<float>{0.0f, 0.0f, 0.0f});    // difference
    
    auto& result = testComponentVariant<float>("lightspeed.trex.logic.Subtract", desiredTypes, props, 2, 0, 3);
    if (!floatEquals(result[0], 7.0f) || !floatEquals(result[1], 3.0f) || !floatEquals(result[2], 2.0f)) {
      throw DxvkError("Subtract<Float> failed");
    }
  }
  
  // Test Float2 variant
  {
    std::unordered_map<std::string, RtComponentPropertyType> desiredTypes = {
      {"a", RtComponentPropertyType::Float2},
      {"b", RtComponentPropertyType::Float2},
      {"difference", RtComponentPropertyType::Float2}
    };
    std::vector<RtComponentPropertyVector> props;
    props.push_back(std::vector<Vector2>{Vector2(10.0f, 8.0f), Vector2(5.0f, 3.0f)});  // a
    props.push_back(std::vector<Vector2>{Vector2(3.0f, 2.0f), Vector2(2.0f, 1.0f)});   // b
    props.push_back(std::vector<Vector2>{Vector2(), Vector2()});                        // difference
    
    auto& result = testComponentVariant<Vector2>("lightspeed.trex.logic.Subtract", desiredTypes, props, 2, 0, 2);
    if (!vectorEquals(result[0], Vector2(7.0f, 6.0f)) || 
        !vectorEquals(result[1], Vector2(3.0f, 2.0f))) {
      throw DxvkError("Subtract<Float2> failed");
    }
  }
  
  // Test Float3 variant
  {
    std::unordered_map<std::string, RtComponentPropertyType> desiredTypes = {
      {"a", RtComponentPropertyType::Float3},
      {"b", RtComponentPropertyType::Float3},
      {"difference", RtComponentPropertyType::Float3}
    };
    std::vector<RtComponentPropertyVector> props;
    props.push_back(std::vector<Vector3>{Vector3(10.0f, 8.0f, 6.0f)});  // a
    props.push_back(std::vector<Vector3>{Vector3(3.0f, 2.0f, 1.0f)});   // b
    props.push_back(std::vector<Vector3>{Vector3()});                    // difference
    
    auto& result = testComponentVariant<Vector3>("lightspeed.trex.logic.Subtract", desiredTypes, props, 2, 0, 1);
    if (!vectorEquals(result[0], Vector3(7.0f, 6.0f, 5.0f))) {
      throw DxvkError("Subtract<Float3> failed");
    }
  }
  
  // Test Float4 variant
  {
    std::unordered_map<std::string, RtComponentPropertyType> desiredTypes = {
      {"a", RtComponentPropertyType::Float4},
      {"b", RtComponentPropertyType::Float4},
      {"difference", RtComponentPropertyType::Float4}
    };
    std::vector<RtComponentPropertyVector> props;
    props.push_back(std::vector<Vector4>{Vector4(10.0f, 8.0f, 6.0f, 4.0f)});  // a
    props.push_back(std::vector<Vector4>{Vector4(3.0f, 2.0f, 1.0f, 2.0f)});   // b
    props.push_back(std::vector<Vector4>{Vector4()});                          // difference
    
    auto& result = testComponentVariant<Vector4>("lightspeed.trex.logic.Subtract", desiredTypes, props, 2, 0, 1);
    if (!vectorEquals(result[0], Vector4(7.0f, 6.0f, 5.0f, 2.0f))) {
      throw DxvkError("Subtract<Float4> failed");
    }
  }
  
  if (variants.size() != 4) {
    throw DxvkError(str::format("Subtract variant count mismatch: expected 4, tested 4, found ", variants.size()));
  }
  
  Logger::info("Subtract component passed (Float, Float2, Float3, Float4)");
}

void testMultiply() {
  
  XXH64_hash_t baseHash = XXH3_64bits("lightspeed.trex.logic.Multiply", strlen("lightspeed.trex.logic.Multiply"));
  const ComponentSpecVariantMap& variants = getAllComponentSpecVariants(baseHash);
  
  int testedCount = 0;
  
  // Test homogeneous variants: Float × Float, Float2 × Float2, etc.
  {
    std::unordered_map<std::string, RtComponentPropertyType> desiredTypes = {
      {"a", RtComponentPropertyType::Float},
      {"b", RtComponentPropertyType::Float},
      {"product", RtComponentPropertyType::Float}
    };
    const RtComponentSpec* spec = getComponentVariant("lightspeed.trex.logic.Multiply", desiredTypes);
    if (!spec) {
      throw DxvkError("Failed to find Multiply<Float, Float> component");
    }
    
    std::vector<RtComponentPropertyVector> props;
    props.push_back(std::vector<float>{2.0f, 5.0f, -3.0f});  // a
    props.push_back(std::vector<float>{3.0f, 2.0f, 4.0f});   // b
    props.push_back(std::vector<float>{0.0f, 0.0f, 0.0f});   // product
    
    std::vector<size_t> indices = {0, 1, 2};
    MockGraphBatch batch;
    auto comp = spec->createComponentBatch(batch, props, indices);
    comp->updateRange(nullptr, 0, 3);
    
    auto& result = std::get<std::vector<float>>(props[2]);
    if (!floatEquals(result[0], 6.0f) || !floatEquals(result[1], 10.0f) || !floatEquals(result[2], -12.0f)) {
      throw DxvkError("Multiply<Float, Float> failed");
    }
    testedCount++;
  }
  
  {
    std::unordered_map<std::string, RtComponentPropertyType> desiredTypes = {
      {"a", RtComponentPropertyType::Float2},
      {"b", RtComponentPropertyType::Float2},
      {"product", RtComponentPropertyType::Float2}
    };
    const RtComponentSpec* spec = getComponentVariant("lightspeed.trex.logic.Multiply", desiredTypes);
    if (!spec) {
      throw DxvkError("Failed to find Multiply<Float2, Float2> component");
    }
    
    std::vector<RtComponentPropertyVector> props;
    props.push_back(std::vector<Vector2>{Vector2(2.0f, 3.0f)});  // a
    props.push_back(std::vector<Vector2>{Vector2(4.0f, 5.0f)});  // b
    props.push_back(std::vector<Vector2>{Vector2()});            // product
    
    std::vector<size_t> indices = {0, 1, 2};
    MockGraphBatch batch;
    auto comp = spec->createComponentBatch(batch, props, indices);
    comp->updateRange(nullptr, 0, 1);
    
    auto& result = std::get<std::vector<Vector2>>(props[2]);
    if (!vectorEquals(result[0], Vector2(8.0f, 15.0f))) {
      throw DxvkError("Multiply<Float2, Float2> failed");
    }
    testedCount++;
  }
  
  {
    std::unordered_map<std::string, RtComponentPropertyType> desiredTypes = {
      {"a", RtComponentPropertyType::Float3},
      {"b", RtComponentPropertyType::Float3},
      {"product", RtComponentPropertyType::Float3}
    };
    const RtComponentSpec* spec = getComponentVariant("lightspeed.trex.logic.Multiply", desiredTypes);
    if (!spec) {
      throw DxvkError("Failed to find Multiply<Float3, Float3> component");
    }
    
    std::vector<RtComponentPropertyVector> props;
    props.push_back(std::vector<Vector3>{Vector3(2.0f, 3.0f, 4.0f)});  // a
    props.push_back(std::vector<Vector3>{Vector3(5.0f, 6.0f, 7.0f)});  // b
    props.push_back(std::vector<Vector3>{Vector3()});                   // product
    
    std::vector<size_t> indices = {0, 1, 2};
    MockGraphBatch batch;
    auto comp = spec->createComponentBatch(batch, props, indices);
    comp->updateRange(nullptr, 0, 1);
    
    auto& result = std::get<std::vector<Vector3>>(props[2]);
    if (!vectorEquals(result[0], Vector3(10.0f, 18.0f, 28.0f))) {
      throw DxvkError("Multiply<Float3, Float3> failed");
    }
    testedCount++;
  }
  
  {
    std::unordered_map<std::string, RtComponentPropertyType> desiredTypes = {
      {"a", RtComponentPropertyType::Float4},
      {"b", RtComponentPropertyType::Float4},
      {"product", RtComponentPropertyType::Float4}
    };
    const RtComponentSpec* spec = getComponentVariant("lightspeed.trex.logic.Multiply", desiredTypes);
    if (!spec) {
      throw DxvkError("Failed to find Multiply<Float4, Float4> component");
    }
    
    std::vector<RtComponentPropertyVector> props;
    props.push_back(std::vector<Vector4>{Vector4(2.0f, 3.0f, 4.0f, 5.0f)});  // a
    props.push_back(std::vector<Vector4>{Vector4(6.0f, 7.0f, 8.0f, 9.0f)});  // b
    props.push_back(std::vector<Vector4>{Vector4()});                         // product
    
    std::vector<size_t> indices = {0, 1, 2};
    MockGraphBatch batch;
    auto comp = spec->createComponentBatch(batch, props, indices);
    comp->updateRange(nullptr, 0, 1);
    
    auto& result = std::get<std::vector<Vector4>>(props[2]);
    if (!vectorEquals(result[0], Vector4(12.0f, 21.0f, 32.0f, 45.0f))) {
      throw DxvkError("Multiply<Float4, Float4> failed");
    }
    testedCount++;
  }
  
  // Test mixed-type variants: Float × Vector
  {
    std::unordered_map<std::string, RtComponentPropertyType> desiredTypes = {
      {"a", RtComponentPropertyType::Float},
      {"b", RtComponentPropertyType::Float2},
      {"product", RtComponentPropertyType::Float2}
    };
    const RtComponentSpec* spec = getComponentVariant("lightspeed.trex.logic.Multiply", desiredTypes);
    if (!spec) {
      throw DxvkError("Failed to find Multiply<Float, Float2> component");
    }
    
    std::vector<RtComponentPropertyVector> props;
    props.push_back(std::vector<float>{2.0f});                    // a
    props.push_back(std::vector<Vector2>{Vector2(3.0f, 4.0f)});  // b
    props.push_back(std::vector<Vector2>{Vector2()});            // product
    
    std::vector<size_t> indices = {0, 1, 2};
    MockGraphBatch batch;
    auto comp = spec->createComponentBatch(batch, props, indices);
    comp->updateRange(nullptr, 0, 1);
    
    auto& result = std::get<std::vector<Vector2>>(props[2]);
    if (!vectorEquals(result[0], Vector2(6.0f, 8.0f))) {
      throw DxvkError("Multiply<Float, Float2> failed");
    }
    testedCount++;
  }
  
  {
    std::unordered_map<std::string, RtComponentPropertyType> desiredTypes = {
      {"a", RtComponentPropertyType::Float},
      {"b", RtComponentPropertyType::Float3},
      {"product", RtComponentPropertyType::Float3}
    };
    const RtComponentSpec* spec = getComponentVariant("lightspeed.trex.logic.Multiply", desiredTypes);
    if (!spec) {
      throw DxvkError("Failed to find Multiply<Float, Float3> component");
    }
    
    std::vector<RtComponentPropertyVector> props;
    props.push_back(std::vector<float>{2.0f});                         // a
    props.push_back(std::vector<Vector3>{Vector3(3.0f, 4.0f, 5.0f)});  // b
    props.push_back(std::vector<Vector3>{Vector3()});                   // product
    
    std::vector<size_t> indices = {0, 1, 2};
    MockGraphBatch batch;
    auto comp = spec->createComponentBatch(batch, props, indices);
    comp->updateRange(nullptr, 0, 1);
    
    auto& result = std::get<std::vector<Vector3>>(props[2]);
    if (!vectorEquals(result[0], Vector3(6.0f, 8.0f, 10.0f))) {
      throw DxvkError("Multiply<Float, Float3> failed");
    }
    testedCount++;
  }
  
  {
    std::unordered_map<std::string, RtComponentPropertyType> desiredTypes = {
      {"a", RtComponentPropertyType::Float},
      {"b", RtComponentPropertyType::Float4},
      {"product", RtComponentPropertyType::Float4}
    };
    const RtComponentSpec* spec = getComponentVariant("lightspeed.trex.logic.Multiply", desiredTypes);
    if (!spec) {
      throw DxvkError("Failed to find Multiply<Float, Float4> component");
    }
    
    std::vector<RtComponentPropertyVector> props;
    props.push_back(std::vector<float>{2.0f});                              // a
    props.push_back(std::vector<Vector4>{Vector4(3.0f, 4.0f, 5.0f, 6.0f)});  // b
    props.push_back(std::vector<Vector4>{Vector4()});                         // product
    
    std::vector<size_t> indices = {0, 1, 2};
    MockGraphBatch batch;
    auto comp = spec->createComponentBatch(batch, props, indices);
    comp->updateRange(nullptr, 0, 1);
    
    auto& result = std::get<std::vector<Vector4>>(props[2]);
    if (!vectorEquals(result[0], Vector4(6.0f, 8.0f, 10.0f, 12.0f))) {
      throw DxvkError("Multiply<Float, Float4> failed");
    }
    testedCount++;
  }
  
  // Test mixed-type variants: Vector × Float
  {
    std::unordered_map<std::string, RtComponentPropertyType> desiredTypes = {
      {"a", RtComponentPropertyType::Float2},
      {"b", RtComponentPropertyType::Float},
      {"product", RtComponentPropertyType::Float2}
    };
    const RtComponentSpec* spec = getComponentVariant("lightspeed.trex.logic.Multiply", desiredTypes);
    if (!spec) {
      throw DxvkError("Failed to find Multiply<Float2, Float> component");
    }
    
    std::vector<RtComponentPropertyVector> props;
    props.push_back(std::vector<Vector2>{Vector2(3.0f, 4.0f)});  // a
    props.push_back(std::vector<float>{2.0f});                    // b
    props.push_back(std::vector<Vector2>{Vector2()});            // product
    
    std::vector<size_t> indices = {0, 1, 2};
    MockGraphBatch batch;
    auto comp = spec->createComponentBatch(batch, props, indices);
    comp->updateRange(nullptr, 0, 1);
    
    auto& result = std::get<std::vector<Vector2>>(props[2]);
    if (!vectorEquals(result[0], Vector2(6.0f, 8.0f))) {
      throw DxvkError("Multiply<Float2, Float> failed");
    }
    testedCount++;
  }
  
  {
    std::unordered_map<std::string, RtComponentPropertyType> desiredTypes = {
      {"a", RtComponentPropertyType::Float3},
      {"b", RtComponentPropertyType::Float},
      {"product", RtComponentPropertyType::Float3}
    };
    const RtComponentSpec* spec = getComponentVariant("lightspeed.trex.logic.Multiply", desiredTypes);
    if (!spec) {
      throw DxvkError("Failed to find Multiply<Float3, Float> component");
    }
    
    std::vector<RtComponentPropertyVector> props;
    props.push_back(std::vector<Vector3>{Vector3(3.0f, 4.0f, 5.0f)});  // a
    props.push_back(std::vector<float>{2.0f});                          // b
    props.push_back(std::vector<Vector3>{Vector3()});                   // product
    
    std::vector<size_t> indices = {0, 1, 2};
    MockGraphBatch batch;
    auto comp = spec->createComponentBatch(batch, props, indices);
    comp->updateRange(nullptr, 0, 1);
    
    auto& result = std::get<std::vector<Vector3>>(props[2]);
    if (!vectorEquals(result[0], Vector3(6.0f, 8.0f, 10.0f))) {
      throw DxvkError("Multiply<Float3, Float> failed");
    }
    testedCount++;
  }
  
  {
    std::unordered_map<std::string, RtComponentPropertyType> desiredTypes = {
      {"a", RtComponentPropertyType::Float4},
      {"b", RtComponentPropertyType::Float},
      {"product", RtComponentPropertyType::Float4}
    };
    const RtComponentSpec* spec = getComponentVariant("lightspeed.trex.logic.Multiply", desiredTypes);
    if (!spec) {
      throw DxvkError("Failed to find Multiply<Float4, Float> component");
    }
    
    std::vector<RtComponentPropertyVector> props;
    props.push_back(std::vector<Vector4>{Vector4(3.0f, 4.0f, 5.0f, 6.0f)});  // a
    props.push_back(std::vector<float>{2.0f});                                // b
    props.push_back(std::vector<Vector4>{Vector4()});                         // product
    
    std::vector<size_t> indices = {0, 1, 2};
    MockGraphBatch batch;
    auto comp = spec->createComponentBatch(batch, props, indices);
    comp->updateRange(nullptr, 0, 1);
    
    auto& result = std::get<std::vector<Vector4>>(props[2]);
    if (!vectorEquals(result[0], Vector4(6.0f, 8.0f, 10.0f, 12.0f))) {
      throw DxvkError("Multiply<Float4, Float> failed");
    }
    testedCount++;
  }
  
  if (testedCount != variants.size()) {
    throw DxvkError(str::format("Multiply variant count mismatch: expected ", variants.size(), ", tested ", testedCount));
  }
  
  Logger::info(str::format("Multiply component passed - all ", variants.size(), " variants tested"));
}

void testDivide() {
  
  XXH64_hash_t baseHash = XXH3_64bits("lightspeed.trex.logic.Divide", strlen("lightspeed.trex.logic.Divide"));
  const ComponentSpecVariantMap& variants = getAllComponentSpecVariants(baseHash);
  
  int testedCount = 0;
  
  // Test homogeneous variants
  {
    std::unordered_map<std::string, RtComponentPropertyType> desiredTypes = {
      {"a", RtComponentPropertyType::Float},
      {"b", RtComponentPropertyType::Float},
      {"quotient", RtComponentPropertyType::Float}
    };
    const RtComponentSpec* spec = getComponentVariant("lightspeed.trex.logic.Divide", desiredTypes);
    if (!spec) {
      throw DxvkError("Failed to find Divide<Float, Float> component");
    }
    
    std::vector<RtComponentPropertyVector> props;
    props.push_back(std::vector<float>{10.0f, 15.0f, -20.0f});  // a
    props.push_back(std::vector<float>{2.0f, 3.0f, 4.0f});      // b
    props.push_back(std::vector<float>{0.0f, 0.0f, 0.0f});      // quotient
    
    std::vector<size_t> indices = {0, 1, 2};
    MockGraphBatch batch;
    auto comp = spec->createComponentBatch(batch, props, indices);
    comp->updateRange(nullptr, 0, 3);
    
    auto& result = std::get<std::vector<float>>(props[2]);
    if (!floatEquals(result[0], 5.0f) || !floatEquals(result[1], 5.0f) || !floatEquals(result[2], -5.0f)) {
      throw DxvkError("Divide<Float, Float> failed");
    }
    testedCount++;
  }
  
  {
    std::unordered_map<std::string, RtComponentPropertyType> desiredTypes = {
      {"a", RtComponentPropertyType::Float2},
      {"b", RtComponentPropertyType::Float2},
      {"quotient", RtComponentPropertyType::Float2}
    };
    const RtComponentSpec* spec = getComponentVariant("lightspeed.trex.logic.Divide", desiredTypes);
    if (!spec) {
      throw DxvkError("Failed to find Divide<Float2, Float2> component");
    }
    
    std::vector<RtComponentPropertyVector> props;
    props.push_back(std::vector<Vector2>{Vector2(12.0f, 15.0f)});  // a
    props.push_back(std::vector<Vector2>{Vector2(3.0f, 5.0f)});    // b
    props.push_back(std::vector<Vector2>{Vector2()});              // quotient
    
    std::vector<size_t> indices = {0, 1, 2};
    MockGraphBatch batch;
    auto comp = spec->createComponentBatch(batch, props, indices);
    comp->updateRange(nullptr, 0, 1);
    
    auto& result = std::get<std::vector<Vector2>>(props[2]);
    if (!vectorEquals(result[0], Vector2(4.0f, 3.0f))) {
      throw DxvkError("Divide<Float2, Float2> failed");
    }
    testedCount++;
  }
  
  {
    std::unordered_map<std::string, RtComponentPropertyType> desiredTypes = {
      {"a", RtComponentPropertyType::Float3},
      {"b", RtComponentPropertyType::Float3},
      {"quotient", RtComponentPropertyType::Float3}
    };
    const RtComponentSpec* spec = getComponentVariant("lightspeed.trex.logic.Divide", desiredTypes);
    if (!spec) {
      throw DxvkError("Failed to find Divide<Float3, Float3> component");
    }
    
    std::vector<RtComponentPropertyVector> props;
    props.push_back(std::vector<Vector3>{Vector3(12.0f, 15.0f, 20.0f)});  // a
    props.push_back(std::vector<Vector3>{Vector3(3.0f, 5.0f, 4.0f)});     // b
    props.push_back(std::vector<Vector3>{Vector3()});                      // quotient
    
    std::vector<size_t> indices = {0, 1, 2};
    MockGraphBatch batch;
    auto comp = spec->createComponentBatch(batch, props, indices);
    comp->updateRange(nullptr, 0, 1);
    
    auto& result = std::get<std::vector<Vector3>>(props[2]);
    if (!vectorEquals(result[0], Vector3(4.0f, 3.0f, 5.0f))) {
      throw DxvkError("Divide<Float3, Float3> failed");
    }
    testedCount++;
  }
  
  {
    std::unordered_map<std::string, RtComponentPropertyType> desiredTypes = {
      {"a", RtComponentPropertyType::Float4},
      {"b", RtComponentPropertyType::Float4},
      {"quotient", RtComponentPropertyType::Float4}
    };
    const RtComponentSpec* spec = getComponentVariant("lightspeed.trex.logic.Divide", desiredTypes);
    if (!spec) {
      throw DxvkError("Failed to find Divide<Float4, Float4> component");
    }
    
    std::vector<RtComponentPropertyVector> props;
    props.push_back(std::vector<Vector4>{Vector4(12.0f, 15.0f, 20.0f, 24.0f)});  // a
    props.push_back(std::vector<Vector4>{Vector4(3.0f, 5.0f, 4.0f, 6.0f)});      // b
    props.push_back(std::vector<Vector4>{Vector4()});                             // quotient
    
    std::vector<size_t> indices = {0, 1, 2};
    MockGraphBatch batch;
    auto comp = spec->createComponentBatch(batch, props, indices);
    comp->updateRange(nullptr, 0, 1);
    
    auto& result = std::get<std::vector<Vector4>>(props[2]);
    if (!vectorEquals(result[0], Vector4(4.0f, 3.0f, 5.0f, 4.0f))) {
      throw DxvkError("Divide<Float4, Float4> failed");
    }
    testedCount++;
  }
  
  // Test mixed-type variants: Vector / Float (dividing a vector by a scalar)
  {
    std::unordered_map<std::string, RtComponentPropertyType> desiredTypes = {
      {"a", RtComponentPropertyType::Float2},
      {"b", RtComponentPropertyType::Float},
      {"quotient", RtComponentPropertyType::Float2}
    };
    const RtComponentSpec* spec = getComponentVariant("lightspeed.trex.logic.Divide", desiredTypes);
    if (!spec) {
      throw DxvkError("Failed to find Divide<Float2, Float> component");
    }
    
    std::vector<RtComponentPropertyVector> props;
    props.push_back(std::vector<Vector2>{Vector2(12.0f, 16.0f)});  // a
    props.push_back(std::vector<float>{4.0f});                      // b
    props.push_back(std::vector<Vector2>{Vector2()});              // quotient
    
    std::vector<size_t> indices = {0, 1, 2};
    MockGraphBatch batch;
    auto comp = spec->createComponentBatch(batch, props, indices);
    comp->updateRange(nullptr, 0, 1);
    
    auto& result = std::get<std::vector<Vector2>>(props[2]);
    if (!vectorEquals(result[0], Vector2(3.0f, 4.0f))) {
      throw DxvkError("Divide<Float2, Float> failed");
    }
    testedCount++;
  }
  
  {
    std::unordered_map<std::string, RtComponentPropertyType> desiredTypes = {
      {"a", RtComponentPropertyType::Float3},
      {"b", RtComponentPropertyType::Float},
      {"quotient", RtComponentPropertyType::Float3}
    };
    const RtComponentSpec* spec = getComponentVariant("lightspeed.trex.logic.Divide", desiredTypes);
    if (!spec) {
      throw DxvkError("Failed to find Divide<Float3, Float> component");
    }
    
    std::vector<RtComponentPropertyVector> props;
    props.push_back(std::vector<Vector3>{Vector3(12.0f, 16.0f, 20.0f)});  // a
    props.push_back(std::vector<float>{4.0f});                             // b
    props.push_back(std::vector<Vector3>{Vector3()});                      // quotient
    
    std::vector<size_t> indices = {0, 1, 2};
    MockGraphBatch batch;
    auto comp = spec->createComponentBatch(batch, props, indices);
    comp->updateRange(nullptr, 0, 1);
    
    auto& result = std::get<std::vector<Vector3>>(props[2]);
    if (!vectorEquals(result[0], Vector3(3.0f, 4.0f, 5.0f))) {
      throw DxvkError("Divide<Float3, Float> failed");
    }
    testedCount++;
  }
  
  {
    std::unordered_map<std::string, RtComponentPropertyType> desiredTypes = {
      {"a", RtComponentPropertyType::Float4},
      {"b", RtComponentPropertyType::Float},
      {"quotient", RtComponentPropertyType::Float4}
    };
    const RtComponentSpec* spec = getComponentVariant("lightspeed.trex.logic.Divide", desiredTypes);
    if (!spec) {
      throw DxvkError("Failed to find Divide<Float4, Float> component");
    }
    
    std::vector<RtComponentPropertyVector> props;
    props.push_back(std::vector<Vector4>{Vector4(12.0f, 16.0f, 20.0f, 24.0f)});  // a
    props.push_back(std::vector<float>{4.0f});                                    // b
    props.push_back(std::vector<Vector4>{Vector4()});                             // quotient
    
    std::vector<size_t> indices = {0, 1, 2};
    MockGraphBatch batch;
    auto comp = spec->createComponentBatch(batch, props, indices);
    comp->updateRange(nullptr, 0, 1);
    
    auto& result = std::get<std::vector<Vector4>>(props[2]);
    if (!vectorEquals(result[0], Vector4(3.0f, 4.0f, 5.0f, 6.0f))) {
      throw DxvkError("Divide<Float4, Float> failed");
    }
    testedCount++;
  }
  
  if (testedCount != variants.size()) {
    throw DxvkError(str::format("Divide variant count mismatch: expected ", variants.size(), ", tested ", testedCount));
  }
  
  Logger::info(str::format("Divide component passed - all ", variants.size(), " variants tested"));
}

void testClamp() {
  
  XXH64_hash_t baseHash = XXH3_64bits("lightspeed.trex.logic.Clamp", strlen("lightspeed.trex.logic.Clamp"));
  const ComponentSpecVariantMap& variants = getAllComponentSpecVariants(baseHash);
  
  // Test Float variant
  {
    std::unordered_map<std::string, RtComponentPropertyType> desiredTypes = {
      {"value", RtComponentPropertyType::Float},
      {"result", RtComponentPropertyType::Float}
    };
    std::vector<RtComponentPropertyVector> props;
    props.push_back(std::vector<float>{-5.0f, 5.0f, 15.0f});  // value
    props.push_back(std::vector<float>{0.0f, 0.0f, 0.0f});    // minValue
    props.push_back(std::vector<float>{10.0f, 10.0f, 10.0f}); // maxValue
    props.push_back(std::vector<float>{0.0f, 0.0f, 0.0f});    // result
    
    auto& result = testComponentVariant<float>("lightspeed.trex.logic.Clamp", desiredTypes, props, 3, 0, 3);
    if (!floatEquals(result[0], 0.0f) || !floatEquals(result[1], 5.0f) || !floatEquals(result[2], 10.0f)) {
      throw DxvkError("Clamp<Float> failed");
    }
  }
  
  // Test Float2 variant
  {
    std::unordered_map<std::string, RtComponentPropertyType> desiredTypes = {
      {"value", RtComponentPropertyType::Float2},
      {"result", RtComponentPropertyType::Float2}
    };
    std::vector<RtComponentPropertyVector> props;
    props.push_back(std::vector<Vector2>{Vector2(-5.0f, 15.0f), Vector2(5.0f, 8.0f)});  // value
    props.push_back(std::vector<float>{0.0f, 0.0f});                                     // minValue
    props.push_back(std::vector<float>{10.0f, 10.0f});                                   // maxValue
    props.push_back(std::vector<Vector2>{Vector2(), Vector2()});                         // result
    
    auto& result = testComponentVariant<Vector2>("lightspeed.trex.logic.Clamp", desiredTypes, props, 3, 0, 2);
    if (!vectorEquals(result[0], Vector2(0.0f, 10.0f)) || 
        !vectorEquals(result[1], Vector2(5.0f, 8.0f))) {
      throw DxvkError("Clamp<Float2> failed");
    }
  }
  
  // Test Float3 variant
  {
    std::unordered_map<std::string, RtComponentPropertyType> desiredTypes = {
      {"value", RtComponentPropertyType::Float3},
      {"result", RtComponentPropertyType::Float3}
    };
    std::vector<RtComponentPropertyVector> props;
    props.push_back(std::vector<Vector3>{Vector3(-5.0f, 5.0f, 15.0f)});  // value
    props.push_back(std::vector<float>{0.0f});                            // minValue
    props.push_back(std::vector<float>{10.0f});                           // maxValue
    props.push_back(std::vector<Vector3>{Vector3()});                     // result
    
    auto& result = testComponentVariant<Vector3>("lightspeed.trex.logic.Clamp", desiredTypes, props, 3, 0, 1);
    if (!vectorEquals(result[0], Vector3(0.0f, 5.0f, 10.0f))) {
      throw DxvkError("Clamp<Float3> failed");
    }
  }
  
  // Test Float4 variant
  {
    std::unordered_map<std::string, RtComponentPropertyType> desiredTypes = {
      {"value", RtComponentPropertyType::Float4},
      {"result", RtComponentPropertyType::Float4}
    };
    std::vector<RtComponentPropertyVector> props;
    props.push_back(std::vector<Vector4>{Vector4(-5.0f, 5.0f, 15.0f, 12.0f)});  // value
    props.push_back(std::vector<float>{0.0f});                                   // minValue
    props.push_back(std::vector<float>{10.0f});                                  // maxValue
    props.push_back(std::vector<Vector4>{Vector4()});                            // result
    
    auto& result = testComponentVariant<Vector4>("lightspeed.trex.logic.Clamp", desiredTypes, props, 3, 0, 1);
    if (!vectorEquals(result[0], Vector4(0.0f, 5.0f, 10.0f, 10.0f))) {
      throw DxvkError("Clamp<Float4> failed");
    }
  }
  
  if (variants.size() != 4) {
    throw DxvkError(str::format("Clamp variant count mismatch: expected 4, tested 4, found ", variants.size()));
  }
  
  Logger::info("Clamp component passed (Float, Float2, Float3, Float4)");
}

void testMin() {
  
  XXH64_hash_t baseHash = XXH3_64bits("lightspeed.trex.logic.Min", strlen("lightspeed.trex.logic.Min"));
  const ComponentSpecVariantMap& variants = getAllComponentSpecVariants(baseHash);
  
  // Test Float variant
  {
    std::unordered_map<std::string, RtComponentPropertyType> desiredTypes = {
      {"a", RtComponentPropertyType::Float},
      {"b", RtComponentPropertyType::Float},
      {"result", RtComponentPropertyType::Float}
    };
    std::vector<RtComponentPropertyVector> props;
    props.push_back(std::vector<float>{5.0f, 2.0f, 10.0f});  // a
    props.push_back(std::vector<float>{3.0f, 8.0f, 10.0f});  // b
    props.push_back(std::vector<float>{0.0f, 0.0f, 0.0f});   // result
    
    auto& result = testComponentVariant<float>("lightspeed.trex.logic.Min", desiredTypes, props, 2, 0, 3);
    if (!floatEquals(result[0], 3.0f) || !floatEquals(result[1], 2.0f) || !floatEquals(result[2], 10.0f)) {
      throw DxvkError("Min<Float> failed");
    }
  }
  
  // Test Float2 variant
  {
    std::unordered_map<std::string, RtComponentPropertyType> desiredTypes = {
      {"a", RtComponentPropertyType::Float2},
      {"b", RtComponentPropertyType::Float2},
      {"result", RtComponentPropertyType::Float2}
    };
    std::vector<RtComponentPropertyVector> props;
    props.push_back(std::vector<Vector2>{Vector2(5.0f, 2.0f)});  // a
    props.push_back(std::vector<Vector2>{Vector2(3.0f, 8.0f)});  // b
    props.push_back(std::vector<Vector2>{Vector2()});            // result
    
    auto& result = testComponentVariant<Vector2>("lightspeed.trex.logic.Min", desiredTypes, props, 2, 0, 1);
    if (!vectorEquals(result[0], Vector2(3.0f, 2.0f))) {
      throw DxvkError("Min<Float2> failed");
    }
  }
  
  // Test Float3 variant
  {
    std::unordered_map<std::string, RtComponentPropertyType> desiredTypes = {
      {"a", RtComponentPropertyType::Float3},
      {"b", RtComponentPropertyType::Float3},
      {"result", RtComponentPropertyType::Float3}
    };
    std::vector<RtComponentPropertyVector> props;
    props.push_back(std::vector<Vector3>{Vector3(5.0f, 2.0f, 10.0f)});  // a
    props.push_back(std::vector<Vector3>{Vector3(3.0f, 8.0f, 10.0f)});  // b
    props.push_back(std::vector<Vector3>{Vector3()});                    // result
    
    auto& result = testComponentVariant<Vector3>("lightspeed.trex.logic.Min", desiredTypes, props, 2, 0, 1);
    if (!vectorEquals(result[0], Vector3(3.0f, 2.0f, 10.0f))) {
      throw DxvkError("Min<Float3> failed");
    }
  }
  
  // Test Float4 variant
  {
    std::unordered_map<std::string, RtComponentPropertyType> desiredTypes = {
      {"a", RtComponentPropertyType::Float4},
      {"b", RtComponentPropertyType::Float4},
      {"result", RtComponentPropertyType::Float4}
    };
    std::vector<RtComponentPropertyVector> props;
    props.push_back(std::vector<Vector4>{Vector4(5.0f, 2.0f, 10.0f, 1.0f)});  // a
    props.push_back(std::vector<Vector4>{Vector4(3.0f, 8.0f, 10.0f, 2.0f)});  // b
    props.push_back(std::vector<Vector4>{Vector4()});                          // result
    
    auto& result = testComponentVariant<Vector4>("lightspeed.trex.logic.Min", desiredTypes, props, 2, 0, 1);
    if (!vectorEquals(result[0], Vector4(3.0f, 2.0f, 10.0f, 1.0f))) {
      throw DxvkError("Min<Float4> failed");
    }
  }
  
  if (variants.size() != 4) {
    throw DxvkError(str::format("Min variant count mismatch: expected 4, tested 4, found ", variants.size()));
  }
  
  Logger::info("Min component passed (Float, Float2, Float3, Float4)");
}

void testMax() {
  
  XXH64_hash_t baseHash = XXH3_64bits("lightspeed.trex.logic.Max", strlen("lightspeed.trex.logic.Max"));
  const ComponentSpecVariantMap& variants = getAllComponentSpecVariants(baseHash);
  
  // Test Float variant
  {
    std::unordered_map<std::string, RtComponentPropertyType> desiredTypes = {
      {"a", RtComponentPropertyType::Float},
      {"b", RtComponentPropertyType::Float},
      {"result", RtComponentPropertyType::Float}
    };
    std::vector<RtComponentPropertyVector> props;
    props.push_back(std::vector<float>{5.0f, 2.0f, 10.0f});  // a
    props.push_back(std::vector<float>{3.0f, 8.0f, 10.0f});  // b
    props.push_back(std::vector<float>{0.0f, 0.0f, 0.0f});   // result
    
    auto& result = testComponentVariant<float>("lightspeed.trex.logic.Max", desiredTypes, props, 2, 0, 3);
    if (!floatEquals(result[0], 5.0f) || !floatEquals(result[1], 8.0f) || !floatEquals(result[2], 10.0f)) {
      throw DxvkError("Max<Float> failed");
    }
  }
  
  // Test Float2 variant
  {
    std::unordered_map<std::string, RtComponentPropertyType> desiredTypes = {
      {"a", RtComponentPropertyType::Float2},
      {"b", RtComponentPropertyType::Float2},
      {"result", RtComponentPropertyType::Float2}
    };
    std::vector<RtComponentPropertyVector> props;
    props.push_back(std::vector<Vector2>{Vector2(5.0f, 2.0f)});  // a
    props.push_back(std::vector<Vector2>{Vector2(3.0f, 8.0f)});  // b
    props.push_back(std::vector<Vector2>{Vector2()});            // result
    
    auto& result = testComponentVariant<Vector2>("lightspeed.trex.logic.Max", desiredTypes, props, 2, 0, 1);
    if (!vectorEquals(result[0], Vector2(5.0f, 8.0f))) {
      throw DxvkError("Max<Float2> failed");
    }
  }
  
  // Test Float3 variant
  {
    std::unordered_map<std::string, RtComponentPropertyType> desiredTypes = {
      {"a", RtComponentPropertyType::Float3},
      {"b", RtComponentPropertyType::Float3},
      {"result", RtComponentPropertyType::Float3}
    };
    std::vector<RtComponentPropertyVector> props;
    props.push_back(std::vector<Vector3>{Vector3(5.0f, 2.0f, 10.0f)});  // a
    props.push_back(std::vector<Vector3>{Vector3(3.0f, 8.0f, 10.0f)});  // b
    props.push_back(std::vector<Vector3>{Vector3()});                    // result
    
    auto& result = testComponentVariant<Vector3>("lightspeed.trex.logic.Max", desiredTypes, props, 2, 0, 1);
    if (!vectorEquals(result[0], Vector3(5.0f, 8.0f, 10.0f))) {
      throw DxvkError("Max<Float3> failed");
    }
  }
  
  // Test Float4 variant
  {
    std::unordered_map<std::string, RtComponentPropertyType> desiredTypes = {
      {"a", RtComponentPropertyType::Float4},
      {"b", RtComponentPropertyType::Float4},
      {"result", RtComponentPropertyType::Float4}
    };
    std::vector<RtComponentPropertyVector> props;
    props.push_back(std::vector<Vector4>{Vector4(5.0f, 2.0f, 10.0f, 1.0f)});  // a
    props.push_back(std::vector<Vector4>{Vector4(3.0f, 8.0f, 10.0f, 2.0f)});  // b
    props.push_back(std::vector<Vector4>{Vector4()});                          // result
    
    auto& result = testComponentVariant<Vector4>("lightspeed.trex.logic.Max", desiredTypes, props, 2, 0, 1);
    if (!vectorEquals(result[0], Vector4(5.0f, 8.0f, 10.0f, 2.0f))) {
      throw DxvkError("Max<Float4> failed");
    }
  }
  
  if (variants.size() != 4) {
    throw DxvkError(str::format("Max variant count mismatch: expected 4, tested 4, found ", variants.size()));
  }
  
  Logger::info("Max component passed (Float, Float2, Float3, Float4)");
}

void testFloor() {
  
  const RtComponentSpec* spec = getComponentSpec("lightspeed.trex.logic.Floor");
  if (!spec) {
    throw DxvkError("Failed to find Floor component");
  }
  
  std::vector<RtComponentPropertyVector> props;
  props.push_back(std::vector<float>{2.7f, -2.7f, 5.0f});  // value
  props.push_back(std::vector<float>{0.0f, 0.0f, 0.0f});   // result
  
  std::vector<size_t> indices = {0, 1};
  MockGraphBatch batch;
  auto comp = spec->createComponentBatch(batch, props, indices);
  comp->updateRange(nullptr, 0, 3);
  
  auto& result = std::get<std::vector<float>>(props[1]);
  if (!floatEquals(result[0], 2.0f) || !floatEquals(result[1], -3.0f) || !floatEquals(result[2], 5.0f)) {
    throw DxvkError("Floor failed");
  }
  Logger::info("Floor component passed");
}

void testCeil() {
  
  const RtComponentSpec* spec = getComponentSpec("lightspeed.trex.logic.Ceil");
  if (!spec) {
    throw DxvkError("Failed to find Ceil component");
  }
  
  std::vector<RtComponentPropertyVector> props;
  props.push_back(std::vector<float>{2.3f, -2.3f, 5.0f});  // value
  props.push_back(std::vector<float>{0.0f, 0.0f, 0.0f});   // result
  
  std::vector<size_t> indices = {0, 1};
  MockGraphBatch batch;
  auto comp = spec->createComponentBatch(batch, props, indices);
  comp->updateRange(nullptr, 0, 3);
  
  auto& result = std::get<std::vector<float>>(props[1]);
  if (!floatEquals(result[0], 3.0f) || !floatEquals(result[1], -2.0f) || !floatEquals(result[2], 5.0f)) {
    throw DxvkError("Ceil failed");
  }
  Logger::info("Ceil component passed");
}

void testRound() {
  
  const RtComponentSpec* spec = getComponentSpec("lightspeed.trex.logic.Round");
  if (!spec) {
    throw DxvkError("Failed to find Round component");
  }
  
  std::vector<RtComponentPropertyVector> props;
  props.push_back(std::vector<float>{2.3f, 2.7f, -2.3f});  // value
  props.push_back(std::vector<float>{0.0f, 0.0f, 0.0f});   // result
  
  std::vector<size_t> indices = {0, 1};
  MockGraphBatch batch;
  auto comp = spec->createComponentBatch(batch, props, indices);
  comp->updateRange(nullptr, 0, 3);
  
  auto& result = std::get<std::vector<float>>(props[1]);
  if (!floatEquals(result[0], 2.0f) || !floatEquals(result[1], 3.0f) || !floatEquals(result[2], -2.0f)) {
    throw DxvkError("Round failed");
  }
  Logger::info("Round component passed");
}

void testInvert() {
  
  XXH64_hash_t baseHash = XXH3_64bits("lightspeed.trex.logic.Invert", strlen("lightspeed.trex.logic.Invert"));
  const ComponentSpecVariantMap& variants = getAllComponentSpecVariants(baseHash);
  
  int testedCount = 0;
  
  // Test Float variant
  {
    std::unordered_map<std::string, RtComponentPropertyType> desiredTypes = {
      {"input", RtComponentPropertyType::Float},
      {"output", RtComponentPropertyType::Float}
    };
    std::vector<RtComponentPropertyVector> props;
    props.push_back(std::vector<float>{0.2f, 0.8f, 1.0f});  // input
    props.push_back(std::vector<float>{0.0f, 0.0f, 0.0f});  // output
    
    auto& result = testComponentVariant<float>("lightspeed.trex.logic.Invert", desiredTypes, props, 1, 0, 3);
    // Invert calculates 1.0 - input
    if (!floatEquals(result[0], 0.8f) || !floatEquals(result[1], 0.2f) || !floatEquals(result[2], 0.0f)) {
      throw DxvkError("Invert<Float> failed");
    }
    testedCount++;
  }
  
  // Test Float2 variant
  {
    std::unordered_map<std::string, RtComponentPropertyType> desiredTypes = {
      {"input", RtComponentPropertyType::Float2},
      {"output", RtComponentPropertyType::Float2}
    };
    std::vector<RtComponentPropertyVector> props;
    props.push_back(std::vector<Vector2>{Vector2(0.2f, 0.8f), Vector2(0.0f, 1.0f)});  // input
    props.push_back(std::vector<Vector2>{Vector2(), Vector2()});  // output
    
    auto& result = testComponentVariant<Vector2>("lightspeed.trex.logic.Invert", desiredTypes, props, 1, 0, 2);
    if (!vectorEquals(result[0], Vector2(0.8f, 0.2f)) || !vectorEquals(result[1], Vector2(1.0f, 0.0f))) {
      throw DxvkError("Invert<Float2> failed");
    }
    testedCount++;
  }
  
  // Test Float3 variant
  {
    std::unordered_map<std::string, RtComponentPropertyType> desiredTypes = {
      {"input", RtComponentPropertyType::Float3},
      {"output", RtComponentPropertyType::Float3}
    };
    std::vector<RtComponentPropertyVector> props;
    props.push_back(std::vector<Vector3>{Vector3(0.2f, 0.5f, 0.8f)});  // input
    props.push_back(std::vector<Vector3>{Vector3()});  // output
    
    auto& result = testComponentVariant<Vector3>("lightspeed.trex.logic.Invert", desiredTypes, props, 1, 0, 1);
    if (!vectorEquals(result[0], Vector3(0.8f, 0.5f, 0.2f))) {
      throw DxvkError("Invert<Float3> failed");
    }
    testedCount++;
  }
  
  // Test Float4 variant
  {
    std::unordered_map<std::string, RtComponentPropertyType> desiredTypes = {
      {"input", RtComponentPropertyType::Float4},
      {"output", RtComponentPropertyType::Float4}
    };
    std::vector<RtComponentPropertyVector> props;
    props.push_back(std::vector<Vector4>{Vector4(0.2f, 0.4f, 0.6f, 0.8f)});  // input
    props.push_back(std::vector<Vector4>{Vector4()});  // output
    
    auto& result = testComponentVariant<Vector4>("lightspeed.trex.logic.Invert", desiredTypes, props, 1, 0, 1);
    if (!vectorEquals(result[0], Vector4(0.8f, 0.6f, 0.4f, 0.2f))) {
      throw DxvkError("Invert<Float4> failed");
    }
    testedCount++;
  }
  
  if (variants.size() != static_cast<size_t>(testedCount)) {
    throw DxvkError(str::format("Invert variant count mismatch: expected ", testedCount, ", tested ", testedCount, ", found ", variants.size()));
  }
  
  Logger::info("Invert component passed (Float, Float2, Float3, Float4)");
}

//=============================================================================
// COMPARISON COMPONENTS
//=============================================================================

void testEqualTo() {
  
  XXH64_hash_t baseHash = XXH3_64bits("lightspeed.trex.logic.EqualTo", strlen("lightspeed.trex.logic.EqualTo"));
  const ComponentSpecVariantMap& variants = getAllComponentSpecVariants(baseHash);
  
  int testedCount = 0;
  
  // Test Float variant with tolerance
  {
    std::unordered_map<std::string, RtComponentPropertyType> desiredTypes = {
      {"a", RtComponentPropertyType::Float},
      {"b", RtComponentPropertyType::Float}
    };
    std::vector<RtComponentPropertyVector> props;
    props.push_back(std::vector<float>{5.0f, 3.0f, 2.0f, 1.0f});      // a
    props.push_back(std::vector<float>{5.0f, 4.0f, 2.0f, 1.05f});     // b
    props.push_back(std::vector<float>{0.00001f, 0.00001f, 0.00001f, 0.1f});  // tolerance
    props.push_back(std::vector<uint32_t>{0, 0, 0, 0});               // result
    
    auto& result = testComponentVariant<uint32_t>("lightspeed.trex.logic.EqualTo", desiredTypes, props, 3, 0, 4);
    // [0]: 5.0 == 5.0 (within tiny tolerance) -> true
    // [1]: 3.0 != 4.0 (diff=1.0, outside tiny tolerance) -> false
    // [2]: 2.0 == 2.0 (within tiny tolerance) -> true
    // [3]: 1.0 ~= 1.05 (diff=0.05, within 0.1 tolerance) -> true
    if (result[0] != 1 || result[1] != 0 || result[2] != 1 || result[3] != 1) {
      throw DxvkError("EqualTo<Float, Float> failed");
    }
    testedCount++;
  }
  
  // Test Float variant tolerance boundary
  {
    std::unordered_map<std::string, RtComponentPropertyType> desiredTypes = {
      {"a", RtComponentPropertyType::Float},
      {"b", RtComponentPropertyType::Float}
    };
    std::vector<RtComponentPropertyVector> props;
    props.push_back(std::vector<float>{0.0f, 0.0f});       // a
    props.push_back(std::vector<float>{0.09f, 0.11f});     // b
    props.push_back(std::vector<float>{0.1f, 0.1f});       // tolerance
    props.push_back(std::vector<uint32_t>{0, 0});          // result
    
    auto& result = testComponentVariant<uint32_t>("lightspeed.trex.logic.EqualTo", desiredTypes, props, 3, 0, 2);
    // [0]: |0.0 - 0.09| = 0.09 < 0.1 -> true
    // [1]: |0.0 - 0.11| = 0.11 >= 0.1 -> false
    if (result[0] != 1 || result[1] != 0) {
      throw DxvkError("EqualTo<Float, Float> tolerance boundary failed");
    }
    testedCount++;
  }
  
  // Test Float2 variant with tolerance
  {
    std::unordered_map<std::string, RtComponentPropertyType> desiredTypes = {
      {"a", RtComponentPropertyType::Float2},
      {"b", RtComponentPropertyType::Float2}
    };
    std::vector<RtComponentPropertyVector> props;
    props.push_back(std::vector<Vector2>{Vector2(1.0f, 2.0f), Vector2(3.0f, 4.0f), Vector2(0.0f, 0.0f)});  // a
    props.push_back(std::vector<Vector2>{Vector2(1.0f, 2.0f), Vector2(3.0f, 5.0f), Vector2(0.05f, 0.05f)});  // b
    props.push_back(std::vector<float>{0.00001f, 0.00001f, 0.1f});  // tolerance
    props.push_back(std::vector<uint32_t>{0, 0, 0});  // result
    
    auto& result = testComponentVariant<uint32_t>("lightspeed.trex.logic.EqualTo", desiredTypes, props, 3, 0, 3);
    // [0]: (1,2) == (1,2) -> true
    // [1]: (3,4) != (3,5) (diff length = 1.0) -> false
    // [2]: (0,0) ~= (0.05,0.05) (diff length = ~0.07 < 0.1) -> true
    if (result[0] != 1 || result[1] != 0 || result[2] != 1) {
      throw DxvkError("EqualTo<Float2, Float2> failed");
    }
    testedCount++;
  }
  
  // Test Float3 variant with tolerance
  {
    std::unordered_map<std::string, RtComponentPropertyType> desiredTypes = {
      {"a", RtComponentPropertyType::Float3},
      {"b", RtComponentPropertyType::Float3}
    };
    std::vector<RtComponentPropertyVector> props;
    props.push_back(std::vector<Vector3>{Vector3(1.0f, 2.0f, 3.0f), Vector3(4.0f, 5.0f, 6.0f)});  // a
    props.push_back(std::vector<Vector3>{Vector3(1.0f, 2.0f, 3.0f), Vector3(4.0f, 5.0f, 7.0f)});  // b
    props.push_back(std::vector<float>{0.00001f, 0.00001f});  // tolerance
    props.push_back(std::vector<uint32_t>{0, 0});  // result
    
    auto& result = testComponentVariant<uint32_t>("lightspeed.trex.logic.EqualTo", desiredTypes, props, 3, 0, 2);
    if (result[0] != 1 || result[1] != 0) {
      throw DxvkError("EqualTo<Float3, Float3> failed");
    }
    testedCount++;
  }
  
  // Test Float4 variant with tolerance
  {
    std::unordered_map<std::string, RtComponentPropertyType> desiredTypes = {
      {"a", RtComponentPropertyType::Float4},
      {"b", RtComponentPropertyType::Float4}
    };
    std::vector<RtComponentPropertyVector> props;
    props.push_back(std::vector<Vector4>{Vector4(1.0f, 2.0f, 3.0f, 4.0f)});  // a
    props.push_back(std::vector<Vector4>{Vector4(1.0f, 2.0f, 3.0f, 4.0f)});  // b
    props.push_back(std::vector<float>{0.00001f});  // tolerance
    props.push_back(std::vector<uint32_t>{0});  // result
    
    auto& result = testComponentVariant<uint32_t>("lightspeed.trex.logic.EqualTo", desiredTypes, props, 3, 0, 1);
    if (result[0] != 1) {
      throw DxvkError("EqualTo<Float4, Float4> failed");
    }
    testedCount++;
  }
  
  // Note: EqualTo supports mixed types (e.g., Float == Float2), but we test homogeneous types here
  Logger::info(str::format("EqualTo component passed (tested ", testedCount, " homogeneous variants, ", variants.size(), " total variants registered)"));
}

void testLessThan() {
  
  std::unordered_map<std::string, RtComponentPropertyType> desiredTypes = {
    {"a", RtComponentPropertyType::Float},
    {"b", RtComponentPropertyType::Float}
  };
  const RtComponentSpec* spec = getComponentVariant("lightspeed.trex.logic.LessThan", desiredTypes);
  if (!spec) {
    throw DxvkError("Failed to find LessThan component");
  }
  
  std::vector<RtComponentPropertyVector> props;
  props.push_back(std::vector<float>{3.0f, 5.0f, 5.0f});  // a
  props.push_back(std::vector<float>{5.0f, 3.0f, 5.0f});  // b
  props.push_back(std::vector<uint32_t>{0, 0, 0});        // result
  
  std::vector<size_t> indices = {0, 1, 2};
  MockGraphBatch batch;
  auto comp = spec->createComponentBatch(batch, props, indices);
  comp->updateRange(nullptr, 0, 3);
  
  auto& result = std::get<std::vector<uint32_t>>(props[2]);
  if (result[0] != 1 || result[1] != 0 || result[2] != 0) {
    throw DxvkError("LessThan failed");
  }
  Logger::info("LessThan component passed");
}

void testGreaterThan() {
  
  std::unordered_map<std::string, RtComponentPropertyType> desiredTypes = {
    {"a", RtComponentPropertyType::Float},
    {"b", RtComponentPropertyType::Float}
  };
  const RtComponentSpec* spec = getComponentVariant("lightspeed.trex.logic.GreaterThan", desiredTypes);
  if (!spec) {
    throw DxvkError("Failed to find GreaterThan component");
  }
  
  std::vector<RtComponentPropertyVector> props;
  props.push_back(std::vector<float>{5.0f, 3.0f, 5.0f});  // a
  props.push_back(std::vector<float>{3.0f, 5.0f, 5.0f});  // b
  props.push_back(std::vector<uint32_t>{0, 0, 0});        // result
  
  std::vector<size_t> indices = {0, 1, 2};
  MockGraphBatch batch;
  auto comp = spec->createComponentBatch(batch, props, indices);
  comp->updateRange(nullptr, 0, 3);
  
  auto& result = std::get<std::vector<uint32_t>>(props[2]);
  if (result[0] != 1 || result[1] != 0 || result[2] != 0) {
    throw DxvkError("GreaterThan failed");
  }
  Logger::info("GreaterThan component passed");
}

void testBetween() {
  
  const RtComponentSpec* spec = getComponentSpec("lightspeed.trex.logic.Between");
  if (!spec) {
    throw DxvkError("Failed to find Between component");
  }
  
  // Test basic functionality and edge cases
  std::vector<RtComponentPropertyVector> props;
  props.push_back(std::vector<float>{5.0f, 0.0f, 15.0f, 10.0f, 5.0f, 5.0f, 5.0f});  // value
  props.push_back(std::vector<float>{0.0f, 0.0f, 0.0f,  0.0f, 5.0f, 10.0f, 0.0f});  // minValue
  props.push_back(std::vector<float>{10.0f, 10.0f, 10.0f, 10.0f, 5.0f, 5.0f, 10.0f}); // maxValue
  props.push_back(std::vector<uint32_t>{0, 0, 0, 0, 0, 0, 0});                       // result
  
  std::vector<size_t> indices = {0, 1, 2, 3};
  MockGraphBatch batch;
  auto comp = spec->createComponentBatch(batch, props, indices);
  comp->updateRange(nullptr, 0, 7);
  
  auto& result = std::get<std::vector<uint32_t>>(props[3]);
  
  // Test 0: value=5.0, min=0.0, max=10.0 → true (within range)
  if (result[0] != 1) { throw DxvkError("Between failed: value within range"); }
  
  // Test 1: value=0.0, min=0.0, max=10.0 → true (at min boundary)
  if (result[1] != 1) { throw DxvkError("Between failed: value at min boundary"); }
  
  // Test 2: value=15.0, min=0.0, max=10.0 → false (above range)
  if (result[2] != 0) { throw DxvkError("Between failed: value above range"); }
  
  // Test 3: value=10.0, min=0.0, max=10.0 → true (at max boundary)
  if (result[3] != 1) { throw DxvkError("Between failed: value at max boundary"); }
  
  // Test 4: value=5.0, min=5.0, max=5.0 → true (min == max, value equals both)
  if (result[4] != 1) { throw DxvkError("Between failed: min == max, value equals both"); }
  
  // Test 5: value=5.0, min=10.0, max=5.0 → false (invalid range: max < min)
  if (result[5] != 0) { throw DxvkError("Between failed: invalid range (max < min)"); }
  
  // Test 6: value=5.0, min=0.0, max=10.0 → true (reconfirm basic case)
  if (result[6] != 1) { throw DxvkError("Between failed: basic case reconfirmation"); }
  
  Logger::info("Between component passed (including edge cases: at max, min==max, max<min)");
}

//=============================================================================
// BOOLEAN COMPONENTS
//=============================================================================

void testBoolAnd() {
  
  const RtComponentSpec* spec = getComponentSpec("lightspeed.trex.logic.BoolAnd");
  if (!spec) {
    throw DxvkError("Failed to find BoolAnd component");
  }
  
  std::vector<RtComponentPropertyVector> props;
  props.push_back(std::vector<uint32_t>{1, 1, 0, 0});  // a
  props.push_back(std::vector<uint32_t>{1, 0, 1, 0});  // b
  props.push_back(std::vector<uint32_t>{0, 0, 0, 0});  // result
  
  std::vector<size_t> indices = {0, 1, 2};
  MockGraphBatch batch;
  auto comp = spec->createComponentBatch(batch, props, indices);
  comp->updateRange(nullptr, 0, 4);
  
  auto& result = std::get<std::vector<uint32_t>>(props[2]);
  if (result[0] != 1 || result[1] != 0 || result[2] != 0 || result[3] != 0) {
    throw DxvkError("BoolAnd failed");
  }
  Logger::info("BoolAnd component passed");
}

void testBoolOr() {
  
  const RtComponentSpec* spec = getComponentSpec("lightspeed.trex.logic.BoolOr");
  if (!spec) {
    throw DxvkError("Failed to find BoolOr component");
  }
  
  std::vector<RtComponentPropertyVector> props;
  props.push_back(std::vector<uint32_t>{1, 1, 0, 0});  // a
  props.push_back(std::vector<uint32_t>{1, 0, 1, 0});  // b
  props.push_back(std::vector<uint32_t>{0, 0, 0, 0});  // result
  
  std::vector<size_t> indices = {0, 1, 2};
  MockGraphBatch batch;
  auto comp = spec->createComponentBatch(batch, props, indices);
  comp->updateRange(nullptr, 0, 4);
  
  auto& result = std::get<std::vector<uint32_t>>(props[2]);
  if (result[0] != 1 || result[1] != 1 || result[2] != 1 || result[3] != 0) {
    throw DxvkError("BoolOr failed");
  }
  Logger::info("BoolOr component passed");
}

void testBoolNot() {
  
  const RtComponentSpec* spec = getComponentSpec("lightspeed.trex.logic.BoolNot");
  if (!spec) {
    throw DxvkError("Failed to find BoolNot component");
  }
  
  std::vector<RtComponentPropertyVector> props;
  props.push_back(std::vector<uint32_t>{1, 0, 1, 0});  // input
  props.push_back(std::vector<uint32_t>{0, 0, 0, 0});  // result
  
  std::vector<size_t> indices = {0, 1};
  MockGraphBatch batch;
  auto comp = spec->createComponentBatch(batch, props, indices);
  comp->updateRange(nullptr, 0, 4);
  
  auto& result = std::get<std::vector<uint32_t>>(props[1]);
  if (result[0] != 0 || result[1] != 1 || result[2] != 0 || result[3] != 1) {
    throw DxvkError("BoolNot failed");
  }
  Logger::info("BoolNot component passed");
}

//=============================================================================
// VECTOR COMPONENTS
//=============================================================================

void testComposeVector2() {
  
  const RtComponentSpec* spec = getComponentSpec("lightspeed.trex.logic.ComposeVector2");
  if (!spec) {
    throw DxvkError("Failed to find ComposeVector2 component");
  }
  
  std::vector<RtComponentPropertyVector> props;
  props.push_back(std::vector<float>{1.0f, 2.0f});          // x
  props.push_back(std::vector<float>{3.0f, 4.0f});          // y
  props.push_back(std::vector<Vector2>{Vector2(), Vector2()});  // result
  
  std::vector<size_t> indices = {0, 1, 2};
  MockGraphBatch batch;
  auto comp = spec->createComponentBatch(batch, props, indices);
  comp->updateRange(nullptr, 0, 2);
  
  auto& result = std::get<std::vector<Vector2>>(props[2]);
  if (!vectorEquals(result[0], Vector2(1.0f, 3.0f)) || !vectorEquals(result[1], Vector2(2.0f, 4.0f))) {
    throw DxvkError("ComposeVector2 failed");
  }
  Logger::info("ComposeVector2 component passed");
}

void testComposeVector3() {
  
  const RtComponentSpec* spec = getComponentSpec("lightspeed.trex.logic.ComposeVector3");
  if (!spec) {
    throw DxvkError("Failed to find ComposeVector3 component");
  }
  
  std::vector<RtComponentPropertyVector> props;
  props.push_back(std::vector<float>{1.0f, -2.0f});       // x
  props.push_back(std::vector<float>{3.0f, 4.0f});        // y
  props.push_back(std::vector<float>{5.0f, 6.0f});        // z
  props.push_back(std::vector<Vector3>{Vector3(), Vector3()});  // result
  
  std::vector<size_t> indices = {0, 1, 2, 3};
  MockGraphBatch batch;
  auto comp = spec->createComponentBatch(batch, props, indices);
  comp->updateRange(nullptr, 0, 2);
  
  auto& result = std::get<std::vector<Vector3>>(props[3]);
  if (!vectorEquals(result[0], Vector3(1.0f, 3.0f, 5.0f)) || !vectorEquals(result[1], Vector3(-2.0f, 4.0f, 6.0f))) {
    throw DxvkError("ComposeVector3 failed");
  }
  Logger::info("ComposeVector3 component passed");
}

void testComposeVector4() {
  
  const RtComponentSpec* spec = getComponentSpec("lightspeed.trex.logic.ComposeVector4");
  if (!spec) {
    throw DxvkError("Failed to find ComposeVector4 component");
  }
  
  std::vector<RtComponentPropertyVector> props;
  props.push_back(std::vector<float>{1.0f, 5.0f});            // x
  props.push_back(std::vector<float>{2.0f, 6.0f});            // y
  props.push_back(std::vector<float>{3.0f, 7.0f});            // z
  props.push_back(std::vector<float>{4.0f, 8.0f});            // w
  props.push_back(std::vector<Vector4>{Vector4(), Vector4()});  // result
  
  std::vector<size_t> indices = {0, 1, 2, 3, 4};
  MockGraphBatch batch;
  auto comp = spec->createComponentBatch(batch, props, indices);
  comp->updateRange(nullptr, 0, 2);
  
  auto& result = std::get<std::vector<Vector4>>(props[4]);
  if (!vectorEquals(result[0], Vector4(1.0f, 2.0f, 3.0f, 4.0f)) || 
      !vectorEquals(result[1], Vector4(5.0f, 6.0f, 7.0f, 8.0f))) {
    throw DxvkError("ComposeVector4 failed");
  }
  Logger::info("ComposeVector4 component passed");
}

void testDecomposeVector2() {
  
  const RtComponentSpec* spec = getComponentSpec("lightspeed.trex.logic.DecomposeVector2");
  if (!spec) {
    throw DxvkError("Failed to find DecomposeVector2 component");
  }
  
  std::vector<RtComponentPropertyVector> props;
  props.push_back(std::vector<Vector2>{Vector2(1.0f, 2.0f), Vector2(3.0f, 4.0f)});  // input
  props.push_back(std::vector<float>{0.0f, 0.0f});  // x
  props.push_back(std::vector<float>{0.0f, 0.0f});  // y
  
  std::vector<size_t> indices = {0, 1, 2};
  MockGraphBatch batch;
  auto comp = spec->createComponentBatch(batch, props, indices);
  comp->updateRange(nullptr, 0, 2);
  
  auto& x = std::get<std::vector<float>>(props[1]);
  auto& y = std::get<std::vector<float>>(props[2]);
  if (!floatEquals(x[0], 1.0f) || !floatEquals(y[0], 2.0f) || 
      !floatEquals(x[1], 3.0f) || !floatEquals(y[1], 4.0f)) {
    throw DxvkError("DecomposeVector2 failed");
  }
  Logger::info("DecomposeVector2 component passed");
}

void testDecomposeVector3() {
  
  const RtComponentSpec* spec = getComponentSpec("lightspeed.trex.logic.DecomposeVector3");
  if (!spec) {
    throw DxvkError("Failed to find DecomposeVector3 component");
  }
  
  std::vector<RtComponentPropertyVector> props;
  props.push_back(std::vector<Vector3>{Vector3(1.0f, 2.0f, 3.0f), Vector3(4.0f, 5.0f, 6.0f)});  // input
  props.push_back(std::vector<float>{0.0f, 0.0f});  // x
  props.push_back(std::vector<float>{0.0f, 0.0f});  // y
  props.push_back(std::vector<float>{0.0f, 0.0f});  // z
  
  std::vector<size_t> indices = {0, 1, 2, 3};
  MockGraphBatch batch;
  auto comp = spec->createComponentBatch(batch, props, indices);
  comp->updateRange(nullptr, 0, 2);
  
  auto& x = std::get<std::vector<float>>(props[1]);
  auto& y = std::get<std::vector<float>>(props[2]);
  auto& z = std::get<std::vector<float>>(props[3]);
  if (!floatEquals(x[0], 1.0f) || !floatEquals(y[0], 2.0f) || !floatEquals(z[0], 3.0f) ||
      !floatEquals(x[1], 4.0f) || !floatEquals(y[1], 5.0f) || !floatEquals(z[1], 6.0f)) {
    throw DxvkError("DecomposeVector3 failed");
  }
  Logger::info("DecomposeVector3 component passed");
}

void testDecomposeVector4() {
  
  const RtComponentSpec* spec = getComponentSpec("lightspeed.trex.logic.DecomposeVector4");
  if (!spec) {
    throw DxvkError("Failed to find DecomposeVector4 component");
  }
  
  std::vector<RtComponentPropertyVector> props;
  props.push_back(std::vector<Vector4>{Vector4(1.0f, 2.0f, 3.0f, 4.0f), Vector4(5.0f, 6.0f, 7.0f, 8.0f)});  // input
  props.push_back(std::vector<float>{0.0f, 0.0f});  // x
  props.push_back(std::vector<float>{0.0f, 0.0f});  // y
  props.push_back(std::vector<float>{0.0f, 0.0f});  // z
  props.push_back(std::vector<float>{0.0f, 0.0f});  // w
  
  std::vector<size_t> indices = {0, 1, 2, 3, 4};
  MockGraphBatch batch;
  auto comp = spec->createComponentBatch(batch, props, indices);
  comp->updateRange(nullptr, 0, 2);
  
  auto& x = std::get<std::vector<float>>(props[1]);
  auto& y = std::get<std::vector<float>>(props[2]);
  auto& z = std::get<std::vector<float>>(props[3]);
  auto& w = std::get<std::vector<float>>(props[4]);
  if (!floatEquals(x[0], 1.0f) || !floatEquals(y[0], 2.0f) || !floatEquals(z[0], 3.0f) || !floatEquals(w[0], 4.0f) ||
      !floatEquals(x[1], 5.0f) || !floatEquals(y[1], 6.0f) || !floatEquals(z[1], 7.0f) || !floatEquals(w[1], 8.0f)) {
    throw DxvkError("DecomposeVector4 failed");
  }
  Logger::info("DecomposeVector4 component passed");
}

void testVectorLength() {
  
  // Test Float2 variant
  {
    std::unordered_map<std::string, RtComponentPropertyType> desiredTypes = {
      {"input", RtComponentPropertyType::Float2}
    };
    const RtComponentSpec* spec = getComponentVariant("lightspeed.trex.logic.VectorLength", desiredTypes);
    if (!spec) {
      throw DxvkError("Failed to find VectorLength<Float2> component");
    }
    
    std::vector<RtComponentPropertyVector> props;
    props.push_back(std::vector<Vector2>{Vector2(3.0f, 4.0f), Vector2(1.0f, 0.0f)});  // input
    props.push_back(std::vector<float>{0.0f, 0.0f});  // length
    
    std::vector<size_t> indices = {0, 1};
    MockGraphBatch batch;
    auto comp = spec->createComponentBatch(batch, props, indices);
    comp->updateRange(nullptr, 0, 2);
    
    auto& result = std::get<std::vector<float>>(props[1]);
    if (!floatEquals(result[0], 5.0f) || !floatEquals(result[1], 1.0f)) {
      throw DxvkError("VectorLength<Float2> failed");
    }
  }
  
  // Test Float3 variant
  {
    std::unordered_map<std::string, RtComponentPropertyType> desiredTypes = {
      {"input", RtComponentPropertyType::Float3}
    };
    const RtComponentSpec* spec = getComponentVariant("lightspeed.trex.logic.VectorLength", desiredTypes);
    if (!spec) {
      throw DxvkError("Failed to find VectorLength<Float3> component");
    }
    
    std::vector<RtComponentPropertyVector> props;
    props.push_back(std::vector<Vector3>{Vector3(3.0f, 4.0f, 0.0f), Vector3(1.0f, 0.0f, 0.0f)});  // input
    props.push_back(std::vector<float>{0.0f, 0.0f});  // length
    
    std::vector<size_t> indices = {0, 1};
    MockGraphBatch batch;
    auto comp = spec->createComponentBatch(batch, props, indices);
    comp->updateRange(nullptr, 0, 2);
    
    auto& result = std::get<std::vector<float>>(props[1]);
    if (!floatEquals(result[0], 5.0f) || !floatEquals(result[1], 1.0f)) {
      throw DxvkError("VectorLength<Float3> failed");
    }
  }
  
  // Test Float4 variant
  {
    std::unordered_map<std::string, RtComponentPropertyType> desiredTypes = {
      {"input", RtComponentPropertyType::Float4}
    };
    const RtComponentSpec* spec = getComponentVariant("lightspeed.trex.logic.VectorLength", desiredTypes);
    if (!spec) {
      throw DxvkError("Failed to find VectorLength<Float4> component");
    }
    
    std::vector<RtComponentPropertyVector> props;
    props.push_back(std::vector<Vector4>{Vector4(0.0f, 0.0f, 3.0f, 4.0f)});  // input
    props.push_back(std::vector<float>{0.0f});  // length
    
    std::vector<size_t> indices = {0, 1};
    MockGraphBatch batch;
    auto comp = spec->createComponentBatch(batch, props, indices);
    comp->updateRange(nullptr, 0, 1);
    
    auto& result = std::get<std::vector<float>>(props[1]);
    if (!floatEquals(result[0], 5.0f)) {
      throw DxvkError("VectorLength<Float4> failed");
    }
  }
  
  Logger::info("VectorLength component passed (Float2, Float3, Float4)");
}

void testNormalize() {
  
  // Test Float2 variant
  {
    std::unordered_map<std::string, RtComponentPropertyType> desiredTypes = {
      {"input", RtComponentPropertyType::Float2},
      {"output", RtComponentPropertyType::Float2}
    };
    const RtComponentSpec* spec = getComponentVariant("lightspeed.trex.logic.Normalize", desiredTypes);
    if (!spec) {
      throw DxvkError("Failed to find Normalize<Float2> component");
    }
    
    std::vector<RtComponentPropertyVector> props;
    props.push_back(std::vector<Vector2>{Vector2(3.0f, 4.0f), Vector2(5.0f, 0.0f)});  // input
    props.push_back(std::vector<Vector2>{Vector2(), Vector2()});  // output
    
    std::vector<size_t> indices = {0, 1};
    MockGraphBatch batch;
    auto comp = spec->createComponentBatch(batch, props, indices);
    comp->updateRange(nullptr, 0, 2);
    
    auto& result = std::get<std::vector<Vector2>>(props[1]);
    if (!vectorEquals(result[0], Vector2(0.6f, 0.8f)) || 
        !vectorEquals(result[1], Vector2(1.0f, 0.0f))) {
      throw DxvkError("Normalize<Float2> failed");
    }
  }
  
  // Test Float3 variant
  {
    std::unordered_map<std::string, RtComponentPropertyType> desiredTypes = {
      {"input", RtComponentPropertyType::Float3},
      {"output", RtComponentPropertyType::Float3}
    };
    const RtComponentSpec* spec = getComponentVariant("lightspeed.trex.logic.Normalize", desiredTypes);
    if (!spec) {
      throw DxvkError("Failed to find Normalize<Float3> component");
    }
    
    std::vector<RtComponentPropertyVector> props;
    props.push_back(std::vector<Vector3>{Vector3(3.0f, 4.0f, 0.0f), Vector3(0.0f, 5.0f, 0.0f)});  // input
    props.push_back(std::vector<Vector3>{Vector3(), Vector3()});  // output
    
    std::vector<size_t> indices = {0, 1};
    MockGraphBatch batch;
    auto comp = spec->createComponentBatch(batch, props, indices);
    comp->updateRange(nullptr, 0, 2);
    
    auto& result = std::get<std::vector<Vector3>>(props[1]);
    if (!vectorEquals(result[0], Vector3(0.6f, 0.8f, 0.0f)) || 
        !vectorEquals(result[1], Vector3(0.0f, 1.0f, 0.0f))) {
      throw DxvkError("Normalize<Float3> failed");
    }
  }
  
  // Test Float4 variant
  {
    std::unordered_map<std::string, RtComponentPropertyType> desiredTypes = {
      {"input", RtComponentPropertyType::Float4},
      {"output", RtComponentPropertyType::Float4}
    };
    const RtComponentSpec* spec = getComponentVariant("lightspeed.trex.logic.Normalize", desiredTypes);
    if (!spec) {
      throw DxvkError("Failed to find Normalize<Float4> component");
    }
    
    std::vector<RtComponentPropertyVector> props;
    props.push_back(std::vector<Vector4>{Vector4(0.0f, 3.0f, 0.0f, 4.0f)});  // input
    props.push_back(std::vector<Vector4>{Vector4()});  // output
    
    std::vector<size_t> indices = {0, 1};
    MockGraphBatch batch;
    auto comp = spec->createComponentBatch(batch, props, indices);
    comp->updateRange(nullptr, 0, 1);
    
    auto& result = std::get<std::vector<Vector4>>(props[1]);
    if (!vectorEquals(result[0], Vector4(0.0f, 0.6f, 0.0f, 0.8f))) {
      throw DxvkError("Normalize<Float4> failed");
    }
  }
  
  Logger::info("Normalize component passed (Float2, Float3, Float4)");
}

//=============================================================================
// LOGIC/STATE COMPONENTS
//=============================================================================

void testToggle() {
  
  const RtComponentSpec* spec = getComponentSpec("lightspeed.trex.logic.Toggle");
  if (!spec) {
    throw DxvkError("Failed to find Toggle component");
  }
  
  std::vector<RtComponentPropertyVector> props;
  props.push_back(std::vector<uint32_t>{1, 0, 1});  // triggerToggle
  props.push_back(std::vector<uint32_t>{0, 0, 1});  // defaultState (instance 0,1 start false, instance 2 starts true)
  props.push_back(std::vector<uint32_t>{0, 0, 0});  // isOn
  
  std::vector<size_t> indices = {0, 1, 2};
  MockGraphBatch batch;
  auto comp = spec->createComponentBatch(batch, props, indices);
  
  // Initialize all instances
  for (size_t i = 0; i < 3; i++) {
    if (spec->initialize) {
      spec->initialize(nullptr, *comp, i);
    }
  }
  
  auto& triggerToggle = std::get<std::vector<uint32_t>>(props[0]);
  auto& isOn = std::get<std::vector<uint32_t>>(props[2]);
  
  // Verify initial states from defaultState
  if (isOn[0] != 0) { throw DxvkError("Toggle instance 0: incorrect initial state (expected false)"); }
  if (isOn[1] != 0) { throw DxvkError("Toggle instance 1: incorrect initial state (expected false)"); }
  if (isOn[2] != 1) { throw DxvkError("Toggle instance 2: incorrect initial state (expected true from defaultState)"); }
  
  // First update: triggers[0]=1, triggers[1]=0, triggers[2]=1
  // Instance 0: false -> true (triggered)
  // Instance 1: false (not triggered)
  // Instance 2: true -> false (triggered)
  comp->updateRange(nullptr, 0, 3);
  if (isOn[0] != 1) { throw DxvkError("Toggle instance 0: failed first toggle (expected true)"); }
  if (isOn[1] != 0) { throw DxvkError("Toggle instance 1: should remain false (not triggered)"); }
  if (isOn[2] != 0) { throw DxvkError("Toggle instance 2: failed first toggle (expected false)"); }
  
  // Second update: triggers[0]=1, triggers[1]=0, triggers[2]=1 (same triggers)
  // Instance 0: true -> false (triggered again)
  // Instance 1: false (still not triggered)
  // Instance 2: false -> true (triggered again)
  comp->updateRange(nullptr, 0, 3);
  if (isOn[0] != 0) { throw DxvkError("Toggle instance 0: failed second toggle (expected false)"); }
  if (isOn[1] != 0) { throw DxvkError("Toggle instance 1: should still remain false"); }
  if (isOn[2] != 1) { throw DxvkError("Toggle instance 2: failed second toggle (expected true)"); }
  
  // Change trigger values: now trigger instance 1, don't trigger instance 0 or 2
  triggerToggle[0] = 0;
  triggerToggle[1] = 1;
  triggerToggle[2] = 0;
  
  // Third update with changed triggers
  // Instance 0: false (not triggered, stays false)
  // Instance 1: false -> true (triggered)
  // Instance 2: true (not triggered, stays true)
  comp->updateRange(nullptr, 0, 3);
  if (isOn[0] != 0) { throw DxvkError("Toggle instance 0: should remain false (not triggered this time)"); }
  if (isOn[1] != 1) { throw DxvkError("Toggle instance 1: failed toggle (expected true)"); }
  if (isOn[2] != 1) { throw DxvkError("Toggle instance 2: should remain true (not triggered this time)"); }
  
  // Fourth update with partial range (only instance 1)
  // Instance 1: true -> false (triggered)
  comp->updateRange(nullptr, 1, 2);
  if (isOn[1] != 0) {
    throw DxvkError("Toggle instance 1: failed partial range toggle");
  }
  
  Logger::info("Toggle component passed (verified all 3 instances with changing triggers)");
}

void testSelect() {
  
  XXH64_hash_t baseHash = XXH3_64bits("lightspeed.trex.logic.Select", strlen("lightspeed.trex.logic.Select"));
  const ComponentSpecVariantMap& variants = getAllComponentSpecVariants(baseHash);
  
  int testedCount = 0;
  
  // Test Float variant
  {
    std::unordered_map<std::string, RtComponentPropertyType> desiredTypes = {
      {"inputA", RtComponentPropertyType::Float},
      {"inputB", RtComponentPropertyType::Float},
      {"output", RtComponentPropertyType::Float}
    };
    std::vector<RtComponentPropertyVector> props;
    props.push_back(std::vector<uint32_t>{1, 0, 1});         // condition
    props.push_back(std::vector<float>{10.0f, 20.0f, 30.0f}); // inputA
    props.push_back(std::vector<float>{5.0f, 15.0f, 25.0f});  // inputB
    props.push_back(std::vector<float>{0.0f, 0.0f, 0.0f});    // output
    
    auto& result = testComponentVariant<float>("lightspeed.trex.logic.Select", desiredTypes, props, 3, 0, 3);
    if (!floatEquals(result[0], 10.0f) || !floatEquals(result[1], 15.0f) || !floatEquals(result[2], 30.0f)) {
      throw DxvkError("Select<Float> failed");
    }
    testedCount++;
  }
  
  // Test Float2 variant
  {
    std::unordered_map<std::string, RtComponentPropertyType> desiredTypes = {
      {"inputA", RtComponentPropertyType::Float2},
      {"inputB", RtComponentPropertyType::Float2},
      {"output", RtComponentPropertyType::Float2}
    };
    std::vector<RtComponentPropertyVector> props;
    props.push_back(std::vector<uint32_t>{1, 0});  // condition
    props.push_back(std::vector<Vector2>{Vector2(1.0f, 2.0f), Vector2(5.0f, 6.0f)}); // inputA
    props.push_back(std::vector<Vector2>{Vector2(3.0f, 4.0f), Vector2(7.0f, 8.0f)}); // inputB
    props.push_back(std::vector<Vector2>{Vector2(), Vector2()}); // output
    
    auto& result = testComponentVariant<Vector2>("lightspeed.trex.logic.Select", desiredTypes, props, 3, 0, 2);
    if (!vectorEquals(result[0], Vector2(1.0f, 2.0f)) || !vectorEquals(result[1], Vector2(7.0f, 8.0f))) {
      throw DxvkError("Select<Float2> failed");
    }
    testedCount++;
  }
  
  // Test Float3 variant
  {
    std::unordered_map<std::string, RtComponentPropertyType> desiredTypes = {
      {"inputA", RtComponentPropertyType::Float3},
      {"inputB", RtComponentPropertyType::Float3},
      {"output", RtComponentPropertyType::Float3}
    };
    std::vector<RtComponentPropertyVector> props;
    props.push_back(std::vector<uint32_t>{1, 0});  // condition
    props.push_back(std::vector<Vector3>{Vector3(1.0f, 2.0f, 3.0f), Vector3(7.0f, 8.0f, 9.0f)}); // inputA
    props.push_back(std::vector<Vector3>{Vector3(4.0f, 5.0f, 6.0f), Vector3(10.0f, 11.0f, 12.0f)}); // inputB
    props.push_back(std::vector<Vector3>{Vector3(), Vector3()}); // output
    
    auto& result = testComponentVariant<Vector3>("lightspeed.trex.logic.Select", desiredTypes, props, 3, 0, 2);
    if (!vectorEquals(result[0], Vector3(1.0f, 2.0f, 3.0f)) || 
        !vectorEquals(result[1], Vector3(10.0f, 11.0f, 12.0f))) {
      throw DxvkError("Select<Float3> failed");
    }
    testedCount++;
  }
  
  // Test Float4 variant
  {
    std::unordered_map<std::string, RtComponentPropertyType> desiredTypes = {
      {"inputA", RtComponentPropertyType::Float4},
      {"inputB", RtComponentPropertyType::Float4},
      {"output", RtComponentPropertyType::Float4}
    };
    std::vector<RtComponentPropertyVector> props;
    props.push_back(std::vector<uint32_t>{1});  // condition
    props.push_back(std::vector<Vector4>{Vector4(1.0f, 2.0f, 3.0f, 4.0f)}); // inputA
    props.push_back(std::vector<Vector4>{Vector4(5.0f, 6.0f, 7.0f, 8.0f)}); // inputB
    props.push_back(std::vector<Vector4>{Vector4()}); // output
    
    auto& result = testComponentVariant<Vector4>("lightspeed.trex.logic.Select", desiredTypes, props, 3, 0, 1);
    if (!vectorEquals(result[0], Vector4(1.0f, 2.0f, 3.0f, 4.0f))) {
      throw DxvkError("Select<Float4> failed");
    }
    testedCount++;
  }
  
  // Test Bool variant
  {
    std::unordered_map<std::string, RtComponentPropertyType> desiredTypes = {
      {"inputA", RtComponentPropertyType::Bool},
      {"inputB", RtComponentPropertyType::Bool},
      {"output", RtComponentPropertyType::Bool}
    };
    std::vector<RtComponentPropertyVector> props;
    props.push_back(std::vector<uint32_t>{1, 0});  // condition
    props.push_back(std::vector<uint32_t>{1, 0});  // inputA
    props.push_back(std::vector<uint32_t>{0, 1});  // inputB
    props.push_back(std::vector<uint32_t>{0, 0});  // output
    
    auto& result = testComponentVariant<uint32_t>("lightspeed.trex.logic.Select", desiredTypes, props, 3, 0, 2);
    if (result[0] != 1 || result[1] != 1) {
      throw DxvkError("Select<Bool> failed");
    }
    testedCount++;
  }
  
  // Test Enum variant
  {
    std::unordered_map<std::string, RtComponentPropertyType> desiredTypes = {
      {"inputA", RtComponentPropertyType::Enum},
      {"inputB", RtComponentPropertyType::Enum},
      {"output", RtComponentPropertyType::Enum}
    };
    std::vector<RtComponentPropertyVector> props;
    props.push_back(std::vector<uint32_t>{1});  // condition
    props.push_back(std::vector<uint32_t>{42}); // inputA
    props.push_back(std::vector<uint32_t>{99}); // inputB
    props.push_back(std::vector<uint32_t>{0});  // output
    
    auto& result = testComponentVariant<uint32_t>("lightspeed.trex.logic.Select", desiredTypes, props, 3, 0, 1);
    if (result[0] != 42) {
      throw DxvkError("Select<Enum> failed");
    }
    testedCount++;
  }
  
  // Test Hash variant
  {
    std::unordered_map<std::string, RtComponentPropertyType> desiredTypes = {
      {"inputA", RtComponentPropertyType::Hash},
      {"inputB", RtComponentPropertyType::Hash},
      {"output", RtComponentPropertyType::Hash}
    };
    std::vector<RtComponentPropertyVector> props;
    props.push_back(std::vector<uint32_t>{0});         // condition
    props.push_back(std::vector<uint64_t>{0x1234567890ABCDEF}); // inputA
    props.push_back(std::vector<uint64_t>{0xFEDCBA0987654321}); // inputB
    props.push_back(std::vector<uint64_t>{0});         // output
    
    auto& result = testComponentVariant<uint64_t>("lightspeed.trex.logic.Select", desiredTypes, props, 3, 0, 1);
    if (result[0] != 0xFEDCBA0987654321) {
      throw DxvkError("Select<Hash> failed");
    }
    testedCount++;
  }
  
  // Test Prim variant
  {
    std::unordered_map<std::string, RtComponentPropertyType> desiredTypes = {
      {"inputA", RtComponentPropertyType::Prim},
      {"inputB", RtComponentPropertyType::Prim},
      {"output", RtComponentPropertyType::Prim}
    };
    std::vector<RtComponentPropertyVector> props;
    props.push_back(std::vector<uint32_t>{1});          // condition
    props.push_back(std::vector<PrimTarget>{PrimTarget{1, 100}}); // inputA
    props.push_back(std::vector<PrimTarget>{PrimTarget{2, 200}}); // inputB
    props.push_back(std::vector<PrimTarget>{PrimTarget{}}); // output
    
    auto& result = testComponentVariant<PrimTarget>("lightspeed.trex.logic.Select", desiredTypes, props, 3, 0, 1);
    if (result[0].replacementIndex != 1 || result[0].instanceId != 100) {
      throw DxvkError("Select<Prim> failed");
    }
    testedCount++;
  }
  
  // Test String variant
  {
    std::unordered_map<std::string, RtComponentPropertyType> desiredTypes = {
      {"inputA", RtComponentPropertyType::String},
      {"inputB", RtComponentPropertyType::String},
      {"output", RtComponentPropertyType::String}
    };
    std::vector<RtComponentPropertyVector> props;
    props.push_back(std::vector<uint32_t>{0});         // condition
    props.push_back(std::vector<std::string>{"hello"}); // inputA
    props.push_back(std::vector<std::string>{"world"}); // inputB
    props.push_back(std::vector<std::string>{""});      // output
    
    auto& result = testComponentVariant<std::string>("lightspeed.trex.logic.Select", desiredTypes, props, 3, 0, 1);
    if (result[0] != "world") {
      throw DxvkError("Select<String> failed");
    }
    testedCount++;
  }
  
  if (variants.size() != static_cast<size_t>(testedCount)) {
    throw DxvkError(str::format("Select variant count mismatch: expected ", testedCount, ", tested ", testedCount, ", found ", variants.size()));
  }
  
  Logger::info("Select component passed (Float, Float2, Float3, Float4, Bool, Enum, Hash, Prim, String)");
}

void testCounter() {
  
  const RtComponentSpec* spec = getComponentSpec("lightspeed.trex.logic.Counter");
  if (!spec) {
    throw DxvkError("Failed to find Counter component");
  }
  
  // Test instances:
  // [0] increment=1, incrementValue=1.0, defaultValue=0.0 (standard increment from 0)
  // [1] increment=0, incrementValue=1.0, defaultValue=0.0 (not incrementing)
  // [2] increment=1, incrementValue=2.5, defaultValue=0.0 (non-1 increment)
  // [3] increment=1, incrementValue=-1.5, defaultValue=0.0 (negative increment)
  // [4] increment=1, incrementValue=10.0, defaultValue=0.0 (will be toggled off later)
  // [5] increment=1, incrementValue=1.0, defaultValue=100.0 (start from non-zero value)
  // [6] increment=1, incrementValue=5.0, defaultValue=-50.0 (start from negative value)
  std::vector<RtComponentPropertyVector> props;
  props.push_back(std::vector<uint32_t>{1, 0, 1, 1, 1, 1, 1});                   // increment
  props.push_back(std::vector<float>{1.0f, 1.0f, 2.5f, -1.5f, 10.0f, 1.0f, 5.0f}); // incrementValue
  props.push_back(std::vector<float>{0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 100.0f, -50.0f}); // defaultValue
  props.push_back(std::vector<float>{0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f});  // count (state)
  props.push_back(std::vector<float>{0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f});  // value (output)
  
  std::vector<size_t> indices = {0, 1, 2, 3, 4, 5, 6};
  MockGraphBatch batch;
  auto comp = spec->createComponentBatch(batch, props, indices);
  
  // Initialize all instances
  for (size_t i = 0; i < 7; i++) {
    if (spec->initialize) {
      spec->initialize(nullptr, *comp, i);
    }
  }
  
  auto& increment = std::get<std::vector<uint32_t>>(props[0]);
  auto& value = std::get<std::vector<float>>(props[4]);
  
  // Verify initial values from defaultValue
  if (!floatEquals(value[0], 0.0f)) { throw DxvkError("Counter[0] incorrect initial value (expected 0.0)"); }
  if (!floatEquals(value[1], 0.0f)) { throw DxvkError("Counter[1] incorrect initial value (expected 0.0)"); }
  if (!floatEquals(value[2], 0.0f)) { throw DxvkError("Counter[2] incorrect initial value (expected 0.0)"); }
  if (!floatEquals(value[3], 0.0f)) { throw DxvkError("Counter[3] incorrect initial value (expected 0.0)"); }
  if (!floatEquals(value[4], 0.0f)) { throw DxvkError("Counter[4] incorrect initial value (expected 0.0)"); }
  if (!floatEquals(value[5], 100.0f)) { throw DxvkError("Counter[5] incorrect initial value (expected 100.0 from defaultValue)"); }
  if (!floatEquals(value[6], -50.0f)) { throw DxvkError("Counter[6] incorrect initial value (expected -50.0 from defaultValue)"); }
  
  // First update
  comp->updateRange(nullptr, 0, 7);
  if (!floatEquals(value[0], 1.0f)) { throw DxvkError("Counter[0] failed first update (0.0 + 1.0 = 1.0)"); }
  if (!floatEquals(value[1], 0.0f)) { throw DxvkError("Counter[1] should not increment (increment=false)"); }
  if (!floatEquals(value[2], 2.5f)) { throw DxvkError("Counter[2] failed first update (0.0 + 2.5 = 2.5)"); }
  if (!floatEquals(value[3], -1.5f)) { throw DxvkError("Counter[3] failed first update (0.0 + -1.5 = -1.5)"); }
  if (!floatEquals(value[4], 10.0f)) { throw DxvkError("Counter[4] failed first update (0.0 + 10.0 = 10.0)"); }
  if (!floatEquals(value[5], 101.0f)) { throw DxvkError("Counter[5] failed first update (100.0 + 1.0 = 101.0)"); }
  if (!floatEquals(value[6], -45.0f)) { throw DxvkError("Counter[6] failed first update (-50.0 + 5.0 = -45.0)"); }
  
  // Second update
  comp->updateRange(nullptr, 0, 7);
  if (!floatEquals(value[0], 2.0f)) { throw DxvkError("Counter[0] failed second update"); }
  if (!floatEquals(value[1], 0.0f)) { throw DxvkError("Counter[1] should still not increment"); }
  if (!floatEquals(value[2], 5.0f)) { throw DxvkError("Counter[2] failed second update (2.5 + 2.5 = 5.0)"); }
  if (!floatEquals(value[3], -3.0f)) { throw DxvkError("Counter[3] failed second update (-1.5 + -1.5 = -3.0)"); }
  if (!floatEquals(value[4], 20.0f)) { throw DxvkError("Counter[4] failed second update (10.0 + 10.0 = 20.0)"); }
  if (!floatEquals(value[5], 102.0f)) { throw DxvkError("Counter[5] failed second update (101.0 + 1.0 = 102.0)"); }
  if (!floatEquals(value[6], -40.0f)) { throw DxvkError("Counter[6] failed second update (-45.0 + 5.0 = -40.0)"); }
  
  // Third update - toggle off instance 4's increment
  increment[4] = 0;
  comp->updateRange(nullptr, 0, 7);
  if (!floatEquals(value[0], 3.0f)) { throw DxvkError("Counter[0] failed third update"); }
  if (!floatEquals(value[1], 0.0f)) { throw DxvkError("Counter[1] should still not increment"); }
  if (!floatEquals(value[2], 7.5f)) { throw DxvkError("Counter[2] failed third update (5.0 + 2.5 = 7.5)"); }
  if (!floatEquals(value[3], -4.5f)) { throw DxvkError("Counter[3] failed third update (-3.0 + -1.5 = -4.5)"); }
  if (!floatEquals(value[4], 20.0f)) { throw DxvkError("Counter[4] should not increment after toggle off"); }
  if (!floatEquals(value[5], 103.0f)) { throw DxvkError("Counter[5] failed third update (102.0 + 1.0 = 103.0)"); }
  if (!floatEquals(value[6], -35.0f)) { throw DxvkError("Counter[6] failed third update (-40.0 + 5.0 = -35.0)"); }
  
  // Fourth update - instance 4 should still not increment
  comp->updateRange(nullptr, 0, 7);
  if (!floatEquals(value[0], 4.0f)) { throw DxvkError("Counter[0] failed fourth update"); }
  if (!floatEquals(value[1], 0.0f)) { throw DxvkError("Counter[1] should still not increment"); }
  if (!floatEquals(value[2], 10.0f)) { throw DxvkError("Counter[2] failed fourth update (7.5 + 2.5 = 10.0)"); }
  if (!floatEquals(value[3], -6.0f)) { throw DxvkError("Counter[3] failed fourth update (-4.5 + -1.5 = -6.0)"); }
  if (!floatEquals(value[4], 20.0f)) { throw DxvkError("Counter[4] should remain at 20.0 with increment=false"); }
  if (!floatEquals(value[5], 104.0f)) { throw DxvkError("Counter[5] failed fourth update (103.0 + 1.0 = 104.0)"); }
  if (!floatEquals(value[6], -30.0f)) { throw DxvkError("Counter[6] failed fourth update (-35.0 + 5.0 = -30.0)"); }
  
  // Fifth update - toggle instance 4 back on
  increment[4] = 1;
  comp->updateRange(nullptr, 0, 7);
  if (!floatEquals(value[0], 5.0f)) { throw DxvkError("Counter[0] failed fifth update"); }
  if (!floatEquals(value[1], 0.0f)) { throw DxvkError("Counter[1] should still not increment"); }
  if (!floatEquals(value[2], 12.5f)) { throw DxvkError("Counter[2] failed fifth update (10.0 + 2.5 = 12.5)"); }
  if (!floatEquals(value[3], -7.5f)) { throw DxvkError("Counter[3] failed fifth update (-6.0 + -1.5 = -7.5)"); }
  if (!floatEquals(value[4], 30.0f)) { throw DxvkError("Counter[4] should resume incrementing (20.0 + 10.0 = 30.0)"); }
  if (!floatEquals(value[5], 105.0f)) { throw DxvkError("Counter[5] failed fifth update (104.0 + 1.0 = 105.0)"); }
  if (!floatEquals(value[6], -25.0f)) { throw DxvkError("Counter[6] failed fifth update (-30.0 + 5.0 = -25.0)"); }
  
  Logger::info("Counter component passed (tested defaultValue initialization, increment=1.0/2.5/-1.5/10.0, toggle on/off)");
}

void testConditionallyStore() {
  
  XXH64_hash_t baseHash = XXH3_64bits("lightspeed.trex.logic.ConditionallyStore", strlen("lightspeed.trex.logic.ConditionallyStore"));
  const ComponentSpecVariantMap& variants = getAllComponentSpecVariants(baseHash);
  
  int testedCount = 0;
  
  // Test Float variant
  {
    std::unordered_map<std::string, RtComponentPropertyType> desiredTypes = {
      {"input", RtComponentPropertyType::Float},
      {"storedValue", RtComponentPropertyType::Float},
      {"output", RtComponentPropertyType::Float}
    };
    std::vector<RtComponentPropertyVector> props;
    props.push_back(std::vector<uint32_t>{1, 0, 1});           // store
    props.push_back(std::vector<float>{10.0f, 20.0f, 30.0f});  // input
    props.push_back(std::vector<float>{0.0f, 0.0f, 0.0f});     // storedValue (state)
    props.push_back(std::vector<float>{0.0f, 0.0f, 0.0f});     // output
    
    auto& result = testComponentVariant<float>("lightspeed.trex.logic.ConditionallyStore", desiredTypes, props, 3, 0, 3);
    if (!floatEquals(result[0], 10.0f) || !floatEquals(result[1], 0.0f) || !floatEquals(result[2], 30.0f)) {
      throw DxvkError("ConditionallyStore<Float> failed");
    }
    testedCount++;
  }
  
  // Test Float2 variant
  {
    std::unordered_map<std::string, RtComponentPropertyType> desiredTypes = {
      {"input", RtComponentPropertyType::Float2},
      {"storedValue", RtComponentPropertyType::Float2},
      {"output", RtComponentPropertyType::Float2}
    };
    std::vector<RtComponentPropertyVector> props;
    props.push_back(std::vector<uint32_t>{1, 0});                    // store
    props.push_back(std::vector<Vector2>{Vector2(1.0f, 2.0f), Vector2(3.0f, 4.0f)});  // input
    props.push_back(std::vector<Vector2>{Vector2(), Vector2()});     // storedValue (state)
    props.push_back(std::vector<Vector2>{Vector2(), Vector2()});     // output
    
    auto& result = testComponentVariant<Vector2>("lightspeed.trex.logic.ConditionallyStore", desiredTypes, props, 3, 0, 2);
    if (!vectorEquals(result[0], Vector2(1.0f, 2.0f)) || !vectorEquals(result[1], Vector2(0.0f, 0.0f))) {
      throw DxvkError("ConditionallyStore<Float2> failed");
    }
    testedCount++;
  }
  
  // Test Float3 variant
  {
    std::unordered_map<std::string, RtComponentPropertyType> desiredTypes = {
      {"input", RtComponentPropertyType::Float3},
      {"storedValue", RtComponentPropertyType::Float3},
      {"output", RtComponentPropertyType::Float3}
    };
    std::vector<RtComponentPropertyVector> props;
    props.push_back(std::vector<uint32_t>{1});  // store
    props.push_back(std::vector<Vector3>{Vector3(1.0f, 2.0f, 3.0f)});  // input
    props.push_back(std::vector<Vector3>{Vector3()});  // storedValue (state)
    props.push_back(std::vector<Vector3>{Vector3()});  // output
    
    auto& result = testComponentVariant<Vector3>("lightspeed.trex.logic.ConditionallyStore", desiredTypes, props, 3, 0, 1);
    if (!vectorEquals(result[0], Vector3(1.0f, 2.0f, 3.0f))) {
      throw DxvkError("ConditionallyStore<Float3> failed");
    }
    testedCount++;
  }
  
  // Test Float4 variant
  {
    std::unordered_map<std::string, RtComponentPropertyType> desiredTypes = {
      {"input", RtComponentPropertyType::Float4},
      {"storedValue", RtComponentPropertyType::Float4},
      {"output", RtComponentPropertyType::Float4}
    };
    std::vector<RtComponentPropertyVector> props;
    props.push_back(std::vector<uint32_t>{1});  // store
    props.push_back(std::vector<Vector4>{Vector4(1.0f, 2.0f, 3.0f, 4.0f)});  // input
    props.push_back(std::vector<Vector4>{Vector4()});  // storedValue (state)
    props.push_back(std::vector<Vector4>{Vector4()});  // output
    
    auto& result = testComponentVariant<Vector4>("lightspeed.trex.logic.ConditionallyStore", desiredTypes, props, 3, 0, 1);
    if (!vectorEquals(result[0], Vector4(1.0f, 2.0f, 3.0f, 4.0f))) {
      throw DxvkError("ConditionallyStore<Float4> failed");
    }
    testedCount++;
  }
  
  // Test Bool variant
  {
    std::unordered_map<std::string, RtComponentPropertyType> desiredTypes = {
      {"input", RtComponentPropertyType::Bool},
      {"storedValue", RtComponentPropertyType::Bool},
      {"output", RtComponentPropertyType::Bool}
    };
    std::vector<RtComponentPropertyVector> props;
    props.push_back(std::vector<uint32_t>{1, 0});  // store
    props.push_back(std::vector<uint32_t>{1, 0});  // input
    props.push_back(std::vector<uint32_t>{0, 0});  // storedValue (state)
    props.push_back(std::vector<uint32_t>{0, 0});  // output
    
    auto& result = testComponentVariant<uint32_t>("lightspeed.trex.logic.ConditionallyStore", desiredTypes, props, 3, 0, 2);
    if (result[0] != 1 || result[1] != 0) { // Second one keeps stored value
      throw DxvkError("ConditionallyStore<Bool> failed");
    }
    testedCount++;
  }
  
  // Test Enum variant
  {
    std::unordered_map<std::string, RtComponentPropertyType> desiredTypes = {
      {"input", RtComponentPropertyType::Enum},
      {"storedValue", RtComponentPropertyType::Enum},
      {"output", RtComponentPropertyType::Enum}
    };
    std::vector<RtComponentPropertyVector> props;
    props.push_back(std::vector<uint32_t>{1});  // store
    props.push_back(std::vector<uint32_t>{42}); // input
    props.push_back(std::vector<uint32_t>{0});  // storedValue (state)
    props.push_back(std::vector<uint32_t>{0});  // output
    
    auto& result = testComponentVariant<uint32_t>("lightspeed.trex.logic.ConditionallyStore", desiredTypes, props, 3, 0, 1);
    if (result[0] != 42) {
      throw DxvkError("ConditionallyStore<Enum> failed");
    }
    testedCount++;
  }
  
  // Test Hash variant
  {
    std::unordered_map<std::string, RtComponentPropertyType> desiredTypes = {
      {"input", RtComponentPropertyType::Hash},
      {"storedValue", RtComponentPropertyType::Hash},
      {"output", RtComponentPropertyType::Hash}
    };
    std::vector<RtComponentPropertyVector> props;
    props.push_back(std::vector<uint32_t>{1});             // store
    props.push_back(std::vector<uint64_t>{0xDEADBEEF});    // input
    props.push_back(std::vector<uint64_t>{0});             // storedValue (state)
    props.push_back(std::vector<uint64_t>{0});             // output
    
    auto& result = testComponentVariant<uint64_t>("lightspeed.trex.logic.ConditionallyStore", desiredTypes, props, 3, 0, 1);
    if (result[0] != 0xDEADBEEF) {
      throw DxvkError("ConditionallyStore<Hash> failed");
    }
    testedCount++;
  }
  
  // Test Prim variant
  {
    std::unordered_map<std::string, RtComponentPropertyType> desiredTypes = {
      {"input", RtComponentPropertyType::Prim},
      {"storedValue", RtComponentPropertyType::Prim},
      {"output", RtComponentPropertyType::Prim}
    };
    std::vector<RtComponentPropertyVector> props;
    props.push_back(std::vector<uint32_t>{1});          // store
    props.push_back(std::vector<PrimTarget>{PrimTarget{5, 500}}); // input
    props.push_back(std::vector<PrimTarget>{PrimTarget{}}); // storedValue (state)
    props.push_back(std::vector<PrimTarget>{PrimTarget{}}); // output
    
    auto& result = testComponentVariant<PrimTarget>("lightspeed.trex.logic.ConditionallyStore", desiredTypes, props, 3, 0, 1);
    if (result[0].replacementIndex != 5 || result[0].instanceId != 500) {
      throw DxvkError("ConditionallyStore<Prim> failed");
    }
    testedCount++;
  }
  
  // Test String variant
  {
    std::unordered_map<std::string, RtComponentPropertyType> desiredTypes = {
      {"input", RtComponentPropertyType::String},
      {"storedValue", RtComponentPropertyType::String},
      {"output", RtComponentPropertyType::String}
    };
    std::vector<RtComponentPropertyVector> props;
    props.push_back(std::vector<uint32_t>{1});          // store
    props.push_back(std::vector<std::string>{"test"});  // input
    props.push_back(std::vector<std::string>{""});      // storedValue (state)
    props.push_back(std::vector<std::string>{""});      // output
    
    auto& result = testComponentVariant<std::string>("lightspeed.trex.logic.ConditionallyStore", desiredTypes, props, 3, 0, 1);
    if (result[0] != "test") {
      throw DxvkError("ConditionallyStore<String> failed");
    }
    testedCount++;
  }
  
  if (variants.size() != static_cast<size_t>(testedCount)) {
    throw DxvkError(str::format("ConditionallyStore variant count mismatch: expected ", testedCount, ", tested ", testedCount, ", found ", variants.size()));
  }
  
  Logger::info("ConditionallyStore component passed (Float, Float2, Float3, Float4, Bool, Enum, Hash, Prim, String)");
}

void testPreviousFrameValue() {
  
  XXH64_hash_t baseHash = XXH3_64bits("lightspeed.trex.logic.PreviousFrameValue", strlen("lightspeed.trex.logic.PreviousFrameValue"));
  const ComponentSpecVariantMap& variants = getAllComponentSpecVariants(baseHash);
  
  int testedCount = 0;
  
  // Test Float variant
  {
    std::unordered_map<std::string, RtComponentPropertyType> desiredTypes = {
      {"input", RtComponentPropertyType::Float},
      {"previousValue", RtComponentPropertyType::Float},
      {"output", RtComponentPropertyType::Float}
    };
    const RtComponentSpec* spec = getComponentVariant("lightspeed.trex.logic.PreviousFrameValue", desiredTypes);
    if (!spec) {
      throw DxvkError("Failed to find PreviousFrameValue<Float> component");
    }
    
    std::vector<RtComponentPropertyVector> props;
    props.push_back(std::vector<float>{10.0f});  // input
    props.push_back(std::vector<float>{0.0f});   // previousValue (state)
    props.push_back(std::vector<float>{0.0f});   // output
    
    std::vector<size_t> indices = {0, 1, 2};
    MockGraphBatch batch;
    auto comp = spec->createComponentBatch(batch, props, indices);
    
    auto& input = std::get<std::vector<float>>(props[0]);
    auto& result = std::get<std::vector<float>>(props[2]);
    
    // First update: input=10.0, output should be 0.0 (initial previous value)
    comp->updateRange(nullptr, 0, 1);
    if (!floatEquals(result[0], 0.0f)) { throw DxvkError("PreviousFrameValue<Float> failed on first update"); }
    
    // Second update: input=20.0, output should be 10.0 (value from first frame)
    input[0] = 20.0f;
    comp->updateRange(nullptr, 0, 1);
    if (!floatEquals(result[0], 10.0f)) { throw DxvkError("PreviousFrameValue<Float> failed on second update"); }
    
    // Third update: input=30.0, output should be 20.0 (value from second frame)
    input[0] = 30.0f;
    comp->updateRange(nullptr, 0, 1);
    if (!floatEquals(result[0], 20.0f)) { throw DxvkError("PreviousFrameValue<Float> failed on third update"); }
    
    testedCount++;
  }
  
  // Test Float2 variant
  {
    std::unordered_map<std::string, RtComponentPropertyType> desiredTypes = {
      {"input", RtComponentPropertyType::Float2},
      {"previousValue", RtComponentPropertyType::Float2},
      {"output", RtComponentPropertyType::Float2}
    };
    const RtComponentSpec* spec = getComponentVariant("lightspeed.trex.logic.PreviousFrameValue", desiredTypes);
    if (!spec) {
      throw DxvkError("Failed to find PreviousFrameValue<Float2> component");
    }
    
    std::vector<RtComponentPropertyVector> props;
    props.push_back(std::vector<Vector2>{Vector2(1.0f, 2.0f)});  // input
    props.push_back(std::vector<Vector2>{Vector2()});            // previousValue (state)
    props.push_back(std::vector<Vector2>{Vector2()});            // output
    
    std::vector<size_t> indices = {0, 1, 2};
    MockGraphBatch batch;
    auto comp = spec->createComponentBatch(batch, props, indices);
    
    comp->updateRange(nullptr, 0, 1);
    auto& result = std::get<std::vector<Vector2>>(props[2]);
    if (!vectorEquals(result[0], Vector2(0.0f, 0.0f))) { throw DxvkError("PreviousFrameValue<Float2> failed on first update"); }
    
    auto& input = std::get<std::vector<Vector2>>(props[0]);
    input[0] = Vector2(3.0f, 4.0f);
    comp->updateRange(nullptr, 0, 1);
    if (!vectorEquals(result[0], Vector2(1.0f, 2.0f))) { throw DxvkError("PreviousFrameValue<Float2> failed on second update"); }
    
    testedCount++;
  }
  
  // Test Float3 variant
  {
    std::unordered_map<std::string, RtComponentPropertyType> desiredTypes = {
      {"input", RtComponentPropertyType::Float3},
      {"previousValue", RtComponentPropertyType::Float3},
      {"output", RtComponentPropertyType::Float3}
    };
    const RtComponentSpec* spec = getComponentVariant("lightspeed.trex.logic.PreviousFrameValue", desiredTypes);
    if (!spec) {
      throw DxvkError("Failed to find PreviousFrameValue<Float3> component");
    }
    
    std::vector<RtComponentPropertyVector> props;
    props.push_back(std::vector<Vector3>{Vector3(1.0f, 2.0f, 3.0f)});  // input
    props.push_back(std::vector<Vector3>{Vector3()});                  // previousValue (state)
    props.push_back(std::vector<Vector3>{Vector3()});                  // output
    
    std::vector<size_t> indices = {0, 1, 2};
    MockGraphBatch batch;
    auto comp = spec->createComponentBatch(batch, props, indices);
    
    auto& input = std::get<std::vector<Vector3>>(props[0]);
    auto& result = std::get<std::vector<Vector3>>(props[2]);
    
    comp->updateRange(nullptr, 0, 1);
    if (!vectorEquals(result[0], Vector3(0.0f, 0.0f, 0.0f))) { throw DxvkError("PreviousFrameValue<Float3> failed on first update"); }
    
    input[0] = Vector3(4.0f, 5.0f, 6.0f);
    comp->updateRange(nullptr, 0, 1);
    if (!vectorEquals(result[0], Vector3(1.0f, 2.0f, 3.0f))) { throw DxvkError("PreviousFrameValue<Float3> failed on second update"); }
    
    testedCount++;
  }
  
  // Test Float4 variant
  {
    std::unordered_map<std::string, RtComponentPropertyType> desiredTypes = {
      {"input", RtComponentPropertyType::Float4},
      {"previousValue", RtComponentPropertyType::Float4},
      {"output", RtComponentPropertyType::Float4}
    };
    const RtComponentSpec* spec = getComponentVariant("lightspeed.trex.logic.PreviousFrameValue", desiredTypes);
    if (!spec) {
      throw DxvkError("Failed to find PreviousFrameValue<Float4> component");
    }
    
    std::vector<RtComponentPropertyVector> props;
    props.push_back(std::vector<Vector4>{Vector4(1.0f, 2.0f, 3.0f, 4.0f)});  // input
    props.push_back(std::vector<Vector4>{Vector4()});                        // previousValue (state)
    props.push_back(std::vector<Vector4>{Vector4()});                        // output
    
    std::vector<size_t> indices = {0, 1, 2};
    MockGraphBatch batch;
    auto comp = spec->createComponentBatch(batch, props, indices);
    
    auto& input = std::get<std::vector<Vector4>>(props[0]);
    auto& result = std::get<std::vector<Vector4>>(props[2]);
    
    comp->updateRange(nullptr, 0, 1);
    if (!vectorEquals(result[0], Vector4(0.0f, 0.0f, 0.0f, 0.0f))) { throw DxvkError("PreviousFrameValue<Float4> failed on first update"); }
    
    input[0] = Vector4(5.0f, 6.0f, 7.0f, 8.0f);
    comp->updateRange(nullptr, 0, 1);
    if (!vectorEquals(result[0], Vector4(1.0f, 2.0f, 3.0f, 4.0f))) { throw DxvkError("PreviousFrameValue<Float4> failed on second update"); }
    
    testedCount++;
  }
  
  // Test Bool variant
  {
    std::unordered_map<std::string, RtComponentPropertyType> desiredTypes = {
      {"input", RtComponentPropertyType::Bool},
      {"previousValue", RtComponentPropertyType::Bool},
      {"output", RtComponentPropertyType::Bool}
    };
    const RtComponentSpec* spec = getComponentVariant("lightspeed.trex.logic.PreviousFrameValue", desiredTypes);
    if (!spec) {
      throw DxvkError("Failed to find PreviousFrameValue<Bool> component");
    }
    
    std::vector<RtComponentPropertyVector> props;
    props.push_back(std::vector<uint32_t>{1});  // input
    props.push_back(std::vector<uint32_t>{0});  // previousValue (state)
    props.push_back(std::vector<uint32_t>{0});  // output
    
    std::vector<size_t> indices = {0, 1, 2};
    MockGraphBatch batch;
    auto comp = spec->createComponentBatch(batch, props, indices);
    
    auto& input = std::get<std::vector<uint32_t>>(props[0]);
    auto& result = std::get<std::vector<uint32_t>>(props[2]);
    
    comp->updateRange(nullptr, 0, 1);
    if (result[0] != 0) {
      throw DxvkError("PreviousFrameValue<Bool> failed on first update");
    }
    
    input[0] = 0;
    comp->updateRange(nullptr, 0, 1);
    if (result[0] != 1) {
      throw DxvkError("PreviousFrameValue<Bool> failed on second update");
    }
    
    testedCount++;
  }
  
  // Test Enum variant
  {
    std::unordered_map<std::string, RtComponentPropertyType> desiredTypes = {
      {"input", RtComponentPropertyType::Enum},
      {"previousValue", RtComponentPropertyType::Enum},
      {"output", RtComponentPropertyType::Enum}
    };
    const RtComponentSpec* spec = getComponentVariant("lightspeed.trex.logic.PreviousFrameValue", desiredTypes);
    if (!spec) {
      throw DxvkError("Failed to find PreviousFrameValue<Enum> component");
    }
    
    std::vector<RtComponentPropertyVector> props;
    props.push_back(std::vector<uint32_t>{42});  // input
    props.push_back(std::vector<uint32_t>{0});   // previousValue (state)
    props.push_back(std::vector<uint32_t>{0});   // output
    
    std::vector<size_t> indices = {0, 1, 2};
    MockGraphBatch batch;
    auto comp = spec->createComponentBatch(batch, props, indices);
    
    auto& input = std::get<std::vector<uint32_t>>(props[0]);
    auto& result = std::get<std::vector<uint32_t>>(props[2]);
    
    comp->updateRange(nullptr, 0, 1);
    if (result[0] != 0) {
      throw DxvkError("PreviousFrameValue<Enum> failed on first update");
    }
    
    input[0] = 99;
    comp->updateRange(nullptr, 0, 1);
    if (result[0] != 42) {
      throw DxvkError("PreviousFrameValue<Enum> failed on second update");
    }
    
    testedCount++;
  }
  
  // Test Hash variant
  {
    std::unordered_map<std::string, RtComponentPropertyType> desiredTypes = {
      {"input", RtComponentPropertyType::Hash},
      {"previousValue", RtComponentPropertyType::Hash},
      {"output", RtComponentPropertyType::Hash}
    };
    const RtComponentSpec* spec = getComponentVariant("lightspeed.trex.logic.PreviousFrameValue", desiredTypes);
    if (!spec) {
      throw DxvkError("Failed to find PreviousFrameValue<Hash> component");
    }
    
    std::vector<RtComponentPropertyVector> props;
    props.push_back(std::vector<uint64_t>{0xCAFEBABE});  // input
    props.push_back(std::vector<uint64_t>{0});           // previousValue (state)
    props.push_back(std::vector<uint64_t>{0});           // output
    
    std::vector<size_t> indices = {0, 1, 2};
    MockGraphBatch batch;
    auto comp = spec->createComponentBatch(batch, props, indices);
    
    auto& input = std::get<std::vector<uint64_t>>(props[0]);
    auto& result = std::get<std::vector<uint64_t>>(props[2]);
    
    comp->updateRange(nullptr, 0, 1);
    if (result[0] != 0) {
      throw DxvkError("PreviousFrameValue<Hash> failed on first update");
    }
    
    input[0] = 0xDEADBEEF;
    comp->updateRange(nullptr, 0, 1);
    if (result[0] != 0xCAFEBABE) {
      throw DxvkError("PreviousFrameValue<Hash> failed on second update");
    }
    
    testedCount++;
  }
  
  // Test Prim variant
  {
    std::unordered_map<std::string, RtComponentPropertyType> desiredTypes = {
      {"input", RtComponentPropertyType::Prim},
      {"previousValue", RtComponentPropertyType::Prim},
      {"output", RtComponentPropertyType::Prim}
    };
    const RtComponentSpec* spec = getComponentVariant("lightspeed.trex.logic.PreviousFrameValue", desiredTypes);
    if (!spec) {
      throw DxvkError("Failed to find PreviousFrameValue<Prim> component");
    }
    
    std::vector<RtComponentPropertyVector> props;
    props.push_back(std::vector<PrimTarget>{PrimTarget{3, 300}});  // input
    props.push_back(std::vector<PrimTarget>{PrimTarget{}});        // previousValue (state)
    props.push_back(std::vector<PrimTarget>{PrimTarget{}});        // output
    
    std::vector<size_t> indices = {0, 1, 2};
    MockGraphBatch batch;
    auto comp = spec->createComponentBatch(batch, props, indices);
    
    auto& input = std::get<std::vector<PrimTarget>>(props[0]);
    auto& result = std::get<std::vector<PrimTarget>>(props[2]);
    
    // First frame should output the previous value (default PrimTarget)
    comp->updateRange(nullptr, 0, 1);
    if (result[0] != kInvalidPrimTarget) {
      throw DxvkError("PreviousFrameValue<Prim> failed on first update");
    }
    
    input[0] = PrimTarget{5, 500};
    comp->updateRange(nullptr, 0, 1);
    if (result[0].replacementIndex != 3 || result[0].instanceId != 300) {
      throw DxvkError("PreviousFrameValue<Prim> failed on second update");
    }
    
    testedCount++;
  }
  
  // Test String variant
  {
    std::unordered_map<std::string, RtComponentPropertyType> desiredTypes = {
      {"input", RtComponentPropertyType::String},
      {"previousValue", RtComponentPropertyType::String},
      {"output", RtComponentPropertyType::String}
    };
    const RtComponentSpec* spec = getComponentVariant("lightspeed.trex.logic.PreviousFrameValue", desiredTypes);
    if (!spec) {
      throw DxvkError("Failed to find PreviousFrameValue<String> component");
    }
    
    std::vector<RtComponentPropertyVector> props;
    props.push_back(std::vector<std::string>{"new"});  // input
    props.push_back(std::vector<std::string>{""});     // previousValue (state)
    props.push_back(std::vector<std::string>{""});     // output
    
    std::vector<size_t> indices = {0, 1, 2};
    MockGraphBatch batch;
    auto comp = spec->createComponentBatch(batch, props, indices);
    
    auto& input = std::get<std::vector<std::string>>(props[0]);
    auto& result = std::get<std::vector<std::string>>(props[2]);
    
    comp->updateRange(nullptr, 0, 1);
    if (result[0] != "") {
      throw DxvkError("PreviousFrameValue<String> failed on first update");
    }
    
    input[0] = "newer";
    comp->updateRange(nullptr, 0, 1);
    if (result[0] != "new") {
      throw DxvkError("PreviousFrameValue<String> failed on second update");
    }
    
    testedCount++;
  }
  
  if (variants.size() != static_cast<size_t>(testedCount)) {
    throw DxvkError(str::format("PreviousFrameValue variant count mismatch: expected ", testedCount, ", tested ", testedCount, ", found ", variants.size()));
  }
  
  Logger::info("PreviousFrameValue component passed (Float, Float2, Float3, Float4, Bool, Enum, Hash, Prim, String)");
}

void testRemap() {
  
  XXH64_hash_t baseHash = XXH3_64bits("lightspeed.trex.logic.Remap", strlen("lightspeed.trex.logic.Remap"));
  const ComponentSpecVariantMap& variants = getAllComponentSpecVariants(baseHash);
  
  int variantsTestedCount = 0;
  
  // Test Float variant - EaseIn interpolation
  {
    std::unordered_map<std::string, RtComponentPropertyType> desiredTypes = {
      {"outputMin", RtComponentPropertyType::Float},
      {"outputMax", RtComponentPropertyType::Float},
      {"output", RtComponentPropertyType::Float}
    };
    const RtComponentSpec* spec = getComponentVariant("lightspeed.trex.logic.Remap", desiredTypes);
    if (!spec) {
      throw DxvkError("Failed to find Remap<Float> component");
    }
    
    std::vector<RtComponentPropertyVector> props;
    props.push_back(std::vector<float>{0.0f, 0.5f, 1.0f, 1.5f, -0.5f});  // value
    props.push_back(std::vector<float>{0.0f, 0.0f, 0.0f, 0.0f, 0.0f});  // inputMin
    props.push_back(std::vector<float>{1.0f, 1.0f, 1.0f, 1.0f, 1.0f});  // inputMax
    props.push_back(std::vector<uint32_t>{0, 0, 0, 0, 1});              // clampInput (last value clamped)
    props.push_back(std::vector<uint32_t>{2, 2, 2, 2, 2});              // easingType (EaseIn = 2)
    props.push_back(std::vector<uint32_t>{0, 0, 0, 0, 0});              // shouldReverse
    props.push_back(std::vector<float>{0.0f, 0.0f, 0.0f, 0.0f, 0.0f}); // outputMin
    props.push_back(std::vector<float>{100.0f, 100.0f, 100.0f, 100.0f, 100.0f}); // outputMax
    props.push_back(std::vector<float>{0.0f, 0.0f, 0.0f, 0.0f, 0.0f}); // output
    
    std::vector<size_t> indices(props.size());
    std::iota(indices.begin(), indices.end(), 0);
    MockGraphBatch batch;
    auto comp = spec->createComponentBatch(batch, props, indices);
    comp->updateRange(nullptr, 0, 5);
    
    auto& result = std::get<std::vector<float>>(props[8]);
    // EaseIn: eased = time^2, then map [0,1] to [0,100]
    // value=0.0: norm=0.0, eased=0.0^2=0.0 → output=0.0
    if (!floatEquals(result[0], 0.0f)) { throw DxvkError("Remap<Float> EaseIn failed: value=0.0"); }
    // value=0.5: norm=0.5, eased=0.5^2=0.25 → output=25.0
    if (!floatEquals(result[1], 25.0f)) { throw DxvkError("Remap<Float> EaseIn failed: value=0.5 should map to 25.0"); }
    // value=1.0: norm=1.0, eased=1.0^2=1.0 → output=100.0
    if (!floatEquals(result[2], 100.0f)) { throw DxvkError("Remap<Float> EaseIn failed: value=1.0"); }
    // Without clamping, extrapolation: norm=1.5, eased=1.5^2=2.25 → output=225.0
    if (!floatEquals(result[3], 225.0f)) { throw DxvkError("Remap<Float> EaseIn failed: value=1.5 should extrapolate to 225.0"); }
    // With clamping: -0.5 clamped to 0.0, eased=0.0 → output=0.0
    if (!floatEquals(result[4], 0.0f)) { throw DxvkError("Remap<Float> EaseIn failed: clamped value=-0.5"); }
  }
  
  // Test Float variant - Input clamping with EaseIn
  {
    std::unordered_map<std::string, RtComponentPropertyType> desiredTypes = {
      {"outputMin", RtComponentPropertyType::Float},
      {"outputMax", RtComponentPropertyType::Float},
      {"output", RtComponentPropertyType::Float}
    };
    const RtComponentSpec* spec = getComponentVariant("lightspeed.trex.logic.Remap", desiredTypes);
    if (!spec) {
      throw DxvkError("Failed to find Remap<Float> component");
    }
    
    std::vector<RtComponentPropertyVector> props;
    props.push_back(std::vector<float>{-10.0f, 0.0f, 5.0f, 10.0f, 20.0f}); // value (below, at min, mid, at max, above)
    props.push_back(std::vector<float>{0.0f, 0.0f, 0.0f, 0.0f, 0.0f});      // inputMin
    props.push_back(std::vector<float>{10.0f, 10.0f, 10.0f, 10.0f, 10.0f}); // inputMax
    props.push_back(std::vector<uint32_t>{1, 1, 1, 1, 1});                  // clampInput (all clamped)
    props.push_back(std::vector<uint32_t>{2, 2, 2, 2, 2});                  // easingType (EaseIn = 2)
    props.push_back(std::vector<uint32_t>{0, 0, 0, 0, 0});                  // shouldReverse
    props.push_back(std::vector<float>{0.0f, 0.0f, 0.0f, 0.0f, 0.0f});      // outputMin
    props.push_back(std::vector<float>{100.0f, 100.0f, 100.0f, 100.0f, 100.0f}); // outputMax
    props.push_back(std::vector<float>{0.0f, 0.0f, 0.0f, 0.0f, 0.0f});      // output
    
    std::vector<size_t> indices(props.size());
    std::iota(indices.begin(), indices.end(), 0);
    MockGraphBatch batch;
    auto comp = spec->createComponentBatch(batch, props, indices);
    comp->updateRange(nullptr, 0, 5);
    
    auto& result = std::get<std::vector<float>>(props[8]);
    // value=-10: clamped to 0, norm=0.0, eased=0.0 → output=0.0
    if (!floatEquals(result[0], 0.0f)) { throw DxvkError("Remap<Float> clamping failed: value=-10 should clamp to min"); }
    // value=0: at min, norm=0.0, eased=0.0 → output=0.0
    if (!floatEquals(result[1], 0.0f)) { throw DxvkError("Remap<Float> clamping failed: value=0 at min"); }
    // value=5: mid, norm=0.5, eased=0.25 → output=25.0
    if (!floatEquals(result[2], 25.0f)) { throw DxvkError("Remap<Float> clamping failed: value=5 mid"); }
    // value=10: at max, norm=1.0, eased=1.0 → output=100.0
    if (!floatEquals(result[3], 100.0f)) { throw DxvkError("Remap<Float> clamping failed: value=10 at max"); }
    // value=20: clamped to 10, norm=1.0, eased=1.0 → output=100.0
    if (!floatEquals(result[4], 100.0f)) { throw DxvkError("Remap<Float> clamping failed: value=20 should clamp to max"); }
  }
  
  // Test Float variant - Input clamping with reversed range and Cubic
  {
    std::unordered_map<std::string, RtComponentPropertyType> desiredTypes = {
      {"outputMin", RtComponentPropertyType::Float},
      {"outputMax", RtComponentPropertyType::Float},
      {"output", RtComponentPropertyType::Float}
    };
    const RtComponentSpec* spec = getComponentVariant("lightspeed.trex.logic.Remap", desiredTypes);
    if (!spec) {
      throw DxvkError("Failed to find Remap<Float> component");
    }
    
    std::vector<RtComponentPropertyVector> props;
    props.push_back(std::vector<float>{25.0f, 20.0f, 15.0f, 10.0f, 5.0f}); // value (above reversed min, at min, mid, at max, below reversed max)
    props.push_back(std::vector<float>{20.0f, 20.0f, 20.0f, 20.0f, 20.0f}); // inputMin (reversed: 20 > 10)
    props.push_back(std::vector<float>{10.0f, 10.0f, 10.0f, 10.0f, 10.0f}); // inputMax
    props.push_back(std::vector<uint32_t>{1, 1, 1, 1, 1});                  // clampInput (all clamped)
    props.push_back(std::vector<uint32_t>{1, 1, 1, 1, 1});                  // easingType (Cubic = 1)
    props.push_back(std::vector<uint32_t>{0, 0, 0, 0, 0});                  // shouldReverse
    props.push_back(std::vector<float>{0.0f, 0.0f, 0.0f, 0.0f, 0.0f});      // outputMin
    props.push_back(std::vector<float>{100.0f, 100.0f, 100.0f, 100.0f, 100.0f}); // outputMax
    props.push_back(std::vector<float>{0.0f, 0.0f, 0.0f, 0.0f, 0.0f});      // output
    
    std::vector<size_t> indices(props.size());
    std::iota(indices.begin(), indices.end(), 0);
    MockGraphBatch batch;
    auto comp = spec->createComponentBatch(batch, props, indices);
    comp->updateRange(nullptr, 0, 5);
    
    auto& result = std::get<std::vector<float>>(props[8]);
    // Reversed range [20,10] with clamping: values clamped to [10,20] (swap for clamping)
    // value=25: clamped to 20 (min), norm=0.0, eased=0.0 → output=0.0
    if (!floatEquals(result[0], 0.0f)) { throw DxvkError("Remap<Float> reversed clamping failed: value=25 should clamp to min(20)"); }
    // value=20: at min, norm=0.0, eased=0.0 → output=0.0
    if (!floatEquals(result[1], 0.0f)) { throw DxvkError("Remap<Float> reversed clamping failed: value=20 at min"); }
    // value=15: mid, norm=0.5, eased=0.125 → output=12.5
    if (!floatEquals(result[2], 12.5f)) { throw DxvkError("Remap<Float> reversed clamping failed: value=15 mid"); }
    // value=10: at max, norm=1.0, eased=1.0 → output=100.0
    if (!floatEquals(result[3], 100.0f)) { throw DxvkError("Remap<Float> reversed clamping failed: value=10 at max"); }
    // value=5: clamped to 10 (max), norm=1.0, eased=1.0 → output=100.0
    if (!floatEquals(result[4], 100.0f)) { throw DxvkError("Remap<Float> reversed clamping failed: value=5 should clamp to max(10)"); }
  }
  
  // Test Float variant - Extrapolation (no clamping) with EaseIn
  {
    std::unordered_map<std::string, RtComponentPropertyType> desiredTypes = {
      {"outputMin", RtComponentPropertyType::Float},
      {"outputMax", RtComponentPropertyType::Float},
      {"output", RtComponentPropertyType::Float}
    };
    const RtComponentSpec* spec = getComponentVariant("lightspeed.trex.logic.Remap", desiredTypes);
    if (!spec) {
      throw DxvkError("Failed to find Remap<Float> component");
    }
    
    std::vector<RtComponentPropertyVector> props;
    props.push_back(std::vector<float>{-5.0f, 0.0f, 5.0f, 10.0f, 15.0f}); // value (below, at min, mid, at max, above)
    props.push_back(std::vector<float>{0.0f, 0.0f, 0.0f, 0.0f, 0.0f});     // inputMin
    props.push_back(std::vector<float>{10.0f, 10.0f, 10.0f, 10.0f, 10.0f}); // inputMax
    props.push_back(std::vector<uint32_t>{0, 0, 0, 0, 0});                 // clampInput (NO clamping)
    props.push_back(std::vector<uint32_t>{2, 2, 2, 2, 2});                 // easingType (EaseIn = 2)
    props.push_back(std::vector<uint32_t>{0, 0, 0, 0, 0});                 // shouldReverse
    props.push_back(std::vector<float>{0.0f, 0.0f, 0.0f, 0.0f, 0.0f});     // outputMin
    props.push_back(std::vector<float>{100.0f, 100.0f, 100.0f, 100.0f, 100.0f}); // outputMax
    props.push_back(std::vector<float>{0.0f, 0.0f, 0.0f, 0.0f, 0.0f});     // output
    
    std::vector<size_t> indices(props.size());
    std::iota(indices.begin(), indices.end(), 0);
    MockGraphBatch batch;
    auto comp = spec->createComponentBatch(batch, props, indices);
    comp->updateRange(nullptr, 0, 5);
    
    auto& result = std::get<std::vector<float>>(props[8]);
    // EaseIn: eased = norm^2
    // value=-5: norm=-0.5, eased=(-0.5)^2=0.25 → output=25.0 (extrapolates below)
    if (!floatEquals(result[0], 25.0f)) { throw DxvkError("Remap<Float> extrapolation failed: value=-5 below range"); }
    // value=0: norm=0.0, eased=0.0 → output=0.0
    if (!floatEquals(result[1], 0.0f)) { throw DxvkError("Remap<Float> extrapolation failed: value=0 at min"); }
    // value=5: norm=0.5, eased=0.25 → output=25.0
    if (!floatEquals(result[2], 25.0f)) { throw DxvkError("Remap<Float> extrapolation failed: value=5 mid"); }
    // value=10: norm=1.0, eased=1.0 → output=100.0
    if (!floatEquals(result[3], 100.0f)) { throw DxvkError("Remap<Float> extrapolation failed: value=10 at max"); }
    // value=15: norm=1.5, eased=1.5^2=2.25 → output=225.0 (extrapolates above)
    if (!floatEquals(result[4], 225.0f)) { throw DxvkError("Remap<Float> extrapolation failed: value=15 above range"); }
  }
  
  // Test Float variant - Extrapolation with reversed range and Cubic
  {
    std::unordered_map<std::string, RtComponentPropertyType> desiredTypes = {
      {"outputMin", RtComponentPropertyType::Float},
      {"outputMax", RtComponentPropertyType::Float},
      {"output", RtComponentPropertyType::Float}
    };
    const RtComponentSpec* spec = getComponentVariant("lightspeed.trex.logic.Remap", desiredTypes);
    if (!spec) {
      throw DxvkError("Failed to find Remap<Float> component");
    }
    
    std::vector<RtComponentPropertyVector> props;
    props.push_back(std::vector<float>{25.0f, 20.0f, 15.0f, 10.0f, 5.0f}); // value (above reversed min, at min, mid, at max, below reversed max)
    props.push_back(std::vector<float>{20.0f, 20.0f, 20.0f, 20.0f, 20.0f}); // inputMin (reversed: 20 > 10)
    props.push_back(std::vector<float>{10.0f, 10.0f, 10.0f, 10.0f, 10.0f}); // inputMax
    props.push_back(std::vector<uint32_t>{0, 0, 0, 0, 0});                  // clampInput (NO clamping)
    props.push_back(std::vector<uint32_t>{1, 1, 1, 1, 1});                  // easingType (Cubic = 1)
    props.push_back(std::vector<uint32_t>{0, 0, 0, 0, 0});                  // shouldReverse
    props.push_back(std::vector<float>{0.0f, 0.0f, 0.0f, 0.0f, 0.0f});      // outputMin
    props.push_back(std::vector<float>{100.0f, 100.0f, 100.0f, 100.0f, 100.0f}); // outputMax
    props.push_back(std::vector<float>{0.0f, 0.0f, 0.0f, 0.0f, 0.0f});      // output
    
    std::vector<size_t> indices(props.size());
    std::iota(indices.begin(), indices.end(), 0);
    MockGraphBatch batch;
    auto comp = spec->createComponentBatch(batch, props, indices);
    comp->updateRange(nullptr, 0, 5);
    
    auto& result = std::get<std::vector<float>>(props[8]);
    // Reversed range [20,10]: norm = (value - 20) / -10
    // Cubic: eased = norm^3
    // value=25: norm=(25-20)/-10=-0.5, eased=(-0.5)^3=-0.125 → output=-12.5 (extrapolates below 0)
    if (!floatEquals(result[0], -12.5f)) { throw DxvkError("Remap<Float> reversed extrapolation failed: value=25 above reversed min"); }
    // value=20: norm=0.0, eased=0.0 → output=0.0
    if (!floatEquals(result[1], 0.0f)) { throw DxvkError("Remap<Float> reversed extrapolation failed: value=20 at min"); }
    // value=15: norm=0.5, eased=0.125 → output=12.5
    if (!floatEquals(result[2], 12.5f)) { throw DxvkError("Remap<Float> reversed extrapolation failed: value=15 mid"); }
    // value=10: norm=1.0, eased=1.0 → output=100.0
    if (!floatEquals(result[3], 100.0f)) { throw DxvkError("Remap<Float> reversed extrapolation failed: value=10 at max"); }
    // value=5: norm=(5-20)/-10=1.5, eased=1.5^3=3.375 → output=337.5 (extrapolates above 100)
    if (!floatEquals(result[4], 337.5f)) { throw DxvkError("Remap<Float> reversed extrapolation failed: value=5 below reversed max"); }
  }
  
  // Test Float variant - Reversed input range with EaseOut
  {
    std::unordered_map<std::string, RtComponentPropertyType> desiredTypes = {
      {"outputMin", RtComponentPropertyType::Float},
      {"outputMax", RtComponentPropertyType::Float},
      {"output", RtComponentPropertyType::Float}
    };
    const RtComponentSpec* spec = getComponentVariant("lightspeed.trex.logic.Remap", desiredTypes);
    if (!spec) {
      throw DxvkError("Failed to find Remap<Float> component");
    }
    
    std::vector<RtComponentPropertyVector> props;
    props.push_back(std::vector<float>{20.0f, 15.0f, 10.0f});   // value
    props.push_back(std::vector<float>{20.0f, 20.0f, 20.0f});   // inputMin (reversed: min > max)
    props.push_back(std::vector<float>{10.0f, 10.0f, 10.0f});   // inputMax
    props.push_back(std::vector<uint32_t>{0, 0, 0});            // clampInput
    props.push_back(std::vector<uint32_t>{3, 3, 3});            // easingType (EaseOut = 3)
    props.push_back(std::vector<uint32_t>{0, 0, 0});            // shouldReverse
    props.push_back(std::vector<float>{0.0f, 0.0f, 0.0f});      // outputMin
    props.push_back(std::vector<float>{100.0f, 100.0f, 100.0f}); // outputMax
    props.push_back(std::vector<float>{0.0f, 0.0f, 0.0f});      // output
    
    std::vector<size_t> indices(props.size());
    std::iota(indices.begin(), indices.end(), 0);
    MockGraphBatch batch;
    auto comp = spec->createComponentBatch(batch, props, indices);
    comp->updateRange(nullptr, 0, 3);
    
    auto& result = std::get<std::vector<float>>(props[8]);
    // Reversed input [20,10]: normalized = (value - 20) / (10 - 20) = (value - 20) / -10
    // EaseOut: eased = 1.0 - (1.0 - norm)^2
    // value=20: norm=0.0, eased=1.0-(1.0-0.0)^2=1.0-1.0=0.0 → output=0.0
    if (!floatEquals(result[0], 0.0f)) { throw DxvkError("Remap<Float> EaseOut failed: reversed input value=20"); }
    // value=15: norm=0.5, eased=1.0-(1.0-0.5)^2=1.0-0.25=0.75 → output=75.0
    if (!floatEquals(result[1], 75.0f)) { throw DxvkError("Remap<Float> EaseOut failed: reversed input value=15"); }
    // value=10: norm=1.0, eased=1.0-(1.0-1.0)^2=1.0-0.0=1.0 → output=100.0
    if (!floatEquals(result[2], 100.0f)) { throw DxvkError("Remap<Float> EaseOut failed: reversed input value=10"); }
    
  }
  
  // Test Float variant - Reversed output range with EaseIn
  {
    std::unordered_map<std::string, RtComponentPropertyType> desiredTypes = {
      {"outputMin", RtComponentPropertyType::Float},
      {"outputMax", RtComponentPropertyType::Float},
      {"output", RtComponentPropertyType::Float}
    };
    const RtComponentSpec* spec = getComponentVariant("lightspeed.trex.logic.Remap", desiredTypes);
    if (!spec) {
      throw DxvkError("Failed to find Remap<Float> component");
    }
    
    std::vector<RtComponentPropertyVector> props;
    props.push_back(std::vector<float>{0.0f, 0.5f, 1.0f});      // value
    props.push_back(std::vector<float>{0.0f, 0.0f, 0.0f});      // inputMin
    props.push_back(std::vector<float>{1.0f, 1.0f, 1.0f});      // inputMax
    props.push_back(std::vector<uint32_t>{0, 0, 0});            // clampInput
    props.push_back(std::vector<uint32_t>{2, 2, 2});            // easingType (EaseIn = 2)
    props.push_back(std::vector<uint32_t>{0, 0, 0});            // shouldReverse
    props.push_back(std::vector<float>{100.0f, 100.0f, 100.0f}); // outputMin (reversed: min > max)
    props.push_back(std::vector<float>{0.0f, 0.0f, 0.0f});       // outputMax
    props.push_back(std::vector<float>{0.0f, 0.0f, 0.0f});      // output
    
    std::vector<size_t> indices(props.size());
    std::iota(indices.begin(), indices.end(), 0);
    MockGraphBatch batch;
    auto comp = spec->createComponentBatch(batch, props, indices);
    comp->updateRange(nullptr, 0, 3);
    
    auto& result = std::get<std::vector<float>>(props[8]);
    // EaseIn: eased = norm^2, reversed output lerp(100, 0, eased) = 100 - 100*eased
    // value=0.0: norm=0.0, eased=0.0^2=0.0 → output=100-0=100
    if (!floatEquals(result[0], 100.0f)) { throw DxvkError("Remap<Float> EaseIn failed: reversed output value=0.0"); }
    // value=0.5: norm=0.5, eased=0.5^2=0.25 → output=100-25=75
    if (!floatEquals(result[1], 75.0f)) { throw DxvkError("Remap<Float> EaseIn failed: reversed output value=0.5"); }
    // value=1.0: norm=1.0, eased=1.0^2=1.0 → output=100-100=0
    if (!floatEquals(result[2], 0.0f)) { throw DxvkError("Remap<Float> EaseIn failed: reversed output value=1.0"); }
    
  }
  
  // Test Float variant - Both ranges reversed with Cubic easing
  {
    std::unordered_map<std::string, RtComponentPropertyType> desiredTypes = {
      {"outputMin", RtComponentPropertyType::Float},
      {"outputMax", RtComponentPropertyType::Float},
      {"output", RtComponentPropertyType::Float}
    };
    const RtComponentSpec* spec = getComponentVariant("lightspeed.trex.logic.Remap", desiredTypes);
    if (!spec) {
      throw DxvkError("Failed to find Remap<Float> component");
    }
    
    std::vector<RtComponentPropertyVector> props;
    props.push_back(std::vector<float>{20.0f, 15.0f, 10.0f});   // value
    props.push_back(std::vector<float>{20.0f, 20.0f, 20.0f});   // inputMin (reversed)
    props.push_back(std::vector<float>{10.0f, 10.0f, 10.0f});   // inputMax
    props.push_back(std::vector<uint32_t>{0, 0, 0});            // clampInput
    props.push_back(std::vector<uint32_t>{1, 1, 1});            // easingType (Cubic = 1)
    props.push_back(std::vector<uint32_t>{0, 0, 0});            // shouldReverse
    props.push_back(std::vector<float>{100.0f, 100.0f, 100.0f}); // outputMin (reversed)
    props.push_back(std::vector<float>{0.0f, 0.0f, 0.0f});       // outputMax
    props.push_back(std::vector<float>{0.0f, 0.0f, 0.0f});      // output
    
    std::vector<size_t> indices(props.size());
    std::iota(indices.begin(), indices.end(), 0);
    MockGraphBatch batch;
    auto comp = spec->createComponentBatch(batch, props, indices);
    comp->updateRange(nullptr, 0, 3);
    
    auto& result = std::get<std::vector<float>>(props[8]);
    // Reversed input [20,10]: norm = (value - 20) / -10
    // Cubic: eased = norm^3, reversed output: lerp(100, 0, eased) = 100 - 100*eased
    // value=20: norm=0.0, eased=0.0^3=0.0 → output=100-0=100
    if (!floatEquals(result[0], 100.0f)) { throw DxvkError("Remap<Float> Cubic failed: both ranges reversed value=20"); }
    // value=15: norm=0.5, eased=0.5^3=0.125 → output=100-12.5=87.5
    if (!floatEquals(result[1], 87.5f)) { throw DxvkError("Remap<Float> Cubic failed: both ranges reversed value=15"); }
    // value=10: norm=1.0, eased=1.0^3=1.0 → output=100-100=0
    if (!floatEquals(result[2], 0.0f)) { throw DxvkError("Remap<Float> Cubic failed: both ranges reversed value=10"); }
    
  }
  
  // Test Float variant - shouldReverse with EaseIn (tests actual easing reversal)
  {
    std::unordered_map<std::string, RtComponentPropertyType> desiredTypes = {
      {"outputMin", RtComponentPropertyType::Float},
      {"outputMax", RtComponentPropertyType::Float},
      {"output", RtComponentPropertyType::Float}
    };
    const RtComponentSpec* spec = getComponentVariant("lightspeed.trex.logic.Remap", desiredTypes);
    if (!spec) {
      throw DxvkError("Failed to find Remap<Float> component");
    }
    
    std::vector<RtComponentPropertyVector> props;
    props.push_back(std::vector<float>{0.0f, 0.5f, 1.0f});      // value
    props.push_back(std::vector<float>{0.0f, 0.0f, 0.0f});      // inputMin
    props.push_back(std::vector<float>{1.0f, 1.0f, 1.0f});      // inputMax
    props.push_back(std::vector<uint32_t>{0, 0, 0});            // clampInput
    props.push_back(std::vector<uint32_t>{2, 2, 2});            // easingType (EaseIn = 2)
    props.push_back(std::vector<uint32_t>{1, 1, 1});            // shouldReverse (true)
    props.push_back(std::vector<float>{0.0f, 0.0f, 0.0f});      // outputMin
    props.push_back(std::vector<float>{100.0f, 100.0f, 100.0f}); // outputMax
    props.push_back(std::vector<float>{0.0f, 0.0f, 0.0f});      // output
    
    std::vector<size_t> indices(props.size());
    std::iota(indices.begin(), indices.end(), 0);
    MockGraphBatch batch;
    auto comp = spec->createComponentBatch(batch, props, indices);
    comp->updateRange(nullptr, 0, 3);
    
    auto& result = std::get<std::vector<float>>(props[8]);
    // shouldReverse: flipped = 1-norm, eased = flipped^2, result = 1-eased
    // EaseIn with reverse = EaseOut behavior
    // value=0.0: norm=0.0, flipped=1.0, eased=1.0, result=1-1=0.0 → output=0
    if (!floatEquals(result[0], 0.0f)) { throw DxvkError("Remap<Float> EaseIn+reverse failed: value=0.0"); }
    // value=0.5: norm=0.5, flipped=0.5, eased=0.25, result=1-0.25=0.75 → output=75
    if (!floatEquals(result[1], 75.0f)) { throw DxvkError("Remap<Float> EaseIn+reverse failed: value=0.5 should map to 75.0 (EaseOut behavior)"); }
    // value=1.0: norm=1.0, flipped=0.0, eased=0.0, result=1-0=1.0 → output=100
    if (!floatEquals(result[2], 100.0f)) { throw DxvkError("Remap<Float> EaseIn+reverse failed: value=1.0"); }
    
  }
  
  // increment test count to account for the float tests above.
  variantsTestedCount++;
  // simpler tests for the rest of the types:

  // Test Float2 variant
  {
    std::unordered_map<std::string, RtComponentPropertyType> desiredTypes = {
      {"outputMin", RtComponentPropertyType::Float2},
      {"outputMax", RtComponentPropertyType::Float2},
      {"output", RtComponentPropertyType::Float2}
    };
    const RtComponentSpec* spec = getComponentVariant("lightspeed.trex.logic.Remap", desiredTypes);
    if (!spec) {
      throw DxvkError("Failed to find Remap<Float2> component");
    }
    
    std::vector<RtComponentPropertyVector> props;
    props.push_back(std::vector<float>{0.0f, 0.5f, 1.0f});                           // value
    props.push_back(std::vector<float>{0.0f, 0.0f, 0.0f});                           // inputMin
    props.push_back(std::vector<float>{1.0f, 1.0f, 1.0f});                           // inputMax
    props.push_back(std::vector<uint32_t>{0, 0, 0});                                 // clampInput
    props.push_back(std::vector<uint32_t>{0, 0, 0});                                 // easingType (Linear)
    props.push_back(std::vector<uint32_t>{0, 0, 0});                                 // shouldReverse
    props.push_back(std::vector<Vector2>{Vector2(0.0f, 0.0f), Vector2(0.0f, 0.0f), Vector2(0.0f, 0.0f)}); // outputMin
    props.push_back(std::vector<Vector2>{Vector2(10.0f, 20.0f), Vector2(10.0f, 20.0f), Vector2(10.0f, 20.0f)}); // outputMax
    props.push_back(std::vector<Vector2>{Vector2(), Vector2(), Vector2()});          // output
    
    std::vector<size_t> indices(props.size());
    std::iota(indices.begin(), indices.end(), 0);
    MockGraphBatch batch;
    auto comp = spec->createComponentBatch(batch, props, indices);
    comp->updateRange(nullptr, 0, 3);
    
    auto& result = std::get<std::vector<Vector2>>(props[8]);
    if (!vectorEquals(result[0], Vector2(0.0f, 0.0f))) { throw DxvkError("Remap<Float2> failed: value=0.0"); }
    if (!vectorEquals(result[1], Vector2(5.0f, 10.0f))) { throw DxvkError("Remap<Float2> failed: value=0.5"); }
    if (!vectorEquals(result[2], Vector2(10.0f, 20.0f))) { throw DxvkError("Remap<Float2> failed: value=1.0"); }
    
    variantsTestedCount++;
  }
  
  // Test Float3 variant
  {
    std::unordered_map<std::string, RtComponentPropertyType> desiredTypes = {
      {"outputMin", RtComponentPropertyType::Float3},
      {"outputMax", RtComponentPropertyType::Float3},
      {"output", RtComponentPropertyType::Float3}
    };
    const RtComponentSpec* spec = getComponentVariant("lightspeed.trex.logic.Remap", desiredTypes);
    if (!spec) {
      throw DxvkError("Failed to find Remap<Float3> component");
    }
    
    std::vector<RtComponentPropertyVector> props;
    props.push_back(std::vector<float>{0.0f, 1.0f});                                 // value
    props.push_back(std::vector<float>{0.0f, 0.0f});                                 // inputMin
    props.push_back(std::vector<float>{1.0f, 1.0f});                                 // inputMax
    props.push_back(std::vector<uint32_t>{0, 0});                                    // clampInput
    props.push_back(std::vector<uint32_t>{0, 0});                                    // easingType (Linear)
    props.push_back(std::vector<uint32_t>{0, 0});                                    // shouldReverse
    props.push_back(std::vector<Vector3>{Vector3(1.0f, 2.0f, 3.0f), Vector3(1.0f, 2.0f, 3.0f)}); // outputMin
    props.push_back(std::vector<Vector3>{Vector3(11.0f, 12.0f, 13.0f), Vector3(11.0f, 12.0f, 13.0f)}); // outputMax
    props.push_back(std::vector<Vector3>{Vector3(), Vector3()});                     // output
    
    std::vector<size_t> indices(props.size());
    std::iota(indices.begin(), indices.end(), 0);
    MockGraphBatch batch;
    auto comp = spec->createComponentBatch(batch, props, indices);
    comp->updateRange(nullptr, 0, 2);
    
    auto& result = std::get<std::vector<Vector3>>(props[8]);
    if (!vectorEquals(result[0], Vector3(1.0f, 2.0f, 3.0f))) { throw DxvkError("Remap<Float3> failed: value=0.0"); }
    if (!vectorEquals(result[1], Vector3(11.0f, 12.0f, 13.0f))) { throw DxvkError("Remap<Float3> failed: value=1.0"); }
    
    variantsTestedCount++;
  }
  
  // Test Float4 variant
  {
    std::unordered_map<std::string, RtComponentPropertyType> desiredTypes = {
      {"outputMin", RtComponentPropertyType::Float4},
      {"outputMax", RtComponentPropertyType::Float4},
      {"output", RtComponentPropertyType::Float4}
    };
    const RtComponentSpec* spec = getComponentVariant("lightspeed.trex.logic.Remap", desiredTypes);
    if (!spec) {
      throw DxvkError("Failed to find Remap<Float4> component");
    }
    
    std::vector<RtComponentPropertyVector> props;
    props.push_back(std::vector<float>{0.25f});                                      // value
    props.push_back(std::vector<float>{0.0f});                                       // inputMin
    props.push_back(std::vector<float>{1.0f});                                       // inputMax
    props.push_back(std::vector<uint32_t>{0});                                       // clampInput
    props.push_back(std::vector<uint32_t>{0});                                       // easingType (Linear)
    props.push_back(std::vector<uint32_t>{0});                                       // shouldReverse
    props.push_back(std::vector<Vector4>{Vector4(0.0f, 0.0f, 0.0f, 0.0f)});          // outputMin
    props.push_back(std::vector<Vector4>{Vector4(100.0f, 200.0f, 300.0f, 400.0f)});  // outputMax
    props.push_back(std::vector<Vector4>{Vector4()});                                // output
    
    std::vector<size_t> indices(props.size());
    std::iota(indices.begin(), indices.end(), 0);
    MockGraphBatch batch;
    auto comp = spec->createComponentBatch(batch, props, indices);
    comp->updateRange(nullptr, 0, 1);
    
    auto& result = std::get<std::vector<Vector4>>(props[8]);
    if (!vectorEquals(result[0], Vector4(25.0f, 50.0f, 75.0f, 100.0f))) { throw DxvkError("Remap<Float4> failed: value=0.25"); }
    
    variantsTestedCount++;
  }
  
  if (variantsTestedCount != static_cast<int>(variants.size())) {
    Logger::warn(str::format("Remap variant count mismatch: expected ", variants.size(), ", tested ", variantsTestedCount));
  }
  
  Logger::info(str::format("Remap component passed - all ", variantsTestedCount, " variants tested (Float with EaseIn/EaseOut/Cubic, clamping, extrapolation, reversed ranges; Float2, Float3, Float4)"));
}

void testLoop() {
  
  XXH64_hash_t baseHash = XXH3_64bits("lightspeed.trex.logic.Loop", strlen("lightspeed.trex.logic.Loop"));
  const ComponentSpecVariantMap& variants = getAllComponentSpecVariants(baseHash);
  
  int testedCount = 0;
  
  // Test Float variant - Loop type
  {
    std::unordered_map<std::string, RtComponentPropertyType> desiredTypes = {
      {"value", RtComponentPropertyType::Float},
      {"minRange", RtComponentPropertyType::Float},
      {"maxRange", RtComponentPropertyType::Float},
      {"loopedValue", RtComponentPropertyType::Float}
    };
    const RtComponentSpec* spec = getComponentVariant("lightspeed.trex.logic.Loop", desiredTypes);
    if (!spec) {
      throw DxvkError("Failed to find Loop<Float> component");
    }
    
    std::vector<RtComponentPropertyVector> props;
    props.push_back(std::vector<float>{0.0f, 0.5f, 1.0f, 1.5f, 2.0f, 2.5f, -0.5f}); // value
    props.push_back(std::vector<float>{0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f}); // minRange
    props.push_back(std::vector<float>{1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f}); // maxRange
    props.push_back(std::vector<uint32_t>{0, 0, 0, 0, 0, 0, 0});                   // loopingType (Loop = 0)
    props.push_back(std::vector<float>{0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f}); // loopedValue
    props.push_back(std::vector<uint32_t>{0, 0, 0, 0, 0, 0, 0});                   // isReversing
    
    std::vector<size_t> indices(props.size());
    std::iota(indices.begin(), indices.end(), 0);
    MockGraphBatch batch;
    auto comp = spec->createComponentBatch(batch, props, indices);
    comp->updateRange(nullptr, 0, 7);
    
    auto& result = std::get<std::vector<float>>(props[4]);
    auto& isReversing = std::get<std::vector<uint32_t>>(props[5]);
    // Loop wraps: normalized = (value - min) / range, then fractional part
    // value=0.0: at min → 0.0
    if (!floatEquals(result[0], 0.0f)) { throw DxvkError("Loop<Float> Loop failed: value=0.0"); }
    // value=0.5: mid → 0.5
    if (!floatEquals(result[1], 0.5f)) { throw DxvkError("Loop<Float> Loop failed: value=0.5"); }
    // value=1.0: at max (boundary) → wraps to 0.0
    if (!floatEquals(result[2], 0.0f)) { throw DxvkError("Loop<Float> Loop failed: value=1.0 should wrap to 0.0"); }
    // value=1.5: beyond range → 0.5
    if (!floatEquals(result[3], 0.5f)) { throw DxvkError("Loop<Float> Loop failed: value=1.5 should wrap to 0.5"); }
    // value=2.0: two cycles → 0.0
    if (!floatEquals(result[4], 0.0f)) { throw DxvkError("Loop<Float> Loop failed: value=2.0 should wrap to 0.0"); }
    // value=2.5: two cycles + half → 0.5
    if (!floatEquals(result[5], 0.5f)) { throw DxvkError("Loop<Float> Loop failed: value=2.5 should wrap to 0.5"); }
    // value=-0.5: negative wraps to 0.5
    if (!floatEquals(result[6], 0.5f)) { throw DxvkError("Loop<Float> Loop failed: value=-0.5 should wrap to 0.5"); }
    // Loop type never reverses
    for (size_t i = 0; i < 7; i++) {
      if (isReversing[i] != 0) {
        throw DxvkError("Loop<Float> Loop failed: isReversing should be false");
      }
    }
    
    testedCount++;
  }
  
  // Test Float variant - PingPong type
  {
    std::unordered_map<std::string, RtComponentPropertyType> desiredTypes = {
      {"value", RtComponentPropertyType::Float},
      {"minRange", RtComponentPropertyType::Float},
      {"maxRange", RtComponentPropertyType::Float},
      {"loopedValue", RtComponentPropertyType::Float}
    };
    const RtComponentSpec* spec = getComponentVariant("lightspeed.trex.logic.Loop", desiredTypes);
    if (!spec) {
      throw DxvkError("Failed to find Loop<Float> component");
    }
    
    std::vector<RtComponentPropertyVector> props;
    props.push_back(std::vector<float>{0.0f, 0.5f, 1.0f, 1.5f, 2.0f, 2.5f, 3.0f, 3.5f, -0.5f}); // value
    props.push_back(std::vector<float>{0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f});  // minRange
    props.push_back(std::vector<float>{1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f});  // maxRange
    props.push_back(std::vector<uint32_t>{1, 1, 1, 1, 1, 1, 1, 1, 1});                          // loopingType (PingPong = 1)
    props.push_back(std::vector<float>{0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f});  // loopedValue
    props.push_back(std::vector<uint32_t>{0, 0, 0, 0, 0, 0, 0, 0, 0});                          // isReversing
    
    std::vector<size_t> indices(props.size());
    std::iota(indices.begin(), indices.end(), 0);
    MockGraphBatch batch;
    auto comp = spec->createComponentBatch(batch, props, indices);
    comp->updateRange(nullptr, 0, 9);
    
    auto& result = std::get<std::vector<float>>(props[4]);
    auto& isReversing = std::get<std::vector<uint32_t>>(props[5]);
    // PingPong: [0,1] forward, [1,2] reverse back, [2,3] forward again...
    // value=0.0: forward, at min → 0.0, not reversing
    if (!floatEquals(result[0], 0.0f) || isReversing[0] != 0) { throw DxvkError("Loop<Float> PingPong failed: value=0.0"); }
    // value=0.5: forward, mid → 0.5, not reversing
    if (!floatEquals(result[1], 0.5f) || isReversing[1] != 0) { throw DxvkError("Loop<Float> PingPong failed: value=0.5"); }
    // value=1.0: at boundary (cyclePos=1.0 >= 1.0) → 1.0, reversing
    if (!floatEquals(result[2], 1.0f) || isReversing[2] != 1) { throw DxvkError("Loop<Float> PingPong failed: value=1.0"); }
    // value=1.5: reverse phase → 0.5, reversing
    if (!floatEquals(result[3], 0.5f) || isReversing[3] != 1) { throw DxvkError("Loop<Float> PingPong failed: value=1.5 should reverse to 0.5"); }
    // value=2.0: back at min → 0.0, not reversing (new cycle)
    if (!floatEquals(result[4], 0.0f) || isReversing[4] != 0) { throw DxvkError("Loop<Float> PingPong failed: value=2.0"); }
    // value=2.5: forward again → 0.5, not reversing
    if (!floatEquals(result[5], 0.5f) || isReversing[5] != 0) { throw DxvkError("Loop<Float> PingPong failed: value=2.5"); }
    // value=3.0: at max again → 1.0, reversing
    if (!floatEquals(result[6], 1.0f) || isReversing[6] != 1) { throw DxvkError("Loop<Float> PingPong failed: value=3.0"); }
    // value=3.5: reverse again → 0.5, reversing
    if (!floatEquals(result[7], 0.5f) || isReversing[7] != 1) { throw DxvkError("Loop<Float> PingPong failed: value=3.5"); }
    // value=-0.5: negative reverse → 0.5, reversing
    if (!floatEquals(result[8], 0.5f) || isReversing[8] != 1) { throw DxvkError("Loop<Float> PingPong failed: value=-0.5"); }
    
    testedCount++;
  }
  
  // Test Float variant - NoLoop type
  {
    std::unordered_map<std::string, RtComponentPropertyType> desiredTypes = {
      {"value", RtComponentPropertyType::Float},
      {"minRange", RtComponentPropertyType::Float},
      {"maxRange", RtComponentPropertyType::Float},
      {"loopedValue", RtComponentPropertyType::Float}
    };
    const RtComponentSpec* spec = getComponentVariant("lightspeed.trex.logic.Loop", desiredTypes);
    if (!spec) {
      throw DxvkError("Failed to find Loop<Float> component");
    }
    
    std::vector<RtComponentPropertyVector> props;
    props.push_back(std::vector<float>{-10.0f, 0.0f, 0.5f, 1.0f, 10.0f}); // value
    props.push_back(std::vector<float>{0.0f, 0.0f, 0.0f, 0.0f, 0.0f});    // minRange
    props.push_back(std::vector<float>{1.0f, 1.0f, 1.0f, 1.0f, 1.0f});    // maxRange
    props.push_back(std::vector<uint32_t>{2, 2, 2, 2, 2});                // loopingType (NoLoop = 2)
    props.push_back(std::vector<float>{0.0f, 0.0f, 0.0f, 0.0f, 0.0f});    // loopedValue
    props.push_back(std::vector<uint32_t>{0, 0, 0, 0, 0});                // isReversing
    
    std::vector<size_t> indices(props.size());
    std::iota(indices.begin(), indices.end(), 0);
    MockGraphBatch batch;
    auto comp = spec->createComponentBatch(batch, props, indices);
    comp->updateRange(nullptr, 0, 5);
    
    auto& result = std::get<std::vector<float>>(props[4]);
    auto& isReversing = std::get<std::vector<uint32_t>>(props[5]);
    // NoLoop: values are unchanged
    if (!floatEquals(result[0], -10.0f)) { throw DxvkError("Loop<Float> NoLoop failed: value=-10.0 should be unchanged"); }
    if (!floatEquals(result[1], 0.0f)) { throw DxvkError("Loop<Float> NoLoop failed: value=0.0 should be unchanged"); }
    if (!floatEquals(result[2], 0.5f)) { throw DxvkError("Loop<Float> NoLoop failed: value=0.5 should be unchanged"); }
    if (!floatEquals(result[3], 1.0f)) { throw DxvkError("Loop<Float> NoLoop failed: value=1.0 should be unchanged"); }
    if (!floatEquals(result[4], 10.0f)) { throw DxvkError("Loop<Float> NoLoop failed: value=10.0 should be unchanged"); }
    for (size_t i = 0; i < 5; i++) {
      if (isReversing[i] != 0) {
        throw DxvkError("Loop<Float> NoLoop failed: isReversing should be false");
      }
    }
    
    testedCount++;
  }
  
  // Test Float variant - Clamp type
  {
    std::unordered_map<std::string, RtComponentPropertyType> desiredTypes = {
      {"value", RtComponentPropertyType::Float},
      {"minRange", RtComponentPropertyType::Float},
      {"maxRange", RtComponentPropertyType::Float},
      {"loopedValue", RtComponentPropertyType::Float}
    };
    const RtComponentSpec* spec = getComponentVariant("lightspeed.trex.logic.Loop", desiredTypes);
    if (!spec) {
      throw DxvkError("Failed to find Loop<Float> component");
    }
    
    std::vector<RtComponentPropertyVector> props;
    props.push_back(std::vector<float>{-10.0f, 0.0f, 0.5f, 1.0f, 10.0f}); // value
    props.push_back(std::vector<float>{0.0f, 0.0f, 0.0f, 0.0f, 0.0f});    // minRange
    props.push_back(std::vector<float>{1.0f, 1.0f, 1.0f, 1.0f, 1.0f});    // maxRange
    props.push_back(std::vector<uint32_t>{3, 3, 3, 3, 3});                // loopingType (Clamp = 3)
    props.push_back(std::vector<float>{0.0f, 0.0f, 0.0f, 0.0f, 0.0f});    // loopedValue
    props.push_back(std::vector<uint32_t>{0, 0, 0, 0, 0});                // isReversing
    
    std::vector<size_t> indices(props.size());
    std::iota(indices.begin(), indices.end(), 0);
    MockGraphBatch batch;
    auto comp = spec->createComponentBatch(batch, props, indices);
    comp->updateRange(nullptr, 0, 5);
    
    auto& result = std::get<std::vector<float>>(props[4]);
    auto& isReversing = std::get<std::vector<uint32_t>>(props[5]);
    // Clamp: values clamped to [0,1]
    if (!floatEquals(result[0], 0.0f)) { throw DxvkError("Loop<Float> Clamp failed: value=-10.0 should clamp to 0.0"); }
    if (!floatEquals(result[1], 0.0f)) { throw DxvkError("Loop<Float> Clamp failed: value=0.0 at min"); }
    if (!floatEquals(result[2], 0.5f)) { throw DxvkError("Loop<Float> Clamp failed: value=0.5 in range"); }
    if (!floatEquals(result[3], 1.0f)) { throw DxvkError("Loop<Float> Clamp failed: value=1.0 at max"); }
    if (!floatEquals(result[4], 1.0f)) { throw DxvkError("Loop<Float> Clamp failed: value=10.0 should clamp to 1.0"); }
    for (size_t i = 0; i < 5; i++) {
      if (isReversing[i] != 0) {
        throw DxvkError("Loop<Float> Clamp failed: isReversing should be false");
      }
    }
    
    testedCount++;
  }
  
  if (testedCount != static_cast<int>(variants.size())) {
    Logger::warn(str::format("Loop variant count mismatch: expected ", variants.size(), ", tested ", testedCount));
  }
  
  Logger::info(str::format("Loop component passed - all ", testedCount, " variants tested (Float with all looping types, Float2, Float3, Float4)"));
}

void testCountToggles() {
  
  const RtComponentSpec* spec = getComponentSpec("lightspeed.trex.logic.CountToggles");
  if (!spec) {
    throw DxvkError("Failed to find CountToggles component");
  }
  
  // Test instances:
  // [0] Basic counting - no reset
  // [1] Counting with reset at 3
  // [2] No toggle - stays false
  // [3] No toggle - stays true
  std::vector<RtComponentPropertyVector> props;
  props.push_back(std::vector<uint32_t>{0, 0, 0, 0});        // value (initial)
  props.push_back(std::vector<float>{0.0f, 3.0f, 0.0f, 0.0f}); // resetValue (instance 1 resets at 3)
  props.push_back(std::vector<uint32_t>{0, 0, 0, 0});        // prevFrameValue (state)
  props.push_back(std::vector<float>{0.0f, 0.0f, 0.0f, 0.0f}); // count (output)
  
  std::vector<size_t> indices = {0, 1, 2, 3};
  MockGraphBatch batch;
  auto comp = spec->createComponentBatch(batch, props, indices);
  
  auto& value = std::get<std::vector<uint32_t>>(props[0]);
  auto& count = std::get<std::vector<float>>(props[3]);
  
  // Initial state: all values false, count=0
  comp->updateRange(nullptr, 0, 4);
  if (!floatEquals(count[0], 0.0f)) { throw DxvkError("CountToggles failed: initial count should be 0"); }
  if (!floatEquals(count[1], 0.0f)) { throw DxvkError("CountToggles failed: initial count should be 0"); }
  
  // Update 1: Toggle instances 0 and 1 to true (rising edge)
  value[0] = 1;
  value[1] = 1;
  comp->updateRange(nullptr, 0, 4);
  if (!floatEquals(count[0], 1.0f)) { throw DxvkError("CountToggles failed: rising edge should increment"); }
  if (!floatEquals(count[1], 1.0f)) { throw DxvkError("CountToggles failed: rising edge should increment"); }
  if (!floatEquals(count[2], 0.0f)) { throw DxvkError("CountToggles failed: no toggle should not increment"); }
  if (!floatEquals(count[3], 0.0f)) { throw DxvkError("CountToggles failed: no toggle should not increment"); }
  
  // Update 2: Toggle instances 0 and 1 back to false (falling edge - no count)
  value[0] = 0;
  value[1] = 0;
  value[3] = 1; // Instance 3 rising edge
  comp->updateRange(nullptr, 0, 4);
  if (!floatEquals(count[0], 1.0f)) { throw DxvkError("CountToggles failed: falling edge should not increment"); }
  if (!floatEquals(count[1], 1.0f)) { throw DxvkError("CountToggles failed: falling edge should not increment"); }
  if (!floatEquals(count[2], 0.0f)) { throw DxvkError("CountToggles failed: no toggle"); }
  if (!floatEquals(count[3], 1.0f)) { throw DxvkError("CountToggles failed: rising edge should increment"); }
  
  // Update 3: Toggle instances 0 and 1 back to true (second rising edge)
  value[0] = 1;
  value[1] = 1;
  value[3] = 1; // Stay true (no edge)
  comp->updateRange(nullptr, 0, 4);
  if (!floatEquals(count[0], 2.0f)) { throw DxvkError("CountToggles failed: second rising edge"); }
  if (!floatEquals(count[1], 2.0f)) { throw DxvkError("CountToggles failed: second rising edge"); }
  if (!floatEquals(count[3], 1.0f)) { throw DxvkError("CountToggles failed: staying true should not increment"); }
  
  // Update 4: Toggle back to false
  value[0] = 0;
  value[1] = 0;
  comp->updateRange(nullptr, 0, 4);
  if (!floatEquals(count[0], 2.0f)) { throw DxvkError("CountToggles failed: count unchanged on falling edge"); }
  if (!floatEquals(count[1], 2.0f)) { throw DxvkError("CountToggles failed: count unchanged on falling edge"); }
  
  // Update 5: Third rising edge - instance 1 should reset at 3
  value[0] = 1;
  value[1] = 1;
  comp->updateRange(nullptr, 0, 4);
  if (!floatEquals(count[0], 3.0f)) { throw DxvkError("CountToggles failed: third rising edge"); }
  if (!floatEquals(count[1], 0.0f)) { throw DxvkError("CountToggles failed: should reset at 3"); }
  
  // Update 6: Toggle back to false
  value[0] = 0;
  value[1] = 0;
  comp->updateRange(nullptr, 0, 4);
  
  // Update 7: Fourth rising edge for instance 0, first rising edge again for instance 1 (after reset)
  value[0] = 1;
  value[1] = 1;
  comp->updateRange(nullptr, 0, 4);
  if (!floatEquals(count[0], 4.0f)) { throw DxvkError("CountToggles failed: fourth rising edge"); }
  if (!floatEquals(count[1], 1.0f)) { throw DxvkError("CountToggles failed: counting should continue after reset"); }
  
  Logger::info("CountToggles component passed (tested counting, reset, rising edges only)");
}

//=============================================================================
// TIME-BASED COMPONENTS
//=============================================================================

void testSmooth() {
  
  // Initialize deterministic time source for testing (60 FPS = 0.01666... seconds per frame)
  GlobalTime::get().init(1.0f / 60.0f);
  
  XXH64_hash_t baseHash = XXH3_64bits("lightspeed.trex.logic.Smooth", strlen("lightspeed.trex.logic.Smooth"));
  const ComponentSpecVariantMap& variants = getAllComponentSpecVariants(baseHash);
  
  int testedCount = 0;
  
  // Test Float variant
  {
    std::unordered_map<std::string, RtComponentPropertyType> desiredTypes = {
      {"input", RtComponentPropertyType::Float},
      {"output", RtComponentPropertyType::Float}
    };
    const RtComponentSpec* spec = getComponentVariant("lightspeed.trex.logic.Smooth", desiredTypes);
    if (!spec) {
      throw DxvkError("Failed to find Smooth<Float> component");
    }
    
    std::vector<RtComponentPropertyVector> props;
    props.push_back(std::vector<float>{0.0f});           // input
    props.push_back(std::vector<float>{10.0f});          // smoothingFactor
    props.push_back(std::vector<uint32_t>{0});           // initialized (state)
    props.push_back(std::vector<float>{0.0f});           // output
    
    std::vector<size_t> indices = {0, 1, 2, 3};
    MockGraphBatch batch;
    auto comp = spec->createComponentBatch(batch, props, indices);
    
    // First frame - should initialize to input value
    GlobalTime::get().update();
    comp->updateRange(nullptr, 0, 1);
    auto& output = std::get<std::vector<float>>(props[3]);
    if (!floatEquals(output[0], 0.0f)) {
      throw DxvkError("Smooth<Float> failed on initialization");
    }
    
    // Change input and advance a few frames - output should smooth towards input
    auto& input = std::get<std::vector<float>>(props[0]);
    input[0] = 100.0f;
    
    GlobalTime::get().update();
    comp->updateRange(nullptr, 0, 1);
    float afterOneFrame = output[0];
    
    // After one frame with smoothing factor 10, output should be between 0 and 100
    if (afterOneFrame <= 0.0f || afterOneFrame >= 100.0f) {
      throw DxvkError("Smooth<Float> failed - output not smoothing correctly");
    }
    
    // After many frames, should approach input value
    for (int frame = 0; frame < 100; ++frame) {
      GlobalTime::get().update();
      comp->updateRange(nullptr, 0, 1);
    }
    
    // Should be very close to 100 now
    if (!floatEquals(output[0], 100.0f, 0.1f)) {
      throw DxvkError(str::format("Smooth<Float> failed - expected ~100, got ", output[0]));
    }
    
    testedCount++;
  }
  
  // Test Float2 variant
  {
    std::unordered_map<std::string, RtComponentPropertyType> desiredTypes = {
      {"input", RtComponentPropertyType::Float2},
      {"output", RtComponentPropertyType::Float2}
    };
    const RtComponentSpec* spec = getComponentVariant("lightspeed.trex.logic.Smooth", desiredTypes);
    if (!spec) {
      throw DxvkError("Failed to find Smooth<Float2> component");
    }
    
    std::vector<RtComponentPropertyVector> props;
    props.push_back(std::vector<Vector2>{Vector2(0.0f, 0.0f)});  // input
    props.push_back(std::vector<float>{100.0f});                  // smoothingFactor
    props.push_back(std::vector<uint32_t>{0});                    // initialized
    props.push_back(std::vector<Vector2>{Vector2()});             // output
    
    std::vector<size_t> indices = {0, 1, 2, 3};
    MockGraphBatch batch;
    auto comp = spec->createComponentBatch(batch, props, indices);
    
    GlobalTime::get().update();
    comp->updateRange(nullptr, 0, 1);
    
    auto& input = std::get<std::vector<Vector2>>(props[0]);
    input[0] = Vector2(10.0f, 20.0f);
    
    for (int frame = 0; frame < 10; ++frame) {
      GlobalTime::get().update();
      comp->updateRange(nullptr, 0, 1);
    }
    
    auto& output = std::get<std::vector<Vector2>>(props[3]);
    if (!vectorEquals(output[0], Vector2(10.0f, 20.0f), 0.5f)) {
      throw DxvkError("Smooth<Float2> failed");
    }
    
    testedCount++;
  }
  
  // Test Float3 variant
  {
    std::unordered_map<std::string, RtComponentPropertyType> desiredTypes = {
      {"input", RtComponentPropertyType::Float3},
      {"output", RtComponentPropertyType::Float3}
    };
    const RtComponentSpec* spec = getComponentVariant("lightspeed.trex.logic.Smooth", desiredTypes);
    if (!spec) {
      throw DxvkError("Failed to find Smooth<Float3> component");
    }
    
    std::vector<RtComponentPropertyVector> props;
    props.push_back(std::vector<Vector3>{Vector3(0.0f, 0.0f, 0.0f)});  // input
    props.push_back(std::vector<float>{100.0f});                        // smoothingFactor (high = fast)
    props.push_back(std::vector<uint32_t>{0});                          // initialized
    props.push_back(std::vector<Vector3>{Vector3()});                   // output
    
    std::vector<size_t> indices = {0, 1, 2, 3};
    MockGraphBatch batch;
    auto comp = spec->createComponentBatch(batch, props, indices);
    
    // Initialize
    GlobalTime::get().update();
    comp->updateRange(nullptr, 0, 1);
    
    // Change input
    auto& input = std::get<std::vector<Vector3>>(props[0]);
    input[0] = Vector3(10.0f, 20.0f, 30.0f);
    
    // Smooth for a few frames
    for (int frame = 0; frame < 10; ++frame) {
      GlobalTime::get().update();
      comp->updateRange(nullptr, 0, 1);
    }
    
    auto& output = std::get<std::vector<Vector3>>(props[3]);
    // With high smoothing factor, should be very close to target
    if (!vectorEquals(output[0], Vector3(10.0f, 20.0f, 30.0f), 0.5f)) {
      throw DxvkError("Smooth<Float3> failed");
    }
    
    testedCount++;
  }
  
  // Test Float4 variant
  {
    std::unordered_map<std::string, RtComponentPropertyType> desiredTypes = {
      {"input", RtComponentPropertyType::Float4},
      {"output", RtComponentPropertyType::Float4}
    };
    const RtComponentSpec* spec = getComponentVariant("lightspeed.trex.logic.Smooth", desiredTypes);
    if (!spec) {
      throw DxvkError("Failed to find Smooth<Float4> component");
    }
    
    std::vector<RtComponentPropertyVector> props;
    props.push_back(std::vector<Vector4>{Vector4(0.0f, 0.0f, 0.0f, 0.0f)});  // input
    props.push_back(std::vector<float>{100.0f});                               // smoothingFactor
    props.push_back(std::vector<uint32_t>{0});                                 // initialized
    props.push_back(std::vector<Vector4>{Vector4()});                          // output
    
    std::vector<size_t> indices = {0, 1, 2, 3};
    MockGraphBatch batch;
    auto comp = spec->createComponentBatch(batch, props, indices);
    
    GlobalTime::get().update();
    comp->updateRange(nullptr, 0, 1);
    
    auto& input = std::get<std::vector<Vector4>>(props[0]);
    input[0] = Vector4(10.0f, 20.0f, 30.0f, 40.0f);
    
    for (int frame = 0; frame < 10; ++frame) {
      GlobalTime::get().update();
      comp->updateRange(nullptr, 0, 1);
    }
    
    auto& output = std::get<std::vector<Vector4>>(props[3]);
    if (!vectorEquals(output[0], Vector4(10.0f, 20.0f, 30.0f, 40.0f), 0.5f)) {
      throw DxvkError("Smooth<Float4> failed");
    }
    
    testedCount++;
  }
  
  if (testedCount != variants.size()) {
    throw DxvkError(str::format("Smooth variant count mismatch: expected ", variants.size(), ", tested ", testedCount));
  }
  
  Logger::info(str::format("Smooth component passed - all ", variants.size(), " variants tested"));
}

void testVelocity() {
  
  // Initialize deterministic time source for testing (60 FPS)
  GlobalTime::get().init(1.0f / 60.0f);
  
  XXH64_hash_t baseHash = XXH3_64bits("lightspeed.trex.logic.Velocity", strlen("lightspeed.trex.logic.Velocity"));
  const ComponentSpecVariantMap& variants = getAllComponentSpecVariants(baseHash);
  
  int testedCount = 0;
  
  // Test Float variant
  {
    std::unordered_map<std::string, RtComponentPropertyType> desiredTypes = {
      {"input", RtComponentPropertyType::Float},
      {"previousValue", RtComponentPropertyType::Float},
      {"velocity", RtComponentPropertyType::Float}
    };
    const RtComponentSpec* spec = getComponentVariant("lightspeed.trex.logic.Velocity", desiredTypes);
    if (!spec) {
      throw DxvkError("Failed to find Velocity<Float> component");
    }
    
    std::vector<RtComponentPropertyVector> props;
    props.push_back(std::vector<float>{0.0f});    // input
    props.push_back(std::vector<float>{0.0f});    // previousValue (state)
    props.push_back(std::vector<float>{0.0f});    // velocity (output)
    
    std::vector<size_t> indices = {0, 1, 2};
    MockGraphBatch batch;
    auto comp = spec->createComponentBatch(batch, props, indices);
    
    // First frame - establish baseline
    GlobalTime::get().update();
    comp->updateRange(nullptr, 0, 1);
    
    // Change input and update - velocity should be (change / deltaTime)
    auto& input = std::get<std::vector<float>>(props[0]);
    input[0] = 10.0f;  // Changed by 10
    
    GlobalTime::get().update();
    comp->updateRange(nullptr, 0, 1);
    
    auto& velocity = std::get<std::vector<float>>(props[2]);
    // deltaTime = 1/60, change = 10, so velocity = 10 / (1/60) = 600
    float expectedVelocity = 10.0f * 60.0f;
    if (!floatEquals(velocity[0], expectedVelocity, 10.0f)) {
      throw DxvkError(str::format("Velocity<Float> failed - expected ~", expectedVelocity, ", got ", velocity[0]));
    }
    
    testedCount++;
  }
  
  // Test Float2 variant
  {
    std::unordered_map<std::string, RtComponentPropertyType> desiredTypes = {
      {"input", RtComponentPropertyType::Float2},
      {"previousValue", RtComponentPropertyType::Float2},
      {"velocity", RtComponentPropertyType::Float2}
    };
    const RtComponentSpec* spec = getComponentVariant("lightspeed.trex.logic.Velocity", desiredTypes);
    if (!spec) {
      throw DxvkError("Failed to find Velocity<Float2> component");
    }
    
    std::vector<RtComponentPropertyVector> props;
    props.push_back(std::vector<Vector2>{Vector2(0.0f, 0.0f)});  // input
    props.push_back(std::vector<Vector2>{Vector2(0.0f, 0.0f)});  // previousValue
    props.push_back(std::vector<Vector2>{Vector2()});            // velocity
    
    std::vector<size_t> indices = {0, 1, 2};
    MockGraphBatch batch;
    auto comp = spec->createComponentBatch(batch, props, indices);
    
    GlobalTime::get().update();
    comp->updateRange(nullptr, 0, 1);
    
    auto& input = std::get<std::vector<Vector2>>(props[0]);
    input[0] = Vector2(1.0f, 2.0f);
    
    GlobalTime::get().update();
    comp->updateRange(nullptr, 0, 1);
    
    auto& velocity = std::get<std::vector<Vector2>>(props[2]);
    Vector2 expectedVelocity(60.0f, 120.0f);
    if (!vectorEquals(velocity[0], expectedVelocity, 1.0f)) {
      throw DxvkError("Velocity<Float2> failed");
    }
    
    testedCount++;
  }
  
  // Test Float3 variant
  {
    std::unordered_map<std::string, RtComponentPropertyType> desiredTypes = {
      {"input", RtComponentPropertyType::Float3},
      {"previousValue", RtComponentPropertyType::Float3},
      {"velocity", RtComponentPropertyType::Float3}
    };
    const RtComponentSpec* spec = getComponentVariant("lightspeed.trex.logic.Velocity", desiredTypes);
    if (!spec) {
      throw DxvkError("Failed to find Velocity<Float3> component");
    }
    
    std::vector<RtComponentPropertyVector> props;
    props.push_back(std::vector<Vector3>{Vector3(0.0f, 0.0f, 0.0f)});  // input
    props.push_back(std::vector<Vector3>{Vector3(0.0f, 0.0f, 0.0f)});  // previousValue
    props.push_back(std::vector<Vector3>{Vector3()});                   // velocity
    
    std::vector<size_t> indices = {0, 1, 2};
    MockGraphBatch batch;
    auto comp = spec->createComponentBatch(batch, props, indices);
    
    // First frame
    GlobalTime::get().update();
    comp->updateRange(nullptr, 0, 1);
    
    // Move to new position
    auto& input = std::get<std::vector<Vector3>>(props[0]);
    input[0] = Vector3(1.0f, 2.0f, 3.0f);
    
    GlobalTime::get().update();
    comp->updateRange(nullptr, 0, 1);
    
    auto& velocity = std::get<std::vector<Vector3>>(props[2]);
    // Velocity = change * 60 (since deltaTime = 1/60)
    Vector3 expectedVelocity(60.0f, 120.0f, 180.0f);
    if (!vectorEquals(velocity[0], expectedVelocity, 1.0f)) {
      throw DxvkError("Velocity<Float3> failed");
    }
    
    testedCount++;
  }
  
  // Test Float4 variant
  {
    std::unordered_map<std::string, RtComponentPropertyType> desiredTypes = {
      {"input", RtComponentPropertyType::Float4},
      {"previousValue", RtComponentPropertyType::Float4},
      {"velocity", RtComponentPropertyType::Float4}
    };
    const RtComponentSpec* spec = getComponentVariant("lightspeed.trex.logic.Velocity", desiredTypes);
    if (!spec) {
      throw DxvkError("Failed to find Velocity<Float4> component");
    }
    
    std::vector<RtComponentPropertyVector> props;
    props.push_back(std::vector<Vector4>{Vector4(0.0f, 0.0f, 0.0f, 0.0f)});  // input
    props.push_back(std::vector<Vector4>{Vector4(0.0f, 0.0f, 0.0f, 0.0f)});  // previousValue
    props.push_back(std::vector<Vector4>{Vector4()});                         // velocity
    
    std::vector<size_t> indices = {0, 1, 2};
    MockGraphBatch batch;
    auto comp = spec->createComponentBatch(batch, props, indices);
    
    GlobalTime::get().update();
    comp->updateRange(nullptr, 0, 1);
    
    auto& input = std::get<std::vector<Vector4>>(props[0]);
    input[0] = Vector4(1.0f, 2.0f, 3.0f, 4.0f);
    
    GlobalTime::get().update();
    comp->updateRange(nullptr, 0, 1);
    
    auto& velocity = std::get<std::vector<Vector4>>(props[2]);
    Vector4 expectedVelocity(60.0f, 120.0f, 180.0f, 240.0f);
    if (!vectorEquals(velocity[0], expectedVelocity, 1.0f)) {
      throw DxvkError("Velocity<Float4> failed");
    }
    
    testedCount++;
  }
  
  if (testedCount != variants.size()) {
    throw DxvkError(str::format("Velocity variant count mismatch: expected ", variants.size(), ", tested ", testedCount));
  }
  
  Logger::info(str::format("Velocity component passed - all ", variants.size(), " variants tested"));
}

//=============================================================================
// TEST RUNNER
//=============================================================================

void runAllTests() {
  Logger::info("===========================================");
  Logger::info("Starting Transform Components Unit Tests");
  Logger::info("===========================================");
  
  // Arithmetic
  testAdd();
  testSubtract();
  testMultiply();
  testDivide();
  testClamp();
  testMin();
  testMax();
  testFloor();
  testCeil();
  testRound();
  testInvert();
  
  // Comparison
  testEqualTo();
  testLessThan();
  testGreaterThan();
  testBetween();
  
  // Boolean
  testBoolAnd();
  testBoolOr();
  testBoolNot();
  
  // Vector
  testComposeVector2();
  testComposeVector3();
  testComposeVector4();
  testDecomposeVector2();
  testDecomposeVector3();
  testDecomposeVector4();
  testVectorLength();
  testNormalize();
  
  // Logic/State
  testToggle();
  testSelect();
  testCounter();
  testConditionallyStore();
  testPreviousFrameValue();
  testRemap();
  testLoop();
  testCountToggles();
  
  
  // Time-based
  testSmooth();
  testVelocity();
  
  Logger::info("===========================================");
  Logger::info("All Transform Component Tests Passed!");
  Logger::info("===========================================");
}

} // namespace test
} // namespace dxvk

int main() {
  try {
    dxvk::test::runAllTests();
  } catch (const dxvk::DxvkError& e) {
    std::cerr << "Test failed: " << e.message() << std::endl;
    dxvk::Logger::err(e.message());
    return -1;
  }
  
  return 0;
}

