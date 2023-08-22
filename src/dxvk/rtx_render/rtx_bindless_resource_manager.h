/*
* Copyright (c) 2022-2023, NVIDIA CORPORATION. All rights reserved.
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
#include "rtx_utils.h"
#include "rtx_common_object.h"

namespace dxvk {
  class DxvkDevice;
  class DxvkCommandList;

  class BindlessResourceManager : public CommonDeviceObject {
  public:
    friend struct BindlessTable;

    enum Table {
      Textures = 0,
      Buffers,
      Count
    };

    static const uint32_t kMaxBindlessResources = 64 * 1024; // our indices are uint16_t...

    BindlessResourceManager() = delete;

    explicit BindlessResourceManager(DxvkDevice* device);

    void prepareSceneData(const Rc<DxvkContext> ctx, const std::vector<TextureRef>& rtTextures, const std::vector<RaytraceBuffer>& rtBuffers);

    VkDescriptorSet getGlobalBindlessTableSet(Table type) const;

    VkDescriptorSetLayout getGlobalBindlessTableLayout(Table type) const {
      return m_tables[type][currentIdx()]->layout;
    }

  private:

    struct BindlessTable {
      BindlessTable() = delete;

      BindlessTable(BindlessResourceManager* pManager)
        : m_pManager(pManager) { }
      ~BindlessTable();

      VkDescriptorSetLayout layout = VK_NULL_HANDLE;
      VkDescriptorSet bindlessDescSet = VK_NULL_HANDLE;

      void createLayout(const VkDescriptorType type);
      void updateDescriptors(VkWriteDescriptorSet& set);

    private:
      const Rc<vk::DeviceFn> vkd() const;

      BindlessResourceManager* m_pManager = nullptr;
    };

    // Persistent desc pool, our sets can be updated after bind (should be no need to reset this pool)
    Rc<DxvkDescriptorPool> m_globalBindlessPool[kMaxFramesInFlight];
    
    std::unique_ptr<BindlessTable> m_tables[Table::Count][kMaxFramesInFlight];

    uint32_t m_globalBindlessDescSetIdx = 0;
    uint32_t m_frameLastUpdated = UINT_MAX;


    uint32_t currentIdx() const {
      return m_globalBindlessDescSetIdx;
    }

    uint32_t nextIdx() const {
      return (m_globalBindlessDescSetIdx + 1) % kMaxFramesInFlight;
    }

    void createGlobalBindlessDescPool();
  };
} // namespace dxvk 