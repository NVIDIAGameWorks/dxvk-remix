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

#include "../util/util_math.h"
#include "../util/util_vector.h"
#include "../util/util_error.h"
#include "../util/util_string.h"
#include "../util/util_fast_cache.h"
#include "../util/log/log.h"
#include "../tracy/Tracy.hpp"
#include "hd/usd_mesh_util.h"
#include "usd_mesh_samplers.h"
#include "usd_mesh_importer.h"

#include "usd_include_begin.h"
#include <pxr/pxr.h>
#include <pxr/usd/sdf/types.h>
#include <pxr/usd/usd/attribute.h>
#include <pxr/usd/usd/modelAPI.h>
#include <pxr/usd/usd/tokens.h>
#include <pxr/usd/usdGeom/camera.h>
#include <pxr/usd/usdGeom/mesh.h>
#include <pxr/usd/usdGeom/subset.h>
#include <pxr/usd/usdGeom/tokens.h>
#include <pxr/usd/usdGeom/primvar.h>
#include <pxr/usd/usdGeom/primvarsAPI.h> 
#include <pxr/usd/usdSkel/bindingAPI.h>
#include "usd_include_end.h"
#include <vector>
#include <d3d9types.h>

using namespace pxr;
using namespace dxvk;

namespace lss {
  inline static const TfToken kFaceVertexCounts("faceVertexCounts");
  inline static const TfToken kFaceVertexIndices("faceVertexIndices");
  inline static const TfToken kHoleIndices("holeIndices");
  inline static const TfToken kNormalsPrimvar("primvars:normals");
  inline static const TfToken kNormalsAttribute("normals");
  inline static const TfToken kColorAttribute("displayColor");
  inline static const TfToken kPoints("points");
  inline static const TfToken kUvs[] = {
    TfToken("primvars:st"),
    TfToken("primvars:uv"),
    TfToken("primvars:st0"),
    TfToken("primvars:st1"),
    TfToken("primvars:st2")
  };
  inline static const TfToken kDoubleSided("doubleSided");
  inline static const TfToken kOrientation("orientation");
  inline static const TfToken kRightHanded("rightHanded");


  size_t sizeOfUsdType(const std::type_info& type) {
    if (type == typeid(GfVec4f))
      return sizeof(GfVec4f);
    if (type == typeid(GfVec3f))
      return sizeof(GfVec3f);
    if (type == typeid(GfVec2f))
      return sizeof(GfVec2f);
    if (type == typeid(int))
      return sizeof(int);
    if (type == typeid(float))
      return sizeof(float);
    return 0;
  }


  const std::vector<uint32_t> UsdMeshImporter::generateSubsetIndices(const UsdGeomSubset& subset, const std::vector<uint32_t>& indices, const UsdMeshImporter::FaceToTriangleMap& triangleMap) {
    ZoneScoped;
    std::vector<uint32_t> subsetIndices;

    VtIntArray faceIndices;
    subset.GetIndicesAttr().Get(&faceIndices);
    for (const int& faceIdx : faceIndices) {
      for (uint32_t i = triangleMap[faceIdx].start; i < triangleMap[faceIdx].end; i++) {
        subsetIndices.emplace_back(indices[i]);
      }
    }

    return subsetIndices;
  }


  UsdMeshImporter::UsdMeshImporter(const UsdPrim& meshPrim)
    : m_meshPrim(UsdGeomMesh(meshPrim)) {
    ZoneScoped;
    if (!meshPrim.IsA<UsdGeomMesh>()) {
      throw DxvkError(str::format("Tried to process mesh, but it doesnt appear to be a valid USD mesh, id=.", m_meshPrim.GetPath().GetString()));
    }

    if (!m_meshPrim.GetPointsAttr().HasValue()) {
      throw DxvkError(str::format("Tried to process mesh with no vertex positions, id=.", m_meshPrim.GetPath().GetString()));
    }

    TfToken orientation;
    VtIntArray faceIndices, faceCounts, holeIndices;
    const bool orientationAuthored = m_meshPrim.GetOrientationAttr().Get(&orientation);
    m_meshPrim.GetFaceVertexIndicesAttr().Get(&faceIndices);
    m_meshPrim.GetFaceVertexCountsAttr().Get(&faceCounts);
    m_meshPrim.GetHoleIndicesAttr().Get(&holeIndices);

    UsdMeshUtil meshUtil(orientation, faceCounts, faceIndices, holeIndices);
    VtVec3iArray triangleIndices;
    VtIntArray trianglePrimitiveParams;
    meshUtil.ComputeTriangleIndices(&triangleIndices, &trianglePrimitiveParams);

    const uint32_t numTriangles = triangleIndices.size();

    if (numTriangles == 0) {
      throw DxvkError(str::format("Tried to process mesh with no triangles, id=.", m_meshPrim.GetPath().GetString()));
    }

    std::unique_ptr<GeomPrimvarSampler> pMeshSamplers[Attributes::Count] = { 0 };
    generateTriangleSamplers(meshUtil, triangleIndices, trianglePrimitiveParams, pMeshSamplers);

    if (pMeshSamplers[Attributes::VertexPositions] == nullptr) {
      throw DxvkError(str::format("Tried to process mesh with no vertex positions, id=.", m_meshPrim.GetPath().GetString()));
    }

    std::vector<UsdGeomSubset> geomSubsets;
    auto children = meshPrim.GetFilteredChildren(UsdPrimIsActive);
    for (auto child : children) {
      if (child.IsA<UsdGeomSubset>()) {
        geomSubsets.push_back(UsdGeomSubset(child));
      }
    }

    m_vertexStride = generateVertexDeclaration(pMeshSamplers);

    std::vector<uint32_t> indices;
    FaceToTriangleMap faceToTriangles(geomSubsets.size() > 0 ? faceCounts.size() : 0);
    triangulate(numTriangles, m_vertexStride / sizeof(float), pMeshSamplers, trianglePrimitiveParams, indices, faceToTriangles);

    m_numVertices = m_vertexData.size() * sizeof(float) / m_vertexStride;

    if (geomSubsets.empty()) {
      m_meshes.emplace_back(std::move(indices), meshPrim);
    } else {
      for (const UsdGeomSubset& subset : geomSubsets) {
        m_meshes.emplace_back(std::move(generateSubsetIndices(subset, indices, faceToTriangles)), subset.GetPrim());
      }
    }

    UsdAttribute doubleSidedAttribute;
    if ((doubleSidedAttribute = meshPrim.GetAttribute(kDoubleSided)).HasAuthoredValue()) {
      bool doubleSided = true;
      doubleSidedAttribute.Get(&doubleSided);
      m_doubleSided = doubleSided ? IsDoubleSided : IsSingleSided;
    }

    m_isRightHanded = orientationAuthored && orientation == kRightHanded;
  }


  uint32_t UsdMeshImporter::generateVertexDeclaration(std::unique_ptr<GeomPrimvarSampler>* ppMeshSamplers) {
    size_t offset = 0;
    const size_t size = sizeof(float) * 3;
    m_vertexDecl.emplace_back(VertexDeclaration { Attributes::VertexPositions, offset, size });
    offset += size;
    if (ppMeshSamplers[Attributes::Normals]) {
      const size_t size = sizeof(float) * 3;
      m_vertexDecl.emplace_back(VertexDeclaration { Attributes::Normals, offset, size });
      offset += size;
    }
    if (ppMeshSamplers[Attributes::Texcoords]) {
      const size_t size = sizeof(float) * 2;
      m_vertexDecl.emplace_back(VertexDeclaration { Attributes::Texcoords, offset, size });
      offset += size;
    }
    if (ppMeshSamplers[Attributes::Colors]) {
      const size_t size = sizeof(uint32_t);
      m_vertexDecl.emplace_back(VertexDeclaration { Attributes::Colors, offset, size });
      offset += size;
    }
    if (ppMeshSamplers[Attributes::BlendWeights]) {
      const size_t size = sizeof(float) * (m_numBonesPerVertex - 1);
      m_vertexDecl.emplace_back(VertexDeclaration { Attributes::BlendWeights, offset, size });
      offset += size;
    }
    if (ppMeshSamplers[Attributes::BlendIndices]) {
      const size_t size = dxvk::align(m_numBonesPerVertex, 4);
      m_vertexDecl.emplace_back(VertexDeclaration { Attributes::BlendIndices, offset, size });
      offset += size;
    }

    return offset;
  }


  void UsdMeshImporter::generateTriangleSamplers(UsdMeshUtil& meshUtil, const VtVec3iArray& usdIndices, const VtIntArray& trianglePrimitiveParams, std::unique_ptr<GeomPrimvarSampler>* ppMeshSamplers) {
    ZoneScoped;
    struct PrimvarDescriptor {
      UsdGeomPrimvar primvar;
      Attributes vertexAttribute;
      size_t expectedSize = 0;
    };
    std::vector<PrimvarDescriptor> primvars;

    auto& addPrimvarFromAttribute = [&](const UsdAttribute& attribute, Attributes vertexAttribute, size_t expectedSize) -> bool {
      if (!attribute.HasValue())
        return false;
      const UsdGeomPrimvar primvar(attribute);
      primvars.emplace_back(PrimvarDescriptor { primvar, vertexAttribute, expectedSize });
      return true;
    };

    auto& addPrimvarFromAttributeName = [&](const TfToken& name, Attributes vertexAttribute, size_t expectedSize) -> bool {
      if (!m_meshPrim.GetPrim().HasAttribute(name))
        return false;
      const UsdAttribute& attribute = m_meshPrim.GetPrim().GetAttribute(name);
      return addPrimvarFromAttribute(attribute, vertexAttribute, expectedSize);
    };

    if (!addPrimvarFromAttribute(m_meshPrim.GetPointsAttr(), Attributes::VertexPositions, sizeof(float) * 3))
      if (!addPrimvarFromAttributeName(kPoints, Attributes::VertexPositions, sizeof(float) * 3))
        throw DxvkError(str::format("Prim: ", m_meshPrim.GetPath().GetString(), ", has no points attribute."));

    if (!addPrimvarFromAttribute(m_meshPrim.GetNormalsAttr(), Attributes::Normals, sizeof(float) * 3))
      addPrimvarFromAttributeName(kNormalsAttribute, Attributes::Normals, sizeof(float) * 3);

    if (!addPrimvarFromAttribute(m_meshPrim.GetDisplayColorAttr(), Attributes::Colors, 0))
      addPrimvarFromAttributeName(kColorAttribute, Attributes::Normals, 0);

    for (const TfToken& uvName : kUvs) {
      if (addPrimvarFromAttributeName(uvName, Attributes::Texcoords, sizeof(float) * 2)) {
        break;
      }
    }

    if (m_meshPrim.GetPrim().HasAPI<UsdSkelBindingAPI>()) {
      UsdSkelBindingAPI skelBinding(m_meshPrim.GetPrim());
      UsdGeomPrimvar jointIndicesPV = skelBinding.GetJointIndicesPrimvar();
      UsdGeomPrimvar jointWeightsPV = skelBinding.GetJointWeightsPrimvar();
      
      m_numBonesPerVertex = jointIndicesPV.GetElementSize();
      if (m_numBonesPerVertex > MaxSupportedNumBones) {
        Logger::warn(str::format("Prim: ", m_meshPrim.GetPath().GetString(), ", uses more bones than is currently supported."));
        m_numBonesPerVertex = MaxSupportedNumBones;
      }

      if (!jointWeightsPV.HasValue()) {
        throw DxvkError(str::format("Prim: ", m_meshPrim.GetPath().GetString(), ", has Skeleton API but no joint weights."));
      }
      if (jointWeightsPV.GetElementSize() != m_numBonesPerVertex) {
        throw DxvkError(str::format("Prim: ", m_meshPrim.GetPath().GetString(), ", joint indices and joint weights must have matching element sizes."));
      }

      primvars.emplace_back(PrimvarDescriptor { jointIndicesPV, Attributes::BlendIndices, sizeof(int)   * m_numBonesPerVertex });
      primvars.emplace_back(PrimvarDescriptor { jointWeightsPV, Attributes::BlendWeights, sizeof(float) * m_numBonesPerVertex });
    }

    uint32_t numPoints = 0;
    for (PrimvarDescriptor const& desc : primvars) {
      const UsdGeomPrimvar& pv = desc.primvar;

      VtValue data;
      pv.ComputeFlattened(&data);

      const size_t elementSize = sizeOfUsdType(data.GetElementTypeid()) * pv.GetElementSize();
      if (elementSize == 0) {
        Logger::warn(str::format("Skipping unknown USD type, ", desc.vertexAttribute, ", for primvar, id=", pv.GetName()));
        continue;
      }

      if (desc.expectedSize != 0 && elementSize != desc.expectedSize) {
        Logger::warn(str::format("Skipping unexpected USD type for attribute, ", desc.vertexAttribute, ", primvar, id=", pv.GetName()));
        continue;
      }

      GeomPrimvarSampler* sampler = nullptr;
      if (desc.vertexAttribute == VertexPositions) {
        numPoints = data.GetArraySize();
        sampler = new TriangleVertexSampler(data, usdIndices, elementSize);
      } else {
        TfToken interpolation = pv.GetInterpolation();
        if (interpolation == UsdGeomTokens->constant) {
          sampler = new ConstantSampler(data, elementSize);
        } else if (interpolation == UsdGeomTokens->uniform) {
          sampler = new UniformSampler(data, trianglePrimitiveParams, elementSize);
        } else if (interpolation == UsdGeomTokens->vertex || interpolation == UsdGeomTokens->varying) {
          const uint32_t expectedArraySize = numPoints * pv.GetElementSize(); 
          if (data.GetArraySize() == expectedArraySize) {
            sampler = new TriangleVertexSampler(data, usdIndices, elementSize);
          } else {
            Logger::warn(str::format("Unexpected number of elements found for vertex attribute, ", desc.vertexAttribute, ", for primvar, id=", pv.GetName()));
          }
        } else if (interpolation == UsdGeomTokens->faceVarying) {
          sampler = new TriangleFaceVaryingSampler(data, meshUtil, elementSize);
        } else {
          throw DxvkError(str::format("Unexpected interpolation mode for primvar, id=", pv.GetName()));
        }
      }

      ppMeshSamplers[desc.vertexAttribute].reset(sampler);
    }
  }

  void UsdMeshImporter::triangulate(const uint32_t numTriangles, 
                                    const uint32_t elementStride,
                                    const std::unique_ptr<GeomPrimvarSampler>* ppMeshSamplers,
                                    const VtIntArray& trianglePrimitiveParams,
                                    std::vector<uint32_t>& indicesOut,
                                    FaceToTriangleMap& triangleMapOut) {
    ZoneScoped;
    const uint32_t numIndices = numTriangles * 3;
    const uint32_t numVertexElements = numIndices * elementStride;
    indicesOut.resize(numIndices);
    m_vertexData.resize(numVertexElements);

    fast_unordered_cache<uint32_t> uniqueVertexToIndex;

    // Temp stack storage for blend weights/indices
    uint32_t blendIndicesStorage[MaxSupportedNumBones];

    IndexRange currentFaceMapRange;
    uint32_t uniqueVertexIndex = 0;
    uint32_t prevFaceIdx = 0;
    uint32_t totalOffset = 0;
    for (uint32_t triIdx = 0; triIdx < numTriangles; triIdx++) {
      for (uint32_t vertIdx = 0; vertIdx < 3; vertIdx++) {
        const uint32_t idx = triIdx * 3 + vertIdx;

        uint32_t vertexOffset = totalOffset;

        // Sample the vertex attributes to get all the data for this vertex.
        for (const VertexDeclaration& decl : m_vertexDecl) {
          switch (decl.attribute) {
          case Attributes::BlendIndices: {
            ppMeshSamplers[Attributes::BlendIndices]->SampleBuffer(idx, &blendIndicesStorage[0]);
            // Encode bone indices into compressed byte form
            for (int j = 0; j < m_numBonesPerVertex; j += 4) {
              uint32_t vertIndices = 0;
              for (int k = 0; k < 4 && j + k < m_numBonesPerVertex; ++k) {
                vertIndices |= blendIndicesStorage[j + k] << 8 * k;
              }
              *(uint32_t*) &m_vertexData[vertexOffset + j/4] = vertIndices;
            }
            break;
          }
          case Attributes::Colors: {
            GfVec4f color(1.0f); // default to white, some colors in USD will be 3 channel, some 4.
            ppMeshSamplers[Attributes::Colors]->SampleBuffer(idx, &color);
            *(uint32_t*) &m_vertexData[vertexOffset] = D3DCOLOR_COLORVALUE(color[0], color[1], color[2], color[3]);
            break;
          }
          case Attributes::Texcoords: {
            ppMeshSamplers[decl.attribute]->SampleBuffer(idx, &m_vertexData[vertexOffset]);
            // Invert texcoord.y for Remix
            m_vertexData[vertexOffset + 1] = 1.f - m_vertexData[vertexOffset + 1];
            break;
          }
          default: {
            ppMeshSamplers[decl.attribute]->SampleBuffer(idx, &m_vertexData[vertexOffset]);
            break;
          }
          }
          vertexOffset += decl.size / 4;
        }

        // If we've indexed this vertex before, no need to waste memory
        const XXH64_hash_t vHash = XXH3_64bits(&m_vertexData[totalOffset], m_vertexStride);

        const auto& existingVertex = uniqueVertexToIndex.find(vHash);
        if (existingVertex == uniqueVertexToIndex.end()) {
          std::memcpy(&m_vertexData[uniqueVertexIndex * elementStride], &m_vertexData[totalOffset], m_vertexStride);
          uniqueVertexToIndex[vHash] = indicesOut[idx] = uniqueVertexIndex++;
          // if this was unique, then offset the vertex data write pointer, if not, overwrite the last vertex entry
          totalOffset += elementStride;
        } else {
#ifndef NDEBUG
          // Check for hash collisions
          assert(memcmp(&m_vertexData[existingVertex->second * elementStride], &m_vertexData[totalOffset], m_vertexStride) == 0);
#endif
          indicesOut[idx] = existingVertex->second;
        }
      }

      // Build the face to index mapping for geom subsets
      if (triangleMapOut.size() > 0) {
        const uint32_t faceIdx = UsdMeshUtil::DecodeFaceIndexFromCoarseFaceParam(trianglePrimitiveParams[triIdx]);
        if (faceIdx != prevFaceIdx) {
          currentFaceMapRange.end = triIdx * 3;
          triangleMapOut[prevFaceIdx] = currentFaceMapRange;
          currentFaceMapRange.start = currentFaceMapRange.end; // restart the count
          prevFaceIdx = faceIdx;
        }
      }
    }

    if (triangleMapOut.size() > 0 && prevFaceIdx != 0xFFFFFFFF) {
      // Add the last face to mapping
      currentFaceMapRange.end = numTriangles * 3;
      triangleMapOut[prevFaceIdx] = currentFaceMapRange;
    }

    m_vertexData.resize(uniqueVertexIndex * elementStride);
  }
}
