/*
* Copyright (c) 2026, NVIDIA CORPORATION. All rights reserved.
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

#include <memory>
#include <vector>

#include "rtx_types.h"

namespace dxvk {

class DxvkAccelStructure;
class RtInstance;

/**
 * Keeps a bounded CPU-side history of the scene data used to build acceleration
 * structures. The history is only serialized when a device loss is reported,
 * and source buffer lifetimes are only extended when separately requested.
 */
class RtxGpuCrashRecorder {
public:
  struct State;

  RtxGpuCrashRecorder(bool enabled, bool retainSourceBuffers);
  ~RtxGpuCrashRecorder();

  RtxGpuCrashRecorder(const RtxGpuCrashRecorder&) = delete;
  RtxGpuCrashRecorder& operator=(const RtxGpuCrashRecorder&) = delete;

  void clear();

  void recordScene(
    uint32_t frameId,
    uint64_t sceneGeneration,
    const std::vector<RtInstance*>& instances,
    const std::vector<Rc<PooledBlas>>& pooledBlases,
    const std::vector<Rc<PooledBlas>>& activeDynamicBlases);

  void recordBlasBuilds(
    uint32_t frameId,
    const std::vector<VkAccelerationStructureBuildGeometryInfoKHR>& builds,
    const std::vector<VkAccelerationStructureBuildRangeInfoKHR*>& ranges,
    VkDeviceAddress transformBufferAddress,
    const std::vector<VkTransformMatrixKHR>& transforms);

  void recordTlasBuild(
    uint32_t frameId,
    uint32_t tlasType,
    const VkAccelerationStructureBuildGeometryInfoKHR& build,
    const VkAccelerationStructureBuildRangeInfoKHR& range,
    const std::vector<VkAccelerationStructureInstanceKHR>& cpuInstances,
    uint32_t gpuGeneratedInstanceCount,
    VkDeviceAddress instanceBufferAddress,
    VkDeviceSize instanceBufferOffset,
    const Rc<DxvkAccelStructure>& destination);

  void dump(const char* reason) const;

private:
  std::unique_ptr<State> m_state;
};

} // namespace dxvk
