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

#include <algorithm>
#include <array>
#include <atomic>
#include <ctime>
#include <deque>
#include <fstream>
#include <iomanip>
#include <limits>
#include <memory>
#include <mutex>
#include <numeric>
#include <sstream>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>

#include "rtx_gpu_crash_recorder.h"
#include "rtx_instance_manager.h"

#include "../dxvk_buffer.h"

#include "../../util/log/log.h"
#include "../../util/util_env.h"
#include "../../util/util_string.h"
#include "../../util/xxHash/xxhash.h"

namespace dxvk {

namespace {

constexpr uint32_t kMaxRecordedFrames = 4;
constexpr size_t kMaxMetadataBytes = 64ull * 1024ull * 1024ull;
constexpr size_t kMaxRetainedSourceBytes = 512ull * 1024ull * 1024ull;
constexpr uint64_t kMaxDumpPayloadBytes = 512ull * 1024ull * 1024ull;
constexpr uint32_t kInvalidRecordIndex = std::numeric_limits<uint32_t>::max();

constexpr std::array<const char*, 7> kSourceBufferNames = {
  "position", "normal", "texcoord", "color0", "index", "blendWeight", "blendIndices"
};

template<typename T>
uint64_t handleValue(T handle) {
  if constexpr (std::is_pointer_v<T>) {
    return reinterpret_cast<uint64_t>(handle);
  } else {
    return static_cast<uint64_t>(handle);
  }
}

// Normalize composite keys before hashing so XXH3 never observes struct padding.
template<typename... Values>
size_t hashValues(Values... values) noexcept {
  static_assert((std::is_integral_v<Values> && ...));
  static_assert(sizeof(size_t) == sizeof(XXH64_hash_t));
  const std::array<uint64_t, sizeof...(Values)> data = {
    static_cast<uint64_t>(values)...
  };
  return static_cast<size_t>(XXH3_64bits(data.data(), sizeof(data)));
}

struct SourceBufferKey {
  uint64_t virtualBufferId = 0;
  VkDeviceSize virtualOffset = 0;
  VkDeviceSize size = 0;
  uint32_t stride = 0;
  uint32_t format = 0;
  bool isIndex = false;

  bool operator==(const SourceBufferKey& other) const {
    return virtualBufferId == other.virtualBufferId
        && virtualOffset == other.virtualOffset
        && size == other.size
        && stride == other.stride
        && format == other.format
        && isIndex == other.isIndex;
  }
};

struct SourceBufferKeyHash {
  size_t operator()(const SourceBufferKey& key) const noexcept {
    return hashValues(
      key.virtualBufferId,
      key.virtualOffset,
      key.size,
      key.stride,
      key.format,
      key.isIndex);
  }
};

struct SourceBufferRecord {
  SourceBufferKey key;
  Rc<DxvkBuffer> retainedBuffer;
  VkBuffer physicalHandle = VK_NULL_HANDLE;
  VkDeviceSize physicalOffset = 0;
  bool hostVisibleAtRecord = false;
  bool retentionDisabled = false;
  bool retentionBudgetExceeded = false;
};

struct GeometryRecord {
  uint64_t blasEntryId = 0;
  uint32_t frameCreated = kInvalidFrameIndex;
  uint32_t frameLastTouched = kInvalidFrameIndex;
  uint32_t frameLastUpdated = kInvalidFrameIndex;
  uint32_t vertexCount = 0;
  uint32_t indexCount = 0;
  uint32_t modifiedVertexCount = 0;
  uint32_t modifiedIndexCount = 0;
  uint32_t numBones = 0;
  uint32_t numBonesPerVertex = 0;
  uint32_t minBoneIndex = 0;
  uint32_t topology = 0;
  uint32_t cullMode = 0;
  uint32_t frontFace = 0;
  uint64_t boneHash = kEmptyHash;
  uint64_t modifiedLastBoneHash = kEmptyHash;
  VkDeviceAddress modifiedPositionAddress = 0;
  VkDeviceAddress modifiedIndexAddress = 0;
  std::array<uint64_t, static_cast<uint32_t>(HashComponents::Count)> sourceHashes = {};
  std::array<uint64_t, static_cast<uint32_t>(HashComponents::Count)> modifiedHashes = {};
  std::array<uint32_t, kSourceBufferNames.size()> sourceBuffers = {};
  std::vector<Matrix4> boneMatrices;
};

struct InstanceRecord {
  uint64_t instanceId = 0;
  uint64_t cacheIdentity = 0;
  uint64_t blasEntryId = 0;
  uint64_t materialHash = kEmptyHash;
  uint64_t materialDataHash = kEmptyHash;
  uint64_t texcoordHash = kEmptyHash;
  uint64_t indexHash = kEmptyHash;
  uint32_t vectorIndex = 0;
  uint32_t orderedSurfaceIndex = 0;
  uint32_t surfaceIndex = 0;
  uint32_t frameLastUpdated = 0;
  uint32_t categoryFlags = 0;
  uint32_t geometryIndex = kInvalidRecordIndex;
  VkAccelerationStructureInstanceKHR vkInstance = {};
};

struct AccelerationStructureRecord {
  VkAccelerationStructureKHR handle = VK_NULL_HANDLE;
  VkBuffer bufferHandle = VK_NULL_HANDLE;
  VkDeviceAddress deviceAddress = 0;
  VkDeviceSize bufferOffset = 0;
  VkDeviceSize size = 0;
  uint32_t frameLastTouched = kInvalidFrameIndex;
  uint64_t opacityMicromapSourceHash = kEmptyHash;
  uint64_t topologyHash = kEmptyHash;
  uint64_t contentHash = kEmptyHash;
  bool activeDynamic = false;
};

struct BuildGeometryRecord {
  VkGeometryTypeKHR type = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
  VkGeometryFlagsKHR flags = 0;
  VkAccelerationStructureBuildRangeInfoKHR range = {};
  bool hasExtension = false;

  VkFormat vertexFormat = VK_FORMAT_UNDEFINED;
  VkDeviceAddress vertexAddress = 0;
  VkDeviceSize vertexStride = 0;
  uint32_t maxVertex = 0;
  VkIndexType indexType = VK_INDEX_TYPE_NONE_KHR;
  VkDeviceAddress indexAddress = 0;
  VkDeviceAddress transformAddress = 0;

  VkDeviceAddress aabbAddress = 0;
  VkDeviceSize aabbStride = 0;

  VkBool32 arrayOfPointers = VK_FALSE;
  VkDeviceAddress instanceAddress = 0;

  bool hasOpacityMicromap = false;
  VkIndexType opacityMicromapIndexType = VK_INDEX_TYPE_NONE_KHR;
  VkDeviceAddress opacityMicromapIndexAddress = 0;
  VkDeviceSize opacityMicromapIndexStride = 0;
  uint32_t opacityMicromapBaseTriangle = 0;
  VkMicromapEXT opacityMicromap = VK_NULL_HANDLE;
  std::vector<VkMicromapUsageEXT> opacityMicromapUsageCounts;
};

struct BuildRecord {
  VkAccelerationStructureTypeKHR type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
  VkBuildAccelerationStructureFlagsKHR flags = 0;
  VkBuildAccelerationStructureModeKHR mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
  VkAccelerationStructureKHR source = VK_NULL_HANDLE;
  VkAccelerationStructureKHR destination = VK_NULL_HANDLE;
  VkDeviceAddress scratchAddress = 0;
  std::vector<BuildGeometryRecord> geometries;
};

struct TlasBuildRecord {
  uint32_t type = 0;
  uint32_t gpuGeneratedInstanceCount = 0;
  VkDeviceAddress instanceBufferAddress = 0;
  VkDeviceSize instanceBufferOffset = 0;
  VkDeviceAddress destinationAddress = 0;
  VkDeviceSize destinationSize = 0;
  BuildRecord build;
  std::vector<VkAccelerationStructureInstanceKHR> cpuInstances;
};

struct SceneRecord {
  uint32_t snapshotFrameId = 0;
  uint64_t sceneGeneration = 0;
  size_t metadataBytes = sizeof(SceneRecord);
  size_t retainedSourceBytes = 0;
  bool truncated = false;
  std::vector<SourceBufferRecord> sourceBuffers;
  std::vector<GeometryRecord> geometries;
  std::vector<InstanceRecord> instances;
  std::vector<AccelerationStructureRecord> accelerationStructures;
  std::unordered_map<SourceBufferKey, uint32_t, SourceBufferKeyHash> sourceBufferIndices;
  std::unordered_map<uint64_t, uint32_t> geometryIndices;
  std::unordered_set<DxvkBuffer*> retainedBuffers;
  std::unordered_set<uint64_t> accelerationStructureHandles;
};

struct FrameRecord {
  uint32_t frameId = 0;
  size_t metadataBytes = sizeof(FrameRecord);
  bool blasTransformsRecorded = false;
  bool truncated = false;
  VkDeviceAddress blasTransformBufferAddress = 0;
  std::vector<VkTransformMatrixKHR> blasTransforms;
  std::vector<BuildRecord> blasBuilds;
  std::vector<TlasBuildRecord> tlasBuilds;
  std::shared_ptr<SceneRecord> scene;
};

template<typename Record>
bool reserveMetadata(Record& record, size_t bytes) {
  if (bytes > kMaxMetadataBytes || record.metadataBytes > kMaxMetadataBytes - bytes) {
    record.truncated = true;
    return false;
  }

  record.metadataBytes += bytes;
  return true;
}

BuildRecord copyBuild(
  const VkAccelerationStructureBuildGeometryInfoKHR& build,
  const VkAccelerationStructureBuildRangeInfoKHR* ranges) {
  BuildRecord result;
  result.type = build.type;
  result.flags = build.flags;
  result.mode = build.mode;
  result.source = build.srcAccelerationStructure;
  result.destination = build.dstAccelerationStructure;
  result.scratchAddress = build.scratchData.deviceAddress;
  result.geometries.reserve(build.geometryCount);

  // Snapshot every geometry because the Vulkan build descriptor arrays are transient.
  for (uint32_t i = 0; i < build.geometryCount; i++) {
    const VkAccelerationStructureGeometryKHR* geometry = build.pGeometries != nullptr
      ? &build.pGeometries[i]
      : build.ppGeometries[i];

    BuildGeometryRecord geometryRecord;
    geometryRecord.type = geometry->geometryType;
    geometryRecord.flags = geometry->flags;
    geometryRecord.hasExtension = geometry->pNext != nullptr;
    if (ranges != nullptr) {
      geometryRecord.range = ranges[i];
    }

    switch (geometry->geometryType) {
      case VK_GEOMETRY_TYPE_TRIANGLES_KHR: {
        const auto& triangles = geometry->geometry.triangles;
        geometryRecord.hasExtension |= triangles.pNext != nullptr;
        geometryRecord.vertexFormat = triangles.vertexFormat;
        geometryRecord.vertexAddress = triangles.vertexData.deviceAddress;
        geometryRecord.vertexStride = triangles.vertexStride;
        geometryRecord.maxVertex = triangles.maxVertex;
        geometryRecord.indexType = triangles.indexType;
        geometryRecord.indexAddress = triangles.indexData.deviceAddress;
        geometryRecord.transformAddress = triangles.transformData.deviceAddress;

        const VkBaseInStructure* extension =
          reinterpret_cast<const VkBaseInStructure*>(triangles.pNext);
        // Preserve the optional opacity micromap descriptor from the pNext chain.
        while (extension != nullptr) {
          if (extension->sType == VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_TRIANGLES_OPACITY_MICROMAP_EXT) {
            const auto* opacityMicromap =
              reinterpret_cast<const VkAccelerationStructureTrianglesOpacityMicromapEXT*>(extension);
            geometryRecord.hasOpacityMicromap = true;
            geometryRecord.opacityMicromapIndexType = opacityMicromap->indexType;
            geometryRecord.opacityMicromapIndexAddress = opacityMicromap->indexBuffer.deviceAddress;
            geometryRecord.opacityMicromapIndexStride = opacityMicromap->indexStride;
            geometryRecord.opacityMicromapBaseTriangle = opacityMicromap->baseTriangle;
            geometryRecord.opacityMicromap = opacityMicromap->micromap;
            geometryRecord.opacityMicromapUsageCounts.reserve(opacityMicromap->usageCountsCount);
            // Flatten either Vulkan usage-count representation into owned storage.
            for (uint32_t usageIndex = 0; usageIndex < opacityMicromap->usageCountsCount; usageIndex++) {
              const VkMicromapUsageEXT* usage = opacityMicromap->pUsageCounts != nullptr
                ? &opacityMicromap->pUsageCounts[usageIndex]
                : (opacityMicromap->ppUsageCounts != nullptr
                  ? opacityMicromap->ppUsageCounts[usageIndex]
                  : nullptr);
              if (usage != nullptr) {
                geometryRecord.opacityMicromapUsageCounts.push_back(*usage);
              }
            }
          }
          extension = extension->pNext;
        }
      } break;

      case VK_GEOMETRY_TYPE_AABBS_KHR: {
        const auto& aabbs = geometry->geometry.aabbs;
        geometryRecord.hasExtension |= aabbs.pNext != nullptr;
        geometryRecord.aabbAddress = aabbs.data.deviceAddress;
        geometryRecord.aabbStride = aabbs.stride;
      } break;

      case VK_GEOMETRY_TYPE_INSTANCES_KHR: {
        const auto& instances = geometry->geometry.instances;
        geometryRecord.hasExtension |= instances.pNext != nullptr;
        geometryRecord.arrayOfPointers = instances.arrayOfPointers;
        geometryRecord.instanceAddress = instances.data.deviceAddress;
      } break;

      default:
        break;
    }

    result.geometries.push_back(geometryRecord);
  }

  return result;
}

VkDeviceSize calculateSourceSize(const RasterBuffer& buffer, uint32_t elementCount) {
  if (!buffer.defined() || buffer.offsetFromSlice() >= buffer.length() || buffer.stride() == 0) {
    return 0;
  }

  const VkDeviceSize available = buffer.length() - buffer.offsetFromSlice();
  const uint64_t requested = uint64_t(elementCount) * uint64_t(buffer.stride());
  return std::min<VkDeviceSize>(available, requested);
}

uint32_t addSourceBuffer(
  SceneRecord& scene,
  const RasterBuffer& buffer,
  uint32_t elementCount,
  bool isIndex,
  bool retainSourceBuffers) {
  if (!buffer.defined()) {
    return kInvalidRecordIndex;
  }

  SourceBufferKey key;
  key.virtualBufferId = reinterpret_cast<uint64_t>(buffer.buffer().ptr());
  key.virtualOffset = buffer.offset() + buffer.offsetFromSlice();
  key.size = calculateSourceSize(buffer, elementCount);
  key.stride = buffer.stride();
  key.format = isIndex
    ? static_cast<uint32_t>(buffer.indexType())
    : static_cast<uint32_t>(buffer.vertexFormat());
  key.isIndex = isIndex;

  auto existing = scene.sourceBufferIndices.find(key);
  if (existing != scene.sourceBufferIndices.end()) {
    return existing->second;
  }

  if (!reserveMetadata(scene, sizeof(SourceBufferRecord))) {
    return kInvalidRecordIndex;
  }

  SourceBufferRecord record;
  record.key = key;
  const DxvkBufferSliceHandle physicalSlice = buffer.buffer()->getSliceHandle(key.virtualOffset, key.size);
  record.physicalHandle = physicalSlice.handle;
  record.physicalOffset = physicalSlice.offset;
  record.hostVisibleAtRecord = physicalSlice.mapPtr != nullptr;

  if (record.hostVisibleAtRecord) {
    if (!retainSourceBuffers) {
      record.retentionDisabled = true;
    } else {
      // Explicitly retain host-visible source allocations so their CPU mappings
      // remain available if the virtual buffer is renamed before a crash.
      DxvkBuffer* pBuffer = buffer.buffer().ptr();
      const bool alreadyRetained = scene.retainedBuffers.find(pBuffer) != scene.retainedBuffers.end();
      const size_t bufferSize = buffer.buffer()->info().size;
      if (alreadyRetained || (bufferSize <= kMaxRetainedSourceBytes
          && scene.retainedSourceBytes <= kMaxRetainedSourceBytes - bufferSize)) {
        record.retainedBuffer = buffer.buffer();
        if (!alreadyRetained) {
          scene.retainedBuffers.insert(pBuffer);
          scene.retainedSourceBytes += bufferSize;
        }
      } else {
        record.retentionBudgetExceeded = true;
      }
    }
  }

  const uint32_t index = static_cast<uint32_t>(scene.sourceBuffers.size());
  scene.sourceBuffers.push_back(std::move(record));
  scene.sourceBufferIndices.emplace(key, index);
  return index;
}

uint32_t addGeometry(
  SceneRecord& scene,
  const BlasEntry& blasEntry,
  bool retainSourceBuffers) {
  const uint64_t blasEntryId = reinterpret_cast<uint64_t>(&blasEntry);
  auto existing = scene.geometryIndices.find(blasEntryId);
  if (existing != scene.geometryIndices.end()) {
    return existing->second;
  }

  const RasterGeometry& source = blasEntry.input.getGeometryData();
  const RaytraceGeometry& modified = blasEntry.modifiedGeometryData;
  const SkinningData& skinning = blasEntry.input.getSkinningState();
  const size_t boneBytes = skinning.pBoneMatrices.size() * sizeof(Matrix4);
  if (!reserveMetadata(scene, sizeof(GeometryRecord) + boneBytes)) {
    return kInvalidRecordIndex;
  }

  GeometryRecord record;
  record.sourceBuffers.fill(kInvalidRecordIndex);
  record.blasEntryId = blasEntryId;
  record.frameCreated = blasEntry.frameCreated;
  record.frameLastTouched = blasEntry.frameLastTouched;
  record.frameLastUpdated = blasEntry.frameLastUpdated;
  record.vertexCount = source.vertexCount;
  record.indexCount = source.indexCount;
  record.modifiedVertexCount = modified.vertexCount;
  record.modifiedIndexCount = modified.indexCount;
  record.numBones = skinning.numBones;
  record.numBonesPerVertex = skinning.numBonesPerVertex;
  record.minBoneIndex = skinning.minBoneIndex;
  record.topology = static_cast<uint32_t>(source.topology);
  record.cullMode = source.cullMode;
  record.frontFace = static_cast<uint32_t>(source.frontFace);
  record.boneHash = skinning.boneHash;
  record.modifiedLastBoneHash = modified.lastBoneHash;
  record.boneMatrices = skinning.pBoneMatrices;

  if (modified.positionBuffer.defined()) {
    record.modifiedPositionAddress = modified.positionBuffer.getDeviceAddress()
      + modified.positionBuffer.offsetFromSlice();
  }
  if (modified.indexBuffer.defined()) {
    record.modifiedIndexAddress = modified.indexBuffer.getDeviceAddress()
      + modified.indexBuffer.offsetFromSlice();
  }

  // Copy both pre-processing and post-processing hashes for later comparison.
  for (uint32_t i = 0; i < static_cast<uint32_t>(HashComponents::Count); i++) {
    const HashComponents component = static_cast<HashComponents>(i);
    record.sourceHashes[i] = source.hashes[component];
    record.modifiedHashes[i] = modified.hashes[component];
  }

  record.sourceBuffers[0] = addSourceBuffer(
    scene, source.positionBuffer, source.vertexCount, false, retainSourceBuffers);
  record.sourceBuffers[1] = addSourceBuffer(
    scene, source.normalBuffer, source.vertexCount, false, retainSourceBuffers);
  record.sourceBuffers[2] = addSourceBuffer(
    scene, source.texcoordBuffer, source.vertexCount, false, retainSourceBuffers);
  record.sourceBuffers[3] = addSourceBuffer(
    scene, source.color0Buffer, source.vertexCount, false, retainSourceBuffers);
  record.sourceBuffers[4] = addSourceBuffer(
    scene, source.indexBuffer, source.indexCount, true, retainSourceBuffers);
  record.sourceBuffers[5] = addSourceBuffer(
    scene, source.blendWeightBuffer, source.vertexCount, false, retainSourceBuffers);
  record.sourceBuffers[6] = addSourceBuffer(
    scene, source.blendIndicesBuffer, source.vertexCount, false, retainSourceBuffers);

  const uint32_t index = static_cast<uint32_t>(scene.geometries.size());
  scene.geometries.push_back(std::move(record));
  scene.geometryIndices.emplace(blasEntryId, index);
  return index;
}

void addAccelerationStructure(
  SceneRecord& scene,
  const Rc<PooledBlas>& pooledBlas,
  bool activeDynamic) {
  if (pooledBlas == nullptr || pooledBlas->accelStructure == nullptr) {
    return;
  }

  const uint64_t handle = handleValue(pooledBlas->accelStructure->getAccelStructure());
  if (scene.accelerationStructureHandles.find(handle) != scene.accelerationStructureHandles.end()) {
    if (activeDynamic) {
      // The dynamic list is a subset of the pool, so upgrade the existing record.
      for (AccelerationStructureRecord& record : scene.accelerationStructures) {
        if (handleValue(record.handle) == handle) {
          record.activeDynamic = true;
          break;
        }
      }
    }
    return;
  }

  if (!reserveMetadata(scene, sizeof(AccelerationStructureRecord))) {
    return;
  }

  const DxvkBufferSliceHandle slice = pooledBlas->accelStructure->getSliceHandle();
  AccelerationStructureRecord record;
  record.handle = pooledBlas->accelStructure->getAccelStructure();
  record.bufferHandle = slice.handle;
  record.deviceAddress = pooledBlas->accelerationStructureReference;
  record.bufferOffset = slice.offset;
  record.size = pooledBlas->accelStructure->info().size;
  record.frameLastTouched = pooledBlas->frameLastTouched;
  record.opacityMicromapSourceHash = pooledBlas->opacityMicromapSourceHash;
  record.topologyHash = pooledBlas->topologyHash;
  record.contentHash = pooledBlas->contentHash;
  record.activeDynamic = activeDynamic;
  scene.accelerationStructures.push_back(record);
  scene.accelerationStructureHandles.insert(handle);
}

struct DumpedRangeKey {
  SourceBufferKey source;
  uint64_t physicalHandle = 0;
  VkDeviceSize physicalOffset = 0;

  bool operator==(const DumpedRangeKey& other) const {
    return source == other.source
        && physicalHandle == other.physicalHandle
        && physicalOffset == other.physicalOffset;
  }
};

struct DumpedRangeKeyHash {
  size_t operator()(const DumpedRangeKey& key) const noexcept {
    return hashValues(
      key.source.virtualBufferId,
      key.source.virtualOffset,
      key.source.size,
      key.source.stride,
      key.source.format,
      key.source.isIndex,
      key.physicalHandle,
      key.physicalOffset);
  }
};

struct DumpedRange {
  std::string status;
  uint64_t binaryOffset = 0;
  uint64_t binarySize = 0;
};

bool writeBinary(
  std::ofstream& stream,
  const void* data,
  uint64_t size,
  uint64_t& payloadBytes,
  uint64_t& offset) {
  if (!stream.is_open() || data == nullptr || size == 0
      || size > kMaxDumpPayloadBytes || payloadBytes > kMaxDumpPayloadBytes - size) {
    return false;
  }

  offset = payloadBytes;
  stream.write(reinterpret_cast<const char*>(data), size);
  if (!stream.good()) {
    return false;
  }

  payloadBytes += size;
  return true;
}

void writeBuild(std::ofstream& stream, const BuildRecord& build, const char* prefix, size_t index) {
  stream << prefix << '[' << index << "] type=" << static_cast<uint32_t>(build.type)
         << " flags=0x" << std::hex << build.flags
         << " mode=" << std::dec << static_cast<uint32_t>(build.mode)
         << " src=0x" << std::hex << handleValue(build.source)
         << " dst=0x" << handleValue(build.destination)
         << " scratch=0x" << build.scratchAddress
         << " geometryCount=" << std::dec << build.geometries.size() << '\n';

  // Serialize every geometry nested under this acceleration structure build.
  for (size_t i = 0; i < build.geometries.size(); i++) {
    const BuildGeometryRecord& geometry = build.geometries[i];
    stream << "  geometry[" << i << "] type=" << static_cast<uint32_t>(geometry.type)
           << " flags=0x" << std::hex << geometry.flags
           << " primitiveCount=" << std::dec << geometry.range.primitiveCount
           << " primitiveOffset=" << geometry.range.primitiveOffset
           << " firstVertex=" << geometry.range.firstVertex
           << " transformOffset=" << geometry.range.transformOffset
           << " hasExtension=" << geometry.hasExtension;

    switch (geometry.type) {
      case VK_GEOMETRY_TYPE_TRIANGLES_KHR:
        stream << " vertexFormat=" << static_cast<uint32_t>(geometry.vertexFormat)
               << " vertexAddress=0x" << std::hex << geometry.vertexAddress
               << " vertexStride=" << std::dec << geometry.vertexStride
               << " maxVertex=" << geometry.maxVertex
               << " indexType=" << static_cast<uint32_t>(geometry.indexType)
               << " indexAddress=0x" << std::hex << geometry.indexAddress
               << " transformAddress=0x" << geometry.transformAddress;
        if (geometry.hasOpacityMicromap) {
          stream << " opacityMicromap=0x" << handleValue(geometry.opacityMicromap)
                 << " opacityMicromapIndexType=" << std::dec
                 << static_cast<uint32_t>(geometry.opacityMicromapIndexType)
                 << " opacityMicromapIndexAddress=0x" << std::hex
                 << geometry.opacityMicromapIndexAddress
                 << " opacityMicromapIndexStride=" << std::dec
                 << geometry.opacityMicromapIndexStride
                 << " opacityMicromapBaseTriangle=" << geometry.opacityMicromapBaseTriangle
                 << " opacityMicromapUsageCounts=";
          for (size_t usageIndex = 0;
               usageIndex < geometry.opacityMicromapUsageCounts.size();
               usageIndex++) {
            const VkMicromapUsageEXT& usage = geometry.opacityMicromapUsageCounts[usageIndex];
            stream << (usageIndex == 0 ? "" : ",")
                   << usage.count << ':'
                   << usage.subdivisionLevel << ':'
                   << static_cast<uint32_t>(usage.format);
          }
        }
        break;

      case VK_GEOMETRY_TYPE_AABBS_KHR:
        stream << " aabbAddress=0x" << std::hex << geometry.aabbAddress
               << " aabbStride=" << std::dec << geometry.aabbStride;
        break;

      case VK_GEOMETRY_TYPE_INSTANCES_KHR:
        stream << " arrayOfPointers=" << std::dec << geometry.arrayOfPointers
               << " instanceAddress=0x" << std::hex << geometry.instanceAddress;
        break;

      default:
        break;
    }

    stream << std::dec << '\n';
  }
}

} // anonymous namespace

struct RtxGpuCrashRecorder::State {
  explicit State(bool shouldRetainSourceBuffers)
    : retainSourceBuffers(shouldRetainSourceBuffers) {
  }

  bool retainSourceBuffers = false;
  mutable std::atomic<bool> dumpStarted = false;
  mutable std::mutex mutex;
  std::deque<FrameRecord> frames;
  std::shared_ptr<SceneRecord> lastScene;
};

static FrameRecord& getFrame(RtxGpuCrashRecorder::State& state, uint32_t frameId) {
  if (state.frames.empty() || state.frames.back().frameId != frameId) {
    FrameRecord frame;
    frame.frameId = frameId;
    frame.scene = state.lastScene;
    state.frames.push_back(std::move(frame));

    // Keep only the most recent fixed-size window of frames.
    while (state.frames.size() > kMaxRecordedFrames) {
      state.frames.pop_front();
    }
  }

  return state.frames.back();
}

static void trimFrames(RtxGpuCrashRecorder::State& state) {
  auto getMetadataBytes = [&state] {
    size_t result = 0;
    std::unordered_set<const SceneRecord*> uniqueScenes;
    // Shared static-scene snapshots only consume metadata once.
    for (const FrameRecord& frame : state.frames) {
      result += frame.metadataBytes;
      if (frame.scene != nullptr && uniqueScenes.insert(frame.scene.get()).second) {
        result += frame.scene->metadataBytes;
      }
    }
    return result;
  };

  auto getRetainedBytes = [&state] {
    size_t result = 0;
    std::unordered_set<DxvkBuffer*> uniqueBuffers;
    std::unordered_set<const SceneRecord*> uniqueScenes;
    // A scene or source allocation referenced by multiple frames only consumes memory once.
    for (const FrameRecord& frame : state.frames) {
      if (frame.scene == nullptr || !uniqueScenes.insert(frame.scene.get()).second) {
        continue;
      }
      for (const SourceBufferRecord& buffer : frame.scene->sourceBuffers) {
        if (buffer.retainedBuffer != nullptr
            && uniqueBuffers.insert(buffer.retainedBuffer.ptr()).second) {
          result += buffer.retainedBuffer->info().size;
        }
      }
    }
    return result;
  };

  // Preserve the newest frame even when a single frame exceeds the global budget.
  while (state.frames.size() > 1
      && (getMetadataBytes() > kMaxMetadataBytes || getRetainedBytes() > kMaxRetainedSourceBytes)) {
    state.frames.pop_front();
  }
}

RtxGpuCrashRecorder::RtxGpuCrashRecorder(bool enabled, bool retainSourceBuffers)
  : m_state(enabled ? std::make_unique<State>(retainSourceBuffers) : nullptr) {
}

RtxGpuCrashRecorder::~RtxGpuCrashRecorder() = default;

void RtxGpuCrashRecorder::clear() {
  if (m_state == nullptr) {
    return;
  }

  std::lock_guard lock(m_state->mutex);
  m_state->frames.clear();
  m_state->lastScene.reset();
}

void RtxGpuCrashRecorder::recordScene(
  uint32_t frameId,
  uint64_t sceneGeneration,
  const std::vector<RtInstance*>& instances,
  const std::vector<Rc<PooledBlas>>& pooledBlases,
  const std::vector<Rc<PooledBlas>>& activeDynamicBlases) {
  if (m_state == nullptr || m_state->dumpStarted.load()) {
    return;
  }

  std::lock_guard lock(m_state->mutex);
  FrameRecord& frame = getFrame(*m_state, frameId);
  if (frame.scene != nullptr && frame.scene->sceneGeneration == sceneGeneration) {
    // getFrame carries the immutable prior snapshot forward on static scenes.
    return;
  }

  auto scene = std::make_shared<SceneRecord>();
  scene->snapshotFrameId = frameId;
  scene->sceneGeneration = sceneGeneration;

  // Capture the instance-to-geometry mapping used to assemble the TLAS.
  for (size_t i = 0; i < instances.size(); i++) {
    const RtInstance* instance = instances[i];
    if (instance == nullptr || !reserveMetadata(*scene, sizeof(InstanceRecord))) {
      continue;
    }

    InstanceRecord record;
    record.instanceId = instance->getId();
    record.cacheIdentity = instance->getCacheIdentity();
    record.vectorIndex = instance->getVectorIdx();
    record.orderedSurfaceIndex = static_cast<uint32_t>(i);
    record.surfaceIndex = instance->getSurfaceIndex();
    record.frameLastUpdated = instance->getFrameLastUpdated();
    record.categoryFlags = instance->getCategoryFlags().raw();
    record.materialHash = instance->getMaterialHash();
    record.materialDataHash = instance->getMaterialDataHash();
    record.texcoordHash = instance->getTexcoordHash();
    record.indexHash = instance->getIndexHash();
    record.vkInstance = instance->getVkInstance();

    const BlasEntry* blasEntry = instance->getBlas();
    if (blasEntry != nullptr) {
      record.blasEntryId = reinterpret_cast<uint64_t>(blasEntry);
      record.geometryIndex = addGeometry(
        *scene, *blasEntry, m_state->retainSourceBuffers);
    }

    scene->instances.push_back(record);
  }

  // Record the complete BLAS pool first, then mark its active dynamic subset.
  for (const Rc<PooledBlas>& blas : pooledBlases) {
    addAccelerationStructure(*scene, blas, false);
  }
  for (const Rc<PooledBlas>& blas : activeDynamicBlases) {
    addAccelerationStructure(*scene, blas, true);
  }

  // These maps and sets only deduplicate data while recording this scene.
  scene->sourceBufferIndices.clear();
  scene->sourceBufferIndices.rehash(0);
  scene->geometryIndices.clear();
  scene->geometryIndices.rehash(0);
  scene->retainedBuffers.clear();
  scene->retainedBuffers.rehash(0);
  scene->accelerationStructureHandles.clear();
  scene->accelerationStructureHandles.rehash(0);

  m_state->lastScene = scene;
  frame.scene = std::move(scene);

  trimFrames(*m_state);
}

void RtxGpuCrashRecorder::recordBlasBuilds(
  uint32_t frameId,
  const std::vector<VkAccelerationStructureBuildGeometryInfoKHR>& builds,
  const std::vector<VkAccelerationStructureBuildRangeInfoKHR*>& ranges,
  VkDeviceAddress transformBufferAddress,
  const std::vector<VkTransformMatrixKHR>& transforms) {
  if (m_state == nullptr || m_state->dumpStarted.load()) {
    return;
  }

  std::lock_guard lock(m_state->mutex);
  FrameRecord& frame = getFrame(*m_state, frameId);
  if (!frame.blasTransformsRecorded) {
    const size_t transformBytes = transforms.size() * sizeof(VkTransformMatrixKHR);
    if (reserveMetadata(frame, transformBytes)) {
      frame.blasTransformsRecorded = true;
      frame.blasTransformBufferAddress = transformBufferAddress;
      frame.blasTransforms = transforms;
    }
  }

  const size_t count = std::min(builds.size(), ranges.size());
  // Own each BLAS build descriptor and its nested geometry metadata.
  for (size_t i = 0; i < count; i++) {
    BuildRecord record = copyBuild(builds[i], ranges[i]);
    const size_t bytes = sizeof(BuildRecord)
      + builds[i].geometryCount * sizeof(BuildGeometryRecord)
      + std::accumulate(
          record.geometries.begin(),
          record.geometries.end(),
          size_t(0),
          [] (size_t total, const BuildGeometryRecord& geometry) {
            return total + geometry.opacityMicromapUsageCounts.size() * sizeof(VkMicromapUsageEXT);
          });
    if (!reserveMetadata(frame, bytes)) {
      break;
    }
    frame.blasBuilds.push_back(std::move(record));
  }

  trimFrames(*m_state);
}

void RtxGpuCrashRecorder::recordTlasBuild(
  uint32_t frameId,
  uint32_t tlasType,
  const VkAccelerationStructureBuildGeometryInfoKHR& build,
  const VkAccelerationStructureBuildRangeInfoKHR& range,
  const std::vector<VkAccelerationStructureInstanceKHR>& cpuInstances,
  uint32_t gpuGeneratedInstanceCount,
  VkDeviceAddress instanceBufferAddress,
  VkDeviceSize instanceBufferOffset,
  const Rc<DxvkAccelStructure>& destination) {
  if (m_state == nullptr || m_state->dumpStarted.load()) {
    return;
  }

  std::lock_guard lock(m_state->mutex);
  FrameRecord& frame = getFrame(*m_state, frameId);
  const size_t bytes = sizeof(TlasBuildRecord)
    + sizeof(BuildGeometryRecord)
    + cpuInstances.size() * sizeof(VkAccelerationStructureInstanceKHR);
  if (!reserveMetadata(frame, bytes)) {
    return;
  }

  TlasBuildRecord record;
  record.type = tlasType;
  record.gpuGeneratedInstanceCount = gpuGeneratedInstanceCount;
  record.instanceBufferAddress = instanceBufferAddress;
  record.instanceBufferOffset = instanceBufferOffset;
  record.build = copyBuild(build, &range);
  record.cpuInstances = cpuInstances;
  if (destination != nullptr) {
    record.destinationAddress = destination->getAccelDeviceAddress();
    record.destinationSize = destination->info().size;
  }
  frame.tlasBuilds.push_back(std::move(record));

  trimFrames(*m_state);
}

void RtxGpuCrashRecorder::dump(const char* reason) const {
  if (m_state == nullptr || m_state->dumpStarted.exchange(true)) {
    return;
  }

  std::lock_guard lock(m_state->mutex);

  const std::time_t currentTime = std::time(nullptr);
  std::tm localTime = {};
#ifdef _WIN32
  localtime_s(&localTime, &currentTime);
#else
  localtime_r(&currentTime, &localTime);
#endif
  std::ostringstream timestamp;
  timestamp << std::put_time(&localTime, "%Y%m%d-%H%M%S");

  std::string path = env::getEnvVar("DXVK_AFTERMATH_DUMP_PATH");
  if (!path.empty() && path.back() != '/' && path.back() != '\\') {
    path += '/';
  }

  const std::string baseName = str::format(
    path, env::getExeNameNoSuffix(), "_", timestamp.str(), "_gpucrash_state");
  const std::string stateFilename = baseName + ".log";
  const std::string binaryFilename = baseName + ".bin";

  std::ofstream stateFile(str::tows(stateFilename.c_str()).c_str());
  if (!stateFile.is_open()) {
    Logger::warn(str::format(
      "GPU crash state recorder failed to open: ", stateFilename));
    return;
  }

  std::ofstream binaryFile(
    str::tows(binaryFilename.c_str()).c_str(),
    std::ios::binary | std::ios::trunc);
  const bool binaryFileOpened = binaryFile.is_open();
  if (!binaryFileOpened) {
    Logger::warn(str::format(
      "GPU crash state recorder failed to open binary payload: ", binaryFilename));
  }

  uint64_t payloadBytes = 0;
  // Multiple frames often reference the same physical range; serialize it once.
  std::unordered_map<DumpedRangeKey, DumpedRange, DumpedRangeKeyHash> dumpedRanges;

  stateFile << "Remix GPU crash CPU flight recorder\n"
            << "reason=" << (reason != nullptr ? reason : "unknown") << '\n'
            << "recordedFrameCount=" << m_state->frames.size() << '\n'
            << "binaryPayload=" << binaryFilename << '\n'
            << "binaryPayloadOpened=" << binaryFileOpened << '\n'
            << "binaryPayloadLimit=" << kMaxDumpPayloadBytes << '\n'
            << "sourceBufferRetention=" << m_state->retainSourceBuffers << '\n'
            << "notes=Source buffers are pre-interleave/pre-skinning CPU inputs. "
               "GPU-modified addresses and exact build descriptors are metadata only.\n"
            << "notes=Point-instancer slots are GPU-generated and are counted but are not present in the CPU payload.\n\n";

  const SceneRecord emptyScene;
  // Serialize each retained frame and its associated scene snapshot.
  for (const FrameRecord& frame : m_state->frames) {
    const bool hasScene = frame.scene != nullptr;
    const SceneRecord& scene = hasScene ? *frame.scene : emptyScene;
    stateFile << "[frame] id=" << frame.frameId
              << " sceneGeneration=" << scene.sceneGeneration
              << " sceneSnapshotFrame=";
    if (hasScene) {
      stateFile << scene.snapshotFrameId;
    } else {
      stateFile << "none";
    }
    stateFile << " truncated=" << (frame.truncated || scene.truncated)
              << " metadataBytes=" << frame.metadataBytes + (hasScene ? scene.metadataBytes : 0)
              << " retainedSourceBytes=" << scene.retainedSourceBytes
              << " instances=" << scene.instances.size()
              << " geometries=" << scene.geometries.size()
              << " accelerationStructures=" << scene.accelerationStructures.size()
              << " blasBuilds=" << frame.blasBuilds.size()
              << " tlasBuilds=" << frame.tlasBuilds.size() << "\n\n";

    stateFile << "[sourceBuffers]\n";
    // Write retained source ranges once and reference duplicate ranges by offset.
    for (size_t i = 0; i < scene.sourceBuffers.size(); i++) {
      const SourceBufferRecord& source = scene.sourceBuffers[i];
      DumpedRangeKey dumpKey;
      dumpKey.source = source.key;
      dumpKey.physicalHandle = handleValue(source.physicalHandle);
      dumpKey.physicalOffset = source.physicalOffset;

      auto dumped = dumpedRanges.find(dumpKey);
      if (dumped == dumpedRanges.end()) {
        DumpedRange result;
        result.binarySize = source.key.size;

        if (source.key.size == 0) {
          result.status = "empty";
        } else if (!source.hostVisibleAtRecord) {
          result.status = "not-host-visible";
        } else if (source.retentionDisabled) {
          result.status = "retention-disabled";
        } else if (source.retentionBudgetExceeded) {
          result.status = "retention-budget-exceeded";
        } else if (source.retainedBuffer == nullptr) {
          result.status = "source-not-retained";
        } else {
          const DxvkBufferSliceHandle currentSlice = source.retainedBuffer->getSliceHandle(
            source.key.virtualOffset, source.key.size);
          if (currentSlice.handle != source.physicalHandle
              || currentSlice.offset != source.physicalOffset) {
            result.status = "renamed-or-recycled";
          } else if (currentSlice.mapPtr == nullptr) {
            result.status = "mapping-unavailable";
          } else if (writeBinary(binaryFile, currentSlice.mapPtr, source.key.size,
                                 payloadBytes, result.binaryOffset)) {
            result.status = "written";
          } else {
            result.status = "payload-limit-or-write-error";
          }
        }

        dumped = dumpedRanges.emplace(dumpKey, std::move(result)).first;
      }

      stateFile << "buffer[" << i << "] virtualBuffer=0x" << std::hex
                << source.key.virtualBufferId
                << " vkBuffer=0x" << handleValue(source.physicalHandle)
                << " virtualOffset=" << std::dec << source.key.virtualOffset
                << " physicalOffset=" << source.physicalOffset
                << " size=" << source.key.size
                << " stride=" << source.key.stride
                << " format=" << source.key.format
                << " isIndex=" << source.key.isIndex
                << " status=" << dumped->second.status;
      if (dumped->second.status == "written") {
        stateFile << " binaryOffset=" << dumped->second.binaryOffset
                  << " binarySize=" << dumped->second.binarySize;
      }
      stateFile << '\n';
    }
    stateFile << '\n';

    stateFile << "[geometries]\n";
    // Serialize each geometry's auxiliary payload and recorded metadata.
    for (size_t i = 0; i < scene.geometries.size(); i++) {
      const GeometryRecord& geometry = scene.geometries[i];
      uint64_t boneOffset = 0;
      const uint64_t boneSize = geometry.boneMatrices.size() * sizeof(Matrix4);
      const bool bonesWritten = writeBinary(
        binaryFile, geometry.boneMatrices.data(), boneSize, payloadBytes, boneOffset);

      stateFile << "geometry[" << i << "] blasEntry=0x" << std::hex << geometry.blasEntryId
                << " sourceVertices=" << std::dec << geometry.vertexCount
                << " sourceIndices=" << geometry.indexCount
                << " modifiedVertices=" << geometry.modifiedVertexCount
                << " modifiedIndices=" << geometry.modifiedIndexCount
                << " topology=" << geometry.topology
                << " cullMode=" << geometry.cullMode
                << " frontFace=" << geometry.frontFace
                << " frameCreated=" << geometry.frameCreated
                << " frameLastTouched=" << geometry.frameLastTouched
                << " frameLastUpdated=" << geometry.frameLastUpdated
                << " modifiedPositionAddress=0x" << std::hex << geometry.modifiedPositionAddress
                << " modifiedIndexAddress=0x" << geometry.modifiedIndexAddress
                << " boneHash=0x" << geometry.boneHash
                << " modifiedLastBoneHash=0x" << geometry.modifiedLastBoneHash
                << " numBones=" << std::dec << geometry.numBones
                << " numBonesPerVertex=" << geometry.numBonesPerVertex
                << " minBoneIndex=" << geometry.minBoneIndex;
      if (boneSize != 0) {
        stateFile << " bonePayloadStatus=" << (bonesWritten ? "written" : "payload-limit-or-write-error");
        if (bonesWritten) {
          stateFile << " boneBinaryOffset=" << boneOffset
                    << " boneBinarySize=" << boneSize;
        }
      }
      stateFile << '\n';

      stateFile << "  sourceBufferIndices=";
      for (size_t j = 0; j < geometry.sourceBuffers.size(); j++) {
        if (j != 0) {
          stateFile << ',';
        }
        stateFile << kSourceBufferNames[j] << ':';
        if (geometry.sourceBuffers[j] == kInvalidRecordIndex) {
          stateFile << "none";
        } else {
          stateFile << geometry.sourceBuffers[j];
        }
      }
      stateFile << '\n';

      stateFile << "  sourceHashes=";
      for (size_t j = 0; j < geometry.sourceHashes.size(); j++) {
        stateFile << (j == 0 ? "" : ",") << "0x" << std::hex << geometry.sourceHashes[j];
      }
      stateFile << "\n  modifiedHashes=";
      for (size_t j = 0; j < geometry.modifiedHashes.size(); j++) {
        stateFile << (j == 0 ? "" : ",") << "0x" << std::hex << geometry.modifiedHashes[j];
      }
      stateFile << std::dec << '\n';
    }
    stateFile << '\n';

    stateFile << "[instances]\n";
    // Emit the scene-to-TLAS instance mapping and transforms.
    for (size_t i = 0; i < scene.instances.size(); i++) {
      const InstanceRecord& instance = scene.instances[i];
      stateFile << "instance[" << i << "] id=" << instance.instanceId
                << " cacheIdentity=" << instance.cacheIdentity
                << " vectorIndex=" << instance.vectorIndex
                << " orderedSurfaceIndex=" << instance.orderedSurfaceIndex
                << " surfaceIndex=" << instance.surfaceIndex
                << " frameLastUpdated=" << instance.frameLastUpdated
                << " categories=0x" << std::hex << instance.categoryFlags
                << " blasEntry=0x" << instance.blasEntryId
                << " geometryIndex=" << std::dec;
      if (instance.geometryIndex == kInvalidRecordIndex) {
        stateFile << "none";
      } else {
        stateFile << instance.geometryIndex;
      }
      stateFile << " materialHash=0x" << std::hex << instance.materialHash
                << " materialDataHash=0x" << instance.materialDataHash
                << " texcoordHash=0x" << instance.texcoordHash
                << " indexHash=0x" << instance.indexHash
                << " customIndex=" << std::dec << instance.vkInstance.instanceCustomIndex
                << " mask=" << instance.vkInstance.mask
                << " sbtOffset=" << instance.vkInstance.instanceShaderBindingTableRecordOffset
                << " flags=0x" << std::hex << instance.vkInstance.flags
                << " accelerationStructureReference=0x"
                << instance.vkInstance.accelerationStructureReference
                << " transform=" << std::dec;
      for (uint32_t row = 0; row < 3; row++) {
        for (uint32_t column = 0; column < 4; column++) {
          stateFile << (row == 0 && column == 0 ? "" : ",")
                    << instance.vkInstance.transform.matrix[row][column];
        }
      }
      stateFile << '\n';
    }
    stateFile << '\n';

    stateFile << "[accelerationStructures]\n";
    // Emit allocation and lifetime details for each recorded acceleration structure.
    for (size_t i = 0; i < scene.accelerationStructures.size(); i++) {
      const AccelerationStructureRecord& as = scene.accelerationStructures[i];
      stateFile << "as[" << i << "] handle=0x" << std::hex << handleValue(as.handle)
                << " buffer=0x" << handleValue(as.bufferHandle)
                << " address=0x" << as.deviceAddress
                << " bufferOffset=" << std::dec << as.bufferOffset
                << " size=" << as.size
                << " frameLastTouched=" << as.frameLastTouched
                << " activeDynamic=" << as.activeDynamic
                << " opacityMicromapSourceHash=0x" << std::hex << as.opacityMicromapSourceHash
                << " topologyHash=0x" << as.topologyHash
                << " contentHash=0x" << as.contentHash << std::dec << '\n';
    }
    stateFile << '\n';

    stateFile << "[blasBuilds]\n";
    uint64_t transformOffset = 0;
    const uint64_t transformSize = frame.blasTransforms.size() * sizeof(VkTransformMatrixKHR);
    const bool transformsWritten = writeBinary(
      binaryFile, frame.blasTransforms.data(), transformSize, payloadBytes, transformOffset);
    stateFile << "transformBufferAddress=0x" << std::hex << frame.blasTransformBufferAddress
              << " transformCount=" << std::dec << frame.blasTransforms.size()
              << " transformPayloadStatus="
              << (transformSize == 0 ? "empty" : (transformsWritten ? "written" : "payload-limit-or-write-error"));
    if (transformsWritten) {
      stateFile << " transformBinaryOffset=" << transformOffset
                << " transformBinarySize=" << transformSize;
    }
    stateFile << '\n';
    // Emit the CPU transforms supplied to the recorded BLAS builds.
    for (size_t transformIndex = 0; transformIndex < frame.blasTransforms.size(); transformIndex++) {
      stateFile << "transform[" << transformIndex << "]=";
      for (uint32_t row = 0; row < 3; row++) {
        for (uint32_t column = 0; column < 4; column++) {
          stateFile << (row == 0 && column == 0 ? "" : ",")
                    << frame.blasTransforms[transformIndex].matrix[row][column];
        }
      }
      stateFile << '\n';
    }
    for (size_t i = 0; i < frame.blasBuilds.size(); i++) {
      writeBuild(stateFile, frame.blasBuilds[i], "blasBuild", i);
    }
    stateFile << '\n';

    stateFile << "[tlasBuilds]\n";
    // Serialize TLAS build descriptors together with their CPU instance payloads.
    for (size_t i = 0; i < frame.tlasBuilds.size(); i++) {
      const TlasBuildRecord& tlas = frame.tlasBuilds[i];
      uint64_t instanceOffset = 0;
      const uint64_t instanceSize = tlas.cpuInstances.size()
        * sizeof(VkAccelerationStructureInstanceKHR);
      const bool instancesWritten = writeBinary(
        binaryFile, tlas.cpuInstances.data(), instanceSize, payloadBytes, instanceOffset);

      stateFile << "tlas[" << i << "] type=" << tlas.type
                << " cpuInstanceCount=" << tlas.cpuInstances.size()
                << " gpuGeneratedInstanceCount=" << tlas.gpuGeneratedInstanceCount
                << " instanceBufferAddress=0x" << std::hex << tlas.instanceBufferAddress
                << " instanceBufferOffset=" << std::dec << tlas.instanceBufferOffset
                << " destinationAddress=0x" << std::hex << tlas.destinationAddress
                << " destinationSize=" << std::dec << tlas.destinationSize
                << " cpuInstancePayloadStatus="
                << (instanceSize == 0 ? "empty" : (instancesWritten ? "written" : "payload-limit-or-write-error"));
      if (instancesWritten) {
        stateFile << " cpuInstanceBinaryOffset=" << instanceOffset
                  << " cpuInstanceBinarySize=" << instanceSize;
      }
      stateFile << '\n';
      writeBuild(stateFile, tlas.build, "tlasBuildDescriptor", i);
    }
    stateFile << "\n[/frame]\n\n";
  }

  stateFile << "payloadBytesWritten=" << payloadBytes << '\n';
  stateFile.close();
  binaryFile.close();

  if (binaryFileOpened) {
    Logger::err(str::format(
      "GPU device loss state written to: ", stateFilename,
      " (binary payload: ", binaryFilename, ")"));
  } else {
    Logger::err(str::format(
      "GPU device loss state written to: ", stateFilename,
      " (binary payload could not be opened: ", binaryFilename, ")"));
  }
}

} // namespace dxvk
