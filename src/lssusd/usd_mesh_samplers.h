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

#include "hd/usd_mesh_util.h"
#include "../util/util_error.h"

#include <bitset>
#include <cstddef>

#include "usd_include_begin.h"
#include <pxr/pxr.h>
#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/gf/matrix4f.h>
#include <pxr/base/gf/vec2d.h>
#include <pxr/base/gf/vec2f.h>
#include <pxr/base/gf/vec2i.h>
#include <pxr/base/gf/vec3d.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/gf/vec3i.h>
#include <pxr/base/gf/vec4d.h>
#include <pxr/base/gf/vec4f.h>
#include <pxr/base/gf/vec4i.h>
#include "usd_include_end.h"

namespace lss {

  class BufferSampler {
  public:
    BufferSampler(pxr::VtValue const& buffer)
      : m_numElements(buffer.GetArraySize())
      , m_buffer(buffer.UncheckedGet<pxr::VtArray<uint8_t>>()) { }

    bool Sample(int index, void* value, size_t size) const {
      if (m_numElements <= (size_t) index) {
        return false;
      }

      size_t offset = size * index;
      memcpy((void*) value, &m_buffer[0] + offset, size);

      return true;
    }

  private:
    pxr::VtArray<uint8_t> const m_buffer;
    int m_numElements;
  };


  class GeomPrimvarSampler {
  public:
    GeomPrimvarSampler() = default;
    virtual ~GeomPrimvarSampler() = default;

    virtual bool SampleBuffer(int index, void* value) const = 0;
  };


  class ConstantSampler : public GeomPrimvarSampler {
  public:
    ConstantSampler(pxr::VtValue const& value, size_t elementSize)
      : m_sampler(value)
      , m_elementSize(elementSize) { }

    bool SampleBuffer(int index, void* value) const override {
      return m_sampler.Sample(0, value, m_elementSize);
    }
  private:
    BufferSampler const m_sampler;
    size_t m_elementSize;
  };


  class UniformSampler : public GeomPrimvarSampler {
  public:
    UniformSampler(pxr::VtValue const& value, pxr::VtIntArray const& primitiveParams, size_t elementSize)
      : m_sampler(value)
      , m_primitiveParams(primitiveParams)
      , m_elementSize(elementSize) { }

    UniformSampler(pxr::VtValue const& value, size_t elementSize)
      : m_sampler(value)
      , m_elementSize(elementSize) { }

    bool SampleBuffer(int index, void* value) const {
      if (m_primitiveParams.empty()) {
        return m_sampler.Sample(index, value, m_elementSize);
      }
      if (index >= m_primitiveParams.size()) {
        return false;
      }
      return m_sampler.Sample(UsdMeshUtil::DecodeFaceIndexFromCoarseFaceParam(m_primitiveParams[index]), value, m_elementSize);
    }

  private:
    BufferSampler const m_sampler;
    pxr::VtIntArray const m_primitiveParams;
    size_t m_elementSize;
  };


  class TriangleVertexSampler : public GeomPrimvarSampler {
  public:
    TriangleVertexSampler(pxr::VtValue const& value, pxr::VtVec3iArray const& indices, size_t elementSize)
      : m_sampler(value)
      , m_indices(indices)
      , m_elementSize(elementSize) { }

    bool SampleBuffer(int index, void* value) const {
      return m_sampler.Sample(m_indices[index / 3][index % 3], value, m_elementSize);
    }

  private:
    BufferSampler const m_sampler;
    pxr::VtVec3iArray const m_indices;
    size_t m_elementSize;
  };


  class TriangleFaceVaryingSampler : public GeomPrimvarSampler {
  public:
    TriangleFaceVaryingSampler(pxr::VtValue const& value, UsdMeshUtil& meshUtil, size_t elementSize)
      : m_sampler(triangulate(value, meshUtil, elementSize))
      , m_elementSize(elementSize) { }

    bool SampleBuffer(int index, void* value) const {
      return m_sampler.Sample(index, value, m_elementSize);
    }

  private:
    BufferSampler const m_sampler;
    size_t m_elementSize;
    static pxr::VtValue triangulate(pxr::VtValue const& value, UsdMeshUtil& meshUtil, size_t elementSize) {
      const pxr::VtArray<int>& buffer = value.UncheckedGet<pxr::VtArray<int>>();
      pxr::VtValue triangulated;
      if (!meshUtil.ComputeTriangulatedFaceVaryingPrimvar(buffer.cdata(), buffer.size(), elementSize, &triangulated)) {
        throw dxvk::DxvkError("Could not triangulate face-varying data primvar");
      }
      return triangulated;
    }

  };
}
