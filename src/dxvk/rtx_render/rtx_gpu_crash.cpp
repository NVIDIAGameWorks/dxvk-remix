/*
* Copyright (c) 2026, NVIDIA CORPORATION. All rights reserved.
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
#ifdef REMIX_DEVELOPMENT

#include <rtx_shaders/gpu_crash_trigger.h>

#include "rtx_gpu_crash.h"
#include "dxvk_device.h"
#include "rtx_context.h"
#include "rtx_debug_view.h"
#include "rtx_render/rtx_shader_manager.h"
#include "rtx/pass/gpu_crash_trigger/gpu_crash_trigger.h"

namespace dxvk {

  namespace {

    class GpuCrashShader : public ManagedShader {
      SHADER_SOURCE(GpuCrashShader, VK_SHADER_STAGE_COMPUTE_BIT, gpu_crash_trigger)
      PUSH_CONSTANTS(GpuCrashTriggerArgs)
      BEGIN_PARAMETER()
        RW_TEXTURE2D(GPU_CRASH_TRIGGER_OUTPUT)
      END_PARAMETER()
    };

    PREWARM_SHADER_PIPELINE(GpuCrashShader);

  } // namespace

  void GpuCrashPass::dispatch(RtxContext* ctx) {
    DebugView& debugView = ctx->getCommonObjects()->metaDebugView();
    ctx->bindResourceView(GPU_CRASH_TRIGGER_OUTPUT, debugView.getDebugOutput(), nullptr);
    ctx->bindShader(VK_SHADER_STAGE_COMPUTE_BIT, GpuCrashShader::getShader());
    const GpuCrashTriggerArgs crashArgs = { 1 };
    ctx->pushConstants(0, sizeof(crashArgs), &crashArgs);
    ctx->dispatch(1, 1, 1);
    // Shader intentionally hangs; no further dispatch this frame
  }

} // namespace dxvk

#endif // REMIX_DEVELOPMENT
