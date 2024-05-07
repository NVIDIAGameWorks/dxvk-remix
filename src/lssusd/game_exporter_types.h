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
#pragma once

#include "../dxvk/rtx_render/rtx_hashing.h"
#include <vulkan/vulkan_core.h>

#include "usd_include_begin.h"
#include <pxr/base/gf/matrix4d.h>
#include <pxr/usd/sdf/types.h>
#include <pxr/usd/sdf/path.h>
#include "usd_include_end.h"


#include <stdint.h>
#include <limits>
#include <map>

static_assert(std::numeric_limits<float>::is_iec559);
static_assert(std::numeric_limits<double>::is_iec559);

// While the interface for USD transform matrices implies that a variety of types are accepted, the documentation
// says that this is merely for consistency. You must provide a 4x4 matrix of doubles or you will get an error.

namespace lss {
using Id = size_t;
static constexpr Id kInvalidId(-1);

struct SampledXform {
  double time;
  pxr::GfMatrix4d xform;
};
using SampledXforms = std::vector<SampledXform>;

struct SampledBoneXform {
  double time;
  pxr::VtMatrix4dArray xforms;
};
using SampledBoneXforms = std::vector<SampledBoneXform>;

struct Skeleton {
  pxr::VtArray<pxr::TfToken> jointNames;
  pxr::VtMatrix4dArray bindPose;
  pxr::VtMatrix4dArray restPose;
};

enum CoordSys {
  RHS,
  LHS
};

struct Camera {
  // Note: FoV in radians.
  float         fov = NAN;
  float         aspectRatio = NAN;
  float         nearPlane = NAN;
  float         farPlane = NAN;
  float         firstTime = NAN;
  float         finalTime = NAN;
  bool          isReverseZ = false;
  SampledXforms xforms;
  struct CamMat {
    bool     bInv = false;
    CoordSys coord = RHS;
    inline bool isLHS() const { return coord == LHS; }
  } view, proj;
  // Do XOR here to check if we need to manually change basis for Projection * View matrix
  inline bool isLHS() const { return view.isLHS() ^ proj.isLHS(); }
};

struct SphereLight {
  std::string   lightName;
  float         color[3];
  float         radius;
  float         intensity;
  float         firstTime = NAN;
  float         finalTime = NAN;
  bool          shapingEnabled = false;
  float         coneAngleDegrees = 180.f;
  float         coneSoftness = 0.f;
  float         focusExponent = 0.f;
  SampledXforms xforms;
};

struct DistantLight {
  std::string  lightName;
  float        color[3];
  float        intensity;
  float        angleDegrees;
  pxr::GfVec3f direction;
  float        firstTime = NAN;
  float        finalTime = NAN;
};

struct Material {
  std::string matName;
  std::string albedoTexPath;
  bool        enableOpacity = false;
  struct Sampler {
    VkSamplerAddressMode addrModeU;
    VkSamplerAddressMode addrModeV;
    VkFilter             filter;
    VkClearColorValue    borderColor;
  } sampler;
  // TODO: std::string normalTexPath;
  // TODO: etc...
};

using Index = int;
using Pos = pxr::GfVec3f;
using Norm = pxr::GfVec3f;
using Texcoord = pxr::GfVec2f;
using Color = pxr::GfVec4f;
using BlendWeight = float;
using BlendIdx = int;
template <typename BufferT>
using Buf = pxr::VtArray<BufferT> ;
template<typename BufferT>
using BufSet = std::map<float,Buf<BufferT>>;
struct MeshBuffers {
  BufSet<Index>       idxBufs;
  BufSet<Pos>         positionBufs;
  BufSet<Norm>        normalBufs;
  BufSet<Texcoord>    texcoordBufs;
  BufSet<Color>       colorBufs;
  BufSet<BlendWeight> blendWeightBufs;
  BufSet<BlendIdx>    blendIndicesBufs;
};

struct RenderingMetaData {
  bool alphaTestEnabled;
  uint32_t alphaTestReferenceValue;
  uint32_t alphaTestCompareOp;
  bool alphaBlendEnabled;
  uint32_t srcColorBlendFactor;
  uint32_t dstColorBlendFactor;
  uint32_t colorBlendOp;
  uint32_t textureColorArg1Source;
  uint32_t textureColorArg2Source;
  uint32_t textureColorOperation;
  uint32_t textureAlphaArg1Source;
  uint32_t textureAlphaArg2Source;
  uint32_t textureAlphaOperation;
  uint32_t tFactor;
  bool isTextureFactorBlend;
};

struct Mesh {
  std::string meshName;
  std::unordered_map<const char*, XXH64_hash_t> componentHashes;
  std::unordered_map<const char*, bool> categoryFlags;
  uint32_t     numVertices = 0;
  uint32_t     numIndices = 0;
  bool         isDoubleSided = false;
  Id           matId = kInvalidId;
  MeshBuffers  buffers;
  pxr::GfVec3f origin = pxr::GfVec3f{0.f,0.f,0.f};
  uint32_t     numBones = 0;
  uint32_t     bonesPerVertex = 0;
  pxr::VtMatrix4dArray boneXForms;
  bool         isLhs = false;
};

struct Instance {
  std::string       instanceName;
  float             firstTime = NAN;
  float             finalTime = NAN;
  Id                matId = kInvalidId;
  Id                meshId = kInvalidId;
  SampledXforms     xforms;
  bool              isSky;
  SampledBoneXforms boneXForms;
  RenderingMetaData metadata;
};

template <typename T>
using IdMap = std::unordered_map<Id,T>;
struct Export {
  std::string debugId;
  struct Meta {
    std::string windowTitle;
    std::string exeName;
    std::string iconPath;
    std::string geometryHashRule;
    double metersPerUnit;
    double timeCodesPerSecond;
    double startTimeCode;
    double endTimeCode;
    size_t numFramesCaptured;
    bool bReduceMeshBuffers;
    bool isZUp;
    std::unordered_map<std::string, std::string> renderingSettingsDict;
    bool bCorrectBakedTransforms;
  } meta;
  std::string baseExportPath;
  bool bExportInstanceStage;
  std::string instanceStagePath;
  std::string bakedSkyProbePath;
  pxr::SdfPath omniDefaultCameraSdfPath;
  IdMap<Material> materials;
  IdMap<Mesh> meshes;
  IdMap<Instance> instances;
  Camera camera;
  IdMap<SphereLight> sphereLights;
  IdMap<DistantLight> distantLights;
  pxr::GfVec3f stageOrigin = pxr::GfVec3f{0.f,0.f,0.f};
  pxr::GfMatrix4d globalXform = pxr::GfMatrix4d{1.0};
};

}