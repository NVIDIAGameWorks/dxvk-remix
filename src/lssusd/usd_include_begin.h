/*
* Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.
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

// If `_DEBUG` is defined but empty (which is how Tracy defines it), the USD headers have errors.
// This breaks debug builds, but we can predefine TBB_USE_DEBUG to 1 to avoid this issue.
#ifdef _DEBUG
#define TBB_USE_DEBUG 1
#endif

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable:4003) // not enough args function-like macro
#pragma warning(disable:4244) // = Conversion from double to float / int to float
#pragma warning(disable:4305) // argument truncation from double to float
#pragma warning(disable:4800) // int to bool
#pragma warning(disable:4996) // call to std::copy with parameters that may be unsafe
#pragma warning(disable:4251) // struct 'std::atomic<T *>' needs to have dll-interface to be used by clients of class 
#pragma warning(disable:4201) // nonstandard extension used: nameless struct/union
#pragma warning(disable:4100) // unreferenced formal parameter
#pragma warning(disable:4127) // conditional expression is constant
#pragma warning(disable:4267) // because of USD headers: conversion from 'size_t' to 'type', possible loss of data
#pragma warning(disable:4275) // boost warning
#endif