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
#include <version.h>

#include "dxvk_instance.h"
#include "dxvk_openvr.h"
#include "dxvk_openxr.h"
#include "dxvk_platform_exts.h"
#include "rtx_render/rtx_options.h"
#include "rtx_render/rtx_mod_manager.h"

// NV-DXVK start: Integrate Aftermath
#include "GFSDK_Aftermath_GpuCrashDump.h"
#include "GFSDK_Aftermath_GpuCrashDumpDecoding.h"
// NV-DXVK end

#include <cstring>
#include <algorithm>

// NV-DXVK start: regex-based filters for VL messages
#include <regex>

#include "../util/util_env.h"
#include "../util/util_string.h"
// NV-DXVK end

// NV-DXVK start: RTXIO
#include "rtx_render/rtx_io.h"
// NV-DXVK end

namespace dxvk {
  bool filterErrorMessages(const char* message) {
    // validation errors that we are currently ignoring --- to fix!
    static const std::vector<const char*> ignoredErrors = {
      // renderpass vs. FB/PSO incompatibilities
      ".*? MessageID = 0x335edc9a .*?",
      ".*? MessageID = 0x8cb637c2 .*?",
      ".*? MessageID = 0x50685725 .*?",

      // Depth comparison without the proper depth comparison bit set in image view
      // Expected behavior according to DXVK 2.1's own validation error bypassing logic
      ".*? MessageID = 0x4b9d1597 .*?",
      ".*? MessageID = 0x534c50ad .*?",

      "^.*?Validation Error: .*? You are adding vk.*? to VkCommandBuffer 0x[0-9a-fA-F]+.*? that is invalid because bound Vk[a-zA-Z0-9]+ 0x[0-9a-fA-F]+.*? was destroyed(.*?)?$",

      // NV SER Extension is not supported by VL
      ".*?SPIR-V module not valid: Invalid capability operand: 5383$",
      ".*?vkCreateShaderModule..: A SPIR-V Capability .Unhandled OpCapability. was declared that is not supported by Vulkan. The Vulkan spec states: pCode must not declare any capability that is not supported by the API, as described by the Capabilities section of the SPIR-V Environment appendix.*?",
      ".*?SPV_NV_shader_invocation_reorder.*?",

      // NV_low_latency extension not supported by VL
      ".*? MessageID = 0x8fe45d78 .*?",

      // Likely a VL bug, started to occur after VK SDK update
      ".*? Timeout waiting for timeline semaphore state to update. This is most likely a validation bug. .*?$",

      // cmdResetQuery has reset commented out since it hits an AV on initial reset - need to update dxvk that handles resets differently
      ".*? After query pool creation, each query must be reset before it is used. Queries must also be reset between uses.$",
      
      // VL bug: it thinks we're using VK_FULL_SCREEN_EXCLUSIVE_APPLICATION_CONTROLLED_EXT when we're not
      ".*? MessageID = 0x769aa5a9 .*?",

      // VL does not know about VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_OPTICAL_FLOW_FEATURES_NV
      ".*? MessageID = 0x901f59ec .*?\\(1000464000\\).*?",
    };

    for(auto& exp : ignoredErrors) {
      std::regex regex(exp);
      std::cmatch res;
      if (std::regex_match(message, res, regex)) {
        return true;
      }
    }

    return false;
  }

  bool filterPerfWarnings(const char* message) {
    static const std::vector<const char*> validationWarningFilters = {
      ".*? For optimal performance VkImage 0x[0-9a-fA-F]+.*? layout should be VK_IMAGE_LAYOUT_.*? instead of GENERAL.$",
    };

    for(auto& exp : validationWarningFilters) {
      std::regex regex(exp);
      std::cmatch res;
      if (std::regex_match(message, res, regex)) {
        return true;
      }
    }

    return false;
  }

  // NV-DXVK start: use EXT_debug_utils
  VKAPI_ATTR VkBool32 VKAPI_CALL
    debugFunction(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
      VkDebugUtilsMessageTypeFlagsEXT messageTypes,
      const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
      void* pUserData)
  {
    const auto pMsg = pCallbackData->pMessage;
    const auto msgStr = str::format("[VK_DEBUG_REPORT] Code ", pCallbackData->messageIdNumber, ": ", pMsg);
    
    if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
      if (!filterErrorMessages(pMsg)) {
        OutputDebugString(msgStr.c_str());
        OutputDebugString("\n\n");       // <-- make easier to see

        Logger::err(msgStr);
      } else {
        Logger::warn(str::format("(waived error) ", msgStr));
      }
    } else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
      if (messageTypes & VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT) {
        if (!filterPerfWarnings(pMsg)) {
          Logger::debug(msgStr);
        }
      } else {
        Logger::warn(msgStr);
      }
    } else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT) {
      Logger::info(msgStr);
    } else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT) {
      Logger::debug(msgStr);
    }

    return VK_FALSE;
  }
  // NV-DXVK end

  void aftermathCrashCallback(const void* pGpuCrashDump, const uint32_t gpuCrashDumpSize, void* pUserData) {
    std::string exeName = env::getExeNameNoSuffix();

    auto t = std::time(nullptr);
    auto tm = *std::localtime(&t);

    std::string path = env::getEnvVar("DXVK_AFTERMATH_DUMP_PATH");

    if (!path.empty() && *path.rbegin() != '/') {
      path += '/';
    }

    std::string dumpFilename = str::format(path, exeName, "_", tm.tm_mday, tm.tm_mon, tm.tm_year, "-", tm.tm_hour, tm.tm_min, tm.tm_sec, "_aftermath.nv-gpudmp");

    Logger::err(str::format("Aftermath detected a crash, writing dump to: ", dumpFilename));

    std::ofstream dumpFile = std::ofstream(str::tows(dumpFilename.c_str()).c_str(), std::ios::binary);
    if (dumpFile.is_open()) {
      dumpFile.write((char*) pGpuCrashDump, gpuCrashDumpSize);
      dumpFile.close();
    } else {
      Logger::warn(str::format("Aftermath was trying to write a GPU dump, but it failed, proposed filename: ", dumpFilename));
    }
  }

  void aftermathShaderDebugInfoCallback(const void* pShaderDebugInfo, const uint32_t shaderDebugInfoSize, void* pUserData) {
    GFSDK_Aftermath_ShaderDebugInfoIdentifier sdiIdentifier;
    GFSDK_Aftermath_GetShaderDebugInfoIdentifier(GFSDK_Aftermath_Version_API, pShaderDebugInfo, shaderDebugInfoSize, &sdiIdentifier);

    std::string exeName = env::getExeNameNoSuffix();

    std::string path = env::getEnvVar("DXVK_AFTERMATH_DUMP_PATH");

    if (!path.empty() && *path.rbegin() != '/') {
      path += '/';
    }

    const std::string shaderDumpInfoDir = path + "shaderDebugInfo/";
    env::createDirectory(shaderDumpInfoDir);

    std::string sdiFilename = str::format(shaderDumpInfoDir, std::uppercase, std::setfill('0'), std::setw(16), std::hex, sdiIdentifier.id[0], "-", sdiIdentifier.id[1], "-0000", ".nvdbg");

    std::ofstream sdiFile = std::ofstream(str::tows(sdiFilename.c_str()).c_str(), std::ios::binary);
    if (sdiFile.is_open()) {
      sdiFile.write((char*) pShaderDebugInfo, shaderDebugInfoSize);
      sdiFile.close();
    } else {
      Logger::warn(str::format("Aftermath requested a shader dump write, but it failed, proposed filename: ", sdiFilename));
    }
  }

  void aftermathMarkerCallback(const void* pMarker, void* pUserData, void** resolvedMarkerData, uint32_t* markerSize) {
    *resolvedMarkerData = (void*)pMarker;
    *markerSize = strlen((const char*) pMarker);
  }

  // NV-DXVK start: validation layer support
  const std::vector<const char*> validationLayers = {
    "VK_LAYER_KHRONOS_validation"
  };

  const bool enableGpuBasedValidationLayers = false;

#ifndef _DEBUG
  const bool enableValidationLayers = false;
#else
  const bool enableValidationLayers = true;
#endif
  // NV-DXVK end

  DxvkInstance::DxvkInstance() {
    Logger::info(str::format("Game: ", env::getExeName()));
    Logger::info(str::format("DXVK_Remix: ", DXVK_VERSION));

    // NV-DXVK start: Decomposed growing config initialization
    initConfigs();
    // NV-DXVK end 

    m_options = DxvkOptions(m_config);
    RtxOptions::Create(m_config);

    // NV-DXVK start: Wait for debugger functionality
    if (m_config.getOption<bool>("dxvk.waitForDebuggerToAttach", false, "DXVK_WAIT_FOR_DEBUGGER_TO_ATTACH"))
      while (!::IsDebuggerPresent())
        ::Sleep(100);
    // NV-DXVK end 

    m_extProviders.push_back(&DxvkPlatformExts::s_instance);
    m_extProviders.push_back(&VrInstance::s_instance);
    m_extProviders.push_back(&DxvkXrProvider::s_instance);

    // NV-DXVK start: RTXIO
#ifdef WITH_RTXIO
    if (RtxIo::enabled()) {
      m_extProviders.push_back(&RtxIoExtensionProvider::s_instance);
    }
#endif
    // NV-DXVK end

    Logger::info("Built-in extension providers:");
    for (const auto& provider : m_extProviders)
      Logger::info(str::format("  ", provider->getName().data()));

    for (const auto& provider : m_extProviders)
      provider->initInstanceExtensions();

    m_vkl = new vk::LibraryFn();
    m_vki = new vk::InstanceFn(true, this->createInstance());

    m_adapters = this->queryAdapters();

    for (const auto& provider : m_extProviders)
      provider->initDeviceExtensions(this);

    for (uint32_t i = 0; i < m_adapters.size(); i++) {
      for (const auto& provider : m_extProviders) {
        m_adapters[i]->enableExtensions(
          provider->getDeviceExtensions(i));
      }

      // NV-DXVK start: Integrate Aftermath
      if (m_options.enableAftermath && !m_aftermathEnabled) {
        GFSDK_Aftermath_Result aftermathResult = GFSDK_Aftermath_EnableGpuCrashDumps(GFSDK_Aftermath_Version_API,
                                                                                     GFSDK_Aftermath_GpuCrashDumpWatchedApiFlags_Vulkan,
                                                                                     GFSDK_Aftermath_GpuCrashDumpFeatureFlags_DeferDebugInfoCallbacks,
                                                                                     &aftermathCrashCallback,
                                                                                     &aftermathShaderDebugInfoCallback,
                                                                                     nullptr,                   // descriptionCb
                                                                                     &aftermathMarkerCallback,  // resolveMarkerCb
                                                                                     nullptr                    // pUserData
                                                                                     );
        if (GFSDK_Aftermath_SUCCEED(aftermathResult)) {
          Logger::info("Aftermath enabled");
          m_aftermathEnabled = true;
        } else {
          Logger::warn(str::format("User requested Aftermath enablement, but it failed.  Code: ", aftermathResult));
          m_options.enableAftermath = (aftermathResult == GFSDK_Aftermath_Result_FAIL_AlreadyInitialized); // Do not disable if already initialized
        }
      }
      // NV-DXVK end
    }

    if (enableValidationLayers) {
      // NV-DXVK start: use EXT_debug_utils
      VkDebugUtilsMessengerCreateInfoEXT info = {};
      info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
      info.pNext = nullptr;
      info.flags = 0;
      info.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
      info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
      info.pfnUserCallback = debugFunction;

      m_vki->vkCreateDebugUtilsMessengerEXT(m_vki->instance(), &info, nullptr, &m_debugUtilsMessenger);
      // NV-DXVK end
    }
  }
  
  
  DxvkInstance::~DxvkInstance() {
    // NV-DXVK start: use EXT_debug_utils
    if (m_debugUtilsMessenger != nullptr) {
      m_vki->vkDestroyDebugUtilsMessengerEXT(m_vki->instance(), m_debugUtilsMessenger, NULL);
    }
    // NV-DXVK end
  }
  
  
  Rc<DxvkAdapter> DxvkInstance::enumAdapters(uint32_t index) const {
    return index < m_adapters.size()
      ? m_adapters[index]
      : nullptr;
  }


  Rc<DxvkAdapter> DxvkInstance::findAdapterByLuid(const void* luid) const {
    for (const auto& adapter : m_adapters) {
      const auto& props = adapter->devicePropertiesExt().coreDeviceId;

      if (props.deviceLUIDValid && !std::memcmp(luid, props.deviceLUID, VK_LUID_SIZE))
        return adapter;
    }

    return nullptr;
  }

  
  Rc<DxvkAdapter> DxvkInstance::findAdapterByDeviceId(uint16_t vendorId, uint16_t deviceId) const {
    for (const auto& adapter : m_adapters) {
      const auto& props = adapter->deviceProperties();

      if (props.vendorID == vendorId
       && props.deviceID == deviceId)
        return adapter;
    }

    return nullptr;
  }

  VkInstance DxvkInstance::createInstance() {
    DxvkInstanceExtensions insExtensions;

    std::vector<DxvkExt*> insExtensionList = {{
      &insExtensions.khrGetSurfaceCapabilities2,
      &insExtensions.khrSurface,
      &insExtensions.khrDeviceProperties2,
    }};

    // Hide VK_EXT_debug_utils behind an environment variable. This extension
    // adds additional overhead to winevulkan
    if (enableValidationLayers || env::getEnvVar("DXVK_PERF_EVENTS") == "1") {
        insExtensionList.push_back(&insExtensions.extDebugUtils);
    }

    DxvkNameSet extensionsEnabled;
    DxvkNameSet extensionsAvailable = DxvkNameSet::enumInstanceExtensions(m_vkl);
    
    if (!extensionsAvailable.enableExtensions(
          insExtensionList.size(),
          insExtensionList.data(),
          extensionsEnabled))
      throw DxvkError("DxvkInstance: Failed to create instance");

    m_extensions = insExtensions;

    // Enable additional extensions if necessary
    for (const auto& provider : m_extProviders)
      extensionsEnabled.merge(provider->getInstanceExtensions());

    // NV-DXVK start: DLFG integration
    std::vector<DxvkExt*> dlfgExtList = { {
      &insExtensions.khrExternalMemoryCapabilities,
      &insExtensions.khrExternalSemaphoreCapabilities
    } };
    extensionsAvailable.enableExtensions(dlfgExtList.size(), dlfgExtList.data(), extensionsEnabled);
    // NV-DXVK end

    DxvkNameList extensionNameList = extensionsEnabled.toNameList();
    
    Logger::info("Enabled instance extensions:");
    this->logNameList(extensionNameList);

    std::string appName = env::getExeName();
    
    VkApplicationInfo appInfo;
    appInfo.sType                 = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pNext                 = nullptr;
    appInfo.pApplicationName      = appName.c_str();
    appInfo.applicationVersion    = 0;
    // NV-DXVK start: custom pEngineName
    appInfo.pEngineName           = "DXVK_NvRemix";
    // NV-DXVK end
    appInfo.engineVersion         = VK_MAKE_VERSION(1, 9, 4);
    // NV-DXVK start: Require Vulkan 1.3
    appInfo.apiVersion            = VK_MAKE_VERSION(1, 3, 0);
    // NV-DXVK end
    
    VkInstanceCreateInfo info;
    info.sType                    = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    info.pNext = nullptr;
    info.flags = 0;
    info.pApplicationInfo = &appInfo;

    // NV-DXVK start: validation layer support/frameview WAR
    std::vector<const char*> layerNames;

    std::vector<VkValidationFeatureEnableEXT> validationFeatureEnables;
    VkValidationFeaturesEXT validationFeatures = {};

    if (enableValidationLayers) {
      if (enableGpuBasedValidationLayers) {
        validationFeatureEnables = { VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_EXT };

        validationFeatures.sType = VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT;
        validationFeatures.pNext = info.pNext;
        validationFeatures.enabledValidationFeatureCount = static_cast<uint32_t>(validationFeatureEnables.size());
        validationFeatures.pEnabledValidationFeatures = validationFeatureEnables.data();

        info.pNext = &validationFeatures;
      }

      for (auto& layer : validationLayers) {
        layerNames.push_back(layer);
      }

      m_validationLayersEnabled = true;
    }
    info.enabledLayerCount = static_cast<uint32_t>(layerNames.size());
    info.ppEnabledLayerNames = layerNames.data();
    
    Logger::info("Enabled Layer Names:");
    for (uint32_t i = 0; i < layerNames.size(); i++)
      Logger::info(str::format("  ", layerNames[i]));
    // NV-DXVK end

    info.enabledExtensionCount    = extensionNameList.count();
    info.ppEnabledExtensionNames  = extensionNameList.names();
    
    VkInstance result = VK_NULL_HANDLE;
    VkResult status = m_vkl->vkCreateInstance(&info, nullptr, &result);

    if (status != VK_SUCCESS)
      throw DxvkError("DxvkInstance::createInstance: Failed to create Vulkan 1.3 instance");

    return result;
  }
  
  
  std::vector<Rc<DxvkAdapter>> DxvkInstance::queryAdapters() {
    uint32_t numAdapters = 0;
    if (m_vki->vkEnumeratePhysicalDevices(m_vki->instance(), &numAdapters, nullptr) != VK_SUCCESS)
      throw DxvkError("DxvkInstance::enumAdapters: Failed to enumerate adapters");
    
    std::vector<VkPhysicalDevice> adapters(numAdapters);
    if (m_vki->vkEnumeratePhysicalDevices(m_vki->instance(), &numAdapters, adapters.data()) != VK_SUCCESS)
      throw DxvkError("DxvkInstance::enumAdapters: Failed to enumerate adapters");

    std::vector<VkPhysicalDeviceProperties> deviceProperties(numAdapters);
    DxvkDeviceFilterFlags filterFlags = 0;

    for (uint32_t i = 0; i < numAdapters; i++) {
      m_vki->vkGetPhysicalDeviceProperties(adapters[i], &deviceProperties[i]);

      if (deviceProperties[i].deviceType != VK_PHYSICAL_DEVICE_TYPE_CPU)
        filterFlags.set(DxvkDeviceFilterFlag::SkipCpuDevices);
    }

    DxvkDeviceFilter filter(filterFlags);
    std::vector<Rc<DxvkAdapter>> result;

    for (uint32_t i = 0; i < numAdapters; i++) {
      if (filter.testAdapter(deviceProperties[i]))
        result.push_back(new DxvkAdapter(m_vki, adapters[i]));
    }
    
    std::stable_sort(result.begin(), result.end(),
      [] (const Rc<DxvkAdapter>& a, const Rc<DxvkAdapter>& b) -> bool {
        static const std::array<VkPhysicalDeviceType, 3> deviceTypes = {{
          VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU,
          VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU,
          VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU,
        }};

        uint32_t aRank = deviceTypes.size();
        uint32_t bRank = deviceTypes.size();

        for (uint32_t i = 0; i < std::min(aRank, bRank); i++) {
          if (a->deviceProperties().deviceType == deviceTypes[i]) aRank = i;
          if (b->deviceProperties().deviceType == deviceTypes[i]) bRank = i;
        }

        return aRank < bRank;
      });
    
    if (result.size() == 0) {
      Logger::warn("DXVK: No adapters found. Please check your "
                   "device filter settings and Vulkan setup.");
    }
    
    return result;
  }
  
  
  void DxvkInstance::logNameList(const DxvkNameList& names) {
    for (uint32_t i = 0; i < names.count(); i++)
      Logger::info(str::format("  ", names.name(i)));
  }
  
  // NV-DXVK start: Custom config loading/logging
  void DxvkInstance::initConfigs() {
    // Load configurations
    // Note: Loading is done in the following order currently, each step overriding values in the previous
    // configuration values when a conflict exist, resulting in the combined "effective" configuration:
    // - Configuration defaults in code (Implicit)
    // - dxvk.conf ("User Config"), can be multiple when set with envvar
    // - Per-application configuration in code ("Built-in Config" from config.cpp)
    // - rtx.conf ("RTX User Config"), can be multiple when set with envvar
    //   - baseGameModPath/rtx.conf (Mod-specific extension of "RTX User Config")
    m_config = Config();
    initConfig<Config::Type_User>();
    initConfig<Config::Type_App>();
    initConfig<Config::Type_RtxUser>();
    initConfig<Config::Type_RtxMod>();

    RtxOption<bool>::updateRtxOptions();
    m_config.logOptions("Effective (combined)");
  }
  
  template<Config::Type type>
  void DxvkInstance::initConfig() {
    const auto name = Config::getDesc(type).name;
    Logger::info(str::format("Init config: ", name));
    std::string configPath = "";
    if constexpr (type == Config::Type_RtxMod) {
      // Handle games that have native mod support, where the base game looks into another folder for mod, 
      // and the new asset path is passed in through the command line
      const auto baseGameModPath = ModManager::getBaseGameModPath(
        m_config.getOption<std::string>("rtx.baseGameModRegex", "", ""),
        m_config.getOption<std::string>("rtx.baseGameModPathRegex", "", ""));
      if (baseGameModPath.empty()) {
        // Skip RtxMod if not present, as it may just pick up a different rtx.mod path
        Logger::info("No base game mod path found. Skipping initilization.");
        return;
      } else {
        Logger::info(str::format("Found base game mod path: ", baseGameModPath));
        configPath = baseGameModPath;
      }
    }
    m_confs[type] = Config::getConfig<type>(configPath);
    m_confs[type].logOptions(name.c_str());
    m_config.merge(m_confs[type]);
    if constexpr (type == Config::Type_App) {
      // Set config so that any rtx option initialized later will use the value in that config object
      // The start-up config contains the values from the code and dxvk.conf, only.
      RtxOption<bool>::setStartupConfig(m_config);
      Logger::info("Set startup config.");
    } else if constexpr ((type == Config::Type_RtxUser) || (type == Config::Type_RtxMod)) {
      // Set custom config after the RTX user config has been merged into the config and
      // update the RTX options. Contains values from rtx.conf
      RtxOption<bool>::setCustomConfig(m_config);
      Logger::info("Set custom config.");
    }
  }
  // NV-DXVK end 
  
}
