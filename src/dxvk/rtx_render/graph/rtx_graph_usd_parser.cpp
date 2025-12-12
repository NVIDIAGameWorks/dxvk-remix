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

#include "rtx_graph_usd_parser.h"
#include "dxvk_scoped_annotation.h"
#include <algorithm>  // for std::count

#include <algorithm>

#include "../../../lssusd/usd_include_begin.h"
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usdGeom/mesh.h>
#include <pxr/usd/usdLux/lightAPI.h>
#include <pxr/usd/sdf/attributeSpec.h>
#include <pxr/usd/sdf/layer.h>
#include <pxr/usd/ar/resolver.h>
#include <pxr/usd/ar/resolvedPath.h>
#include "../../../lssusd/usd_include_end.h"

namespace dxvk {

namespace {

// Helper function to resolve asset paths relative to the attribute's authoring layer
std::string resolveAssetPath(const pxr::UsdAttribute& attr, const std::string& pathStr) {
  if (pathStr.empty()) {
    return pathStr;
  }
  
  // Get the layer where this attribute value is authored (strongest opinion)
  auto propertyStack = attr.GetPropertyStack();
  if (!propertyStack.empty()) {
    pxr::SdfLayerHandle authoringLayer = propertyStack.front()->GetLayer();
    if (authoringLayer) {
      // Use ArResolver to properly resolve the asset path relative to the authoring layer
      pxr::ArResolver& resolver = pxr::ArGetResolver();
      
      // Create an anchored identifier using the layer's path as the anchor
      std::string identifier = resolver.CreateIdentifier(
        pathStr, 
        pxr::ArResolvedPath(authoringLayer->GetRealPath())
      );
      
      // Resolve the identifier to get the final resolved path
      pxr::ArResolvedPath resolvedPath = resolver.Resolve(identifier);
      if (!resolvedPath.empty()) {
        return resolvedPath.GetPathString();
      }
    }
  }
  
  // If we couldn't resolve it, return the original path
  return pathStr;
}

// Get the last valid connection from a USD attribute.
// Some connections may be old or invalid, so we iterate in reverse to find the most recent valid one.
// A connection is considered valid if the prim it references actually exists in the USD stage.
// Returns true if a valid connection was found, false otherwise.
bool getLastValidConnection(
    const pxr::UsdAttribute& attr,
    pxr::SdfPath& outConnection) {
  if (!attr || !attr.IsValid()) {
    return false;
  }
  
  pxr::SdfPathVector connections;
  attr.GetConnections(&connections);
  
  if (connections.empty()) {
    return false;
  }
  
  pxr::UsdStageRefPtr stage = attr.GetPrim().GetStage();
  if (!stage) {
    return false;
  }
  
  // Iterate in reverse to find the last valid connection
  for (auto it = connections.rbegin(); it != connections.rend(); ++it) {
    // Check if the connected prim actually exists in the stage
    pxr::UsdPrim connectedPrim = stage->GetPrimAtPath(it->GetPrimPath());
    if (connectedPrim && connectedPrim.IsValid()) {
      outConnection = *it;
      return true;
    }
  }
  
  return false;
}

} // anonymous namespace

RtGraphState GraphUsdParser::parseGraph(AssetReplacements& replacements, const pxr::UsdPrim& graphPrim, PathToOffsetMap& pathToOffsetMap) {
  ScopedCpuProfileZone();
  RtGraphTopology topology;
  std::vector<RtComponentPropertyValue> initialValues;

  // Iterate over all active nodes in the graph
  std::vector<DAGNode> sortedNodes = getDAGSortedNodes(graphPrim);
  for (DAGNode& dagNode : sortedNodes) {
    const RtComponentSpec& baseComponentSpec = *dagNode.spec;
    pxr::UsdPrim child = graphPrim.GetPrimAtPath(dagNode.path);

    if (!versionCheck(child, baseComponentSpec)) {
      Logger::err(str::format("Component not loaded: ", child.GetPath().GetString(), " failed the version check."));
      continue;
    }

    topology.propertyIndices.push_back(std::vector<size_t>());
    std::vector<size_t>& propertyIndices = topology.propertyIndices.back();

    // Track resolved types for flexible input/state properties
    std::unordered_map<std::string, RtComponentPropertyType> resolvedTypes;

    // First pass: Resolve flexible input/state types only (to find the correct variant)
    for (const RtComponentPropertySpec& property : baseComponentSpec.properties) {
      if (property.ioType == RtComponentPropertyIOType::Output ||
          property.declaredType == property.type) {
        continue; // Skip outputs and non-flexible properties
      }
      
      pxr::SdfPath propertyPath = resolvePropertyPath(child, property);
      pxr::UsdAttribute attr = child.GetAttributeAtPath(propertyPath);
      
      // Start with the declared type (flexible type) not the default resolved type
      RtComponentPropertyType resolvedType = property.declaredType;
      
      pxr::SdfPath connection;
      if (getLastValidConnection(attr, connection)) {
        std::string connectionPath = connection.GetString();
        auto iter = topology.propertyPathHashToIndexMap.find(connectionPath);
        if (iter != topology.propertyPathHashToIndexMap.end()) {
          resolvedType = topology.propertyTypes[iter->second];
        }
      }
      
      // If resolvedType still equals declaredType (the flexible type), we haven't resolved it yet
      if (resolvedType == property.declaredType) {
        resolvedType = GraphUsdParser::resolveFlexibleTypeFromAttribute(attr, property, graphPrim, propertyPath);
      }
      
      resolvedTypes[property.name] = resolvedType;
    }
    
    // Find the correct variant based on resolved input types
    const RtComponentSpec* componentSpec = &baseComponentSpec;
    if (!resolvedTypes.empty()) {
      const ComponentSpecVariantMap& variants = getAllComponentSpecVariants(baseComponentSpec.componentType);
      const RtComponentSpec* matchingVariant = nullptr;
      
      for (const auto* variantSpec : variants) {
        bool allMatch = true;
        for (const auto& [propName, resolvedType] : resolvedTypes) {
          auto variantIt = variantSpec->resolvedTypes.find(propName);
          if (variantIt == variantSpec->resolvedTypes.end() || variantIt->second != resolvedType) {
            allMatch = false;
            break;
          }
        }
        
        if (allMatch) {
          matchingVariant = variantSpec;
          break;
        }
      }
      
      if (matchingVariant != nullptr) {
        componentSpec = matchingVariant;
      } else {
        Logger::warn(str::format("Could not find matching variant for component ", baseComponentSpec.name, 
                                 " with resolved input types"));
      }
    }
    
    // Second pass: Process ALL properties from the matched variant in order
    for (const RtComponentPropertySpec& property : componentSpec->properties) {
      pxr::SdfPath propertyPath = resolvePropertyPath(child, property);
      
      if (property.type == RtComponentPropertyType::Prim) {
        pxr::UsdRelationship rel = child.GetRelationshipAtPath(propertyPath);
        bool hasConnection = false;
        if (rel && rel.IsValid()) {
          pxr::SdfPathVector targets;
          rel.GetTargets(&targets);
          if (targets.size() > 1) {
            if (targets.size() != 2) {
              Logger::err("Multiple prims are not (currently) supported in Component prim target properties.");
            }
            std::string sourcePath = targets.back().GetString();
            auto iter = topology.propertyPathHashToIndexMap.find(sourcePath);
            if (iter == topology.propertyPathHashToIndexMap.end()) {
              Logger::err(str::format("Property ", propertyPath.GetString(), " has a connection to property ", sourcePath, " that has not been loaded yet.  This may be because that prim failed to load, or it may indicate an error in the topological sort."));
            } else {
              // Verify that the source property type matches the target property type
              RtComponentPropertyType sourceType = topology.propertyTypes[iter->second];
              if (sourceType != property.type) {
                Logger::err(str::format("Property ", propertyPath.GetString(), " (type ", property.type, 
                                        ") has a connection to property ", sourcePath, " (type ", sourceType, 
                                        ") with mismatched types. Connection ignored."));
              } else {
                propertyIndices.push_back(iter->second);
                hasConnection = true;
              }
            }
          }
        }
        if (!hasConnection) {
          propertyIndices.push_back(getPropertyIndex(topology, propertyPath, property));
          initialValues.push_back(getPropertyValue(rel, property, pathToOffsetMap));
        }
      } else {
        pxr::UsdAttribute attr = child.GetAttributeAtPath(propertyPath);
        bool hasConnection = false;
        pxr::SdfPath connection;
        if (getLastValidConnection(attr, connection)) {
          std::string connectionPath = connection.GetString();
          auto iter = topology.propertyPathHashToIndexMap.find(connectionPath);
          if (iter == topology.propertyPathHashToIndexMap.end()) {
            Logger::err(str::format("Property ", propertyPath.GetString(), " has a connection to property ", connectionPath, " that has not been loaded yet.  This may be because that prim failed to load, or it may indicate an error in the topological sort."));
          } else {
            // Verify that the source property type matches the target property type
            RtComponentPropertyType sourceType = topology.propertyTypes[iter->second];
            if (sourceType != property.type) {
              Logger::err(str::format("Property ", propertyPath.GetString(), " (type ", property.type, 
                                      ") has a connection to property ", connectionPath, " (type ", sourceType, 
                                      ") with mismatched types. Connection ignored."));
            } else {
              propertyIndices.push_back(iter->second);
              hasConnection = true;
            }
          }
        }
        if (!hasConnection) {
          // Use the resolved type from the matched variant (property.type is already resolved in componentSpec)
          propertyIndices.push_back(getPropertyIndex(topology, propertyPath, property));
          initialValues.push_back(getPropertyValue(attr, property, pathToOffsetMap));
        }
      }
    }
    
    topology.componentSpecs.push_back(componentSpec);
    
    // Hash the component type
    topology.graphHash = XXH3_64bits_withSeed(&componentSpec->componentType, sizeof(RtComponentType), topology.graphHash);
    
    // Hash the property indices (which properties are connected to which)
    topology.graphHash = XXH3_64bits_withSeed(&propertyIndices[0], sizeof(size_t) * propertyIndices.size(), topology.graphHash);
    
    // Hash the resolved types of each property for this component
    // This is crucial for flexible types - different type combinations must have different hashes
    std::vector<RtComponentPropertyType> propertyTypes;
    propertyTypes.reserve(propertyIndices.size());
    for (size_t propIdx : propertyIndices) {
      propertyTypes.push_back(topology.propertyTypes[propIdx]);
    }
    topology.graphHash = XXH3_64bits_withSeed(propertyTypes.data(), sizeof(RtComponentPropertyType) * propertyTypes.size(), topology.graphHash);
  }

  return { replacements.storeObject(topology.graphHash, RtGraphTopology{topology}), initialValues, graphPrim.GetPath().GetString() };
}

std::vector<GraphUsdParser::DAGNode> GraphUsdParser::getDAGSortedNodes(const pxr::UsdPrim& graphPrim) {
  ScopedCpuProfileZone();
  static const pxr::TfToken omniGraphNodeType("OmniGraphNode");
  std::unordered_map<pxr::SdfPath, size_t, pxr::SdfPath::Hash> pathToIndexMap;
  std::vector<DAGNode> nodes;
  pxr::UsdPrimSiblingRange children = graphPrim.GetFilteredChildren(pxr::UsdPrimIsActive);
  size_t numNodes = 0;
  // UsdPrimSiblingRange has no size() method, so precalculate the size to avoid reallocations.
  for (auto child : children) {
    if (child.GetTypeName() == omniGraphNodeType) {
      numNodes++;
    }
  }
  nodes.reserve(numNodes);
  pathToIndexMap.reserve(numNodes);
  // First, make a list of all the nodes in the graph:
  for (auto child : children) {
    if (child.GetTypeName() != omniGraphNodeType) {
      continue;
    }
    const RtComponentSpec* componentSpec = getComponentSpecForPrim(child);
    if (componentSpec == nullptr) {
      continue;
    }
    if (!versionCheck(child, *componentSpec)) {
      continue;
    }
    pathToIndexMap[child.GetPath()] = nodes.size();
    nodes.push_back(DAGNode { child.GetPath(), componentSpec, 0, {} });
  }

  // Check for connections between properties, and make edges based on them:
  for (size_t nodeIndex = 0; nodeIndex < nodes.size(); nodeIndex++) {
    DAGNode& node = nodes[nodeIndex];
    for (const RtComponentPropertySpec& property : node.spec->properties) {
      // Get the node prim to check for connections
      pxr::UsdPrim nodePrim = graphPrim.GetPrimAtPath(node.path);
      
      // Check for connections on the resolved property path (current name or strongest old name)
      pxr::SdfPath propertyPath = resolvePropertyPath(nodePrim, property);
      
      // Handle Prim type properties (use relationships) differently from attribute connections
      if (property.type == RtComponentPropertyType::Prim) {
        pxr::UsdRelationship rel = nodePrim.GetRelationshipAtPath(propertyPath);
        if (rel && rel.IsValid()) {
          pxr::SdfPathVector targets;
          rel.GetTargets(&targets);
          if (targets.size() > 1) {
            pxr::SdfPath sourcePath = targets.back();
            auto iter = pathToIndexMap.find(sourcePath.GetPrimPath());
            if (iter == pathToIndexMap.end()) {
              Logger::err(str::format("Node ", node.path.GetString(), " has a connection to a prim that exists but was not loaded (may have failed to load earlier in the process): ", sourcePath.GetPrimPath().GetString()));
              continue;
            }
            size_t dependentIndex = iter->second;
            if (node.dependents.find(dependentIndex) == node.dependents.end()) {
              nodes[dependentIndex].dependencyCount++;
              node.dependents.insert(dependentIndex);
            }
          }
        }
      } else {
        pxr::UsdAttribute attr = nodePrim.GetAttributeAtPath(propertyPath);
        
        pxr::SdfPath connection;
        if (getLastValidConnection(attr, connection)) {
          auto iter = pathToIndexMap.find(connection.GetPrimPath());
          if (iter == pathToIndexMap.end()) {
            Logger::err(str::format("Node ", node.path.GetString(), " has a connection to a prim that exists but was not loaded (may have failed to load earlier in the process): ", connection.GetPrimPath().GetString()));
            continue;
          }
          size_t dependentIndex = iter->second;
          // Note: multiple properties can link the same node, so we need to avoid adding duplicate edges.
          if (node.dependents.find(dependentIndex) == node.dependents.end()) {
            nodes[dependentIndex].dependencyCount++;
            node.dependents.insert(dependentIndex);
          }
        }
      }
    }
  }
  // Now, sort the nodes in topological order, with nodes that have equal dependencies sorted by type, then by path name.

  // Get the initial batch of nodes with no dependencies
  std::vector<size_t> noDependencies;
  noDependencies.reserve(nodes.size());
  for (size_t nodeIndex = 0; nodeIndex < nodes.size(); nodeIndex++) {
    if (nodes[nodeIndex].dependencyCount == 0) {
      noDependencies.push_back(nodeIndex);
    }
  }
  std::vector<size_t> sortedNodes;
  sortedNodes.reserve(nodes.size());
  size_t sortedNodeVisitedIndex = 0;
  // This loop will 
  //   identify nodes that have no dependencies,
  //   sort them by type and then prim path,
  //   add them to a list, 
  //   remove their dependencies from the remaining nodes,
  //   and repeat until all nodes are added to the list.
  while (!noDependencies.empty()) {
    // sort the nodes that have no remaining dependencies by type and then prim path
    std::sort(noDependencies.begin(), noDependencies.end(), [&nodes](const size_t a, const size_t b) {
      if (nodes[a].spec->componentType == nodes[b].spec->componentType) {
        return nodes[a].path < nodes[b].path;
      }
      // NOTE: this will mean that graphs with the same topology may get different orderings based on path names.
      // would be ideal to find a way to stably sort the nodes in some other fashion.
      return nodes[a].spec->componentType < nodes[b].spec->componentType;
    });
    // add them to the sorted list
    for (size_t nodeIndex : noDependencies) {
      sortedNodes.push_back(nodeIndex);
    }
    noDependencies.clear();

    // remove the processed nodes' dependencies from the remaining nodes, and if the node now has no dependencies, add it to the list of nodes with no dependencies
    for (; sortedNodeVisitedIndex < sortedNodes.size(); ++sortedNodeVisitedIndex) {
      size_t nodeIndex = sortedNodes[sortedNodeVisitedIndex];
      for (size_t dependentIndex : nodes[nodeIndex].dependents) {
        nodes[dependentIndex].dependencyCount--;
        if (nodes[dependentIndex].dependencyCount == 0) {
          noDependencies.push_back(dependentIndex);
        }
      }
    }
  }

  // Check that the DAG sort found all of the nodes.  Failure indicates there was a cycle of dependencies.
  if (sortedNodes.size() != nodes.size()) {
    Logger::err(str::format("Graph ", graphPrim.GetPath().GetString(), " has a cycle.  These nodes will not be loaded due to unresolvable dependencies:"));
    for (size_t nodeIndex = 0; nodeIndex < nodes.size(); nodeIndex++) {
      if (std::find(sortedNodes.begin(), sortedNodes.end(), nodeIndex) == sortedNodes.end()) {
        Logger::err(str::format("  ", nodes[nodeIndex].path.GetString()));
      }
    }
    assert(false && "Graph has a cycle.");
  }

  // Sorting was done on indices to avoid repeated copies.  Now that the sorting is done, copy the nodes into a new vector.
  std::vector<DAGNode> sortedDAGNodes;
  sortedDAGNodes.reserve(sortedNodes.size());
  for (auto it = sortedNodes.rbegin(); it != sortedNodes.rend(); ++it) {
    sortedDAGNodes.push_back(nodes[*it]);
  }
  return sortedDAGNodes;
}



const RtComponentSpec* GraphUsdParser::getComponentSpecForPrim(const pxr::UsdPrim& nodePrim) {
  const static auto kTypeToken = pxr::TfToken { "node:type" };
  pxr::UsdAttribute typeAttr = nodePrim.GetAttribute(kTypeToken);
  std::string typeName = "";
  if (typeAttr && typeAttr.IsValid()) {
    pxr::VtValue value;
    typeAttr.Get(&value);
    pxr::TfToken valueToken;
    valueToken = value.Get<pxr::TfToken>();
    typeName = valueToken.GetString();
  } else {
    Logger::err(str::format("Node ", nodePrim.GetPath().GetString(), " has no `node:type` attribute"));
    return nullptr;
  }
  if (typeName.empty()) {
    Logger::err(str::format("Node ", nodePrim.GetPath().GetString(), " has an empty `node:type` attribute"));
    return nullptr;
  }
  RtComponentType componentType = XXH3_64bits(typeName.c_str(), typeName.size());
  
  // Get any variant of this component type
  const RtComponentSpec* spec = getComponentSpec(componentType);
  
  if (spec == nullptr) {
    Logger::err(str::format("Node ", nodePrim.GetPath().GetString(), " has an unknown `node:type` attribute: ", typeName));
    return nullptr;
  }
  
  return spec;
}

// Helper function to infer type from a token string value
RtComponentPropertyType GraphUsdParser::inferTypeFromTokenString(const std::string& tokenStr, const RtComponentPropertySpec& property) {
  // Try to parse the token string to infer the type
  
  // Check for vector/array syntax like "(1.0, 2.0, 3.0)" or "[1.0, 2.0, 3.0]"
  if ((tokenStr.front() == '(' && tokenStr.back() == ')') ||
      (tokenStr.front() == '[' && tokenStr.back() == ']')) {
    // Count commas to determine dimension
    size_t commaCount = std::count(tokenStr.begin(), tokenStr.end(), ',');
    size_t dimension = commaCount + 1;
    
    if (dimension == 2) {
      return RtComponentPropertyType::Float2;
    }
    if (dimension == 3) {
      return RtComponentPropertyType::Float3;
    }
    if (dimension == 4) {
      return RtComponentPropertyType::Float4;
    }
  }
  
  // Check for hexadecimal hash values (0x prefix with up to 16 hex digits for uint64_t)
  if (tokenStr.length() >= 3 && (tokenStr[0] == '0' && (tokenStr[1] == 'x' || tokenStr[1] == 'X'))) {
    // Verify all characters after "0x" are valid hex digits
    bool isValidHex = true;
    size_t hexDigitCount = 0;
    for (size_t i = 2; i < tokenStr.length(); ++i) {
      char c = tokenStr[i];
      if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) {
        isValidHex = false;
        break;
      }
      hexDigitCount++;
    }
    
    // If it's valid hex and fits in uint64_t (up to 16 hex digits), treat as Hash
    if (isValidHex && hexDigitCount > 0 && hexDigitCount <= 16) {
      return RtComponentPropertyType::Hash;
    }
  }
  
  // Check for decimal point or exponential notation to identify floats
  if (tokenStr.find('.') != std::string::npos || 
      tokenStr.find('e') != std::string::npos || 
      tokenStr.find('E') != std::string::npos) {
    return RtComponentPropertyType::Float;
  }
  
  // Check for boolean values
  if (tokenStr == "true" || tokenStr == "false" || tokenStr == "True" || tokenStr == "False") {
    return RtComponentPropertyType::Bool;
  }
  
  // Check if it's a valid number (integer or float without decimal point)
  // These should map to Float type
  try {
    [[maybe_unused]] double value = std::stod(tokenStr);  // Try to parse as a number
    return RtComponentPropertyType::Float;
  } catch (...) {
    // Not a valid number
  }
  
  // Default to String for all other types
  return RtComponentPropertyType::String;
}

// Helper function to find a strict type requirement from connected properties
RtComponentPropertyType GraphUsdParser::inferTypeFromConnections(
    const pxr::UsdAttribute& attr,
    const RtComponentPropertySpec& property,
    const pxr::UsdPrim& graphPrim,
    const pxr::SdfPath& propertyPath) {
  
  // Check if this is an output - look for inputs that connect to it
  if (property.ioType == RtComponentPropertyIOType::Output) {
    // Scan all prims in the graph to find inputs that connect to this output
    pxr::UsdPrimSiblingRange children = graphPrim.GetFilteredChildren(pxr::UsdPrimIsActive);
    for (const pxr::UsdPrim& childPrim : children) {
      const RtComponentSpec* componentSpec = getComponentSpecForPrim(childPrim);
      if (!componentSpec) {
        continue;
      }
      
      // Check each property of this component
      for (const RtComponentPropertySpec& otherProp : componentSpec->properties) {
        if (otherProp.ioType != RtComponentPropertyIOType::Input) {
          continue;
        }
        
        // Check if this input connects to our output
        pxr::SdfPath otherPropPath = resolvePropertyPath(childPrim, otherProp);
        pxr::UsdAttribute otherAttr = childPrim.GetAttributeAtPath(otherPropPath);
        if (otherAttr && otherAttr.IsValid()) {
          pxr::SdfPathVector connections;
          otherAttr.GetConnections(&connections);
          for (const pxr::SdfPath& conn : connections) {
            if (conn == propertyPath) {
              // This input connects to our output!
              // If it has a strict type requirement (not flexible), use that
              if (otherProp.type == otherProp.declaredType) {
                return otherProp.type;
              }
            }
          }
        }
      }
    }
  }
  
  // Check if this is an input - look for the output that connects to it
  if (property.ioType == RtComponentPropertyIOType::Input) {
    pxr::SdfPath sourcePath;
    if (getLastValidConnection(attr, sourcePath)) {
      // Get the source property
      pxr::UsdPrim sourcePrim = graphPrim.GetPrimAtPath(sourcePath.GetPrimPath());
      if (sourcePrim) {
        const RtComponentSpec* sourceSpec = getComponentSpecForPrim(sourcePrim);
        if (sourceSpec) {
          // Find the property in the source component
          for (const RtComponentPropertySpec& sourceProp : sourceSpec->properties) {
            pxr::SdfPath sourcePropPath = resolvePropertyPath(sourcePrim, sourceProp);
            if (sourcePropPath == sourcePath) {
              // Found the source property!
              // If it has a strict type requirement, use that
              if (sourceProp.type == sourceProp.declaredType) {
                return sourceProp.type;
              }
            }
          }
        }
      }
    }
  }
  
  // No strict type requirement found from connections
  return RtComponentPropertyType::Float; // Return default
}

RtComponentPropertyType GraphUsdParser::resolveFlexibleTypeFromAttribute(
    const pxr::UsdAttribute& attr, 
    const RtComponentPropertySpec& property,
    const pxr::UsdPrim& graphPrim,
    const pxr::SdfPath& propertyPath) {
  // Note: OGN flexible types should be deterministically resolved purely based on input connections,
  // with unconnected inputs causing unresolved types, which shouldn't load.

  // TODO simplify this to match OGN's behavior.
  
  if (property.declaredType != RtComponentPropertyType::Any && 
      property.declaredType != RtComponentPropertyType::NumberOrVector) {
    // Not a flexible type, return as-is
    return property.type;
  }
  
  if (!attr || !attr.IsValid()) {
    // No attribute to infer from, try connections first
    RtComponentPropertyType connectedType = inferTypeFromConnections(attr, property, graphPrim, propertyPath);
    if (connectedType != RtComponentPropertyType::Float) {
      return connectedType;
    }
    
    // No connection info, return Float as default
    Logger::warn(str::format("Could not resolve flexible type for property ", property.name, ", defaulting to Float"));
    return RtComponentPropertyType::Float;
  }
  
  // Get the attribute's USD type name
  pxr::SdfValueTypeName typeName = attr.GetTypeName();
  std::string typeStr = typeName.GetAsToken().GetString();
  
  // If it's a "token" type, we need to parse the token string value
  if (typeStr == "token") {
    pxr::VtValue value;
    if (attr.Get(&value) && !value.IsEmpty()) {
      std::string tokenStr;
      
      // Token values are stored as TfToken, not string
      if (value.IsHolding<pxr::TfToken>()) {
        tokenStr = value.Get<pxr::TfToken>().GetString();
      } else if (value.IsHolding<std::string>()) {
        tokenStr = value.Get<std::string>();
      }
      
      if (!tokenStr.empty()) {
        RtComponentPropertyType inferredType = inferTypeFromTokenString(tokenStr, property);
        
        // If the token string is ambiguous (like "0"), check connections
        if ((tokenStr == "0" || tokenStr == "1") && 
            (inferredType == RtComponentPropertyType::Enum)) {
          // This could be int32, uint32, uint64, or even float - check connections
          RtComponentPropertyType connectedType = inferTypeFromConnections(attr, property, graphPrim, propertyPath);
          if (connectedType != RtComponentPropertyType::Float) {
            return connectedType;
          }
        }
        
        return inferredType;
      }
    }
    
    // If we couldn't get the token value, try to infer from connections
    RtComponentPropertyType connectedType = inferTypeFromConnections(attr, property, graphPrim, propertyPath);
    if (connectedType != RtComponentPropertyType::Float) {
      return connectedType;
    }
    
    // Default based on property type
    if (property.declaredType == RtComponentPropertyType::NumberOrVector) {
      return RtComponentPropertyType::Float3;
    }
    return RtComponentPropertyType::Float;
  }
  
  // Map USD type strings to RtComponentPropertyType
  if (typeStr == "bool") { return RtComponentPropertyType::Bool; }
  if (typeStr == "float" || typeStr == "double") { return RtComponentPropertyType::Float; }
  if (typeStr == "float2" || typeStr == "double2") { return RtComponentPropertyType::Float2; }
  if (typeStr == "float3" || typeStr == "double3" || typeStr == "normal3f" || typeStr == "normal3d" || typeStr == "color3f" || typeStr == "color3d") { return RtComponentPropertyType::Float3; }
  if (typeStr == "float4" || typeStr == "double4" || typeStr == "color4f" || typeStr == "color4d") { return RtComponentPropertyType::Float4; }
  if (typeStr == "uint") { return RtComponentPropertyType::Enum; }
  if (typeStr == "uint64") { return RtComponentPropertyType::Hash; }
  
  // Default to Float if we can't determine the type
  Logger::warn(str::format("Could not resolve flexible type for property ", property.name, 
                           " with USD type ", typeStr, ", defaulting to Float"));
  return RtComponentPropertyType::Float;
}

size_t GraphUsdParser::getPropertyIndex(
    RtGraphTopology& topology,
    const pxr::SdfPath& propertyPath,
    const RtComponentPropertySpec& property) {
  auto iter = topology.propertyPathHashToIndexMap.find(propertyPath.GetString());
  if (iter == topology.propertyPathHashToIndexMap.end()) {
    // This is a new property, so create an index for it.
    size_t propertyIndex = topology.propertyTypes.size();
    topology.propertyTypes.push_back(property.type);
    topology.propertyPathHashToIndexMap[propertyPath.GetString()] = propertyIndex;
    
    // Add all possible property paths (current name + all old names) to the map
    // so that connected properties can find the correct index regardless of which name they use
    std::string nodePathStr = propertyPath.GetParentPath().GetString();
    for (const std::string& oldName : property.oldUsdNames) {
      topology.propertyPathHashToIndexMap[nodePathStr + "." + oldName] = propertyIndex;
    }
    
    return propertyIndex;
  }
  // This is a property that already exists.
  return iter->second;
}

bool GraphUsdParser::versionCheck(const pxr::UsdPrim& nodePrim, const RtComponentSpec& node) {
  const static auto kVersionToken = pxr::TfToken { "node:typeVersion" };
  pxr::UsdAttribute versionAttr = nodePrim.GetAttribute(kVersionToken);
  if (versionAttr && versionAttr.IsValid()) {
    pxr::VtValue value;
    versionAttr.Get(&value);
    int dataVersion = value.Get<int>();
    if (dataVersion > node.version) {
      Logger::err(str::format("Component: ", nodePrim.GetPath().GetString(), " is newer than this runtime can handle.  This means the graph was authored with a newer version of the runtime."));
      return false;
    } else {
      if (dataVersion < node.version) {
        Logger::warn(str::format("Component: ", nodePrim.GetPath().GetString(), " is old.  This means the graph was authored with an older version of the schema, and should be updated in the Toolkit."));
      }
      return true;
    }
  }
  Logger::err(str::format("Component:", nodePrim.GetPath().GetString(), " is missing a `node:typeVersion` attribute."));
  return false;
}
RtComponentPropertyValue GraphUsdParser::getPropertyValue(const pxr::UsdRelationship& rel, const RtComponentPropertySpec& spec, PathToOffsetMap& pathToOffsetMap) {
  static const pxr::TfToken kGraphPrimType = pxr::TfToken("OmniGraph");
  if (spec.type != RtComponentPropertyType::Prim) {
    Logger::err(str::format("Incorrect type of USD property: ", spec.usdPropertyName, " should be an attribute, but was a Relationship."));
    return spec.defaultValue;
  }

  if (rel && rel.IsValid()) {
    pxr::SdfPathVector targets;
    rel.GetTargets(&targets);
    if (targets.size() == 1) {
      // Convert an SdfPath to an offset into the list of replacements.
      // When an instance of the graph is created, these will be further converted to RtInstance* or RtLight*.
      pxr::SdfPath path = targets[0];
      // TODO[REMIX-4405]: To support graphs in pointInstancers, we'll need to add the pointInstanceIndex
      // to pathHash calculated here... but we don't want to re-parse the entire graph for each instance.
      XXH64_hash_t pathHash = XXH3_64bits(path.GetString().c_str(), path.GetString().size());
      auto iter = pathToOffsetMap.find(pathHash);
      PrimTarget result = { ReplacementInstance::kInvalidReplacementIndex, kInvalidInstanceId };
      if (iter == pathToOffsetMap.end()) {
        Logger::err(str::format("Relationship path ", path.GetString(), " not found in replacement hierarchy."));
      } else {
        pxr::UsdPrim prim = rel.GetStage()->GetPrimAtPath(path);
        // if the offset is 0, it may be referring to the original mesh, which may not be a valid prim.
        if (!prim.IsValid() && iter->second != 0) {
          Logger::err(str::format("Relationship path ", path.GetString(), " not found in replacement hierarchy."));
        } else {
          result = { iter->second, kInvalidInstanceId };
        }
      }
      return propertyValueForceType<PrimTarget>(result);
    } else if (targets.size() > 1) {
      Logger::err(str::format("Relationship ", rel.GetPath().GetString(), " has multiple targets, which is not supported."));
    }
  }
  // Note: this intentionally ignores the default value - if the relationship isn't connected,
  // we need to use kInvalidReplacementIndex.
  return PrimTarget{ ReplacementInstance::kInvalidReplacementIndex, kInvalidInstanceId };
}

RtComponentPropertyValue GraphUsdParser::getPropertyValue(const pxr::UsdAttribute& attr, const RtComponentPropertySpec& spec, PathToOffsetMap& pathToOffsetMap) {
  if (attr && attr.IsValid()) {
    pxr::VtValue value;
    attr.Get(&value);
    switch (spec.type) {
    case RtComponentPropertyType::Bool:
      return getPropertyValue<bool>(value, spec);
    case RtComponentPropertyType::Float:
      return getPropertyValue<float>(value, spec);
    case RtComponentPropertyType::Float2:
      return getPropertyValue<Vector2>(value, spec);
    case RtComponentPropertyType::Float3:
      return getPropertyValue<Vector3>(value, spec);
    case RtComponentPropertyType::Float4:
      return getPropertyValue<Vector4>(value, spec);
    case RtComponentPropertyType::Enum:
      return getPropertyValue<uint32_t>(value, spec);
    case RtComponentPropertyType::String:
      return getPropertyValue<std::string>(value, spec);
    case RtComponentPropertyType::AssetPath:
      // Special handling for AssetPath when the value is a TfToken
      // We need to resolve the path relative to the attribute's layer
      if (value.IsHolding<pxr::TfToken>()) {
        const std::string pathStr = value.Get<pxr::TfToken>().GetString();
        if (!pathStr.empty()) {
          return resolveAssetPath(attr, pathStr);
        }
        return spec.defaultValue;
      }
      return getPropertyValue<std::string>(value, spec);
    case RtComponentPropertyType::Hash:
      // Hash is stored as uint64_t but represented as a token in USD
      return getPropertyValue<uint64_t>(value, spec);
    case RtComponentPropertyType::Prim:
      throw DxvkError(str::format("Prim target properties should be UsdRelationships, not UsdAttributes."));
      return spec.defaultValue;
    case RtComponentPropertyType::Any:
    case RtComponentPropertyType::NumberOrVector:
      throw DxvkError(str::format("Flexible types (Any, NumberOrVector) should not be loaded from USD attributes."));
      return spec.defaultValue;
    }
    Logger::err(str::format("Unknown property type: ", spec.type));
    assert(false && "Unknown property type in getPropertyValue");
  }
  return spec.defaultValue;
}

// Helper function to resolve the correct property path considering old property names and layer strength
pxr::SdfPath GraphUsdParser::resolvePropertyPath(const pxr::UsdPrim& nodePrim, const RtComponentPropertySpec& property) {
  // Start with the current property path
  pxr::SdfPath propertyPath = nodePrim.GetPath().AppendProperty(pxr::TfToken(property.usdPropertyName));
  
  // Check if we need to replace propertyPath with an old property path
  if (!property.oldUsdNames.empty()) {
    // This property has legacy names.
    // We want to use the property with the strongest layer, not the one with the newest name.
    
    // Check if the current property path is valid and has authored values
    pxr::UsdProperty propertyObj = nodePrim.GetPropertyAtPath(propertyPath);
    std::vector<std::pair<pxr::SdfPropertySpecHandle, pxr::SdfLayerOffset>> bestPropertyStackWithOffsets;
    
    // If current property is valid, use it as the initial best choice
    if (propertyObj && propertyObj.IsValid() && propertyObj.IsAuthored()) {
      bestPropertyStackWithOffsets = propertyObj.GetPropertyStackWithLayerOffsets();
    }
    
    // Check all old property names and find the one with the strongest layer
    for (const std::string& oldName : property.oldUsdNames) {
      pxr::SdfPath oldPropertyPath = nodePrim.GetPath().AppendProperty(pxr::TfToken(oldName));
      pxr::UsdProperty oldPropertyObj = nodePrim.GetPropertyAtPath(oldPropertyPath);
      
      if (oldPropertyObj && oldPropertyObj.IsValid() && oldPropertyObj.IsAuthored()) {
        std::vector<std::pair<pxr::SdfPropertySpecHandle, pxr::SdfLayerOffset>> oldPropertyStackWithOffsets = oldPropertyObj.GetPropertyStackWithLayerOffsets();
        
        // If this is the first valid property found, or if it's defined on a stronger layer
        if (bestPropertyStackWithOffsets.empty() || 
            (!oldPropertyStackWithOffsets.empty() && 
             oldPropertyStackWithOffsets[0].second.GetOffset() > bestPropertyStackWithOffsets[0].second.GetOffset())) {
          propertyPath = oldPropertyPath;
          bestPropertyStackWithOffsets = oldPropertyStackWithOffsets;
        }
      }
    }
  }
  
  return propertyPath;
}

} // namespace dxvk
