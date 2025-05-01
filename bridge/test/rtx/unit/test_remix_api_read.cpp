#include "test_remix_api_common.h"

#include <fstream>
#include <iostream>
#include <assert.h>

using namespace remixapi::util::serialize;

template <typename SerializableT, typename BaseT = SerializableT::BaseT>
bool read_deserialize_compare(char*& pDeserialize,
                              SerializableT& serializable,
                              const expected::Expected<BaseT>& expected,
                              const char* const typeName) {
  serializable.deserialize();
  serializable.pNext = nullptr;
  pDeserialize += serializable.size();
  if(expected != serializable) {
    std::cout << typeName << " failed deserialization!" << std::endl;
    return false;
  }
  return true;
}

#define READ_DESERIALIZE_COMPARE(pDeserialize, SerializableT, Name) \
  SerializableT Name(pDeserialize); \
  const bool b##Name = read_deserialize_compare(pDeserialize, Name, expected::Name, #SerializableT); \
  if (!b##Name) { \
    delete pDataBegin; \
    return 1; \
  }


int main(int argc, char * argv[]) {
  assert(argc == 2);
  std::string filePath(argv[1]);
  std::ifstream file(filePath, std::ios::binary | std::ios::ate);
  if(!file) {
    std::cout << filePath << " not found!" << std::endl;
    return 1;
  }
  const std::streamsize fileSize = file.tellg();
  file.seekg(0, std::ios::beg);
  char* const pDataBegin = new char[fileSize];
  file.read(pDataBegin, fileSize);
  file.close();

  char* pDeserialize = pDataBegin;

  READ_DESERIALIZE_COMPARE(pDeserialize, MaterialInfo, mat);
  READ_DESERIALIZE_COMPARE(pDeserialize, MaterialInfoOpaque, matOpaque);
  READ_DESERIALIZE_COMPARE(pDeserialize, MaterialInfoOpaqueSubsurface, matOpaqueSubSurf);
  READ_DESERIALIZE_COMPARE(pDeserialize, MaterialInfoTranslucent, matTrans);
  READ_DESERIALIZE_COMPARE(pDeserialize, MaterialInfoPortal, matPortal);
  READ_DESERIALIZE_COMPARE(pDeserialize, MeshInfo, mesh);
  READ_DESERIALIZE_COMPARE(pDeserialize, InstanceInfo, inst);
  READ_DESERIALIZE_COMPARE(pDeserialize, InstanceInfoObjectPicking, instObjPick);
  READ_DESERIALIZE_COMPARE(pDeserialize, InstanceInfoBlend, instBlend);
  READ_DESERIALIZE_COMPARE(pDeserialize, InstanceInfoTransforms, instBoneXform);
  READ_DESERIALIZE_COMPARE(pDeserialize, LightInfo, light);
  READ_DESERIALIZE_COMPARE(pDeserialize, LightInfoSphere, lightSphere);
  READ_DESERIALIZE_COMPARE(pDeserialize, LightInfoRect, lightRect);
  READ_DESERIALIZE_COMPARE(pDeserialize, LightInfoDisk, lightDisk);
  READ_DESERIALIZE_COMPARE(pDeserialize, LightInfoCylinder, lightCyl);
  READ_DESERIALIZE_COMPARE(pDeserialize, LightInfoDistant, lightDist);
  READ_DESERIALIZE_COMPARE(pDeserialize, LightInfoDome, lightDome);
  READ_DESERIALIZE_COMPARE(pDeserialize, LightInfoUSD, lightUSD);

  delete pDataBegin;

  return 0;
}