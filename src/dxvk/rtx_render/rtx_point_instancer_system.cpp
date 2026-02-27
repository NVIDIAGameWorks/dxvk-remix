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
#include "rtx_point_instancer_system.h"

#include "dxvk_device.h"
#include "rtx_render/rtx_shader_manager.h"
#include "dxvk_scoped_annotation.h"
#include "dxvk_context.h"
#include "rtx_context.h"
#include "rtx_imgui.h"

#include <rtx_shaders/point_instancer_culling.h>

namespace dxvk {

  namespace {
    class PointInstancerCullingShader : public ManagedShader {
      SHADER_SOURCE(PointInstancerCullingShader, VK_SHADER_STAGE_COMPUTE_BIT, point_instancer_culling)

      BEGIN_PARAMETER()
        CONSTANT_BUFFER(POINT_INSTANCER_CULLING_BINDING_CONSTANTS)
        STRUCTURED_BUFFER(POINT_INSTANCER_CULLING_BINDING_TRANSFORMS_INPUT)
        RW_STRUCTURED_BUFFER(POINT_INSTANCER_CULLING_BINDING_INSTANCE_BUFFER)
        RW_STRUCTURED_BUFFER(POINT_INSTANCER_CULLING_BINDING_SURFACE_BUFFER)
        RW_STRUCTURED_BUFFER(POINT_INSTANCER_CULLING_BINDING_MATERIAL_BUFFER)
      END_PARAMETER()
    };
  }

  RtxPointInstancerSystem::RtxPointInstancerSystem(DxvkDevice* device)
    : CommonDeviceObject(device) { }

  void RtxPointInstancerSystem::showImguiSettings() {
    if (RemixGui::CollapsingHeader("Point Instancer Culling")) {
      ImGui::PushID("rtx_point_instancer");
      ImGui::Dummy({ 0, 2 });
      ImGui::Indent();

      RemixGui::Checkbox("Enable Culling", &enableObject());
      ImGui::BeginDisabled(!enable());

      RemixGui::DragFloat("Culling Radius", &cullingRadiusObject(), 10.f, fadeStartRadius(), 100000.f, "%.0f");
      RemixGui::DragFloat("Fade Start Radius", &fadeStartRadiusObject(), 10.f, 0.f, cullingRadius(), "%.0f");

      ImGui::EndDisabled();
      ImGui::Unindent();
      ImGui::PopID();
    }
  }

  void RtxPointInstancerSystem::dispatchCulling(
      Rc<DxvkContext> ctx,
      const Rc<DxvkBuffer>& instanceBuffer,
      const Rc<DxvkBuffer>& surfaceBuffer,
      const Rc<DxvkBuffer>& surfaceMaterialBuffer,
      const std::vector<PointInstancerBatch>& batches,
      const Vector3& cameraPosition) {
    ScopedGpuProfileZone(ctx, "PointInstancerCulling");

    if (batches.empty()) {
      return;
    }

    const Rc<DxvkDevice>& dev = ctx->getDevice();

    // Allocate constant buffer (once)
    if (m_cb.ptr() == nullptr) {
      DxvkBufferCreateInfo info;
      info.usage  = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
      info.stages = VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
      info.access = VK_ACCESS_TRANSFER_WRITE_BIT;
      info.size   = sizeof(PointInstancerCullingConstants);
      m_cb = dev->createBuffer(info, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                               DxvkMemoryStats::Category::RTXBuffer,
                               "RTX PointInstancer - Constant Buffer");
    }

    for (const PointInstancerBatch& batch : batches) {
      const uint32_t count = batch.instanceCount;
      if (count == 0) {
        continue;
      }

      // Upload source transforms to GPU
      const size_t transformsSize = count * sizeof(Matrix4);
      if (m_transformsGpu.ptr() == nullptr || m_transformsGpu->info().size < transformsSize) {
        DxvkBufferCreateInfo info;
        info.usage  = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        info.stages = VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
        info.access = VK_ACCESS_TRANSFER_WRITE_BIT;
        info.size   = align(transformsSize, 256);
        m_transformsGpu = dev->createBuffer(info, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                            DxvkMemoryStats::Category::RTXBuffer,
                                            "RTX PointInstancer - Transforms Input");
      }

      ctx->writeToBuffer(m_transformsGpu, 0, transformsSize, batch.transforms->data());

      // Fill constant buffer
      // When culling is disabled, use FLT_MAX so every instance passes the distance test.
      const bool cullingEnabled = enable();
      PointInstancerCullingConstants constants {};
      memcpy(&constants.objectToWorld, &batch.objectToWorld, sizeof(mat4));
      memcpy(&constants.prevObjectToWorld, &batch.prevObjectToWorld, sizeof(mat4));
      constants.cameraPosition    = { cameraPosition.x, cameraPosition.y, cameraPosition.z };
      constants.cullingRadius     = cullingEnabled ? cullingRadius() : FLT_MAX;
      constants.totalInstanceCount = count;
      constants.baseSurfaceIndex  = batch.baseSurfaceIndex;
      constants.fadeStartRadius   = cullingEnabled ? fadeStartRadius() : 0.f;
      constants.customIndexFlags  = batch.customIndexFlags;
      constants.instanceMask      = batch.instanceMask;
      constants.sbtOffsetAndFlags = batch.sbtOffsetAndFlags;
      constants.blasRefLo         = static_cast<uint32_t>(batch.blasReference & 0xFFFFFFFFull);
      constants.blasRefHi         = static_cast<uint32_t>(batch.blasReference >> 32);
      constants.instanceBufferOffset = batch.instanceBufferByteOffset;

      const DxvkBufferSliceHandle cSlice = m_cb->allocSlice();
      ctx->invalidateBuffer(m_cb, cSlice);
      ctx->writeToBuffer(m_cb, 0, sizeof(PointInstancerCullingConstants), &constants);

      // Bind resources
      ctx->bindResourceBuffer(POINT_INSTANCER_CULLING_BINDING_CONSTANTS, DxvkBufferSlice(m_cb));
      ctx->bindResourceBuffer(POINT_INSTANCER_CULLING_BINDING_TRANSFORMS_INPUT, DxvkBufferSlice(m_transformsGpu));
      ctx->bindResourceBuffer(POINT_INSTANCER_CULLING_BINDING_INSTANCE_BUFFER, DxvkBufferSlice(instanceBuffer));
      ctx->bindResourceBuffer(POINT_INSTANCER_CULLING_BINDING_SURFACE_BUFFER, DxvkBufferSlice(surfaceBuffer));
      ctx->bindResourceBuffer(POINT_INSTANCER_CULLING_BINDING_MATERIAL_BUFFER, DxvkBufferSlice(surfaceMaterialBuffer));

      ctx->bindShader(VK_SHADER_STAGE_COMPUTE_BIT, PointInstancerCullingShader::getShader());

      const VkExtent3D workgroups = util::computeBlockCount(
        VkExtent3D { count, 1, 1 },
        VkExtent3D { 64, 1, 1 });

      ctx->dispatch(workgroups.width, workgroups.height, workgroups.depth);
    }
  }
}
