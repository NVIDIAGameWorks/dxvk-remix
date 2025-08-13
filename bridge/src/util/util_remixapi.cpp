/*
 * Copyright (c) 2024, NVIDIA CORPORATION. All rights reserved.
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

#include "util_remixapi.h"

#include <array>
#include <cstring>
#include <type_traits>
#include <typeinfo>
#include <assert.h>

using namespace remixapi::util::serialize;

namespace remixapi {
namespace util {
#ifdef REMIX_BRIDGE_SERVER
MaterialHandle::HandleMapT MaterialHandle::s_handleMap;
MeshHandle::HandleMapT MeshHandle::s_handleMap;
LightHandle::HandleMapT LightHandle::s_handleMap;
#endif
}
}

namespace {

// Convenience function templates to help with the boilerplate necessary
// to handle deserializing the `const T*` RemixApi struct member pattern

template<typename T>
static inline void deserialize_const_p(void*& pDeserialize, const T*& deserializeTo, const uint32_t size) {
  T* new_arr = (T*) new uint8_t[size];
  bridge_util::deserialize(pDeserialize, new_arr, size);
  deserializeTo = new_arr;
}

template<typename T>
static inline void deserialize_const_p_for_each(void*& pDeserialize, const T*& deserializeTo, const size_t num) {
  T* new_arr = (T*) new T[num];
  for(size_t i = 0; i < num; ++i) {
    bridge_util::deserialize(pDeserialize, new_arr[i]);
  }
  deserializeTo = new_arr;
}

}

namespace bridge_util {

// Types that should de-/serialize out of the box, because
// they only contain a single member which should be aligned
// with the start of the struct

// remixapi_Rect2D
template<>
static inline constexpr uint32_t sizeOf<remixapi_Rect2D>() {
  return 4 * sizeof(int32_t);
}

// remixapi_Float2D
template<>
static inline constexpr uint32_t sizeOf<remixapi_Float2D>() {
  return 2 * sizeof(float);
}

// remixapi_Float3D
template<>
static inline constexpr uint32_t sizeOf<remixapi_Float3D>() {
  return 3 * sizeof(float);
}

//remixapi_Float4D
template<>
static inline constexpr uint32_t sizeOf<remixapi_Float4D>() {
  return 4 * sizeof(float);
}

// remixapi_Transform
template<>
static inline constexpr uint32_t sizeOf<remixapi_Transform>() {
  return 3 * 4 * sizeof(float);
}

// remixapi_Path
static inline uint32_t pathSize(const remixapi_Path& path) {
  return (path ? wcslen(path) + 1 : 0) * sizeof(wchar_t);
}
template<>
static inline uint32_t sizeOf(const remixapi_Path& path) {
  return sizeOf<bool>() + pathSize(path);
}
template<>
void serialize(const remixapi_Path& serializeFrom, void*& pSerialize) {
  serialize((serializeFrom) ? true : false, pSerialize);
  if (serializeFrom) {
    serialize(serializeFrom, pSerialize, pathSize(serializeFrom));
  }
}
template<>
void deserialize(void*& deserializeFrom, remixapi_Path& deserializeTo) {
  bool bIsValidString = false;
  deserialize(deserializeFrom, bIsValidString);
  if(bIsValidString) {
    const uint32_t size = pathSize(reinterpret_cast<const remixapi_Path&>(deserializeFrom));
    assert(size <= MAX_PATH);
    auto intermediate = new wchar_t[size];
    deserialize(deserializeFrom, intermediate, size);
    deserializeTo = intermediate;
  } else {
    deserializeTo = nullptr;
  }
}

// remixapi_HardcodedVertex
template<>
static inline constexpr uint32_t sizeOf<remixapi_HardcodedVertex>() {
  return sizeof(remixapi_HardcodedVertex::position)
       + sizeof(remixapi_HardcodedVertex::normal)
       + sizeof(remixapi_HardcodedVertex::texcoord)
       + sizeof(remixapi_HardcodedVertex::color);
}
template<>
void serialize(const remixapi_HardcodedVertex& serializeFrom, void*& pSerialize) {
  serialize(&serializeFrom.position[0], pSerialize, sizeOf(serializeFrom.position));
  serialize(&serializeFrom.normal[0], pSerialize, sizeOf(serializeFrom.normal));
  serialize(&serializeFrom.texcoord[0], pSerialize, sizeOf(serializeFrom.texcoord));
  serialize(&serializeFrom.color, pSerialize, sizeOf(serializeFrom.color));
}
template<>
void deserialize(void*& deserializeFrom, remixapi_HardcodedVertex& deserializeTo) {
  deserialize(deserializeFrom, &deserializeTo.position[0], sizeOf(deserializeTo.position));
  deserialize(deserializeFrom, &deserializeTo.normal[0], sizeOf(deserializeTo.normal));
  deserialize(deserializeFrom, &deserializeTo.texcoord[0], sizeOf(deserializeTo.texcoord));
  deserialize(deserializeFrom, &deserializeTo.color, sizeOf(deserializeTo.color));
}

// remixapi_*Handles
// convenience macro to define same specializations for all three types
#define REMIX_API_HANDLE_FUNCS(HandleT) \
template<> \
static inline constexpr uint32_t sizeOf<HandleT>() { \
  return sizeof(uint32_t); \
} \
template<> \
void serialize(const HandleT& serializeFrom, void*& serializeTo) { \
  serialize(&serializeFrom, serializeTo, sizeof(uint32_t)); \
} \
template<> \
void deserialize(void*& deserializeFrom, HandleT& deserializeTo) { \
  deserializeTo = nullptr; \
  deserialize(deserializeFrom, &deserializeTo, sizeof(uint32_t)); \
}
REMIX_API_HANDLE_FUNCS(remixapi_MaterialHandle)
REMIX_API_HANDLE_FUNCS(remixapi_MeshHandle)
REMIX_API_HANDLE_FUNCS(remixapi_LightHandle)


//////////////////
// MaterialInfo //
//////////////////

#define MaterialInfoVars sType, \
                         hash, \
                         albedoTexture, \
                         normalTexture, \
                         tangentTexture, \
                         emissiveTexture, \
                         emissiveIntensity, \
                         emissiveColorConstant, \
                         spriteSheetRow, \
                         spriteSheetCol, \
                         spriteSheetFps, \
                         filterMode, \
                         wrapModeU, \
                         wrapModeV
uint32_t MaterialInfo::_calcSize() const {
  return fold_helper::calcSize(MaterialInfoVars);
}
void MaterialInfo::_serialize(void*& pSerialize) const {
  fold_helper::serialize(pSerialize, MaterialInfoVars);
}
void MaterialInfo::_deserialize(void*& pDeserialize) {
  pNext = nullptr;
  fold_helper::deserialize(pDeserialize, MaterialInfoVars);
}
void MaterialInfo::_dtor() {
  delete albedoTexture;
  delete normalTexture;
  delete tangentTexture;
  delete emissiveTexture;
}


#define MaterialInfoOpaqueVars sType, \
                               roughnessTexture, \
                               metallicTexture, \
                               anisotropy, \
                               albedoConstant, \
                               opacityConstant, \
                               roughnessConstant, \
                               metallicConstant, \
                               thinFilmThickness_hasvalue, \
                               thinFilmThickness_value, \
                               alphaIsThinFilmThickness, \
                               heightTexture, \
                               displaceIn, \
                               useDrawCallAlphaState, \
                               blendType_hasvalue, \
                               blendType_value, \
                               invertedBlend, \
                               alphaTestType, \
                               alphaReferenceValue, \
                               displaceOut
uint32_t MaterialInfoOpaque::_calcSize() const {
  return fold_helper::calcSize(MaterialInfoOpaqueVars);
}
void MaterialInfoOpaque::_serialize(void*& pSerialize) const {
  fold_helper::serialize(pSerialize, MaterialInfoOpaqueVars);
}
void MaterialInfoOpaque::_deserialize(void*& pDeserialize) {
  pNext = nullptr;
  fold_helper::deserialize(pDeserialize, MaterialInfoOpaqueVars);
}
void MaterialInfoOpaque::_dtor() {
  delete roughnessTexture;
  delete metallicTexture;
  delete heightTexture;
}


#define MaterialInfoOpaqueSubsurfaceVars sType, \
                                         subsurfaceTransmittanceTexture, \
                                         subsurfaceThicknessTexture, \
                                         subsurfaceSingleScatteringAlbedoTexture, \
                                         subsurfaceTransmittanceColor, \
                                         subsurfaceMeasurementDistance, \
                                         subsurfaceSingleScatteringAlbedo, \
                                         subsurfaceVolumetricAnisotropy
uint32_t MaterialInfoOpaqueSubsurface::_calcSize() const {
  return fold_helper::calcSize(MaterialInfoOpaqueSubsurfaceVars);
}
void MaterialInfoOpaqueSubsurface::_serialize(void*& pSerialize) const {
  fold_helper::serialize(pSerialize, MaterialInfoOpaqueSubsurfaceVars);
}
void MaterialInfoOpaqueSubsurface::_deserialize(void*& pDeserialize) {
  pNext = nullptr;
  fold_helper::deserialize(pDeserialize, MaterialInfoOpaqueSubsurfaceVars);
}
void MaterialInfoOpaqueSubsurface::_dtor() {
  delete subsurfaceTransmittanceTexture;
  delete subsurfaceThicknessTexture;
  delete subsurfaceSingleScatteringAlbedoTexture;
}


#define MaterialInfoTranslucentVars sType, \
                                    transmittanceTexture, \
                                    refractiveIndex, \
                                    transmittanceColor, \
                                    transmittanceMeasurementDistance, \
                                    thinWallThickness_hasvalue, \
                                    thinWallThickness_value, \
                                    useDiffuseLayer
uint32_t MaterialInfoTranslucent::_calcSize() const {
  return fold_helper::calcSize(MaterialInfoTranslucentVars);
}
void MaterialInfoTranslucent::_serialize(void*& pSerialize) const {
  fold_helper::serialize(pSerialize, MaterialInfoTranslucentVars);
}
void MaterialInfoTranslucent::_deserialize(void*& pDeserialize) {
  pNext = nullptr;
  fold_helper::deserialize(pDeserialize, MaterialInfoTranslucentVars);
}
void MaterialInfoTranslucent::_dtor() {
  delete transmittanceTexture;
}


#define MaterialInfoPortalVars sType, \
                               rayPortalIndex, \
                               rotationSpeed
uint32_t MaterialInfoPortal::_calcSize() const {
  return fold_helper::calcSize(MaterialInfoPortalVars);
}
void MaterialInfoPortal::_serialize(void*& pSerialize) const {
  fold_helper::serialize(pSerialize, MaterialInfoPortalVars);
}
void MaterialInfoPortal::_deserialize(void*& pDeserialize) {
  pNext = nullptr;
  fold_helper::deserialize(pDeserialize, MaterialInfoPortalVars);
}
void MaterialInfoPortal::_dtor() {
}

//////////////
// MeshInfo //
//////////////

uint32_t MeshInfo::_calcSize() const;
template<>
static inline uint32_t sizeOf(const remixapi_MeshInfoSurfaceTriangles& surface);

void MeshInfo::_serialize(void*& pSerialize) const;
template<>
void serialize(const remixapi_MeshInfoSurfaceTriangles& surface, void*& pSerialize);

void MeshInfo::_deserialize(void*& pDeserialize);
template<>
void deserialize(void*& pDeserialize, remixapi_MeshInfoSurfaceTriangles& surface);


uint32_t MeshInfo::_calcSize() const {
  uint32_t size = 0;
  size += sizeOf(sType);
  size += sizeOf(hash);
  size += sizeOf(surfaces_count);
  for(size_t nSurface = 0; nSurface < surfaces_count; ++nSurface) {
    size += sizeOf(surfaces_values[nSurface]);
  }
  return size;
}

static inline uint32_t blendWeightSizePerVtx(const remixapi_MeshInfoSkinning& skinning) {
  return skinning.blendWeights_count * skinning.bonesPerVertex * sizeof(float);
}
static inline uint32_t blendIndicesSizePerVtx(const remixapi_MeshInfoSkinning& skinning) {
  return skinning.blendIndices_count * skinning.bonesPerVertex * sizeof(uint32_t);
}
template<>
static inline uint32_t sizeOf(const remixapi_MeshInfoSurfaceTriangles& surface) {
  uint32_t size = 0;
  size += sizeOf(surface.vertices_count);
  size += surface.vertices_count * sizeOf(*surface.vertices_values);
  size += sizeOf(surface.indices_count);
  size += surface.indices_count * sizeOf(*surface.indices_values);
  size += sizeOf(surface.skinning_hasvalue);
  if(surface.skinning_hasvalue) {
    size += sizeOf(surface.skinning_value.bonesPerVertex);
    size += sizeOf(surface.skinning_value.blendWeights_count);
    size += surface.vertices_count * blendWeightSizePerVtx(surface.skinning_value);
    size += sizeOf(surface.skinning_value.blendIndices_count);
    size += surface.vertices_count * blendIndicesSizePerVtx(surface.skinning_value);
  }
  size += sizeof(uint32_t);
  return size;
}

void MeshInfo::_serialize(void*& pSerialize) const {
  bridge_util::serialize(sType, pSerialize);
  bridge_util::serialize(hash, pSerialize);
  bridge_util::serialize(surfaces_count, pSerialize);
  for(size_t nSurface = 0; nSurface < surfaces_count; ++nSurface) {
    bridge_util::serialize(surfaces_values[nSurface], pSerialize);
  }
}

template<>
void serialize(const remixapi_MeshInfoSurfaceTriangles& surface, void*& pSerialize) {
  // Vtxs
  bridge_util::serialize(surface.vertices_count, pSerialize);
  for(size_t iVtx = 0; iVtx < surface.vertices_count; ++iVtx) {
    bridge_util::serialize(surface.vertices_values[iVtx], pSerialize);
  }
  // Idxs
  bridge_util::serialize(surface.indices_count, pSerialize);
  const size_t indicesSize = surface.indices_count * sizeof(uint32_t);
  bridge_util::serialize(surface.indices_values, pSerialize, indicesSize);
  // Skinning
  bridge_util::serialize(surface.skinning_hasvalue, pSerialize);
  if(surface.skinning_hasvalue) {
    const auto& skinning = surface.skinning_value;
    bridge_util::serialize(skinning.bonesPerVertex, pSerialize);
    // Blend Weights
    bridge_util::serialize(skinning.blendWeights_count, pSerialize);
    const size_t blendWeightsSize = surface.vertices_count * blendWeightSizePerVtx(skinning);
    bridge_util::serialize(skinning.blendWeights_values, pSerialize, blendWeightsSize);
    // Blend Indices
    bridge_util::serialize(skinning.blendIndices_count, pSerialize);
    const size_t blendIndicesSize = surface.vertices_count * blendIndicesSizePerVtx(skinning);
    bridge_util::serialize(skinning.blendIndices_values, pSerialize, blendIndicesSize);
  }
  bridge_util::serialize(surface.material, pSerialize);
}

void MeshInfo::_deserialize(void*& pDeserialize) {
  bridge_util::deserialize(pDeserialize, sType);
  bridge_util::deserialize(pDeserialize, hash);
  bridge_util::deserialize(pDeserialize, surfaces_count);
  deserialize_const_p_for_each(pDeserialize, surfaces_values, surfaces_count);
}

template<>
void deserialize(void*& pDeserialize, remixapi_MeshInfoSurfaceTriangles& surface) {
  // Vtxs
  bridge_util::deserialize(pDeserialize, surface.vertices_count);
  deserialize_const_p_for_each(pDeserialize, surface.vertices_values, surface.vertices_count);
  // Idxs
  bridge_util::deserialize(pDeserialize, surface.indices_count);
  const size_t indicesSize = surface.indices_count * sizeof(uint32_t);
  deserialize_const_p(pDeserialize, surface.indices_values, indicesSize);
  // Skinning
  bridge_util::deserialize(pDeserialize, surface.skinning_hasvalue);
  if(surface.skinning_hasvalue) {
    auto& skinning = surface.skinning_value;
    bridge_util::deserialize(pDeserialize, skinning.bonesPerVertex);
    // Blend Weights
    bridge_util::deserialize(pDeserialize, skinning.blendWeights_count);
    const size_t blendWeightsSize = surface.vertices_count * blendWeightSizePerVtx(skinning);
    deserialize_const_p(pDeserialize, skinning.blendWeights_values, blendWeightsSize);
    // Blend Indices
    bridge_util::deserialize(pDeserialize, skinning.blendIndices_count);
    const size_t blendIndicesSize = surface.vertices_count * blendIndicesSizePerVtx(skinning);
    deserialize_const_p(pDeserialize, skinning.blendIndices_values, blendIndicesSize);
  }
  bridge_util::deserialize(pDeserialize, surface.material);
}

void MeshInfo::_dtor() {
  for(size_t nSurface = 0; nSurface < surfaces_count; ++nSurface) {
    auto& surface = surfaces_values[nSurface];
    delete surface.vertices_values;
    delete surface.indices_values;
    if(surface.skinning_hasvalue) {
      auto& skinning = surface.skinning_value;
      delete skinning.blendWeights_values;
      delete skinning.blendIndices_values;
    }
  }
}

//////////////////
// InstanceInfo //
//////////////////
#define InstanceInfoVars sType, \
                         categoryFlags, \
                         mesh, \
                         transform, \
                         doubleSided               
uint32_t InstanceInfo::_calcSize() const {
  return fold_helper::calcSize(InstanceInfoVars);
}
void InstanceInfo::_serialize(void*& pSerialize) const {
  fold_helper::serialize(pSerialize, InstanceInfoVars);
}
void InstanceInfo::_deserialize(void*& pDeserialize) {
  pNext = nullptr;
  fold_helper::deserialize(pDeserialize, InstanceInfoVars);
}
void InstanceInfo::_dtor() {
}


#define InstanceInfoObjectPickingVars objectPickingValue
uint32_t InstanceInfoObjectPicking::_calcSize() const {
  return fold_helper::calcSize(InstanceInfoObjectPickingVars);
}
void InstanceInfoObjectPicking::_serialize(void*& pSerialize) const {
  fold_helper::serialize(pSerialize, InstanceInfoObjectPickingVars);
}
void InstanceInfoObjectPicking::_deserialize(void*& pDeserialize) {
  pNext = nullptr;
  fold_helper::deserialize(pDeserialize, InstanceInfoObjectPickingVars);
}
void InstanceInfoObjectPicking::_dtor() {
}


#define InstanceInfoBlendVars sType, \
                              alphaTestEnabled, \
                              alphaTestReferenceValue, \
                              alphaTestCompareOp, \
                              alphaBlendEnabled, \
                              srcColorBlendFactor, \
                              dstColorBlendFactor, \
                              colorBlendOp, \
                              textureColorArg1Source, \
                              textureColorArg2Source, \
                              textureColorOperation, \
                              textureAlphaArg1Source, \
                              textureAlphaArg2Source, \
                              textureAlphaOperation, \
                              tFactor, \
                              isTextureFactorBlend, \
                              srcAlphaBlendFactor, \
                              dstAlphaBlendFactor, \
                              alphaBlendOp, \
                              writeMask
uint32_t InstanceInfoBlend::_calcSize() const {
  return fold_helper::calcSize(InstanceInfoBlendVars);
}
void InstanceInfoBlend::_serialize(void*& pSerialize) const {
  fold_helper::serialize(pSerialize, InstanceInfoBlendVars);
}
void InstanceInfoBlend::_deserialize(void*& pDeserialize) {
  pNext = nullptr;
  fold_helper::deserialize(pDeserialize, InstanceInfoBlendVars);
}
void InstanceInfoBlend::_dtor() {
}


#define InstanceInfoParticleSystemVars sType, \
                                       maxNumParticles, \
                                       useTurbulence, \
                                       alignParticlesToVelocity, \
                                       useSpawnTexcoords, \
                                       enableCollisionDetection, \
                                       enableMotionTrail, \
                                       hideEmitter, \
                                       minSpawnColor, \
                                       maxSpawnColor, \
                                       minTimeToLive, \
                                       maxTimeToLive, \
                                       initialVelocityFromMotion, \
                                       initialVelocityFromNormal, \
                                       initialVelocityConeAngleDegrees, \
                                       minSpawnSize, \
                                       maxSpawnSize, \
                                       gravityForce, \
                                       maxSpeed, \
                                       turbulenceFrequency, \
                                       turbulenceForce, \
                                       minSpawnRotationSpeed, \
                                       maxSpawnRotationSpeed, \
                                       spawnRatePerSecond, \
                                       collisionThickness, \
                                       collisionRestitution, \
                                       motionTrailMultiplier, \
                                       minTargetSize, \
                                       maxTargetSize, \
                                       minTargetRotationSpeed, \
                                       maxTargetRotationSpeed, \
                                       minTargetColor, \
                                       maxTargetColor 
uint32_t InstanceInfoParticleSystem::_calcSize() const {
  return fold_helper::calcSize(InstanceInfoParticleSystemVars);
}
void InstanceInfoParticleSystem::_serialize(void*& pSerialize) const {
  fold_helper::serialize(pSerialize, InstanceInfoParticleSystemVars);
}
void InstanceInfoParticleSystem::_deserialize(void*& pDeserialize) {
  pNext = nullptr;
  fold_helper::deserialize(pDeserialize, InstanceInfoParticleSystemVars);
}
void InstanceInfoParticleSystem::_dtor() { }


uint32_t InstanceInfoTransforms::_calcSize() const {
  uint32_t size = 0;
  size += sizeOf(sType);
  size += sizeOf(boneTransforms_count);
  size += sizeOf<remixapi_Transform>() * boneTransforms_count;
  return size;
}
void InstanceInfoTransforms::_serialize(void*& pSerialize) const {
  bridge_util::serialize(sType, pSerialize);
  bridge_util::serialize(boneTransforms_count, pSerialize);
  for(size_t iXform = 0; iXform < boneTransforms_count; ++iXform) {
    bridge_util::serialize(boneTransforms_values[iXform], pSerialize);
  }
}
void InstanceInfoTransforms::_deserialize(void*& pDeserialize) {
  bridge_util::deserialize(pDeserialize, sType);
  pNext = nullptr;
  bridge_util::deserialize(pDeserialize, boneTransforms_count);
  deserialize_const_p_for_each(pDeserialize, boneTransforms_values, boneTransforms_count);
}
void InstanceInfoTransforms::_dtor() {
  delete boneTransforms_values;
}


///////////////
// LightInfo //
///////////////

#define LightInfoVars sType, \
                      hash, \
                      radiance
uint32_t LightInfo::_calcSize() const {
  return fold_helper::calcSize(LightInfoVars);
}
void LightInfo::_serialize(void*& pSerialize) const {
  fold_helper::serialize(pSerialize, LightInfoVars);
}
void LightInfo::_deserialize(void*& pDeserialize) {
  pNext = nullptr;
  fold_helper::deserialize(pDeserialize, LightInfoVars);
}
void LightInfo::_dtor() {
}


#define LightShapingVars shaping.direction, \
                         shaping.coneAngleDegrees, \
                         shaping.coneSoftness, \
                         shaping.focusExponent  
template<>
static inline uint32_t sizeOf(const remixapi_LightInfoLightShaping& shaping) {
  return fold_helper::calcSize(LightShapingVars);
}
template<>
void serialize(const remixapi_LightInfoLightShaping& shaping, void*& pSerialize) {
  fold_helper::serialize(pSerialize, LightShapingVars);
}
template<>
void deserialize(void*& pDeserialize, remixapi_LightInfoLightShaping& shaping) {
  fold_helper::deserialize(pDeserialize, LightShapingVars);
}


#define LightInfoSphereVars sType, \
                            position, \
                            radius, \
                            shaping_hasvalue, \
                            shaping_value
uint32_t LightInfoSphere::_calcSize() const {
  return fold_helper::calcSize(LightInfoSphereVars);
}
void LightInfoSphere::_serialize(void*& pSerialize) const {
  fold_helper::serialize(pSerialize, LightInfoSphereVars);
}
void LightInfoSphere::_deserialize(void*& pDeserialize) {
  pNext = nullptr;
  fold_helper::deserialize(pDeserialize, LightInfoSphereVars);
}
void LightInfoSphere::_dtor() {
}


#define LightInfoRectVars sType, \
                          position, \
                          xAxis, \
                          xSize, \
                          yAxis, \
                          ySize, \
                          direction, \
                          shaping_hasvalue, \
                          shaping_value
uint32_t LightInfoRect::_calcSize() const {
  return fold_helper::calcSize(LightInfoRectVars);
}
void LightInfoRect::_serialize(void*& pSerialize) const {
  fold_helper::serialize(pSerialize, LightInfoRectVars);
}
void LightInfoRect::_deserialize(void*& pDeserialize) {
  pNext = nullptr;
  fold_helper::deserialize(pDeserialize, LightInfoRectVars);
}
void LightInfoRect::_dtor() {
}


#define LightInfoDiskVars sType, \
                          position, \
                          xAxis, \
                          xRadius, \
                          yAxis, \
                          yRadius, \
                          direction, \
                          shaping_hasvalue, \
                          shaping_value
uint32_t LightInfoDisk::_calcSize() const {
  return fold_helper::calcSize(LightInfoDiskVars);
}
void LightInfoDisk::_serialize(void*& pSerialize) const {
  fold_helper::serialize(pSerialize, LightInfoDiskVars);
}
void LightInfoDisk::_deserialize(void*& pDeserialize) {
  pNext = nullptr;
  fold_helper::deserialize(pDeserialize, LightInfoDiskVars);
}
void LightInfoDisk::_dtor() {
}


#define LightInfoCylinderVars sType, \
                              position, \
                              radius, \
                              axis, \
                              axisLength
uint32_t LightInfoCylinder::_calcSize() const {
  return fold_helper::calcSize(LightInfoCylinderVars);
}
void LightInfoCylinder::_serialize(void*& pSerialize) const {
  fold_helper::serialize(pSerialize, LightInfoCylinderVars);
}
void LightInfoCylinder::_deserialize(void*& pDeserialize) {
  pNext = nullptr;
  fold_helper::deserialize(pDeserialize, LightInfoCylinderVars);
}
void LightInfoCylinder::_dtor() {
}


#define LightInfoDistantVars sType, \
                             direction, \
                             angularDiameterDegrees
uint32_t LightInfoDistant::_calcSize() const {
  return fold_helper::calcSize(LightInfoDistantVars);
}
void LightInfoDistant::_serialize(void*& pSerialize) const {
  fold_helper::serialize(pSerialize, LightInfoDistantVars);
}
void LightInfoDistant::_deserialize(void*& pDeserialize) {
  pNext = nullptr;
  fold_helper::deserialize(pDeserialize, LightInfoDistantVars);
}
void LightInfoDistant::_dtor() {
}



#define LightInfoDomeVars sType, \
                          transform, \
                          colorTexture
uint32_t LightInfoDome::_calcSize() const {
  return fold_helper::calcSize(LightInfoDomeVars);
}
void LightInfoDome::_serialize(void*& pSerialize) const {
  fold_helper::serialize(pSerialize, LightInfoDomeVars);
}
void LightInfoDome::_deserialize(void*& pDeserialize) {
  pNext = nullptr;
  fold_helper::deserialize(pDeserialize, LightInfoDomeVars);
}
void LightInfoDome::_dtor() {
  delete colorTexture;
}


// remixapi_LightInfoUSDEXT uses a pattern wherein optional members
// are determined by whether a given pointer is NULL or not, therefore
// we have defined some helpers in an anonymous namespace to operate on
// such members a bit differently, and have separated the variables
#define LightInfoUSDVars sType, \
                         lightType, \
                         transform
#define LightInfoUSDOptionalVars pRadius, \
                                 pWidth, \
                                 pHeight, \
                                 pLength, \
                                 pAngleRadians, \
                                 pEnableColorTemp, \
                                 pColor, \
                                 pColorTemp, \
                                 pExposure, \
                                 pIntensity, \
                                 pConeAngleRadians, \
                                 pConeSoftness, \
                                 pFocus
// calcSize helpers
namespace{
template<typename T>
static inline uint32_t sizeOf__optionalPointer(const T* pObj) {
  uint32_t size = bridge_util::sizeOf<bool>(); // We serialize out bools to indicated populated value
  if(pObj != nullptr) {
    size += bridge_util::sizeOf<underlying<T>>();
  }
  return size;
}
template<typename... Types>
static uint32_t calcSize__optionalPointer(const Types&... args) {
  return (sizeOf__optionalPointer(args) + ...);
}
}
uint32_t LightInfoUSD::_calcSize() const {
  return fold_helper::calcSize(LightInfoUSDVars)
       + calcSize__optionalPointer(LightInfoUSDOptionalVars);
}
// serialize helpers
namespace{
template<typename T>
static inline void serialize__optionalPointer(const T* pObj, void*& pSerialize) {
  const bool bHasValue = (pObj != nullptr);
  bridge_util::serialize(bHasValue, pSerialize);
  if(bHasValue) {
    bridge_util::serialize(*pObj, pSerialize);
  }
}
template<typename... Types>
static void fold_serialize__optionalPointer(void*& pSerialize, const Types&... args) {
  return (serialize__optionalPointer(args, pSerialize) , ...);
}
}
void LightInfoUSD::_serialize(void*& pSerialize) const {
  fold_helper::serialize(pSerialize, LightInfoUSDVars);
  fold_serialize__optionalPointer(pSerialize, LightInfoUSDOptionalVars);
}
// deserialize helpers
namespace{
template<typename T>
static inline void deserialize__optionalPointer(void*& pDeserialize, T& val) {
  bool bHasValue = false;
  bridge_util::deserialize(pDeserialize, bHasValue);
  if(bHasValue) {
    deserialize_const_p(pDeserialize, val, bridge_util::sizeOf(*val));
  } else {
    val = nullptr;
  }
}
template<typename... Types>
static void fold_deserialize__optionalPointer(void*& pDeserialize, Types&... args) {
  return (deserialize__optionalPointer(pDeserialize, args) , ...);
}
}
void LightInfoUSD::_deserialize(void*& pDeserialize) {
  pNext = nullptr;
  fold_helper::deserialize(pDeserialize, LightInfoUSDVars);
  fold_deserialize__optionalPointer(pDeserialize, LightInfoUSDOptionalVars);
}
void LightInfoUSD::_dtor() {
  if (pRadius) delete pRadius;
  if (pWidth) delete pWidth;
  if (pHeight) delete pHeight;
  if (pLength) delete pLength;
  if (pAngleRadians) delete pAngleRadians;
  if (pEnableColorTemp) delete pEnableColorTemp;
  if (pColor) delete pColor;
  if (pColorTemp) delete pColorTemp;
  if (pExposure) delete pExposure;
  if (pIntensity) delete pIntensity;
  if (pConeAngleRadians) delete pConeAngleRadians;
  if (pConeSoftness) delete pConeSoftness;
  if (pFocus) delete pFocus;
}

}