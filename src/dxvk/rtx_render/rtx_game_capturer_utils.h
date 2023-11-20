/*
* Copyright (c) 2023, NVIDIA CORPORATION. All rights reserved.
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

#include "../../lssusd/usd_include_begin.h"
#include <pxr/base/gf/vec3f.h>
#include "../../lssusd/usd_include_end.h"

#include <atomic>

namespace dxvk {

struct AtomicOriginCalc; // Forward declare
struct OriginCalc {
  inline void compareAndSwap(const pxr::GfVec3f& vec3) {
    replaceMin(&vec3[0]);
    replaceMax(&vec3[0]);
  }
  inline void compareAndSwap(const OriginCalc& other) {
    replaceMin(other.min);
    replaceMax(other.max);
  }
  inline pxr::GfVec3f calc() const {
    return (pxr::GfVec3f(&min[0]) + pxr::GfVec3f(&max[0])) / 2;
  }
private:
  static constexpr float fMax = std::numeric_limits<float>::max();
  float min[3] = { fMax, fMax, fMax};
  float max[3] = {-fMax,-fMax,-fMax};
  inline void replaceMin(const float* const vec3) {
    min[0] = std::min(min[0],vec3[0]);
    min[1] = std::min(min[1],vec3[1]);
    min[2] = std::min(min[2],vec3[2]);
  }
  inline void replaceMax(const float* const vec3) {
    max[0] = std::max(max[0],vec3[0]);
    max[1] = std::max(max[1],vec3[1]);
    max[2] = std::max(max[2],vec3[2]);
  }
  friend AtomicOriginCalc;
};

struct AtomicOriginCalc {
  inline void compareAndSwap(const pxr::GfVec3f& vec3) {
    replaceMin(&vec3[0]);
    replaceMax(&vec3[0]);
  }
  inline void compareAndSwap(const OriginCalc& other) {
    replaceMin(other.min);
    replaceMax(other.max);
  }
  inline pxr::GfVec3f calc() const {
    return (pxr::GfVec3f{min[0].load(),min[1].load(),min[2].load()} +
            pxr::GfVec3f{max[0].load(),max[1].load(),max[2].load()})
            / 2;
  }
  inline void reset() {
    for(size_t i = 0; i < 3; ++i) {
      min[i].store(fMax);
      max[i].store(-fMax);
    }
  }
private:
  static constexpr float fMax = std::numeric_limits<float>::max();
  std::atomic<float> min[3] = { fMax, fMax, fMax};
  std::atomic<float> max[3] = {-fMax,-fMax,-fMax};
  inline void replaceMin(const float* const vec3) {
    swapIfLess(min[0],vec3[0]);
    swapIfLess(min[1],vec3[1]);
    swapIfLess(min[2],vec3[2]);
  }
  inline void replaceMax(const float* const vec3) {
    swapIfGreater(max[0],vec3[0]);
    swapIfGreater(max[1],vec3[1]);
    swapIfGreater(max[2],vec3[2]);
  }
  static inline void swapIfLess(std::atomic<float>& atomic, const float other) {
    float val = fMax;
    do {
      val = atomic.load();
    } while(val > other && !atomic.compare_exchange_weak(val, other));
  }
  static void swapIfGreater(std::atomic<float>& atomic, const float other) {
    float val = -fMax;
    do {
      val = atomic.load();
    } while(val < other && !atomic.compare_exchange_weak(val, other));
  }
};

}
