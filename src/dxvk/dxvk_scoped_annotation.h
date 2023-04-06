#pragma once

#include "dxvk_include.h"
#include "../tracy/TracyVulkan.hpp"

#define ScopedGpuProfileZone(ctx, name) \
  TracyVkZone(ctx->getDevice()->queues().graphics.tracyCtx, ctx->getCmdBuffer(DxvkCmdBuffer::ExecBuffer), name); \
  __ScopedAnnotation __scopedAnnotation(ctx, name)

namespace dxvk {

  class DxvkContext;

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
}