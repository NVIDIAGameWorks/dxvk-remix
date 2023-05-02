/*
* Copyright (c) 2021-2022, NVIDIA CORPORATION. All rights reserved.
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

#include <vulkan/vulkan.h>

#include "../../util/util_macro.h"

namespace dxvk {

enum class RtxResult
{
    eSuccess = 0,
    eErrorInvalidArgs = 1,
    eErrorOutOfMemory = 2,
    eErrorUnknown = 3,
    eNotReady = 4 ///< Asynchronous operation or waiting is not yet complete.
};

inline bool vkFailed(VkResult res) {
  return res != VK_SUCCESS &&
    res != VK_OPERATION_DEFERRED_KHR &&
    res != VK_THREAD_DONE_KHR &&
    res != VK_THREAD_IDLE_KHR;
}

#   define VK_THROW_IF_FAILED(value)                                                                        \
        do {                                                                                                \
            VkResult res = value;                                                                           \
            if (res != VK_SUCCESS &&                                                                        \
                res != VK_OPERATION_DEFERRED_KHR &&                                                         \
                res != VK_OPERATION_NOT_DEFERRED_KHR &&                                                     \
                res != VK_THREAD_DONE_KHR &&                                                                \
                res != VK_THREAD_IDLE_KHR)                                                                  \
            {                                                                                               \
                std::stringstream ss;                                                                       \
                ss << "[Vulkan call failed] " << __FILE__ << "(" << __LINE__ << "): " << STRINGIFY(value);  \
                __debugbreak();                                                                             \
                throw DxvkError(ss.str());                                                                  \
            }                                                                                               \
        }                                                                                                   \
        while (false)

#   define THROW_IF_FALSE(value)                                                                            \
        do {                                                                                                \
            bool res = !!(value);                                                                           \
            if (res != true) {                                                                              \
                std::stringstream ss;                                                                       \
                ss << "[Check failed] " << __FILE__ << "(" << __LINE__ << "): " << STRINGIFY(value);        \
                __debugbreak();                                                                             \
                throw DxvkError(ss.str());                                                                  \
            }                                                                                               \
        }                                                                                                   \
        while (false)

} // namespace dxvk