/*
* Copyright (c) 2022-2025, NVIDIA CORPORATION. All rights reserved.
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

#include <unordered_map>
#include <vector>
#include <cstdint>

#include "imgui.h"

#include "dxvk_device.h" 
#include "dxvk_context.h"

#include <rtx_shaders/imgui_vertex.h>
#include <rtx_shaders/imgui_fragment.h>
#include "../shaders/rtx/pass/imgui/imgui_bindings.h"
#include "../rtx_render/rtx_shader_manager.h"

namespace ImGui_ImplDxvk {

  using namespace dxvk;

  // Defined within an unnamed namespace to ensure unique definition across binary
  namespace {

    class ImGuiVertexShader : public ManagedShader {
      SHADER_SOURCE(ImGuiVertexShader, VK_SHADER_STAGE_VERTEX_BIT, imgui_vertex)

      PUSH_CONSTANTS(ImGuiPushConstants)

      BEGIN_PARAMETER()
      END_PARAMETER()

      // color and uv
      INTERFACE_OUTPUT_SLOTS(2);
    };


    class ImGuiPixelShader : public ManagedShader {
      SHADER_SOURCE(ImGuiPixelShader, VK_SHADER_STAGE_FRAGMENT_BIT, imgui_fragment)

      BEGIN_PARAMETER()
        SAMPLER2D(IMGUI_TEXTURE0_INPUT)
      END_PARAMETER()

      // Color and UV fetched from VS
      INTERFACE_INPUT_SLOTS(2);

      // Writing out of pixel shader to render target
      INTERFACE_OUTPUT_SLOTS(1);
    };
  }

  struct TextureHandle {
    Rc<DxvkImageView> view;
    Rc<DxvkSampler>   sampler;
  };

  struct FrameBuffers {
    Rc<DxvkBuffer> vb;
    Rc<DxvkBuffer> ib;
    VkDeviceSize   vbSize = 0;
    VkDeviceSize   ibSize = 0;
  };

  struct Data {
    VkImageView FontView = (VkImageView) 0;
    VkImage     FontImage = (VkImage) 0;
  };

  struct Backend {
    Data pub;

    Rc<DxvkDevice> device;

    // We do not hold a context permanently (caller passes the active DxvkContext each frame)

    // One set of dynamic buffers reused every frame (resize-on-demand)
    FrameBuffers buffers;

    // Map ImTextureID -> TextureHandle (image view + sampler)
    std::unordered_map<ImTextureID, TextureHandle> textures;

    ImTextureID fontTexID = nullptr;

    // alignment heuristic for streaming buffers
    const VkDeviceSize alignment = 256;
  };

  // Global singleton per ImGui context
  static Backend* g = nullptr;

  // Utility: (re)create a HOST_VISIBLE | HOST_COHERENT buffer of given size/usage, name is for debugging.
  static Rc<DxvkBuffer> CreateHostBuffer(const Rc<DxvkDevice>& dev,
                                         VkDeviceSize size,
                                         VkBufferUsageFlags usage,
                                         const char* name) {
    DxvkBufferCreateInfo info = {};
    info.size = size;
    info.usage = usage;
    info.stages = VK_PIPELINE_STAGE_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT;
    info.access = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_INDEX_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;

    // Host-visible for direct CPU writes (streaming)
    VkMemoryPropertyFlags mem = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

    return dev->createBuffer(info, mem, DxvkMemoryStats::Category::RTXBuffer, name);
  }

  inline bool Init(const Rc<DxvkDevice>& device) {
    IM_ASSERT(g == nullptr && "ImGui_ImplDxvk already initialized");

    g = new Backend();
    g->device = device;

    ImGuiIO& io = ImGui::GetIO();
    io.BackendRendererUserData = &g->pub;
    io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;

    // Start with small streaming buffers; grow on demand.
    g->buffers.vb = CreateHostBuffer(device, 64 * 1024, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, "ImGuiVB");
    g->buffers.ib = CreateHostBuffer(device, 64 * 1024, VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, "ImGuiIB");
    g->buffers.vbSize = 64 * 1024;
    g->buffers.ibSize = 64 * 1024;

    return true;
  }

  inline void Shutdown() {
    if (!g) return;
    delete g;
    g = nullptr;
  }

  inline void NewFrame() {
  }

  // Register a texture (image view + sampler). Returns an ImTextureID you can pass to ImGui::Image / ImageButton.
  // Slot 0 is used at draw time via bindResourceView(0, view) and bindResourceSampler(0, sampler).
  inline ImTextureID AddTexture(const Rc<DxvkSampler>& sampler,
                                const Rc<DxvkImageView>& imageView) {
    IM_ASSERT(g != nullptr);
    TextureHandle h { imageView, sampler };

    // Use the imageView pointer as a stable key for ImTextureID by default.
    ImTextureID id = reinterpret_cast<ImTextureID>(imageView.ptr());
    g->textures[id] = h;
    return id;
  }

  // Optional helper to store the font texture id (if you create one yourself outside this backend)
  inline void SetFontTexture(ImTextureID tex) {
    IM_ASSERT(g); g->fontTexID = tex;
  }

  // Ensure our streaming buffers are large enough; recreate if needed.
  static void EnsureBufferCapacity(FrameBuffers& fb, const Rc<DxvkDevice>& dev, size_t vtxBytes, size_t idxBytes) {
    if (vtxBytes > fb.vbSize) {
      VkDeviceSize newSize = (vtxBytes + g->alignment - 1) & ~(g->alignment - 1);
      fb.vb = CreateHostBuffer(dev, newSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, "ImGuiVB");
      fb.vbSize = newSize;
    }

    if (idxBytes > fb.ibSize) {
      VkDeviceSize newSize = (idxBytes + g->alignment - 1) & ~(g->alignment - 1);
      fb.ib = CreateHostBuffer(dev, newSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, "ImGuiIB");
      fb.ibSize = newSize;
    }
  }

  static void setupImGuiPipeline(dxvk::DxvkContext* ctx) {
    // Vertex layout for ImDrawVert
    DxvkVertexBinding bindings[1] = {};
    bindings[0].binding = 0;
    bindings[0].fetchRate = 0;
    bindings[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    DxvkVertexAttribute attrs[3] = {};
    // location 0: pos (RG32)
    attrs[0].location = 0; 
    attrs[0].binding = 0; 
    attrs[0].format = VK_FORMAT_R32G32_SFLOAT; 
    attrs[0].offset = IM_OFFSETOF(ImDrawVert, pos);
    // location 1: uv (RG32)
    attrs[1].location = 1; 
    attrs[1].binding = 0; 
    attrs[1].format = VK_FORMAT_R32G32_SFLOAT; 
    attrs[1].offset = IM_OFFSETOF(ImDrawVert, uv);
    // location 2: col (RGBA8 UNORM)
    attrs[2].location = 2; 
    attrs[2].binding = 0; 
    attrs[2].format = VK_FORMAT_R8G8B8A8_UNORM; 
    attrs[2].offset = IM_OFFSETOF(ImDrawVert, col);

    ctx->setInputLayout(/*attrCount*/3, attrs, /*bindingCount*/1, bindings);

    // Fixed function state
    DxvkInputAssemblyState ia = {};
    ia.primitiveTopology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    ia.primitiveRestart = VK_FALSE;
    ctx->setInputAssemblyState(ia);

    DxvkRasterizerState rs = {};
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    ctx->setRasterizerState(rs);

    DxvkMultisampleState ms = {};
    ms.sampleMask = 0xFFFFFFFFu;
    ctx->setMultisampleState(ms);

    DxvkDepthStencilState ds = {};
    ds.enableDepthTest = VK_FALSE;
    ds.enableDepthWrite = VK_FALSE;
    ds.enableStencilTest = VK_FALSE;
    ctx->setDepthStencilState(ds);

    // Blend for target 0 (premult not required; ImGui uses straight alpha)
    DxvkBlendMode bm = {};
    bm.enableBlending = VK_TRUE;
    bm.colorSrcFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    bm.colorDstFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    bm.colorBlendOp = VK_BLEND_OP_ADD;
    bm.alphaSrcFactor = VK_BLEND_FACTOR_ONE;
    bm.alphaDstFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    bm.alphaBlendOp = VK_BLEND_OP_ADD;
    bm.writeMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    ctx->setBlendMode(0, bm);

    // Bind shaders, then let DXVK bake/commit the pipeline
    ctx->bindShader(VK_SHADER_STAGE_VERTEX_BIT, ImGuiVertexShader::getShader());
    ctx->bindShader(VK_SHADER_STAGE_FRAGMENT_BIT, ImGuiPixelShader::getShader());
  }


  inline void RenderDrawData(ImDrawData* draw_data,
                             DxvkContext* ctx,
                             uint32_t framebuffer_width,
                             uint32_t framebuffer_height) {
    IM_ASSERT(g && draw_data);
    if (framebuffer_width <= 0 || framebuffer_height <= 0) return;

    // Build combined vertex/index data sizes
    size_t totalVtx = 0, totalIdx = 0;
    for (int n = 0; n < draw_data->CmdListsCount; n++) {
      totalVtx += draw_data->CmdLists[n]->VtxBuffer.Size;
      totalIdx += draw_data->CmdLists[n]->IdxBuffer.Size;
    }
    const size_t vtxBytes = totalVtx * sizeof(ImDrawVert);
    const size_t idxBytes = totalIdx * sizeof(ImDrawIdx);

    EnsureBufferCapacity(g->buffers, g->device, vtxBytes, idxBytes);

    // Push constants for pixel->NDC transform
    const ImVec2 display_pos = draw_data->DisplayPos;
    const ImVec2 display_size = draw_data->DisplaySize;
    struct Pc {
      float scale[2]; 
      float translate[2];
    };
    Pc pc;
    pc.scale[0] = 2.0f / display_size.x;
    pc.scale[1] = 2.0f / display_size.y;
    pc.translate[0] = -1.0f - display_pos.x * pc.scale[0];
    pc.translate[1] = -1.0f - display_pos.y * pc.scale[1];

    ctx->pushConstants(0, sizeof(Pc), &pc);

    // Stream vertex/index data into our host-visible buffers
    VkDeviceSize vbOffset = 0;
    VkDeviceSize ibOffset = 0;
    for (int n = 0; n < draw_data->CmdListsCount; n++) {
      const ImDrawList* cmd_list = draw_data->CmdLists[n];
      const size_t listVtxBytes = cmd_list->VtxBuffer.Size * sizeof(ImDrawVert);
      const size_t listIdxBytes = cmd_list->IdxBuffer.Size * sizeof(ImDrawIdx);
      ctx->updateBuffer(g->buffers.vb, vbOffset, listVtxBytes, cmd_list->VtxBuffer.Data);
      ctx->updateBuffer(g->buffers.ib, ibOffset, listIdxBytes, cmd_list->IdxBuffer.Data);

      vbOffset += listVtxBytes;
      ibOffset += listIdxBytes;
    }

    setupImGuiPipeline(ctx);

    // Bind VB/IB for the draw pass
    {
      DxvkBufferSlice vbSlice(g->buffers.vb, 0, vtxBytes);
      DxvkBufferSlice ibSlice(g->buffers.ib, 0, idxBytes);
      ctx->bindVertexBuffer(0u, vbSlice, sizeof(ImDrawVert)); // binding 0
      ctx->bindIndexBuffer(ibSlice, VK_INDEX_TYPE_UINT32);
    }

    // Setup viewport to match draw_data->DisplaySize
    {
      const ImVec2& clip_off = draw_data->DisplayPos;   // left/top origin
      const ImVec2& clip_scale = draw_data->FramebufferScale; 

      VkViewport vp = {};
      vp.x = 0.0f;
      vp.y = 0.0f;
      vp.width = (float)framebuffer_width;
      vp.height = (float) framebuffer_height;
      vp.minDepth = 0.0f;
      vp.maxDepth = 1.0f;

      VkRect2D sc = { {0, 0}, { (uint32_t) framebuffer_width, (uint32_t) framebuffer_height } };
      ctx->setViewports(1, &vp, &sc);

      // Draw lists
      int globalVtxOffset = 0;
      int globalIdxOffset = 0;

      for (int n = 0; n < draw_data->CmdListsCount; n++) {
        const ImDrawList* cmd_list = draw_data->CmdLists[n];
        for (int cmd_i = 0; cmd_i < cmd_list->CmdBuffer.Size; cmd_i++) {
          const ImDrawCmd& pcmd = cmd_list->CmdBuffer[cmd_i];

          if (pcmd.UserCallback) {
            pcmd.UserCallback(cmd_list, &pcmd);
            continue;
          }

          // Project scissor/clip rectangles into framebuffer space
          ImVec4 clip = pcmd.ClipRect;
          ImVec2 clip_min((clip.x - clip_off.x) * clip_scale.x,
                           (clip.y - clip_off.y) * clip_scale.y);
          ImVec2 clip_max((clip.z - clip_off.x) * clip_scale.x,
                           (clip.w - clip_off.y) * clip_scale.y);
          if (clip_max.x <= clip_min.x || clip_max.y <= clip_min.y)
            continue; // nothing to draw

          VkRect2D scissor;
          scissor.offset.x = (int32_t) (clip_min.x < 0.0f ? 0.0f : clip_min.x);
          scissor.offset.y = (int32_t) (clip_min.y < 0.0f ? 0.0f : clip_min.y);
          scissor.extent.width = (uint32_t) (clip_max.x - scissor.offset.x);
          scissor.extent.height = (uint32_t) (clip_max.y - scissor.offset.y);

          // Update scissor only (keep same viewport)
          ctx->setViewports(1, &vp, &scissor);

          // Bind texture for this draw (slot 0)
          TextureHandle tex = {};
          {
            ImTextureID id = pcmd.GetTexID() ? pcmd.GetTexID() : g->fontTexID;
            auto it = g->textures.find(id);
            if (it != g->textures.end())
              tex = it->second;
          }
          if (tex.view.ptr()) {
            ctx->bindResourceView(0, tex.view, nullptr);
          }
          if (tex.sampler.ptr()) {
            ctx->bindResourceSampler(0, tex.sampler);
          }

          // Issue the draw
          ctx->drawIndexed(pcmd.ElemCount,
                           1,                                  // instanceCount
                           pcmd.IdxOffset + globalIdxOffset,   // firstIndex
                           pcmd.VtxOffset + globalVtxOffset,   // vertexOffset
                           0);                                 // firstInstance
        }
        globalIdxOffset += cmd_list->IdxBuffer.Size;
        globalVtxOffset += cmd_list->VtxBuffer.Size;
      }

      // Restore default scissor to full framebuffer
      ctx->setViewports(1, &vp, &sc);
    }
  }
}
