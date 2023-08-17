#pragma once

#include "dxvk_include.h"

namespace dxvk {

  /**
   * \brief Device info
   * 
   * Stores core properties and a bunch of extension-specific
   * properties, if the respective extensions are available.
   * Structures for unsupported extensions will be undefined,
   * so before using them, check whether they are supported.
   */
  struct DxvkDeviceInfo {
    VkPhysicalDeviceProperties2                               core;
    VkPhysicalDeviceIDProperties                              coreDeviceId;
    VkPhysicalDeviceSubgroupProperties                        coreSubgroup;
    VkPhysicalDeviceConservativeRasterizationPropertiesEXT    extConservativeRasterization;
    VkPhysicalDeviceCustomBorderColorPropertiesEXT            extCustomBorderColor;
    VkPhysicalDeviceRobustness2PropertiesEXT                  extRobustness2;
    VkPhysicalDeviceTransformFeedbackPropertiesEXT            extTransformFeedback;
    VkPhysicalDeviceVertexAttributeDivisorPropertiesEXT       extVertexAttributeDivisor;
    VkPhysicalDeviceDepthStencilResolvePropertiesKHR          khrDepthStencilResolve;
    VkPhysicalDeviceDriverPropertiesKHR                       khrDeviceDriverProperties;
    VkPhysicalDeviceRayTracingPipelinePropertiesKHR           khrDeviceRayTracingPipelineProperties;
    VkPhysicalDeviceAccelerationStructurePropertiesKHR        khrDeviceAccelerationStructureProperties;
    VkPhysicalDeviceFloatControlsPropertiesKHR                khrShaderFloatControls;
    VkPhysicalDeviceOpacityMicromapPropertiesEXT              extOpacityMicromapProperties;
    VkPhysicalDeviceRayTracingInvocationReorderPropertiesNV   nvRayTracingInvocationReorderProperties;
  };


  /**
   * \brief Device features
   * 
   * Stores core features and extension-specific features.
   * If the respective extensions are not available, the
   * extended features will be marked as unsupported.
   */
  struct DxvkDeviceFeatures {
    VkPhysicalDeviceFeatures2                                 core;
    VkPhysicalDeviceVulkan11Features                          vulkan11Features;
    VkPhysicalDeviceVulkan12Features                          vulkan12Features;
    VkPhysicalDevice4444FormatsFeaturesEXT                    ext4444Formats;
    VkPhysicalDeviceCustomBorderColorFeaturesEXT              extCustomBorderColor;
    VkPhysicalDeviceDepthClipEnableFeaturesEXT                extDepthClipEnable;
    VkPhysicalDeviceExtendedDynamicStateFeaturesEXT           extExtendedDynamicState;
    VkPhysicalDeviceMemoryPriorityFeaturesEXT                 extMemoryPriority;
    VkPhysicalDeviceRobustness2FeaturesEXT                    extRobustness2;
    VkPhysicalDeviceShaderDemoteToHelperInvocationFeaturesEXT extShaderDemoteToHelperInvocation;
    VkPhysicalDeviceTransformFeedbackFeaturesEXT              extTransformFeedback;
    VkPhysicalDeviceVertexAttributeDivisorFeaturesEXT         extVertexAttributeDivisor;
  	VkPhysicalDeviceRayQueryFeaturesKHR	                      khrRayQueryFeatures;
    VkPhysicalDeviceRayTracingPipelineFeaturesKHR	            khrDeviceRayTracingPipelineFeatures;
  	VkPhysicalDeviceAccelerationStructureFeaturesKHR          khrAccelerationStructureFeatures;
    VkPhysicalDeviceShaderAtomicFloatFeaturesEXT              extShaderAtomicFloat;
    VkPhysicalDeviceDiagnosticsConfigFeaturesNV               nvDeviceDiagnosticsConfig;
    VkPhysicalDeviceBufferDeviceAddressFeaturesKHR            khrBufferDeviceAddress;

    // NV-DXVK start: opacity micromap
    VkPhysicalDeviceSynchronization2FeaturesKHR               khrSynchronization2;
    // NV-DXVK end

    // NV-DXVK start: DLFG integration
    VkPhysicalDeviceOpticalFlowFeaturesNV                     nvOpticalFlow;
    // NV-DXVK end
  };

}