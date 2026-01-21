/*
* Copyright (c) 2021-2025, NVIDIA CORPORATION. All rights reserved.
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
#include "rtx_render/rtx_system_info.h"
#include "rtx_render/rtx_options.h"
#include "rtx_render/rtx_mod_manager.h"

// NV-DXVK start: Integrate Aftermath
#include "GFSDK_Aftermath_GpuCrashDump.h"
#include "GFSDK_Aftermath_GpuCrashDumpDecoding.h"
// NV-DXVK end

#include <cstring>
#include <algorithm>
#include <unordered_set>
#include <deque>

// NV-DXVK start: callstack logging support
#include <sstream>
#include <mutex>
#include <windows.h>
#include <dbghelp.h>
// NV-DXVK end

// NV-DXVK start: regex-based filters for VL messages
#include <array>
#include <regex>

#include "../util/xxHash/xxhash.h"
#include "../util/util_env.h"
#include "../util/util_string.h"
// NV-DXVK end

// NV-DXVK start: RTXIO
#include "rtx_render/rtx_io.h"
// NV-DXVK end
#include "rtx_render/rtx_env.h"

// NV-DXVK start: Provide error code on exception
#include <remix/remix_c.h>
// NV-DXVK end
namespace dxvk {

  // NV-DXVK start: debug callback context (stack trace + duplicate filtering)
  struct DxvkDebugUtilsContext {
    struct StackTrace {
      HANDLE process = GetCurrentProcess();
      std::once_flag symInitFlag;
      bool symInitOk = false;
      std::mutex dbghelpMutex;

      void ensureSymbolsAreInitialized() {
        std::call_once(symInitFlag, [this]() {
          std::lock_guard<std::mutex> lock(dbghelpMutex);

          bool initializedByUs = false;

          const BOOL initResult = SymInitialize(process, nullptr, FALSE);
          if (!initResult) {
            const DWORD error = GetLastError();
            if (error == ERROR_INVALID_FUNCTION) {
              // Symbol handler already initialized elsewhere; treat as success.
              symInitOk = true;
            } else {
              Logger::err(str::format("[VK_DEBUG_REPORT] Failed to initialize DbgHelp symbols for stack trace capture. Error: ", error));
              symInitOk = false;
            }
          } else {
            symInitOk = true;
            initializedByUs = true;
          }

          if (symInitOk) {
            SymSetOptions(SymGetOptions() | SYMOPT_LOAD_LINES | SYMOPT_UNDNAME);
            if (initializedByUs) {
              if (!SymRefreshModuleList(process)) {
                Logger::warn(str::format("[VK_DEBUG_REPORT] SymRefreshModuleList failed. Error: ", GetLastError()));
              }
            }
          }
        });
      }
    };

    StackTrace stackTrace;
    std::mutex seenMessagesMutex;
    std::unordered_set<XXH64_hash_t> seenMessageHashes;
    std::deque<XXH64_hash_t> seenMessageOrder;
    static constexpr size_t kMaxSeenMessages = 4096;
    bool loggedEvictionWarning = false;
  };
  // NV-DXVK end

  bool filterErrorMessages(const char* message) {
    // validation errors that we are currently ignoring --- to fix!
    constexpr std::array ignoredErrors{
      // renderpass vs. FB/PSO incompatibilities
      "MessageID = 0x335edc9a",
      "MessageID = 0x8cb637c2",
      "MessageID = 0x50685725",

      // Depth comparison without the proper depth comparison bit set in image view
      // Expected behavior according to DXVK 2.1's own validation error bypassing logic
      "MessageID = 0x4b9d1597",
      "MessageID = 0x534c50ad",

      "You are adding vk.*? to VkCommandBuffer 0x[0-9a-fA-F]+.*? that is invalid because bound Vk[a-zA-Z0-9]+ 0x[0-9a-fA-F]+.*? was destroyed",
// NV-DXVK start:
      // NV SER Extension is not supported by VL
      "SPIR-V module not valid: Invalid capability operand: 5383",
      "vkCreateShaderModule..: A SPIR-V Capability .Unhandled OpCapability. was declared that is not supported by Vulkan. The Vulkan spec states: pCode must not declare any capability that is not supported by the API, as described by the Capabilities section of the SPIR-V Environment appendix",
      "SPV_NV_shader_invocation_reorder",

      // createCuModuleNVX
      "vkCreateCuModuleNVX: value of pCreateInfo->pNext must be NULL. This error is based on the Valid Usage documentation for version [0-9]+ of the Vulkan header.  It is possible that you are using a struct from a private extension or an extension that was added to a later version of the Vulkan header, in which case the use of pCreateInfo->pNext is undefined and may not work correctly with validation enabled The Vulkan spec states: pNext must be NULL",

      // Vulkan 1.4.313.2 VL Errors
      "vkCmdBeginRenderPass\\(\\): dependencyCount is incompatible between VkRenderPass 0x[0-9a-fA-F]+.* \\(from VkRenderPass 0x[0-9a-fA-F]+.*\\) and VkRenderPass 0x[0-9a-fA-F]+.* \\(from VkFramebuffer 0x[0-9a-fA-F]+.*\\), [0-9]+ != [0-9]+.",
      "vkCmdDrawIndexed\\(\\): dependencyCount is incompatible between VkRenderPass 0x[0-9a-fA-F]+.* \\(from VkCommandBuffer 0x[0-9a-fA-F]+.*\\) and VkRenderPass 0x[0-9a-fA-F]+.* \\(from VkPipeline 0x[0-9a-fA-F]+.*\\), [0-9]+ != [0-9]+.",
      "vkCmdDraw\\(\\): dependencyCount is incompatible between VkRenderPass 0x[0-9a-fA-F]+.* \\(from VkCommandBuffer 0x[0-9a-fA-F]+.*\\) and VkRenderPass 0x[0-9a-fA-F]+.* \\(from VkPipeline 0x[0-9a-fA-F]+.*\\), [0-9]+ != [0-9]+.",
      "vkAcquireNextImageKHR\\(\\): Semaphore must not be currently signaled.",
      "vkQueueSubmit\\(\\): pSubmits\\[[0-9]+\\].pWaitSemaphores\\[[0-9]+\\] queue \\(VkQueue 0x[0-9a-fA-F]+.*\\) is waiting on semaphore \\(VkSemaphore 0x[0-9a-fA-F]+.*\\[*\\]\\) that has no way to be signaled.",
      "vkQueuePresentKHR\\(\\): pPresentInfo->pWaitSemaphores\\[[0-9]+\\] queue \\(VkQueue 0x[0-9a-fA-F]+.*\\) is waiting on semaphore \\(VkSemaphore 0x[0-9a-fA-F]+.*\\[Presenter: present semaphore\\]\\) that has no way to be signaled.",
      "vkAcquireNextImageKHR\\(\\): Semaphore must not have any pending operations.",
      "vkQueueSubmit\\(\\): pSubmits\\[[0-9]+\\].pCommandBuffers\\[[0-9]+\\] command buffer VkCommandBuffer 0x[0-9a-fA-F]+.* expects VkImage 0x[0-9a-fA-F]+.* \\(subresource: aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, mipLevel = [0-9]+, arrayLayer = [0-9]+\\) to be in layout VK_IMAGE_LAYOUT_PRESENT_SRC_KHR--instead, current layout is VK_IMAGE_LAYOUT_UNDEFINED.",
      "vkDestroySemaphore\\(\\): can't be called on VkSemaphore 0x[0-9a-fA-F]+.*\\[*\\] that is currently in use by VkQueue 0x[0-9a-fA-F]+.*.",
// NV-DXVK end
    };

    for (auto& exp : ignoredErrors) {
      std::regex regex(exp);
      std::cmatch res;
      if (std::regex_search(message, res, regex)) {
        return true;
      }
    }

    return false;
  }

  bool filterPerfWarnings(const char* message) {
    constexpr std::array validationWarningFilters{
      "For optimal performance VkImage 0x[0-9a-fA-F]+.*? layout should be VK_IMAGE_LAYOUT_.*? instead of GENERAL",
    };

    for (auto& exp : validationWarningFilters) {
      std::regex regex(exp);
      std::cmatch res;
      if (std::regex_search(message, res, regex)) {
        return true;
      }
    }

    return false;
  }

  // NV-DXVK start: capture stack trace for debug messages
  std::string captureStackTrace(DxvkDebugUtilsContext& ctx) {
    ctx.stackTrace.ensureSymbolsAreInitialized();

    if (!ctx.stackTrace.symInitOk) {
      return {};
    }

    std::array<void*, 64> frames{};
    const USHORT captured = RtlCaptureStackBackTrace(
      1, static_cast<ULONG>(frames.size()), frames.data(), nullptr);

    if (captured == 0) {
      return {};
    }

    // captureStackTrace() is called from potentially multiple threads concurrently.
    // Prevent concurrent calls from causing intermittent crashes, corrupted symbol output, or failures like SymFromAddr returning nonsense depending on timing.
    std::lock_guard<std::mutex> dbghelpLock(ctx.stackTrace.dbghelpMutex);

    std::ostringstream stream;

    for (USHORT i = 0; i < captured; ++i) {
      const DWORD64 address = reinterpret_cast<DWORD64>(frames[i]);

      char symbolBuffer[sizeof(SYMBOL_INFO) + MAX_SYM_NAME] = {};
      SYMBOL_INFO* symbol = reinterpret_cast<SYMBOL_INFO*>(symbolBuffer);
      symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
      symbol->MaxNameLen = MAX_SYM_NAME;

      stream << "  [" << i << "] ";

      if (SymFromAddr(ctx.stackTrace.process, address, nullptr, symbol)) {
        stream << symbol->Name;
      } else {
        stream << "<unknown>";
      }

      stream << " (0x" << std::hex << address << std::dec << ")";

      IMAGEHLP_LINE64 line;
      std::memset(&line, 0, sizeof(line));
      line.SizeOfStruct = sizeof(line);
      DWORD lineDisplacement = 0;
      if (SymGetLineFromAddr64(ctx.stackTrace.process, address, &lineDisplacement, &line) && line.FileName != nullptr) {
        stream << " - " << line.FileName << ":" << line.LineNumber;
      }

      stream << "\n";
    }

    return stream.str();
  }

  bool filterDuplicateMessages(DxvkDebugUtilsContext& ctx, const char* pMsg) {
    std::lock_guard<std::mutex> lock(ctx.seenMessagesMutex);

    const XXH64_hash_t msgHash = XXH3_64bits(pMsg, std::strlen(pMsg));

    if (ctx.seenMessageHashes.find(msgHash) != ctx.seenMessageHashes.end()) {
      return true;
    }

    if (ctx.seenMessageHashes.empty()) {
      ctx.seenMessageHashes.reserve(ctx.kMaxSeenMessages + 1);  // Allow for one extra insertion before eviction logic kicks in
    }

    ctx.seenMessageHashes.insert(msgHash);
    ctx.seenMessageOrder.push_back(msgHash);

    if (ctx.seenMessageOrder.size() > ctx.kMaxSeenMessages) {
      if (!ctx.loggedEvictionWarning) {
        Logger::info("[VK_DEBUG_REPORT] Maximum validation layer duplicate message filtering reached. Older messages may appear again.");
        ctx.loggedEvictionWarning = true;
      }
    }

    while (ctx.seenMessageOrder.size() > ctx.kMaxSeenMessages) {
      const XXH64_hash_t oldest = ctx.seenMessageOrder.front();
      ctx.seenMessageOrder.pop_front();
      ctx.seenMessageHashes.erase(oldest);
    }

    return false;
  }

  // NV-DXVK end

  // NV-DXVK start: use EXT_debug_utils
  VKAPI_ATTR VkBool32 VKAPI_CALL
    debugFunction(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
      VkDebugUtilsMessageTypeFlagsEXT messageTypes,
      const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
      void* pUserData) {
    DxvkDebugUtilsContext& ctx = *reinterpret_cast<DxvkDebugUtilsContext*>(pUserData);
    const auto pMsg = pCallbackData->pMessage;
    std::string msgStr = str::format("[VK_DEBUG_REPORT] Code ", pCallbackData->messageIdNumber, ": ", pMsg);

    const bool shouldFilterErrors = true; 
    const bool showFilteredErrorsAsWarnings = false; // Set it to true to ouput the waived errors as warnings rather than skipping them entirely
    const bool shouldFilterDuplicateMessages = true;

    bool isWaivedError = 
      shouldFilterErrors && 
      (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) &&
      filterErrorMessages(pMsg);

    // Only filter duplicate messages that end up being shown since duplicate filtering is constrained in size for performance
    if (!isWaivedError || showFilteredErrorsAsWarnings) {    
      if (shouldFilterDuplicateMessages && filterDuplicateMessages(ctx, pMsg)) {
        return VK_FALSE;
      }
    }

    if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
      if (!isWaivedError) {
        if (RtxOptions::logCallstacksOnValidationLayerErrors()) {
          const std::string stackTrace = captureStackTrace(ctx);
          if (!stackTrace.empty()) {
            msgStr = str::format(msgStr, "\n[VK_DEBUG_REPORT] Callstack:\n", stackTrace, "\n");
          }
        }
        
        OutputDebugString(msgStr.c_str());
        OutputDebugString("\n\n");       // <-- make easier to see

        Logger::err(msgStr);
      } else {
        if (showFilteredErrorsAsWarnings) {
          Logger::warn(str::format("(waived error) ", msgStr));
        }
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

  DxvkInstance::DxvkInstance() {
    Logger::info(str::format("Game: ", env::getExeName()));
    Logger::info(str::format("DXVK_Remix: ", DXVK_VERSION));

    // NV-DXVK start: Log System Info Report
    RtxSystemInfo::logReport();
    // NV-DXVK end 

    // NV-DXVK start: Decomposed growing config initialization
    // TODO[REMIX-4106] we need to avoid re-parsing the same config files when dxvk_instance is recreated.
    initConfigs();
    // NV-DXVK end 

    m_options = DxvkOptions(m_config);
    RtxOptions::Create(m_config);

    // NV-DXVK start: Wait for debugger functionality
    if (m_config.getOption<bool>("dxvk.waitForDebuggerToAttach", false, "DXVK_WAIT_FOR_DEBUGGER_TO_ATTACH"))
      while (!::IsDebuggerPresent())
        ::Sleep(100);
    // NV-DXVK end 

    // NV-DXVK start: Workaround hybrid AMD iGPU+Nvidia dGPU device enumeration issues
    if (RtxOptions::disableAMDSwitchableGraphics()) {
      // Note: The VK_LAYER_AMD_swichable_graphics layer in older AMD drivers is somewhat buggy and seems to filter away all non-AMD devices even if this means leaving
      // an empty device list for Vulkan despite having a GPU on the machine. In turn this causes a subsequent call to vkEnumeratePhysicalDevices to return VK_INCOMPLETE
      // for some reason (which previously was considered an error, not that Remix would be able to launch anyways though due to having no devices reported). This was
      // reported many times by users using some sort of AMD iGPU and Nvidia dGPU setup (such as a laptop) combined with older AMD integrated graphics drivers (e.g. around
      // early 2020).
      //
      // Disabling the switchable graphics layer works around this issue, though may in rare cases cause undesirable behavior if one actually wishes to use
      // the layer to control which devices are exposed to an application, which is why Remix provides a way to disable this option. Generally though this should do the
      // right thing as on systems with Nvidia GPUs Nvidia Optimus itself will already handle selecting an integrated or dedicated GPU for an application, and on systems
      // with both an AMD iGPU and dGPU Remix will prefer the dedicated GPU which is the generally desired behavior (unless the user actually wants to run on the iGPU,
      // in which case this workaround will need to be disabled).
      //
      // If this really becomes a problem, a better approach may be to only enable this override if enumerating devices results in 0 devices rather than setting it upfront,
      // but other large projects set this upfront unconditionally as well, so for now it's probably fine as doing a retry would require re-creating the instance which is
      // not super trivial to do with how Remix's code is set up currently.
      //
      // For more information, see:
      // https://github.com/KhronosGroup/Vulkan-Loader/issues/552
      // https://github.com/godotengine/godot/issues/57708
      // https://nvidia.custhelp.com/app/answers/detail/a_id/5182/~/unable-to-launch-vulkan-apps%2Fgame-on-notebooks-with-amd-radeon-igpus
      env::setEnvVar("DISABLE_LAYER_AMD_SWITCHABLE_GRAPHICS_1", "1");
    }
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

    if (RtxOptions::areValidationLayersEnabled()) {
      // NV-DXVK start: use EXT_debug_utils
      VkDebugUtilsMessengerCreateInfoEXT info = {};
      info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
      info.pNext = nullptr;
      info.flags = 0;
      info.messageSeverity =
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
      info.messageType =
        VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
      info.pfnUserCallback = debugFunction;
      info.pUserData = nullptr;

      if (!m_debugUtilsContext) {
        m_debugUtilsContext = std::make_unique<DxvkDebugUtilsContext>();
        Logger::info("[VK_DEBUG_REPORT] Enabling validation layer duplicate message filtering.");
      }

      info.pUserData = m_debugUtilsContext.get();

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
    const auto areValidationLayersEnabled = RtxOptions::areValidationLayersEnabled();
    const auto enableValidationLayerExtendedValidation = RtxOptions::enableValidationLayerExtendedValidation();

    // Attempt to enable required Instance Extensions

    DxvkInstanceExtensions insExtensions;

    std::vector<DxvkExt*> insExtensionList = {{
      &insExtensions.khrGetSurfaceCapabilities2,
      &insExtensions.khrSurface,
      &insExtensions.khrDeviceProperties2,
    }};

    // Hide VK_EXT_debug_utils behind an environment variable. This extension
    // adds additional overhead to winevulkan
    if (areValidationLayersEnabled || env::getEnvVar("DXVK_PERF_EVENTS") == "1") {
      insExtensionList.push_back(&insExtensions.extDebugUtils);
    }

    DxvkNameSet extensionsEnabled;
    DxvkNameSet extensionsAvailable = DxvkNameSet::enumInstanceExtensions(m_vkl);
    
    if (!extensionsAvailable.enableExtensions(
        insExtensionList.size(),
        insExtensionList.data(),
        extensionsEnabled)) {
      Logger::err("Unable to find all required Vulkan extensions for instance creation.");

      // Note: Once macro used to ensure this message is only displayed to the user once in case multiple instances are created.
      ONCE(messageBox("Your GPU driver doesn't support the required instance extensions to run RTX Remix.\nSee the log file 'rtx-remix/logs/remix-dxvk.log' for which extensions are unsupported and try updating your driver.\nThe game will exit now.", "RTX Remix - Instance Extension Error!", MB_OK));

      // NV-DXVK start: Provide error code on exception
      throw DxvkErrorWithId(REMIXAPI_ERROR_CODE_HRESULT_DXVK_INSTANCE_EXTENSION_FAIL, "DxvkInstance: Failed to create instance, instance does not support all required extensions.");
      // NV-DXVK end
    }

    m_extensions = insExtensions;

    // Attempt to enable additional extensions if necessary

    for (const auto& provider : m_extProviders)
      extensionsEnabled.merge(provider->getInstanceExtensions());

    // NV-DXVK start: DLFG integration
    std::vector<DxvkExt*> dlfgExtList = { {
      &insExtensions.khrExternalMemoryCapabilities,
      &insExtensions.khrExternalSemaphoreCapabilities
    } };
    extensionsAvailable.enableExtensions(dlfgExtList.size(), dlfgExtList.data(), extensionsEnabled);
    // NV-DXVK end

    // NV-DXVK start: Add debug utils extension for Remix
    if (extensionsAvailable.supports(VK_EXT_DEBUG_UTILS_EXTENSION_NAME))
      extensionsEnabled.add(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    // NV-DXVK end

    DxvkNameList extensionNameList = extensionsEnabled.toNameList();
    
    Logger::info("Enabled instance extensions:");
    this->logNameList(extensionNameList);

    const auto appName = env::getExeName();
    
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
    appInfo.apiVersion            = VK_MAKE_VERSION(1, 4, 0);
    // NV-DXVK end
    
    VkInstanceCreateInfo info;
    info.sType                    = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    info.pNext = nullptr;
    info.flags = 0;
    info.pApplicationInfo = &appInfo;

    // NV-DXVK start: Validation layer support
    std::vector<const char*> layerNames;

    // Add validation layers if enabled

    // Note: These variables are defined outside the validation layer enable scope as their pointers must remain valid until
    // instance creation.
    VkLayerSettingsCreateInfoEXT validationLayerSettingsCreateInfo;
    constexpr auto khronosValidationLayerName{ "VK_LAYER_KHRONOS_validation" };
    const VkBool32 trueValidationLayerSetting{ VK_TRUE };
    const std::array<VkLayerSettingEXT, 3> validationLayerSettings{ {
      // Note: Enable validation settings disabled by default in the Khronos Validation Layer, currently synchronization
      // validation, GPU assisted validation and best practices.
      // See this documentation for more information: https://vulkan.lunarg.com/doc/view/latest/windows/khronos_validation_layer.html
      { khronosValidationLayerName, "validate_sync", VK_LAYER_SETTING_TYPE_BOOL32_EXT, 1, &trueValidationLayerSetting },
      { khronosValidationLayerName, "gpuav_enable", VK_LAYER_SETTING_TYPE_BOOL32_EXT, 1, &trueValidationLayerSetting },
      { khronosValidationLayerName, "validate_best_practices", VK_LAYER_SETTING_TYPE_BOOL32_EXT, 1, &trueValidationLayerSetting },
    } };

    if (areValidationLayersEnabled) {
      // Configure validation layers if extended validation is desired

      if (enableValidationLayerExtendedValidation) {
        validationLayerSettingsCreateInfo.sType = VK_STRUCTURE_TYPE_LAYER_SETTINGS_CREATE_INFO_EXT;
        validationLayerSettingsCreateInfo.pNext = info.pNext;
        validationLayerSettingsCreateInfo.settingCount = static_cast<std::uint32_t>(validationLayerSettings.size());
        validationLayerSettingsCreateInfo.pSettings = validationLayerSettings.data();

        info.pNext = &validationLayerSettingsCreateInfo;
      }

      // Add desired validation layers to the array of layers to enable

      layerNames.push_back(khronosValidationLayerName);
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

    if (status != VK_SUCCESS) {
      Logger::err(str::format("Unable to create a Vulkan instance, error code: ", status, "."));

      const auto instanceCreationFailureDialogMessage = str::format(
        "Vulkan Instance creation failed with error code: ", status, ".\nTry updating your driver and reporting this as a bug if the problem persists.\nThe game will exit now.");

      // Note: Once macro used to ensure this message is only displayed to the user once in case multiple instances are created.
      ONCE(messageBox(instanceCreationFailureDialogMessage.c_str(), "RTX Remix - Instance Creation Error!", MB_OK));

      // NV-DXVK start: Provide error code on exception
      throw DxvkErrorWithId(REMIXAPI_ERROR_CODE_HRESULT_VK_CREATE_INSTANCE_FAIL, "DxvkInstance::createInstance: Failed to create a Vulkan 1.3 instance");
      // NV-DXVK end
    }

    return result;
  }
  
  
  std::vector<Rc<DxvkAdapter>> DxvkInstance::queryAdapters() {
    // Enumerate Physical Devices

    uint32_t numAdapters = 0;
    const auto enumeratePhysicalDeviceCountResult = m_vki->vkEnumeratePhysicalDevices(m_vki->instance(), &numAdapters, nullptr);

    if (enumeratePhysicalDeviceCountResult != VK_SUCCESS) {
      // Note: No message box here as this case is not expected to happen in normal operation.

      throw DxvkError("DxvkInstance::enumAdapters: Failed to enumerate physical device count");
    }
    
    std::vector<VkPhysicalDevice> adapters(numAdapters);
    const auto enumeratePhysicalDevicesResult = m_vki->vkEnumeratePhysicalDevices(m_vki->instance(), &numAdapters, adapters.data());

    if (enumeratePhysicalDevicesResult != VK_SUCCESS) {
      // Note: VK_INCOMPLETE can be returned potentially if the number of devices changed between calls, or occasionally in some implementations
      // despite passing the correct queried value back into the function. Since Vulkan considers this code a success code technically, it is best
      // to carry on and only warn that some devices may be missed rather than treating this as a hard error.
      if (enumeratePhysicalDevicesResult == VK_INCOMPLETE) {
        Logger::warn("Physical Device enumeration returned VK_INCOMPLETE, indicating that not all devices may have been enumerated. This usually shouldn't happen and may be indicative of a Vulkan driver issue.");
      } else {
        // Note: No message box here as this case is not expected to happen in normal operation.

        throw DxvkError("DxvkInstance::enumAdapters: Failed to enumerate physical devices");
      }
    }

    // Filter Physical Devices

    std::vector<VkPhysicalDeviceProperties> deviceProperties(numAdapters);
    DxvkDeviceFilterFlags filterFlags = 0;

    for (uint32_t i = 0; i < numAdapters; i++) {
      m_vki->vkGetPhysicalDeviceProperties(adapters[i], &deviceProperties[i]);

      // Skip CPU or Integrated GPU devices if any other device type is present
      // Note: Originally DXVK only did this for CPU devices, but Remix extends this logic to include
      // Integrated GPUs too. This is because applications have little information about which device
      // is best when exposed as adapters through DirectX and cannot be expected to make a good selection
      // on their own. In the case of Remix, if a dedicated GPU is installed on a system it should almost
      // always be prioritized over integrated GPUs, as some applications will attempt to select a
      // non-default adapter and often times end up severely degrading performance by unknowingly selecting
      // one corresponding to an integrated GPU.

      if (deviceProperties[i].deviceType != VK_PHYSICAL_DEVICE_TYPE_CPU) {
        filterFlags.set(DxvkDeviceFilterFlag::SkipCpuDevices);
      }

      if (deviceProperties[i].deviceType != VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) {
        filterFlags.set(DxvkDeviceFilterFlag::SkipIntegratedGPUDevices);
      }
    }

    DxvkDeviceFilter filter(filterFlags);
    std::vector<Rc<DxvkAdapter>> result;

    for (uint32_t i = 0; i < numAdapters; i++) {
      if (filter.testAdapter(deviceProperties[i]))
        result.push_back(new DxvkAdapter(m_vki, adapters[i]));
    }

    // Rank Physical Devices
    // Note: Generally only the highest ranked adapter is relevant as it will be selected when applications use D3DADAPTER_DEFAULT
    // which is reasonably common. Otherwise, the ranking isn't as important as applications only have a minor amount of information
    // about the properties of each adapter when querying through DirectX and the order won't matter anyways usually if applications are
    // doing their own sort of ranking system.
    
    std::stable_sort(result.begin(), result.end(),
      [] (const Rc<DxvkAdapter>& a, const Rc<DxvkAdapter>& b) -> bool {
        constexpr std::array<VkPhysicalDeviceType, 3> deviceTypes{{
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

    RtxOptionImpl::addRtxOptionLayer("quality.conf", (uint32_t) RtxOptionLayer::SystemLayerPriority::Quality, true, 1.0f, 0.1f);
    Logger::info("Set quality configs.");

    RtxOptionImpl::addRtxOptionLayer("user.conf", (uint32_t) RtxOptionLayer::SystemLayerPriority::USER, true, 1.0f, 0.1f);
    Logger::info("Set user realtime configs.");

    RtxOptionManager::initializeRtxOptions();
    for (const auto& [unusedLayerKey, optionLayerPtr] : RtxOptionImpl::getRtxOptionLayerMap()) {
      RtxOptionManager::addRtxOptionLayer(*optionLayerPtr);
    }

    m_config.logOptions("Effective (combined)");

    // Output environment variable info
    // Todo: This being here is kinda not great as this results in the Environment variables being parsed 3 times
    // which is quite redundant. Unfortunately this logging can't go in Config::getOption as this function is called
    // twice (again, redundant) resulting in duplicate messages. Ideally this system should be refactored to get all the
    // relevant environment variable values for the desired RtxOptions in a loop like this, and then use those when
    // setting the options up to avoid redundantly making a ton of syscalls. Luckily this code only happens in loading
    // so it is not a huge performance overhead, and the value of seeing which environment variables are overriding options
    // is currently more valuable (since they continue to cause problems when unseen in the log).

    bool firstEnvironmentOverride = true;

    for (auto&& option : RtxOptionImpl::getGlobalRtxOptionMap()) {
      const auto optionName = option.second->getFullName();
      const auto envVarName = option.second->environment;

      if (envVarName) {
        const std::string& envVarValue = env::getEnvVar(envVarName);

        if (envVarValue != "") {
          // Note: Only print out the section header if there's at least one environment variable override.
          if (firstEnvironmentOverride) {
            Logger::info("Environment variable option overrides:");

            firstEnvironmentOverride = false;
          }

          Logger::info(str::format("  ", optionName, " overridden by environment variable: ", envVarName, "=", envVarValue));
        }
      }
    }
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
        Logger::info("No base game mod path found. Skipping initialization.");
        return;
      } else {
        Logger::info(str::format("Found base game mod path: ", baseGameModPath));
        configPath = baseGameModPath;
      }
    }
    m_confs[type] = Config::getConfig<type>(configPath);
    m_confs[type].logOptions(name.c_str());
    m_config.merge(m_confs[type]);

    if constexpr (type == Config::Type_User) {
      RtxOptionImpl::addRtxOptionLayer("dxvk.conf", (uint32_t) RtxOptionLayer::SystemLayerPriority::DxvkConf, true, 1.0f, 0.1f, &m_confs[type]);
      Logger::info("Set user specific config.");
    } else if constexpr (type == Config::Type_App) {
      // Set config so that any rtx option initialized later will use the value in that config object
      // The start-up config contains the values from the code and dxvk.conf, only.
      RtxOptionManager::setStartupConfig(m_config);
      RtxOptionImpl::addRtxOptionLayer("<APPLICATION DEFAULT>", (uint32_t) RtxOptionLayer::SystemLayerPriority::Default, true, 1.0f, 0.1f, &m_confs[type]);
      Logger::info("Set startup config.");
    } else if constexpr ((type == Config::Type_RtxUser) || (type == Config::Type_RtxMod)) {
      // Set custom config after the RTX user config has been merged into the config and
      // update the RTX options. Contains values from rtx.conf
      RtxOptionManager::setCustomConfig(m_config);
      RtxOptionImpl::addRtxOptionLayer("rtx.conf", (uint32_t) RtxOptionLayer::SystemLayerPriority::RtxConf, true, 1.0f, 0.1f, nullptr);
      Logger::info("Set custom config.");
    }
  }
  // NV-DXVK end 
  
}
