/*
* Copyright (c) 2023, NVIDIA CORPORATION. All rights reserved.
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

#include <vector>
#include <filesystem>

#include "dxvk_include.h"
#include "dxvk_shader.h"

#include "../spirv/spirv_code_buffer.h"
#include "../spirv/spirv_compression.h"

// Note: Define Shader Classes within an unnamed namespace to avoid violating One Definition Rule 
//  where multiple Shader Classes with same names could be defined across different cpp causing
//  definition aliasing.

// Helper macros for managed shader
#define SHADER_SOURCE(className, stage, code) \
public: \
  static const uint32_t* getStaticCodeData() { return code; } \
  static size_t getStaticCodeSize() { return sizeof(code); } \
  static const char* getName() { return #code; } \
  static const VkShaderStageFlagBits getStage() { return stage; } \
  static Rc<DxvkShader> getShader() { \
    Rc<DxvkShader> shader = ShaderManager::getInstance()->getShader<className>(); \
    shader->setDebugName(getName()); \
    return shader; \
   } \
  struct Ctor { Ctor() { if (getStage() == VK_SHADER_STAGE_COMPUTE_BIT) AutoShaderPipelinePrewarmer::registerComputeShaderForPrewarm([]{ return className::getShader(); }); }}; \
  static Ctor ctor;

#define PREWARM_SHADER_PIPELINE(className) \
  className::Ctor className::ctor

#define GET_SHADER_VARIANT(stage, className, code) \
  ShaderManager::getInstance()->getShaderVariant<className>(stage, sizeof(code), code, #code)


#define BINDLESS_ENABLED() static const bool requiresGlobalExtraLayout() { return true; }

#define BEGIN_PARAMETER() \
private: \
  inline static std::vector<dxvk::DxvkResourceSlot> resourceSlotList = {

#define END_PARAMETER() }; \
public: \
  static std::vector<dxvk::DxvkResourceSlot> getResourceSlots() { return resourceSlotList; }

// Slang: RaytracingAccelerationStructure
// GLSL: accelerationStructureEXT 
#define ACCELERATION_STRUCTURE(binding)     { binding, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR },

// Slang: StructuredBuffer<...>
// GLSL: readonly buffer {...};
#define STRUCTURED_BUFFER(binding)          { binding, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER },

// Slang: RWStructuredBuffer<...>
// GLSL: buffer {...};
#define RW_STRUCTURED_BUFFER(binding)       { binding, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_IMAGE_VIEW_TYPE_MAX_ENUM, VK_ACCESS_SHADER_WRITE_BIT },

// Slang: Texture2DArray
// GLSL: uniform texture2DArray
#define TEXTURE2DARRAY(binding)             { binding, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_IMAGE_VIEW_TYPE_2D_ARRAY },

// Slang: ConstantBuffer<...>
// GLSL: uniform { ... }
#define CONSTANT_BUFFER(binding)            { binding, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER },

// Slang: RWTexture3D<...> (note: there's no readonly qualifier in Slang)
// GLSL: readonly image3D
#define RW_TEXTURE3D_READONLY(binding)      { binding, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_IMAGE_VIEW_TYPE_3D },

// Slang: RWTexture3D<...>
// GLSL: image3D
#define RW_TEXTURE3D(binding)               { binding, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_IMAGE_VIEW_TYPE_3D, VK_ACCESS_SHADER_WRITE_BIT },

// Slang: RWTexture2D<...> (note: there's no readonly qualifier in Slang)
// GLSL: readonly image2D
#define RW_TEXTURE2D_READONLY(binding)      { binding, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_IMAGE_VIEW_TYPE_2D },

// Slang: RWTexture2D<...>
// GLSL: image2D
#define RW_TEXTURE2D(binding)               { binding, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_IMAGE_VIEW_TYPE_2D, VK_ACCESS_SHADER_WRITE_BIT },

// Slang: RWTexture2DArray<...>
// GLSL: image2DArray
#define RW_TEXTURE2DARRAY(binding)          { binding, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_IMAGE_VIEW_TYPE_2D_ARRAY, VK_ACCESS_SHADER_WRITE_BIT },

// Slang: RWTexture1D<...>
// GLSL: readonly image1D
#define RW_TEXTURE1D_READONLY(binding)      { binding, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_IMAGE_VIEW_TYPE_1D },

// Slang: RWTexture1D<...>
// GLSL: image1D
#define RW_TEXTURE1D(binding)                 { binding, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_IMAGE_VIEW_TYPE_1D, VK_ACCESS_SHADER_WRITE_BIT },

// Slang: Sampler3D<...> (combined texture-sampler)
// GLSL: uniform sampler3D
#define SAMPLER3D(binding)                  { binding, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_IMAGE_VIEW_TYPE_3D },

// Slang: Sampler2D<...> (combined texture-sampler)
// GLSL: uniform sampler2D
#define SAMPLER2D(binding)                  { binding, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_IMAGE_VIEW_TYPE_2D },

// Slang: Sampler2DArray<...> (combined texture-sampler)
// GLSL: uniform sampler2DArray
#define SAMPLER2DARRAY(binding)             { binding, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_IMAGE_VIEW_TYPE_2D_ARRAY },

// Slang: Sampler1D<...> (combined texture-sampler)
// GLSL: uniform sampler1D
#define SAMPLER1D(binding)                  { binding, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_IMAGE_VIEW_TYPE_1D },

// Slang: SamplerCube<...> (combined texture-sampler)
// GLSL: uniform samplerCube
#define SAMPLERCUBE(binding)                { binding, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_IMAGE_VIEW_TYPE_CUBE },

// Slang: SamplerState (just the sampler)
// GLSL: uniform sampler
#define SAMPLER(binding)                    { binding, VK_DESCRIPTOR_TYPE_SAMPLER },

// Slang: Texture3D<...> (just the texture)
// GLSL: uniform texture3D
#define TEXTURE3D(binding)                  { binding, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_IMAGE_VIEW_TYPE_3D },

// Slang: Texture2D<...> (just the texture)
// GLSL: uniform texture2D
#define TEXTURE2D(binding)                  { binding, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_IMAGE_VIEW_TYPE_2D },

#define INTERFACE_INPUT_SLOTS(n)            static const uint32_t getInterfaceInputSlots() { return n; }
#define INTERFACE_OUTPUT_SLOTS(n)           static const uint32_t getInterfaceOutputSlots() { return n; }

#define PUSH_CONSTANTS(data) \
static const uint32_t getPushBufferSize() { return sizeof(data); }

namespace dxvk {

  class DxvkDevice;
  class RtxContext;

  class ManagedShader {
  public:
    static const bool requiresGlobalExtraLayout() { return false; }
    static const uint32_t getPushBufferSize() { return 0; }
    static const uint32_t getInterfaceInputSlots() { return 0; }
    static const uint32_t getInterfaceOutputSlots() { return 0; }
  };

  // This is a helper for pre-warming pipelines with the driver.
  // Shaders are registered statically during init time, and the list is then processed in RtxInitializer
  // Currently only compute shaders are supported (as they are single shader pipelines).  RT and graphics shaders 
  // require a matching/building at instance time.
  // Generally this isn't a problem, without the prewarmer shaders will still execute as normal, but they may incur a hitch
  // when compiled on the hot path (i.e. inline with draw calls).
  class AutoShaderPipelinePrewarmer {
    inline static dxvk::mutex s_listMutex;
    inline static std::vector<std::function<Rc<DxvkShader>()>> s_shaderGetters;
    inline static bool s_prewarmed = false;

  public:
    static void registerComputeShaderForPrewarm(std::function<Rc<DxvkShader>()> getShader) {
      std::lock_guard lock(s_listMutex);

      if (s_prewarmed) {
        Logger::debug("Attempted to prewarm shader after prewarming was complete.");
        return;
      }

      s_shaderGetters.push_back(getShader);
    }

    static void prewarmComputePipelines(DxvkPipelineManager& pipelineManager) {
      std::lock_guard lock(s_listMutex);

      for (auto& shaderGetter : s_shaderGetters) {
        Rc<DxvkShader> shader = shaderGetter();

        // Currently automatic prewarm only applies to compute shaders
        if (shader->stage() != VK_SHADER_STAGE_COMPUTE_BIT) {
          Logger::warn(str::format("Attempted to prewarm shader (name=", shader->debugName(), ", stage=", shader->stage(), ") when currently only automatic prewarming for compute shaders is supported."));
        }
      }

      s_shaderGetters.clear();
      s_prewarmed = true;
    }
  };

  class ShaderManager {
  public:
    ShaderManager(const ShaderManager& other) = delete;
    ShaderManager(ShaderManager&& other) noexcept = delete;
    ShaderManager& operator=(const ShaderManager& other) = delete;
    ShaderManager& operator=(ShaderManager&& other) noexcept = delete;

    static ShaderManager* getInstance();
    static void destroyInstance();

    void setDevice(DxvkDevice* device) {
      m_device = device;
    }

    void addGlobalExtraLayout(const VkDescriptorSetLayout extraLayout) {
      m_extraLayouts.push_back(extraLayout);
    }

#ifdef REMIX_DEVELOPMENT
    void checkForShaderChanges();
    bool reloadShaders();
#endif

    template<typename T>
    Rc<DxvkShader> getShader() {
      return getShaderVariant<T>(T::getStage(), T::getStaticCodeSize(), T::getStaticCodeData(), T::getName());
    }

    template<typename T>
    Rc<DxvkShader> getShaderVariant(VkShaderStageFlagBits stage, size_t codeSize, const uint32_t* staticCode, const char* name) {
      std::string shaderName = name;
      
      m_shaderMapLock.lock();

      auto pShaderPair = m_shaderMap.find(shaderName);
      if (pShaderPair == m_shaderMap.end()) {
        m_shaderMapLock.unlock();

        // Create a new shader
        ShaderInfo info;
        info.m_name = name;
        info.m_shaderType = stage;
        info.m_pushBufferSize = T::getPushBufferSize();
        info.m_requiresExtraLayout = T::requiresGlobalExtraLayout();
        info.m_slots = T::getResourceSlots();
        info.m_staticCode = SpirvCodeBuffer(uint32_t(codeSize / sizeof(uint32_t)), staticCode);
        info.m_interfaceInputs = T::getInterfaceInputSlots();
        info.m_interfaceOutputs = T::getInterfaceOutputSlots();

        Rc<DxvkShader> shader = createShader(info);
        info.m_shader.push_back(shader);

        m_shaderMapLock.lock();
        pShaderPair = m_shaderMap.find(shaderName);
        if (pShaderPair == m_shaderMap.end()) {
          m_shaderMap[shaderName] = info;
        }

        m_shaderMapLock.unlock();
        return info.m_shader.back();
      } else {
        m_shaderMapLock.unlock();
        return pShaderPair->second.m_shader.back();
      }
    }

  private:

    struct ShaderInfo {
      std::vector<Rc<DxvkShader>> m_shader;
      std::vector<dxvk::DxvkResourceSlot> m_slots;
      SpirvCodeBuffer m_staticCode;
      const char* m_name;
      uint32_t m_pushBufferSize;
      bool m_requiresExtraLayout;
      VkShaderStageFlagBits m_shaderType;
      uint32_t m_interfaceInputs;
      uint32_t m_interfaceOutputs;
    };

    ShaderManager();
    ~ShaderManager() = default;

    Rc<DxvkShader> createShader(const ShaderInfo& info) {
      DxvkShaderOptions options;

      if (info.m_requiresExtraLayout) {
        options.extraLayouts = m_extraLayouts;
      }

      Rc<DxvkShader> shader = new DxvkShader(info.m_shaderType,
        info.m_slots.size(), info.m_slots.data(), { info.m_interfaceInputs, info.m_interfaceOutputs, 0u, info.m_pushBufferSize }, info.m_staticCode,
        options, DxvkShaderConstData());

      shader->setDebugName(info.m_name);
      shader->generateShaderKey();
      m_device->registerShader(shader, true);

      return shader;
    }

    static ShaderManager* s_instance;

    const std::filesystem::path m_shaderBinaryPath;
    bool m_recompileShadersOnLaunch;
    // HANDLE m_shaderChangeNotificationObject;

    DxvkDevice* m_device;

    std::vector<VkDescriptorSetLayout> m_extraLayouts;

    std::unordered_map<std::string, ShaderInfo> m_shaderMap;
    dxvk::mutex m_shaderMapLock;
  };
}