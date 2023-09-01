// This is a reimplementation of pxr::HdMeshUtils from OpenUSD's Hydra, to work on untyped data, and to work outside of Hydra

//
// Copyright 2017 Pixar
//
// Licensed under the Apache License, Version 2.0 (the "Apache License")
// with the following modification; you may not use this file except in
// compliance with the Apache License and the following modification to it:
// Section 6. Trademarks. is deleted and replaced with:
//
// 6. Trademarks. This License does not grant permission to use the trade
//    names, trademarks, service marks, or product names of the Licensor
//    and its affiliates, except as required to comply with Section 4(c) of
//    the License and to reproduce the content of the NOTICE file.
//
// You may obtain a copy of the Apache License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the Apache License with the above modification is
// distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied. See the Apache License for the specific
// language governing permissions and limitations under the Apache License.
//

#include "usd_mesh_util.h"

#include "usd_include_begin.h"
#include <pxr/base/gf/vec3i.h>
#include <pxr/base/gf/vec2f.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/gf/vec4f.h>
#include <pxr/base/gf/vec2d.h>
#include <pxr/base/gf/vec3d.h>
#include <pxr/base/gf/vec4d.h>
#include "usd_include_end.h"

using namespace pxr;

namespace lss {

  //-------------------------------------------------------------------------
  // Triangulation

  // Fan triangulation helper function.
  static bool _FanTriangulate(uint8_t* dst, uint8_t const* src, int offset, int index, int size, bool flip, size_t elementSize) {
    // overrun check
    if ((offset + index + 2) >= size) {
      memset(dst, 0, elementSize * 3);
      return false;
    }

    int next[2] = { 2, 1 };
    if (flip)
      std::swap(next[0], next[1]);

    memcpy(dst, &src[offset * elementSize], elementSize);
    dst += elementSize;
    memcpy(dst, &src[(offset + index + next[0]) * elementSize], elementSize);
    dst += elementSize;
    memcpy(dst, &src[(offset + index + next[1]) * elementSize], elementSize);

    return true;
  }

  static bool _FanTriangulate(GfVec3i* dst, int const* src, int offset, int index, int size, bool flip) {
    return _FanTriangulate((uint8_t*) dst->data(), (uint8_t*) src, offset, index, size, flip, sizeof(int));
  }

  void UsdMeshUtil::ComputeTriangleIndices(VtVec3iArray* indices, VtIntArray* primitiveParams, VtIntArray* edgeIndices/*=nullptr*/) const {
    if (indices == nullptr || primitiveParams == nullptr) {
      return;
    }

    // generate triangle index buffer

    int const* numVertsPtr = m_faceVertexCounts.cdata();
    int const* vertsPtr = m_faceVertexIndices.cdata();
    int const* holeIndicesPtr = m_holeIndices.cdata();
    int numFaces = m_faceVertexCounts.size();
    int numVertIndices = m_faceVertexIndices.size();
    int numTris = 0;
    int numholeIndices = m_holeIndices.size();
    bool invalidTopology = false;
    int holeIndex = 0;
    for (int i = 0; i < numFaces; ++i) {
      int nv = numVertsPtr[i] - 2;
      if (nv < 1) {
        // skip degenerated face
        invalidTopology = true;
      } else if (holeIndex < numholeIndices && holeIndicesPtr[holeIndex] == i) {
        // skip hole face
        ++holeIndex;
      } else {
        numTris += nv;
      }
    }
    if (invalidTopology) {
      invalidTopology = false;
    }

    indices->resize(numTris); // vec3 per face
    primitiveParams->resize(numTris); // int per face
    if (edgeIndices) {
      edgeIndices->resize(numTris); // int per face
    }

    const bool flip = (m_orientation != TfToken("rightHanded"));

    // reset holeIndex
    holeIndex = 0;

    // i  -> authored face index [0, numFaces)
    // tv -> triangulated face index [0, numTris)
    // v  -> index to the first vertex (index) for face i
    // ev -> edges visited
    for (int i = 0, tv = 0, v = 0, ev = 0; i < numFaces; ++i) {
      int nv = numVertsPtr[i];
      if (nv < 3) {
        // Skip degenerate faces.
      } else if (holeIndex < numholeIndices && holeIndicesPtr[holeIndex] == i) {
        // Skip hole faces.
        ++holeIndex;
      } else {
        // edgeFlag is used for inner-line removal of non-triangle
        // faces on wireframe shading.
        //
        //          0__                0  0   0__
        //        _/|\ \_            _/.  ..   . \_
        //      _/  | \  \_   ->   _/  .  . .   .  \_
        //     /  A |C \ B \_     /  A .  .C .   . B \_
        //    1-----2---3----4   1-----2  1---2   1----2
        //
        //  Type   EdgeFlag    Draw
        //    -       0        show all edges
        //    A       1        hide [2-0]
        //    B       2        hide [0-1]
        //    C       3        hide [0-1] and [2-0]
        //
        int edgeFlag = 0;
        int edgeIndex = ev;
        for (int j = 0; j < nv - 2; ++j) {
          if (!_FanTriangulate(&(*indices)[tv], vertsPtr, v, j, numVertIndices, flip)) {
            invalidTopology = true;
          }

          if (nv > 3) {
            if (j == 0) {
              if (flip) {
                // If the topology is flipped, we get the triangle
                // 021 instead of 012, and we'd hide edge 0-1
                // instead of 0-2; so we rotate the indices to
                // produce triangle 210.
                GfVec3i& index = (*indices)[tv];
                index.Set(index[1], index[2], index[0]);
              }
              edgeFlag = 1;
            } else if (j == nv - 3) {
              if (flip) {
                // If the topology is flipped, we get the triangle
                // 043 instead of 034, and we'd hide edge 0-4
                // instead of 0-3; so we rotate the indices to
                // produce triangle 304.
                GfVec3i& index = (*indices)[tv];
                index.Set(index[2], index[0], index[1]);
              }
              edgeFlag = 2;
            } else {
              edgeFlag = 3;
            }
            ++edgeIndex;
          }

          (*primitiveParams)[tv] = EncodeCoarseFaceParam(i, edgeFlag);
          if (edgeIndices) {
            (*edgeIndices)[tv] = edgeIndex;
          }

          ++tv;
        }
      }
      // When the face is degenerate and nv > 0, we need to increment the v
      // pointer to walk past the degenerate verts.
      v += nv;
      ev += nv;
    }
  }

  // Face-varying triangulation helper function, to deal with type polymorphism.
  static
    void _TriangulateFaceVarying(
            VtIntArray const& faceVertexCounts,
            VtIntArray const& holeIndices,
            bool flip,
            void const* sourceUntyped,
            int numElements,
            size_t elementSize,
            VtValue* triangulated) {
    // CPU face-varying triangulation
    bool invalidTopology = false;
    int numFVarValues = 0;
    int holeIndex = 0;
    int numholeIndices = holeIndices.size();
    for (int i = 0; i < (int) faceVertexCounts.size(); ++i) {
      int nv = faceVertexCounts[i] - 2;
      if (nv < 1) {
        // skip degenerated face
        invalidTopology = true;
      } else if (holeIndex < numholeIndices && holeIndices[holeIndex] == i) {
        // skip hole face
        ++holeIndex;
      } else {
        numFVarValues += 3 * nv;
      }
    }
    if (invalidTopology) {
      //TF_WARN("degenerated face found [%s]", id.GetText());
      invalidTopology = false;
    }

    VtArray<uint8_t> results(numFVarValues * elementSize);
    // reset holeIndex
    holeIndex = 0;

    int dstIndex = 0;
    for (int i = 0, v = 0; i < (int) faceVertexCounts.size(); ++i) {
      int nVerts = faceVertexCounts[i];

      if (nVerts < 3) {
        // Skip degenerate faces.
      } else if (holeIndex < numholeIndices && holeIndices[holeIndex] == i) {
        // Skip hole faces.
        ++holeIndex;
      } else {
        // triangulate.
        // apply same triangulation as index does
        for (int j = 0; j < nVerts - 2; ++j) {
          if (!_FanTriangulate(&results[dstIndex], (uint8_t*) sourceUntyped, v, j, numElements, flip, elementSize)) {
            invalidTopology = true;
          }
          // To keep edge flags consistent, when a face is triangulated
          // and the topology is flipped we rotate the first and last
          // triangle indices. See ComputeTriangleIndices.
          if (nVerts > 3 && flip) {
            if (j == 0) {
              for (uint32_t o = 0; o < elementSize; o++) {
                std::swap(results[dstIndex + o], results[dstIndex + elementSize + o]);
                std::swap(results[dstIndex + elementSize + o], results[dstIndex + 2 * elementSize + o]);
              }
            } else if (j == nVerts - 3) {
              for (uint32_t o = 0; o < elementSize; o++) {
                std::swap(results[dstIndex + elementSize + o], results[dstIndex + 2 * elementSize + o]);
                std::swap(results[dstIndex + o], results[dstIndex + elementSize + o]);
              }
            }
          }
          dstIndex += 3 * elementSize;
        }
      }
      v += nVerts;
    }
    if (invalidTopology) {
      //TF_WARN("numVerts and verts are incosistent [%s]", id.GetText());
    }

    *triangulated = results;
  }

  bool UsdMeshUtil::ComputeTriangulatedFaceVaryingPrimvar(void const* source,
                                                          int numElements,
                                                          size_t elementSize,
                                                          VtValue* triangulated) const {
    if (m_faceVertexCounts.size() == 0) {
      return false;
    }

    if (triangulated == nullptr) {
      return false;
    }

    const bool flip = (m_orientation != TfToken("rightHanded"));

    _TriangulateFaceVarying(m_faceVertexCounts, m_holeIndices, flip, source, numElements, elementSize, triangulated);

    return true;
  }

}
