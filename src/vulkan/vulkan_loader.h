/*
* Copyright (c) 2021-2023, NVIDIA CORPORATION. All rights reserved.
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

#include "../util/rc/util_rc.h"
#include "../util/rc/util_rc_ptr.h"

#define VK_USE_PLATFORM_WIN32_KHR 1
#include <vulkan/vulkan.h>

#define VULKAN_FN(name) \
  ::PFN_ ## name name = reinterpret_cast<::PFN_ ## name>(sym(#name))

namespace dxvk::vk {
  
  /**
   * \brief Vulkan library loader
   * 
   * Provides methods to load Vulkan functions that
   * can be called before creating a  instance.
   */
  struct LibraryLoader : public RcObject {
    PFN_vkVoidFunction sym(const char* name) const;
  };
  
  
  /**
   * \brief Vulkan instance loader
   * 
   * Loads Vulkan functions that can be
   * called for a specific instance.
   */
  struct InstanceLoader : public RcObject {
    InstanceLoader(bool owned, VkInstance instance);
    PFN_vkVoidFunction sym(const char* name) const;
    VkInstance instance() const { return m_instance; }
  protected:
    const VkInstance m_instance;
    const bool       m_owned;
  };
  
  
  /**
   * \brief Vulkan device loader
   * 
   * Loads Vulkan functions for a
   * specific device.
   */
  struct DeviceLoader : public RcObject {
    DeviceLoader(bool owned, VkInstance instance, VkDevice device);
    PFN_vkVoidFunction sym(const char* name) const;
    VkDevice device() const { return m_device; }
  protected:
    const PFN_vkGetDeviceProcAddr m_getDeviceProcAddr;
    const VkDevice                m_device;
    const bool                    m_owned;
  };
  
  
  /**
   * \brief Vulkan library functions
   * 
   * Vulkan functions that are called before
   * creating an actual Vulkan instance.
   */
  struct LibraryFn : LibraryLoader {
    LibraryFn();
    ~LibraryFn();
    
    VULKAN_FN(vkCreateInstance);
    VULKAN_FN(vkEnumerateInstanceLayerProperties);
    VULKAN_FN(vkEnumerateInstanceExtensionProperties);
  };
  
  
  /**
   * \brief Vulkan instance functions
   * 
   * Vulkan functions for a given instance that
   * are independent of any Vulkan devices.
   */
  struct InstanceFn : InstanceLoader {
    InstanceFn(bool owned, VkInstance instance);
    ~InstanceFn();
    
    VULKAN_FN(vkCreateDevice);
    VULKAN_FN(vkDestroyInstance);
    VULKAN_FN(vkEnumerateDeviceExtensionProperties);
    VULKAN_FN(vkEnumeratePhysicalDevices);
    VULKAN_FN(vkGetPhysicalDeviceFeatures);
    VULKAN_FN(vkGetPhysicalDeviceFeatures2);
    VULKAN_FN(vkGetPhysicalDeviceFormatProperties);
    VULKAN_FN(vkGetPhysicalDeviceFormatProperties2);
    VULKAN_FN(vkGetPhysicalDeviceProperties2);
    VULKAN_FN(vkGetPhysicalDeviceImageFormatProperties);
    VULKAN_FN(vkGetPhysicalDeviceImageFormatProperties2);
    VULKAN_FN(vkGetPhysicalDeviceMemoryProperties);
    VULKAN_FN(vkGetPhysicalDeviceMemoryProperties2);
    VULKAN_FN(vkGetPhysicalDeviceProperties);
    VULKAN_FN(vkGetPhysicalDeviceQueueFamilyProperties);
    VULKAN_FN(vkGetPhysicalDeviceQueueFamilyProperties2);
    VULKAN_FN(vkGetPhysicalDeviceSparseImageFormatProperties);
    VULKAN_FN(vkGetPhysicalDeviceSparseImageFormatProperties2);

    #ifdef VK_KHR_get_surface_capabilities2
    VULKAN_FN(vkGetPhysicalDeviceSurfaceCapabilities2KHR);
    VULKAN_FN(vkGetPhysicalDeviceSurfaceFormats2KHR);
    #endif
    
    #ifdef VK_KHR_surface
    #ifdef VK_USE_PLATFORM_XCB_KHR
    VULKAN_FN(vkCreateXcbSurfaceKHR);
    VULKAN_FN(vkGetPhysicalDeviceXcbPresentationSupportKHR);
    #endif
    
    #ifdef VK_USE_PLATFORM_XLIB_KHR
    VULKAN_FN(vkCreateXlibSurfaceKHR);
    VULKAN_FN(vkGetPhysicalDeviceXlibPresentationSupportKHR);
    #endif
    
    #ifdef VK_USE_PLATFORM_WAYLAND_KHR
    VULKAN_FN(vkCreateWaylandSurfaceKHR);
    VULKAN_FN(vkGetPhysicalDeviceWaylandPresentationSupportKHR);
    #endif

#ifdef VK_KHR_win32_surface
	VULKAN_FN(vkCreateWin32SurfaceKHR);
	VULKAN_FN(vkGetPhysicalDeviceWin32PresentationSupportKHR);
#endif
    
    VULKAN_FN(vkDestroySurfaceKHR);
    
    VULKAN_FN(vkGetPhysicalDeviceSurfaceSupportKHR);
    VULKAN_FN(vkGetPhysicalDeviceSurfaceCapabilitiesKHR);
    VULKAN_FN(vkGetPhysicalDeviceSurfaceFormatsKHR);
    VULKAN_FN(vkGetPhysicalDeviceSurfacePresentModesKHR);
    #endif



    
    #ifdef VK_EXT_debug_report
    VULKAN_FN(vkCreateDebugReportCallbackEXT);
    VULKAN_FN(vkDestroyDebugReportCallbackEXT);
    VULKAN_FN(vkDebugReportMessageEXT);
    #endif

    #ifdef VK_EXT_debug_utils
    VULKAN_FN(vkCreateDebugUtilsMessengerEXT);
    VULKAN_FN(vkDestroyDebugUtilsMessengerEXT);
    VULKAN_FN(vkSubmitDebugUtilsMessageEXT);
    #endif

    #ifdef VK_EXT_full_screen_exclusive
    VULKAN_FN(vkGetPhysicalDeviceSurfacePresentModes2EXT);
    #endif
  };
  
  
  /**
   * \brief Vulkan device functions
   * 
   * Vulkan functions for a specific Vulkan device.
   * This ensures that no slow dispatch code is executed.
   */
  struct DeviceFn : DeviceLoader {
    DeviceFn(bool owned, VkInstance instance, VkDevice device);
    ~DeviceFn();
    
    VULKAN_FN(vkDestroyDevice);
    VULKAN_FN(vkGetDeviceQueue);
    VULKAN_FN(vkQueueSubmit);
    VULKAN_FN(vkQueueWaitIdle);
    VULKAN_FN(vkDeviceWaitIdle);
    VULKAN_FN(vkAllocateMemory);
    VULKAN_FN(vkFreeMemory);
    VULKAN_FN(vkMapMemory);
    VULKAN_FN(vkUnmapMemory);
    VULKAN_FN(vkFlushMappedMemoryRanges);
    VULKAN_FN(vkInvalidateMappedMemoryRanges);
    VULKAN_FN(vkGetDeviceMemoryCommitment);
    VULKAN_FN(vkBindBufferMemory);
    VULKAN_FN(vkBindImageMemory);
    VULKAN_FN(vkGetBufferMemoryRequirements);
    VULKAN_FN(vkGetBufferMemoryRequirements2);
    VULKAN_FN(vkGetImageMemoryRequirements);
    VULKAN_FN(vkGetImageMemoryRequirements2);
    VULKAN_FN(vkGetImageSparseMemoryRequirements);
    VULKAN_FN(vkGetImageSparseMemoryRequirements2);
    VULKAN_FN(vkQueueBindSparse);
    VULKAN_FN(vkCreateFence);
    VULKAN_FN(vkDestroyFence);
    VULKAN_FN(vkResetFences);
    VULKAN_FN(vkGetFenceStatus);
    VULKAN_FN(vkWaitForFences);
    VULKAN_FN(vkCreateSemaphore);
    VULKAN_FN(vkDestroySemaphore);
    VULKAN_FN(vkCreateEvent);
    VULKAN_FN(vkDestroyEvent);
    VULKAN_FN(vkGetEventStatus);
    VULKAN_FN(vkSetEvent);
    VULKAN_FN(vkResetEvent);
    VULKAN_FN(vkCreateQueryPool);
    VULKAN_FN(vkDestroyQueryPool);
    VULKAN_FN(vkGetQueryPoolResults);
    VULKAN_FN(vkCreateBuffer);
    VULKAN_FN(vkDestroyBuffer);
    VULKAN_FN(vkCreateBufferView);
    VULKAN_FN(vkDestroyBufferView);
    VULKAN_FN(vkCreateImage);
    VULKAN_FN(vkDestroyImage);
    VULKAN_FN(vkGetImageSubresourceLayout);
    VULKAN_FN(vkCreateImageView);
    VULKAN_FN(vkDestroyImageView);
    VULKAN_FN(vkCreateShaderModule);
    VULKAN_FN(vkDestroyShaderModule);
    VULKAN_FN(vkCreatePipelineCache);
    VULKAN_FN(vkDestroyPipelineCache);
    VULKAN_FN(vkGetPipelineCacheData);
    VULKAN_FN(vkMergePipelineCaches);
    VULKAN_FN(vkCreateGraphicsPipelines);
    VULKAN_FN(vkCreateComputePipelines);
    VULKAN_FN(vkDestroyPipeline);
    VULKAN_FN(vkCreatePipelineLayout);
    VULKAN_FN(vkDestroyPipelineLayout);
    VULKAN_FN(vkCreateSampler);
    VULKAN_FN(vkDestroySampler);
    VULKAN_FN(vkCreateDescriptorSetLayout);
    VULKAN_FN(vkDestroyDescriptorSetLayout);
    VULKAN_FN(vkCreateDescriptorPool);
    VULKAN_FN(vkDestroyDescriptorPool);
    VULKAN_FN(vkResetDescriptorPool);
    VULKAN_FN(vkAllocateDescriptorSets);
    VULKAN_FN(vkFreeDescriptorSets);
    VULKAN_FN(vkUpdateDescriptorSets);
    VULKAN_FN(vkCreateFramebuffer);
    VULKAN_FN(vkDestroyFramebuffer);
    VULKAN_FN(vkCreateRenderPass);
    VULKAN_FN(vkDestroyRenderPass);
    VULKAN_FN(vkGetRenderAreaGranularity);
    VULKAN_FN(vkCreateCommandPool);
    VULKAN_FN(vkDestroyCommandPool);
    VULKAN_FN(vkResetCommandPool);
    VULKAN_FN(vkAllocateCommandBuffers);
    VULKAN_FN(vkFreeCommandBuffers);
    VULKAN_FN(vkBeginCommandBuffer);
    VULKAN_FN(vkEndCommandBuffer);
    VULKAN_FN(vkResetCommandBuffer);
    VULKAN_FN(vkCreateDescriptorUpdateTemplate);
    VULKAN_FN(vkDestroyDescriptorUpdateTemplate);
    VULKAN_FN(vkUpdateDescriptorSetWithTemplate);
    VULKAN_FN(vkCmdBindPipeline);
    VULKAN_FN(vkCmdSetViewport);
    VULKAN_FN(vkCmdSetScissor);
    VULKAN_FN(vkCmdSetLineWidth);
    VULKAN_FN(vkCmdSetDepthBias);
    VULKAN_FN(vkCmdSetBlendConstants);
    VULKAN_FN(vkCmdSetDepthBounds);
    VULKAN_FN(vkCmdSetStencilCompareMask);
    VULKAN_FN(vkCmdSetStencilWriteMask);
    VULKAN_FN(vkCmdSetStencilReference);
    VULKAN_FN(vkCmdBindDescriptorSets);
    VULKAN_FN(vkCmdBindIndexBuffer);
    VULKAN_FN(vkCmdBindVertexBuffers);
    VULKAN_FN(vkCmdDraw);
    VULKAN_FN(vkCmdDrawIndexed);
    VULKAN_FN(vkCmdDrawIndirect);
    VULKAN_FN(vkCmdDrawIndexedIndirect);
    VULKAN_FN(vkCmdDispatch);
    VULKAN_FN(vkCmdDispatchIndirect);
    VULKAN_FN(vkCmdCopyBuffer);
    VULKAN_FN(vkCmdCopyImage);
    VULKAN_FN(vkCmdBlitImage);
    VULKAN_FN(vkCmdCopyBufferToImage);
    VULKAN_FN(vkCmdCopyImageToBuffer);
    VULKAN_FN(vkCmdUpdateBuffer);
    VULKAN_FN(vkCmdFillBuffer);
    VULKAN_FN(vkCmdClearColorImage);
    VULKAN_FN(vkCmdClearDepthStencilImage);
    VULKAN_FN(vkCmdClearAttachments);
    VULKAN_FN(vkCmdResolveImage);
    VULKAN_FN(vkCmdSetEvent);
    VULKAN_FN(vkCmdResetEvent);
    VULKAN_FN(vkCmdWaitEvents);
    VULKAN_FN(vkCmdPipelineBarrier);
    VULKAN_FN(vkCmdBeginQuery);
    VULKAN_FN(vkCmdEndQuery);
    VULKAN_FN(vkCmdResetQueryPool);
    VULKAN_FN(vkCmdWriteTimestamp);
    VULKAN_FN(vkCmdCopyQueryPoolResults);
    VULKAN_FN(vkCmdPushConstants);
    VULKAN_FN(vkCmdBeginRenderPass);
    VULKAN_FN(vkCmdNextSubpass);
    VULKAN_FN(vkCmdEndRenderPass);
    VULKAN_FN(vkCmdExecuteCommands);
    VULKAN_FN(vkGetSemaphoreCounterValue);
    VULKAN_FN(vkWaitSemaphores);
    // NV-DXVK start: RTXIO
    VULKAN_FN(vkSignalSemaphore);
    // NV-DXVK end

    #ifdef VK_KHR_synchronization2
    VULKAN_FN(vkCmdPipelineBarrier2KHR);
    #endif

    #ifdef VK_EXT_opacity_micromap
    VULKAN_FN(vkCreateMicromapEXT);
    VULKAN_FN(vkDestroyMicromapEXT);
    VULKAN_FN(vkCmdBuildMicromapsEXT);
    VULKAN_FN(vkGetMicromapBuildSizesEXT);
    #endif

    #ifdef VK_KHR_acceleration_structure
    VULKAN_FN(vkCreateAccelerationStructureKHR);
    VULKAN_FN(vkDestroyAccelerationStructureKHR);
    VULKAN_FN(vkCmdBuildAccelerationStructuresKHR);
    VULKAN_FN(vkCmdBuildAccelerationStructuresIndirectKHR);
    VULKAN_FN(vkBuildAccelerationStructuresKHR);
    VULKAN_FN(vkCopyAccelerationStructureKHR);
    VULKAN_FN(vkCopyAccelerationStructureToMemoryKHR);
    VULKAN_FN(vkCopyMemoryToAccelerationStructureKHR);
    VULKAN_FN(vkWriteAccelerationStructuresPropertiesKHR);
    VULKAN_FN(vkCmdCopyAccelerationStructureKHR);
    VULKAN_FN(vkCmdCopyAccelerationStructureToMemoryKHR);
    VULKAN_FN(vkCmdCopyMemoryToAccelerationStructureKHR);
    VULKAN_FN(vkGetAccelerationStructureDeviceAddressKHR);
    VULKAN_FN(vkCmdWriteAccelerationStructuresPropertiesKHR);
    VULKAN_FN(vkGetDeviceAccelerationStructureCompatibilityKHR);
    VULKAN_FN(vkGetAccelerationStructureBuildSizesKHR);
    #endif

    #ifdef VK_KHR_ray_tracing_pipeline
    VULKAN_FN(vkCmdTraceRaysKHR);
    VULKAN_FN(vkCreateRayTracingPipelinesKHR);
    VULKAN_FN(vkGetRayTracingShaderGroupHandlesKHR);
    VULKAN_FN(vkGetRayTracingCaptureReplayShaderGroupHandlesKHR);
    VULKAN_FN(vkCmdTraceRaysIndirectKHR);
    VULKAN_FN(vkGetRayTracingShaderGroupStackSizeKHR);
    #endif

    #ifdef VK_KHR_create_renderpass2
    VULKAN_FN(vkCreateRenderPass2KHR);
    VULKAN_FN(vkCmdBeginRenderPass2KHR);
    VULKAN_FN(vkCmdNextSubpass2KHR);
    VULKAN_FN(vkCmdEndRenderPass2KHR);
    #endif
    
    #ifdef VK_KHR_draw_indirect_count
    VULKAN_FN(vkCmdDrawIndirectCountKHR);
    VULKAN_FN(vkCmdDrawIndexedIndirectCountKHR);
    #endif
    
    #ifdef VK_KHR_swapchain
    VULKAN_FN(vkCreateSwapchainKHR);
    VULKAN_FN(vkDestroySwapchainKHR);
    VULKAN_FN(vkGetSwapchainImagesKHR);
    VULKAN_FN(vkAcquireNextImageKHR);
    VULKAN_FN(vkQueuePresentKHR);
    #endif

    #ifdef VK_EXT_conditional_rendering
    VULKAN_FN(vkCmdBeginConditionalRenderingEXT);
    VULKAN_FN(vkCmdEndConditionalRenderingEXT);
    #endif

    #ifdef VK_EXT_extended_dynamic_state
    VULKAN_FN(vkCmdBindVertexBuffers2EXT);
    VULKAN_FN(vkCmdSetCullModeEXT);
    VULKAN_FN(vkCmdSetDepthBoundsTestEnableEXT);
    VULKAN_FN(vkCmdSetDepthCompareOpEXT);
    VULKAN_FN(vkCmdSetDepthTestEnableEXT);
    VULKAN_FN(vkCmdSetDepthWriteEnableEXT);
    VULKAN_FN(vkCmdSetFrontFaceEXT);
    VULKAN_FN(vkCmdSetPrimitiveTopologyEXT);
    VULKAN_FN(vkCmdSetScissorWithCountEXT);
    VULKAN_FN(vkCmdSetStencilOpEXT);
    VULKAN_FN(vkCmdSetStencilTestEnableEXT);
    VULKAN_FN(vkCmdSetViewportWithCountEXT);
    #endif

    #ifdef VK_EXT_full_screen_exclusive
    VULKAN_FN(vkAcquireFullScreenExclusiveModeEXT);
    VULKAN_FN(vkReleaseFullScreenExclusiveModeEXT);
    VULKAN_FN(vkGetDeviceGroupSurfacePresentModes2EXT);
    #endif

    #ifdef VK_EXT_host_query_reset
    VULKAN_FN(vkResetQueryPoolEXT);
    #endif

    #ifdef VK_EXT_transform_feedback
    VULKAN_FN(vkCmdBindTransformFeedbackBuffersEXT);
    VULKAN_FN(vkCmdBeginTransformFeedbackEXT);
    VULKAN_FN(vkCmdEndTransformFeedbackEXT);
    VULKAN_FN(vkCmdDrawIndirectByteCountEXT);
    VULKAN_FN(vkCmdBeginQueryIndexedEXT);
    VULKAN_FN(vkCmdEndQueryIndexedEXT);
    #endif

    #ifdef VK_EXT_buffer_device_address
    VULKAN_FN(vkGetBufferDeviceAddress);
    #endif

    #ifdef VK_KHR_deferred_host_operations
    VULKAN_FN(vkCreateDeferredOperationKHR);
    VULKAN_FN(vkDeferredOperationJoinKHR);
    VULKAN_FN(vkDestroyDeferredOperationKHR);
    VULKAN_FN(vkGetDeferredOperationMaxConcurrencyKHR);
    VULKAN_FN(vkGetDeferredOperationResultKHR);
    #endif

    #ifdef VK_EXT_calibrated_timestamps
    VULKAN_FN(vkGetPhysicalDeviceCalibrateableTimeDomainsEXT);
    VULKAN_FN(vkGetCalibratedTimestampsEXT);
    #endif

    // NV-DXVK start: Integrate Aftermath
    #ifdef VK_NV_device_diagnostic_checkpoints
    VULKAN_FN(vkCmdSetCheckpointNV);
    VULKAN_FN(vkGetQueueCheckpointDataNV);
    #endif
    // NV-DXVK end

    #ifdef VK_NVX_image_view_handle
    VULKAN_FN(vkGetImageViewHandleNVX);
    VULKAN_FN(vkGetImageViewAddressNVX);
    #endif

    #ifdef VK_NVX_binary_import
    VULKAN_FN(vkCreateCuModuleNVX);
    VULKAN_FN(vkCreateCuFunctionNVX);
    VULKAN_FN(vkDestroyCuModuleNVX);
    VULKAN_FN(vkDestroyCuFunctionNVX);
    VULKAN_FN(vkCmdCuLaunchKernelNVX);
    #endif

    #ifdef VK_KHR_buffer_device_address
    VULKAN_FN(vkGetBufferDeviceAddressKHR);
    #endif

    // NV-DXVK start: use EXT_debug_utils
    #ifdef VK_EXT_debug_utils
    VULKAN_FN(vkCmdBeginDebugUtilsLabelEXT);
    VULKAN_FN(vkCmdEndDebugUtilsLabelEXT);
    VULKAN_FN(vkCmdInsertDebugUtilsLabelEXT);
    VULKAN_FN(vkQueueBeginDebugUtilsLabelEXT);
    VULKAN_FN(vkQueueEndDebugUtilsLabelEXT);
    VULKAN_FN(vkQueueInsertDebugUtilsLabelEXT);
    VULKAN_FN(vkSetDebugUtilsObjectNameEXT);
    VULKAN_FN(vkSetDebugUtilsObjectTagEXT);
    #endif
    // NV-DXVK end
  };
  
}
