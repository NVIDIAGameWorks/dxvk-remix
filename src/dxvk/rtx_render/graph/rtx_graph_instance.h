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

#include "../rtx_types.h"
#include "rtx_graph_types.h"

namespace dxvk {
class GraphManager;

class GraphInstance {
public:
  GraphInstance(GraphManager* graphManager, const XXH64_hash_t graphHash, const size_t batchIndex, const uint64_t id)
    : m_graphManager(graphManager), m_graphHash(graphHash), m_batchIndex(batchIndex), m_id(id) {
  }

  ~GraphInstance() {
    m_primInstanceOwner.setReplacementInstance(nullptr, ReplacementInstance::kInvalidReplacementIndex, this, PrimInstance::Type::Graph);
  }

  const XXH64_hash_t getGraphHash() const {
    return m_graphHash;
  }
  const size_t getBatchIndex() const {
    return m_batchIndex;
  }
  void setBatchIndex(const size_t batchIndex) {
    m_batchIndex = batchIndex;
  }
  const uint64_t getId() const {
    return m_id;
  }

  void removeInstance();

  PrimInstanceOwner& getPrimInstanceOwner() {
    return m_primInstanceOwner;
  }

  const PrimInstanceOwner& getPrimInstanceOwner() const {
    return m_primInstanceOwner;
  }

private:
  GraphManager* m_graphManager;
  PrimInstanceOwner m_primInstanceOwner;

  // Hash of the batch this instance is in.
  const XXH64_hash_t m_graphHash;

  // Current index of the instance in the batch.
  size_t m_batchIndex;

  // Unique ID of this instance, assigned at creation time.
  const uint64_t m_id;

};

} // namespace dxvk
