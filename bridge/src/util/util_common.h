/*
 * Copyright (c) 2022-2023, NVIDIA CORPORATION. All rights reserved.
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
#ifndef UTIL_COMMON_H_
#define UTIL_COMMON_H_

#include <stdint.h>
#include <type_traits>

// This setting enables sending lock data row by row instead of one big data
// chunk at once.Note that volumes will still be sent one slice at a time.
#define SEND_ALL_LOCK_DATA_AT_ONCE

// The next two settings enable and configure logging of server calls that take
// longer to process than the given timeout value in milliseconds. Useful for
// troubleshooting issues with calls that trigger a timeout or cause the
// server to fall behind with processing the command queue.
#ifdef _DEBUG
#define LOG_SERVER_COMMAND_TIME
#endif
#define SERVER_COMMAND_THRESHOLD_MS 500

// This enables extra log calls for the present semaphore acquisition on
// the client and release on the server. Helpful for troubleshooting deadlock
// issues caused by this semaphore, but due to high call volume will tank
// performance and should be used with caution!
//#define ENABLE_PRESENT_SEMAPHORE_TRACE

// This enables extra log calls when data batching is enabled via the global
// config settings. Useful for troubleshooting issues with data batching, but
// due to high call volume will tank performance and should be used with caution!
//#define ENABLE_DATA_BATCHING_TRACE

// This enables extra log calls inside the waitForCommand() logic, useful for
// troubleshooting issues with the startup handshake or during regular command
// processing. Due to high call volume this will likely have a significant impact
// on performance and should only be used if needed!
//#define ENABLE_WAIT_FOR_COMMAND_TRACE

// This enables the alternate circular queue implementation that uses semaphores
// for synchronization, and performed slower in testing than the implementation
// using atomic counters. There shouldn't be any need to use this, but leaving
// it in the code for now just in case.
//#define USE_BLOCKING_QUEUE

#ifdef WIN32 
#ifdef _DEBUG
// Enable our hack to get D3D error messages from the runtime.
// Note: Only tested on Windows 10 [d3d9.dll version: 10.0.19041.1387 (WinBuild.160101.0800)],
//       may require changes to work on a newer Windows 10 or Windows 11 build.
// #define HACK_D3D_DEBUG_MSG
#endif
#endif

#ifndef _WIN64
#undef REMIX_BRIDGE_CLIENT
#define REMIX_BRIDGE_CLIENT
#else
#undef REMIX_BRIDGE_SERVER
#define REMIX_BRIDGE_SERVER
#endif

#if defined(__GNUC__)
#define FORCEINLINE __attribute__((always_inline)) inline
#elif defined(_MSC_VER)
#define FORCEINLINE __forceinline
#else
#define FORCEINLINE inline
#endif

namespace bridge_util {

  enum class Result {
    Success, // Action success
    Timeout, // Timeout failure
    Failure // Other failure
  };

  template<typename T>
  static constexpr T align(T v, T a) {
    static_assert(std::is_integral_v<T>, "Cannot align a value of non-integral type.");
    return (v + a - 1) & ~(a - 1);
  }

  struct NonCopyable {
    NonCopyable(const NonCopyable&) = delete;
    NonCopyable& operator=(const NonCopyable&) = delete;
    NonCopyable() = default;
  };
}

#define RESULT_SUCCESS(result) (result == bridge_util::Result::Success)
#define RESULT_FAILURE(result) (result != bridge_util::Result::Success)

namespace caps {
  static const uint32_t MaxClipPlanes = 6;
  static const uint32_t MaxSamplers = 16;
  static const uint32_t MaxStreams = 16;
  static const uint32_t MaxSimultaneousTextures = 8;
  static const uint32_t MaxTextureBlendStages = MaxSimultaneousTextures;
  static const uint32_t MaxSimultaneousRenderTargets = 4;

  static const uint32_t MaxFloatConstantsVS = 256;
  static const uint32_t MaxFloatConstantsPS = 224;
  static const uint32_t MaxOtherConstants = 16;
  static const uint32_t MaxFloatConstantsSoftware = 8192;
  static const uint32_t MaxOtherConstantsSoftware = 2048;

  static const uint32_t InputRegisterCount = 16;

  static const uint32_t MaxTextureDimension = 16384;
  static const uint32_t MaxMipLevels = 15;
  static const uint32_t MaxCubeFaces = 6;
  static const uint32_t MaxSubresources = MaxMipLevels * MaxCubeFaces;

  static const uint32_t MaxTransforms = 10 + 256;

  static const uint32_t TextureStageCount = MaxSimultaneousTextures;

  static const uint32_t MaxEnabledLights = 8;

  static const uint32_t MaxTexturesVS = 4;

  static const uint32_t MaxTexturesPS = 16;

  static const uint32_t MinSurfacePitch = sizeof(size_t);
}

// Dynamic cast has RTTI checks - which are more expensive at runtime (hence debug only) but much better at catching bugs
#if NDEBUG
#define bridge_cast static_cast
#else
#define bridge_cast dynamic_cast
#endif

using UID = size_t;

template<typename Dest, typename Source>
typename std::enable_if_t<(sizeof(Dest) == sizeof(Source)), Dest> bit_cast(const Source& source) noexcept {
  Dest dest;
  std::memcpy(&dest, &source, sizeof(dest));
  return dest;
}

#endif // UTIL_COMMON_H_
