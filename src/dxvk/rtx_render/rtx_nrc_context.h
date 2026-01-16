/*
* Copyright (c) 2024, NVIDIA CORPORATION. All rights reserved.
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

#include "dxvk_context.h"
#include "rtx_common_object.h"

#include "rtx/external/NRC.h"
#include <nrc/include/NrcVK.h>

namespace dxvk {
  struct NrcCtxOptions {
    friend class NeuralRadianceCache;
    RTX_OPTION("rtx.neuralRadianceCache", bool, enableCustomNetworkConfig, false,
               "Enables usage of a custom config \"CustomNetworkConfig.json\" for NRC.\n"
               "The file needs to be present in the application's working directory.");
    RTX_OPTION_ENV("rtx.neuralRadianceCache", std::string, cudaDllDepsDirectoryPath, std::string(), "RTX_NRC_CUDA_DEPS_DIR", "Optional setting for specifying a custom directory path where the CUDA run-time dll dependencies are located.");
  };

  // Encapsulates lower level calls to NRC library and management of memory objects shared between NRC and the app
  class NrcContext : public CommonDeviceObject, public RcObject {
  public:

    struct Configuration {
      bool debugBufferIsRequired = false;
    };

    NrcContext(DxvkDevice& device, const Configuration& config);
    ~NrcContext();

    // Must be called first after NrcContext is created
    nrc::Status initialize();

    // Returns if NRC is supported.
    // The function needs to be called with a valid device pointer to initialize the support capability, after that it can be called with nullptr.
    static bool checkIsSupported(const dxvk::DxvkDevice* device = nullptr);

    // Returns true if the call succeeded
    bool onFrameBegin(DxvkContext& ctx, const nrc::ContextSettings& config, const nrc::FrameSettings& frameSettings, bool* hasCacheBeenReset);
    float queryAndTrain(DxvkContext& ctx, bool calculateTrainingLoss);
    void resolve(DxvkContext& ctx, const Rc<DxvkImageView>& outputImage);
    void endFrame();

    VkDeviceSize getCurrentMemoryConsumption() const;
    bool isDebugBufferRequired() const;

    void populateShaderConstants(struct NrcConstants& outConstants) const;

    Rc<DxvkBuffer>& getBuffer(nrc::BufferIdx nrcResourceType);
    DxvkBufferSlice getBufferSlice(DxvkContext& ctx, nrc::BufferIdx nrcResourceType);

    void clearBuffer(DxvkContext& ctx, nrc::BufferIdx nrcResourceType, VkPipelineStageFlagBits dstStageMask, VkAccessFlags dstAccessMask);
    VkBufferMemoryBarrier createVkBufferMemoryBarrier(nrc::BufferIdx bufferIndex, VkAccessFlags srcAccessMask, VkAccessFlags dstAccessMask);

  private:
    void allocateOrCheckAllResources(bool forceAllocate = false);
    void tryReallocateBuffer(Rc<DxvkBuffer>& buffer, nrc::vulkan::BufferInfo& bufferInfo, 
                             const nrc::AllocationInfo& allocationInfo);

    inline static bool s_isNrcSupported = false;
    inline static bool s_hasCheckedNrcSupport = false;

    bool m_isSupported = false;
    bool m_isDebugBufferRequired = false;
    nrc::vulkan::Context* m_nrcContext = nullptr;
    nrc::ContextSettings m_nrcContextSettings;
    nrc::FrameSettings m_nrcFrameSettings;

    Rc<DxvkBuffer> m_buffers[static_cast<uint32_t>(nrc::BufferIdx::Count)];
    nrc::vulkan::Buffers        m_nrcBuffers = {};
    nrc::BuffersAllocationInfo  m_nrcBuffersAllocation = {};
  };
}