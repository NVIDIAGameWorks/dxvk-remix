/*
* Copyright (c) 2021-2023, NVIDIA CORPORATION. All rights reserved.
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
#include "rtx_asset_package_writer.h"

#include "util_threadpool.h"

#include <thread>
#include <atomic>
#include <future>

#if WITH_RTXIO
#include <rtxio/rtxio.h>
#include <rtxio/rtxioCompression.h>

#define DISK_DATA_ALIGNMENT kRtxIoDataAlignment
#endif

#include <gli/gli.hpp>

#ifdef WIN32
#define fseek64 _fseeki64
#define ftell64 _ftelli64
#else
#define fseek64 fseeko64
#define ftell64 ftello64
#define fopen_s(pFile,filename,mode) (((*(pFile))=fopen((filename),(mode)))==NULL)
#endif

#ifndef DISK_DATA_ALIGNMENT
#define DISK_DATA_ALIGNMENT 4
#endif

const uint32_t g_compressionLevel = 8;

//namespace dxvk {
using namespace dxvk;

enum class AssetSourceType {
  None,
  File,
  Image,
  Buffer
};

struct PackageJobDesc {
  AssetPackageWriter::PackageHandle package;

  int volume : 8;
  AssetSourceType type : 8;
  uint32_t compressionLevel : 8;
  uint32_t assetIndex;

  std::vector<uint8_t> data;
  std::string file;
};

static dxvk::AssetPackage::AssetDesc::Type toAssetDescType(gli::target target) {
  switch (target) {
  case gli::target::TARGET_1D:
  case gli::target::TARGET_1D_ARRAY:
    return dxvk::AssetPackage::AssetDesc::Type::IMAGE_1D;
  case gli::target::TARGET_2D:
  case gli::target::TARGET_2D_ARRAY:
  case gli::target::TARGET_CUBE:
  case gli::target::TARGET_CUBE_ARRAY:
    return dxvk::AssetPackage::AssetDesc::Type::IMAGE_2D;
  case gli::target::TARGET_3D:
    return dxvk::AssetPackage::AssetDesc::Type::IMAGE_3D;
  }

  assert(0 && "Unsupported gli image target type!");

  return dxvk::AssetPackage::AssetDesc::Type::UNKNOWN;
}

static uint64_t getNextAlignedFileOffset(FILE* f) {
  fseek64(f, 0, SEEK_END);

  // RTX IO data must have minimum 4-byte data alignment on disk.
  if (uint64_t padRem = (ftell64(f) & (DISK_DATA_ALIGNMENT - 1))) {
    static uint8_t padding[DISK_DATA_ALIGNMENT];
    fwrite(padding, 1, DISK_DATA_ALIGNMENT - padRem, f);
  }

  return ftell64(f);
}

static std::vector<uint8_t> compressBuffer(const void* data, size_t dataSize,
  uint32_t level, uint32_t& crc32, uint8_t& usedMethod) {
#if WITH_RTXIO
  auto method = RTXIO_COMPRESSION_GDEFLATE_1_0;
  std::vector<uint8_t> compressed(rtxioGetCompressedUpperBound(method, dataSize));
  size_t compressedSize = compressed.size();

  if (level == 0) {
    method = RTXIO_COMPRESSION_NONE;
    compressed.resize(dataSize);
    memcpy(compressed.data(), data, dataSize);
  } else {
    if (rtxioCompress(compressed.data(), &compressedSize,
      reinterpret_cast<const uint8_t*>(data), dataSize, method, 1, level)) {
      compressed.resize(compressedSize);
    }
  }

  crc32 = rtxioCrc32(0, data, dataSize);
  usedMethod = method;

  return compressed;
#else
  // Passthrough
  std::vector<uint8_t> out(dataSize);
  memcpy(out.data(), data, dataSize);
  usedMethod = 0;
  crc32 = 0;
  return out;
#endif
}

class AsyncAssetPackager {
  typedef dxvk::WorkerThreadPool<1024, true, false> ThreadPoolType;

  AssetPackageWriter& m_writer;

  // Note: WorkerThreadPool is not thread-safe
  inline static dxvk::sync::Spinlock g_threadPoolMutex;
  inline static std::shared_ptr<ThreadPoolType> g_threadPool;

  std::shared_ptr<ThreadPoolType> m_threadPool;

  // Controls access to the packager job and underlying package
  std::recursive_mutex m_accessMutex;

  // Using two counters to calculate approximate progress.
  std::atomic_uint32_t m_numQueued;
  std::atomic_uint32_t m_numFinished;

  static constexpr uint32_t kFinalizeQueued = 1;
  static constexpr uint32_t kFinalized = 2;

  std::atomic_uint32_t m_finalizeState{0};

public:
  AsyncAssetPackager() = delete;
  AsyncAssetPackager(AssetPackageWriter& writer)
  : m_writer(writer) {
    std::lock_guard<sync::Spinlock> lock(g_threadPoolMutex);

    if (g_threadPool == nullptr) {
      const uint32_t numCpuCores = dxvk::thread::hardware_concurrency();
      g_threadPool = std::make_shared<ThreadPoolType>(numCpuCores / 2, "rtx-asset-packager");
    }
    m_threadPool = g_threadPool;
  }

  bool packageJob(const PackageJobDesc& desc) {
    if (desc.assetIndex == AssetPackage::kNoAssetIdx) {
      using namespace std::chrono_literals;
      // NOTE: Racy but works since finalization job is last in queue,
      // and queue length is constant.
      const uint32_t numQueued = m_numQueued.load();
      do {
        if (numQueued - m_numFinished == 1) {
          break;
        }
        std::this_thread::sleep_for(500ms);
      } while(true);

      // Finalize the package
      const bool result = m_writer.finalizePackageImpl(desc.package);
      ++m_numFinished;

      Logger::info(str::format("[", desc.package->getBaseFilename(), "] completed."));

      // Nothing may touch this object after this point, since the waiter releases it.
      m_finalizeState.fetch_or(kFinalized, std::memory_order_release);

      return result;
    }

    m_writer.packageJob(desc);
    ++m_numFinished;

    Logger::info(str::format("[", desc.package->getBaseFilename(), "] progress: ",
      m_numFinished.load(), " / ", m_numQueued.load()));

    return true;
  }

  bool isFinalized() const {
    return (m_finalizeState.load(std::memory_order_acquire) & kFinalized) != 0;
  }

  // Returns true when no job can touch this object anymore.
  bool isSettled() const {
    const uint32_t state = m_finalizeState.load(std::memory_order_acquire);
    const bool finalizeDone = (state & kFinalizeQueued) == 0 || (state & kFinalized) != 0;
    return finalizeDone && m_numFinished.load() == m_numQueued.load();
  }

  float getProgressApprox() const {
    const uint32_t numQueued = m_numQueued.load();
    if (numQueued > 0) {
      return (100.f * m_numFinished.load()) / numQueued;
    }
    return 0.f;
  }

  dxvk::Future<bool> addJob(PackageJobDesc&& jobDesc) {
    std::lock_guard<sync::Spinlock> lock(g_threadPoolMutex);

    while (m_threadPool->isFullApprox()) {
      ONCE(Logger::warn("The threadpool is full. Retrying..."));

      using namespace std::chrono_literals;
      std::this_thread::sleep_for(50ms);
    }

    const bool isFinalizeJob = jobDesc.assetIndex == AssetPackage::kNoAssetIdx;

    if (isFinalizeJob) {
      m_finalizeState.fetch_or(kFinalizeQueued);
    }

    ++m_numQueued;

    dxvk::Future<bool> future = m_threadPool->Schedule([jobDesc = std::move(jobDesc), this]() -> bool {
      return packageJob(jobDesc);
    });

    if (future.valid()) {
      return future;
    }

    --m_numQueued;

    if (isFinalizeJob) {
      // Mark as finalized so waiters do not spin forever on a job that never ran.
      m_finalizeState.fetch_or(kFinalized, std::memory_order_release);
    }

    Logger::err("Fatal: unable to schedule a package job.");

    return future;
  }

  std::unique_lock<std::recursive_mutex> lock() {
    return std::unique_lock<std::recursive_mutex>(m_accessMutex);
  }
};

AssetPackageWriter::AssetPackageWriter() { }

AssetPackageWriter::~AssetPackageWriter() {
  using namespace std::chrono_literals;

  for (auto& [name, packager] : m_asyncPackagers) {
    while (packager && !packager->isSettled()) {
      std::this_thread::sleep_for(50ms);
    }
  }
}

bool AssetPackageWriter::writeFile(const PackageJobDesc& jobDesc) {
  // Package a raw file
  FILE* f;
  fopen_s(&f, jobDesc.file.c_str(), "rb");

  if (f == nullptr) {
    Logger::warn(str::format("Unable to open file ", jobDesc.file, " for packaging."));
    return false;
  }

  fseek64(f, 0, SEEK_END);
  const size_t size = ftell64(f);
  fseek64(f, 0, SEEK_SET);

  std::vector<uint8_t> data(size);
  fread(data.data(), 1, data.size(), f);
  fclose(f);

  uint32_t crc32 = 0;
#if WITH_RTXIO
  crc32 = rtxioCrc32(0, data.data(), data.size());
#endif

  auto package = jobDesc.package;
  auto lock = lockPackage(package);

  auto& assetDesc = package->m_assetDescs[jobDesc.assetIndex];

  assetDesc.type = AssetPackage::AssetDesc::Type::RAW_FILE;
  assetDesc.size = static_cast<uint32_t>(size);
  assetDesc.baseBlobIdx = static_cast<uint16_t>(package->m_blobDescs.size());

  // Files always go to the base volume
  const uint32_t volume = 0;

  if (!package->createFile(volume)) {
    Logger::err(str::format("Fatal: unable create package volume ", volume, " file."));
    return false;
  }

  auto handle = package->getVolumeHandle(volume);

  AssetPackage::BlobDesc blobDesc;
  blobDesc.offset = getNextAlignedFileOffset(handle);
  blobDesc.compression = 0;
  blobDesc.volume = volume;
  blobDesc.flags = 0;
  blobDesc.size = static_cast<uint32_t>(data.size());
  blobDesc.crc32 = crc32;

  package->m_blobDescs.emplace_back(blobDesc);

  fseek64(handle, blobDesc.offset, SEEK_SET);
  if (data.size() != fwrite(data.data(), 1, data.size(), handle)) {
    Logger::err(str::format("Fatal: incomplete package write for file ", jobDesc.file));
    return false;
  }

  return true;
}

bool AssetPackageWriter::writeBuffer(const PackageJobDesc& jobDesc) {
  // package a raw buffer
  uint8_t usedMethod;
  uint32_t crc32;
  auto blobData = compressBuffer(jobDesc.data.data(), jobDesc.data.size(),
    jobDesc.compressionLevel, crc32, usedMethod);

  auto package = jobDesc.package;
  auto lock = lockPackage(package);

  auto& assetDesc = package->m_assetDescs[jobDesc.assetIndex];

  assetDesc.type = AssetPackage::AssetDesc::Type::BUFFER;
  assetDesc.size = static_cast<uint32_t>(jobDesc.data.size());
  assetDesc.baseBlobIdx = static_cast<uint16_t>(package->m_blobDescs.size());

  const uint32_t volume = jobDesc.volume;

  if (!package->createFile(volume)) {
    Logger::err(str::format("Fatal: unable create package volume ", volume, " file."));
    return false;
  }

  auto handle = package->getVolumeHandle(volume);

  AssetPackage::BlobDesc blobDesc;
  blobDesc.offset = getNextAlignedFileOffset(handle);
  blobDesc.compression = usedMethod;
  blobDesc.volume = volume;
  blobDesc.flags = 0;
  blobDesc.size = static_cast<uint32_t>(blobData.size());
  blobDesc.crc32 = crc32;

  package->m_blobDescs.emplace_back(blobDesc);

  fseek64(handle, blobDesc.offset, SEEK_SET);
  if (blobData.size() != fwrite(blobData.data(), 1, blobData.size(), handle)) {
    Logger::err(str::format("Fatal: incomplete package write for buffer ", assetDesc.baseBlobIdx));
    return false;
  }

  return true;
}

bool AssetPackageWriter::isImageAssetChanged(const std::string& path,
  const PackageHandle& package, uint32_t assetIdx) {
  if (assetIdx == dxvk::AssetPackage::kNoAssetIdx)
    return false;

  gli::texture asset = gli::load(path.c_str());

  if (asset.empty()) {
    return false;
  }

  auto assetDesc = package->getAssetDesc(assetIdx);

  if (assetDesc == nullptr || assetDesc->format != asset.format() ||
    assetDesc->width != asset.extent(0).x ||
    assetDesc->height != asset.extent(0).y ||
    assetDesc->depth != asset.extent(0).z ||
    assetDesc->numMips != asset.levels() ||
    assetDesc->arraySize != asset.layers()) {
    return false;
  }

  const bool isSingleBlob = assetDesc->baseBlobIdx == assetDesc->tailBlobIdx;
  const uint32_t blobStride = isSingleBlob ? 1 : assetDesc->numMips - assetDesc->numTailMips;

  for (uint32_t arrayIdx = 0; arrayIdx < assetDesc->arraySize; arrayIdx++) {
    // Only check the top level
    if (auto blobDesc = package->getDataBlobDesc(assetDesc->baseBlobIdx + arrayIdx * blobStride)) {
      const void* data = asset.data(arrayIdx, 0, 0);
      const size_t size = isSingleBlob ? asset.size() : asset.size(0);

      if (blobDesc->crc32 != rtxioCrc32(0, data, size)) {
        return false;
      }
    }
  }

  return true;
}


bool AssetPackageWriter::writeImage(const PackageJobDesc& jobDesc) {
  gli::texture asset = gli::load(jobDesc.file.c_str());

  if (asset.empty()) {
    Logger::warn(str::format("Corrupted image discovered at ", jobDesc.file));
    return false;
  }

  AssetPackage::AssetDesc assetDesc;
  assetDesc.type = toAssetDescType(asset.target());
  assetDesc.format = asset.format();
  assetDesc.width = asset.extent(0).x;
  assetDesc.height = asset.extent(0).y;
  assetDesc.depth = asset.extent(0).z;

  assetDesc.numMips = asset.levels();
  assetDesc.arraySize = asset.layers();

  assetDesc.numTailMips = std::min<uint16_t>(8, assetDesc.numMips);

  const uint32_t numLooseMips = assetDesc.numMips - assetDesc.numTailMips;
  const auto blockExtent = gli::block_extent(asset.format());
  const size_t elementSize = gli::block_size(asset.format());

  const uint32_t volume = jobDesc.volume;

  auto package = jobDesc.package;
  auto handle = package->getVolumeHandle(volume);

  std::vector<AssetPackage::BlobDesc> blobDescs;

  for (uint32_t arrayIdx = 0; arrayIdx < assetDesc.arraySize; arrayIdx++) {
    for (uint32_t mipIdx = 0; mipIdx < numLooseMips; mipIdx++) {
      const void* imageData = asset.data(arrayIdx, 0, mipIdx);

      const auto levelExtent = asset.extent(mipIdx);
      const auto elementCount = (levelExtent + blockExtent - 1) / blockExtent;
      const size_t rowPitch = elementCount.x * elementSize;
      const size_t layerPitch = rowPitch * elementCount.y;

      uint8_t usedMethod;
      uint32_t crc32;
      auto blobData = compressBuffer(imageData, layerPitch,
        jobDesc.compressionLevel, crc32, usedMethod);

      auto lock = lockPackage(package);

      if (handle == nullptr) {
        if (!package->createFile(volume)) {
          Logger::err(str::format("Fatal: unable create package volume ", volume, " file."));
          return false;
        }

        handle = package->getVolumeHandle(volume);
      }

      AssetPackage::BlobDesc blobDesc;
      blobDesc.offset = getNextAlignedFileOffset(handle);
      blobDesc.compression = usedMethod;
      blobDesc.volume = volume;
      blobDesc.flags = 0;
      blobDesc.size = static_cast<uint32_t>(blobData.size());
      blobDesc.crc32 = crc32;

      blobDescs.emplace_back(blobDesc);

      fseek64(handle, blobDesc.offset, SEEK_SET);
      if (blobData.size() != fwrite(blobData.data(), 1, blobData.size(), handle)) {
        Logger::err(str::format("Fatal: incomplete package write for image ", jobDesc.file));
        return false;
      }
    }
  }

  assetDesc.tailBlobIdx = assetDesc.numTailMips > 0 ? blobDescs.size() : 0;

  for (uint32_t arrayIdx = 0; arrayIdx < assetDesc.arraySize; arrayIdx++) {
    std::vector<uint8_t> tail;
    for (uint32_t mipIdx = numLooseMips; mipIdx < assetDesc.numMips; mipIdx++) {
      const void* imageData = asset.data(arrayIdx, 0, mipIdx);

      const auto levelExtent = asset.extent(mipIdx);
      const auto elementCount = (levelExtent + blockExtent - 1) / blockExtent;
      const size_t rowPitch = elementCount.x * elementSize;
      const size_t layerPitch = rowPitch * elementCount.y;
      const size_t layerOffset = tail.size();

      tail.resize(layerOffset + layerPitch);

      const uint8_t* data = reinterpret_cast<const uint8_t*>(imageData);

      for (size_t y = 0; y < elementCount.y; y++) {
        memcpy(tail.data() + layerOffset + y * rowPitch,
               data + y * rowPitch, rowPitch);
      }
    }

    if (tail.empty())
      continue;

    uint8_t usedMethod;
    uint32_t crc32;
    auto blobData = compressBuffer(tail.data(), tail.size(),
      jobDesc.compressionLevel, crc32, usedMethod);

    auto lock = lockPackage(package);

    if (handle == nullptr) {
      if (!package->createFile(volume)) {
        Logger::err(str::format("Fatal: unable create package volume ", volume, " file."));
        return false;
      }

      handle = package->getVolumeHandle(volume);
    }

    AssetPackage::BlobDesc blobDesc;
    blobDesc.offset = getNextAlignedFileOffset(handle);
    blobDesc.compression = usedMethod;
    blobDesc.volume = volume;
    blobDesc.flags = 0;
    blobDesc.size = static_cast<uint32_t>(blobData.size());
    blobDesc.crc32 = crc32;

    blobDescs.emplace_back(blobDesc);

    fseek64(handle, blobDesc.offset, SEEK_SET);
    if (blobData.size() != fwrite(blobData.data(), 1, blobData.size(), handle)) {
      Logger::err(str::format("Fatal: incomplete package write for image ", jobDesc.file, " tail."));
      return false;
    }
  }

  auto lock = lockPackage(package);

  assetDesc.baseBlobIdx = package->m_blobDescs.size();
  assetDesc.tailBlobIdx += assetDesc.baseBlobIdx;

  package->m_assetDescs[jobDesc.assetIndex] = assetDesc;
  package->m_blobDescs.insert(package->m_blobDescs.end(),
    std::make_move_iterator(blobDescs.begin()),
    std::make_move_iterator(blobDescs.end()));

  return true;
}

bool AssetPackageWriter::finalizePackageImpl(const PackageHandle& package) {
  auto handle = package->getVolumeHandle(0);

  if (handle == nullptr)
    return false;

  auto lock = lockPackage(package);

  fseek64(handle, 0, SEEK_END);

  AssetPackage::Header header;
  header.magic = AssetPackage::kMagic;
  header.version = AssetPackage::kVersion;
  header.dictOffset = ftell64(handle);

  const uint32_t numAssets = static_cast<uint32_t>(package->m_assetDescs.size());
  fwrite(&numAssets, 2, 1, handle);

  const uint32_t numBlobs = static_cast<uint32_t>(package->m_blobDescs.size());
  fwrite(&numBlobs, 2, 1, handle);

  fwrite(package->m_assetDescs.data(), sizeof(AssetPackage::AssetDesc), numAssets, handle);
  fwrite(package->m_blobDescs.data(), sizeof(AssetPackage::BlobDesc), numBlobs, handle);

  std::vector<const std::string*> sortedNames;
  sortedNames.resize(numAssets);
  for (auto& [name, index] : package->m_nameHash) {
    sortedNames[index] = &name;
  }

  for (auto namePtr : sortedNames) {
    if (namePtr) {
      fwrite(namePtr->c_str(), 1, namePtr->length() + 1, handle);
    } else {
      static uint8_t zeroByte = 0;
      fwrite(&zeroByte, 1, 1, handle);
    }
  }

  fseek64(handle, 0, SEEK_SET);
  fwrite(&header, 1, sizeof(header), handle);

  package->closeAllFiles();

  return true;
}

bool AssetPackageWriter::packageJob(const PackageJobDesc& jobDesc) {
  switch (jobDesc.type) {
  default:
  case AssetSourceType::None:
    break;
  case AssetSourceType::File:
    return writeFile(jobDesc);
  case AssetSourceType::Buffer:
    return writeBuffer(jobDesc);
  case AssetSourceType::Image:
    return writeImage(jobDesc);
  }
  return finalizePackageImpl(jobDesc.package);
}

void AssetPackageWriter::scheduleJob(PackageJobDesc&& jobDesc) {
  AsyncAssetPackager* packager;
  {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    packager = m_asyncPackagers[jobDesc.package->getBaseFilename()].get();
  }

  if (packager != nullptr) {
    packager->addJob(std::forward<PackageJobDesc>(jobDesc));
  } else {
    packageJob(jobDesc);
  }
}

uint32_t AssetPackageWriter::addUsdAsset(
  const PackageHandle& package,
  const std::string& name, const std::string& file) {

  uint32_t assetIndex;
  {
    auto lock = lockPackage(package);
    if (package->reserveAsset(name, assetIndex)) {
      return assetIndex;
    }
  }

  PackageJobDesc jobDesc{package};

  jobDesc.volume = 0;
  jobDesc.assetIndex = assetIndex;
  jobDesc.type = AssetSourceType::File;
  jobDesc.file = file;

  scheduleJob(std::move(jobDesc));

  return assetIndex;
}

uint32_t AssetPackageWriter::addImageAsset(
  const PackageHandle& package, int volume,
  const std::string& name, uint32_t compressionLevel,
  const std::string& imageFile) {

  uint32_t assetIndex;
  {
    auto lock = lockPackage(package);
    if (package->reserveAsset(name, assetIndex)) {
      return assetIndex;
    }
  }

  PackageJobDesc jobDesc{package};

  jobDesc.volume = volume;
  jobDesc.assetIndex = assetIndex;
  jobDesc.type = AssetSourceType::Image;
  jobDesc.compressionLevel = compressionLevel;
  jobDesc.file = imageFile;

  scheduleJob(std::move(jobDesc));

  return assetIndex;
}

uint32_t AssetPackageWriter::addBufferAsset(
  const PackageHandle& package, int volume,
  const std::string& name,  uint32_t compressionLevel,
  const void* data, size_t size) {

  uint32_t assetIndex;
  {
    auto lock = lockPackage(package);
    if (package->reserveAsset(name, assetIndex)) {
      return assetIndex;
    }
  }

  PackageJobDesc jobDesc{package};

  jobDesc.volume = volume;
  jobDesc.type = AssetSourceType::Buffer;
  jobDesc.assetIndex = assetIndex;
  jobDesc.compressionLevel = compressionLevel;

  jobDesc.data.resize(size);
  memcpy(jobDesc.data.data(), data, size);

  scheduleJob(std::move(jobDesc));

  return assetIndex;
}

AssetPackageWriter::PackageHandle AssetPackageWriter::createPackage(
  const std::string& filename) {

  auto package = std::make_shared<AssetPackage>(filename);

  if (!package->createFile(0))
    return nullptr;

  static AssetPackage::Header tmp;
  fwrite(&tmp, 1, sizeof(tmp), package->getVolumeHandle(0));

  std::lock_guard<std::recursive_mutex> lock(m_mutex);
  m_asyncPackagers[filename] = std::make_unique<AsyncAssetPackager>(*this);

  return package;
}

void AssetPackageWriter::finalizePackage(const PackageHandle& package) {
  PackageJobDesc jobDesc{package};
  jobDesc.type = AssetSourceType::None;
  jobDesc.assetIndex = AssetPackage::kNoAssetIdx;

  scheduleJob(std::move(jobDesc));
}

void AssetPackageWriter::waitPackageCompletion(const PackageHandle& package) {
  using namespace std::chrono_literals;

  AsyncAssetPackager* packager = nullptr;
  {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    auto it = m_asyncPackagers.find(package->getBaseFilename());
    if (it == m_asyncPackagers.end()) {
      return;
    }
    packager = it->second.get();
  }

  while (packager && !packager->isFinalized()) {
    std::this_thread::sleep_for(50ms);
  }

  std::lock_guard<std::recursive_mutex> lock(m_mutex);
  m_asyncPackagers.erase(package->getBaseFilename());
}

std::unique_lock<std::recursive_mutex> AssetPackageWriter::lockPackage(const PackageHandle& package) const {
  AsyncAssetPackager* packager = nullptr;
  {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    auto it = m_asyncPackagers.find(package->getBaseFilename());
    if (it != m_asyncPackagers.end()) {
      packager = it->second.get();
    }
  }

  if (packager) {
    return packager->lock();
  }

  return {};
}

//}
