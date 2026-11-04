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

// Returns true when the runtime can serve a client built against clientVersion.
// While the major version is 0 the minor version is the compatibility level and the patch version only appends,
// so a client may not be newer than the runtime. From 1.0 on, any client with an older or equal minor works.
// 0.6.3 inserted SetCameraMediumMaterial in the middle of the interface and 0.6.4 moved it to the end,
// so no runtime can serve a 0.6.3 client.
constexpr bool isClientVersionCompatible(uint64_t clientVersion, uint64_t runtimeVersion) {
  if (clientVersion == REMIXAPI_VERSION_MAKE(0, 6, 3)) {
    return false;
  }
  if (REMIXAPI_VERSION_GET_MAJOR(clientVersion) != REMIXAPI_VERSION_GET_MAJOR(runtimeVersion)) {
    return false;
  }
  if (REMIXAPI_VERSION_GET_MAJOR(runtimeVersion) == 0) {
    return REMIXAPI_VERSION_GET_MINOR(clientVersion) == REMIXAPI_VERSION_GET_MINOR(runtimeVersion) &&
           REMIXAPI_VERSION_GET_PATCH(clientVersion) <= REMIXAPI_VERSION_GET_PATCH(runtimeVersion);
  }
  return REMIXAPI_VERSION_GET_MINOR(clientVersion) <= REMIXAPI_VERSION_GET_MINOR(runtimeVersion);
}

// Returns the size in bytes of the remixapi_Interface prefix that a client of the given version allocates.
// Every version that appends a function pointer adds a branch here.
constexpr size_t clientInterfaceSize(uint64_t clientVersion) {
  if (clientVersion < REMIXAPI_VERSION_MAKE(0, 6, 4)) {
    return offsetof(remixapi_Interface, SetCameraMediumMaterial);
  }
  return sizeof(remixapi_Interface);
}

static_assert(sizeof(remixapi_Interface) == 22 * sizeof(void*), "Add a branch to clientInterfaceSize for the new interface");

}
