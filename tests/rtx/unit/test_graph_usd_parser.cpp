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

#include <memory>
#include <vector>
#include <unordered_map>
#include <iostream>
#include <fstream>

#include "../../test_utils.h"
#include "rtx_render/graph/rtx_graph_usd_parser.h"
#include "rtx_render/graph/rtx_graph_types.h"
#include "rtx_render/rtx_asset_replacer.h"
#include "rtx_render/rtx_mod_usd.h"
#include "../../../src/util/util_string.h"
#include "../../../src/util/log/log.h"
#include "../../../src/util/util_error.h"

// Include the test component
#include "graph/test_component.h"

// USD includes for testing
#include "../../../src/lssusd/usd_include_begin.h"
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usd/attribute.h>
#include <pxr/usd/sdf/path.h>
#include <pxr/usd/sdf/types.h>
#include <pxr/usd/usdGeom/mesh.h>
#include <pxr/usd/usdLux/lightAPI.h>
#include <pxr/usd/usdGeom/sphere.h>
#include <pxr/usd/usdLux/sphereLight.h>
#include "../../../src/lssusd/usd_include_end.h"

namespace dxvk {

// Note: Logger needed by some shared code used in this Unit Test.
Logger Logger::s_instance("test_graph_usd_parser.log");

// Test fixture class for GraphUsdParser tests
class GraphUsdParserTest {
public:
  GraphUsdParserTest() {
    // Create a temporary USD stage for testing
    m_stage = pxr::UsdStage::CreateInMemory("test_graph.usda");
    if (!m_stage) {
      throw DxvkError("Failed to create USD stage for testing");
    }
  }

  ~GraphUsdParserTest() = default;

  // Helper method to create a simple test graph with TestComponent
  pxr::UsdPrim createTestGraph() {
    pxr::SdfPath graphPath("/World/testGraph");
    pxr::UsdPrim graphPrim = m_stage->DefinePrim(graphPath, pxr::TfToken("OmniGraph"));
    
    // Create a test node
    pxr::SdfPath nodePath = graphPath.AppendChild(pxr::TfToken("testNode"));
    pxr::UsdPrim nodePrim = m_stage->DefinePrim(nodePath, pxr::TfToken("OmniGraphNode"));
    
    // Add node:type attribute
    pxr::UsdAttribute typeAttr = nodePrim.CreateAttribute(pxr::TfToken("node:type"), pxr::SdfValueTypeNames->Token);
    typeAttr.Set(pxr::TfToken("lightspeed.trex.components.TestComponent"));
    
    // Add node:typeVersion attribute
    pxr::UsdAttribute versionAttr = nodePrim.CreateAttribute(pxr::TfToken("node:typeVersion"), pxr::SdfValueTypeNames->Int);
    versionAttr.Set(1);
    
    return graphPrim;
  }

  // Helper method to create a graph based on example_graph.usda
  pxr::UsdPrim createExampleGraph() {
    // Create World prim
    pxr::SdfPath worldPath("/World");
    pxr::UsdPrim worldPrim = m_stage->DefinePrim(worldPath, pxr::TfToken("Xform"));
    
    // Create testGraph
    pxr::SdfPath graphPath = worldPath.AppendChild(pxr::TfToken("testGraph"));
    pxr::UsdPrim graphPrim = m_stage->DefinePrim(graphPath, pxr::TfToken("OmniGraph"));
    
    return graphPrim;
  }

  // Helper method to create a TestComponent node
  pxr::UsdPrim createTestAllTypesNode(const pxr::SdfPath& parentPath, const std::string& nodeName) {
    pxr::SdfPath nodePath = parentPath.AppendChild(pxr::TfToken(nodeName));
    pxr::UsdPrim nodePrim = m_stage->DefinePrim(nodePath, pxr::TfToken("OmniGraphNode"));
    
    // Add required attributes
    pxr::UsdAttribute typeAttr = nodePrim.CreateAttribute(pxr::TfToken("node:type"), pxr::SdfValueTypeNames->Token);
    typeAttr.Set(pxr::TfToken("lightspeed.trex.components.TestComponent"));
    
    pxr::UsdAttribute versionAttr = nodePrim.CreateAttribute(pxr::TfToken("node:typeVersion"), pxr::SdfValueTypeNames->Int);
    versionAttr.Set(1);
    
    return nodePrim;
  }

  // Helper method to add input properties to a node
  void addInputProperty(pxr::UsdPrim& nodePrim, const std::string& propertyName, const std::string& value) {
    pxr::UsdAttribute attr = nodePrim.CreateAttribute(pxr::TfToken("inputs:" + propertyName), pxr::SdfValueTypeNames->Token);
    attr.Set(pxr::TfToken(value));
  }

  // Helper method to add enum input properties to a node
  void addEnumInputProperty(pxr::UsdPrim& nodePrim, const std::string& propertyName, const std::string& value, const std::vector<std::string>& allowedTokens) {
    pxr::UsdAttribute attr = nodePrim.CreateAttribute(pxr::TfToken("inputs:" + propertyName), pxr::SdfValueTypeNames->Token);
    attr.Set(pxr::TfToken(value));
    
    // Add allowedTokens metadata
    pxr::VtArray<pxr::TfToken> tokens;
    for (const auto& token : allowedTokens) {
      tokens.push_back(pxr::TfToken(token));
    }
    attr.SetMetadata(pxr::TfToken("allowedTokens"), pxr::VtValue(tokens));
  }

  // Helper method to add output properties to a node
  void addOutputProperty(pxr::UsdPrim& nodePrim, const std::string& propertyName) {
    pxr::UsdAttribute attr = nodePrim.CreateAttribute(pxr::TfToken("outputs:" + propertyName), pxr::SdfValueTypeNames->Token);
  }

  // Helper method to connect two nodes
  void connectNodes(pxr::UsdPrim& sourceNode, const std::string& sourceOutput, 
                   pxr::UsdPrim& targetNode, const std::string& targetInput) {
    pxr::SdfPath sourcePath = sourceNode.GetPath().AppendProperty(pxr::TfToken("outputs:" + sourceOutput));
    pxr::SdfPath targetPath = targetNode.GetPath().AppendProperty(pxr::TfToken("inputs:" + targetInput));
    
    pxr::UsdAttribute targetAttr = targetNode.GetAttribute(pxr::TfToken("inputs:" + targetInput));
    if (targetAttr.IsValid()) {
      targetAttr.AddConnection(sourcePath);
    }
  }

  // Helper method to connect relationships (for *Instance properties)
  void connectRelationships(pxr::UsdPrim& sourceNode, const std::string& sourceOutput, 
                           pxr::UsdPrim& targetNode, const std::string& targetInput,
                           pxr::UsdPrim& targetPrim) {
    pxr::SdfPath sourcePath = sourceNode.GetPath().AppendProperty(pxr::TfToken("outputs:" + sourceOutput));
    pxr::UsdRelationship sourceRel = sourceNode.CreateRelationship(pxr::TfToken("outputs:" + sourceOutput));
    sourceRel.SetTargets({targetPrim.GetPath()});
    
    pxr::UsdRelationship targetRel = targetNode.CreateRelationship(pxr::TfToken("inputs:" + targetInput));
    targetRel.SetTargets({targetPrim.GetPath(), sourcePath});
  }
  
  pxr::UsdStageRefPtr m_stage;
  AssetReplacements m_replacements;
  GraphUsdParser::PathToOffsetMap m_pathToOffsetMap;
};

// Test class that can access private methods of GraphUsdParser
class GraphUsdParserTestApp {
public:
  // Define a type alias for DAGNode to make it accessible
  using DAGNode = GraphUsdParser::DAGNode;
  
  static std::vector<DAGNode> getDAGSortedNodes(const pxr::UsdPrim& graphPrim) {
    return GraphUsdParser::getDAGSortedNodes(graphPrim);
  }

  static const RtComponentSpec* getComponentSpecForPrim(const pxr::UsdPrim& nodePrim) {
    return GraphUsdParser::getComponentSpecForPrim(nodePrim);
  }

  static size_t getPropertyIndex(RtGraphTopology& topology, const pxr::SdfPath& propertyPath, const RtComponentPropertySpec& property) {
    return GraphUsdParser::getPropertyIndex(topology, propertyPath, property);
  }

  static bool versionCheck(const pxr::UsdPrim& nodePrim, const RtComponentSpec& componentSpec) {
    return GraphUsdParser::versionCheck(nodePrim, componentSpec);
  }

  static RtComponentPropertyValue getPropertyValue(const pxr::UsdAttribute& attr, const RtComponentPropertySpec& spec, GraphUsdParser::PathToOffsetMap& pathToOffsetMap) {
    return GraphUsdParser::getPropertyValue(attr, spec, pathToOffsetMap);
  }
};

// Helper function to test if an assert fires
template<typename Func>
bool testAssertFires(Func func, const std::string& testName) {
  bool assertFired = false;
  
#ifdef _WIN32
  // Use Windows SEH to catch the assert
  __try {
    func();
    // If we get here, the assert didn't fire, which is a test failure
    Logger::err("ERROR: Assert did not fire in " + testName + "!");
    return false;
  }
  __except(EXCEPTION_EXECUTE_HANDLER) {
    // The assert fired, which is what we expect
    assertFired = true;
    Logger::info("Successfully caught assert in " + testName);
  }
#else
  // On non-Windows systems, we can't easily catch the assert
  // So we'll just call the function and let it crash if the assert fires
  Logger::err("ERROR: testing for assertions is not implemented for this operating system.");
#endif
  
  return assertFired;
}

// Test cases
void testCreateTestGraph() {
  Logger::info("Testing createTestGraph...");
  
  GraphUsdParserTest test;
  pxr::UsdPrim graphPrim = test.createTestGraph();
  
  if (!graphPrim.IsValid()) {
    throw DxvkError("testCreateTestGraph: graphPrim is not valid");
  }
  if (graphPrim.GetTypeName() != pxr::TfToken("OmniGraph")) {
    throw DxvkError("testCreateTestGraph: graphPrim type name is not OmniGraph");
  }
  
  pxr::UsdPrim nodePrim = graphPrim.GetChild(pxr::TfToken("testNode"));
  if (!nodePrim.IsValid()) {
    throw DxvkError("testCreateTestGraph: nodePrim is not valid");
  }
  
  pxr::UsdAttribute typeAttr = nodePrim.GetAttribute(pxr::TfToken("node:type"));
  if (!typeAttr.IsValid()) {
    throw DxvkError("testCreateTestGraph: typeAttr is not valid");
  }
  
  pxr::TfToken typeValue;
  typeAttr.Get(&typeValue);
  if (typeValue.GetString() != "lightspeed.trex.components.TestComponent") {
    throw DxvkError("testCreateTestGraph: typeValue is not 'remix.test.component'");
  }
  
  Logger::info("createTestGraph passed");
}

void testGetComponentSpecForPrim() {
  Logger::info("Testing getComponentSpecForPrim...");
  
  GraphUsdParserTest test;
  pxr::UsdPrim graphPrim = test.createTestGraph();
  pxr::UsdPrim nodePrim = graphPrim.GetChild(pxr::TfToken("testNode"));
  
  // Test with valid node
  const RtComponentSpec* spec = GraphUsdParserTestApp::getComponentSpecForPrim(nodePrim);
  if (spec == nullptr) {
    throw DxvkError("testGetComponentSpecForPrim: valid spec should not be nullptr");
  }
  if (spec->componentType != components::TestComponent::getStaticSpec()->componentType) {
    throw DxvkError("testGetComponentSpecForPrim: spec should be TestComponent");
  }
  
  // Test with node missing node:type attribute
  pxr::SdfPath invalidNodePath = graphPrim.GetPath().AppendChild(pxr::TfToken("invalidNode"));
  pxr::UsdPrim invalidNodePrim = test.m_stage->DefinePrim(invalidNodePath);
  
  Logger::info("Expecting 'err: Node /World/testGraph/invalidNode has no `node:type` attribute'");
  const RtComponentSpec* invalidSpec = GraphUsdParserTestApp::getComponentSpecForPrim(invalidNodePrim);
  if (invalidSpec != nullptr) {
    throw DxvkError("testGetComponentSpecForPrim: invalidSpec should be nullptr");
  }
  
  Logger::info("getComponentSpecForPrim passed");
}

void testVersionCheck() {
  Logger::info("Testing versionCheck...");
  
  GraphUsdParserTest test;
  pxr::UsdPrim graphPrim = test.createTestGraph();
  pxr::UsdPrim nodePrim = graphPrim.GetChild(pxr::TfToken("testNode"));
  
  // Get the TestComponent component spec
  const RtComponentSpec* componentSpec = components::TestComponent::getStaticSpec();
  if (componentSpec == nullptr) {
    throw DxvkError("testVersionCheck: componentSpec is nullptr");
  }
  
  // Test with matching version
  bool result = GraphUsdParserTestApp::versionCheck(nodePrim, *componentSpec);
  if (result != true) {
    throw DxvkError("testVersionCheck: result should be true for matching version");
  }
  
  // Test with non-matching version
  pxr::UsdAttribute versionAttr = nodePrim.GetAttribute(pxr::TfToken("node:typeVersion"));
  versionAttr.Set(2);
  result = GraphUsdParserTestApp::versionCheck(nodePrim, *componentSpec);
  if (result != false) {
    throw DxvkError("testVersionCheck: result should be false for non-matching version");
  }
  
  // Test with node missing version attribute
  pxr::SdfPath noVersionNodePath = graphPrim.GetPath().AppendChild(pxr::TfToken("noVersionNode"));
  pxr::UsdPrim noVersionNodePrim = test.m_stage->DefinePrim(noVersionNodePath);
  pxr::UsdAttribute typeAttr = noVersionNodePrim.CreateAttribute(pxr::TfToken("node:type"), pxr::SdfValueTypeNames->Token);
  typeAttr.Set(pxr::TfToken("lightspeed.trex.components.TestComponent"));
  
  Logger::info("Expecting 'err:   Node /World/testGraph/noVersionNode is missing a `node:typeVersion` attribute.'");
  result = GraphUsdParserTestApp::versionCheck(noVersionNodePrim, *componentSpec);
  if (result != false) {
    throw DxvkError("testVersionCheck: result should be false for node missing version attribute");
  }
  
  Logger::info("versionCheck passed");
}

void testGetPropertyIndex() {
  Logger::info("Testing getPropertyIndex...");
  
  RtGraphTopology topology;
  pxr::SdfPath propertyPath("/test/path");
  RtComponentPropertySpec property;
  property.type = RtComponentPropertyType::Float;
  property.name = "testProperty";
  property.usdPropertyName = "testProperty";
  
  // Test creating new property index
  size_t index1 = GraphUsdParserTestApp::getPropertyIndex(topology, propertyPath, property);
  if (index1 != 0) {
    throw DxvkError("testGetPropertyIndex: index1 should be 0");
  }
  if (topology.propertyTypes.size() != 1) {
    throw DxvkError("testGetPropertyIndex: propertyTypes size should be 1");
  }
  if (topology.propertyTypes[0] != RtComponentPropertyType::Float) {
    throw DxvkError("testGetPropertyIndex: propertyTypes[0] should be Float");
  }
  if (topology.propertyPathHashToIndexMap.size() != 1) {
    throw DxvkError("testGetPropertyIndex: propertyPathHashToIndexMap size should be 1");
  }
  
  // Test getting existing property index
  size_t index2 = GraphUsdParserTestApp::getPropertyIndex(topology, propertyPath, property);
  if (index2 != 0) {
    throw DxvkError("testGetPropertyIndex: index2 should be 0");
  }
  if (index1 != index2) {
    throw DxvkError("testGetPropertyIndex: index1 should equal index2");
  }
  if (topology.propertyTypes.size() != 1) { // Should not add duplicate
    throw DxvkError("testGetPropertyIndex: propertyTypes size should still be 1");
  }
  
  // Test with different property path
  pxr::SdfPath propertyPath2("/test/path2");
  size_t index3 = GraphUsdParserTestApp::getPropertyIndex(topology, propertyPath2, property);
  if (index3 != 1) {
    throw DxvkError("testGetPropertyIndex: index3 should be 1");
  }
  if (topology.propertyTypes.size() != 2) {
    throw DxvkError("testGetPropertyIndex: propertyTypes size should be 2");
  }
  
  Logger::info("getPropertyIndex passed");
}

void testGetPropertyValue() {
  Logger::info("Testing getPropertyValue...");
  
  GraphUsdParserTest test;
  GraphUsdParser::PathToOffsetMap pathToOffsetMap;
  
  // Test with float property
  pxr::SdfPath nodePath("/testNode");
  pxr::UsdPrim nodePrim = test.m_stage->DefinePrim(nodePath);
  pxr::SdfPath propertyPath = nodePath.AppendProperty(pxr::TfToken("floatProperty"));
  pxr::UsdAttribute floatAttr = nodePrim.CreateAttribute(pxr::TfToken("floatProperty"), pxr::SdfValueTypeNames->Float);
  floatAttr.Set(3.14f);
  
  RtComponentPropertySpec floatSpec;
  floatSpec.type = RtComponentPropertyType::Float;
  floatSpec.defaultValue = 0.0f;
  
  RtComponentPropertyValue floatValue = GraphUsdParserTestApp::getPropertyValue(floatAttr, floatSpec, pathToOffsetMap);
  if (!std::holds_alternative<float>(floatValue)) {
    throw DxvkError("testGetPropertyValue: floatValue should hold float");
  }
  if (std::get<float>(floatValue) != 3.14f) {
    throw DxvkError("testGetPropertyValue: floatValue should be 3.14f");
  }
  
  // Test with bool property
  pxr::SdfPath boolPropertyPath = nodePath.AppendProperty(pxr::TfToken("boolProperty"));
  pxr::UsdAttribute boolAttr = nodePrim.CreateAttribute(pxr::TfToken("boolProperty"), pxr::SdfValueTypeNames->Bool);
  boolAttr.Set(true);
  
  RtComponentPropertySpec boolSpec;
  boolSpec.type = RtComponentPropertyType::Bool;
  boolSpec.defaultValue = uint8_t(0);
  
  RtComponentPropertyValue boolValue = GraphUsdParserTestApp::getPropertyValue(boolAttr, boolSpec, pathToOffsetMap);
  if (!std::holds_alternative<uint8_t>(boolValue)) {
    throw DxvkError("testGetPropertyValue: boolValue should hold uint8_t");
  }
  if (std::get<uint8_t>(boolValue) != 1) {
    throw DxvkError("testGetPropertyValue: boolValue should be 1");
  }
  
  // Test with empty attribute (should return default value)
  RtComponentPropertyValue emptyValue = GraphUsdParserTestApp::getPropertyValue(pxr::UsdAttribute(), floatSpec, pathToOffsetMap);
  if (!std::holds_alternative<float>(emptyValue)) {
    throw DxvkError("testGetPropertyValue: emptyValue should hold float");
  }
  if (std::get<float>(emptyValue) != 0.0f) {
    throw DxvkError("testGetPropertyValue: emptyValue should be 0.0f");
  }
  
  Logger::info("getPropertyValue passed");
}

void testSimpleGraph() {
  Logger::info("Testing simple graph...");
  
  GraphUsdParserTest test;
  
  // Get the TestComponent component spec (it's auto-registered)
  const RtComponentSpec* testSpec = components::TestComponent::getStaticSpec();
  if (testSpec == nullptr) {
    throw DxvkError("testSimpleGraph: testSpec is nullptr");
  }
  
  // Create a graph
  pxr::SdfPath graphPath("/World/testGraph");
  pxr::UsdPrim graphPrim = test.m_stage->DefinePrim(graphPath, pxr::TfToken("OmniGraph"));
  
  pxr::UsdPrim nodePrim = test.createTestAllTypesNode(graphPath, "testNode");
  
  // Add some input properties
  test.addInputProperty(nodePrim, "inputFloat", "2.5");
  test.addInputProperty(nodePrim, "inputBool", "1");
  test.addInputProperty(nodePrim, "inputInt32", "42");
  test.addEnumInputProperty(nodePrim, "inputUint32Enum", "One", {"One", "Two"});
  
  // Test getComponentSpecForPrim
  const RtComponentSpec* spec = GraphUsdParserTestApp::getComponentSpecForPrim(nodePrim);
  if (spec == nullptr) {
    throw DxvkError("testSimpleGraph: spec is nullptr");
  }
  if (spec->componentType != testSpec->componentType) {
    throw DxvkError("testSimpleGraph: spec componentType mismatch");
  }
  if (spec->name != "lightspeed.trex.components.TestComponent") {
    throw DxvkError("testSimpleGraph: spec name mismatch");
  }
  
  // Test getDAGSortedNodes
  std::vector<GraphUsdParserTestApp::DAGNode> nodes = GraphUsdParserTestApp::getDAGSortedNodes(graphPrim);
  if (nodes.size() != 1) {
    throw DxvkError("testSimpleGraph: nodes size should be 1");
  }
  if (nodes[0].path != nodePrim.GetPath()) {
    throw DxvkError("testSimpleGraph: nodes[0].path mismatch");
  }
  if (nodes[0].spec != testSpec) {
    throw DxvkError("testSimpleGraph: nodes[0].spec mismatch");
  }
  if (nodes[0].dependencyCount != 0) {
    throw DxvkError("testSimpleGraph: nodes[0].dependencyCount should be 0");
  }
  
  // Test parseGraph
  RtGraphState graphState = GraphUsdParser::parseGraph(test.m_replacements, graphPrim, test.m_pathToOffsetMap);
  // Should have some values from the input properties
  if (graphState.values.size() != components::TestComponent::getStaticSpec()->properties.size()) {
    throw DxvkError(str::format("testSimpleGraph: graphState.values should be size of TestComponent properties, but is ", graphState.values.size()));
  }
  
  Logger::info("testSimpleGraph passed");
}

void testPropertyValueTypes() {
  Logger::info("Testing property value types...");
  
  GraphUsdParserTest test;
  GraphUsdParser::PathToOffsetMap pathToOffsetMap;
  
  pxr::SdfPath nodePath("/testNode");
  pxr::UsdPrim nodePrim = test.m_stage->DefinePrim(nodePath);
  
  // Test Vector2 property
  pxr::SdfPath vec2PropertyPath = nodePath.AppendProperty(pxr::TfToken("vec2Property"));
  pxr::UsdAttribute vec2Attr = nodePrim.CreateAttribute(pxr::TfToken("vec2Property"), pxr::SdfValueTypeNames->Float2);
  vec2Attr.Set(pxr::GfVec2f(1.0f, 2.0f));
  
  RtComponentPropertySpec vec2Spec;
  vec2Spec.type = RtComponentPropertyType::Float2;
  vec2Spec.defaultValue = Vector2(0.0f, 0.0f);
  
  RtComponentPropertyValue vec2Value = GraphUsdParserTestApp::getPropertyValue(vec2Attr, vec2Spec, pathToOffsetMap);
  if (!std::holds_alternative<Vector2>(vec2Value)) {
    throw DxvkError("testPropertyValueTypes: vec2Value should hold Vector2");
  }
  Vector2 vec2Result = std::get<Vector2>(vec2Value);
  if (vec2Result.x != 1.0f || vec2Result.y != 2.0f) {
    throw DxvkError("testPropertyValueTypes: vec2Result values mismatch");
  }
  
  // Test Vector3 property
  pxr::SdfPath vec3PropertyPath = nodePath.AppendProperty(pxr::TfToken("vec3Property"));
  pxr::UsdAttribute vec3Attr = nodePrim.CreateAttribute(pxr::TfToken("vec3Property"), pxr::SdfValueTypeNames->Float3);
  vec3Attr.Set(pxr::GfVec3f(1.0f, 2.0f, 3.0f));
  
  RtComponentPropertySpec vec3Spec;
  vec3Spec.type = RtComponentPropertyType::Float3;
  vec3Spec.defaultValue = Vector3(0.0f, 0.0f, 0.0f);
  
  RtComponentPropertyValue vec3Value = GraphUsdParserTestApp::getPropertyValue(vec3Attr, vec3Spec, pathToOffsetMap);
  if (!std::holds_alternative<Vector3>(vec3Value)) {
    throw DxvkError("testPropertyValueTypes: vec3Value should hold Vector3");
  }
  Vector3 vec3Result = std::get<Vector3>(vec3Value);
  if (vec3Result.x != 1.0f || vec3Result.y != 2.0f || vec3Result.z != 3.0f) {
    throw DxvkError("testPropertyValueTypes: vec3Result values mismatch");
  }
  
  // Test Int32 property
  pxr::SdfPath intPropertyPath = nodePath.AppendProperty(pxr::TfToken("intProperty"));
  pxr::UsdAttribute intAttr = nodePrim.CreateAttribute(pxr::TfToken("intProperty"), pxr::SdfValueTypeNames->Int);
  intAttr.Set(42);
  
  RtComponentPropertySpec intSpec;
  intSpec.type = RtComponentPropertyType::Int32;
  intSpec.defaultValue = 0;
  
  RtComponentPropertyValue intValue = GraphUsdParserTestApp::getPropertyValue(intAttr, intSpec, pathToOffsetMap);
  if (!std::holds_alternative<int32_t>(intValue)) {
    throw DxvkError("testPropertyValueTypes: intValue should hold int32_t");
  }
  if (std::get<int32_t>(intValue) != 42) {
    throw DxvkError("testPropertyValueTypes: intValue should be 42");
  }
  
  // Test Uint32 property
  pxr::SdfPath uintPropertyPath = nodePath.AppendProperty(pxr::TfToken("uintProperty"));
  pxr::UsdAttribute uintAttr = nodePrim.CreateAttribute(pxr::TfToken("uintProperty"), pxr::SdfValueTypeNames->UInt);
  uintAttr.Set(uint32_t(123));
  
  RtComponentPropertySpec uintSpec;
  uintSpec.type = RtComponentPropertyType::Uint32;
  uintSpec.defaultValue = uint32_t(0);
  
  RtComponentPropertyValue uintValue = GraphUsdParserTestApp::getPropertyValue(uintAttr, uintSpec, pathToOffsetMap);
  if (!std::holds_alternative<uint32_t>(uintValue)) {
    throw DxvkError("testPropertyValueTypes: uintValue should hold uint32_t");
  }
  if (std::get<uint32_t>(uintValue) != 123) {
    throw DxvkError("testPropertyValueTypes: uintValue should be 123");
  }
  
  Logger::info("property value types test passed");
}

void testEmptyGraph() {
  Logger::info("Testing empty graph...");
  
  GraphUsdParserTest test;
  
  // Create an empty graph (no nodes)
  pxr::SdfPath graphPath("/emptyGraph");
  pxr::UsdPrim graphPrim = test.m_stage->DefinePrim(graphPath, pxr::TfToken("OmniGraph"));
  
  // Test DAG sorting on empty graph
  std::vector<GraphUsdParserTestApp::DAGNode> nodes = GraphUsdParserTestApp::getDAGSortedNodes(graphPrim);
  if (!nodes.empty()) {
    throw DxvkError("testEmptyGraph: nodes should be empty");
  }
  
  // Test parsing empty graph
  RtGraphState graphState = GraphUsdParser::parseGraph(test.m_replacements, graphPrim, test.m_pathToOffsetMap);
  if (!graphState.values.empty()) {
    throw DxvkError("testEmptyGraph: graphState.values should be empty");
  }
  
  Logger::info("empty graph test passed");
}

void testTwoNodeGraph() {
  Logger::info("Testing two node graph with all properties connected...");
  
  GraphUsdParserTest test;
  
  // Create a graph based on example_graph.usda
  pxr::UsdPrim graphPrim = test.createExampleGraph();
  
  // Create test prims for relationships to point to
  pxr::SdfPath worldPath = graphPrim.GetPath().GetParentPath();
  pxr::UsdPrim testPrim = test.m_stage->DefinePrim(worldPath.AppendChild(pxr::TfToken("testMesh")), pxr::TfToken("Mesh"));
  
  // Create nodes like in the example
  pxr::UsdPrim node1 = test.createTestAllTypesNode(graphPrim.GetPath(), "sourceNode");
  pxr::UsdPrim node2 = test.createTestAllTypesNode(graphPrim.GetPath(), "targetNode");
  
  // Add all input properties to source node with test values
  test.addInputProperty(node1, "inputBool", "1");
  test.addInputProperty(node1, "inputFloat", "3.14");
  test.addInputProperty(node1, "inputFloat2", "(1.0,2.0)");
  test.addInputProperty(node1, "inputFloat3", "(1.0,2.0,3.0)");
  test.addInputProperty(node1, "inputColor3", "(1.0,2.0,3.0)");
  test.addInputProperty(node1, "inputColor4", "(1.0,2.0,3.0,4.0)");
  test.addInputProperty(node1, "inputInt32", "42");
  test.addInputProperty(node1, "inputUint32", "123");
  test.addInputProperty(node1, "inputUint64", "456");
  test.addEnumInputProperty(node1, "inputUint32Enum", "One", {"One", "Two"});
  
  // Add all output properties to source node
  test.addOutputProperty(node1, "outputBool");
  test.addOutputProperty(node1, "outputFloat");
  test.addOutputProperty(node1, "outputFloat2");
  test.addOutputProperty(node1, "outputFloat3");
  test.addOutputProperty(node1, "outputColor3");
  test.addOutputProperty(node1, "outputColor4");
  test.addOutputProperty(node1, "outputInt32");
  test.addOutputProperty(node1, "outputUint32");
  test.addOutputProperty(node1, "outputUint64");
  test.addOutputProperty(node1, "outputUint32Enum");
  
  // Add all input properties to target node with different test values
  test.addInputProperty(node2, "inputBool", "0");
  test.addInputProperty(node2, "inputFloat", "2.718");
  test.addInputProperty(node2, "inputFloat2", "(5.0,6.0)");
  test.addInputProperty(node2, "inputFloat3", "(5.0,6.0,7.0)");
  test.addInputProperty(node2, "inputColor3", "(5.0,6.0,7.0)");
  test.addInputProperty(node2, "inputColor4", "(5.0,6.0,7.0,8.0)");
  test.addInputProperty(node2, "inputInt32", "99");
  test.addInputProperty(node2, "inputUint32", "789");
  test.addInputProperty(node2, "inputUint64", "101112");
  test.addEnumInputProperty(node2, "inputUint32Enum", "Two", {"One", "Two"});
  
  // Connect ALL output properties from source node to corresponding input properties in target node
  // This creates a comprehensive test of all property connections
  test.connectNodes(node1, "outputBool", node2, "inputBool");
  test.connectNodes(node1, "outputFloat", node2, "inputFloat");
  test.connectNodes(node1, "outputFloat2", node2, "inputFloat2");
  test.connectNodes(node1, "outputFloat3", node2, "inputFloat3");
  test.connectNodes(node1, "outputColor3", node2, "inputColor3");
  test.connectNodes(node1, "outputColor4", node2, "inputColor4");
  test.connectNodes(node1, "outputInt32", node2, "inputInt32");
  test.connectNodes(node1, "outputUint32", node2, "inputUint32");
  test.connectNodes(node1, "outputUint64", node2, "inputUint64");
  test.connectNodes(node1, "outputUint32Enum", node2, "inputUint32Enum");
  
  // Connect relationships (for Prim properties)
  // The first target is the prim the relationship points to, the second is the output relationship
  test.connectRelationships(node1, "outputPrim", node2, "inputPrim", testPrim);

  // Add path to offset mappings for the test prims
  test.m_pathToOffsetMap[XXH3_64bits(testPrim.GetPath().GetString().c_str(), testPrim.GetPath().GetString().size())] = 100;

  size_t numConnections = 11;
  
  // Test DAG sorting
  std::vector<GraphUsdParserTestApp::DAGNode> nodes = GraphUsdParserTestApp::getDAGSortedNodes(graphPrim);
  if (nodes.size() != 2) {
    throw DxvkError("testTwoNodeGraph: nodes size should be 2");
  }
  
  // First node should have no dependencies
  if (nodes[0].path != node1.GetPath()) {
    throw DxvkError("testTwoNodeGraph: nodes[0].path should be node1 path");
  }
  
  // Second node should depend on first
  if (nodes[1].path != node2.GetPath()) {
    throw DxvkError("testTwoNodeGraph: nodes[1].path should be node2 path");
  }
  
  // Test parsing the graph
  RtGraphState graphState = GraphUsdParser::parseGraph(test.m_replacements, graphPrim, test.m_pathToOffsetMap);
  
  // Should have values from the input properties
  if (graphState.values.empty()) {
    throw DxvkError("testTwoNodeGraph: graphState.values should not be empty");
  }
  
  // Verify we have the correct number of component specs
  if (graphState.topology.componentSpecs.size() != 2) {
    throw DxvkError("testTwoNodeGraph: graphState.topology.componentSpecs should be size 2");
  }
  if (graphState.topology.componentSpecs[0]->componentType != components::TestComponent::getStaticSpec()->componentType) {
    throw DxvkError("testTwoNodeGraph: graphState.topology.componentSpecs[0] should be TestComponent");
  }
  if (graphState.topology.componentSpecs[1]->componentType != components::TestComponent::getStaticSpec()->componentType) {
    throw DxvkError("testTwoNodeGraph: graphState.topology.componentSpecs[1] should be TestComponent");
  }

  // Verify property indices for both nodes
  if (graphState.topology.propertyIndices[0].size() != components::TestComponent::getStaticSpec()->properties.size()) {
    throw DxvkError("testTwoNodeGraph: graphState.topology.propertyIndices[0] should be size of TestComponent properties");
  }
  if (graphState.topology.propertyIndices[1].size() != components::TestComponent::getStaticSpec()->properties.size()) {
    throw DxvkError("testTwoNodeGraph: graphState.topology.propertyIndices[1] should be size of TestComponent properties");
  }
  
  // Test that connected properties share the same value index
  // This verifies that the graph parser correctly identifies shared values between connected nodes
  const RtComponentSpec* testSpec = components::TestComponent::getStaticSpec();
  
  // Build a map of property name to index for efficient lookup
  std::unordered_map<std::string, size_t> propertyNameToIndex;
  for (size_t i = 0; i < testSpec->properties.size(); i++) {
    propertyNameToIndex[testSpec->properties[i].name] = i;
  }
  
  // Verify that connected properties share the same value index
  std::vector<std::pair<std::string, std::string>> connectedProperties = {
    {"outputBool", "inputBool"},
    {"outputFloat", "inputFloat"},
    {"outputFloat2", "inputFloat2"},
    {"outputFloat3", "inputFloat3"},
    {"outputColor3", "inputColor3"},
    {"outputColor4", "inputColor4"},
    {"outputInt32", "inputInt32"},
    {"outputUint32", "inputUint32"},
    {"outputUint64", "inputUint64"},
    {"outputPrim", "inputPrim"},
    {"outputUint32Enum", "inputUint32Enum"}
  };
  
  for (const auto& [outputProp, inputProp] : connectedProperties) {
    size_t outputIndex = propertyNameToIndex[outputProp];
    size_t inputIndex = propertyNameToIndex[inputProp];
    
    if (graphState.topology.propertyIndices[0][outputIndex] != graphState.topology.propertyIndices[1][inputIndex]) {
      throw DxvkError(str::format("testTwoNodeGraph: ", outputProp, " and ", inputProp, " should share value index, but got ", 
                                  graphState.topology.propertyIndices[0][outputIndex], " and ", 
                                  graphState.topology.propertyIndices[1][inputIndex]));
    }
  }
  
  // Verify the total number of values
  // We should have 33 properties per node, but 10 of them are shared (the connected ones)
  // So total = 33 + 33 - 10 = 56 values
  size_t expectedValues = components::TestComponent::getStaticSpec()->properties.size() * 2 - numConnections;
  if (graphState.values.size() != expectedValues) {
    throw DxvkError(str::format("testTwoNodeGraph: graphState.values should be size ", expectedValues, 
                                " (33 properties per node - ", numConnections, " shared connections), but is ", graphState.values.size()));
  }
  
  Logger::info("two node graph with all properties connected test passed");
}

void testAllPropertyTypesAsStrings() {
  Logger::info("Testing all property types as strings...");
  
  GraphUsdParserTest test;
  
  // Get the TestComponent component spec
  const RtComponentSpec* testSpec = components::TestComponent::getStaticSpec();
  if (testSpec == nullptr) {
    throw DxvkError("testAllPropertyTypesAsStrings: testSpec is nullptr");
  }
  
  // Create a graph with all property types
  pxr::SdfPath graphPath("/World/testGraph");
  pxr::UsdPrim graphPrim = test.m_stage->DefinePrim(graphPath, pxr::TfToken("OmniGraph"));
  
  pxr::UsdPrim nodePrim = test.createTestAllTypesNode(graphPath, "allTypesNode");
  
  // Add all input properties
  test.addInputProperty(nodePrim, "inputBool", "1");
  test.addInputProperty(nodePrim, "inputFloat", "1.5");
  test.addInputProperty(nodePrim, "inputFloat2", "(1.0,2.0)");
  test.addInputProperty(nodePrim, "inputFloat3", "(1.0,2.0,3.0)");
  test.addInputProperty(nodePrim, "inputColor3", "(1.0,2.0,3.0)");
  test.addInputProperty(nodePrim, "inputColor4", "(1.0,2.0,3.0,4.0)");
  test.addInputProperty(nodePrim, "inputInt32", "42");
  test.addInputProperty(nodePrim, "inputUint32", "123");
  test.addInputProperty(nodePrim, "inputUint64", "456");
  test.addEnumInputProperty(nodePrim, "inputUint32Enum", "One", {"One", "Two"});
  
  // Test parsing the graph
  RtGraphState graphState = GraphUsdParser::parseGraph(test.m_replacements, graphPrim, test.m_pathToOffsetMap);
  // Should have values from all the input properties
  if (graphState.values.size() != components::TestComponent::getStaticSpec()->properties.size()) {
    throw DxvkError(str::format("testAllPropertyTypesAsStrings: graphState.values.size() should be ", components::TestComponent::getStaticSpec()->properties.size()));
  }

  // Note: Values order is based on the order they're listed in the component declaration.
  if (!std::holds_alternative<RtComponentPropertyTypeToCppType<RtComponentPropertyType::Bool>>(graphState.values[0])) {
    throw DxvkError(str::format("testAllPropertyTypesAsStrings: values[0] should hold Bool, instead it holds ", graphState.values[0].index()));
  }
  if (!std::holds_alternative<RtComponentPropertyTypeToCppType<RtComponentPropertyType::Float>>(graphState.values[1])) {
    throw DxvkError(str::format("testAllPropertyTypesAsStrings: values[1] should hold Float, instead it holds ", graphState.values[1].index()));
  }
  if (!std::holds_alternative<RtComponentPropertyTypeToCppType<RtComponentPropertyType::Float2>>(graphState.values[2])) {
    throw DxvkError(str::format("testAllPropertyTypesAsStrings: values[2] should hold Float2, instead it holds ", graphState.values[2].index()));
  }
  if (!std::holds_alternative<RtComponentPropertyTypeToCppType<RtComponentPropertyType::Float3>>(graphState.values[3])) {
    throw DxvkError(str::format("testAllPropertyTypesAsStrings: values[3] should hold Float3, instead it holds ", graphState.values[3].index()));
  }
  if (!std::holds_alternative<RtComponentPropertyTypeToCppType<RtComponentPropertyType::Color3>>(graphState.values[4])) {
    throw DxvkError(str::format("testAllPropertyTypesAsStrings: values[4] should hold Color3, instead it holds ", graphState.values[4].index()));
  }
  if (!std::holds_alternative<RtComponentPropertyTypeToCppType<RtComponentPropertyType::Color4>>(graphState.values[5])) {
    throw DxvkError(str::format("testAllPropertyTypesAsStrings: values[5] should hold Color4, instead it holds ", graphState.values[5].index()));
  }
  if (!std::holds_alternative<RtComponentPropertyTypeToCppType<RtComponentPropertyType::Int32>>(graphState.values[6])) {
    throw DxvkError(str::format("testAllPropertyTypesAsStrings: values[6] should hold Int32, instead it holds ", graphState.values[6].index()));
  }
  if (!std::holds_alternative<RtComponentPropertyTypeToCppType<RtComponentPropertyType::Uint32>>(graphState.values[7])) {
    throw DxvkError(str::format("testAllPropertyTypesAsStrings: values[7] should hold Uint32, instead it holds ", graphState.values[7].index()));
  }
  if (!std::holds_alternative<RtComponentPropertyTypeToCppType<RtComponentPropertyType::Uint64>>(graphState.values[8])) {
    throw DxvkError(str::format("testAllPropertyTypesAsStrings: values[8] should hold Uint64, instead it holds ", graphState.values[8].index()));
  }
  if (!std::holds_alternative<RtComponentPropertyTypeToCppType<RtComponentPropertyType::Prim>>(graphState.values[9])) {
    throw DxvkError(str::format("testAllPropertyTypesAsStrings: values[9] should hold Prim, instead it holds ", graphState.values[9].index()));
  }
  if (!std::holds_alternative<RtComponentPropertyTypeToCppType<RtComponentPropertyType::Uint32>>(graphState.values[10])) {
    throw DxvkError(str::format("testAllPropertyTypesAsStrings: values[10] should hold Uint32 (enum), instead it holds ", graphState.values[10].index()));
  }

  if (std::get<uint8_t>(graphState.values[0]) != 1) {
    throw DxvkError("testAllPropertyTypesAsStrings: values[0] should be 1");
  }
  if (std::get<float>(graphState.values[1]) != 1.5f) {
    throw DxvkError("testAllPropertyTypesAsStrings: values[1] should be 1.5f");
  }
  if (std::get<Vector2>(graphState.values[2]) != Vector2(1.0f, 2.0f)) {
    throw DxvkError("testAllPropertyTypesAsStrings: values[2] should be Vector2(1.0f, 2.0f)");
  }
  if (std::get<Vector3>(graphState.values[3]) != Vector3(1.0f, 2.0f, 3.0f)) {
    throw DxvkError("testAllPropertyTypesAsStrings: values[3] should be Vector3(1.0f, 2.0f, 3.0f)");
  }
  if (std::get<Vector3>(graphState.values[4]) != Vector3(1.0f, 2.0f, 3.0f)) {
    throw DxvkError("testAllPropertyTypesAsStrings: values[4] should be Vector3(1.0f, 2.0f, 3.0f)");
  }
  if (std::get<Vector4>(graphState.values[5]) != Vector4(1.0f, 2.0f, 3.0f, 4.0f)) {
    throw DxvkError("testAllPropertyTypesAsStrings: values[5] should be Vector4(1.0f, 2.0f, 3.0f, 4.0f)");
  }
  if (std::get<int32_t>(graphState.values[6]) != 42) {
    throw DxvkError("testAllPropertyTypesAsStrings: values[6] should be 42");
  }
  if (std::get<uint32_t>(graphState.values[7]) != 123) {
    throw DxvkError("testAllPropertyTypesAsStrings: values[7] should be 123");
  }
  if (std::get<uint64_t>(graphState.values[8]) != 456) {
    throw DxvkError("testAllPropertyTypesAsStrings: values[8] should be 456");
  }
  if (std::get<uint32_t>(graphState.values[9]) != ReplacementInstance::kInvalidReplacementIndex) {
    throw DxvkError("testAllPropertyTypesAsStrings: values[9] should be ReplacementInstance::kInvalidReplacementIndex");
  }
  if (std::get<uint32_t>(graphState.values[10]) != 1) {
    throw DxvkError("testAllPropertyTypesAsStrings: values[10] should be 1 (One)");
  }
  // relationships can't be set via string / token, so not testing the values here.

  Logger::info("all property types as strings test passed");
}

void testAllPropertyTypes() {
  Logger::info("Testing all property types...");
  
  GraphUsdParserTest test;
  
  // Get the TestComponent component spec
  const RtComponentSpec* testSpec = components::TestComponent::getStaticSpec();
  if (testSpec == nullptr) {
    throw DxvkError("testAllPropertyTypes: testSpec is nullptr");
  }
  
  // Create a graph with all property types
  pxr::SdfPath worldPath("/World");
  pxr::SdfPath graphPath(worldPath.AppendChild(pxr::TfToken("testGraph")));
  pxr::UsdPrim graphPrim = test.m_stage->DefinePrim(graphPath, pxr::TfToken("OmniGraph"));
  pxr::UsdPrim meshPrim = test.m_stage->DefinePrim(worldPath.AppendChild(pxr::TfToken("testMesh")), pxr::TfToken("Mesh"));
  
  pxr::UsdPrim nodePrim = test.createTestAllTypesNode(graphPath, "allTypesNode");
  
  GraphUsdParser::PathToOffsetMap pathToOffsetMap;
  pathToOffsetMap[XXH3_64bits(meshPrim.GetPath().GetString().c_str(), meshPrim.GetPath().GetString().size())] = 10;

  // Add all input properties
  
  pxr::UsdAttribute attr = nodePrim.CreateAttribute(pxr::TfToken("inputs:inputBool"), pxr::SdfValueTypeNames->Bool);
  attr.Set(true);

  attr = nodePrim.CreateAttribute(pxr::TfToken("inputs:inputFloat"), pxr::SdfValueTypeNames->Float);
  attr.Set(1.5f);

  attr = nodePrim.CreateAttribute(pxr::TfToken("inputs:inputFloat2"), pxr::SdfValueTypeNames->Float2);
  attr.Set(pxr::GfVec2f(1.0f, 2.0f)); 

  attr = nodePrim.CreateAttribute(pxr::TfToken("inputs:inputFloat3"), pxr::SdfValueTypeNames->Float3);
  attr.Set(pxr::GfVec3f(1.0f, 2.0f, 3.0f));

  attr = nodePrim.CreateAttribute(pxr::TfToken("inputs:inputColor3"), pxr::SdfValueTypeNames->Float3);
  attr.Set(pxr::GfVec3f(1.0f, 2.0f, 3.0f));

  attr = nodePrim.CreateAttribute(pxr::TfToken("inputs:inputColor4"), pxr::SdfValueTypeNames->Float4);
  attr.Set(pxr::GfVec4f(1.0f, 2.0f, 3.0f, 4.0f));

  attr = nodePrim.CreateAttribute(pxr::TfToken("inputs:inputInt32"), pxr::SdfValueTypeNames->Int);
  attr.Set(42);

  attr = nodePrim.CreateAttribute(pxr::TfToken("inputs:inputUint32"), pxr::SdfValueTypeNames->UInt);
  attr.Set(123u);

  attr = nodePrim.CreateAttribute(pxr::TfToken("inputs:inputUint64"), pxr::SdfValueTypeNames->UInt64);
  attr.Set(uint64_t(456));

  pxr::UsdRelationship rel = nodePrim.CreateRelationship(pxr::TfToken("inputs:inputPrim"));
  rel.SetTargets({meshPrim.GetPath()});
  
  // Test that the component spec has all the expected properties
  if (testSpec->properties.size() != components::TestComponent::getStaticSpec()->properties.size()) {
    throw DxvkError(str::format("testAllPropertyTypes: testSpec->properties.size() should be ", components::TestComponent::getStaticSpec()->properties.size(), " but was ", testSpec->properties.size()));
  }
  
  // Test parsing the graph
  RtGraphState graphState = GraphUsdParser::parseGraph(test.m_replacements, graphPrim, pathToOffsetMap);
  // Should have values from all the input properties
  if (graphState.values.empty()) {
    throw DxvkError("testAllPropertyTypes: graphState.values should not be empty");
  }
  // Note: Values order is based on the order they're listed in the component declaration.
  if (!std::holds_alternative<RtComponentPropertyTypeToCppType<RtComponentPropertyType::Bool>>(graphState.values[0])) {
    throw DxvkError("testAllPropertyTypes: values[0] should hold Bool");
  }
  if (!std::holds_alternative<RtComponentPropertyTypeToCppType<RtComponentPropertyType::Float>>(graphState.values[1])) {
    throw DxvkError("testAllPropertyTypes: values[1] should hold Float");
  }
  if (!std::holds_alternative<RtComponentPropertyTypeToCppType<RtComponentPropertyType::Float2>>(graphState.values[2])) {
    throw DxvkError("testAllPropertyTypes: values[2] should hold Float2");
  }
  if (!std::holds_alternative<RtComponentPropertyTypeToCppType<RtComponentPropertyType::Float3>>(graphState.values[3])) {
    throw DxvkError("testAllPropertyTypes: values[3] should hold Float3");
  }
  if (!std::holds_alternative<RtComponentPropertyTypeToCppType<RtComponentPropertyType::Color3>>(graphState.values[4])) {
    throw DxvkError("testAllPropertyTypes: values[4] should hold Color3");
  }
  if (!std::holds_alternative<RtComponentPropertyTypeToCppType<RtComponentPropertyType::Color4>>(graphState.values[5])) {
    throw DxvkError("testAllPropertyTypes: values[5] should hold Color4");
  }
  if (!std::holds_alternative<RtComponentPropertyTypeToCppType<RtComponentPropertyType::Int32>>(graphState.values[6])) {
    throw DxvkError("testAllPropertyTypes: values[6] should hold Int32");
  }
  if (!std::holds_alternative<RtComponentPropertyTypeToCppType<RtComponentPropertyType::Uint32>>(graphState.values[7])) {
    throw DxvkError("testAllPropertyTypes: values[7] should hold Uint32");
  }
  if (!std::holds_alternative<RtComponentPropertyTypeToCppType<RtComponentPropertyType::Uint64>>(graphState.values[8])) {
    throw DxvkError("testAllPropertyTypes: values[8] should hold Uint64");
  }
  if (!std::holds_alternative<RtComponentPropertyTypeToCppType<RtComponentPropertyType::Prim>>(graphState.values[9])) {
    throw DxvkError("testAllPropertyTypes: values[9] should hold Prim");
  }
  if (!std::holds_alternative<RtComponentPropertyTypeToCppType<RtComponentPropertyType::Uint32>>(graphState.values[10])) {
    throw DxvkError("testAllPropertyTypes: values[10] should hold Uint32 (enum)");
  }

  if (std::get<uint8_t>(graphState.values[0]) != 1) {
    throw DxvkError(str::format("testAllPropertyTypes: values[0] should be 1.  value was ", std::get<uint8_t>(graphState.values[0])));
  }
  if (std::get<float>(graphState.values[1]) != 1.5f) {
    throw DxvkError(str::format("testAllPropertyTypes: values[1] should be 1.5f.  value was ", std::get<float>(graphState.values[1])));
  }
  if (std::get<Vector2>(graphState.values[2]) != Vector2(1.0f, 2.0f)) {
    throw DxvkError(str::format("testAllPropertyTypes: values[2] should be Vector2(1.0f, 2.0f).  value was ", std::get<Vector2>(graphState.values[2])));
  }
  if (std::get<Vector3>(graphState.values[3]) != Vector3(1.0f, 2.0f, 3.0f)) {
    throw DxvkError(str::format("testAllPropertyTypes: values[3] should be Vector3(1.0f, 2.0f, 3.0f).  value was ", std::get<Vector3>(graphState.values[3])));
  }
  if (std::get<Vector3>(graphState.values[4]) != Vector3(1.0f, 2.0f, 3.0f)) {
    throw DxvkError(str::format("testAllPropertyTypes: values[4] should be Vector3(1.0f, 2.0f, 3.0f).  value was ", std::get<Vector3>(graphState.values[4])));
  }
  if (std::get<Vector4>(graphState.values[5]) != Vector4(1.0f, 2.0f, 3.0f, 4.0f)) {
    throw DxvkError(str::format("testAllPropertyTypes: values[5] should be Vector4(1.0f, 2.0f, 3.0f, 4.0f).  value was ", std::get<Vector4>(graphState.values[5])));
  }
  if (std::get<int32_t>(graphState.values[6]) != 42) {
    throw DxvkError(str::format("testAllPropertyTypes: values[6] should be 42.  value was ", std::get<int32_t>(graphState.values[6])));
  }
  if (std::get<uint32_t>(graphState.values[7]) != 123) {
    throw DxvkError(str::format("testAllPropertyTypes: values[7] should be 123.  value was ", std::get<uint32_t>(graphState.values[7])));
  }
  if (std::get<uint64_t>(graphState.values[8]) != 456) {
    throw DxvkError(str::format("testAllPropertyTypes: values[8] should be 456.  value was ", std::get<uint64_t>(graphState.values[8])));
  }
  if (std::get<uint32_t>(graphState.values[9]) != 10) {
    throw DxvkError(str::format("testAllPropertyTypes: values[9] should be 10.  value was ", std::get<uint32_t>(graphState.values[9])));
  }
  if (std::get<uint32_t>(graphState.values[10]) != 1) {
    throw DxvkError(str::format("testAllPropertyTypes: values[10] should be 1 (One).  value was ", std::get<uint32_t>(graphState.values[10])));
  }

  Logger::info("all property types test passed");
}

void testGraphWithCycle() {
  Logger::info("Testing graph with cycle...");
  
  GraphUsdParserTest test;
  
  // Get the TestComponent component spec
  const RtComponentSpec* testSpec = components::TestComponent::getStaticSpec();
  if (testSpec == nullptr) {
    throw DxvkError("testGraphWithCycle: testSpec is nullptr");
  }
  
  // Create a graph with a cycle: A -> B -> C -> A
  pxr::SdfPath graphPath("/World/cyclicGraph");
  pxr::UsdPrim graphPrim = test.m_stage->DefinePrim(graphPath, pxr::TfToken("OmniGraph"));
  
  // Create node A
  pxr::UsdPrim nodeA = test.createTestAllTypesNode(graphPath, "nodeA");
  test.addInputProperty(nodeA, "inputFloat", "1.0");
  test.addInputProperty(nodeA, "inputBool", "1");
  test.addOutputProperty(nodeA, "outputFloat");
  
  // Create node B
  pxr::UsdPrim nodeB = test.createTestAllTypesNode(graphPath, "nodeB");
  test.addInputProperty(nodeB, "inputFloat", "2.0");
  test.addInputProperty(nodeB, "inputBool", "1");
  test.addOutputProperty(nodeB, "outputFloat");
  
  // Create node C
  pxr::UsdPrim nodeC = test.createTestAllTypesNode(graphPath, "nodeC");
  test.addInputProperty(nodeC, "inputFloat", "3.0");
  test.addInputProperty(nodeC, "inputBool", "1");
  test.addOutputProperty(nodeC, "outputFloat");
  
  // Create the cycle: A -> B -> C -> A
  test.connectNodes(nodeA, "outputFloat", nodeB, "inputFloat");
  test.connectNodes(nodeB, "outputFloat", nodeC, "inputFloat");
  test.connectNodes(nodeC, "outputFloat", nodeA, "inputFloat");
  
  Logger::info("Expecting 'err:   Graph /World/cyclicGraph has a cycle...'");
  // Test that the assert fires when a cycle is detected
  // TODO: this fires an assert I want to test for, but we can't catch asserts.  need to refactor it to a throw or something.
  std::vector<GraphUsdParserTestApp::DAGNode> nodes = GraphUsdParserTestApp::getDAGSortedNodes(graphPrim);
  
}

} // namespace dxvk

int main() {
  dxvk::Logger::info("Starting test_graph_usd_parser...");
  dxvk::Logger::info("Expecting 'Coding Error: in _DefineCppType at line 969 of C:/g/122538378/USD/pxr/base/tf/type.cpp'");
  dxvk::UsdMod::loadUsdPlugins();
  
  try {
    dxvk::testCreateTestGraph();
    dxvk::testGetComponentSpecForPrim();
    dxvk::testVersionCheck();
    dxvk::testGetPropertyIndex();
    dxvk::testGetPropertyValue();
    dxvk::testEmptyGraph();
    dxvk::testSimpleGraph();
    dxvk::testTwoNodeGraph();
    dxvk::testPropertyValueTypes();
    dxvk::testAllPropertyTypesAsStrings();
    dxvk::testAllPropertyTypes();
    dxvk::testGraphWithCycle();
    
    dxvk::Logger::info("\n All tests passed successfully!");
    return 0;
  } catch (const std::exception& e) {
    dxvk::Logger::err(dxvk::str::format("Test failed with exception: ", e.what()));
    return 1;
  } catch (dxvk::DxvkError& e) {
    dxvk::Logger::err(dxvk::str::format("Test failed with exception: ", e.message()));
    return 1;
  } catch (...) {
    dxvk::Logger::err("Test failed with unknown exception");
    return 1;
  }
} 