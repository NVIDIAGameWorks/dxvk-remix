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

#include <stddef.h>
#include <stdint.h>
#include <string.h>

// The C header refuses to compile for 32-bit targets, but the bridge client only needs its types.
#if _WIN64 != 1
#define REMIX_ALLOW_X86
#endif
#include <remix/remix_c.h>
#if _WIN64 != 1
#undef REMIX_ALLOW_X86
#endif

// Version rules of the Remix API, shared by the runtime, the bridge client and the unit tests.
// See documentation/RemixApiChangelog.md for the rules every API change follows.
namespace remixapi_version {

constexpr uint64_t kVersion063 = REMIXAPI_VERSION_MAKE(0, 6, 3);

// Holds the interface layout of 0.6.3, the only version that is not a prefix of the current one.
// 0.6.4 moved SetCameraMediumMaterial from after SetupCamera to the end.
struct remixapi_Interface_063 {
  PFN_remixapi_Shutdown                   Shutdown;
  PFN_remixapi_CreateMaterial             CreateMaterial;
  PFN_remixapi_DestroyMaterial            DestroyMaterial;
  PFN_remixapi_CreateMesh                 CreateMesh;
  PFN_remixapi_DestroyMesh                DestroyMesh;
  PFN_remixapi_SetupCamera                SetupCamera;
  PFN_remixapi_SetCameraMediumMaterial    SetCameraMediumMaterial;
  PFN_remixapi_DrawInstance               DrawInstance;
  PFN_remixapi_CreateLight                CreateLight;
  PFN_remixapi_DestroyLight               DestroyLight;
  PFN_remixapi_DrawLightInstance          DrawLightInstance;
  PFN_remixapi_SetConfigVariable          SetConfigVariable;
  PFN_remixapi_dxvk_CreateD3D9            dxvk_CreateD3D9;
  PFN_remixapi_dxvk_RegisterD3D9Device    dxvk_RegisterD3D9Device;
  PFN_remixapi_dxvk_GetExternalSwapchain  dxvk_GetExternalSwapchain;
  PFN_remixapi_dxvk_GetVkImage            dxvk_GetVkImage;
  PFN_remixapi_dxvk_CopyRenderingOutput   dxvk_CopyRenderingOutput;
  PFN_remixapi_dxvk_SetDefaultOutput      dxvk_SetDefaultOutput;
  PFN_remixapi_pick_RequestObjectPicking  pick_RequestObjectPicking;
  PFN_remixapi_pick_HighlightObjects      pick_HighlightObjects;
  PFN_remixapi_Startup                    Startup;
  PFN_remixapi_Present                    Present;
};

// Returns true when the runtime can serve a client built against clientVersion.
// While the major version is 0, any client with the same minor works, because patches only append.
// From 1.0 on, any client with an older or equal minor works.
constexpr bool isClientVersionCompatible(uint64_t clientVersion, uint64_t runtimeVersion) {
  if (REMIXAPI_VERSION_GET_MAJOR(clientVersion) != REMIXAPI_VERSION_GET_MAJOR(runtimeVersion)) {
    return false;
  }
  if (REMIXAPI_VERSION_GET_MAJOR(runtimeVersion) == 0) {
    return REMIXAPI_VERSION_GET_MINOR(clientVersion) == REMIXAPI_VERSION_GET_MINOR(runtimeVersion);
  }
  return REMIXAPI_VERSION_GET_MINOR(clientVersion) <= REMIXAPI_VERSION_GET_MINOR(runtimeVersion);
}

// Returns the size in bytes of the interface that a client of the given version allocates.
// Every version that appends a function pointer adds a branch here.
// A client newer than the runtime gets the runtime's whole interface, and the functions that the runtime lacks stay null.
constexpr size_t clientInterfaceSize(uint64_t clientVersion) {
  if (clientVersion == kVersion063) {
    return sizeof(remixapi_Interface_063);
  }
  if (clientVersion < REMIXAPI_VERSION_MAKE(0, 6, 4)) {
    return offsetof(remixapi_Interface, SetCameraMediumMaterial);
  }
  if (clientVersion < REMIXAPI_VERSION_MAKE(0, 6, 11)) {
    return offsetof(remixapi_Interface, SetWaterSim);
  }
  return sizeof(remixapi_Interface);
}

static_assert(sizeof(remixapi_Interface) == 24 * sizeof(void*), "Add a branch to clientInterfaceSize for the new interface");

// Writes only the part of the interface that the client's version knows, in that version's order.
inline void writeClientInterface(const remixapi_Interface& interf, uint64_t clientVersion, remixapi_Interface* pOut) {
  if (clientVersion == kVersion063) {
    remixapi_Interface_063 interf063 = {};
    interf063.Shutdown = interf.Shutdown;
    interf063.CreateMaterial = interf.CreateMaterial;
    interf063.DestroyMaterial = interf.DestroyMaterial;
    interf063.CreateMesh = interf.CreateMesh;
    interf063.DestroyMesh = interf.DestroyMesh;
    interf063.SetupCamera = interf.SetupCamera;
    interf063.SetCameraMediumMaterial = interf.SetCameraMediumMaterial;
    interf063.DrawInstance = interf.DrawInstance;
    interf063.CreateLight = interf.CreateLight;
    interf063.DestroyLight = interf.DestroyLight;
    interf063.DrawLightInstance = interf.DrawLightInstance;
    interf063.SetConfigVariable = interf.SetConfigVariable;
    interf063.dxvk_CreateD3D9 = interf.dxvk_CreateD3D9;
    interf063.dxvk_RegisterD3D9Device = interf.dxvk_RegisterD3D9Device;
    interf063.dxvk_GetExternalSwapchain = interf.dxvk_GetExternalSwapchain;
    interf063.dxvk_GetVkImage = interf.dxvk_GetVkImage;
    interf063.dxvk_CopyRenderingOutput = interf.dxvk_CopyRenderingOutput;
    interf063.dxvk_SetDefaultOutput = interf.dxvk_SetDefaultOutput;
    interf063.pick_RequestObjectPicking = interf.pick_RequestObjectPicking;
    interf063.pick_HighlightObjects = interf.pick_HighlightObjects;
    interf063.Startup = interf.Startup;
    interf063.Present = interf.Present;
    memcpy(pOut, &interf063, sizeof(interf063));
    return;
  }
  memcpy(pOut, &interf, clientInterfaceSize(clientVersion));
}

}
