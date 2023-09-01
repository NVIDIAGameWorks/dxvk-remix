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
#pragma once

#include "usd_include_begin.h"
#include <pxr/pxr.h>
#include <pxr/base/gf/vec2i.h>
#include <pxr/base/gf/vec3i.h>
#include <pxr/base/gf/vec4i.h>
#include <pxr/base/vt/array.h>
#include <pxr/base/vt/value.h>
#include <pxr/usd/sdf/types.h>
#include "usd_include_end.h"

namespace lss {

  class UsdMeshUtil {
  public:
    UsdMeshUtil(const pxr::TfToken& orientation,
                const pxr::VtIntArray& faceVertexCounts,
                const pxr::VtIntArray& faceVertexIndices,
                const pxr::VtIntArray& holeIndices)
      : m_orientation(orientation)
      , m_faceVertexCounts(faceVertexCounts)
      , m_faceVertexIndices(faceVertexIndices)
      , m_holeIndices(holeIndices) { }
    virtual ~UsdMeshUtil() { }

    // --------------------------------------------------------------------
    /// \name Triangulation
    ///
    /// Produces a mesh where each non-triangle face in the base mesh topology
    /// is fan-triangulated such that the resulting mesh consists entirely
    /// of triangles.
    ///
    /// In order to access per-face signals (face color, face selection etc)
    /// we need a mapping from primitiveID to authored face index domain.
    /// This is encoded in primitiveParams, and computed along with indices.
    /// See \ref PrimitiveParamEncoding.
    /// @{
    /*
                 +--------+-------+
                /| \      |\      |\
               / |  \  1  | \  2  | \
              /  |   \    |  \    |  \
             /   |    \   |   \   | 2 +
            / 0  |  1  \  | 2  \  |  /
           /     |      \ |     \ | /
          /      |       \|      \|/
         +-------+--------+-------+
    */

    /// Return a triangulation of the input topology.  indices and
    /// primitiveParams are output parameters.
    void ComputeTriangleIndices(pxr::VtVec3iArray* indices,
                                pxr::VtIntArray* primitiveParams,
                                pxr::VtIntArray* edgeIndices = nullptr) const;

    /// Return a triangulation of a face-varying primvar. source is
    /// a buffer of size numElements and type corresponding to dataType
    /// (e.g. HdTypeFloatVec3); the result is a VtArray<T> of the
    /// correct type written to the variable "triangulated".
    /// This function returns false if it can't resolve dataType.
    bool ComputeTriangulatedFaceVaryingPrimvar(void const* source,
                                               int numElements,
                                               size_t elementSize,
                                               pxr::VtValue* triangulated) const;

    // --------------------------------------------------------------------
    /// \anchor PrimitiveParamEncoding
    /// \name Primitive Param bit encoding
    ///
    /// This encoding provides information about each sub-face resulting
    /// from the triangulation of a base topology face.
    ///
    /// The encoded faceIndex is the index of the base topology face
    /// corresponding to a triangulated sub-face.
    ///
    /// The encoded edge flag identifies where a sub-face occurs in the
    /// sequence of sub-faces produced for each base topology face.
    /// This edge flag can be used to determine which edges of a sub-face
    /// correspond to edges of a base topology face and which are internal
    /// edges that were introduced by triangulation:
    /// - 0 unaffected triangle or quad base topology face
    /// - 1 first sub-face produced by triangulation
    /// - 2 last sub-face produced by triangulation
    /// - 3 intermediate sub-face produced by triangulation
    /// @{

    // Per-primitive coarse-face-param encoding/decoding functions
    static int EncodeCoarseFaceParam(int faceIndex, int edgeFlag) {
      return ((faceIndex << 2) | (edgeFlag & 3));
    }
    static int DecodeFaceIndexFromCoarseFaceParam(int coarseFaceParam) {
      return (coarseFaceParam >> 2);
    }
    static int DecodeEdgeFlagFromCoarseFaceParam(int coarseFaceParam) {
      return (coarseFaceParam & 3);
    }

  private:

    const pxr::TfToken& m_orientation;
    const pxr::VtIntArray& m_faceVertexCounts;
    const pxr::VtIntArray& m_faceVertexIndices;
    const pxr::VtIntArray& m_holeIndices;
  };
}
