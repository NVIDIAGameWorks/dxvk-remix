#include "test_remix_api_common.h"

#include <fstream>
#include <iostream>
#include <cstring>
#include <assert.h>

using namespace remixapi::util::serialize;

static constexpr uint8_t kSentinelByte = 0xCD;
static constexpr size_t kSentinelSize = 16;

template <typename T>
bool writeOutSerializable(const T& serializable, std::ofstream& file) {
  const size_t size = serializable.size();

  // Allocate padding bytes and fill with sentinel value
  auto buffer = new char[size + kSentinelSize];
  std::memset(buffer + size, kSentinelByte, kSentinelSize);
  serializable.serialize(buffer);

  // Buffer overrun detection:
  // Inspect the reserved sentinel bytes at the end of the buffer to make sure nothing has been overwritten
  for (size_t i = 0; i < kSentinelSize; ++i) {
    if (static_cast<uint8_t>(buffer[size + i]) != kSentinelByte) {
      delete[] buffer;
      return false;
    }
  }
  file.write(buffer, size);
  delete[] buffer;
  return true;
}

template <typename SerializableT, typename BaseT = SerializableT::BaseT>
SerializableT init(const expected::Expected<BaseT>& expected) {
  return SerializableT((const BaseT&)expected);
}

template <typename SerializableT, typename BaseT = typename SerializableT::BaseT>
bool staticSizeInvariantCheck(const expected::Expected<BaseT>& expectedData) {
  if constexpr (SerializableT::bHasStaticSize) {
    SerializableT forSerialize(static_cast<const BaseT&>(expectedData));
    const uint32_t computedSize = forSerialize.calcSize();
    if (computedSize != SerializableT::s_kSize) {
      return false;
    }
  }
  return true;
}

#define INIT_SERIALIZE_WRITE(SerializableT, Name, File) \
  auto Name = init<SerializableT>(expected::Name); \
  if (!writeOutSerializable(Name, File)) { \
    std::cout << #SerializableT << " buffer overrun detected (sentinel corrupted)" << std::endl; \
    return 1; \
  } \
  if (!staticSizeInvariantCheck<SerializableT>(expected::Name)) { \
    std::cout << #SerializableT << " static-size invariant failed" << std::endl; \
    return 1; \
  }

int main(int argc, char * argv[]) {
  assert(argc == 2);
  std::string filePath(argv[1]);
  std::ofstream file(filePath, std::ios::binary);

  INIT_SERIALIZE_WRITE(MaterialInfo, mat, file);
  INIT_SERIALIZE_WRITE(MaterialInfoOpaque, matOpaque, file);
  INIT_SERIALIZE_WRITE(MaterialInfoOpaqueSubsurface, matOpaqueSubSurf, file);
  INIT_SERIALIZE_WRITE(MaterialInfoTranslucent, matTrans, file);
  INIT_SERIALIZE_WRITE(MaterialInfoPortal, matPortal, file);
  INIT_SERIALIZE_WRITE(MeshInfo, mesh, file);
  INIT_SERIALIZE_WRITE(InstanceInfo, inst, file);
  INIT_SERIALIZE_WRITE(InstanceInfoObjectPicking, instObjPick, file);
  INIT_SERIALIZE_WRITE(InstanceInfoBlend, instBlend, file);
  INIT_SERIALIZE_WRITE(InstanceInfoTransforms, instBoneXform, file);
  INIT_SERIALIZE_WRITE(InstanceInfoGpuInstancing, instGpuInstancing, file);
  INIT_SERIALIZE_WRITE(LightInfo, light, file);
  INIT_SERIALIZE_WRITE(LightInfoSphere, lightSphere, file);
  INIT_SERIALIZE_WRITE(LightInfoRect, lightRect, file);
  INIT_SERIALIZE_WRITE(LightInfoDisk, lightDisk, file);
  INIT_SERIALIZE_WRITE(LightInfoCylinder, lightCyl, file);
  INIT_SERIALIZE_WRITE(LightInfoDistant, lightDist, file);
  INIT_SERIALIZE_WRITE(LightInfoDome, lightDome, file);
  INIT_SERIALIZE_WRITE(LightInfoUSD, lightUSD, file);
  INIT_SERIALIZE_WRITE(InstanceInfoParticleSystem, instParticle, file);

  file.close();
  return 0;
}
