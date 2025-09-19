#pragma once

#include "util_remixapi.h"

#include <vector>

namespace expected {

template<typename T>
struct ValueSet {
  static_assert("false");
};

#define Values(T, ...) \
template<> \
struct ValueSet<T> { \
  inline static const std::vector<T> vals = {__VA_ARGS__}; \
};

Values(bool, false, true, false, false, false, true, true, false, false, true, true, true)
Values(uint8_t, 0xA0, 0xB1, 0xC2, 0xD3, 0xE5, 0xF6)
Values(uint32_t, 0x01234567, 0x0, 0x89ABCDEF, 0xABABCDCD, 0x0, 0xEEEEFFFF, 0xDEADBEEF)
Values(int32_t, 0x7123456F, 0x789ABCDF, 0x7ABACDCF, 0x7FFFFFFF, 0x7BDCCDB7)
Values(uint64_t, 0xFEDCBA987654320, 0xFFEEDDCCBBAA9988, 0x77665544332211, 0x010011000111ACDB)
Values(float, 0.1234f, 1.234f, 9.876f, 0.9876f, 13.37f)
Values(remixapi_Float3D, {1.f,2.f,3.f}, {4.f,5.f,6.f}, {7.f,8.f,9.f})
Values(remixapi_Transform, {{{ 1.f, 2.f,   3.f,  4.f },
                             { 5.f, 6.f,   7.f,  8.f },
                             { 9.f, 10.f, 11.f, 12.f }}},
                           {{{ 1.f, 0.f, 0.f, 0.f },
                             { 0.f, 1.f, 0.f, 0.f },
                             { 0.f, 0.f, 1.f, 0.f }}},
                           {{{ 42.f, 1337.f, 101.f, 777.f },
                             { 25.f,   25.f,  25.f,  25.f },
                             {  8.f,     0.f,  8.f,   5.f }}})
Values(remixapi_Path, L"ABCDEFGH",
                      L"Twinkle twinkle little star",
                      L"",
                      nullptr,
                      L"I:\\\\look\\like\\a\\path\\",
                      L"H:\\ow\\about\\me?",
                      L"./My/name/is/Jeff")

template<typename T>
const std::vector<T>& vals() { return ValueSet<T>::vals; }

template<typename T>
static void populateVal(T& val) {
  static size_t seed = 0;
  val = vals<T>().at(seed++ % vals<T>().size());
}

template<>
static void populateVal(remixapi_MaterialHandle& val) {
  populateVal((uint32_t&)(uintptr_t&)val);
}
template<>
static void populateVal(remixapi_MeshHandle& val) {
  populateVal((uint32_t&)(uintptr_t&)val);
}
template<>
static void populateVal(remixapi_LightHandle& val) {
  populateVal((uint32_t&)(uintptr_t&)val);
}

template <typename... Types>
static void populateVals(Types&... args) {
  (populateVal(args), ...);
}

template <typename T>
static void populateReasonableVal(T& val) {
  static const std::vector<size_t> vals{
    1,2,5,10,20,42,100
  };
  static size_t seed = 0;
  val = (T)(vals.at(seed++ % vals.size()));
}

#define SIMPLE_COMPARE(ME_NAME, OTHER_NAME, VAL_NAME) \
  if(!expected::compare(ME_NAME.VAL_NAME,OTHER_NAME.VAL_NAME)) { return false; }

#define PTR_COMPARE(ME_NAME, OTHER_NAME, VAL_NAME, SIZE) \
  if(memcmp(ME_NAME.VAL_NAME, \
            OTHER_NAME.VAL_NAME, \
            SIZE) != 0) { return false; }

template<typename T>
static bool compare(const T& a, const T& b) {
  return a == b;
}

template<typename T, size_t arrLen>
static bool compare(const T (&a)[arrLen], const T (&b)[arrLen]) {
  return memcmp(&a[0], &b[0], arrLen * sizeof(T)) == 0;
}

template<>
static bool compare(const remixapi_Float3D& a, const remixapi_Float3D& b) {
  return a.x == b.x
      && a.y == b.y
      && a.z == b.z;
}

template<>
static bool compare(const remixapi_Transform& a, const remixapi_Transform& b) {
  return memcmp( &(a.matrix[0][0]), &(b.matrix[0][0]), (3 * 4 * sizeof(float)) ) == 0;
}

template<>
static bool compare(const remixapi_Path& a, const remixapi_Path& b) {
  if(!a || !b) {
    return !a && !b;
  }
  return wcscmp(a, b) == 0;
}

template<typename T>
struct Comparable{
  using Type = T;
  Comparable(const uintptr_t a_inst, const uintptr_t b_inst, const uintptr_t a_member)
    : a(*reinterpret_cast<T*>(a_member)),
      b(*reinterpret_cast<T*>(b_inst + (a_member - a_inst))) {}
  const T& a;
  const T& b;
};

template<typename T>
static bool compareVal(const Comparable<T>& comparable) {
  return expected::compare(comparable.a, comparable.b);
}

template <typename RemixApiT, typename T>
static bool createComparableAndCompare(const RemixApiT& a, const RemixApiT& b, const T& val) {
  Comparable<T> comparable((uintptr_t)(void*)(&a),
                           (uintptr_t)(void*)(&b),
                           (uintptr_t)(void*)(&val));
  return compareVal(comparable);
}

template <typename RemixApiT, typename... Types>
static bool compareVals(const RemixApiT& a, const RemixApiT& b, const Types&... args) {
  return (createComparableAndCompare(a, b, args) && ...);
}

template<typename T>
class Expected : public T {
public:
  using RemixApiT = T;
  Expected() {
    init();
  }
  inline bool operator==(const RemixApiT& other) const {
    return compare(*this, other);
  }
  inline bool operator!=(const RemixApiT& other) const {
    return !(*this == other);
  }
private:
  void init(); // Implement me
  bool compare(const RemixApiT& me, const RemixApiT& other) const; // Implement me
};


using MaterialInfo = Expected<remixapi_MaterialInfo>;
#define MaterialInfoVars hash, \
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
void MaterialInfo::init() {
  sType = REMIXAPI_STRUCT_TYPE_MATERIAL_INFO;
  pNext = nullptr;
  populateVals(MaterialInfoVars);
}
bool MaterialInfo::compare(const RemixApiT& me, const RemixApiT& other) const {
  return compareVals(me, other, MaterialInfoVars);
}


using MaterialInfoOpaque = Expected<remixapi_MaterialInfoOpaqueEXT>;
#define MaterialInfoOpaqueVars roughnessTexture, \
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
void MaterialInfoOpaque::init() {
  sType = REMIXAPI_STRUCT_TYPE_MATERIAL_INFO_OPAQUE_EXT;
  pNext = nullptr;
  populateVals(MaterialInfoOpaqueVars);
}
bool MaterialInfoOpaque::compare(const RemixApiT& me, const RemixApiT& other) const {
  return compareVals(me, other, MaterialInfoOpaqueVars);
}


using MaterialInfoOpaqueSubsurface = Expected<remixapi_MaterialInfoOpaqueSubsurfaceEXT>;
#define MaterialInfoOpaqueSubsurfaceVars subsurfaceTransmittanceTexture, \
                                         subsurfaceThicknessTexture, \
                                         subsurfaceSingleScatteringAlbedoTexture, \
                                         subsurfaceTransmittanceColor, \
                                         subsurfaceMeasurementDistance, \
                                         subsurfaceSingleScatteringAlbedo, \
                                         subsurfaceVolumetricAnisotropy
void MaterialInfoOpaqueSubsurface::init() {
  sType = REMIXAPI_STRUCT_TYPE_MATERIAL_INFO_OPAQUE_SUBSURFACE_EXT;
  pNext = nullptr;
  populateVals(MaterialInfoOpaqueSubsurfaceVars);
}
bool MaterialInfoOpaqueSubsurface::compare(const RemixApiT& me, const RemixApiT& other) const {
  return compareVals(me, other, MaterialInfoOpaqueSubsurfaceVars);
}


using MaterialInfoTranslucent = Expected<remixapi_MaterialInfoTranslucentEXT>;
#define MaterialInfoTranslucentVars transmittanceTexture, \
                                    refractiveIndex, \
                                    transmittanceColor, \
                                    transmittanceMeasurementDistance, \
                                    thinWallThickness_hasvalue, \
                                    thinWallThickness_value, \
                                    useDiffuseLayer
void MaterialInfoTranslucent::init() {
  sType = REMIXAPI_STRUCT_TYPE_MATERIAL_INFO_TRANSLUCENT_EXT;
  pNext = nullptr;
  populateVals(MaterialInfoTranslucentVars);
}
bool MaterialInfoTranslucent::compare(const RemixApiT& me, const RemixApiT& other) const {
  return compareVals(me, other, MaterialInfoTranslucentVars);
}


using MaterialInfoPortal = Expected<remixapi_MaterialInfoPortalEXT>;
#define MaterialInfoPortalVars rayPortalIndex, \
                               rotationSpeed
void MaterialInfoPortal::init() {
  sType = REMIXAPI_STRUCT_TYPE_MATERIAL_INFO_PORTAL_EXT;
  pNext = nullptr;
  populateVals(MaterialInfoPortalVars);
}
bool MaterialInfoPortal::compare(const RemixApiT& me, const RemixApiT& other) const {
  return compareVals(me, other, MaterialInfoPortalVars);
}


using MeshInfo = Expected<remixapi_MeshInfo>;
static inline uint32_t blendWeightsPerVtx(const remixapi_MeshInfoSkinning& skinning) {
  return skinning.blendWeights_count * skinning.bonesPerVertex;
}
static inline uint32_t blendIndicesSizePerVtx(const remixapi_MeshInfoSkinning& skinning) {
  return skinning.blendIndices_count * skinning.bonesPerVertex * sizeof(uint32_t);
}
void MeshInfo::init() {
  sType = REMIXAPI_STRUCT_TYPE_MESH_INFO;
  pNext = nullptr;
  populateVal(hash);
  populateReasonableVal(surfaces_count);
  auto* pSurfaces = new remixapi_MeshInfoSurfaceTriangles[surfaces_count];
  for(size_t iSurface = 0; iSurface < surfaces_count; ++iSurface) {
    remixapi_MeshInfoSurfaceTriangles& surface = pSurfaces[iSurface];
    // Vtxs
    populateReasonableVal(surface.vertices_count);
    auto* pVertices = new remixapi_HardcodedVertex[surface.vertices_count];
    for(size_t iVtx = 0; iVtx < surface.vertices_count; ++iVtx) {
      remixapi_HardcodedVertex& vtx = pVertices[iVtx];
      populateVal(vtx.position[0]);
      populateVal(vtx.position[1]);
      populateVal(vtx.position[2]);
      populateVal(vtx.normal[0]);
      populateVal(vtx.normal[1]);
      populateVal(vtx.normal[2]);
      populateVal(vtx.texcoord[0]);
      populateVal(vtx.texcoord[1]);
      populateVal(vtx.color);
    }
    surface.vertices_values = pVertices;
    // Idxs
    populateReasonableVal(surface.indices_count);
    auto* pIndices = new uint32_t[surface.vertices_count];
    for(size_t iIdx = 0; iIdx < surface.indices_count; ++iIdx) {
      populateVal(pIndices[iIdx]);
    }
    surface.indices_values = pIndices;
    // Skinning
    populateVal(surface.skinning_hasvalue);
    if(surface.skinning_hasvalue) {
      remixapi_MeshInfoSkinning& skinning = surface.skinning_value;
      populateReasonableVal(skinning.bonesPerVertex);
      // Blend Weights
      populateReasonableVal(skinning.blendWeights_count);
      const size_t blendWeightsPerVtx = skinning.blendWeights_count * skinning.bonesPerVertex;
      const size_t numBlendWeights = blendWeightsPerVtx * surface.vertices_count;
      auto* pBlendWeights = new float[numBlendWeights];
      for(size_t iBW = 0; iBW < numBlendWeights; ++iBW) {
        populateVal(pBlendWeights[iBW]);
      }
      skinning.blendWeights_values = pBlendWeights;
      // Blend Indices
      populateReasonableVal(skinning.blendIndices_count);
      const size_t blendIdxsPerVtx = skinning.blendIndices_count * skinning.bonesPerVertex;
      const size_t numBlendIdxs = blendIdxsPerVtx * surface.vertices_count;
      auto* pBlendIdxs = new uint32_t[numBlendIdxs];
      for(size_t iBI = 0; iBI < numBlendIdxs; ++iBI) {
        populateVal(pBlendIdxs[iBI]);
      }
      skinning.blendIndices_values = pBlendIdxs;
    }
    surface.material = nullptr;
    populateVal((uint32_t&)(uintptr_t&)surface.material);
  }
  surfaces_values = pSurfaces;
}
bool MeshInfo::compare(const RemixApiT& me, const RemixApiT& other) const {
  SIMPLE_COMPARE(me, other, hash);
  SIMPLE_COMPARE(me, other, surfaces_count);
  for(size_t iSurface = 0; iSurface < surfaces_count; ++iSurface) {
    const auto& meSurface = me.surfaces_values[iSurface];
    const auto& otherSurface = other.surfaces_values[iSurface];
    // Vtxs
    SIMPLE_COMPARE(meSurface, otherSurface, vertices_count);
    for(size_t iVtx = 0; iVtx < meSurface.vertices_count; ++iVtx) {
      const auto& meVtx = meSurface.vertices_values[iVtx];
      const auto& otherVtx = otherSurface.vertices_values[iVtx];
      SIMPLE_COMPARE(meVtx, otherVtx, position[0]);
      SIMPLE_COMPARE(meVtx, otherVtx, position[1]);
      SIMPLE_COMPARE(meVtx, otherVtx, position[2]);
      SIMPLE_COMPARE(meVtx, otherVtx, normal[0]);
      SIMPLE_COMPARE(meVtx, otherVtx, normal[1]);
      SIMPLE_COMPARE(meVtx, otherVtx, normal[2]);
      SIMPLE_COMPARE(meVtx, otherVtx, texcoord[0]);
      SIMPLE_COMPARE(meVtx, otherVtx, texcoord[1]);
      SIMPLE_COMPARE(meVtx, otherVtx, color);
    }
    // Idxs
    SIMPLE_COMPARE(meSurface, otherSurface, indices_count);
    const auto nIdxs = meSurface.indices_count;
    for(size_t iIdx = 0; iIdx < nIdxs; ++iIdx) {
      PTR_COMPARE(meSurface, otherSurface, indices_values, nIdxs * sizeof(uint32_t));
    }
    // Skinning
    SIMPLE_COMPARE(meSurface, otherSurface, skinning_hasvalue);
    if(meSurface.skinning_hasvalue) {
      const auto& meSkinning = meSurface.skinning_value;
      const auto& otherSkinning = otherSurface.skinning_value;
      SIMPLE_COMPARE(meSkinning, otherSkinning, bonesPerVertex);
      // Blend Weights
      SIMPLE_COMPARE(meSkinning, otherSkinning, blendWeights_count);
      const size_t blendWeightsPerVtx = meSkinning.blendWeights_count * meSkinning.bonesPerVertex;
      const size_t numBlendWeights = blendWeightsPerVtx * meSurface.vertices_count;
      PTR_COMPARE(meSkinning, otherSkinning, blendWeights_values, numBlendWeights * sizeof(float));
      // Blend Indices
      SIMPLE_COMPARE(meSkinning, otherSkinning, blendIndices_count);
      const size_t blendIdxsPerVtx = meSkinning.blendIndices_count * meSkinning.bonesPerVertex;
      const size_t numBlendIdxs = blendIdxsPerVtx * meSurface.vertices_count;
      PTR_COMPARE(meSkinning, otherSkinning, blendIndices_values, numBlendWeights * sizeof(uint32_t));
    }
    SIMPLE_COMPARE(meSurface, otherSurface, material);
  }
  return true;
}


#define InstanceInfoVars categoryFlags, \
                         mesh, \
                         transform, \
                         doubleSided
using InstanceInfo = Expected<remixapi_InstanceInfo>;
void InstanceInfo::init() {
  sType = REMIXAPI_STRUCT_TYPE_INSTANCE_INFO;
  pNext = nullptr;
  mesh = nullptr;
  populateVals(InstanceInfoVars);
}
bool InstanceInfo::compare(const RemixApiT& me, const RemixApiT& other) const {
  return compareVals(me, other, InstanceInfoVars);
}


#define InstanceInfoObjectPickingVars objectPickingValue
using InstanceInfoObjectPicking = Expected<remixapi_InstanceInfoObjectPickingEXT>;
void InstanceInfoObjectPicking::init() {
  sType = REMIXAPI_STRUCT_TYPE_INSTANCE_INFO_OBJECT_PICKING_EXT;
  pNext = nullptr;
  populateVals(InstanceInfoObjectPickingVars);
}
bool InstanceInfoObjectPicking::compare(const RemixApiT& me, const RemixApiT& other) const {
  return compareVals(me, other, InstanceInfoObjectPickingVars);
}


#define InstanceInfoBlendVars alphaTestEnabled, \
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
                              writeMask, \
                              isVertexColorBakedLighting
using InstanceInfoBlend = Expected<remixapi_InstanceInfoBlendEXT>;
void InstanceInfoBlend::init() {
  sType = REMIXAPI_STRUCT_TYPE_INSTANCE_INFO_BLEND_EXT;
  pNext = nullptr;
  populateVals(InstanceInfoBlendVars);
}
bool InstanceInfoBlend::compare(const RemixApiT& me, const RemixApiT& other) const {
  return compareVals(me, other, InstanceInfoBlendVars);
}


using InstanceInfoTransforms = Expected<remixapi_InstanceInfoBoneTransformsEXT>;
void InstanceInfoTransforms::init() {
  sType = REMIXAPI_STRUCT_TYPE_INSTANCE_INFO_BONE_TRANSFORMS_EXT;
  pNext = nullptr;
  populateReasonableVal(boneTransforms_count);
  auto* pXforms = new remixapi_Transform[boneTransforms_count];
  for(size_t iXform = 0; iXform < boneTransforms_count; ++iXform) {
    populateVal(pXforms[iXform]);
  }
  boneTransforms_values = pXforms;
}
bool InstanceInfoTransforms::compare(const RemixApiT& me, const RemixApiT& other) const {
  SIMPLE_COMPARE(me, other, boneTransforms_count);
  for(size_t iXform = 0; iXform < boneTransforms_count; ++iXform) {
    const auto& meXform = me.boneTransforms_values[iXform];
    const auto& otherXform = other.boneTransforms_values[iXform];
    SIMPLE_COMPARE(meXform, otherXform, matrix);
  }
  return true;
}


#define LightInfoVars hash, \
                      radiance
using LightInfo = Expected<remixapi_LightInfo>;
void LightInfo::init() {
  sType = REMIXAPI_STRUCT_TYPE_LIGHT_INFO;
  pNext = nullptr;
  populateVals(LightInfoVars);
}
bool LightInfo::compare(const RemixApiT& me, const RemixApiT& other) const {
  return compareVals(me, other, LightInfoVars);
}


template<>
static void populateVal(remixapi_LightInfoLightShaping& val) {
  populateVal(val.direction);
  populateVal(val.coneAngleDegrees);
  populateVal(val.coneSoftness);
  populateVal(val.focusExponent);
}
template<>
static bool compare(const remixapi_LightInfoLightShaping& a, const remixapi_LightInfoLightShaping& b) {
  return compare(a.direction, b.direction)
      && compare(a.coneAngleDegrees, b.coneAngleDegrees)
      && compare(a.coneSoftness, b.coneSoftness)
      && compare(a.focusExponent, b.focusExponent);
}


#define LightInfoSphereVars position, \
                            radius, \
                            shaping_hasvalue, \
                            shaping_value
using LightInfoSphere = Expected<remixapi_LightInfoSphereEXT>;
void LightInfoSphere::init() {
  sType = REMIXAPI_STRUCT_TYPE_LIGHT_INFO_SPHERE_EXT;
  pNext = nullptr;
  populateVals(LightInfoSphereVars);
}
bool LightInfoSphere::compare(const RemixApiT& me, const RemixApiT& other) const {
  return compareVals(me, other, LightInfoSphereVars);
}


#define LightInfoRectVars position, \
                          xAxis, \
                          xSize, \
                          yAxis, \
                          ySize, \
                          direction, \
                          shaping_hasvalue, \
                          shaping_value
using LightInfoRect = Expected<remixapi_LightInfoRectEXT>;
void LightInfoRect::init() {
  sType = REMIXAPI_STRUCT_TYPE_LIGHT_INFO_RECT_EXT;
  pNext = nullptr;
  populateVals(LightInfoRectVars);
}
bool LightInfoRect::compare(const RemixApiT& me, const RemixApiT& other) const {
  return compareVals(me, other, LightInfoRectVars);
}


#define LightInfoDiskVars position, \
                          xAxis, \
                          xRadius, \
                          yAxis, \
                          yRadius, \
                          direction, \
                          shaping_hasvalue, \
                          shaping_value
using LightInfoDisk = Expected<remixapi_LightInfoDiskEXT>;
void LightInfoDisk::init() {
  sType = REMIXAPI_STRUCT_TYPE_LIGHT_INFO_DISK_EXT;
  pNext = nullptr;
  populateVals(LightInfoDiskVars);
}
bool LightInfoDisk::compare(const RemixApiT& me, const RemixApiT& other) const {
  return compareVals(me, other, LightInfoDiskVars);
}


#define LightInfoCylinderVars position, \
                              radius, \
                              axis, \
                              axisLength
using LightInfoCylinder = Expected<remixapi_LightInfoCylinderEXT>;
void LightInfoCylinder::init() {
  sType = REMIXAPI_STRUCT_TYPE_LIGHT_INFO_CYLINDER_EXT;
  pNext = nullptr;
  populateVals(LightInfoCylinderVars);
}
bool LightInfoCylinder::compare(const RemixApiT& me, const RemixApiT& other) const {
  return compareVals(me, other, LightInfoCylinderVars);
}


#define LightInfoDistantVars direction, \
                             angularDiameterDegrees
using LightInfoDistant = Expected<remixapi_LightInfoDistantEXT>;
void LightInfoDistant::init() {
  sType = REMIXAPI_STRUCT_TYPE_LIGHT_INFO_DISTANT_EXT;
  pNext = nullptr;
  populateVals(LightInfoDistantVars);
}
bool LightInfoDistant::compare(const RemixApiT& me, const RemixApiT& other) const {
  return compareVals(me, other, LightInfoDistantVars);
}


#define LightInfoDomeVars transform, \
                          colorTexture
using LightInfoDome = Expected<remixapi_LightInfoDomeEXT>;
void LightInfoDome::init() {
  sType = REMIXAPI_STRUCT_TYPE_LIGHT_INFO_DOME_EXT;
  pNext = nullptr;
  populateVals(LightInfoDomeVars);
}
bool LightInfoDome::compare(const RemixApiT& me, const RemixApiT& other) const {
  return compareVals(me, other, LightInfoDomeVars);
}


// remixapi_LightInfoUSDEXT uses a pattern wherein optional members
// are determined by whether a given pointer is NULL or not, therefore
// we have defined some helpers in an anonymous namespace to operate on
// such members a bit differently. If a non-optional pointer member is
// added to remixapi_LightInfoUSDEXT, then LightInfoUSD::calcSize(...),
// LightInfoUSD::_serialize(...), and LightInfoUSD::_deserialize(...)
// must be redefined explicitly.
#define LightInfoUSDPtrVars pRadius, \
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
using LightInfoUSD = Expected<remixapi_LightInfoUSDEXT>;
template<typename T>
void populateVal__constP(const T* (&pVal)) {
  bool bPopulateVal = false;
  populateVal(bPopulateVal);
  if(bPopulateVal) {
    T* pNewT = new T;
    populateVal(*pNewT);
    pVal = pNewT;
  } else {
    pVal = nullptr;
  }
}
template<typename... Types>
void populateVals__constP(Types&... pVals) {
  (populateVal__constP(pVals), ...);
}
void LightInfoUSD::init() {
  sType = REMIXAPI_STRUCT_TYPE_LIGHT_INFO_USD_EXT;
  pNext = nullptr;
  lightType = REMIXAPI_STRUCT_TYPE_LIGHT_INFO_USD_EXT;
  populateVal(transform);
  populateVals__constP(LightInfoUSDPtrVars);
}
template<typename RemixApiT, typename T>
static inline bool compareVal__optionalPointer(const RemixApiT& me, const RemixApiT& other, const T& val) {
  Comparable<T> comp((uintptr_t)(void*)(&me),
                     (uintptr_t)(void*)(&other),
                     (uintptr_t)(void*)(&val));
  if constexpr (std::is_pointer_v<T>) {
    const bool bMeHasValue = (comp.a != nullptr);
    const bool bOtherHasValue = (comp.b != nullptr);
    if(bMeHasValue == bOtherHasValue) {
      if(bMeHasValue) {
        return expected::compare(*comp.a, *comp.b);
      }
      return true;
    }
  } else {
    return expected::createComparableAndCompare(me, other, val);
  }
  return false;
}
template<typename RemixApiT, typename... Types>
static bool compareVals__optionalPointer(const RemixApiT& me, const RemixApiT& other, Types&... args) {
  return (compareVal__optionalPointer(me, other, args) , ...);
}
bool LightInfoUSD::compare(const RemixApiT& me, const RemixApiT& other) const {
  return compareVals__optionalPointer(me, other, LightInfoUSDPtrVars);
}


MaterialInfo mat;
MaterialInfoOpaque matOpaque;
MaterialInfoOpaqueSubsurface matOpaqueSubSurf;
MaterialInfoTranslucent matTrans;
MaterialInfoPortal matPortal;
MeshInfo mesh;
InstanceInfo inst;
InstanceInfoObjectPicking instObjPick;
InstanceInfoBlend instBlend;
InstanceInfoTransforms instBoneXform;
LightInfo light;
LightInfoSphere lightSphere;
LightInfoRect lightRect;
LightInfoDisk lightDisk;
LightInfoCylinder lightCyl;
LightInfoDistant lightDist;
LightInfoDome lightDome;
LightInfoUSD lightUSD;

}
