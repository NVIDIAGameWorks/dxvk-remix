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

#include <algorithm>
#include <map>
#include <vector>

#include "dxvk_include.h"
#include "vulkan/vulkan_win32.h"

namespace dxvk {
  
  /**
   * \brief Vulkan extension mode
   * 
   * Defines whether an extension is
   * optional, required, or disabled.
   */
  enum class DxvkExtMode {
    Disabled,
    Optional,
    Required,
    Passive,
  };


  /**
   * \brief Vulkan extension info
   * 
   * Stores information for a single extension.
   * The renderer can use this information to
   * find out which extensions are enabled.
   */
  class DxvkExt {

  public:

    DxvkExt(
      const char*       pName,
            DxvkExtMode mode)
    : m_name(pName), m_mode(mode) { }

    /**
     * \brief Extension name
     * \returns Extension name
     */
    const char* name() const {
      return m_name;
    }

    /**
     * \brief Extension mode
     * \returns Extension mode
     */
    DxvkExtMode mode() const {
      return m_mode;
    }

    /**
     * \brief Checks whether the extension is enabled
     * 
     * If an extension is enabled, the features
     * provided by the extension can be used.
     * \returns \c true if the extension is enabled
     */
    operator bool () const {
      return m_revision != 0;
    }

    /**
     * \brief Supported revision
     * \returns Supported revision
     */
    uint32_t revision() const {
      return m_revision;
    }

    /**
     * \brief Changes extension mode
     * 
     * In some cases, it may be useful to change the
     * default mode dynamically after initialization.
     */
    void setMode(DxvkExtMode mode) {
      m_mode = mode;
    }

    /**
     * \brief Enables the extension
     */
    void enable(uint32_t revision) {
      m_revision = revision;
    }

    /**
     * \brief Disables the extension
     */
    void disable() {
      m_revision = 0;
    }

  private:

    const char* m_name     = nullptr;
    DxvkExtMode m_mode     = DxvkExtMode::Disabled;
    uint32_t    m_revision = 0;

  };


  /**
   * \brief Vulkan name list
   * 
   * A simple \c vector wrapper that can
   * be used to build a list of layer and
   * extension names.
   */
  class DxvkNameList {

  public:

    /**
     * \brief Adds a name
     * \param [in] pName The name
     */
    void add(const char* pName) {
      m_names.push_back(pName);
    }

    /**
     * \brief Number of names
     * \returns Name count
     */
    uint32_t count() const {
      return m_names.size();
    }

    /**
     * \brief Name list
     * \returns Name list
     */
    const char* const* names() const {
      return m_names.data();
    }

    /**
     * \brief Retrieves a single name
     * 
     * \param [in] index Name index
     * \returns The given name
     */
    const char* name(uint32_t index) const {
      return m_names.at(index);
    }

  private:

    std::vector<const char*> m_names;

  };


  /**
   * \brief Vulkan extension set
   * 
   * Stores a set of extensions or layers
   * supported by the Vulkan implementation.
   */
  class DxvkNameSet {

  public:

    DxvkNameSet();
    ~DxvkNameSet();

    /**
     * \brief Adds a name to the set
     * \param [in] pName Extension name
     */
    void add(
      const char*             pName);
    
    /**
     * \brief Merges two name sets
     * 
     * Adds all names from the given name set to
     * this name set, avoiding duplicate entries.
     * \param [in] names Name set to merge
     */
    void merge(
      const DxvkNameSet&      names);

    /**
     * \brief Checks whether an extension is supported
     * 
     * \param [in] pName Extension name
     * \returns Supported revision, or zero
     */
    uint32_t supports(
      const char*             pName) const;
    
    /**
     * \brief Enables requested extensions
     * 
     * Walks over a set of extensions and enables all
     * extensions that are supported and not disabled.
     * This also checks whether all required extensions
     * could be enabled, and returns \c false otherwise.
     * \param [in] numExtensions Number of extensions
     * \param [in] ppExtensions List of extensions
     * \param [out] nameSet Extension name set
     * \returns \c true on success
     */
    bool enableExtensions(
            uint32_t          numExtensions,
            DxvkExt**         ppExtensions,
            DxvkNameSet&      nameSet) const;
    
    /**
     * \brief Disables given extension
     *
     * Removes the given extension from the set
     * and sets its revision to 0 (i.e. disabled).
     * \param [in,out] ext Extension to disable
     */
    void disableExtension(
            DxvkExt&          ext);

    /**
     * \brief Creates name list from name set
     * 
     * Adds all names contained in the name set
     * to a name list, which can then be passed
     * to Vulkan functions.
     * \returns Name list
     */
    DxvkNameList toNameList() const;

    /**
     * \brief Enumerates instance layers
     * 
     * \param [in] vkl Vulkan library functions
     * \returns Set of available instance layers
     */
    static DxvkNameSet enumInstanceLayers(
      const Rc<vk::LibraryFn>&  vkl);
    
    /**
     * \brief Enumerates instance extensions
     * 
     * \param [in] vkl Vulkan library functions
     * \returns Set of available instance extensions
     */
    static DxvkNameSet enumInstanceExtensions(
      const Rc<vk::LibraryFn>&  vkl);
    
    /**
     * \brief Enumerates device extensions
     * 
     * \param [in] vki Vulkan instance functions
     * \param [in] device The device to query
     * \returns Set of available device extensions
     */
    static DxvkNameSet enumDeviceExtensions(
      const Rc<vk::InstanceFn>& vki,
            VkPhysicalDevice    device);

  private:

    std::map<std::string, uint32_t> m_names;

  };

  /**
   * \brief Device extensions
   * 
   * Lists all Vulkan extensions that are potentially
   * used by DXVK if supported by the implementation.
   */
  struct DxvkDeviceExtensions {
    DxvkExt amdMemoryOverallocationBehaviour  = { VK_AMD_MEMORY_OVERALLOCATION_BEHAVIOR_EXTENSION_NAME,     DxvkExtMode::Optional };
    DxvkExt amdShaderFragmentMask             = { VK_AMD_SHADER_FRAGMENT_MASK_EXTENSION_NAME,               DxvkExtMode::Optional };
    DxvkExt ext4444Formats                    = { VK_EXT_4444_FORMATS_EXTENSION_NAME,                       DxvkExtMode::Optional };
    DxvkExt extConservativeRasterization      = { VK_EXT_CONSERVATIVE_RASTERIZATION_EXTENSION_NAME,         DxvkExtMode::Optional };
    DxvkExt extCustomBorderColor              = { VK_EXT_CUSTOM_BORDER_COLOR_EXTENSION_NAME,                DxvkExtMode::Optional };
    DxvkExt extDepthClipEnable                = { VK_EXT_DEPTH_CLIP_ENABLE_EXTENSION_NAME,                  DxvkExtMode::Optional };
    DxvkExt extExtendedDynamicState           = { VK_EXT_EXTENDED_DYNAMIC_STATE_EXTENSION_NAME,             DxvkExtMode::Optional };
    DxvkExt extFullScreenExclusive            = { VK_EXT_FULL_SCREEN_EXCLUSIVE_EXTENSION_NAME,              DxvkExtMode::Optional };
    DxvkExt extMemoryBudget                   = { VK_EXT_MEMORY_BUDGET_EXTENSION_NAME,                      DxvkExtMode::Passive  };
    DxvkExt extMemoryPriority                 = { VK_EXT_MEMORY_PRIORITY_EXTENSION_NAME,                    DxvkExtMode::Optional };
    DxvkExt extRobustness2                    = { VK_EXT_ROBUSTNESS_2_EXTENSION_NAME,                       DxvkExtMode::Optional };
    DxvkExt extShaderDemoteToHelperInvocation = { VK_EXT_SHADER_DEMOTE_TO_HELPER_INVOCATION_EXTENSION_NAME, DxvkExtMode::Optional };
    DxvkExt extShaderStencilExport            = { VK_EXT_SHADER_STENCIL_EXPORT_EXTENSION_NAME,              DxvkExtMode::Optional };
    DxvkExt extShaderViewportIndexLayer       = { VK_EXT_SHADER_VIEWPORT_INDEX_LAYER_EXTENSION_NAME,        DxvkExtMode::Optional };
    DxvkExt extTransformFeedback              = { VK_EXT_TRANSFORM_FEEDBACK_EXTENSION_NAME,                 DxvkExtMode::Optional };
    DxvkExt extVertexAttributeDivisor         = { VK_EXT_VERTEX_ATTRIBUTE_DIVISOR_EXTENSION_NAME,           DxvkExtMode::Optional };
    DxvkExt khrCreateRenderPass2              = { VK_KHR_CREATE_RENDERPASS_2_EXTENSION_NAME,                DxvkExtMode::Optional };
    DxvkExt khrDepthStencilResolve            = { VK_KHR_DEPTH_STENCIL_RESOLVE_EXTENSION_NAME,              DxvkExtMode::Optional };
    DxvkExt khrDrawIndirectCount              = { VK_KHR_DRAW_INDIRECT_COUNT_EXTENSION_NAME,                DxvkExtMode::Optional };
    DxvkExt khrDriverProperties               = { VK_KHR_DRIVER_PROPERTIES_EXTENSION_NAME,                  DxvkExtMode::Optional };
    DxvkExt khrImageFormatList                = { VK_KHR_IMAGE_FORMAT_LIST_EXTENSION_NAME,                  DxvkExtMode::Required };
    DxvkExt khrSamplerMirrorClampToEdge       = { VK_KHR_SAMPLER_MIRROR_CLAMP_TO_EDGE_EXTENSION_NAME,       DxvkExtMode::Optional };
    DxvkExt khrShaderFloatControls            = { VK_KHR_SHADER_FLOAT_CONTROLS_EXTENSION_NAME,              DxvkExtMode::Optional };
    DxvkExt khrSwapchain                      = { VK_KHR_SWAPCHAIN_EXTENSION_NAME,                          DxvkExtMode::Required };
    DxvkExt khrDeferredHostOperations         = { VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,           DxvkExtMode::Required };
    DxvkExt khrAccelerationStructure          = { VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,             DxvkExtMode::Required };
    DxvkExt khrRayQueries                     = { VK_KHR_RAY_QUERY_EXTENSION_NAME,                          DxvkExtMode::Required };
    DxvkExt khrRayTracingPipeline             = { VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME,               DxvkExtMode::Required };
    DxvkExt khrBufferDeviceAddress            = { VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME,              DxvkExtMode::Required };
    DxvkExt khrPipelineLibrary                = { VK_KHR_PIPELINE_LIBRARY_EXTENSION_NAME,                   DxvkExtMode::Optional };
    DxvkExt nvxBinaryImport                   = { VK_NVX_BINARY_IMPORT_EXTENSION_NAME,                      DxvkExtMode::Optional };
    DxvkExt nvxImageViewHandle                = { VK_NVX_IMAGE_VIEW_HANDLE_EXTENSION_NAME,                  DxvkExtMode::Optional };
    DxvkExt khrPushDescriptor                 = { VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME,                    DxvkExtMode::Optional };
    DxvkExt khrShaderInt8Float16Types         = { VK_KHR_SHADER_FLOAT16_INT8_EXTENSION_NAME,                DxvkExtMode::Required };
    DxvkExt extCalibratedTimestamps           = { VK_EXT_CALIBRATED_TIMESTAMPS_EXTENSION_NAME,              DxvkExtMode::Optional };
    // NV-DXVK start: Integrate Aftermath
    DxvkExt nvDeviceDiagnostics               = { VK_NV_DEVICE_DIAGNOSTICS_CONFIG_EXTENSION_NAME,           DxvkExtMode::Optional };
    DxvkExt nvDeviceDiagnosticCheckpoints     = { VK_NV_DEVICE_DIAGNOSTIC_CHECKPOINTS_EXTENSION_NAME,       DxvkExtMode::Optional };
    // NV-DXVK end
    DxvkExt nvRayTracingInvocationReorder     = { VK_NV_RAY_TRACING_INVOCATION_REORDER_EXTENSION_NAME,      DxvkExtMode::Optional };

    // Opacity micromap
    DxvkExt khrSynchronization2               = { VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME,                  DxvkExtMode::Optional };
    DxvkExt extOpacityMicromap                = { VK_EXT_OPACITY_MICROMAP_EXTENSION_NAME,                   DxvkExtMode::Optional };

    // NV-DXVK start: Integrate Reflex
    DxvkExt nvLowLatency                      = { "VK_NV_low_latency",                                      DxvkExtMode::Optional };
    // NV-DXVK end

    // NV-DXVK start: RTXIO
    DxvkExt khrTimelineSemaphore              = { VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME,                 DxvkExtMode::Optional };
    // NV-DXVK end

    // NV-DXVK start: DLFG integration
    DxvkExt khrMaintenance4                   = { VK_KHR_MAINTENANCE_4_EXTENSION_NAME,                      DxvkExtMode::Optional };
    DxvkExt khrExternalMemory                 = { VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME,                    DxvkExtMode::Optional };
    DxvkExt khrExternalMemoryWin32            = { VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME,              DxvkExtMode::Optional };
    DxvkExt khrExternalSemaphore              = { VK_KHR_EXTERNAL_SEMAPHORE_EXTENSION_NAME,                 DxvkExtMode::Optional };
    DxvkExt khrExternalSemaphoreWin32         = { VK_KHR_EXTERNAL_SEMAPHORE_WIN32_EXTENSION_NAME,           DxvkExtMode::Optional };
    DxvkExt extBufferDeviceAddress            = { VK_EXT_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME,              DxvkExtMode::Optional };
    DxvkExt nvPresentMetering                 = { VK_NV_PRESENT_METERING_EXTENSION_NAME,                    DxvkExtMode::Optional };
    // NV-DXVK end

    // NV-DXVK start:
    DxvkExt extShaderAtomicFloat              = { VK_EXT_SHADER_ATOMIC_FLOAT_EXTENSION_NAME,                DxvkExtMode::Optional };
    // NV-DXVK end
  };
  
  /**
   * \brief Instance extensions
   * 
   * Lists all Vulkan extensions that are potentially
   * used by DXVK if supported by the implementation.
   */
  struct DxvkInstanceExtensions {
    DxvkExt extDebugUtils                   = { VK_EXT_DEBUG_UTILS_EXTENSION_NAME,                      DxvkExtMode::Optional };
    DxvkExt khrGetSurfaceCapabilities2      = { VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME,       DxvkExtMode::Optional };
    DxvkExt khrSurface                      = { VK_KHR_SURFACE_EXTENSION_NAME,                          DxvkExtMode::Required };
    DxvkExt khrDeviceProperties2            = { VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME, DxvkExtMode::Required };
    DxvkExt khrDebugReport                  = { VK_EXT_DEBUG_REPORT_EXTENSION_NAME,                     DxvkExtMode::Required };
    // NV-DXVK start: DLFG integration
    DxvkExt khrExternalMemoryCapabilities   = { VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME,     DxvkExtMode::Optional };
    DxvkExt khrExternalSemaphoreCapabilities = { VK_KHR_EXTERNAL_SEMAPHORE_CAPABILITIES_EXTENSION_NAME, DxvkExtMode::Optional };
    // NV-DXVK end
  };
  
}
