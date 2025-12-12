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

#include "rtx_graph_types.h"
#include "rtx_graph_instance.h"
#include "../../dxvk_context.h"
#include <cassert>

namespace dxvk {

class RtGraphBatch {
public:
  RtGraphBatch() = default;
  ~RtGraphBatch() = default;

  // Prevent copying to avoid resource management issues
  RtGraphBatch(const RtGraphBatch&) = delete;
  RtGraphBatch& operator=(const RtGraphBatch&) = delete;

  // Allow moving
  RtGraphBatch(RtGraphBatch&&) = default;
  RtGraphBatch& operator=(RtGraphBatch&&) = default;

  void Initialize(const RtGraphTopology& topology);
  
  bool addInstance(Rc<DxvkContext> context, const RtGraphState& graphState, GraphInstance* replacementInstance);

  void removeInstance(GraphInstance* graphInstance);

  // Reserve space for N new instances all at once.  This should save some
  // memory allocations if adding many new instances at once.
  void increaseReserve(size_t numInstances);

  void update(Rc<DxvkContext> context);

  void applySceneOverrides(Rc<DxvkContext> context);

  void removeAllInstances();

  size_t getNumInstances() const {
    return m_graphInstances.size();
  }

  const std::vector<GraphInstance*>& getInstances() const {
    return m_graphInstances;
  }

  // Safety and validation methods

  // Returns true if the graph has no valid components.
  // Empty graphs can exist without causing errors, but they won't do anything.
  bool isEmpty() const {
    return m_componentBatches.empty();
  }

  bool hasInstance(size_t index) const {
    return index < m_graphInstances.size();
  }

  bool validateInstanceIndex(size_t index) const {
    if (!hasInstance(index)) {
      Logger::err(str::format("Invalid instance index: ", index, " (max: ", m_graphInstances.size() - 1, ")"));
      return false;
    }
    return true;
  }

  // GUI access methods
  const std::vector<std::unique_ptr<RtComponentBatch>>& getComponentBatches() const {
    return m_componentBatches;
  }

  const std::vector<RtComponentPropertyVector>& getProperties() const {
    return m_properties;
  }

  const RtGraphTopology& getTopology() const {
    assert(m_topology != nullptr && "Topology not initialized");
    return *m_topology;
  }

  // Look up a PrimInstance from the data a component can store / pass around.
  // This is done instead of passing pointers directly, to avoid stale pointer problems.
  PrimInstance* resolvePrimTarget(const Rc<DxvkContext>& context, size_t batchIndex, PrimTarget primTarget) const {
    // context unused for now, but will probably be needed for fetching prim target from instanceId.
    if (primTarget.replacementIndex != ReplacementInstance::kInvalidReplacementIndex) {
      const std::vector<GraphInstance*>& instances = m_graphInstances;
      if (instances[batchIndex] != nullptr) {
        ReplacementInstance* replacementInstance = instances[batchIndex]->getPrimInstanceOwner().getReplacementInstance();
        if (replacementInstance != nullptr) {
          return &replacementInstance->prims[primTarget.replacementIndex];
        }
      }
      return nullptr;
    } else if (primTarget.instanceId != kInvalidInstanceId) {
      Logger::err("components targetting prims in other draw calls is not supported yet.");
      return nullptr;
    }
    return nullptr;
  }

private:
  XXH64_hash_t m_graphHash;
  const RtGraphTopology* m_topology = nullptr;
  std::vector<std::unique_ptr<RtComponentBatch>> m_componentBatches;
  std::vector<uint32_t> m_batchesWithSceneOverrides;
  std::vector<RtComponentPropertyVector> m_properties;

  std::vector<GraphInstance*> m_graphInstances;

  void updateRange(Rc<DxvkContext> context, size_t start, size_t end);

};

} // namespace dxvk
