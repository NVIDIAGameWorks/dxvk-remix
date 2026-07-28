/*
* Copyright (c) 2026, NVIDIA CORPORATION. All rights reserved.
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

#include <vector>

#include "rtx_instance_manager.h"
#include "rtx_objectpicking.h"

namespace dxvk {

  // Keep frame-local picking IDs and metadata registration in lockstep with preserved instances.
  template<typename PreserveInstanceFn, typename TrackObjectPickingMetaFn>
  bool preserveInstancesWithObjectPicking(
      const std::vector<PrimInstance>& prims,
      ObjectPickingValue objectPickingValue,
      PreserveInstanceFn&& preserveInstance,
      TrackObjectPickingMetaFn&& trackObjectPickingMeta) {
    bool hasInstance = false;

    for (const PrimInstance& prim : prims) {
      RtInstance* instance = prim.getInstance();
      if (instance == nullptr) {
        continue;
      }

      // Object-picking values are frame-local, including for instances whose geometry is preserved.
      instance->surface.objectPickingValue = objectPickingValue;
      preserveInstance(*instance);
      hasInstance = true;
    }

    if (hasInstance) {
      trackObjectPickingMeta();
    }

    return hasInstance;
  }

}
