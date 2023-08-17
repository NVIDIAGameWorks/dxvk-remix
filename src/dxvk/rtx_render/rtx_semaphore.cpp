/*
* Copyright (c) 2023, NVIDIA CORPORATION. All rights reserved.
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
#include "rtx_semaphore.h"

namespace dxvk {
  RtxSemaphore* RtxSemaphore::createTimeline(DxvkDevice* device, const char* name, uint64_t initialValue, bool win32Shared) {
    RtxSemaphore* ret = new RtxSemaphore();
    ret->m_device = device;
    ret->m_isTimeline = true;
    
    VkSemaphoreTypeCreateInfo timelineCreateInfo;
    timelineCreateInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
    timelineCreateInfo.pNext = nullptr;
    timelineCreateInfo.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    timelineCreateInfo.initialValue = initialValue;

    VkExportSemaphoreCreateInfo sharedInfo;
    if (win32Shared) {
      sharedInfo.sType = VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO;
      sharedInfo.pNext = nullptr;
      sharedInfo.handleTypes = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_WIN32_BIT;

      timelineCreateInfo.pNext = &sharedInfo;
    }
      
    VkSemaphoreCreateInfo createInfo;
    createInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    createInfo.pNext = &timelineCreateInfo;
    createInfo.flags = 0;

    VkResult result = device->vkd()->vkCreateSemaphore(device->handle(),
                                                       &createInfo,
                                                       nullptr, &ret->m_sema);
    if (result != VK_SUCCESS) {
      throw DxvkError(str::format("Timeline semaphore creation failed with: ",
                                  result));
    }

    ret->labelSemaphore(name);
    
    return ret;
  }

  RtxSemaphore* RtxSemaphore::createBinary(DxvkDevice* device, const char* name) {
    RtxSemaphore* ret = new RtxSemaphore();
    ret->m_device = device;
    ret->m_isTimeline = false;

    VkSemaphoreCreateInfo createInfo;
    createInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    createInfo.pNext = nullptr;
    createInfo.flags = 0;
    
    VkResult result = device->vkd()->vkCreateSemaphore(device->handle(),
                                                   &createInfo,
                                                   nullptr, &ret->m_sema);
    if (result != VK_SUCCESS) {
      throw DxvkError(str::format("Binary semaphore creation failed with: ",
                                  result));
    }

    ret->labelSemaphore(name);
    return ret;
  }

  void RtxSemaphore::labelSemaphore(const char* name) {
    if (m_device->vkd()->vkSetDebugUtilsObjectNameEXT) {
      VkDebugUtilsObjectNameInfoEXT nameInfo;
      nameInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
      nameInfo.pNext = nullptr;
      nameInfo.objectType = VK_OBJECT_TYPE_SEMAPHORE;
      nameInfo.objectHandle = (uint64_t) m_sema;
      nameInfo.pObjectName = name;
      m_device->vkd()->vkSetDebugUtilsObjectNameEXT(m_device->handle(), &nameInfo);
    }
  }

  RtxSemaphore::~RtxSemaphore() {
    m_device->vkd()->vkDestroySemaphore(m_device->handle(), m_sema, nullptr);
  }

  uint64_t RtxSemaphore::value() const {
    uint64_t value;
    m_device->vkd()->vkGetSemaphoreCounterValue(m_device->handle(),
                                                m_sema, &value);
    return value;
  }

  void RtxSemaphore::signal(uint64_t value) {
    VkSemaphoreSignalInfo signalInfo;
    signalInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO;
    signalInfo.pNext = nullptr;
    signalInfo.semaphore = m_sema;
    signalInfo.value = value;

    m_device->vkd()->vkSignalSemaphore(m_device->handle(), &signalInfo);
  }

  void RtxSemaphore::wait(uint64_t value) {
    VkSemaphoreWaitInfo waitInfo;
    waitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
    waitInfo.pNext = nullptr;
    waitInfo.flags = 0;
    waitInfo.semaphoreCount = 1;
    waitInfo.pSemaphores = &m_sema;
    waitInfo.pValues = &value;

    do {
      auto status = m_device->vkd()->vkWaitSemaphores(m_device->handle(),
                                                      &waitInfo,
                                                      1'000'000'000ull);

      if (status == VK_SUCCESS) {
        break;
      }

      if (status == VK_TIMEOUT) {
        Logger::warn("Timeline semaphore wait timeout!");
      } else {
        throw DxvkError("Timeline semaphore wait failed!");
      }
    } while (true);
  }

  RtxFence::RtxFence(DxvkDevice* device)
    : m_device(device) {
    VkFenceCreateInfo info;
    info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    info.pNext = nullptr;
    info.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    if (device->vkd()->vkCreateFence(device->handle(), &info, nullptr, &m_fence) != VK_SUCCESS) {
      throw DxvkError("RtxFence: vkCreateFence failed");
    }
  }

  RtxFence::~RtxFence() {
    if (m_fence) {
      m_device->vkd()->vkDestroyFence(m_device->handle(), m_fence, nullptr);
      m_fence = nullptr;
    }
  }
}
