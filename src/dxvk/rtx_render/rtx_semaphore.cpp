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
  RtxSemaphore::RtxSemaphore(const Rc<DxvkDevice>& device)
  : m_device(device) {
    VkSemaphoreTypeCreateInfo timelineCreateInfo;
    timelineCreateInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
    timelineCreateInfo.pNext = nullptr;
    timelineCreateInfo.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    timelineCreateInfo.initialValue = 0;

    VkSemaphoreCreateInfo createInfo;
    createInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    createInfo.pNext = &timelineCreateInfo;
    createInfo.flags = 0;

    VkResult result = device->vkd()->vkCreateSemaphore(device->handle(),
                                                       &createInfo,
                                                       nullptr, &m_sema);
    if (result != VK_SUCCESS) {
      throw DxvkError(str::format("Timeline semaphore creation failed with: ",
                                  result));
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
}
