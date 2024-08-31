#pragma once

#include <cstring>

#include "dxvk_include.h"
#include "../tracy/Tracy.hpp"
#include "../tracy/TracyVulkan.hpp"

#define ScopedCpuProfileZoneN(name) \
        ZoneScopedN(name)

#define ScopedCpuProfileZone() \
        ScopedCpuProfileZoneN(__FUNCTION__)

#define ProfilerPlotValue(name, val) \
        TracyPlot(name, val)

#define ProfilerPlotValueF32(name, val) \
        TracyPlot(name, float(val))

#define ProfilerPlotValueF64(name, val) \
        TracyPlot(name, double(val))

#define ProfilerPlotValueI64(name, val) \
        TracyPlot(name, int64_t(val))

#define ScopedGpuProfileZone(ctx, name) \
        ScopedCpuProfileZoneN(name); \
        TracyVkZone((ctx)->getDevice()->queues().graphics.tracyCtx, (ctx)->getCmdBuffer(DxvkCmdBuffer::ExecBuffer), name); \
        __ScopedAnnotation __scopedAnnotation(ctx, name)

#define ScopedGpuProfileZoneQ(device, cmdbuf, queue, name) \
        ScopedCpuProfileZoneN(name); \
        TracyVkZone((device)->queues().queue.tracyCtx, cmdbuf, name); \
        __ScopedQueueAnnotation __scopedQueueAnnotation(device, cmdbuf, name)

#define ScopedGpuProfileZone_Present(device, cmdbuf, name) \
        ScopedGpuProfileZoneQ(device, cmdbuf, present, name)

#ifdef REMIX_DEVELOPMENT
  // NOTE: Since this uses dynamic strings to write variables to profiler, it can be more expensive than constexpr above, and so is only enabled in REMIX_DEVELOPMENT
  //       even still, it should only be used when absolutely necessary.  Ideally the cost of profiling is minimal for most representative results.
  // Note: *Z variants take a C-style null-terminated string, normal variants take something std::string-esque with a data and length member.
  #define ScopedCpuProfileZoneDynamic(name) \
          ScopedCpuProfileZone(); \
          ZoneText((name).data(), (name).length());
  #define ScopedGpuProfileZoneDynamicZ(ctx, name) \
          ScopedCpuProfileZone(); \
          ZoneText(name, std::strlen(name)); \
          TracyVkZoneTransient((ctx)->getDevice()->queues().graphics.tracyCtx, TracyConcat(__tracy_gpu_source_location,__LINE__), (ctx)->getCmdBuffer(DxvkCmdBuffer::ExecBuffer), name, true); \
          __ScopedAnnotation __scopedAnnotation(ctx, name)

  #define TRACY_OBJECT_MEMORY_PROFILING \
          void * operator new ( std :: size_t count ) { \
            auto ptr = malloc(count); \
            TracyAlloc(ptr, count); \
            return ptr; \
          } \
          void operator delete (void* ptr) noexcept { \
            TracyFree(ptr); \
            free(ptr); \
          }

#else
  #define ScopedCpuProfileZoneDynamic(ctx, name)
  #define ScopedGpuProfileZoneDynamicZ(ctx, name)
  #define TRACY_OBJECT_MEMORY_PROFILING
#endif

namespace dxvk {

  class DxvkContext;
  class DxvkDevice;

  /**
   * A helper class to add annotation/profiler ranges as renderOps into cmd buffer.
   */
  class __ScopedAnnotation {
  public:
    __ScopedAnnotation(Rc<DxvkContext> ctx, const char* name);
    ~__ScopedAnnotation();

  private:
    Rc<DxvkContext> m_ctx;
  };

  class __ScopedQueueAnnotation {
  public:
    __ScopedQueueAnnotation(DxvkDevice* dev, VkCommandBuffer cmdBuf, const char* name);
    ~__ScopedQueueAnnotation();

  private:
    DxvkDevice* m_device;
    VkCommandBuffer m_cmdBuf;
  };
}