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

#include "dxvk_context.h"
#include "dxvk_scoped_annotation.h"
#include "rtx_graph_batch.h"
#include "rtx_graph_ogn_writer.h"
#include "rtx_graph_types.h"
#include "rtx_render/rtx_asset_replacer.h"
#include "rtx_render/rtx_option.h"
#include "../util/util_fast_cache.h"
#include <atomic>
#include <mutex>


namespace dxvk {

// The class responsible for managing graph lifetime and updates.
class GraphManager {
public:
  RTX_OPTION("rtx.graph", bool, enable, true, "Enable graph loading.  If disabled, all graphs will be unloaded, losing any state.");
  RTX_OPTION("rtx.graph", bool, pauseGraphUpdates, false, "Pause graph updating.  If enabled, graphs logic will not be updated, but graph state will be retained.");

  GraphManager() {
    static std::once_flag schemaWriteFlag;
    std::call_once(schemaWriteFlag, [this]() {
      if (env::getEnvVar("RTX_GRAPH_WRITE_OGN_SCHEMA") == "1") {
        std::string schemaPath = env::getEnvVar("RTX_GRAPH_SCHEMA_PATH");
        if (schemaPath.empty()) {
          schemaPath = "rtx-remix/schemas/";
        }
        std::string docsPath = env::getEnvVar("RTX_GRAPH_DOCS_PATH");
        if (docsPath.empty()) {
          docsPath = "rtx-remix/docs/";
        }
        writeAllOGNSchemas(schemaPath.c_str());
        writeAllMarkdownDocs(docsPath.c_str());
      }
    });
  }

  GraphInstance* addInstance(Rc<DxvkContext> context, const RtGraphState& graphState) {
    ScopedCpuProfileZone();
    if (!enable()) {
      return nullptr;
    }
    auto iter = m_batches.find(graphState.topology.graphHash);
    if (iter == m_batches.end()) {
      iter = m_batches.emplace(graphState.topology.graphHash, RtGraphBatch()).first;
      iter->second.Initialize(graphState.topology);
    }
    uint64_t instanceId = m_nextInstanceId++;
    auto pair = m_graphInstances.try_emplace(instanceId, this, graphState.topology.graphHash, 0, instanceId, graphState);
    if (!pair.second) {
      Logger::err(str::format("GraphInstance already exists. Instance: ", instanceId));
      return nullptr;
    }
    iter->second.addInstance(context, graphState, &pair.first->second);
    return &pair.first->second;
  }

  void removeInstance(const uint64_t instanceId) {
    auto iter = m_graphInstances.find(instanceId);
    if (iter == m_graphInstances.end()) {
      Logger::err(str::format("GraphInstance to remove not found. Instance: ", instanceId));
    }
    auto batchIter = m_batches.find(iter->second.getGraphHash());
    if (batchIter == m_batches.end()) {
      Logger::err(str::format("Batch for GraphInstance to remove not found. Batch hash: ", iter->second.getGraphHash()));
    }
    batchIter->second.removeInstance(&iter->second);
    if (batchIter->second.getNumInstances() == 0) {
      m_batches.erase(batchIter);
    }
    m_graphInstances.erase(instanceId);
  }

  // Queues the graph manager to wipe all graphs in the next update.
  void resetGraphState() const {
    std::lock_guard<std::mutex> lock(m_instanceResetMutex);
    m_resetPending = true;
  }

  // Queues a specific graph instance to be reset in the next update.
  void queueInstanceReset(const uint64_t instanceId) const {
    std::lock_guard<std::mutex> lock(m_instanceResetMutex);
    m_instanceResetQueue.push_back(instanceId);
  }

  void clear() {
    m_batches.clear();
    m_graphInstances.clear();
  }

  void update(Rc<DxvkContext>& context) {
    ScopedCpuProfileZone();
    
    // Check if a reset was requested from another thread
    {
      std::lock_guard<std::mutex> lock(m_instanceResetMutex);
      if (m_resetPending) {
        clear();
        m_instanceResetQueue.clear();
        m_resetPending = false;
      } else {
        // Process queued instance resets
        for (uint64_t instanceId : m_instanceResetQueue) {
          removeInstance(instanceId);
        }
        m_instanceResetQueue.clear();
      }
    }
    
    if (!enable()) {
      clear();
      return;
    }
    if (pauseGraphUpdates()) {
      return;
    }
    for (auto& batch : m_batches) {
      batch.second.update(context);
    }
  }

  void applySceneOverrides(Rc<DxvkContext> context) {
    ScopedCpuProfileZone();
    if (!enable() || pauseGraphUpdates()) {
      return;
    }
    for (auto& batch : m_batches) {
      batch.second.applySceneOverrides(context);
    }
  }

  // GUI access methods
  const std::unordered_map<uint64_t, GraphInstance>& getGraphInstances() const {
    return m_graphInstances;
  }

  const fast_unordered_cache<RtGraphBatch>& getBatches() const {
    return m_batches;
  }

private:
  Rc<DxvkContext> m_context;

  fast_unordered_cache<RtGraphBatch> m_batches;

  std::unordered_map<uint64_t, GraphInstance> m_graphInstances;

  uint64_t m_nextInstanceId = 1;
  
  mutable std::mutex m_instanceResetMutex;
  mutable bool m_resetPending = false;
  mutable std::vector<uint64_t> m_instanceResetQueue;
};

}
