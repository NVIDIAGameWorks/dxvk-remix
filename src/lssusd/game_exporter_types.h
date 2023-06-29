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

#include "usd_include_begin.h"
#include <pxr/base/gf/matrix4d.h>
#include <pxr/usd/sdf/types.h>
#include <pxr/usd/sdf/path.h>
#include "usd_include_end.h"

#include <stdint.h>
#include <limits>
#include <map>
#include "../dxvk/rtx_render/rtx_hashing.h"

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
  
struct Camera {
  // Note: FoV in radians.
  float         fov = NAN;
  float         aspectRatio = NAN;
  float         nearPlane = NAN;
  float         farPlane = NAN;
  float         firstTime = NAN;
  float         finalTime = NAN;
  bool          isLHS = false;
  bool          isReverseZ = false;
  bool          bFlipVertAperture = false; // WAR until able to expect flipped meshes
  SampledXforms xforms;
};

struct SphereLight {
  std::string   lightName;
  float         color[3];
  float         radius;
  float         intensity;
  float         firstTime = NAN;
  float         finalTime = NAN;
  bool          shapingEnabled = false;
  float         coneAngle = 180.f;
  float         coneSoftness = 0.f;
  float         focusExponent = 0.f;
  SampledXforms xforms;
};

struct DistantLight {
  std::string  lightName;
  float        color[3];
  float        intensity;
  float        angle;
  pxr::GfVec3f direction;
  float        firstTime = NAN;
  float        finalTime = NAN;
};

struct Material {
  std::string matName;
  std::string albedoTexPath;
  bool        enableOpacity = false;
  // TODO: std::string normalTexPath;
  // TODO: etc...
};

using IndexBuffer = pxr::VtArray<int>;
using PositionBuffer = pxr::VtArray<pxr::GfVec3f>;
using NormalBuffer = pxr::VtArray<pxr::GfVec3f>;
using TexcoordBuffer = pxr::VtArray<pxr::GfVec2f>;
using ColorBuffer = pxr::VtArray<pxr::GfVec3f>;
using BlendWeightBuffer = pxr::VtArray<float>;
using BlendIndicesBuffer = pxr::VtArray<int>;
struct MeshBuffers {
  std::map<float,IndexBuffer> idxBufs;
  std::map<float,PositionBuffer> positionBufs;
  std::map<float,NormalBuffer> normalBufs;
  std::map<float,TexcoordBuffer> texcoordBufs;
  std::map<float,ColorBuffer> colorBufs;
  std::map<float,BlendWeightBuffer> blendWeightBufs;
  std::map<float,BlendIndicesBuffer> blendIndicesBufs;
};

struct Mesh {
  std::string meshName;
  std::unordered_map<const char*, XXH64_hash_t> componentHashes;
  uint32_t    numVertices = 0;
  uint32_t    numIndices = 0;
  bool        isDoubleSided = false;
  Id          matId = kInvalidId;
  MeshBuffers buffers;
  uint32_t    numBones = 0;
  uint32_t    bonesPerVertex = 0;
  pxr::VtMatrix4dArray boneXForms;
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
};

template <typename T>
using IdMap = std::unordered_map<Id,T>;
struct Export {
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
    bool bUseLssUsdPlugins;
    bool bReduceMeshBuffers;
    bool isZUp;
    bool isLHS;
  } meta;
  std::string debugId;
  std::string baseExportPath;
  bool bExportInstanceStage;
  std::string instanceExportName;
  std::string bakedSkyProbePath;
  pxr::SdfPath omniDefaultCameraSdfPath;
  IdMap<Material> materials;
  IdMap<Mesh> meshes;
  IdMap<Instance> instances;
  Camera camera;
  IdMap<SphereLight> sphereLights;
  IdMap<DistantLight> distantLights;
};

}