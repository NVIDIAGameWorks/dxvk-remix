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
#include <pxr/pxr.h>
#include <pxr/usd/usdGeom/mesh.h>
#include <pxr/usd/usdGeom/subset.h>
#include "usd_include_end.h"

#include "../util/util_bounding_box.h"
#include "../util/util_vector.h"

namespace lss {
  class UsdMeshUtil;
  class GeomPrimvarSampler;

  class UsdMeshImporter {
  public:
    UsdMeshImporter(const pxr::UsdPrim& meshPrim, const uint32_t limitedNumBonesPerVertex);

    enum Attributes : uint32_t {
      VertexPositions = 0,
      Normals,
      Texcoords,
      Colors,
      Opacity,
      BlendWeights,
      BlendIndices,

      Count
    };

    struct SubMesh {
      SubMesh(const std::vector<uint32_t>&& ib, const pxr::UsdPrim& _prim)
        : indexBuffer(std::move(ib))
        , prim(_prim) { }

      std::vector<uint32_t> indexBuffer;
      pxr::UsdPrim prim;

      size_t GetNumIndices() const {
        return indexBuffer.size();
      }
    };

    struct VertexDeclaration {
      Attributes attribute;
      size_t offset;
      size_t size;
    };

    const std::vector<SubMesh>& GetSubMeshes() const {
      return m_meshes;
    }

    const std::vector<VertexDeclaration>& GetVertexDecl() const {
      return m_vertexDecl;
    }

    const std::vector<float>& GetVertexData() const {
      return m_vertexData;
    }

    uint32_t GetNumVertices() const {
      return m_numVertices;
    }

    size_t GetVertexStride() const {
      return m_vertexStride;
    }

    size_t GetNumBonesPerVertex() const {
      return m_limitedNumBonesPerVertex;
    }

    bool IsRightHanded() const {
      return m_isRightHanded;
    }

    enum DoubleSidedState {
      Inherit = 0,
      IsSingleSided,
      IsDoubleSided
    };

    DoubleSidedState GetDoubleSidedState() const {
      return m_doubleSided;
    }

    const dxvk::AxisAlignedBoundingBox& GetBoundingBox() const {
      return m_boundingBox;
    }

  private:
    inline static const uint32_t MaxSupportedNumBones = 256;

    struct IndexRange {
      uint32_t start = 0, end = 0;
    };

    using FaceToTriangleMap = std::vector<IndexRange>;

    void triangulate(const uint32_t numTriangles, 
                     const uint32_t elementStride,
                     const std::unique_ptr<GeomPrimvarSampler>* ppMeshSamplers,
                     const pxr::VtIntArray& trianglePrimitiveParams,
                     std::vector<uint32_t>& indicesOut,
                     FaceToTriangleMap& triangleMapOut);

    static const std::vector<uint32_t> generateSubsetIndices(const pxr::UsdGeomSubset& subset, const std::vector<uint32_t>& indices, const FaceToTriangleMap& triangleMap);
    void generateTriangleSamplers(UsdMeshUtil& meshUtil, const pxr::VtVec3iArray& usdIndices, const pxr::VtIntArray& trianglePrimitiveParams, std::unique_ptr<GeomPrimvarSampler>* ppMeshSamplers);
    uint32_t generateVertexDeclaration(std::unique_ptr<GeomPrimvarSampler>* ppMeshSamplers);

    // This class does not support copying.
    UsdMeshImporter(const UsdMeshImporter&) = delete;
    UsdMeshImporter& operator =(const UsdMeshImporter&) = delete;

    std::vector<SubMesh> m_meshes;
    std::vector<float> m_vertexData;
    std::vector<VertexDeclaration> m_vertexDecl;

    uint32_t m_vertexStride = 0;
    uint32_t m_numVertices = 0;
    uint32_t m_actualNumBonesPerVertex = 0;
    uint32_t m_limitedNumBonesPerVertex = 0;

    DoubleSidedState m_doubleSided = Inherit;
    bool m_isRightHanded = true; // By default USD is right handed

    dxvk::AxisAlignedBoundingBox m_boundingBox;

    const pxr::UsdGeomMesh& m_meshPrim;
  };
}
