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

#include <pxr/usd/sdf/path.h>
#include <pxr/usd/usd/tokens.h>

namespace lss {

const pxr::TfToken gTokLights("lights");
const pxr::TfToken gTokMesh("mesh");
const pxr::TfToken gTokSkel("skel");
const pxr::TfToken gTokPose("pose");
const pxr::TfToken gTokMeshes("meshes");
const pxr::TfToken gTokLooks("Looks");
const pxr::TfToken gTokRemixSettings("remix_settings");
const pxr::TfToken gTokScope("Scope");
const pxr::TfToken gTokInstances("instances");
const pxr::TfToken kTokCameras("cameras");
const pxr::TfToken gVisibilityInherited("inherited");
const pxr::TfToken gVisibilityInvisible("invisible");
const pxr::TfToken gZ("Z");
const pxr::TfToken gY("Y");

const pxr::SdfPath gStageRootPath("/");
const pxr::SdfPath gRootNodePath("/RootNode");
const pxr::SdfPath gRootLightsPath = gRootNodePath.AppendChild(gTokLights);
const pxr::SdfPath gRootMeshesPath = gRootNodePath.AppendChild(gTokMeshes);
const pxr::SdfPath gRootMaterialsPath = gRootNodePath.AppendChild(gTokLooks);
const pxr::SdfPath gRootInstancesPath = gRootNodePath.AppendChild(gTokInstances);
const pxr::SdfPath gRootLightCamera = gRootNodePath.AppendChild(kTokCameras);
}