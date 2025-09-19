/*
 * Copyright (c) 2024, NVIDIA CORPORATION. All rights reserved.
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

#include <remixapi/bridge_remix_api.h>

#include "util_devicecommand.h"
#include "util_remixapi.h"

#include <mutex>

namespace remixapi {
  extern remixapi_Interface g_remix;
  extern bool g_remix_initialized;
  extern HMODULE g_remix_dll;
  extern IDirect3DDevice9Ex* g_device;
  extern std::mutex g_device_mutex;
  extern IDirect3DDevice9Ex* getDevice();
  
  static inline remixapi_StructType pullSType() {
    return (remixapi_StructType) DeviceBridge::get_data();
  }
  
  static inline bool pullBool() {
    const auto boolVal = (Bool)DeviceBridge::get_data();
    // Since standalone values are pushed/pulled at a DWORD resolution
    // we must mask out the lowest-most byte
    bool b = (bool)((uint32_t)boolVal & 0x00ff);
    return b;
  }
}
