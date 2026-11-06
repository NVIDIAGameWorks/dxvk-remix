// Copyright (c) 2026, NVIDIA CORPORATION. All rights reserved.
// SPDX-License-Identifier: MIT

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include <vulkan/vulkan.h>

#include "rtx/pass/particles/particle_spawn_distribution.h"
#include "rtx/pass/particles/particle_system_common.h"
#include "../../../src/dxvk/rtx_render/rtx_particle_spawn_cache.h"

namespace {
  constexpr uint32_t kSampleCount = 4097;
  constexpr uint32_t kPassCount = dxvk::kParticleDistributionPassCount;
  constexpr uint32_t kMaxTriangles = (dxvk::kParticleDistributionBlockSize + 6) * dxvk::kParticleDistributionBlockSize;

  void expect(bool condition, const char* message) {
    if (!condition) {
      throw std::runtime_error(message);
    }
  }

  void check(VkResult result) {
    if (result != VK_SUCCESS) {
      throw std::runtime_error("Vulkan call failed: " + std::to_string(result));
    }
  }

  struct Buffer {
    VkDevice device;
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    void* pData = nullptr;
    VkDeviceSize size;

    Buffer(VkDevice device, const VkPhysicalDeviceMemoryProperties& properties, VkDeviceSize size)
      : device(device), size(size) {
      VkBufferCreateInfo info = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
      info.size = size;
      info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
      check(vkCreateBuffer(device, &info, nullptr, &buffer));
      VkMemoryRequirements requirements;
      vkGetBufferMemoryRequirements(device, buffer, &requirements);
      uint32_t type = 0;
      for (; type < properties.memoryTypeCount; type++) {
        const auto flags = properties.memoryTypes[type].propertyFlags;
        if ((requirements.memoryTypeBits & (1u << type))
            && (flags & (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
              == (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
          break;
        }
      }
      expect(type < properties.memoryTypeCount, "No coherent host-visible memory");
      VkMemoryAllocateInfo allocation = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
      allocation.allocationSize = requirements.size;
      allocation.memoryTypeIndex = type;
      check(vkAllocateMemory(device, &allocation, nullptr, &memory));
      check(vkBindBufferMemory(device, buffer, memory, 0));
      check(vkMapMemory(device, memory, 0, size, 0, &pData));
    }

    ~Buffer() {
      if (pData) {
        vkUnmapMemory(device, memory);
      }
      vkDestroyBuffer(device, buffer, nullptr);
      vkFreeMemory(device, memory, nullptr);
    }

    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;
  };

  using Triangle = std::array<float, 9>;

  struct Distribution {
    std::vector<float> prefixes;
    std::vector<uint32_t> samples;
  };

  class GpuFixture {
  public:
    GpuFixture(const char* pBuildShader, const char* pSampleShader) {
      VkApplicationInfo app = { VK_STRUCTURE_TYPE_APPLICATION_INFO };
      app.apiVersion = VK_API_VERSION_1_2;
      VkInstanceCreateInfo instanceInfo = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
      instanceInfo.pApplicationInfo = &app;
      check(vkCreateInstance(&instanceInfo, nullptr, &m_instance));
      uint32_t count = 0;
      check(vkEnumeratePhysicalDevices(m_instance, &count, nullptr));
      std::vector<VkPhysicalDevice> devices(count);
      check(vkEnumeratePhysicalDevices(m_instance, &count, devices.data()));
      for (const auto device : devices) {
        VkPhysicalDevice16BitStorageFeatures storage16 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_16BIT_STORAGE_FEATURES };
        VkPhysicalDeviceVulkan12Features features12 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES, &storage16 };
        VkPhysicalDeviceFeatures2 features = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &features12 };
        vkGetPhysicalDeviceFeatures2(device, &features);
        if (features12.runtimeDescriptorArray && features12.shaderStorageBufferArrayNonUniformIndexing
            && features12.scalarBlockLayout && storage16.storageBuffer16BitAccess && features12.shaderInt8
            && storage16.uniformAndStorageBuffer16BitAccess
            && features.features.shaderInt16 && features.features.shaderInt64) {
          m_physical = device;
          break;
        }
      }
      expect(m_physical != VK_NULL_HANDLE, "No GPU supports the production shader's Vulkan features");
      VkPhysicalDeviceProperties properties;
      vkGetPhysicalDeviceProperties(m_physical, &properties);
      std::cout << "GPU: " << properties.deviceName << '\n';
      vkGetPhysicalDeviceMemoryProperties(m_physical, &m_memoryProperties);
      vkGetPhysicalDeviceQueueFamilyProperties(m_physical, &count, nullptr);
      std::vector<VkQueueFamilyProperties> families(count);
      vkGetPhysicalDeviceQueueFamilyProperties(m_physical, &count, families.data());
      uint32_t family = 0;
      while (family < count && !(families[family].queueFlags & VK_QUEUE_COMPUTE_BIT)) {
        family++;
      }
      expect(family < count, "No compute queue");
      float priority = 1.0f;
      VkDeviceQueueCreateInfo queueInfo = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
      queueInfo.queueFamilyIndex = family;
      queueInfo.queueCount = 1;
      queueInfo.pQueuePriorities = &priority;
      VkPhysicalDevice16BitStorageFeatures storage16 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_16BIT_STORAGE_FEATURES };
      storage16.storageBuffer16BitAccess = VK_TRUE;
      storage16.uniformAndStorageBuffer16BitAccess = VK_TRUE;
      VkPhysicalDeviceVulkan12Features features12 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES, &storage16 };
      features12.runtimeDescriptorArray = VK_TRUE;
      features12.shaderStorageBufferArrayNonUniformIndexing = VK_TRUE;
      features12.scalarBlockLayout = VK_TRUE;
      features12.shaderInt8 = VK_TRUE;
      VkPhysicalDeviceFeatures features = {};
      features.shaderInt16 = VK_TRUE;
      features.shaderInt64 = VK_TRUE;
      VkDeviceCreateInfo deviceInfo = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, &features12 };
      deviceInfo.queueCreateInfoCount = 1;
      deviceInfo.pQueueCreateInfos = &queueInfo;
      deviceInfo.pEnabledFeatures = &features;
      check(vkCreateDevice(m_physical, &deviceInfo, nullptr, &m_device));
      vkGetDeviceQueue(m_device, family, 0, &m_queue);

      m_positions = makeBuffer(VkDeviceSize(kMaxTriangles) * 9 * sizeof(float));
      m_indices = makeBuffer(VkDeviceSize(kMaxTriangles) * 3 * sizeof(uint32_t));
      m_distribution = makeBuffer((2 * kMaxTriangles + dxvk::particleDistributionBlockCount(kMaxTriangles) + 1) * sizeof(float));
      m_samples = makeBuffer(kSampleCount * sizeof(uint32_t));
      for (auto& constants : m_constants) {
        constants = makeBuffer(sizeof(ParticleSpawnDistributionConstants));
      }

      const VkDescriptorSetLayoutBinding bindings[] = {
        { 50, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
        { 63, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
        { 64, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr }
      };
      VkDescriptorSetLayoutCreateInfo setInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
      setInfo.bindingCount = uint32_t(std::size(bindings));
      setInfo.pBindings = bindings;
      check(vkCreateDescriptorSetLayout(m_device, &setInfo, nullptr, &m_layouts[0]));
      const VkDescriptorSetLayoutBinding geometryBinding = { 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
      setInfo.bindingCount = 1;
      setInfo.pBindings = &geometryBinding;
      check(vkCreateDescriptorSetLayout(m_device, &setInfo, nullptr, &m_layouts[1]));
      VkPipelineLayoutCreateInfo layoutInfo = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
      layoutInfo.setLayoutCount = 2;
      layoutInfo.pSetLayouts = m_layouts;
      check(vkCreatePipelineLayout(m_device, &layoutInfo, nullptr, &m_pipelineLayout));
      m_build = makePipeline(pBuildShader);
      m_sample = makePipeline(pSampleShader);
      const VkDescriptorPoolSize poolSizes[] = {
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, kPassCount }, { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2 * kPassCount + 2 }
      };
      VkDescriptorPoolCreateInfo poolInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
      poolInfo.maxSets = kPassCount + 1;
      poolInfo.poolSizeCount = 2;
      poolInfo.pPoolSizes = poolSizes;
      check(vkCreateDescriptorPool(m_device, &poolInfo, nullptr, &m_descriptorPool));
      VkDescriptorSetLayout layouts[kPassCount + 1];
      std::fill_n(layouts, kPassCount, m_layouts[0]);
      layouts[kPassCount] = m_layouts[1];
      VkDescriptorSetAllocateInfo allocate = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
      allocate.descriptorPool = m_descriptorPool;
      allocate.descriptorSetCount = kPassCount + 1;
      allocate.pSetLayouts = layouts;
      check(vkAllocateDescriptorSets(m_device, &allocate, m_sets));
      for (uint32_t pass = 0; pass < kPassCount; pass++) {
        bindBuffer(m_sets[pass], 50, 0, *m_constants[pass], VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
        bindBuffer(m_sets[pass], 63, 0, *m_distribution);
        bindBuffer(m_sets[pass], 64, 0, *m_samples);
      }
      bindBuffer(m_sets[kPassCount], 0, 0, *m_positions);
      bindBuffer(m_sets[kPassCount], 0, 1, *m_indices);
      VkCommandPoolCreateInfo commandPoolInfo = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
      commandPoolInfo.queueFamilyIndex = family;
      commandPoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
      check(vkCreateCommandPool(m_device, &commandPoolInfo, nullptr, &m_commandPool));
      VkCommandBufferAllocateInfo commandInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
      commandInfo.commandPool = m_commandPool;
      commandInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
      commandInfo.commandBufferCount = 1;
      check(vkAllocateCommandBuffers(m_device, &commandInfo, &m_command));
    }

    ~GpuFixture() {
      if (m_device) {
        vkDeviceWaitIdle(m_device);
        vkDestroyCommandPool(m_device, m_commandPool, nullptr);
        vkDestroyDescriptorPool(m_device, m_descriptorPool, nullptr);
        vkDestroyPipeline(m_device, m_build, nullptr);
        vkDestroyPipeline(m_device, m_sample, nullptr);
        vkDestroyPipelineLayout(m_device, m_pipelineLayout, nullptr);
        for (const auto layout : m_layouts) {
          vkDestroyDescriptorSetLayout(m_device, layout, nullptr);
        }
        m_positions.reset();
        m_indices.reset();
        m_distribution.reset();
        m_samples.reset();
        for (auto& constants : m_constants) {
          constants.reset();
        }
        vkDestroyDevice(m_device, nullptr);
      }
      vkDestroyInstance(m_instance, nullptr);
    }

    Distribution build(const std::vector<float>& weights, const dxvk::Matrix4& transform = dxvk::Matrix4(), bool indices16 = false,
                       float vertexScale = 1.0f, float edgeRatio = 1.0f) {
      std::vector<Triangle> triangles;
      triangles.reserve(weights.size());
      for (const float weight : weights) {
        triangles.push_back({ 0, 0, 0, weight * vertexScale * edgeRatio, 0, 0, 0, vertexScale / edgeRatio, 0 });
      }
      return buildGeometry(triangles, transform, indices16);
    }

    Distribution buildGeometry(const std::vector<Triangle>& triangles, const dxvk::Matrix4& transform = dxvk::Matrix4(),
                               bool indices16 = false) {
      expect(!triangles.empty() && triangles.size() <= kMaxTriangles, "Unsupported test triangle count");
      auto* pPositions = static_cast<float*>(m_positions->pData);
      auto* pIndices = static_cast<uint32_t*>(m_indices->pData);
      auto* pIndices16 = static_cast<uint16_t*>(m_indices->pData);
      expect(!indices16 || triangles.size() * 3 <= 65536, "16-bit test index overflow");
      for (uint32_t triangle = 0; triangle < triangles.size(); triangle++) {
        std::memcpy(pPositions + triangle * 9, triangles[triangle].data(), sizeof(Triangle));
        for (uint32_t vertex = 0; vertex < 3; vertex++) {
          const uint32_t index = triangle * 3 + vertex;
          if (indices16) {
            pIndices16[index] = uint16_t(index);
          } else {
            pIndices[index] = index;
          }
        }
      }
      ParticleSpawnDistributionConstants constants = {};
      constants.spawnContext.spawnObjectToWorld = mat4x3(dxvk::particleSpawnAreaTransform(transform));
      constants.spawnContext.spawnMeshPositionsStride = 3 * sizeof(float);
      constants.spawnContext.spawnMeshIndexIdx = 1;
      constants.spawnContext.indices32bit = !indices16;
      constants.spawnContext.numTriangles = uint32_t(triangles.size());
      for (uint32_t pass = 0; pass < kPassCount; pass++) {
        constants.pass = pass;
        std::memcpy(m_constants[pass]->pData, &constants, sizeof(constants));
      }
      const uint32_t blocks = dxvk::particleDistributionBlockCount(uint32_t(triangles.size()));
      check(vkResetCommandBuffer(m_command, 0));
      VkCommandBufferBeginInfo begin = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
      check(vkBeginCommandBuffer(m_command, &begin));
      vkCmdBindPipeline(m_command, VK_PIPELINE_BIND_POINT_COMPUTE, m_build);
      vkCmdBindDescriptorSets(m_command, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipelineLayout, 1, 1, &m_sets[kPassCount], 0, nullptr);
      for (uint32_t pass = 0; pass < kPassCount; pass++) {
        vkCmdBindDescriptorSets(m_command, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipelineLayout, 0, 1, &m_sets[pass], 0, nullptr);
        vkCmdDispatch(m_command, (pass == dxvk::kParticleDistributionMeasure || pass == dxvk::kParticleDistributionBuild) ? blocks : 1, 1, 1);
        barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
      }
      vkCmdBindPipeline(m_command, VK_PIPELINE_BIND_POINT_COMPUTE, m_sample);
      vkCmdDispatch(m_command, (kSampleCount + 63) / 64, 1, 1);
      barrier(VK_PIPELINE_STAGE_HOST_BIT, VK_ACCESS_HOST_READ_BIT);
      check(vkEndCommandBuffer(m_command));
      VkSubmitInfo submit = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
      submit.commandBufferCount = 1;
      submit.pCommandBuffers = &m_command;
      check(vkQueueSubmit(m_queue, 1, &submit, VK_NULL_HANDLE));
      check(vkQueueWaitIdle(m_queue));
      const auto* pPrefixes = static_cast<const float*>(m_distribution->pData);
      const auto* pSamples = static_cast<const uint32_t*>(m_samples->pData);
      return { { pPrefixes, pPrefixes + triangles.size() + blocks }, { pSamples, pSamples + kSampleCount } };
    }

  private:
    std::unique_ptr<Buffer> makeBuffer(VkDeviceSize size) {
      return std::make_unique<Buffer>(m_device, m_memoryProperties, size);
    }

    VkPipeline makePipeline(const char* pPath) {
      std::ifstream file(pPath, std::ios::binary | std::ios::ate);
      expect(bool(file), "Cannot open compiled shader");
      const auto bytes = size_t(file.tellg());
      expect(bytes > 0 && bytes % 4 == 0, "Invalid SPIR-V size");
      std::vector<uint32_t> code(bytes / 4);
      file.seekg(0);
      file.read(reinterpret_cast<char*>(code.data()), bytes);
      expect(bool(file), "Cannot read compiled shader");
      VkShaderModuleCreateInfo moduleInfo = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
      moduleInfo.codeSize = bytes;
      moduleInfo.pCode = code.data();
      VkShaderModule module;
      check(vkCreateShaderModule(m_device, &moduleInfo, nullptr, &module));
      VkComputePipelineCreateInfo info = { VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
      info.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
      info.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
      info.stage.module = module;
      info.stage.pName = "main";
      info.layout = m_pipelineLayout;
      VkPipeline pipeline;
      const auto result = vkCreateComputePipelines(m_device, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline);
      vkDestroyShaderModule(m_device, module, nullptr);
      check(result);
      return pipeline;
    }

    void bindBuffer(VkDescriptorSet set, uint32_t binding, uint32_t element, const Buffer& buffer,
                    VkDescriptorType type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER) {
      VkDescriptorBufferInfo info = { buffer.buffer, 0, buffer.size };
      VkWriteDescriptorSet write = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
      write.dstSet = set;
      write.dstBinding = binding;
      write.dstArrayElement = element;
      write.descriptorCount = 1;
      write.descriptorType = type;
      write.pBufferInfo = &info;
      vkUpdateDescriptorSets(m_device, 1, &write, 0, nullptr);
    }

    void barrier(VkPipelineStageFlags stage, VkAccessFlags access) {
      VkMemoryBarrier barrier = { VK_STRUCTURE_TYPE_MEMORY_BARRIER };
      barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
      barrier.dstAccessMask = access;
      vkCmdPipelineBarrier(m_command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, stage, 0, 1, &barrier, 0, nullptr, 0, nullptr);
    }

    VkInstance m_instance = VK_NULL_HANDLE;
    VkPhysicalDevice m_physical = VK_NULL_HANDLE;
    VkPhysicalDeviceMemoryProperties m_memoryProperties = {};
    VkDevice m_device = VK_NULL_HANDLE;
    VkQueue m_queue = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_layouts[2] = {};
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_build = VK_NULL_HANDLE;
    VkPipeline m_sample = VK_NULL_HANDLE;
    VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet m_sets[kPassCount + 1] = {};
    VkCommandPool m_commandPool = VK_NULL_HANDLE;
    VkCommandBuffer m_command = VK_NULL_HANDLE;
    std::unique_ptr<Buffer> m_positions;
    std::unique_ptr<Buffer> m_indices;
    std::unique_ptr<Buffer> m_distribution;
    std::unique_ptr<Buffer> m_samples;
    std::unique_ptr<Buffer> m_constants[kPassCount];
  };

  void verifyExclusion(const Distribution& distribution, const std::vector<float>& weights) {
    const uint32_t count = uint32_t(weights.size());
    const uint32_t blocks = dxvk::particleDistributionBlockCount(count);
    float previousBlock = 0;
    for (uint32_t block = 0; block < blocks; block++) {
      float previous = 0;
      bool positive = false;
      for (uint32_t i = block * dxvk::kParticleDistributionBlockSize;
           i < std::min(count, (block + 1) * dxvk::kParticleDistributionBlockSize); i++) {
        const float prefix = distribution.prefixes[i];
        expect(std::isfinite(prefix) && prefix >= previous, "GPU triangle prefixes are not ordered");
        if (!(weights[i] > 0 && std::isfinite(weights[i]))) {
          expect(prefix == previous, "Excluded triangle has a probability interval");
        } else {
          positive = true;
        }
        previous = prefix;
      }
      const float prefix = distribution.prefixes[count + block];
      expect(std::isfinite(prefix) && prefix >= previousBlock, "GPU block prefixes are not ordered");
      if (!positive) {
        expect(prefix == previousBlock, "Empty block has a probability interval");
      }
      previousBlock = prefix;
    }
    for (const auto triangle : distribution.samples) {
      if (previousBlock == 0) {
        expect(triangle == dxvk::kInvalidParticleTriangle, "Zero-area emitter accepted a spawn");
      } else {
        expect(triangle < count && weights[triangle] > 0 && std::isfinite(weights[triangle]), "GPU sample selected an excluded triangle");
      }
    }
  }

  void testPrefixes(GpuFixture& gpu) {
    const std::array<std::vector<float>, 2> cases = {{
      { 16777216, 1, 1, 0, 0, 16777216 },
      { 16777216, 1, 1, 1, 0, 16777216 }
    }};
    for (const auto& weights : cases) {
      for (const bool indices16 : { false, true }) {
        const auto result = gpu.build(weights, dxvk::Matrix4(), indices16);
        verifyExclusion(result, weights);
        expect(result.samples[2048] < weights.size() && weights[result.samples[2048]] > 0,
               "Sample 0.5 selected a zero-weight triangle");
      }
      for (const uint32_t firstBlock : { 0u, dxvk::kParticleDistributionBlockSize }) {
        std::vector<float> blockWeights((firstBlock + uint32_t(weights.size())) * dxvk::kParticleDistributionBlockSize, 0);
        if (firstBlock) {
          blockWeights[0] = 4;
        }
        for (uint32_t i = 0; i < weights.size(); i++) {
          blockWeights[(firstBlock + i) * dxvk::kParticleDistributionBlockSize] = weights[i];
        }
        verifyExclusion(gpu.build(blockWeights), blockWeights);
      }
    }
    for (const std::vector<float>& invalid : {
        std::vector<float> { 0, 0, 0 },
        std::vector<float> { 1, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN(), 0, 3 } }) {
      verifyExclusion(gpu.build(invalid), invalid);
    }
  }

  void testTransformProductUnderflow(GpuFixture& gpu) {
    for (const int exponent : { 80, 64, 100 }) {
      const float tiny = std::ldexp(1.0f, -exponent);
      const float large = std::ldexp(1.0f, exponent);
      dxvk::Matrix4 transform;
      transform[2][0] = 1.0f;
      transform[1][1] = tiny;
      const Triangle skew = { 0, 0, 0, tiny, tiny, 0, large, 2 * large, 0 };
      const Triangle reference = { 0, 0, 0, 1, 0, 0, 0, 1, 0 };
      for (const bool indices16 : { false, true }) {
        const auto result = gpu.buildGeometry({ skew, reference }, transform, indices16);
        verifyExclusion(result, { 1, 1 });
        expect(result.prefixes[0] / result.prefixes.back() == 0.5f,
               "Transformed-edge underflow changed equal areas into unequal probabilities");
        expect(std::count(result.samples.begin(), result.samples.end(), 0u) == 2048,
               "Transformed-edge underflow changed equal-area sampling");
        expect(result.samples[2048] == 1, "Equal-area midpoint selected the wrong triangle");
      }
    }
  }

  void testMixedMagnitudeEdges(GpuFixture& gpu) {
    const float large = std::ldexp(1.0f, 80);
    const float smallComponent = std::ldexp(1.0f, -80);
    // The last shape also cancels products that would overflow FP32. Its weight is sqrt(2); the others have weight 1.
    const std::array<Triangle, 3> skewed = {{
      { 0, 0, 0, large, smallComponent, 0, large, 0, 0 },
      { 0, 0, 0, large, 2 * smallComponent, 0, large, smallComponent, 0 },
      { 0, 0, 0, large, smallComponent, large, large, 0, large }
    }};
    for (const bool indices16 : { false, true }) {
      for (uint32_t shape = 0; shape < skewed.size(); shape++) {
        const auto& skew = skewed[shape];
        for (uint32_t axis = 0; axis < 3; axis++) {
          Triangle rotated;
          for (uint32_t vertex = 0; vertex < 3; vertex++) {
            for (uint32_t component = 0; component < 3; component++) {
              rotated[3 * vertex + (component + axis) % 3] = skew[3 * vertex + component];
            }
          }
          const auto single = gpu.buildGeometry({ rotated }, dxvk::Matrix4(), indices16);
          expect(std::isfinite(single.prefixes.back()) && single.prefixes.back() > 0,
                 "Mixed-magnitude edges lost their representable cross product");
          expect(std::all_of(single.samples.begin(), single.samples.end(), [](uint32_t triangle) { return triangle == 0; }),
                 "Skewed single-triangle emitter rejected births");

          const Triangle degenerate = {};
          const Triangle areaThree = { 0, 0, 0, 3, 0, shape == 2 ? 3.0f : 0.0f, 0, 1, 0 };
          const auto mixed = gpu.buildGeometry({ rotated, degenerate, areaThree }, dxvk::Matrix4(), indices16);
          verifyExclusion(mixed, { 1, 0, 3 });
          expect(std::abs(mixed.prefixes[0] / mixed.prefixes.back() - 0.25f) < 1e-6f, "Skewed geometry changed the 1:3 area ratio");
          expect(std::abs(std::count(mixed.samples.begin(), mixed.samples.end(), 0u) - 1024) <= 1,
                 "Skewed geometry changed sampling probabilities");
        }
      }
    }
  }

  void testTransformCancellation(GpuFixture& gpu) {
    const float large = std::ldexp(1.0f, 80);
    const float tiny = std::ldexp(1.0f, -80);
    dxvk::Matrix4 transform(0.0f);
    transform[0][0] = 0.5f;
    transform[1][0] = 0.5f;
    transform[2][0] = -0.5f;
    transform[1][1] = 1.5f;
    // The two huge X-transform terms cancel, leaving the tiny middle term.
    const Triangle skew = { 0, 0, 0, large, tiny, large, 1, -1, 0 };
    const Triangle reference = { 0, 0, 0, 0, tiny, 0, 1, -1, 0 };
    for (const bool indices16 : { false, true }) {
      const auto result = gpu.buildGeometry({ skew, reference }, transform, indices16);
      verifyExclusion(result, { 1, 1 });
      expect(result.prefixes[0] / result.prefixes.back() == 0.5f, "Transform cancellation lost a small area contribution");
      expect(std::count(result.samples.begin(), result.samples.end(), 0u) == 2048, "Transform cancellation changed equal-area sampling");
    }
  }

  void testUnrepresentableAbsoluteAreas(GpuFixture& gpu) {
    const auto rightTriangle = [](int exponent) -> Triangle {
      const float edge = std::ldexp(1.0f, exponent);
      return { 0, 0, 0, edge, 0, 0, 0, edge, 0 };
    };
    // One absolute area is representable; the other exceeds the FP32 range.
    const auto overflow = gpu.buildGeometry({ rightTriangle(63), rightTriangle(64) });
    verifyExclusion(overflow, { 1, 4 });
    expect(overflow.prefixes[0] / overflow.prefixes.back() == 0.2f,
           "Overflowing absolute areas changed relative probabilities");
    // Preserve a tiny positive interval even though its unscaled cross product underflows.
    const auto underflow = gpu.buildGeometry({ rightTriangle(-80), rightTriangle(-60) });
    verifyExclusion(underflow, { 1, std::ldexp(1.0f, 40) });
    expect(underflow.prefixes[0] / underflow.prefixes.back() == std::ldexp(1.0f, -40),
           "Underflowing absolute areas changed relative probabilities");
  }

  void testCommonAreaScaleAcrossBlocks(GpuFixture& gpu) {
    // Put the largest exponent beyond the first batch of 256 block maxima.
    std::vector<float> weights(257 * dxvk::kParticleDistributionBlockSize + 1, 0.0f);
    weights.front() = 1.0f;
    weights.back() = 3.0f;
    const auto result = gpu.build(weights);
    verifyExclusion(result, weights);
    expect(result.prefixes[0] / result.prefixes.back() == 0.25f, "Blocks used different area scales");
    expect(std::count(result.samples.begin(), result.samples.end(), 0u) == 1024,
           "Common area scale changed sampling probabilities across blocks");
  }

  void testCompensatedScale(GpuFixture& gpu) {
    const std::vector<float> weights = { 1, 0, 3 };
    const auto reference = gpu.build(weights);
    expect(reference.prefixes[0] / reference.prefixes.back() == 0.25f, "Relative face areas were not preserved");
    for (const int exponent : { -80, 80 }) {
      for (const bool indices16 : { false, true }) {
        const float vertexScale = std::ldexp(1.0f, exponent);
        const dxvk::Matrix4 transform(std::ldexp(1.0f, -exponent));
        const auto result = gpu.build(weights, transform, indices16, vertexScale);
        expect(result.prefixes.back() > 0, "Compensated vertex/transform scale lost all area");
        verifyExclusion(result, weights);
        expect(result.samples == reference.samples, "Compensated scale changed relative face probabilities");
        expect(result.prefixes[0] / result.prefixes.back() == 0.25f, "Compensated scale changed the 1:3 area ratio");

        const auto stretched = gpu.build(weights, dxvk::Matrix4(), indices16, 1.0f, vertexScale);
        expect(stretched.samples == reference.samples, "Reciprocal edge scales changed relative face probabilities");
        expect(stretched.prefixes[0] / stretched.prefixes.back() == 0.25f, "Reciprocal edge scales lost area");

        auto anisotropic = transform;
        anisotropic[2][2] = 1.0f;
        const auto flattened = gpu.build(weights, anisotropic, indices16, vertexScale);
        verifyExclusion(flattened, weights);
        expect(std::abs(flattened.prefixes[0] / flattened.prefixes.back() - 0.25f) < 1e-6f,
               "Anisotropic compensated scale changed relative areas");
        // Transform normalization can move the exact 0.25 boundary by one ULP.
        const auto firstTriangleSamples = std::count(flattened.samples.begin(), flattened.samples.end(), 0u);
        expect(std::abs(firstTriangleSamples - 1024) <= 1, "Anisotropic compensated scale changed sampling probabilities");
      }
    }
  }

  void testScaleRecovery(GpuFixture& gpu) {
    const std::vector<float> weights = { 1, 0, 3 };
    const dxvk::Matrix4 unit;
    const auto cold = gpu.build(weights, unit);
    verifyExclusion(cold, weights);
    for (const int exponent : { -80, 80 }) {
      const dxvk::Matrix4 extreme(std::ldexp(1.0f, exponent));
      const auto cached = gpu.build(weights, extreme);
      const bool reuse = dxvk::canReuseParticleSpawnAreas(dxvk::particleSpawnAreaMetric(extreme), dxvk::particleSpawnAreaMetric(unit));
      expect(reuse, "Uniform scale unnecessarily invalidated the static-emitter cache");
      const auto warm = reuse ? cached : gpu.build(weights, unit);
      expect(warm.prefixes == cold.prefixes && warm.samples == cold.samples, "Warm and cold distributions disagree after scale recovery");
      verifyExclusion(warm, weights);
    }
    const dxvk::Matrix4 collapsed(0.0f);
    const auto invalid = gpu.build(weights, collapsed);
    expect(invalid.prefixes.back() == 0, "Collapsed transform has positive area");
    expect(std::all_of(invalid.samples.begin(), invalid.samples.end(), [](uint32_t triangle) {
      return triangle == dxvk::kInvalidParticleTriangle;
    }), "Collapsed transform accepted a spawn");
    expect(!dxvk::canReuseParticleSpawnAreas(dxvk::particleSpawnAreaMetric(collapsed), dxvk::particleSpawnAreaMetric(unit)),
           "Restoring a collapsed emitter reused an empty table");
  }
}

int main(int argc, char** argv) {
  try {
    expect(argc == 3, "Usage: test_particle_spawn_distribution_gpu <production-build.spv> <sample.spv>");
    GpuFixture gpu(argv[1], argv[2]);
    testTransformProductUnderflow(gpu);
    testMixedMagnitudeEdges(gpu);
    testTransformCancellation(gpu);
    testUnrepresentableAbsoluteAreas(gpu);
    testPrefixes(gpu);
    testScaleRecovery(gpu);
    testCompensatedScale(gpu);
    testCommonAreaScaleAcrossBlocks(gpu);
    std::cout << "Production GPU distribution regressions passed.\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "GPU distribution regression failed: " << error.what() << '\n';
    return 1;
  }
}
