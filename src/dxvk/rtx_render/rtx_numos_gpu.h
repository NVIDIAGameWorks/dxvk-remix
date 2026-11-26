/*
* Copyright (c) 2026, Kim2091
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

#include <string>
#include <unordered_map>
#include <vector>

#include "numos/gpu.h"
#include "numos/shaders.h"

#include "../dxvk_context.h"

namespace dxvk {

  // Implements the GPU interface of NUMOS on DXVK.
  class NumosGpu final : public numos::IGpu {
  public:
    explicit NumosGpu(DxvkDevice* pDevice, std::string spirvDirectory);
    ~NumosGpu() override;

    // Every IGpu call until endFrame records into this context.
    void beginFrame(const Rc<DxvkContext>& ctx);

    // The context holds the device that owns this object, so keeping it past the frame would keep the device alive.
    void endFrame() {
      m_ctx = nullptr;
    }

    // Each kind of host resource has a fixed handle, which is pointed at the current resource every frame.
    numos::GpuHandle setExternalAccel(const Rc<DxvkAccelStructure>& accel);
    numos::GpuHandle setExternalView(uint32_t index, const Rc<DxvkImageView>& view);

    // These return null for a zero or unknown handle.
    Rc<DxvkImageView> getView(numos::GpuHandle texture) const;
    Rc<DxvkImage> getImage(numos::GpuHandle texture) const;
    Rc<DxvkBuffer> getBuffer(numos::GpuHandle buffer) const;
    Rc<DxvkSampler> getSampler(numos::GpuHandle sampler) const;

    // Returns false when the installed SPIR-V does not match the library's pass table.
    bool areShadersValid() const {
      return !m_shaderLoadFailed;
    }

    // Registers the shader of every pass that the settings can dispatch, so their pipelines compile asynchronously.
    // Only the selected variant of the cloud render passes is registered.
    void prewarmShaders(numos::PassId cloudRender);

    // numos::IGpu
    numos::GpuHandle createTexture(const numos::TextureDesc& desc) override;
    numos::GpuHandle createBuffer(const numos::BufferDesc& desc) override;
    numos::GpuHandle createSampler(const numos::SamplerDesc& desc) override;
    void destroy(numos::GpuHandle resource) override;
    ConstantsRegion uploadConstants(const void* pData, size_t size) override;
    void updateBuffer(numos::GpuHandle buffer, size_t offset, size_t size, const void* pData) override;
    void copyBuffer(numos::GpuHandle dst, size_t dstOffset, numos::GpuHandle src, size_t srcOffset, size_t size) override;
    void copyTexture(numos::GpuHandle dst, numos::GpuHandle src) override;
    void clearTexture(numos::GpuHandle texture, const float rgba[4]) override;
    void uploadTexture(numos::GpuHandle texture, uint32_t mip, const void* pData, size_t size) override;
    void dispatch(const numos::DispatchDesc& desc) override;
    void barrier() override;
    bool readBuffer(numos::GpuHandle readback, void* pDst, size_t size) override;
    void beginZone(const char* name) override;
    void endZone() override;

  private:
    struct Texture {
      numos::TextureDesc desc;
      std::string name;
      Rc<DxvkImage> image;
      // This view covers every mip with the declared dimension.
      Rc<DxvkImageView> view;
      // The other views are keyed by mip, mip count, dimension and usage.
      std::unordered_map<uint64_t, Rc<DxvkImageView>> views;
    };
    struct Buffer {
      numos::BufferDesc desc;
      Rc<DxvkBuffer> buffer;
      // A readback buffer is readable once a copy into it was recorded.
      bool copied = false;
    };
    struct Resource {
      enum class Kind { Texture, Buffer, Sampler, Accel, ExternalView } kind;
      Texture texture;
      Buffer buffer;
      Rc<DxvkSampler> sampler;
      Rc<DxvkAccelStructure> accel;
      Rc<DxvkImageView> externalView;
    };
    struct Pass {
      bool loaded = false;
      Rc<DxvkShader> shader;
    };

    Rc<DxvkShader> getShader(uint32_t pass);
    Resource* find(numos::GpuHandle handle);
    const Resource* find(numos::GpuHandle handle) const;
    numos::GpuHandle add(Resource&& resource);
    Rc<DxvkImageView> viewFor(Texture& texture, uint32_t mip, uint32_t mipCount, numos::BindingDim dim, bool storage);
    Rc<DxvkImageView> placeholder(numos::BindingDim dim, bool storage, uint32_t spirvFormat);

    DxvkDevice* m_pDevice;
    std::string m_spirvDirectory;
    Rc<DxvkContext> m_ctx;

    std::unordered_map<numos::GpuHandle, Resource> m_resources;
    numos::GpuHandle m_nextHandle = 1;
    std::vector<Pass> m_passes;
    bool m_shaderLoadFailed = false;

    // The constants ring is written in the command stream and rewinds every frame.
    // It is never written whole and exceeds DXVK's rename limit, so it is never renamed.
    Rc<DxvkBuffer> m_constants;
    size_t m_constantsOffset = 0;
    size_t m_constantsAlign = 256;
    numos::GpuHandle m_constantsHandle = 0;
    // A ring that was outgrown stays resolvable until the next frame, because its regions may still be bound.
    std::vector<numos::GpuHandle> m_retiredConstants;

    std::unordered_map<uint64_t, Rc<DxvkImageView>> m_placeholders;
    Rc<DxvkBuffer> m_placeholderBuffer;
    Rc<DxvkSampler> m_defaultSampler;

    std::vector<std::string> m_zones;
  };

}
