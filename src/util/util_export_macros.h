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

// Note: Must match remix_c.h. Defined here to avoid plumbing down the heavy
// remix_c.h to the projects that may not have access to Remix SDK public headers.

#if defined(_WIN32)
  #ifndef REMIXAPI_CALL
    #define REMIXAPI_CALL __stdcall
  #endif

  #ifndef REMIXAPI
    // Note: always exporting from Remix runtime
    #define REMIXAPI __declspec(dllexport)
  #endif
#else
  #ifndef REMIXAPI_CALL
    #define REMIXAPI_CALL
  #endif

  #ifndef REMIXAPI
    #define REMIXAPI __attribute__((visibility("default")))
  #endif
#endif
