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

#include <detours.h>

#include "log/log.h"

#define API_HOOK_DECL(x) inline static decltype(x)* Orig##x = nullptr
#define API_ATTACH(x) error = DetourAttach(&(PVOID&)Orig##x, Hooked##x)
#define API_DETACH(x) DetourDetach(&(PVOID&)Orig##x, Hooked##x)


//////////////////////////////////
// Detours Boilerplate Wrappers //
//////////////////////////////////

// (1) DETOURS_DECL(<func_to_detour>)
// (2) DETOURS_ASSIGN_NEW_FUNC(<func_to_detour>,<your_new_impl>)
// (3) DETOURS_DECL_ASSERT(<func_to_detour>)
// (Or just use DETOURS_FUNC(<func_to_detour>,<your_new_impl>), which wraps all the above)
//
// DETOURS_ATTACH + DETOURS_DETACH requires boilerplate around, and must be called in a function after above:
//   DetourTransactionBegin();
//   DetourUpdateThread(GetCurrentThread());
//   DETOURS_ATTACH(<func_to_detour>) or DETOURS_DETACH(<func_to_detour>)
//   DetourTransactionCommit();

#define DETOURS_DECL(FuncName) \
  inline static decltype(FuncName)* Orig##FuncName = nullptr;

#define DETOURS_ASSIGN_NEW_FUNC(FuncName, NewFunc) \
  inline static decltype(FuncName)* Hooked##FuncName = NewFunc;

#define DETOURS_DECL_ASSERT(FuncName) \
  static_assert(std::is_same_v<decltype(Orig##FuncName), decltype(Hooked##FuncName)>);
  
#define DETOURS_FUNC(BaseFuncName, NewFunc) \
  DETOURS_DECL(BaseFuncName##); \
  DETOURS_ASSIGN_NEW_FUNC(BaseFuncName##, NewFunc); \
  DETOURS_DECL_ASSERT(BaseFuncName##);

#define DETOURS_ATTACH(FuncName) \
[&](){ \
  Orig##FuncName = FuncName; \
  const auto error = DetourAttach(&(PVOID&)Orig##FuncName, Hooked##FuncName); \
  if (error) { \
    bridge_util::Logger::err(bridge_util::format_string("[Detours] Unable to attach " #FuncName ": %d", error)); \
  } \
  return error == 0; \
}()

#define DETOURS_DETACH(FuncName) \
[&](){ \
  const auto error = DetourDetach(&(PVOID&)Orig##FuncName, Hooked##FuncName); \
  if (error) { \
    bridge_util::Logger::err(bridge_util::format_string("[Detours] Failed to detach " #FuncName ": %d", error)); \
  } \
  return error == 0; \
}()


//////////////////////////////////
// Unicode Convenience Wrappers //
//////////////////////////////////

// Same as above instructions, except with __UNICODE suffix

#define DETOURS_DECL__UNICODE(BaseFuncName) \
  DETOURS_DECL(BaseFuncName ## W); \
  DETOURS_DECL(BaseFuncName ## A);

#define DETOURS_ASSIGN_NEW_FUNC__UNICODE(BaseFuncName, WFunc, AFunc) \
  DETOURS_ASSIGN_NEW_FUNC(BaseFuncName ## W, WFunc); \
  DETOURS_ASSIGN_NEW_FUNC(BaseFuncName ## A, AFunc);

#define DETOURS_DECL_ASSERT__UNICODE(BaseFuncName) \
  DETOURS_DECL_ASSERT(BaseFuncName ## W); \
  DETOURS_DECL_ASSERT(BaseFuncName ## A);

#define DETOURS_FUNC__UNICODE(BaseFuncName, WFunc, AFunc) \
  DETOURS_FUNC(BaseFuncName ## W, WFunc); \
  DETOURS_FUNC(BaseFuncName ## A, AFunc);
  
#define DETOURS_ATTACH__UNICODE(BaseFuncName) \
  (DETOURS_ATTACH(BaseFuncName ## W) && DETOURS_ATTACH(BaseFuncName ## A))

#define DETOURS_DETACH__UNICODE(BaseFuncName) \
  (DETOURS_DETACH(BaseFuncName ## W) && DETOURS_DETACH(BaseFuncName ## A))
