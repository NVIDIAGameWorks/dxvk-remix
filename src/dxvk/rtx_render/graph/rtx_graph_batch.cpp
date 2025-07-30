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


#include "rtx_graph_batch.h"
#include "dxvk_device.h"
#include "dxvk_context.h"
#include "dxvk_objects.h"
#include "dxvk_scoped_annotation.h"
#include "../util/log/log.h"
#include "../util/util_string.h"

#include "rtx_component_list.h"

namespace dxvk {
namespace {
  /**
   * Removes an element at the specified index from a vector by moving the last element
   * and then removing the last element. This is more efficient than shifting elements.
   *
   * @param vec The vector to modify
   * @param index The index of the element to remove
   * @return true if the element was removed, false if the index was invalid
   */
  template<typename T>
  bool swapAndRemove(std::vector<T>& vec, size_t index) {
    if (index >= vec.size()) {
      return false;
    }

    // If it's not the last element, copy the last element
    if (index != vec.size() - 1) {
      vec[index] = std::move(vec.back());
    }

    // Remove the last element
    vec.pop_back();
    return true;
  }

  /**
   * Specialized version of swapAndRemove for RtComponentPropertyVector (std::variant).
   * Uses std::visit to handle the different vector types in the variant.
   *
   * @param propVec The RtComponentPropertyVector variant to modify
   * @param index The index of the element to remove
   * @return true if the element was removed, false if the index was invalid
   */
  bool swapAndRemove(RtComponentPropertyVector& propVec, size_t index) {
    return std::visit([index](auto&& vec) -> bool {
      return swapAndRemove(vec, index);
    }, propVec);
  }

  // Helper function to convert from the absolute type of an initial value, to the type of 
  // the property vector it is being pushed into.
  template<typename T>
  void pushBack(RtComponentPropertyVector& properties, T value) {
    // If this throws a `std::bad_variant_access` exception, it means that the RtGraphState
    // that was passed to addInstance doesn't match the RtGraphTopology that was passed to
    // 
    // the constructor.  This indicates as parsing error in the loading of the graph, or 
    // a logical error that is either finding the wrong batch, or using the wrong state
    // during instance creation.
    std::get<std::vector<T>>(properties).push_back(value);
  }
}

void RtGraphBatch::Initialize(const RtGraphTopology& topology) {
  // NOTE: need to do this separate from the constructor, because the address of `this` changes when it's moved.
  ScopedCpuProfileZone();
  if (topology.componentSpecs.empty()) {
    Logger::err("RtGraphTopology has no component specs");
    return;
  }

  for (size_t i = 0; i < topology.propertyTypes.size(); i++) {
    m_properties.push_back(propertyVectorFromType(topology.propertyTypes[i]));
  }
  // Create a RtComponentBatch for each component in the topology, which will keep references to the 
  // property vectors it cares about.
  for (size_t i = 0; i < topology.componentSpecs.size(); i++) {
    if (topology.componentSpecs[i] == nullptr) {
      Logger::err(str::format("Component spec at index ", i, " is null"));
      continue;
    }
    m_componentBatches.push_back(topology.componentSpecs[i]->createComponentBatch(*this, m_properties, topology.propertyIndices[i]));
    if (topology.componentSpecs[i]->applySceneOverrides != nullptr) {
      m_batchesWithSceneOverrides.push_back(m_componentBatches.size() - 1);
    }
  }
  m_graphHash = topology.graphHash;
}

void RtGraphBatch::addInstance(Rc<DxvkContext> context, const RtGraphState& initialGraphState, GraphInstance* graphInstance) {
  ScopedCpuProfileZone();
  if (graphInstance == nullptr) {
    Logger::err("Cannot add null GraphInstance");
    return;
  }

  if (initialGraphState.values.size() != m_properties.size()) {
    Logger::err(str::format("RtGraphState had the wrong number of values. Expected: ",
                            m_properties.size(), " got: ", initialGraphState.values.size()));
    assert(false && "RtGraphState had the wrong number of values.");
    return;
  }

  graphInstance->setBatchIndex(m_graphInstances.size());
  m_graphInstances.push_back(graphInstance);

  // Add a new slot to the end of each property vector, and set it to the intitial value.
  for (size_t i = 0; i < m_properties.size(); i++) {
    // TODO if the property is a relationship, we need to convert the offset into a pointer.

    // We're dealing with two variants here: the Value from the initial state, and
    // the Vector from m_properties.  We use std::visit to resolve the type of the Value,
    // and then infer the type of the Vector based on the Value's type.
    try {
      std::visit([this, i](auto&& arg) {
        pushBack(m_properties[i], arg);
      }, initialGraphState.values[i]);
    }
    catch (const std::bad_variant_access& e) {
      Logger::err(str::format("Type mismatch when adding instance to property ", i, ": ", e.what()));
      // Remove the instance we just added
      m_graphInstances.pop_back();
      return;
    }
  }

  // Update the new graph once, to fill in the initial values.
  updateRange(context, m_graphInstances.size() - 1, m_graphInstances.size());
}

void RtGraphBatch::removeInstance(GraphInstance* graphInstance) {
  if (graphInstance == nullptr) {
    Logger::err("Cannot remove null GraphInstance");
    return;
  }

  const size_t index = graphInstance->getBatchIndex();
  if (!validateInstanceIndex(index)) {
    return;
  }

  if (m_graphInstances[index] != graphInstance) {
    Logger::err(str::format("GraphInstance to remove has the wrong index.  Instance: ", graphInstance->getId()));
    assert(false && "GraphInstance to remove has the wrong index.");
    return;
  }

  // Swap the instance to be removed to the back of the lists, and then pop it off.
  // This keeps the property lists densely packed while avoiding the need to shift elements.
  for (size_t i = 0; i < m_properties.size(); i++) {
    swapAndRemove(m_properties[i], index);
  }
  swapAndRemove(m_graphInstances, index);

  // If the removed instance wasn't already the last one, need to update the index of the swapped instance.
  if (index < m_graphInstances.size()) {
    m_graphInstances[index]->setBatchIndex(index);
  }
}

void RtGraphBatch::increaseReserve(size_t numInstances) {
  size_t newSize = m_graphInstances.size() + numInstances;
  m_graphInstances.reserve(newSize);
  for (auto& prop : m_properties) {
    // std::visit is needed because m_properties are variants.  
    // This simply resolves them to an std::vector<T>.
    std::visit([newSize](auto& vec) { vec.reserve(newSize); }, prop);
  }
}

void RtGraphBatch::update(Rc<DxvkContext> context) {
  updateRange(context, 0, m_graphInstances.size());
}

void RtGraphBatch::updateRange(Rc<DxvkContext> context, size_t start, size_t end) {
  ScopedCpuProfileZone();
  for (auto& componentBatch : m_componentBatches) {
    componentBatch->updateRange(context, start, end);
  }
}

void RtGraphBatch::applySceneOverrides(Rc<DxvkContext> context) {
  for (auto& batchIndex : m_batchesWithSceneOverrides) {
    m_componentBatches[batchIndex]->getSpec()->applySceneOverrides(context, *m_componentBatches[batchIndex], 0, m_graphInstances.size());
  }
}

void RtGraphBatch::removeAllInstances() {
  // This removes all of the instances and clears out the per-instance data
  // It retains the graph structure (the list of components and their properties).

  m_graphInstances.clear();

  // clear out the contents of each of the property vectors, but keep the actual vectors around.
  for (auto& prop : m_properties) {
    // std::visit is needed because m_properties are variants.  
    // This simply resolves them to an std::vector<T>.
    std::visit([](auto& vec) { vec.clear(); }, prop);
  }
}

} // namespace dxvk
