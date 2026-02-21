#include "test_remix_api_common.h"

#include <fstream>
#include <iostream>
#include <assert.h>

using namespace remixapi::util::serialize;

template <typename T>
void writeOutSerializable(const T& serializable, std::ofstream& file) {
  const size_t size = serializable.size();
  auto buffer = new char[size];
  serializable.serialize(buffer);
  file.write(buffer, size);
  delete buffer;
}

template <typename SerializableT, typename BaseT = SerializableT::BaseT>
SerializableT init(const expected::Expected<BaseT>& expected) {
  return SerializableT((const BaseT&)expected);
}

#define INIT_SERIALIZE_WRITE(SerializableT, Name, File) \
  auto Name = init<SerializableT>(expected::Name); \
  writeOutSerializable(Name, File);

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

  file.close();
  return 0;
}