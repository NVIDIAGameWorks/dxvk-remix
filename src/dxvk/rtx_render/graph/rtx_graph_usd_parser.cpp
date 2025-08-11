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

namespace dxvk {

RtGraphState GraphUsdParser::parseGraph(AssetReplacements& replacements, const pxr::UsdPrim& graphPrim, PathToOffsetMap& pathToOffsetMap) {
  ScopedCpuProfileZone();
  RtGraphTopology topology;
  std::vector<RtComponentPropertyValue> initialValues;

  // Iterate over all active nodes in the graph
  std::vector<DAGNode> sortedNodes = getDAGSortedNodes(graphPrim);
  for (DAGNode& dagNode : sortedNodes) {
    const RtComponentSpec& componentSpec = *dagNode.spec;
    pxr::UsdPrim child = graphPrim.GetStage()->GetPrimAtPath(dagNode.path);

    if (!versionCheck(child, componentSpec)) {
      Logger::err(str::format("Version mismatch for componentSpec ", child.GetPath().GetString(), " . The runtime's version is: ", componentSpec.version, ".  Attempting to load anyway."));
      continue;
    }

    topology.componentSpecs.push_back(&componentSpec);
    topology.propertyIndices.push_back(std::vector<size_t>());
    std::vector<size_t>& propertyIndices = topology.propertyIndices.back();

    // Iterate over the properties of the node
    for (const RtComponentPropertySpec& property : componentSpec.properties) {
      // NOTE: This would be more efficient if we cached all of the TfTokens.  Unsure how to
      // do that without leaking pxr includes to the wider codebase.
      pxr::SdfPath propertyPath = child.GetPath().AppendProperty(pxr::TfToken(property.usdPropertyName));
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
            // OmniGraph seems to indicate connections by putting the source property
            // as the last entry in the targets list.
            std::string sourcePath = targets.back().GetString();
            auto iter = topology.propertyPathHashToIndexMap.find(sourcePath);
            if (iter == topology.propertyPathHashToIndexMap.end()) {
              // If the associated output property hasn't been found, just treat this as a non-connected property.
              Logger::err(str::format("Property ", propertyPath.GetString(), " has a connection to property ", sourcePath, " that has not been loaded yet.  This may be because that prim failed to load, or it may indicate an error in the topological sort."));
            } else {
              propertyIndices.push_back(iter->second);
              hasConnection = true;
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
        if (attr && attr.IsValid()) {
          pxr::SdfPathVector connections;
          attr.GetConnections(&connections);
          if (connections.size() > 0) {
            std::string connectionPath = connections[0].GetString();
            auto iter = topology.propertyPathHashToIndexMap.find(connectionPath);
            if (iter == topology.propertyPathHashToIndexMap.end()) {
              // If the associated output property hasn't been found, just treat this as a non-connected property.
              Logger::err(str::format("Property ", propertyPath.GetString(), " has a connection to property ", connectionPath, " that has not been loaded yet.  This may be because that prim failed to load, or it may indicate an error in the topological sort."));
            } else {
              propertyIndices.push_back(iter->second);
              hasConnection = true;
            }
          }
        }
        if (!hasConnection) {
          propertyIndices.push_back(getPropertyIndex(topology, propertyPath, property));
          initialValues.push_back(getPropertyValue(attr, property, pathToOffsetMap));
        }
      }
    }
    topology.graphHash = XXH3_64bits_withSeed(&componentSpec.componentType, sizeof(RtComponentType), topology.graphHash);
    topology.graphHash = XXH3_64bits_withSeed(&propertyIndices[0], sizeof(size_t) * propertyIndices.size(), topology.graphHash);
  }

  return { replacements.storeObject(topology.graphHash, RtGraphTopology{topology}), initialValues };
}

std::vector<GraphUsdParser::DAGNode> GraphUsdParser::getDAGSortedNodes(const pxr::UsdPrim& graphPrim) {
  ScopedCpuProfileZone();
  std::unordered_map<pxr::SdfPath, size_t, pxr::SdfPath::Hash> pathToIndexMap;
  std::vector<DAGNode> nodes;
  pxr::UsdPrimSiblingRange children = graphPrim.GetFilteredChildren(pxr::UsdPrimIsActive);
  size_t numNodes = 0;
  // UsdPrimSiblingRange has no size() method, so precalculate the size to avoid reallocations.
  for (auto child : children) {
    numNodes++;
  }
  nodes.reserve(numNodes);
  pathToIndexMap.reserve(numNodes);
  // First, make a list of all the nodes in the graph:
  for (auto child : children) {
    const RtComponentSpec* componentSpec = getComponentSpecForPrim(child);
    if (componentSpec == nullptr) {
      continue;
    }
    pathToIndexMap[child.GetPath()] = nodes.size();
    nodes.push_back(DAGNode { child.GetPath(), componentSpec, 0, {} });
  }

  // Check for connections between properties, and make edges based on them:
  for (size_t nodeIndex = 0; nodeIndex < nodes.size(); nodeIndex++) {
    DAGNode& node = nodes[nodeIndex];
    for (const RtComponentPropertySpec& property : node.spec->properties) {
      // NOTE: This would be more efficient if we cached all of the TfTokens.  Unsure how to
      // do that without leaking pxr includes to the wider codebase.
      pxr::SdfPath propertyPath = node.path.AppendProperty(pxr::TfToken(property.usdPropertyName));
      pxr::UsdAttribute attr = graphPrim.GetAttributeAtPath(propertyPath);
      bool hasConnection = false;
      if (attr && attr.IsValid()) {
        pxr::SdfPathVector connections;
        attr.GetConnections(&connections);
        if (connections.size() == 1) {
          auto iter = pathToIndexMap.find(connections[0].GetPrimPath());
          if (iter == pathToIndexMap.end()) {
            Logger::err(str::format("Node ", node.path.GetString(), " has a connection to a node that does not exist (may have failed to load earlier in the process): ", connections[0].GetPrimPath().GetString()));
            continue;
          }
          size_t dependentIndex = iter->second;
          // Note: multiple properties can link the same node, so we need to avoid adding duplicate edges.
          if (node.dependents.find(dependentIndex) == node.dependents.end()) {
            nodes[dependentIndex].dependencyCount++;
            node.dependents.insert(dependentIndex);
          }
        } else if (connections.size() > 1) {
          // NOTE: unclear what the behavior should be here.  There are some attributes that
          // can take multiple connections to combine into a list, but we don't currently support those.
          assert(false && "Node has multiple connections to the same property.");
          Logger::err(str::format("Node ", node.path.GetString(), " has multiple connections to the same property: ", property.usdPropertyName));
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
  const RtComponentSpec* spec = getComponentSpec(componentType);
  if (spec == nullptr) {
    Logger::err(str::format("Node ", nodePrim.GetPath().GetString(), " has an unknown `node:type` attribute: ", typeName));
    return nullptr;
  }
  return spec;
}

// If the `propertyPath` has been encountered before, return the original index.
// Otherwise, create a new index for the property and return tht.
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
    return value.Get<int>() == node.version;
  }
  Logger::err(str::format("Node ", nodePrim.GetPath().GetString(), " is missing a `node:typeVersion` attribute."));
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
      uint32_t result = ReplacementInstance::kInvalidReplacementIndex;
      if (iter == pathToOffsetMap.end()) {
        Logger::err(str::format("Relationship path ", path.GetString(), " not found in replacement hierarchy."));
      } else {
        pxr::UsdPrim prim = rel.GetStage()->GetPrimAtPath(path);
        if (!prim.IsValid()) {
          Logger::err(str::format("Relationship path ", path.GetString(), " not found in replacement hierarchy."));
        } else {
          result = iter->second;
        }
      }
      return propertyValueForceType<uint32_t>(result);
    } else {
      Logger::err(str::format("Relationship ", rel.GetPath().GetString(), " has multiple targets, which is not supported."));
    }
  }
  // Note: this intentionally ignores the default value - if the relationship isn't connected,
  // we need to use kInvalidReplacementIndex.
  return ReplacementInstance::kInvalidReplacementIndex;
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
    case RtComponentPropertyType::Color3:
      return getPropertyValue<Vector3>(value, spec);
    case RtComponentPropertyType::Color4:
      return getPropertyValue<Vector4>(value, spec);
    case RtComponentPropertyType::Int32:
      return getPropertyValue<int>(value, spec);
    case RtComponentPropertyType::Uint32:
      return getPropertyValue<uint32_t>(value, spec);
    case RtComponentPropertyType::Uint64:
      return getPropertyValue<uint64_t>(value, spec);
    case RtComponentPropertyType::Prim:
      throw DxvkError(str::format("Prim target properties should be UsdRelationships, not UsdAttributes."));
      return spec.defaultValue;
    }
    Logger::err(str::format("Unknown property type: ", spec.type));
    assert(false && "Unknown property type in getPropertyValue");
  }
  return spec.defaultValue;
}

} // namespace dxvk
