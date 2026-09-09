/*
* Copyright (c) 2026, NVIDIA CORPORATION. All rights reserved.
*
* Permission is hereby granted, free of charge, to any person obtaining a
* copy of this software and associated documentation files (the "Software"),
* to deal in the Software without restriction, including without limitation
* the rights to use, copy, modify, merge, publish, distribute, sublicense,
* and/or sell copies of the Software and to permit persons to whom the
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
#include "util_aftermath.h"

#include <atomic>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <vulkan/vulkan.h>

#include "GFSDK_Aftermath_GpuCrashDump.h"
#include "GFSDK_Aftermath_GpuCrashDumpDecoding.h"

namespace dxvk {

  namespace {

    std::mutex g_aftermathShaderRegistryMutex;
    std::unordered_map<uint64_t, std::string> g_aftermathShaderRegistry;
    std::atomic<bool> g_aftermathShaderRegistrationEnabled { false };

    const char* describeAftermathDeviceStatus(GFSDK_Aftermath_Device_Status status) {
      switch (status) {
        case GFSDK_Aftermath_Device_Status_Timeout: return "Timeout";
        case GFSDK_Aftermath_Device_Status_OutOfMemory: return "OutOfMemory";
        case GFSDK_Aftermath_Device_Status_PageFault: return "PageFault";
        case GFSDK_Aftermath_Device_Status_Stopped: return "Stopped";
        case GFSDK_Aftermath_Device_Status_Reset: return "Reset";
        case GFSDK_Aftermath_Device_Status_DmaFault: return "DmaFault";
        case GFSDK_Aftermath_Device_Status_Active: return "Active";
        case GFSDK_Aftermath_Device_Status_Unknown:
        default: return "Unknown";
      }
    }

    const char* describeAftermathPageFaultAccessType(GFSDK_Aftermath_AccessType accessType) {
      switch (accessType) {
        case GFSDK_Aftermath_AccessType_Read: return "read";
        case GFSDK_Aftermath_AccessType_Write: return "write";
        case GFSDK_Aftermath_AccessType_Atomic: return "atomic";
        default: return "unknown access";
      }
    }

    const char* describeAftermathFaultType(GFSDK_Aftermath_FaultType faultType) {
      switch (faultType) {
        case GFSDK_Aftermath_FaultType_AddressTranslationError: return "address translation error";
        case GFSDK_Aftermath_FaultType_IllegalAccessError: return "illegal access";
        default: return "unknown fault type";
      }
    }

    const char* describeAftermathEngine(GFSDK_Aftermath_Engine engine) {
      switch (engine) {
        case GFSDK_Aftermath_Engine_Graphics: return "graphics";
        case GFSDK_Aftermath_Engine_Display: return "display";
        case GFSDK_Aftermath_Engine_CopyEngine: return "copy engine";
        case GFSDK_Aftermath_Engine_VideoDecoder: return "video decoder";
        case GFSDK_Aftermath_Engine_VideoEncoder: return "video encoder";
        case GFSDK_Aftermath_Engine_Other: return "other";
        default: return "unknown engine";
      }
    }

    const char* describeAftermathClient(GFSDK_Aftermath_Client client) {
      switch (client) {
        case GFSDK_Aftermath_Client_HostInterface: return "host interface";
        case GFSDK_Aftermath_Client_FrontEnd: return "front end";
        case GFSDK_Aftermath_Client_PrimitiveDistributor: return "primitive distributor";
        case GFSDK_Aftermath_Client_GraphicsProcessingCluster: return "graphics processing cluster";
        case GFSDK_Aftermath_Client_PolymorphEngine: return "polymorph engine";
        case GFSDK_Aftermath_Client_RasterEngine: return "raster engine";
        case GFSDK_Aftermath_Client_Rasterizer2D: return "rasterizer 2d";
        case GFSDK_Aftermath_Client_RenderOutputUnit: return "render output unit";
        case GFSDK_Aftermath_Client_TextureProcessingCluster: return "texture processing cluster";
        case GFSDK_Aftermath_Client_CopyEngine: return "copy engine";
        case GFSDK_Aftermath_Client_VideoDecoder: return "video decoder";
        case GFSDK_Aftermath_Client_VideoEncoder: return "video encoder";
        case GFSDK_Aftermath_Client_Other: return "other";
        default: return "unknown client";
      }
    }

    const char* describeAftermathShaderType(GFSDK_Aftermath_ShaderType shaderType) {
      switch (shaderType) {
        case GFSDK_Aftermath_ShaderType_Vertex: return "vertex";
        case GFSDK_Aftermath_ShaderType_Tessellation_Control: return "tessellation control";
        case GFSDK_Aftermath_ShaderType_Tessellation_Evaluation: return "tessellation evaluation";
        case GFSDK_Aftermath_ShaderType_Geometry: return "geometry";
        case GFSDK_Aftermath_ShaderType_Fragment: return "fragment";
        case GFSDK_Aftermath_ShaderType_Compute: return "compute";
        case GFSDK_Aftermath_ShaderType_RayTracing_RayGeneration: return "ray generation";
        case GFSDK_Aftermath_ShaderType_RayTracing_Miss: return "ray miss";
        case GFSDK_Aftermath_ShaderType_RayTracing_Intersection: return "ray intersection";
        case GFSDK_Aftermath_ShaderType_RayTracing_AnyHit: return "ray any-hit";
        case GFSDK_Aftermath_ShaderType_RayTracing_ClosestHit: return "ray closest-hit";
        case GFSDK_Aftermath_ShaderType_RayTracing_Callable: return "ray callable";
        case GFSDK_Aftermath_ShaderType_RayTracing_Internal: return "ray tracing internal";
        case GFSDK_Aftermath_ShaderType_Mesh: return "mesh";
        case GFSDK_Aftermath_ShaderType_Task: return "task";
        case GFSDK_Aftermath_ShaderType_Unknown:
        default: return "unknown";
      }
    }

    // Registered shaders are listed by name, the rest only counted.
    void collectActiveShaders(GFSDK_Aftermath_GpuCrashDump_Decoder decoder, AftermathCrashInfo& info) {
      uint32_t shaderCount = 0;
      if (!GFSDK_Aftermath_SUCCEED(GFSDK_Aftermath_GpuCrashDump_GetActiveShadersInfoCount(decoder, &shaderCount))
          || shaderCount == 0) {
        return;
      }

      std::vector<GFSDK_Aftermath_GpuCrashDump_ShaderInfo> shaderInfos(shaderCount);
      if (!GFSDK_Aftermath_SUCCEED(GFSDK_Aftermath_GpuCrashDump_GetActiveShadersInfo(decoder, shaderCount, shaderInfos.data()))) {
        return;
      }

      // Aftermath reports one entry per in-flight shaderInstance, so the same shader repeats.
      std::unordered_set<uint64_t> seenShaderHashes;
      std::lock_guard<std::mutex> lock(g_aftermathShaderRegistryMutex);
      for (const auto& shaderInfo : shaderInfos) {
        if (!seenShaderHashes.insert(shaderInfo.shaderHash).second) {
          continue;
        }

        GFSDK_Aftermath_ShaderBinaryHash shaderHash = {};
        if (GFSDK_Aftermath_SUCCEED(GFSDK_Aftermath_GetShaderHashForShaderInfo(decoder, &shaderInfo, &shaderHash))) {
          auto it = g_aftermathShaderRegistry.find(shaderHash.hash);
          if (it != g_aftermathShaderRegistry.end()) {
            info.activeShaders.push_back({ describeAftermathShaderType(shaderInfo.shaderType), it->second });
            continue;
          }
        }

        if (shaderInfo.isInternal) {
          ++info.driverInternalShaderCount;
        } else {
          ++info.unregisteredShaderCount;
        }
      }
    }

  }

  void setAftermathShaderRegistrationEnabled(bool enabled) {
    g_aftermathShaderRegistrationEnabled.store(enabled, std::memory_order_relaxed);
  }

  void registerAftermathShader(const uint32_t* spirvCode, size_t spirvSizeBytes, const std::string& debugName) {
    if (!g_aftermathShaderRegistrationEnabled.load(std::memory_order_relaxed)) {
      return;
    }

    GFSDK_Aftermath_SpirvCode code = {};
    code.pData = const_cast<uint32_t*>(spirvCode);
    code.size = static_cast<uint32_t>(spirvSizeBytes);

    GFSDK_Aftermath_ShaderBinaryHash shaderHash = {};
    if (!GFSDK_Aftermath_SUCCEED(GFSDK_Aftermath_GetShaderHashSpirv(GFSDK_Aftermath_Version_API, &code, &shaderHash))) {
      return;
    }

    std::lock_guard<std::mutex> lock(g_aftermathShaderRegistryMutex);
    g_aftermathShaderRegistry[shaderHash.hash] = debugName;
  }

  AftermathCrashInfo decodeAftermathCrashInfo(const void* pGpuCrashDump, const uint32_t gpuCrashDumpSize) {
    AftermathCrashInfo info;

    GFSDK_Aftermath_GpuCrashDump_Decoder decoder = nullptr;
    if (!GFSDK_Aftermath_SUCCEED(GFSDK_Aftermath_GpuCrashDump_CreateDecoder(
            GFSDK_Aftermath_Version_API, pGpuCrashDump, gpuCrashDumpSize, &decoder))) {
      info.reason = "Aftermath decode failed";
      return info;
    }
    // Guards against leaking the decoder if anything below throws (e.g. a vector allocation).
    struct DecoderGuard {
      GFSDK_Aftermath_GpuCrashDump_Decoder decoder;
      ~DecoderGuard() { GFSDK_Aftermath_GpuCrashDump_DestroyDecoder(decoder); }
    } decoderGuard { decoder };

    info.reason = "Unknown GPU crash";
    GFSDK_Aftermath_GpuCrashDump_DeviceInfo deviceInfo = {};
    if (GFSDK_Aftermath_SUCCEED(GFSDK_Aftermath_GpuCrashDump_GetDeviceInfo(decoder, &deviceInfo))) {
      info.reason = describeAftermathDeviceStatus(deviceInfo.status);
      info.adapterReset = deviceInfo.adapterReset;
      info.engineReset = deviceInfo.engineReset;

      if (deviceInfo.status == GFSDK_Aftermath_Device_Status_PageFault) {
        GFSDK_Aftermath_GpuCrashDump_PageFaultInfo pageFaultInfo = {};
        if (GFSDK_Aftermath_SUCCEED(GFSDK_Aftermath_GpuCrashDump_GetPageFaultInfo(decoder, &pageFaultInfo))) {
          info.pageFaultAccessType = describeAftermathPageFaultAccessType(pageFaultInfo.accessType);
          info.pageFaultType = describeAftermathFaultType(pageFaultInfo.faultType);
          info.pageFaultEngine = describeAftermathEngine(pageFaultInfo.engine);
          info.pageFaultClient = describeAftermathClient(pageFaultInfo.client);
          info.pageFaultingGpuVA = pageFaultInfo.faultingGpuVA;
          info.hasPageFaultResourceInfo = pageFaultInfo.bHasResourceInfo;
          if (info.hasPageFaultResourceInfo) {
            const auto& res = pageFaultInfo.resourceInfo;
            auto& dst = info.pageFaultResourceInfo;
            dst.gpuVa = res.gpuVa;
            dst.size = res.size;
            dst.width = res.width;
            dst.height = res.height;
            dst.depth = res.depth;
            dst.mipLevels = res.mipLevels;
            dst.format = res.format;
            dst.isBufferHeap = res.bIsBufferHeap;
            dst.isStaticTextureHeap = res.bIsStaticTextureHeap;
            dst.isRenderTargetOrDepthStencilViewHeap = res.bIsRenderTargetOrDepthStencilViewHeap;
            dst.isPlacedResource = res.bPlacedResource;
            dst.wasDestroyed = res.bWasDestroyed;
            dst.createDestroyTickCount = res.createDestroyTickCount;
          }
        }
      }
    }

    collectActiveShaders(decoder, info);

    return info;
  }

}
