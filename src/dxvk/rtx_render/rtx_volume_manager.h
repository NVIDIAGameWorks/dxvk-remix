/*
* Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.
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

#include <unordered_map>
#include "../util/rc/util_rc_ptr.h"
#include "rtx_types.h"
#include "rtx/pass/volume_args.h"
#include "rtx/utility/shader_types.h"

struct RaytraceArgs;

namespace dxvk 
{
class RtxContext;
class DxvkDevice;

struct VolumeManager : public CommonDeviceObject {
public:
  VolumeManager(VolumeManager const&) = delete;
  VolumeManager& operator=(VolumeManager const&) = delete;

  VolumeManager(DxvkDevice* device);
  ~VolumeManager();

  VolumeArgs getVolumeArgs(CameraManager const& cameraManager, VkExtent3D froxelGridDimensions, uint32_t numFroxelVolumes, FogState const& fogState, bool enablePortalVolumes) const;
};

}  // namespace dxvk

